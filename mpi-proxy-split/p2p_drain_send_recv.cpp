/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License in the files COPYING and COPYING.LESSER.  If not, see           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mpi.h>
#include <map>
#include <algorithm>
#include <string>
#include <vector>
#include "kvdb.h"
#include "jassert.h"
#include "p2p_drain_send_recv.h"
#include "p2p_log_replay.h"
#include "mpi_nextfunc.h"
#include "virtual_id.h"

using namespace dmtcp;
using dmtcp::kvdb::KVDBRequest;
using dmtcp::kvdb::KVDBResponse;

extern int MPI_Alltoall_internal(const void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype,
                                 MPI_Comm comm);
// Defined with C linkage in mpi-wrappers/mpi_request_wrappers.cpp.
extern "C" int MPI_Test_internal(MPI_Request *, int *flag, MPI_Status *status,
                                 bool isRealRequest);
// FIXME: These three internal functions were added to avoid record and replay.
// Since we no longer record MPI_Comm and MPI_Group related functions, these
// internal functions can be removed.
extern int MPI_Comm_create_group_internal(MPI_Comm comm, MPI_Group group,
                                          int tag, MPI_Comm *newcomm);
extern int MPI_Comm_free_internal(MPI_Comm *comm);
extern int MPI_Comm_group_internal(MPI_Comm comm, MPI_Group *group);
extern int MPI_Group_free_internal(MPI_Group *group);
#ifdef DEBUG_P2P
int *g_sendBytesByRank; // Number of bytes sent to other ranks
int *g_rsendBytesByRank; // Number of bytes sent to other ranks by MPI_rsend
int *g_bytesSentToUsByRank; // Number of bytes other ranks sent to us
int *g_recvBytesByRank; // Number of bytes received from other ranks
#endif
int64_t global_sent_messages = 0, global_recv_messages = 0;
int64_t local_sent_messages = 0, local_recv_messages = 0;
std::unordered_set<MPI_Comm> active_comms;
dmtcp::vector<mpi_message_t*> g_message_queue;

// See p2p_drain_send_recv.h for documentation of these globals.
pending_recv_t g_pending_recv = { /*.state=*/ PENDING_RECV_IDLE };
volatile bool p2p_dummy_phase = false;
p2p_wait_t g_p2p_wait = P2P_WAIT_POLLING;

DrainStats g_drain_stats;

// This checkpoint's coordinator database (named in drainP2p()) and the
// in-flight drain's round.  Keys are never reused, so the counters and the
// blocked-rank bitmap, which only accumulate (INCRBY, OR), need no reset.
static char g_drain_db[128];
static int g_drain_round;

uint64_t
drainStatsNow()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void
resetDrainStats()
{
  memset(&g_drain_stats, 0, sizeof(g_drain_stats));
}

// The drain's requests to the coordinator, counted in g_drain_stats.
// kvGet() leaves *val unchanged if there is no such key.
static inline void
kvGet(const char *db, const char *key, int64_t *val)
{
  g_drain_stats.kvdb_requests++;
  kvdb::get64(db, key, val);
}

static inline void
kvIncr(const char *db, const char *key, int64_t val)
{
  g_drain_stats.kvdb_requests++;
  kvdb::request64(KVDBRequest::INCRBY, db, key, val);
}

// Adds val to the key (0 if it doesn't exist); returns the value before.
static inline int64_t
kvFetchAdd(const char *db, const char *key, int64_t val)
{
  g_drain_stats.kvdb_requests++;
  int64_t old = 0;
  KVDBResponse rc = kvdb::request64(KVDBRequest::INCRBY, db, key, val, &old);
  JASSERT(rc == KVDBResponse::SUCCESS)(db)(key);
  return old;
}

static inline void
kvOr(const char *db, const char *key, int64_t val)
{
  g_drain_stats.kvdb_requests++;
  kvdb::request64(KVDBRequest::OR, db, key, val);
}

static inline void
kvSetString(const char *db, const char *key, const char *val)
{
  g_drain_stats.kvdb_requests++;
  kvdb::set(db, key, val);
}

