#include <mpi.h>

#include <map>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "jassert.h"
#include "jconvert.h"
#include "kvdb.h"
#include "seq_num.h"
#include "mpi_nextfunc.h"
#include "virtual_id.h"

using dmtcp::kvdb::KVDBRequest;
using dmtcp::kvdb::KVDBResponse;

// #define DEBUG_SEQ_NUM

constexpr int MAX_DRAIN_ROUNDS = 200;

extern "C" int MPI_Iprobe_internal(int source, int tag, MPI_Comm comm,
                                   int *flag, MPI_Status *status);
extern "C" int MPI_Test_internal(MPI_Request *request, int *flag,
                                 MPI_Status *status, bool isRealRequest);

extern int g_world_rank;
extern int g_world_size;
// Global communicator for MANA internal use
MPI_Comm g_world_comm;
volatile bool ckpt_pending;
int converged;
volatile phase_t current_phase = IS_READY;
unsigned int comm_gid;
int num_converged;
pthread_mutex_t seq_num_lock;
std::unordered_map<unsigned int, unsigned long> seq_num;
std::unordered_map<unsigned int, unsigned long> target;
std::unordered_map<MPI_Comm, unsigned int> ggid_table;

void seq_num_init() {
  ckpt_pending = false;
  pthread_mutex_init(&seq_num_lock, NULL);
}

void seq_num_destroy() {
  pthread_mutex_destroy(&seq_num_lock);
}

void seq_num_reset() {
  ckpt_pending = false;
}

void print_seq_nums() {
  unsigned int comm_id;
  unsigned long seq, target;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    seq = pair.second;
    printf("%d, %u, %lu\n", g_world_rank, comm_id, seq);
  }
  fflush(stdout);
}

int check_seq_nums() {
  unsigned int comm_id;
  int target_reached = 1;
  for (comm_seq_pair_t pair : seq_num) {
    comm_id = pair.first;
    if (target[comm_id] > seq_num[comm_id]) {
      target_reached = 0;
      break;
    }
  }
  return target_reached;
}

void seq_num_broadcast(MPI_Comm comm, unsigned long new_target) {
  unsigned int comm_gid = ggid_table[comm];
  unsigned long msg[2] = {comm_gid, new_target};
  int comm_size;
  int comm_rank;
  int world_rank;
  MPI_Comm_size(comm, &comm_size);
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Group world_group, local_group;
  MPI_Comm real_local_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Comm real_world_comm = get_real_id((mana_mpi_handle){.comm = g_world_comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Comm_group)(real_world_comm, &world_group);
  NEXT_FUNC(Comm_group)(real_local_comm, &local_group);
  for (int i = 0; i < comm_size; i++) {
    if (i != comm_rank) {
      NEXT_FUNC(Group_translate_ranks)(local_group, 1, &i,
                world_group, &world_rank);
      NEXT_FUNC(Send)(&msg, 2, MPI_UNSIGNED_LONG, world_rank,
                      0, real_world_comm);
#ifdef DEBUG_SEQ_NUM
      printf("rank %d sending to rank %d new target comm %u seq %lu target %lu\n",
             g_world_rank, world_rank, comm_gid, seq_num[comm_gid], new_target);
      fflush(stdout);
#endif
    }
  }
  NEXT_FUNC(Group_free)(&world_group);
  NEXT_FUNC(Group_free)(&local_group);
  RETURN_TO_UPPER_HALF();
}

// Finds the Collective Clock state of 'comm' in its virtual-ID entry.
// MPI_COMM_WORLD uses g_world_comm's entry; other predefined communicators,
// which have no entry, use the maps.
static inline void
lookup_comm_clock(MPI_Comm comm, unsigned int *comm_gid,
                  unsigned long **comm_seq, unsigned long **comm_target)
{
  MPI_Comm virt_comm = (comm == MPI_COMM_WORLD) ? g_world_comm : comm;
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.comm = virt_comm});
  if (entry != NULL) {
    mana_comm_desc *desc = (mana_comm_desc*)entry->desc;
    *comm_gid = desc->ggid;
    *comm_seq = desc->seq_num;
    *comm_target = desc->target;
  } else {
    *comm_gid = ggid_table[comm];
    *comm_seq = &seq_num[*comm_gid];
    *comm_target = &target[*comm_gid];
  }
}

