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

#include <inttypes.h>
#include <stdio.h>
#include <mpi.h>
#include <map>
#include <algorithm>
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
extern int MPI_Test_internal(MPI_Request *, int *flag, MPI_Status *status,
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
int64_t local_sent_messages = 0, local_recv_messages = 0;
std::unordered_set<MPI_Comm> active_comms;
dmtcp::vector<mpi_message_t*> g_message_queue;

// See p2p_drain_send_recv.h for documentation of these globals.
pending_recv_t g_pending_recv = { /*.active=*/ false };
volatile bool p2p_dummy_phase = false;

void
initialize_drain_send_recv()
{
  getLocalRankInfo();
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

// The coordinator clears its kvdb only on restart, so each checkpoint uses
// its own database.  During pre-suspend, dmtcp_get_generation() still
// returns the previous checkpoint's generation.
static void
getDrainDbName(char *db, size_t len, const char *name)
{
  snprintf(db, len, "/plugin/MANA/%s-%06u", name, dmtcp_get_generation());
}

// Returns the global number of sent but not yet received messages.  Each
// round uses a new key, so the counter never needs to be reset.
static int64_t
countUnreceivedMessages(int round)
{
  char db[64];
  char key[64];
  char barrier[64];
  getDrainDbName(db, sizeof(db), "p2p-unreceived");
  snprintf(key, sizeof(key), "round-%06d", round);
  snprintf(barrier, sizeof(barrier), "MPI:Register-p2p-send-recv-%06d",
           round);

  JASSERT(kvdb::request64(KVDBRequest::INCRBY, db, key,
                          local_sent_messages - local_recv_messages) ==
          KVDBResponse::SUCCESS);
  dmtcp_global_barrier(barrier);
  int64_t unreceived = 0;
  JASSERT(kvdb::get64(db, key, &unreceived) == KVDBResponse::SUCCESS);
  return unreceived;
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
  local_recv_messages++;

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

// Go through each MPI_Irecv in the g_nonblocking_calls map and try to complete
// the MPI_Irecv and MPI_Isend before checkpointing.
int
completePendingP2pRequests()
{
  int bytesReceived = 0;
  dmtcp::map<MPI_Request, mpi_nonblocking_call_t*>::iterator it;
  for (it = g_nonblocking_calls.begin(); it != g_nonblocking_calls.end();) {
    MPI_Request request = it->first;
    mpi_nonblocking_call_t *call = it->second;
    int flag = 0;
    MPI_Status status;
    // This is needed if an MPI_Isend was called earlier.  Without this,
    // large messages within the same node will fail under MPICH and some
    // other MPIs.  A previous call to MPI_Irecv caused only the metadata to be
    // exchanged.  So, MPI_Iprobe succeeds and MPI_Irecv will later fail, unless
    // we force the sending of data via MPI_Test.
    MPI_Test_internal(&request, &flag, &status, false);
    if (flag) {
      if (call->type == IRECV_REQUEST) {
        int size = 0;
        MPI_Type_size(call->datatype, &size);
        int worldRank = localRankToGlobalRank(status.MPI_SOURCE,
                                              call->comm);
#ifdef DEBUG_P2P
        g_recvBytesByRank[worldRank] += call->count * size;
#endif
        local_recv_messages++;
      }
      update_virt_id((mana_mpi_handle){.request = request},(mana_mpi_handle){.request = MPI_REQUEST_NULL});
      it = g_nonblocking_calls.erase(it);
    } else {
      /*  We update the iterator even if the MPI_Test fails.
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
      it++;
    }
  }
  return bytesReceived;
}

int
drainRemainingP2pMsgs()
{
  int bytesReceived = 0;
  std::unordered_set<MPI_Comm>::iterator comm;
  for (comm = active_comms.begin(); comm != active_comms.end(); comm++) {
    // If the communicator is MPI_COMM_NULL, skip it.
    // MPI_COMM_NULL can be returned from functions like MPI_Comm_split
    // if the color is specified on only one side of the intercommunicator, or
    // specified as MPI_UNDEFINED by the program. In this case, the MPI function
    // still returns MPI_SUCCESS. So the MPI_COMM_NULL can be added to the
    // active communicator set `active_comms'.
    if (*comm == MPI_COMM_NULL) {
      continue;
    }
    int flag = 1;
    while (flag) {
      MPI_Status status;
      int retval = MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, *comm, &flag, &status);
      JASSERT(retval == MPI_SUCCESS);
      if (flag) {
        MPI_Request matched_request = MPI_REQUEST_NULL;
        std::map<MPI_Request, mpi_nonblocking_call_t*>::iterator it;
        // Check if there are pending MPI_Irecv's that matches the envelope of the
        // probed message.
        for (it = g_nonblocking_calls.begin(); it != g_nonblocking_calls.end(); it++) {
          MPI_Request req = it->first;
          mpi_nonblocking_call_t *call = it->second;
          if (call->type == IRECV_REQUEST &&
              call->comm == *comm &&
              (call->tag == status.MPI_TAG || call->tag == MPI_ANY_TAG) &&
              (call->remote_node == status.MPI_SOURCE ||
               call->remote_node == MPI_ANY_SOURCE)) {
            matched_request = req;
            break;
          }
        }
        if (matched_request != MPI_REQUEST_NULL) {
          // If there are matched pending MPI_Irecv's, wait
          // on the request to complete the communication.
          // Otherwise, the message will be drained to the MANA internal buffer,
          // and then be received out of order, after restart.
          MPI_Wait(&matched_request, MPI_STATUS_IGNORE);
        } else {
          bytesReceived += recvMsgIntoInternalBuffer(status, *comm);
        }
      }
    }
  }
  return bytesReceived;
}

void
drainInFlightP2p()
{
  int round = 0;
  while (countUnreceivedMessages(round++) > 0) {
    // If pending MPI_Irecv or MPI_Isend, use MPI_Test to try to complete it.
    completePendingP2pRequests();
    // If MPI_Irecv not posted but msg was sent, use MPI_Iprobe to drain msg.
    drainRemainingP2pMsgs();
  }
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
  int cpysize;
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

  cpysize = (size < foundMsg->size) ? size: foundMsg->size;
  memcpy(buf, foundMsg->buf, cpysize);
  *mpi_status = foundMsg->status;
  g_message_queue.erase(req);
  JALLOC_HELPER_FREE(foundMsg->buf);
  JALLOC_HELPER_FREE(foundMsg);
  return MPI_SUCCESS;
}

// Publish g_pending_recv to kvdb (one record per blocked rank), then for
// each rank that is blocked on MPI_Recv, decide whether *this* rank is the
// designated dummy sender and, if so, issue a zero-byte MPI_Send to
// unblock the receiver.
//
// Must be called after drainInFlightP2p() has returned (i.e., once no
// sent message remains unreceived).  At that point, no real
// user-issued p2p messages remain in flight, so any MPI_Send issued
// here can only be consumed by a blocked MPI_Recv, and that Recv's
// wrapper will recognize the dummy via p2p_dummy_phase.
//
// Dispatch rule (every rank computes the same answer from the same
// kvdb snapshot, so each dummy is sent by exactly one rank):
//   - If pr.source != MPI_ANY_SOURCE, the sender is pr.source (a rank
//     in pr.comm).
//   - If pr.source == MPI_ANY_SOURCE, the sender is the lowest-ranked
//     member of pr.comm that is not itself blocked on MPI_Recv.  MANA
//     assumes deadlock-free programs, so at least one such member
//     exists.
//   - Tag = pr.tag, or 0 if pr.tag == MPI_ANY_TAG.
//
// The dummy MPI_Send is issued directly via NEXT_FUNC(Send), bypassing
// the MPI_Send wrapper, so it does NOT increment local_sent_messages.
// (Otherwise the next checkpoint's drain test would start from a
// skewed baseline.)
void
unblockPendingRecvs()
{
  char db[64];
  char key[64];
  char record[128];
  getDrainDbName(db, sizeof(db), "p2p-pending-recv");

  // Phase A: only blocked ranks publish.  INCRBY returns the previous
  // value, which gives each blocked rank a unique slot.
  if (g_pending_recv.active) {
    int64_t slot = 0;
    JASSERT(kvdb::request64(KVDBRequest::INCRBY, db, "num_blocked", 1,
                            &slot) == KVDBResponse::SUCCESS);
    snprintf(key, sizeof(key), "blocked-%06" PRId64, slot);
    snprintf(record, sizeof(record), "%d %d %d %" PRId64,
             g_world_rank, g_pending_recv.source, g_pending_recv.tag,
             (int64_t)g_pending_recv.comm);
    JASSERT(kvdb::set(db, key, record) == KVDBResponse::SUCCESS)(key);
  }

  p2p_dummy_phase = true;

  dmtcp_global_barrier("MPI:P2P-Pending-Recv-Published");

  // Phase B: num_blocked stays 0 if no rank published.  Every rank reads
  // the same value, so all ranks skip the Dummies-Sent barrier or none do.
  int64_t num_blocked = 0;
  kvdb::get64(db, "num_blocked", &num_blocked);
  if (num_blocked == 0) {
    return;
  }

  std::vector<int64_t> active(g_world_size, 0);
  std::vector<int64_t> source(g_world_size);
  std::vector<int64_t> tag(g_world_size);
  std::vector<int64_t> comm(g_world_size);
  for (int64_t slot = 0; slot < num_blocked; slot++) {
    snprintf(key, sizeof(key), "blocked-%06" PRId64, slot);
    dmtcp::string value;
    JASSERT(kvdb::get(db, key, &value) == KVDBResponse::SUCCESS)(key);
    int r, src, tg;
    int64_t cm;
    JASSERT(sscanf(value.c_str(), "%d %d %d %" SCNd64,
                   &r, &src, &tg, &cm) == 4)(key)(value);
    JASSERT(r >= 0 && r < g_world_size)(r)(value);
    active[r] = 1;
    source[r] = src;
    tag[r] = tg;
    comm[r] = cm;
  }

  // Phase C: dispatch dummies.
  // For each blocked rank j, every member of j's communicator
  // independently computes the sender; only the designated 
  // sender issues the MPI_Send.
  // For each communicator, there must be at least one process
  // is not blocked in a pending receive. Otherwise, it's a
  // deadlock in user's program.
  for (int j = 0; j < g_world_size; j++) {
    if (!active[j]) { continue; }

    MPI_Comm virtComm = (MPI_Comm)comm[j];
    MPI_Comm realComm =
      get_real_id((mana_mpi_handle){.comm = virtComm}).comm;

    int comm_size;
    MPI_Group local_group;
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    NEXT_FUNC(Comm_size)(realComm, &comm_size);
    NEXT_FUNC(Comm_group)(realComm, &local_group);
    RETURN_TO_UPPER_HALF();

    int *local_ranks = (int*)malloc(sizeof(int) * comm_size);
    int *global_ranks = (int*)malloc(sizeof(int) * comm_size);
    for (int i = 0; i < comm_size; i++) {
      local_ranks[i] = i;
    }
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    NEXT_FUNC(Group_translate_ranks)(local_group, comm_size, local_ranks,
                                     g_world_group, global_ranks);
    NEXT_FUNC(Group_free)(&local_group);
    RETURN_TO_UPPER_HALF();
    free(local_ranks);

    // Find my local rank in this comm (or -1 if I am not a member).
    int my_local_rank_in_comm = -1;
    for (int i = 0; i < comm_size; i++) {
      if (global_ranks[i] == g_world_rank) {
        my_local_rank_in_comm = i;
        break;
      }
    }
    if (my_local_rank_in_comm < 0) { free(global_ranks); continue; }

    // Find j's local rank in this comm.
    int j_local_rank_in_comm = -1;
    for (int i = 0; i < comm_size; i++) {
      if (global_ranks[i] == j) {
        j_local_rank_in_comm = i;
        break;
      }
    }
    JASSERT(j_local_rank_in_comm >= 0)(j)(virtComm)
      .Text("Blocked rank not a member of its own pending-Recv comm.");

    // Determine the sender's local rank in this comm.
    int sender_local_rank_in_comm;
    if ((int)source[j] != MPI_ANY_SOURCE) {
      // If the pending recv has a specific source, use the source
      // to send the dummy message.
      sender_local_rank_in_comm = (int)source[j];
    } else {
      // If the pending recv uses MPI_ANY_SOURCE, pick the process
      // in the same communicator that has the smallest rank number
      // and is not blocked.
      sender_local_rank_in_comm = -1;
      for (int i = 0; i < comm_size; i++) {
        int global = global_ranks[i];
        if (!active[global]) {
          sender_local_rank_in_comm = i;
          break;
        }
      }
      JASSERT(sender_local_rank_in_comm >= 0)(j)(virtComm)
        .Text("MPI_ANY_SOURCE Recv with no unblocked sender in comm; "
              "user program may have deadlocked.");
    }

    if (sender_local_rank_in_comm != my_local_rank_in_comm) {
      free(global_ranks);
      continue;
    }

    // Zero-byte MPI_BYTE dummy: the receiver's datatype handle is valid
    // only on the receiver, and a non-empty dummy would overwrite the
    // user's receive buffer.
    int dummy_tag = ((int)tag[j] == MPI_ANY_TAG) ? 0 : (int)tag[j];
    char dummy_buf;
    MPI_Datatype realByte =
      get_real_id((mana_mpi_handle){.datatype = MPI_BYTE}).datatype;

    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    int rc = NEXT_FUNC(Send)(&dummy_buf, 0, realByte,
                             j_local_rank_in_comm, dummy_tag, realComm);
    JASSERT(rc == MPI_SUCCESS)(rc)(j)(dummy_tag);
    RETURN_TO_UPPER_HALF();
    free(global_ranks);
  }

  // Phase D: wait for all dummies to have been issued globally.
  dmtcp_global_barrier("MPI:P2P-Pending-Recv-Dummies-Sent");
}

void
drainP2p()
{
  drainInFlightP2p();
  unblockPendingRecvs();
}

void
resetDrainCounters()
{
  // p2p_dummy_phase is cleared on EVENT_RESUME and EVENT_RESTART
  // (where this function is called from mpi_plugin_event_hook).  This
  // releases any MPI_Recv wrappers that were parked in their
  // wait-for-resume loop after consuming a dummy.
  p2p_dummy_phase = false;
  g_pending_recv.active = false;
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