static inline KVDBResponse
kvGetString(const char *db, const char *key, dmtcp::string *val)
{
  g_drain_stats.kvdb_requests++;
  return kvdb::get(db, key, val);
}

static inline void
globalBarrier(const char *name)
{
  g_drain_stats.barriers++;
  dmtcp_global_barrier(name);
}

void
initialize_drain_send_recv()
{
  getLocalRankInfo();
  const char *wait = getenv("MANA_P2P_WAIT");
  if (wait != NULL && strcmp(wait, "blocking") == 0) {
    g_p2p_wait = P2P_WAIT_BLOCKING;
  } else if (wait != NULL && strcmp(wait, "polling") != 0 &&
             g_world_rank == 0) {
    // Not JWARNING: mana_launch silences it.
    fprintf(stderr, "WARNING: MANA_P2P_WAIT is 'polling' or 'blocking', not "
            "'%s'.  MANA uses 'polling'.\n", wait);
  }
#ifdef DEBUG_P2P
  g_sendBytesByRank = (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  g_rsendBytesByRank = (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  g_bytesSentToUsByRank =
    (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  g_recvBytesByRank = (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  memset(g_sendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_rsendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_bytesSentToUsByRank, 0, g_world_size * sizeof(int));
  memset(g_recvBytesByRank, 0, g_world_size * sizeof(int));
#endif
  active_comms.insert(MPI_COMM_WORLD);
  active_comms.insert(MPI_COMM_SELF);
}

// Sums all ranks' sent and received counts in this round's keys.  The next
// round uses new keys, so no barrier is needed after the reads: every rank
// reads the same sums and decides the same way whether to drain again.
void
registerLocalSendsAndRecvs()
{
  char sent_key[32], recv_key[32];
  uint64_t t0 = drainStatsNow();
  snprintf(sent_key, sizeof(sent_key), "sent_%d", g_drain_round);
  snprintf(recv_key, sizeof(recv_key), "recv_%d", g_drain_round);
  g_drain_round++;
  kvIncr(g_drain_db, sent_key, local_sent_messages);
  kvIncr(g_drain_db, recv_key,
         __atomic_load_n(&local_recv_messages, __ATOMIC_ACQUIRE));
  globalBarrier("MPI:Register-p2p-send-recv");
  global_sent_messages = 0;
  global_recv_messages = 0;
  kvGet(g_drain_db, sent_key, &global_sent_messages);
  kvGet(g_drain_db, recv_key, &global_recv_messages);
  g_drain_stats.t_register += drainStatsNow() - t0;
}

// status was received by MPI_Iprobe
int
recvMsgIntoInternalBuffer(MPI_Status status, MPI_Comm comm)
{
  int count = 0;
  int size = 0;
  MPI_Get_count(&status, MPI_BYTE, &count);
  MPI_Type_size(MPI_BYTE, &size);
  JASSERT(size == 1);
  void *buf = JALLOC_HELPER_MALLOC(count);
  // Bypass the MPI_Recv wrapper deliberately.  The wrapper publishes
  // g_pending_recv from a single-slot global, and the user thread's
  // pending blocking Recv may already have written that slot.  Calling
  // the wrapper here would clobber it, and unblockPendingRecvs would
  // then fail to dispatch a dummy for the user's Recv, deadlocking it.
  // The wrapper would also (incorrectly) check p2p_dummy_phase on the
  // return value of this real drained message.  Both problems are
  // avoided by going through NEXT_FUNC(Recv) directly.
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  int retval = NEXT_FUNC(Recv)(buf, count, MPI_BYTE,
                               status.MPI_SOURCE, status.MPI_TAG,
                               realComm, MPI_STATUS_IGNORE);
  JASSERT(retval == MPI_SUCCESS);
  RETURN_TO_UPPER_HALF();
  // The wrapper would have incremented local_recv_messages for this
  // real receive; we must do it explicitly when bypassing the wrapper,
  // so the global drain test in drainInFlightP2p() sees a balanced count.
  count_received_message();

  mpi_message_t *message = (mpi_message_t *)JALLOC_HELPER_MALLOC(sizeof(mpi_message_t));
  message->buf        = buf;
  message->count      = count;
  message->datatype   = MPI_BYTE;
  message->comm       = comm;
  message->status     = status;
  message->size       = size * count;

  // queue it
  g_message_queue.push_back(message);

  return count;
}

// Go through each pending MPI_Irecv (and MPI_Isend) and try to complete
// them before checkpointing.
int
completePendingP2pRequests()
{
  int bytesReceived = 0;
  for (MPI_Request request : pendingRequestsInPostingOrder()) {
    mpi_nonblocking_call_t call;
    if (!getPendingCall(request, &call)) {
      continue;  // The application completed it meanwhile.
    }
    int flag = 0;
    MPI_Status status;
    // This is needed if an MPI_Isend was called earlier.  Without this,
    // large messages within the same node will fail under MPICH and some
    // other MPIs.  A previous call to MPI_Irecv caused only the metadata to be
    // exchanged.  So, MPI_Iprobe succeeds and MPI_Irecv will later fail, unless
    // we force the sending of data via MPI_Test.
    MPI_Test_internal(&request, &flag, &status, false);
    if (flag) {
      if (call.type == IRECV_REQUEST) {
        int size = 0;
        MPI_Type_size(call.datatype, &size);
        int worldRank = localRankToGlobalRank(status.MPI_SOURCE,
                                              call.comm);
#ifdef DEBUG_P2P
        g_recvBytesByRank[worldRank] += call.count * size;
#endif
        count_received_message();
        g_drain_stats.irecvs_completed++;
        // Keep the status for the application's MPI_Wait or MPI_Test.
        complete_virt_request(request, &status);
      } else {
        if (call.type == ISEND_REQUEST) {
          g_drain_stats.isends_completed++;
        }
        update_virt_id((mana_mpi_handle){.request = request},
                       (mana_mpi_handle){.request = MPI_REQUEST_NULL});
      }
      clearPendingRequestFromLog(request);
    } else {
      /*  We go on to the next request even if the MPI_Test fails.
       * Otherwise, the message we are waiting for will be sent
       * after the checkpoint. This can result in an infinite loop.
       *
       * NOTE: This function will be called only if the global arrays
       * do not match. This can happen if a second sender has sent
       * a message to us, and we will receive the message only
       * after the checkpoint. The following diagram is an example:
       *
       * RANK 0           RANK 1              RANK 2        TIME
       *                                    Send to Rank 1   |
       *                Recv from Rank 0                     |
       * =====CKPT=======CKPT======CKPT======CKPT========    |
       *                Recv from Rank 2                     |
       * Send to Rank 1                                      V
       */
    }
  }
  return bytesReceived;
}

int
drainRemainingP2pMsgs()
{
  int bytesReceived = 0;
  // Probe the predefined communicators in active_comms and the live ones in
  // the virtual-ID table.  Skip MANA's g_world_comm: its Collective Clock
  // messages are not counted as sent or received.
  std::vector<MPI_Comm> comms(active_comms.begin(), active_comms.end());
  for (MPI_Comm virtComm : live_virt_comms()) {
    if (virtComm != g_world_comm) {
      comms.push_back(virtComm);
    }
  }
  for (MPI_Comm comm : comms) {
    // If the communicator is MPI_COMM_NULL, skip it.
    // MPI_COMM_NULL can be returned from functions like MPI_Comm_split
    // if the color is specified on only one side of the intercommunicator, or
    // specified as MPI_UNDEFINED by the program. In this case, the MPI function
    // still returns MPI_SUCCESS. So the MPI_COMM_NULL can be added to the
    // active communicator set `active_comms'.
    if (comm == MPI_COMM_NULL) {
      continue;
    }
    // Skip a communicator that the application has freed since.
    if (!is_predefined_id((mana_mpi_handle){.comm = comm}) &&
        lookup_virt_id_entry((mana_mpi_handle){.comm = comm}) == NULL) {
      continue;
    }
    g_drain_stats.comms_probed++;
    int flag = 1;
    while (flag) {
      MPI_Status status;
      g_drain_stats.iprobes++;
      int retval = MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &flag,
                              &status);
      JASSERT(retval == MPI_SUCCESS);
      if (flag) {
        MPI_Request matched_request = MPI_REQUEST_NULL;
        // Check if there are pending MPI_Irecv's that matches the envelope of the
        // probed message.  MPI matches the earliest posted one.
        for (MPI_Request req : pendingRequestsInPostingOrder()) {
          mpi_nonblocking_call_t call;
          if (getPendingCall(req, &call) &&
              call.type == IRECV_REQUEST &&
              call.comm == comm &&
              (call.tag == status.MPI_TAG || call.tag == MPI_ANY_TAG) &&
              (call.remote_node == status.MPI_SOURCE ||
               call.remote_node == MPI_ANY_SOURCE)) {
            matched_request = req;
            break;
          }
        }
        if (matched_request != MPI_REQUEST_NULL) {
          // If there are matched pending MPI_Irecv's, wait
          // on the request to complete the communication.
          // Otherwise, the message will be drained to the MANA internal buffer,
          // and then be received out of order, after restart.
          // Don't use the MPI_Wait wrapper: it would free the virtual
          // request, which the application still holds.  The application's
          // own MPI_Wait/MPI_Test frees it.
          int done = 0;
          MPI_Status recv_status;
          while (!done) {
            MPI_Test_internal(&matched_request, &done, &recv_status, false);
          }
          count_received_message();
          g_drain_stats.irecvs_completed++;
          complete_virt_request(matched_request, &recv_status);
          clearPendingRequestFromLog(matched_request);
        } else {
          int bytes = recvMsgIntoInternalBuffer(status, comm);
          bytesReceived += bytes;
          g_drain_stats.drained_msgs++;
          g_drain_stats.drained_bytes += bytes;
        }
      }
    }
  }
  return bytesReceived;
}

// Completes the pending MPI_Isends, whose messages the drain has seen
// received, since restart cannot replay a send.  The application's
// MPI_Wait/MPI_Test then sees the real request MPI_REQUEST_NULL.
static void
completePendingIsends()
{
  for (MPI_Request request : pendingRequestsInPostingOrder()) {
    if (pendingRequestType(request) != ISEND_REQUEST) {
      continue;
    }
    int flag = 0;
    MPI_Status status;
    while (!flag) {
      MPI_Test_internal(&request, &flag, &status, false);
    }
    update_virt_id((mana_mpi_handle){.request = request},
                   (mana_mpi_handle){.request = MPI_REQUEST_NULL});
    clearPendingRequestFromLog(request);
    g_drain_stats.isends_completed++;
  }
}

void
drainInFlightP2p()
{
  uint64_t t0 = drainStatsNow();
  registerLocalSendsAndRecvs();
  while (global_sent_messages > global_recv_messages) {
    g_drain_stats.iterations++;
    // If pending MPI_Irecv or MPI_Isend, use MPI_Test to try to complete it.
    uint64_t t = drainStatsNow();
    completePendingP2pRequests();
    g_drain_stats.t_complete += drainStatsNow() - t;
    // If MPI_Irecv not posted but msg was sent, use MPI_Iprobe to drain msg.
    t = drainStatsNow();
    drainRemainingP2pMsgs();
    g_drain_stats.t_probe += drainStatsNow() - t;
    // Update global recv coutner.
    registerLocalSendsAndRecvs();
  }
  uint64_t t = drainStatsNow();
  completePendingIsends();
  g_drain_stats.t_isends += drainStatsNow() - t;
  g_drain_stats.t_inflight = drainStatsNow() - t0;
}

// FIXME: existsMatchingMsgBuffer and consumeMatchingMsgBuffer both search
// in the g_message_queue with the same condition. Maybe we can
// combine them into one function.
bool
existsMatchingMsgBuffer(int source, int tag, MPI_Comm comm, int *flag,
                        MPI_Status *status)
{
  bool ret = false;
  dmtcp::vector<mpi_message_t*>::iterator req =
    std::find_if(g_message_queue.begin(), g_message_queue.end(),
                 [source, tag, comm](const mpi_message_t *msg)
                 { return ((msg->status.MPI_SOURCE == source) ||
                           (source == MPI_ANY_SOURCE)) &&
                          ((msg->status.MPI_TAG == tag) ||
                           (tag == MPI_ANY_TAG)) &&
                          ((msg->comm == comm)); });
  if (req != std::end(g_message_queue)) {
    *flag = 1;
    *status = (*req)->status;
    ret = true;
  }
  return ret;
}

int
consumeMatchingMsgBuffer(void *buf, int count, MPI_Datatype datatype,
                         int source, int tag, MPI_Comm comm,
                         MPI_Status *mpi_status, int size)
{
  mpi_message_t *foundMsg = NULL;
  dmtcp::vector<mpi_message_t*>::iterator req =
    std::find_if(g_message_queue.begin(), g_message_queue.end(),
                 [source, tag, comm](const mpi_message_t *msg)
                 { return ((msg->status.MPI_SOURCE == source) ||
                           (source == MPI_ANY_SOURCE)) &&
                          ((msg->status.MPI_TAG == tag) ||
                           (tag == MPI_ANY_TAG)) &&
                          ((msg->comm == comm)); });
  // This should never happen (since the caller should always check first using
  // existsMatchingMsgBuffer())!
  JASSERT(req != std::end(g_message_queue))(count)(datatype)
         .Text("Unexpected error: no message in the queue matches the given"
               " attributes.");
  foundMsg = *req;

  // The message was drained as packed MPI_BYTEs.  Unpack only the whole
  // elements it holds; a raw copy would break non-contiguous datatypes.
  int type_size = (count > 0) ? size / count : 0;
  int elements = (type_size > 0) ? foundMsg->size / type_size : 0;
  if (elements > count) {
    elements = count;
  }
  if (elements > 0) {
    int position = 0;
    MPI_Datatype realType =
      get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
    // Any communicator will do for unpacking; the message's may be freed.
    MPI_Comm realComm =
      get_real_id((mana_mpi_handle){.comm = MPI_COMM_SELF}).comm;
    int retval;
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    retval = NEXT_FUNC(Unpack)(foundMsg->buf, foundMsg->size, &position,
                               buf, elements, realType, realComm);
    RETURN_TO_UPPER_HALF();
    JASSERT(retval == MPI_SUCCESS)(retval);
  }
  *mpi_status = foundMsg->status;
  g_message_queue.erase(req);
  JALLOC_HELPER_FREE(foundMsg->buf);
  JALLOC_HELPER_FREE(foundMsg);
  return MPI_SUCCESS;
}

// Phase B of unblockPendingRecvs(), on a blocked rank: posts the dummy to
// its sender as dummy_<sender>_<slot>.  Virtual handles differ between
// ranks, so the dummy names the communicator by its mana_comm_desc name and
// gives the receive's size in bytes; the sender sends that many MPI_BYTEs.
static void
postDummy()
{
  // The communicator's name, size, this rank's rank in it, and its members'
  // world ranks ('members' NULL: member i is world rank i).  A predefined
  // communicator is named by its handle, which every rank shares, with
  // instance -1.
  MPI_Comm comm = g_pending_recv.comm;
  uint64_t comm_hash;
  int64_t comm_instance;
  int size, my_rank;
  const int *members = NULL;
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.comm = comm});
  if (entry != NULL) {
    mana_comm_desc *desc = (mana_comm_desc*)entry->desc;
    comm_hash = desc->ranks_hash;
    comm_instance = desc->instance;
    size = desc->size;
    my_rank = desc->rank;
    members = desc->global_ranks;
  } else if (comm == MPI_COMM_WORLD) {
    comm_hash = (uint64_t)comm;
    comm_instance = -1;
    size = g_world_size;
    my_rank = g_world_rank;
  } else {
    JASSERT(comm == MPI_COMM_SELF)(comm).Text("MPI_Recv on an unknown comm");
    comm_hash = (uint64_t)comm;
    comm_instance = -1;
    size = 1;
    my_rank = 0;
    members = &g_world_rank;
  }

  // The sender: the MPI_Recv's source, or for MPI_ANY_SOURCE the first
  // member that is not blocked itself (the bitmap is complete: every rank
  // published before the barrier).  A deadlock-free program has one.
  int source = g_pending_recv.source;
  int sender = -1;
  if (source != MPI_ANY_SOURCE) {
    JASSERT(source >= 0 && source < size)(source)(size);
    sender = source;
  } else {
    std::map<int, uint64_t> words;  // bitmap words read so far
    char key[64];
    for (int i = 0; i < size && sender < 0; i++) {
      int rank = members != NULL ? members[i] : i;
      if (words.find(rank / 64) == words.end()) {
        int64_t word = 0;
        snprintf(key, sizeof(key), "blocked_%d", rank / 64);
        kvGet(g_drain_db, key, &word);
        words[rank / 64] = (uint64_t)word;
      }
      if (!(words[rank / 64] & ((uint64_t)1 << (rank % 64)))) {
        sender = i;
      }
    }
    JASSERT(sender >= 0)(comm)
      .Text("MPI_ANY_SOURCE Recv with no unblocked sender in comm; "
            "user program may have deadlocked.");
  }
  int sender_world_rank = members != NULL ? members[sender] : sender;

  MPI_Datatype realType =
    get_real_id((mana_mpi_handle){.datatype = g_pending_recv.datatype})
      .datatype;
  int type_size = 0;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Type_size)(realType, &type_size);
  RETURN_TO_UPPER_HALF();
  int64_t bytes = (int64_t)type_size * g_pending_recv.count;
  int tag = g_pending_recv.tag == MPI_ANY_TAG ? 0 : g_pending_recv.tag;

  char key[64], dummy[128];
  snprintf(key, sizeof(key), "ndummies_%d", sender_world_rank);
  int64_t slot = kvFetchAdd(g_drain_db, key, 1);
  snprintf(key, sizeof(key), "dummy_%d_%lld", sender_world_rank,
           (long long)slot);
  snprintf(dummy, sizeof(dummy), "%llu %lld %d %d %lld",
           (unsigned long long)comm_hash, (long long)comm_instance, my_rank,
           tag, (long long)bytes);
  kvSetString(g_drain_db, key, dummy);
}

