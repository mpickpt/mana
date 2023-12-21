#include <mpi.h>

#include <map>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "jassert.h"
#include "kvdb.h"
#include "seq_num.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"
#include "record-replay.h"

using namespace dmtcp_mpi;
using dmtcp::kvdb::KVDBRequest;
using dmtcp::kvdb::KVDBResponse;

// #define DEBUG_SEQ_NUM

constexpr int MAX_DRAIN_ROUNDS = 200;

extern int g_world_rank;
extern int g_world_size;
// Global communicator for MANA internal use
MPI_Comm g_world_comm;
extern int p2p_deterministic_skip_save_request;
volatile bool ckpt_pending;
int converged;
volatile phase_t current_phase;
unsigned int comm_gid;
int num_converged;
reset_type_t reset_type;

pthread_mutex_t seq_num_lock;
sem_t user_thread_sem;
sem_t ckpt_thread_sem;

/* Freepass implemented using semaphore to avoid the following situation:
*  T1: current_phase = STOP_BEFORE_CS;
*  T2: freepass = true;
*      while (current_phase == STOP_BEFORE_CS);
*  T1: while (!freepass && ckpt_pending);
*      freepass = false;
*      current_phase = IN_CS;
*      ....
*      current_phase = STOP_BEFORE_CS;
*
*  Note that this situation can still occur with only a semaphore to
*  implement the freepass. So another semaphore is required to
*  enforce that the thread in commit_begin cannot enter the state
*  STOP_BEFORE_CS until we verify that the thread giving the free pass
*  has checked the thread state
*/
sem_t freepass_sem;
sem_t freepass_sync_sem;

std::map<unsigned int, unsigned long> seq_num;
std::map<unsigned int, unsigned long> target;
typedef std::pair<unsigned int, unsigned long> comm_seq_pair_t;

void seq_num_init() {
  ckpt_pending = false;
  pthread_mutex_init(&seq_num_lock, NULL);
  sem_init(&user_thread_sem, 0, 0);
  sem_init(&ckpt_thread_sem, 0, 0);
  sem_init(&freepass_sem, 0, 0);
  sem_init(&freepass_sync_sem, 0, 0);
  sem_post(&freepass_sync_sem);
}

void seq_num_reset(reset_type_t type) {
  ckpt_pending = false;
}

void seq_num_destroy() {
  pthread_mutex_destroy(&seq_num_lock);
  sem_destroy(&user_thread_sem);
  sem_destroy(&ckpt_thread_sem);
  sem_destroy(&freepass_sem);
  sem_destroy(&freepass_sync_sem);
}

int print_seq_nums() {
  unsigned int comm_id;
  unsigned long seq;
  int target_reached = 1;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    seq = pair.second;
    printf("%d, %u, %lu\n", g_world_rank, comm_id, seq);
  }
  fflush(stdout);
  return target_reached;
}

int check_seq_nums(bool exclusive) {
  unsigned int comm_id;
  int target_reached = 1;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    if (exclusive) {
      if (target[comm_id] + 1 > seq_num[comm_id]) {
        target_reached = 0;
        break;
      }
    } else {
      if (target[comm_id] > seq_num[comm_id]) {
        target_reached = 0;
        break;
      }
    }
  }
  return target_reached;
}

int twoPhaseCommit(MPI_Comm comm,
                   std::function<int(void)>doRealCollectiveComm) {
  if (!MPI_LOGGING() || comm == MPI_COMM_NULL) {
    return doRealCollectiveComm(); // lambda function: already captured args
  }

  commit_begin(comm, false);
  int retval = doRealCollectiveComm();
  commit_finish(comm, false);
  return retval;
}