void commit_begin(MPI_Comm comm) {
  if (mana_state == RESTART_REPLAY || comm == MPI_COMM_NULL) {
    return;
  }
  while (ckpt_pending && check_seq_nums()) {
    MPI_Status status;
    int flag;
    MPI_Iprobe_internal(MPI_ANY_SOURCE, MPI_ANY_TAG, g_world_comm, &flag,
                        &status);
    if (flag) {
      unsigned long new_target[2];
      MPI_Comm real_world_comm = get_real_id((mana_mpi_handle){.comm = g_world_comm}).comm;
      JUMP_TO_LOWER_HALF(lh_info->fsaddr);
      NEXT_FUNC(Recv)(&new_target, 2, MPI_UNSIGNED_LONG,
          status.MPI_SOURCE, status.MPI_TAG, real_world_comm,
          MPI_STATUS_IGNORE);
      RETURN_TO_UPPER_HALF();
      unsigned int updated_comm = (unsigned int) new_target[0];
      unsigned long updated_target = new_target[1];
      std::unordered_map<unsigned int, unsigned long>::iterator it =
        target.find(updated_comm);
      if (it != target.end() && it->second < updated_target) {
        target[updated_comm] = updated_target;
#ifdef DEBUG_SEQ_NUM
        printf("rank %d received new target comm %u seq %lu target %lu\n",
            g_world_rank, updated_comm, it->second->seq_num, updated_target);
        fflush(stdout);
#endif
      }
    }
  }
  unsigned int comm_gid;
  unsigned long *comm_seq, *comm_target;
  pthread_mutex_lock(&seq_num_lock);
  current_phase = IN_CS;
  lookup_comm_clock(comm, &comm_gid, &comm_seq, &comm_target);
  (*comm_seq)++;
  pthread_mutex_unlock(&seq_num_lock);
  if (ckpt_pending && *comm_seq > *comm_target) {
    *comm_target = *comm_seq;
    seq_num_broadcast(comm, *comm_seq);
  }
}

void commit_finish(MPI_Comm comm) {
  if (mana_state == RESTART_REPLAY) {
    return;
  }
  current_phase = IS_READY;
  while (ckpt_pending && check_seq_nums()) {
    MPI_Status status;
    int flag;
    MPI_Iprobe_internal(MPI_ANY_SOURCE, MPI_ANY_TAG, g_world_comm, &flag,
                        &status);
    if (flag) {
      unsigned long new_target[2];
      MPI_Comm real_world_comm = get_real_id((mana_mpi_handle){.comm = g_world_comm}).comm;
      JUMP_TO_LOWER_HALF(lh_info->fsaddr);
      NEXT_FUNC(Recv)(&new_target, 2, MPI_UNSIGNED_LONG,
          status.MPI_SOURCE, status.MPI_TAG, real_world_comm,
          MPI_STATUS_IGNORE);
      RETURN_TO_UPPER_HALF();
      unsigned int updated_comm = (unsigned int) new_target[0];
      unsigned long updated_target = new_target[1];
      std::unordered_map<unsigned int, unsigned long>::iterator it =
        target.find(updated_comm);
      if (it != target.end() && it->second < updated_target) {
        target[updated_comm] = updated_target;
#ifdef DEBUG_SEQ_NUM
        printf("rank %d received new target comm %u seq %lu target %lu\n",
            g_world_rank, updated_comm, it->second->seq_num, updated_target);
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
                                   check_seq_nums()) == KVDBResponse::SUCCESS);
    JASSERT(dmtcp::kvdb::request64(KVDBRequest::OR, cs_id, key,
                                   current_phase == IN_CS) == KVDBResponse::SUCCESS);

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

// Completes the pending non-blocking collectives with MPI_Test.  Call it
// after drain_mpi_collective(): all ranks have then initiated the same
// collectives and commit_begin() blocks new ones, so each request completes.
// Call it with the lower half closed, so that no application thread is in
// MPI or tests these requests meanwhile.  The request then maps to
// MPI_REQUEST_NULL, so the application's later MPI_Wait/MPI_Test on it
// returns at once.  A rank that finishes first may stop calling MPI: its
// request completes only after its own part is done.
void
complete_pending_nonblocking_collectives()
{
  for (MPI_Request request : pending_collective_requests()) {
    int flag = 0;
    MPI_Status status;
    while (!flag) {
      int rc = MPI_Test_internal(&request, &flag, &status, false);
      JASSERT(rc == MPI_SUCCESS)(rc)
        .Text("MPI_Test failed on a pending non-blocking collective");
    }
    update_virt_id((mana_mpi_handle){.request = request},
                   (mana_mpi_handle){.request = MPI_REQUEST_NULL});
  }
}

// Tests each pending non-blocking collective once, under the same conditions
// as complete_pending_nonblocking_collectives(), and returns how many are
// still pending.
int
test_pending_nonblocking_collectives()
{
  int pending = 0;
  for (MPI_Request request : pending_collective_requests()) {
    int flag = 0;
    MPI_Status status;
    int rc = MPI_Test_internal(&request, &flag, &status, false);
    JASSERT(rc == MPI_SUCCESS)(rc)
      .Text("MPI_Test failed on a pending non-blocking collective");
    if (flag) {
      update_virt_id((mana_mpi_handle){.request = request},
                     (mana_mpi_handle){.request = MPI_REQUEST_NULL});
    } else {
      pending++;
    }
  }
  return pending;
}