// Phase C of unblockPendingRecvs(): sends the dummies posted to this rank.
static void
sendPostedDummies()
{
  char key[64];
  int64_t count = 0;  // No key: no dummy to send
  snprintf(key, sizeof(key), "ndummies_%d", g_world_rank);
  kvGet(g_drain_db, key, &count);
  for (int64_t k = 0; k < count; k++) {
    snprintf(key, sizeof(key), "dummy_%d_%lld", g_world_rank, (long long)k);
    dmtcp::string dummy;
    KVDBResponse rc = kvGetString(g_drain_db, key, &dummy);
    JASSERT(rc == KVDBResponse::SUCCESS)(key)(rc);
    unsigned long long comm_hash;
    long long comm_instance, bytes;
    int dest, tag;
    JASSERT(sscanf(dummy.c_str(), "%llu %lld %d %d %lld", &comm_hash,
                   &comm_instance, &dest, &tag, &bytes) == 5)(dummy);

    // This rank's handle of the blocked rank's communicator: it is a
    // member, since the blocked rank chose it among the members.
    MPI_Comm virtComm = comm_instance == -1
                          ? (MPI_Comm)comm_hash
                          : find_virt_comm(comm_hash,
                                           (unsigned int)comm_instance);
    JASSERT(virtComm != MPI_COMM_NULL)(comm_hash)(comm_instance)
      .Text("The dummy's sender doesn't know the blocked MPI_Recv's comm");
    MPI_Comm realComm =
      get_real_id((mana_mpi_handle){.comm = virtComm}).comm;

    // Bypass the MPI_Send wrapper: a dummy must not count in
    // local_sent_messages (the receiver doesn't count it either).
    void *dummy_buf = calloc(bytes > 0 ? bytes : 1, 1);
    int ret;
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    ret = NEXT_FUNC(Send)(dummy_buf, (int)bytes, lh_info->MANA_BYTE, dest,
                          tag, realComm);
    RETURN_TO_UPPER_HALF();
    JASSERT(ret == MPI_SUCCESS)(ret)(dest)(tag);
    g_drain_stats.dummies++;
    free(dummy_buf);
  }
}

