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

// #define DEBUG_SEQ_NUM

extern int g_world_rank;
extern int g_world_size;
extern int p2p_deterministic_skip_save_request;
volatile bool ckpt_pending;
volatile bool freepass;
volatile phase_t current_phase;
unsigned int current_global_comm_id;
reset_type_t reset_type;

pthread_mutex_t seq_num_lock;
sem_t user_thread_sem;
sem_t ckpt_thread_sem;

std::map<unsigned int, unsigned long> seq_num;
std::map<unsigned int, unsigned long> target_start_triv_barrier;
std::map<unsigned int, unsigned long> target_stop_triv_barrier;
typedef std::pair<unsigned int, unsigned long> comm_seq_pair_t;

int comm_ckpt_target_reached(unsigned int global_comm) {
  return seq_num[global_comm] > target_start_triv_barrier[global_comm];
}

// For restart, we have a fresh lower half which does not have pending
// MPI_Ibarrier's. So don't need to add trivial barriers after restart.
int comm_resume_target_reached(unsigned int global_comm) {
  return seq_num[global_comm] > target_stop_triv_barrier[global_comm];
}

void seq_num_init() {
  ckpt_pending = false;
  freepass = false;
  pthread_mutex_init(&seq_num_lock, NULL);
  sem_init(&user_thread_sem, 0, 0);
  sem_init(&ckpt_thread_sem, 0, 0);
}

void seq_num_reset(reset_type_t type) {
  ckpt_pending = false;
  freepass = false;
  reset_type = type;
  if (type == RESUME) {
    share_seq_nums(target_stop_triv_barrier);
#ifdef DEBUG_SEQ_NUM
    printf("rank %d resumed\n",
           g_world_rank);
    fflush(stdout);
  } else {
    printf("rank %d restarted\n", g_world_rank);
    fflush(stdout);
#endif
  }
}

void seq_num_destroy() {
  pthread_mutex_destroy(&seq_num_lock);
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
    if (target_start_triv_barrier[comm_id] > seq_num[comm_id]) {
      target_reached = 0;
      break;
    }
  }
  return target_reached;
}

int twoPhaseCommit(MPI_Comm comm,
                   std::function<int(void)>doRealCollectiveComm) {
  if (!MPI_LOGGING() || comm == MPI_COMM_NULL) {
    return doRealCollectiveComm(); // lambda function: already captured args
  }

  commit_begin(comm);
  int retval = doRealCollectiveComm();
  commit_finish();
  return retval;
}

void commit_begin(MPI_Comm comm) {
  if (mana_state == RESTART_REPLAY || comm == MPI_COMM_NULL) {
    return;
  }
  pthread_mutex_lock(&seq_num_lock);
  current_global_comm_id = VirtualGlobalCommId::instance().getGlobalId(comm);
  seq_num[current_global_comm_id]++;
  pthread_mutex_unlock(&seq_num_lock);
  // There are two cases we need to insert a trivial barrier:
  // 1. If the sequence number of this communicator has passed the
  //    target_start_triv_barrier
  // 2. If it's in the resume mode, and the current sequence number is smaller
  //    or equal to the target_stop_triv_barrier.
  if ((ckpt_pending && comm_ckpt_target_reached(current_global_comm_id)) ||
      (!comm_resume_target_reached(current_global_comm_id))) {
#ifdef DEBUG_SEQ_NUM
    if (!comm_resume_target_reached(current_global_comm_id)) {
      printf("rank %d starts trivial barrier with comm %u seq num %lu after resume.\n",
            g_world_rank, current_global_comm_id,
            seq_num[current_global_comm_id]);
      fflush(stdout);
    } else {
      printf("rank %d starts trivial barrier with comm %u seq num %lu.\n",
            g_world_rank, current_global_comm_id,
            seq_num[current_global_comm_id]);
      fflush(stdout);
    }
#endif
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
      // If ckpt_pending is false, and no communicator is in the trivial barrier
      // mode, we are back from restart. In this case, re-enable ckpt and break
      // from this test loop.
      if (!ckpt_pending && reset_type == RESTART) {
#ifdef DEBUG_SEQ_NUM
        printf("rank %d returned from trivial barrier because of restart\n",
               g_world_rank);
        fflush(stdout);
#endif
        DMTCP_PLUGIN_ENABLE_CKPT();
        break;
      }
      JUMP_TO_LOWER_HALF(lh_info.fsaddr);
      NEXT_FUNC(Test)(&request, &flag, MPI_STATUS_IGNORE);
      RETURN_TO_UPPER_HALF();
      DMTCP_PLUGIN_ENABLE_CKPT();
    }
#ifdef DEBUG_SEQ_NUM
    printf("rank %d finished trivial barrier\n", g_world_rank);
    fflush(stdout);
#endif
    p2p_deterministic_skip_save_request = 0;

    // Stop before the critical section during checkpoint time.
    if (ckpt_pending && check_seq_nums()) {
      current_phase = STOP_BEFORE_CS;
      while (!freepass && ckpt_pending);
      freepass = false;
      current_phase = IN_CS;
    }
  }
  current_phase = IN_CS;
}

void commit_finish() {
  if (mana_state != RUNNING) {
    return;
  }
  pthread_mutex_lock(&seq_num_lock);
  current_phase = IS_READY;
  current_global_comm_id = MPI_COMM_NULL;
  pthread_mutex_unlock(&seq_num_lock);
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

void download_targets(std::map<unsigned int, unsigned long> &target) {
  int64_t max_seq = 0;
  unsigned int comm_id;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    dmtcp_kvdb64_get("/mana/comm-seq-max", comm_id, &max_seq);
    target[comm_id] = max_seq;
  }
}

void share_seq_nums(std::map<unsigned int, unsigned long> &target) {
  pthread_mutex_lock(&seq_num_lock);
  upload_seq_num();
  dmtcp_global_barrier("mana/comm-seq-round");
  download_targets(target);
  pthread_mutex_unlock(&seq_num_lock);
}


rank_state_t preSuspendBarrier(query_t query) {
  switch (query) {
    case INTENT:
      share_seq_nums(target_start_triv_barrier);
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
  pthread_mutex_lock(&seq_num_lock);
  // maintain consistent view for DMTCP coordinator
  rank_state_t state = { .rank = g_world_rank,
                         .comm = current_global_comm_id,
                         .st = current_phase};
  pthread_mutex_unlock(&seq_num_lock);
  return state;
}
