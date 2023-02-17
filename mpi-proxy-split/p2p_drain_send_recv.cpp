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
#include <mpi.h>
#include <map>
#include <algorithm>
#include <vector>
#include "jassert.h"
#include "p2p_drain_send_recv.h"
#include "p2p_log_replay.h"
#include "split_process.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"

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
int *g_sendBytesByRank; // Number of bytes sent to other ranks
int *g_rsendBytesByRank; // Number of bytes sent to other ranks by MPI_rsend
int *g_bytesSentToUsByRank; // Number of bytes other ranks sent to us
int *g_recvBytesByRank; // Number of bytes received from other ranks
std::unordered_set<MPI_Comm> active_comms;
dmtcp::vector<mpi_message_t*> g_message_queue;

void
initialize_drain_send_recv()
{
  getLocalRankInfo();
  g_sendBytesByRank = (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  g_rsendBytesByRank = (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  g_bytesSentToUsByRank =
    (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  g_recvBytesByRank = (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  memset(g_sendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_rsendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_bytesSentToUsByRank, 0, g_world_size * sizeof(int));
  memset(g_recvBytesByRank, 0, g_world_size * sizeof(int));
  active_comms.insert(MPI_COMM_WORLD);
  active_comms.insert(MPI_COMM_SELF);
}

void
registerLocalSendsAndRecvs()
{
  // Get a copy of MPI_COMM_WORLD
  MPI_Group group_world;
  MPI_Comm mana_comm;
  MPI_Comm_group(MPI_COMM_WORLD, &group_world);
  MPI_Comm_create_group_internal(MPI_COMM_WORLD, group_world, 1, &mana_comm);

  // broadcast sendBytes and recvBytes
  MPI_Alltoall_internal(g_sendBytesByRank, 1, MPI_INT,
                        g_bytesSentToUsByRank, 1, MPI_INT, mana_comm);
  g_bytesSentToUsByRank[g_world_rank] = 0;

  // Free resources
  MPI_Comm_free_internal(&mana_comm);
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
  int retval = MPI_Recv(buf, count, MPI_BYTE, status.MPI_SOURCE, status.MPI_TAG,
           comm, MPI_STATUS_IGNORE);
  JASSERT(retval == MPI_SUCCESS);

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
        g_recvBytesByRank[worldRank] += call->count * size;
        bytesReceived += call->count * size;
      }
      UPDATE_REQUEST_MAP(request, MPI_REQUEST_NULL);
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
    // if the color is specified on only one side of the inter-communicator, or
    // specified as MPI_UNDEFINED by the program. In this case, the MPI function
    // still returns MPI_SUCCESS, so the MPI_COMM_NULL can be added to the
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

bool
allDrained()
{
  int i;
  // Return false if number of bytes that should be sent to this rank
  // and number of bytes that has been received do not match.
  for (i = 0; i < g_world_size; i++) {
    if (g_bytesSentToUsByRank[i] > g_recvBytesByRank[i]) {
      return false;
    }
  }
  // Returns false if MPI_Isend request is found.
  dmtcp::map<MPI_Request, mpi_nonblocking_call_t*>::iterator it;
  for (it = g_nonblocking_calls.begin(); it != g_nonblocking_calls.end();) {
    MPI_Request request = it->first;
    mpi_nonblocking_call_t *call = it->second;
    if (call->type == ISEND_REQUEST) {
      return false;
    } else {
      it++;
    }
  }
  return true;
}
    
void
drainSendRecv()
{
  int numReceived = -1;
  while (numReceived != 0) {
    while (!allDrained()) {
      // If pending MPI_Irecv or MPI_Isend, use MPI_Test to try to complete it.
      completePendingP2pRequests();
      // If MPI_Irecv not posted but msg was sent, use MPI_Iprobe to drain msg 
      drainRemainingP2pMsgs();
    }
    sleep(5);
    numReceived = 0;
    numReceived = completePendingP2pRequests();
    // If any messages are completed aftera  5 seconds sleep,
    // go back to the loop.
  }
}

// FIXME: isBufferedPacket and consumeBufferedPacket both search
// in the g_message_queue with the same condition. Maybe we can
// combine them into one function.
bool
isBufferedPacket(int source, int tag, MPI_Comm comm, int *flag,
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
consumeBufferedPacket(void *buf, int count, MPI_Datatype datatype,
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
  // isBufferedPacket())!
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

void
resetDrainCounters()
{
  memset(g_sendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_rsendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_bytesSentToUsByRank, 0, g_world_size * sizeof(int));
  memset(g_recvBytesByRank, 0, g_world_size * sizeof(int));
}

int
localRankToGlobalRank(int localRank, MPI_Comm localComm)
{
  int worldRank;
  // FIXME: For interface8, use the new architecture.
  // This only works for interface7
  MPI_Group worldGroup, localGroup;
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(localComm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(MPI_COMM_WORLD, &worldGroup);
  NEXT_FUNC(Comm_group)(realComm, &localGroup);
  NEXT_FUNC(Group_translate_ranks)(localGroup, 1, &localRank,
                                   worldGroup, &worldRank); 
  NEXT_FUNC(Group_free)(&worldGroup);
  NEXT_FUNC(Group_free)(&localGroup);
  RETURN_TO_UPPER_HALF();
  return worldRank;
}
