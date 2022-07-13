#include <mpi.h>

#include <map>
#include <pthread.h>
#include <semaphore.h>

#include "jassert.h"
#include "seq_num.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"
#include "record-replay.h"

using namespace dmtcp_mpi;

#define DB_CONVERGED 1

#define DEBUG_SEQ_NUM

extern int g_world_rank;
extern int g_world_size;
extern int p2p_deterministic_skip_save_request;
volatile bool ckpt_pending;
volatile bool freepass;
volatile phase_t current_phase;
unsigned int current_global_comm_id;

pthread_rwlock_t seq_num_lock;
sem_t user_thread_sem;
sem_t ckpt_thread_sem;

std::map<unsigned int, unsigned int> seq_num;
std::map<unsigned int, unsigned int> target_seq_num;
typedef std::pair<unsigned int, unsigned int> comm_seq_pair_t;

void seq_num_init() {
  ckpt_pending = false;
  freepass = false;
  pthread_rwlock_init(&seq_num_lock, NULL);
  sem_init(&user_thread_sem, 0, 0);
  sem_init(&ckpt_thread_sem, 0, 0);
}

void seq_num_reset() {
  ckpt_pending = false;
  freepass = false;
  for (comm_seq_pair_t i : seq_num) {
    seq_num[i.first] = 0;
    target_seq_num[i.first] = 0;
    dmtcp_kvdb64(DMTCP_KVDB_SET, "/mana/comm-seq-max", i.first, 0);
  }
}

void seq_num_destroy() {
  pthread_rwlock_destroy(&seq_num_lock);
  sem_destroy(&user_thread_sem);
  sem_destroy(&ckpt_thread_sem);
}

int check_seq_nums() {
  unsigned int comm_id;
  unsigned int seq;
  int target_reached = 1;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    seq = pair.second;
    if (target_seq_num[comm_id] > seq_num[comm_id]) {
      target_reached = 0;
    }
  }
  return target_reached;
}

int twoPhaseCommit(MPI_Comm comm, 
                   std::function<int(void)>doRealCollectiveComm) {
  if (!MPI_LOGGING() || comm == MPI_COMM_NULL) {
    return doRealCollectiveComm(); // lambda function: already captured args
  }

  if (!MPI_LOGGING()) {
    return doRealCollectiveComm();
  }

  commit_begin(comm);
  int retval = doRealCollectiveComm();
  commit_finish();
  return retval;
}

int comm_target_reached(unsigned int global_comm) {
  return seq_num[current_global_comm_id] >
         target_seq_num[current_global_comm_id];
}

void commit_begin(MPI_Comm comm) {
  pthread_rwlock_rdlock(&seq_num_lock);
  current_global_comm_id = VirtualGlobalCommId::instance().getGlobalId(comm);
  seq_num[current_global_comm_id]++;
  pthread_rwlock_unlock(&seq_num_lock);
  if (ckpt_pending) {
    // If the sequence number of this communicator has passed the
    // target sequence number, switch to the trivial barrier algorithm.
    if (comm_target_reached(current_global_comm_id)) {
      // Call the trivial barrier
      DMTCP_PLUGIN_DISABLE_CKPT();
      current_phase = IN_TRIVIAL_BARRIER;
      // Set state again incase we returned from beforeTrivialBarrier
      MPI_Request request;
      MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
      int flag = 0;
      JUMP_TO_LOWER_HALF(lh_info.fsaddr);
      NEXT_FUNC(Ibarrier)(realComm, &request);
      RETURN_TO_UPPER_HALF();
      DMTCP_PLUGIN_ENABLE_CKPT();

      // Wait on the trivial barrier.
      p2p_deterministic_skip_save_request = 1;
      while (!flag) {
        DMTCP_PLUGIN_DISABLE_CKPT();
        // If ckpt_pending is false, it means we are back from restart.
        // In this case, re-enable ckpt and break from this test loop.
        if (!ckpt_pending) {
          DMTCP_PLUGIN_ENABLE_CKPT();
          break;
        }
        JUMP_TO_LOWER_HALF(lh_info.fsaddr);
        NEXT_FUNC(Test)(&request, &flag, MPI_STATUS_IGNORE);
        RETURN_TO_UPPER_HALF();
        DMTCP_PLUGIN_ENABLE_CKPT();
      }
      p2p_deterministic_skip_save_request = 0;

      // Stop before the critical section during checkpoint time.
      if (ckpt_pending && check_seq_nums()) {
        current_phase = STOP_BEFORE_CS;
        while (!freepass && ckpt_pending);
        freepass = false;
        current_phase = IN_CS;
      }
    }
  }
  current_phase = IN_CS;
}

void commit_finish() {
  pthread_rwlock_rdlock(&seq_num_lock);
  current_phase = IS_READY;
  current_global_comm_id = MPI_COMM_NULL;
  pthread_rwlock_unlock(&seq_num_lock);
}

void upload_seq_num() {
  unsigned int comm_id;
  unsigned int seq;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    seq = pair.second;
    dmtcp_kvdb64(DMTCP_KVDB_MAX, "/mana/comm-seq-max", comm_id, seq);
  }
}

void download_targets() {
  int64_t max_seq = 0;
  unsigned int comm_id;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    dmtcp_kvdb64_get("/mana/comm-seq-max", comm_id, &max_seq);
    target_seq_num[comm_id] = max_seq;
  }
}

void share_seq_nums() {
  pthread_rwlock_wrlock(&seq_num_lock);
  ckpt_pending = true;
  upload_seq_num();
  dmtcp_global_barrier("mana/comm-seq-round");
  download_targets();
  pthread_rwlock_unlock(&seq_num_lock);
}


rank_state_t preSuspendBarrier(query_t query) {
  switch (query) {
    case INTENT:
      share_seq_nums();
      // Set the ckpt_pending after sharing sequence numbers.
      // Otherwise, the user thread can enter the trivial barrier
      // before target_seq_num are properly updated.
      ckpt_pending = true;
      break;
    case FREE_PASS:
      freepass = true;
      while (current_phase == STOP_BEFORE_CS);
      break;
    case WAIT_STRAGGLER:
      // Maybe some peers in critical section and some in PHASE_1 or
      // IN_TRIVIAL_BARRIER. This may happen if a checkpoint pending is
      // announced after some member already entered the critical
      // section. Then the coordinator will send
      // WAIT_STRAGGLER to those ranks in the critical section.
      // In this case, we just report back the current state.
      // But the user thread can continue running and enter IS_READY,
      // IN_TRIVIAL_BARRIER for a different communication, etc.
      break;
    default:
      JWARNING(false)(query).Text("Unknown query from coordinator");
      break;
  }
  pthread_rwlock_wrlock(&seq_num_lock);
  // maintain consistent view for DMTCP coordinator
  rank_state_t state = { .rank = g_world_rank,
                         .comm = current_global_comm_id,
                         .st = current_phase};
  pthread_rwlock_unlock(&seq_num_lock);
  return state;
}