// Runs after drainInFlightP2p(): with no real message in flight, a blocked
// MPI_Recv can receive only its dummy (see p2p_dummy_phase).
void
unblockPendingRecvs()
{
  char key[64];
  uint64_t t0 = drainStatsNow();

  // Phase A: a rank blocked in MPI_Recv sets its bit in blocked_<rank / 64>.
  // Closing an IDLE slot makes a later MPI_Recv wait in the upper half,
  // where it needs no dummy; an ACTIVE slot means that MPI_Recv gets one.
  int state = PENDING_RECV_IDLE;
  __atomic_compare_exchange_n(&g_pending_recv.state, &state,
                              PENDING_RECV_CLOSED, false,
                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  bool blocked = (state == PENDING_RECV_ACTIVE);
  JASSERT(!blocked || g_p2p_wait == P2P_WAIT_BLOCKING)
    .Text("An MPI_Recv waits in the lower half with MANA_P2P_WAIT=polling");
  if (blocked) {
    snprintf(key, sizeof(key), "blocked_%d", g_world_rank / 64);
    kvOr(g_drain_db, key, (int64_t)((uint64_t)1 << (g_world_rank % 64)));
  }
  p2p_dummy_phase = true;
  g_drain_stats.blocked = blocked;
  uint64_t t1 = drainStatsNow();
  g_drain_stats.t_publish = t1 - t0;

  globalBarrier("MPI:P2P-Pending-Recv-Published");
  uint64_t t2 = drainStatsNow();
  g_drain_stats.t_published = t2 - t1;

  // Phase B: each blocked rank posts its dummy to the rank that sends it.
  if (blocked) {
    postDummy();
  }
  uint64_t t3 = drainStatsNow();
  g_drain_stats.t_post = t3 - t2;

  globalBarrier("MPI:P2P-Pending-Recv-Posted");
  uint64_t t4 = drainStatsNow();
  g_drain_stats.t_posted = t4 - t3;

  // Phase C: send the dummies posted to this rank.  Every rank has set
  // p2p_dummy_phase before the first barrier.
  sendPostedDummies();
  uint64_t t5 = drainStatsNow();
  g_drain_stats.t_dispatch = t5 - t4;

  // Phase D: wait for all dummies to have been issued globally.
  globalBarrier("MPI:P2P-Pending-Recv-Dummies-Sent");
  g_drain_stats.t_dispatched = drainStatsNow() - t5;
  g_drain_stats.t_unblock = drainStatsNow() - t0;
}

void
drainP2p()
{
  // Name this checkpoint's database by the computation ID and a drain count;
  // both are the same on every rank and survive restart.
  static int drains = 0;
  DmtcpUniqueProcessId id = dmtcp_get_computation_id();
  snprintf(g_drain_db, sizeof(g_drain_db), "/plugin/MANA/p2p-%llx-%llx-%x-%d",
           (unsigned long long)id._hostid, (unsigned long long)id._time,
           (unsigned int)id._pid, ++drains);
  g_drain_round = 0;
  drainInFlightP2p();
  unblockPendingRecvs();
}

void
resetDrainCounters()
{
  // p2p_dummy_phase is cleared on EVENT_RESUME and EVENT_RESTART
  // (where this function is called from mpi_plugin_event_hook).  This
  // releases any MPI_Recv wrappers that were parked in their
  // wait-for-resume loop after consuming a dummy, and reopens the
  // pending-Recv slot.
  p2p_dummy_phase = false;
  __atomic_store_n(&g_pending_recv.state, PENDING_RECV_IDLE, __ATOMIC_RELEASE);
#ifdef DEBUG_P2P
  memset(g_sendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_rsendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_bytesSentToUsByRank, 0, g_world_size * sizeof(int));
  memset(g_recvBytesByRank, 0, g_world_size * sizeof(int));
#endif
}

int
localRankToGlobalRank(int localRank, MPI_Comm localComm)
{
  int worldRank;
  // FIXME: For interface8, use the new architecture.
  // This only works for interface7
  MPI_Group worldGroup, localGroup;
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = localComm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Comm_group)(MPI_COMM_WORLD, &worldGroup);
  NEXT_FUNC(Comm_group)(realComm, &localGroup);
  NEXT_FUNC(Group_translate_ranks)(localGroup, 1, &localRank,
                                   worldGroup, &worldRank);
  NEXT_FUNC(Group_free)(&worldGroup);
  NEXT_FUNC(Group_free)(&localGroup);
  RETURN_TO_UPPER_HALF();
  return worldRank;
}

