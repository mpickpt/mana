#include <mpi.h>

#include <map>
#include <unordered_set>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "dmtcp.h"
#include "dmtcp_assert.h"
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
int num_converged;
pthread_mutex_t seq_num_lock;
// The clocks of this process, by gid.  The descriptor of a communicator
// points to its clock.
static std::unordered_map<uint64_t, comm_clock> clocks;

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
  for (auto &pair : clocks) {
    printf("%d, %lu, %lu\n", g_world_rank, pair.first, pair.second.seq);
  }
  fflush(stdout);
}

int check_seq_nums() {
  int target_reached = 1;
  for (auto &pair : clocks) {
    if (pair.second.target > pair.second.seq) {
      target_reached = 0;
      break;
    }
  }
  return target_reached;
}

// Calls check_seq_nums() under seq_num_lock and returns its result.  Another
// thread can add or erase clocks at the same time.
static int
check_seq_nums_locked()
{
  pthread_mutex_lock(&seq_num_lock);
  int target_reached = check_seq_nums();
  pthread_mutex_unlock(&seq_num_lock);
  return target_reached;
}

comm_clock*
add_comm_clock(uint64_t gid)
{
  pthread_mutex_lock(&seq_num_lock);
  comm_clock *clock = &clocks[gid];
  pthread_mutex_unlock(&seq_num_lock);
  return clock;
}

// Erases the clocks of the communicators that this process freed.  Call it
// under seq_num_lock when a drain completes.  At that time, all members of
// such a communicator made the same number of calls on it.  On this process,
// the last call was MPI_Comm_free.  So all members freed the communicator.
static void
erase_freed_clocks()
{
  std::unordered_set<uint64_t> live;
  live.insert(get_comm_desc(MPI_COMM_SELF)->gid);
  for (MPI_Comm comm : live_virt_comms()) {
    live.insert(get_comm_desc(comm)->gid);
  }
  for (auto it = clocks.begin(); it != clocks.end();) {
    if (live.count(it->first) == 0) {
      it = clocks.erase(it);
    } else {
      ++it;
    }
  }
}

void seq_num_broadcast(MPI_Comm comm, unsigned long new_target) {
  uint64_t comm_gid = get_comm_desc(comm)->gid;
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
      NEXT_FUNC(Send)(&msg, 2, lh_info->MANA_UNSIGNED_LONG, world_rank,
                      0, real_world_comm);
#ifdef DEBUG_SEQ_NUM
      printf("rank %d sending to rank %d new target comm %u seq %lu target %lu\n",
             g_world_rank, world_rank, comm_gid, clocks[comm_gid].seq,
             new_target);
      fflush(stdout);
#endif
    }
  }
  NEXT_FUNC(Group_free)(&world_group);
  NEXT_FUNC(Group_free)(&local_group);
  RETURN_TO_UPPER_HALF();
}

// Receives a target message, if one arrived, and raises the target of its
// clock.  It ignores the target of a clock that it erased, because every
// member freed that communicator.
static void
receive_target()
{
  MPI_Status status;
  int flag;
  MPI_Iprobe_internal(MPI_ANY_SOURCE, MPI_ANY_TAG, g_world_comm, &flag,
                      &status);
  if (flag) {
    unsigned long new_target[2];
    MPI_Comm real_world_comm =
      get_real_id((mana_mpi_handle){.comm = g_world_comm}).comm;
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    NEXT_FUNC(Recv)(&new_target, 2, lh_info->MANA_UNSIGNED_LONG,
        status.MPI_SOURCE, status.MPI_TAG, real_world_comm,
        MPI_STATUS_IGNORE);
    RETURN_TO_UPPER_HALF();
    uint64_t updated_comm = new_target[0];
    unsigned long updated_target = new_target[1];
    pthread_mutex_lock(&seq_num_lock);
    std::unordered_map<uint64_t, comm_clock>::iterator it =
      clocks.find(updated_comm);
    if (it != clocks.end() && it->second.target < updated_target) {
      it->second.target = updated_target;
#ifdef DEBUG_SEQ_NUM
      printf("rank %d received new target comm %lu seq %lu target %lu\n",
          g_world_rank, updated_comm, it->second.seq, updated_target);
      fflush(stdout);
#endif
    }
    pthread_mutex_unlock(&seq_num_lock);
  }
}

// Waits while a drain attempt runs and this process has reached all its
// targets.  Then it enters a critical section.  If 'counted' is true, it also
// counts a call on the clock of 'comm'.
static void
begin_critical_section(MPI_Comm comm, bool counted)
{
  // Checking ckpt_pending and incrementing the sequence number should be
  // atomic.  drain_mpi_collective() sets ckpt_pending and shares the
  // sequence numbers under this lock.
  pthread_mutex_lock(&seq_num_lock);
  while (ckpt_pending && check_seq_nums()) {
    pthread_mutex_unlock(&seq_num_lock);
    receive_target();
    pthread_mutex_lock(&seq_num_lock);
  }
  current_phase = IN_CS;
  if (!counted) {
    pthread_mutex_unlock(&seq_num_lock);
    return;
  }
  comm_clock *clock = get_comm_desc(comm)->clock;
  clock->seq++;
  pthread_mutex_unlock(&seq_num_lock);
  if (ckpt_pending && clock->seq > clock->target) {
    clock->target = clock->seq;
    seq_num_broadcast(comm, clock->seq);
  }
}

