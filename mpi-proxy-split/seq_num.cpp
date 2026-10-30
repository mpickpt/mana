#include <mpi.h>

#include <map>
#include <unordered_set>
#include <vector>
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
// The gids of the clocks whose targets this process raised in the current
// drain attempt.  The next drain round uploads their sequence numbers.
static std::vector<uint64_t> raised_clocks;

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

// Lets the MPI library make progress while this process waits for a drain.
// The other members may wait for a non-blocking collective that this process
// started.  The MPI library progresses that collective only during MPI calls.
static void
progress_mpi()
{
  MPI_Status status;
  int flag;
  MPI_Iprobe_internal(MPI_ANY_SOURCE, MPI_ANY_TAG, g_world_comm, &flag,
                      &status);
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
    progress_mpi();
    pthread_mutex_lock(&seq_num_lock);
  }
  current_phase = IN_CS;
  if (!counted) {
    pthread_mutex_unlock(&seq_num_lock);
    return;
  }
  mana_comm_desc *desc = get_comm_desc(comm);
  comm_clock *clock = desc->clock;
  clock->seq++;
  // This process passed its target.  The next drain round uploads the new
  // target for the other members.
  if (ckpt_pending && clock->seq > clock->target) {
    clock->target = clock->seq;
    if (desc->size > 1) {
      raised_clocks.push_back(desc->gid);
    }
  }
  pthread_mutex_unlock(&seq_num_lock);
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
    progress_mpi();
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

// Sets the target of each clock to its value in the KVDB 'db'.  A clock keeps
// its target if the KVDB has no value for it.  That is so for a clock that no
// process had at the start of the attempt and whose target no process raised.
void
download_targets(const char *db)
{
  int64_t max_seq = 0;
  for (auto &pair : clocks) {
    dmtcp::string comm_id_str(jalib::XToString(pair.first));
    KVDBResponse rc = dmtcp::kvdb::get64(db, comm_id_str, &max_seq);
    ASSERT(rc == KVDBResponse::SUCCESS || rc == KVDBResponse::KEY_NOT_FOUND,
           "db={} key={} rc={}", db, comm_id_str, rc);
    if (rc == KVDBResponse::SUCCESS) {
      pair.second.target = max_seq;
    }
  }
}

// Uploads the sequence number of each clock in raised_clocks to the KVDB 'db'.
// Then it empties raised_clocks and returns the number of uploads.
static int64_t
upload_raised_targets(const char *db)
{
  int64_t uploaded = 0;
  for (uint64_t gid : raised_clocks) {
    std::unordered_map<uint64_t, comm_clock>::iterator it = clocks.find(gid);
    if (it == clocks.end()) {
      continue;
    }
    dmtcp::string comm_id_str(jalib::XToString(gid));
    int64_t seq = it->second.seq;
    ASSERT(dmtcp::kvdb::request64(KVDBRequest::MAX, db, comm_id_str, seq) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", db, comm_id_str);
    uploaded++;
  }
  raised_clocks.clear();
  return uploaded;
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
  int64_t raised = 0;
  char db[64] = { 0 };
  ckpt_kvdb_name(db, sizeof(db), "comm-seq-max", attemptId);

  for (int i = 0; i < MAX_DRAIN_ROUNDS; i++) {
    char key[64] = { 0 };
    char barrier_id[64] = { 0 };
    char cs_id[64] = { 0 };
    char converge_id[64] = { 0 };
    char raised_id[64] = { 0 };

    ckpt_kvdb_name(cs_id, sizeof(cs_id), "CRITICAL-SECTION", attemptId);
    ckpt_kvdb_name(converge_id, sizeof(converge_id), "CONVERGE", attemptId);
    ckpt_kvdb_name(raised_id, sizeof(raised_id), "TARGETS-RAISED", attemptId);
    snprintf(barrier_id, 63, "MANA-PRESUSPEND-%06d-%06d", attemptId, round_num);
    snprintf(key, 63, "round-%06d", round_num);

    // An application thread can raise a target, add a clock or erase a clock
    // at the same time.
    pthread_mutex_lock(&seq_num_lock);
    int64_t uploaded = upload_raised_targets(db);
    int target_reached = check_seq_nums();
    pthread_mutex_unlock(&seq_num_lock);

    ASSERT(dmtcp::kvdb::request64(KVDBRequest::INCRBY, converge_id, key,
                                  target_reached) == KVDBResponse::SUCCESS,
           "db={} key={}", converge_id, key);
    ASSERT(dmtcp::kvdb::request64(KVDBRequest::OR, cs_id, key,
                                  current_phase == IN_CS) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", cs_id, key);
    ASSERT(dmtcp::kvdb::request64(KVDBRequest::INCRBY, raised_id, key,
                                  uploaded) == KVDBResponse::SUCCESS,
           "db={} key={}", raised_id, key);

    dmtcp_global_barrier(barrier_id);

    ASSERT(dmtcp::kvdb::get64(converge_id, key, &num_converged) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", converge_id, key);
    ASSERT(dmtcp::kvdb::get64(cs_id, key, &in_cs) == KVDBResponse::SUCCESS,
           "db={} key={}", cs_id, key);
    ASSERT(dmtcp::kvdb::get64(raised_id, key, &raised) ==
             KVDBResponse::SUCCESS,
           "db={} key={}", raised_id, key);

    // A process that reached its targets in this round can pass a target only
    // after its targets rise.  Its targets rise only in the download below.
    // So if no process raised a target, the drain ends.
    if (in_cs == 0 && num_converged == g_world_size && raised == 0) {
      return true;
    }
    if (raised > 0) {
      pthread_mutex_lock(&seq_num_lock);
      download_targets(db);
      pthread_mutex_unlock(&seq_num_lock);
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
    raised_clocks.clear();
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