void seq_num_broadcast(MPI_Comm comm, unsigned long new_target) {
  unsigned int comm_gid = VirtualGlobalCommId::instance().getGlobalId(comm);
  unsigned long msg[2] = {comm_gid, new_target};
  int comm_size;
  int comm_rank;
  int world_rank;
  MPI_Comm_size(comm, &comm_size);
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Group world_group, local_group;
  MPI_Comm real_local_comm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Comm real_world_comm = VIRTUAL_TO_REAL_COMM(g_world_comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(real_world_comm, &world_group);
  NEXT_FUNC(Comm_group)(real_local_comm, &local_group);
  RETURN_TO_UPPER_HALF();
  for (int i = 0; i < comm_size; i++) {
    if (i != comm_rank) {
      JUMP_TO_LOWER_HALF(lh_info.fsaddr);
      NEXT_FUNC(Group_translate_ranks)(local_group, 1, &i,
                world_group, &world_rank);
      NEXT_FUNC(Send)(&msg, 2, MPI_UNSIGNED_LONG, world_rank,
                      0, real_world_comm);
      RETURN_TO_UPPER_HALF();
#ifdef DEBUG_SEQ_NUM
      printf("rank %d sending to rank %d new target comm %u seq %lu target %lu\n",
             g_world_rank, world_rank, comm_gid, seq_num[comm_gid], new_target);
      fflush(stdout);
#endif
    }
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_free)(&world_group);
  NEXT_FUNC(Group_free)(&local_group);
  RETURN_TO_UPPER_HALF();
}

void commit_begin(MPI_Comm comm, bool passthrough) {
  if (mana_state == RESTART_REPLAY || comm == MPI_COMM_NULL) {
    return;
  }
  while (ckpt_pending && check_seq_nums(passthrough)) {
    MPI_Status status;
    int flag;
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, g_world_comm, &flag, &status);
    if (flag) {
      unsigned long new_target[2];
      MPI_Comm real_world_comm = VIRTUAL_TO_REAL_COMM(g_world_comm);
      JUMP_TO_LOWER_HALF(lh_info.fsaddr);
      NEXT_FUNC(Recv)(&new_target, 2, MPI_UNSIGNED_LONG,
          status.MPI_SOURCE, status.MPI_TAG, real_world_comm,
          MPI_STATUS_IGNORE);
      RETURN_TO_UPPER_HALF();
      unsigned int updated_comm = (unsigned int) new_target[0];
      unsigned long updated_target = new_target[1];
      std::map<unsigned int, unsigned long>::iterator it =
        target.find(updated_comm);
      if (it != target.end() && it->second < updated_target) {
        target[updated_comm] = updated_target;
#ifdef DEBUG_SEQ_NUM
        printf("rank %d received new target comm %u seq %lu target %lu\n",
            g_world_rank, updated_comm, seq_num[updated_comm], updated_target);
        fflush(stdout);
#endif
      }
    }
  }
  pthread_mutex_lock(&seq_num_lock);
  current_phase = IN_CS;
  unsigned int comm_gid = VirtualGlobalCommId::instance().getGlobalId(comm);
  seq_num[comm_gid]++;
  pthread_mutex_unlock(&seq_num_lock);
#ifdef DEBUG_SEQ_NUM
  // print_seq_nums();
#endif
  if (ckpt_pending && seq_num[comm_gid] > target[comm_gid]) {
    target[comm_gid] = seq_num[comm_gid];
    seq_num_broadcast(comm, seq_num[comm_gid]);
  }
}

void commit_finish(MPI_Comm comm, bool passthrough) {
  if (mana_state == RESTART_REPLAY) {
    return;
  }
  current_phase = IS_READY;
  if (passthrough) {
    return;
  }
  while (ckpt_pending && check_seq_nums(false)) {
    MPI_Status status;
    int flag;
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, g_world_comm, &flag, &status);
    if (flag) {
      unsigned long new_target[2];
      MPI_Comm real_world_comm = VIRTUAL_TO_REAL_COMM(g_world_comm);
      JUMP_TO_LOWER_HALF(lh_info.fsaddr);
      NEXT_FUNC(Recv)(&new_target, 2, MPI_UNSIGNED_LONG,
          status.MPI_SOURCE, status.MPI_TAG, real_world_comm,
          MPI_STATUS_IGNORE);
      RETURN_TO_UPPER_HALF();
      unsigned int updated_comm = (unsigned int) new_target[0];
      unsigned long updated_target = new_target[1];
      std::map<unsigned int, unsigned long>::iterator it =
        target.find(updated_comm);
      if (it != target.end() && it->second < updated_target) {
        target[updated_comm] = updated_target;
#ifdef DEBUG_SEQ_NUM
        printf("rank %d received new target comm %u seq %lu target %lu\n",
            g_world_rank, updated_comm, seq_num[updated_comm], updated_target);
        fflush(stdout);
#endif
      }
    }
  }
}