void commit_begin(MPI_Comm comm) {
  if (comm == MPI_COMM_NULL) {
    return;
  }
  begin_critical_section(comm, true);
}

void commit_begin_uncounted() {
  begin_critical_section(MPI_COMM_NULL, false);
}

void commit_finish(MPI_Comm comm) {
  current_phase = IS_READY;
  while (ckpt_pending && check_seq_nums_locked()) {
    receive_target();
  }
}

void
upload_seq_num(const char *db)
{
  for (auto &pair : clocks) {
    dmtcp::string comm_id_str(jalib::XToString(pair.first));
    int64_t seq = pair.second.seq;
    ASSERT(dmtcp::kvdb::request64(KVDBRequest::MAX, db, comm_id_str, seq) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", db, comm_id_str);
  }
}

void
download_targets(const char *db)
{
  int64_t max_seq = 0;
  for (auto &pair : clocks) {
    dmtcp::string comm_id_str(jalib::XToString(pair.first));
    ASSERT(dmtcp::kvdb::get64(db, comm_id_str, &max_seq) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", db, comm_id_str);
    pair.second.target = max_seq;
  }
}

// Writes the KVDB name "/plugin/MANA/<what>-<start>-<generation>" to buf,
// with "-<attempt>" appended if attempt >= 0: <start> is when the computation
// started, and <generation> is the checkpoint's.  The coordinator keeps its
// KVDB across checkpoints, and across computations, until a restart, so each
// checkpoint needs names of its own.  Every rank has both values from the
// coordinator before DMTCP_EVENT_PRESUSPEND.  Fails if the name is longer
// than 63 characters, where DMTCP would truncate it.
void
ckpt_kvdb_name(char *buf, size_t size, const char *what, int attempt)
{
  DmtcpUniqueProcessId id = dmtcp_get_computation_id();
  unsigned long long start = id._time;
  unsigned int generation = id._computation_generation;
  int n = attempt < 0
    ? snprintf(buf, size, "/plugin/MANA/%s-%llx-%u", what, start, generation)
    : snprintf(buf, size, "/plugin/MANA/%s-%llx-%u-%06d", what, start,
               generation, attempt);
  // DMTCP silently truncates a KVDB name at 63 characters (kvdbId).
  ASSERT(n > 0 && n < 64 && (size_t)n < size, "KVDB name too long: {}", buf);
}

void
share_seq_nums(int attemptId)
{
  char db[64] = { 0 };
  char barrier[64] = { 0 };
  ckpt_kvdb_name(db, sizeof(db), "comm-seq-max", attemptId);
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

    ckpt_kvdb_name(cs_id, sizeof(cs_id), "CRITICAL-SECTION", attemptId);
    ckpt_kvdb_name(converge_id, sizeof(converge_id), "CONVERGE", attemptId);
    snprintf(barrier_id, 63, "MANA-PRESUSPEND-%06d-%06d", attemptId, round_num);
    snprintf(key, 63, "round-%06d", round_num);

    ASSERT(dmtcp::kvdb::request64(KVDBRequest::INCRBY, converge_id, key,
                                  check_seq_nums_locked()) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", converge_id, key);
    ASSERT(dmtcp::kvdb::request64(KVDBRequest::OR, cs_id, key,
                                  current_phase == IN_CS) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", cs_id, key);

    dmtcp_global_barrier(barrier_id);

    ASSERT(dmtcp::kvdb::get64(converge_id, key, &num_converged) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", converge_id, key);
    ASSERT(dmtcp::kvdb::get64(cs_id, key, &in_cs) == KVDBResponse::SUCCESS,
           "db={} key={}", cs_id, key);

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
      pthread_mutex_lock(&seq_num_lock);
      erase_freed_clocks();
      pthread_mutex_unlock(&seq_num_lock);
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
      ASSERT(rc == MPI_SUCCESS,
             "MPI_Test failed on a pending non-blocking collective: rc={}", rc);
    }
    update_virt_id((mana_mpi_handle){.request = request},
                   (mana_mpi_handle){.request = lh_info->MANA_REQUEST_NULL});
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
    ASSERT(rc == MPI_SUCCESS,
           "MPI_Test failed on a pending non-blocking collective: rc={}", rc);
    if (flag) {
      update_virt_id((mana_mpi_handle){.request = request},
                     (mana_mpi_handle){.request = lh_info->MANA_REQUEST_NULL});
    } else {
      pending++;
    }
  }
  return pending;
}