// With MANA_DRAIN_STATS set, rank 0 prints the drain's maximum times and
// total counts over all ranks (this adds a global barrier).
void
reportDrainStats()
{
  if (getenv("MANA_DRAIN_STATS") == NULL) {
    return;
  }
  static int checkpoint = 0;
  checkpoint++;
  char db[sizeof(g_drain_db) + 16];
  snprintf(db, sizeof(db), "%s-stats", g_drain_db);
  const DrainStats &d = g_drain_stats;
  struct { const char *name; int64_t value; bool is_time; } metrics[] = {
    {"collective", (int64_t)d.t_collective, true},
    {"inflight", (int64_t)d.t_inflight, true},
    {"register", (int64_t)d.t_register, true},
    {"complete", (int64_t)d.t_complete, true},
    {"probe", (int64_t)d.t_probe, true},
    {"isends", (int64_t)d.t_isends, true},
    {"unblock", (int64_t)d.t_unblock, true},
    {"publish", (int64_t)d.t_publish, true},
    {"published", (int64_t)d.t_published, true},
    {"post", (int64_t)d.t_post, true},
    {"posted", (int64_t)d.t_posted, true},
    {"dispatch", (int64_t)d.t_dispatch, true},
    {"dispatched", (int64_t)d.t_dispatched, true},
    {"wait_lower_half", (int64_t)d.t_wait_lower_half, true},
    {"iterations", d.iterations, true},  // The same on every rank
    {"comms_probed", d.comms_probed, false},
    {"iprobes", d.iprobes, false},
    {"drained_msgs", d.drained_msgs, false},
    {"drained_bytes", d.drained_bytes, false},
    {"irecvs_completed", d.irecvs_completed, false},
    {"isends_completed", d.isends_completed, false},
    {"blocked", d.blocked, false},
    {"dummies", d.dummies, false},
    {"kvdb_requests", d.kvdb_requests, false},
    {"barriers", d.barriers, true},      // The same on every rank
  };
  const int n = sizeof(metrics) / sizeof(metrics[0]);
  for (int i = 0; i < n; i++) {
    kvdb::request64(metrics[i].is_time ? KVDBRequest::MAX : KVDBRequest::INCRBY,
                    db, metrics[i].name, metrics[i].value);
  }
  dmtcp_global_barrier("MPI:Drain-Stats");
  if (g_world_rank != 0) {
    return;
  }
  std::map<std::string, long> v;
  for (int i = 0; i < n; i++) {
    int64_t value = 0;
    kvdb::get64(db, metrics[i].name, &value);
    v[metrics[i].name] = (long)value;
  }
  fprintf(stderr,
          "MANA drain stats, checkpoint %d, %d ranks, MANA_P2P_WAIT=%s on "
          "rank 0 (us: max over ranks):\n"
          "  collective %ld | in-flight %ld (register %ld, complete %ld, "
          "probe %ld, isends %ld) | unblock %ld (publish %ld, barrier %ld, "
          "post %ld, barrier %ld, dispatch %ld, barrier %ld) | "
          "wait-lower-half %ld\n"
          "  iterations %ld, barriers %ld; totals: comms probed %ld, "
          "iprobes %ld, drained %ld msgs %ld bytes, irecvs completed %ld, "
          "isends completed %ld, blocked %ld, dummies %ld, "
          "kvdb requests %ld\n",
          checkpoint, g_world_size,
          g_p2p_wait == P2P_WAIT_POLLING ? "polling" : "blocking",
          v["collective"], v["inflight"],
          v["register"], v["complete"], v["probe"], v["isends"], v["unblock"],
          v["publish"], v["published"], v["post"], v["posted"],
          v["dispatch"], v["dispatched"], v["wait_lower_half"],
          v["iterations"], v["barriers"], v["comms_probed"], v["iprobes"],
          v["drained_msgs"], v["drained_bytes"], v["irecvs_completed"],
          v["isends_completed"], v["blocked"], v["dummies"],
          v["kvdb_requests"]);
}