void
upload_seq_num(const char *db)
{
  for (comm_seq_pair_t pair : seq_num) {
    dmtcp::string comm_id_str(jalib::XToString(pair.first));
    unsigned int seq = pair.second;
    JASSERT(dmtcp::kvdb::request64(KVDBRequest::MAX, db, comm_id_str, seq) ==
            KVDBResponse::SUCCESS);
  }
}

void
download_targets(const char *db)
{
  int64_t max_seq = 0;
  unsigned int comm_id;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    dmtcp::string comm_id_str(jalib::XToString(pair.first));
    JASSERT(dmtcp::kvdb::get64(db, comm_id_str, &max_seq) ==
            KVDBResponse::SUCCESS);
    target[comm_id] = max_seq;
  }
}

void
share_seq_nums(int attemptId)
{
  char db[64] = { 0 };
  char barrier[64] = { 0 };
  snprintf(db, 63, "/plugin/MANA/comm-seq-max-%06d", attemptId);
  snprintf(barrier, 63, "MANA-SHARE-SEQ-NUM-%06d", attemptId);

  upload_seq_num(db);
  dmtcp_global_barrier(barrier);
  download_targets(db);
}

static bool
try_drain_mpi_collective(int attemptId)
{
  int round_num = 0;
  int64_t num_converged = 0;
  int64_t in_cs = 0;

  for (int i = 0; i < MAX_DRAIN_ROUNDS; i++) {
    char key[64] = { 0 };
    char barrier_id[64] = { 0 };
    char cs_id[64] = { 0 };
    char converge_id[64] = { 0 };

    snprintf(cs_id, 63, "/plugin/MANA/CRITICAL-SECTION-%06d", attemptId);
    snprintf(converge_id, 63, "/plugin/MANA/CONVERGE-%06d", attemptId);
    snprintf(barrier_id, 63, "MANA-PRESUSPEND-%06d-%06d", attemptId, round_num);
    snprintf(key, 63, "round-%06d", round_num);

    JASSERT(dmtcp::kvdb::request64(KVDBRequest::INCRBY, converge_id, key,
                                   check_seq_nums(false)) ==
            KVDBResponse::SUCCESS);
    JASSERT(dmtcp::kvdb::request64(KVDBRequest::OR, cs_id, key,
                                   current_phase == IN_CS) ==
            KVDBResponse::SUCCESS);

    dmtcp_global_barrier(barrier_id);

    JASSERT(dmtcp::kvdb::get64(converge_id, key, &num_converged) ==
            KVDBResponse::SUCCESS);
    JASSERT(dmtcp::kvdb::get64(cs_id, key, &in_cs) == KVDBResponse::SUCCESS);

    if (in_cs == 0 && num_converged == g_world_size) {
      return true;
    }

    round_num++;
  }

  return false;
}

void
drain_mpi_collective()
{
  int attemptId = 0;

  while (true) {
    // Publish our seq_num to kvdb.
    pthread_mutex_lock(&seq_num_lock);
    ckpt_pending = true;
    share_seq_nums(attemptId);
    pthread_mutex_unlock(&seq_num_lock);

    if (try_drain_mpi_collective(attemptId)) {
      return;
    }

    // We couldn't drain the collective.  Let's try again after sleeping for a
    // second. We set ckpt_pending to false to allow the user threads to
    // continue for a bit.
    pthread_mutex_lock(&seq_num_lock);
    ckpt_pending = false;
    pthread_mutex_unlock(&seq_num_lock);

    sleep(1);

    attemptId++;
  }
}
