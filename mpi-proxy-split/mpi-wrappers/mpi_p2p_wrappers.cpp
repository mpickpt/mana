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

#include <time.h>
#include <unistd.h>
#include "config.h"
#include "dmtcp.h"
#include "jassert.h"

#include "mpi_plugin.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "mpi_nextfunc.h"
#include "virtual_id.h"
#include "record-replay.h"
// To support MANA_P2P_LOG and MANA_P2P_REPLAY:
#include "p2p-deterministic.h"

extern int p2p_deterministic_skip_save_request;

extern "C" {

#pragma weak MPI_Send = PMPI_Send
int PMPI_Send(const void *buf, int count, MPI_Datatype datatype,
             int dest, int tag, MPI_Comm comm)
{
  int retval;
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  local_sent_messages++;
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Send)(buf, count, realType, dest, tag, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
#ifdef DEBUG_P2P
  if (retval == MPI_SUCCESS) {
    // Updating global counter of send bytes
    int size;
    MPI_Type_size(datatype, &size);
    int worldRank = localRankToGlobalRank(dest, comm);
    g_sendBytesByRank[worldRank] += count * size;
  }
#endif
  return retval;
}

#pragma weak MPI_Isend = PMPI_Isend
int PMPI_Isend(const void *buf, int count, MPI_Datatype datatype,
              int dest, int tag,
              MPI_Comm comm, MPI_Request *request)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  local_sent_messages++;
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Isend)(buf, count, realType, dest, tag, realComm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
#ifdef DEBUG_P2P
    // Updating global counter of send bytes
    int size;
    MPI_Type_size(datatype, &size);
    int worldRank = localRankToGlobalRank(dest, comm);
    g_sendBytesByRank[worldRank] += count * size;
    printf("rank %d sends %d bytes to rank %d\n", g_world_rank, count * size, worldRank);
    fflush(stdout);
#endif
    // Virtualize request
    *request = new_virt_request(*request);
    addPendingRequestToLog(ISEND_REQUEST, buf, NULL, count,
                           datatype, dest, tag, comm, *request);
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, ISEND_REQUEST);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Rsend = PMPI_Rsend
int PMPI_Rsend(const void* ibuf, int count,
              MPI_Datatype datatype, int dest,
              int tag, MPI_Comm comm)
{
  int retval;
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  local_sent_messages++;
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Rsend)(ibuf, count, realType, dest, tag, realComm);
  RETURN_TO_UPPER_HALF();
#ifdef DEBUG_P2P
  if (retval == MPI_SUCCESS) {
    // Updating global counter of send bytes
    int size;
    MPI_Type_size(datatype, &size);
    int worldRank = localRankToGlobalRank(dest, comm);
    g_sendBytesByRank[worldRank] += count * size;
    g_rsendBytesByRank[worldRank] += count * size;
  }
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Recv = PMPI_Recv
int PMPI_Recv(void *buf, int count, MPI_Datatype datatype,
             int source, int tag, MPI_Comm comm, MPI_Status *status)
{
  // MANA does not support MPI_THREAD_MULTIPLE.  This wrapper relies on
  // a single global pending-Recv slot (g_pending_recv) being claimed
  // by at most one thread at a time.
  //
  // Protocol overview (replaces the older MPI_Iprobe polling loop):
  //
  //   1. Check the MANA-internal message buffer first.  Messages
  //      drained from in-flight Sends during a previous checkpoint's
  //      pre-suspend (via recvMsgIntoInternalBuffer) are served here.
  //
  //   2. Publish (source, tag, comm, count, datatype) to g_pending_recv
  //      with active=true, so that unblockPendingRecvs(), running on
  //      the DMTCP coordinator thread during a future pre-suspend, can
  //      identify this rank as blocked and arrange for a matching dummy
  //      MPI_Send to unblock us.
  //
  //   3. Call NEXT_FUNC(Recv) WITHOUT bracketing it in
  //      DMTCP_PLUGIN_DISABLE_CKPT.  The DMTCP coordinator thread must
  //      be able to run the pre-suspend hook (and dispatch a dummy)
  //      while this thread is blocked in the lower-half MPI_Recv.
  //
  //   4. After NEXT_FUNC(Recv) returns, read p2p_dummy_phase.  If true,
  //      the message we just received was a dummy injected by
  //      unblockPendingRecvs; discard it, park until mana_state ==
  //      RUNNING (resume/restart complete), and retry.  Otherwise the
  //      message is real: increment local_recv_messages, deliver
  //      status, and return.
  //
  // The reason a single post-call read of p2p_dummy_phase is sufficient
  // is documented at the declaration of p2p_dummy_phase in
  // p2p_drain_send_recv.h.  Briefly: unblockPendingRecvs only sets
  // p2p_dummy_phase = true AFTER drainInFlightP2p() exits, which requires
  // this rank's local_recv_messages to have caught up with local_send_messages
  // which only happens after we have already incremented for any real
  // message; hence the post-call read is correctly false for real
  // messages and (by the dispatch-after-barrier ordering in
  // unblockPendingRecvs) correctly true for dummies.

  int retval = MPI_SUCCESS;
  int flag = 0;

retry:
  // Step 1: serve from the MANA-internal buffer if a matching message
  // was drained during a previous pre-suspend cycle.
  if (mana_state == RUNNING &&
      existsMatchingMsgBuffer(source, tag, comm, &flag, status)) {
    int type_size;
    MPI_Type_size(datatype, &type_size);
    int msg_size = type_size * count;
    consumeMatchingMsgBuffer(buf, count, datatype, source, tag, comm,
                             status, msg_size);
    local_recv_messages++;
    return MPI_SUCCESS;
  }

  // Step 2: publish the pending-Recv slot.  Field writes precede the
  // 'active = true' write so that unblockPendingRecvs sees a fully
  // populated record once it observes active==true.  (volatile bool
  // active gives single-flag visibility; the field reads in
  // unblockPendingRecvs are gated on active and synchronized by the
  // global barrier inside that function.)
  g_pending_recv.source = source;
  g_pending_recv.tag = tag;
  g_pending_recv.comm = comm;
  g_pending_recv.count = count;
  g_pending_recv.datatype = datatype;
  g_pending_recv.active = true;

  // Step 3: resolve virtual handles and call into the lower half.
  // No DMTCP_PLUGIN_DISABLE_CKPT bracket here (see protocol overview
  // above): the pre-suspend hook must be able to fire while we are
  // blocked in NEXT_FUNC(Recv) so it can dispatch a dummy to
  // unblock us.
  MPI_Status local_status;
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Recv)(buf, count, realType, source, tag, realComm,
                           &local_status);
  RETURN_TO_UPPER_HALF();

  // Step 4: classify as real or dummy.
  if (p2p_dummy_phase) {
    // Dummy.  buf and local_status contain unusable data; discard.
    // Do NOT increment local_recv_messages (the dummy bypassed the
    // MPI_Send wrapper on the sender, so global counters stay balanced
    // only if we also skip the increment here).
    g_pending_recv.active = false;
    // Park until the checkpoint completes and we are back in the
    // RUNNING state.  EVENT_RESUME and EVENT_RESTART both transition
    // mana_state back to RUNNING after resetDrainCounters() clears
    // p2p_dummy_phase.  Checkpointing is enabled during this wait
    // loop (we hold no DMTCP_PLUGIN_DISABLE_CKPT), so DMTCP can
    // suspend the user thread here cleanly.
    while (mana_state != RUNNING) {
      usleep(100);
    }
    goto retry;
  }

  // Real message.
  local_recv_messages++;
  g_pending_recv.active = false;
  if (status != MPI_STATUS_IGNORE) {
    *status = local_status;
  }
  return retval;
}

#pragma weak MPI_Irecv = PMPI_Irecv
int PMPI_Irecv(void *buf, int count, MPI_Datatype datatype,
              int source, int tag, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  int flag = 0;
  MPI_Status status;

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (mana_state == RUNNING &&
      existsMatchingMsgBuffer(source, tag, comm, &flag, &status)) {
    int type_size;
    retval = MPI_Type_size(datatype, &type_size);
    int msg_size = type_size * count;
    consumeMatchingMsgBuffer(buf, count, datatype, source, tag, comm,
                             &status, msg_size);

    // Use (MPI_REQUEST_NULL+1) as a fake non-null real request to create
    // a new non-null virtual request and map the virtual request to the
    // real request MPI_REQUEST_NULL.
    // A bug can occur in the following situation:
    //   MPI_Irecv(..., &request, ...);
    //   array_of_requests[0] = &request;
    //   # ckpt-request or ckpt-resume occurs here
    //   MPI_Waitany(1, array_of_requests, index, ...);
    //   # And the bug occurs with MPI_Waitsome(..., outcount, ...),
    //   #   and with MPI_Testany and MPI_Testsome.
    // The bug occurs when MANA drains the network of messages during ckpt.
    // MANA calls MPI_Wait to receive the network MPI message, and MPI_Wait
    //   then sets the corresponding request to MPI_REQUEST_NULL.
    // But the application doesn't know that MANA "stole" the message.
    // The application is calling MPI_Waitany for the first time,
    //   and then crashes when it gets an invalid index set to MPI_UNDEFINED.
    // And same occurs for MPI_Waitsome, with outcount set to MPI_UNDEFINED.
    // And the same issue occurs for MPI_Testany and MPI_Testsome.
    // FIXME:  We should add to some include file:
    // MPI_REQUEST_FAKE_NULL is needed by the MPI_Waitany wrapper.
    //   #define MPI_REQUEST_FAKE_NULL MPI_REQUEST_NULL + 1
    // FIXME:  In the wrappers for MPI_Waitany/Waitsome/Testany/Testsome
    //    We should add a comment that MPI_REQUEST_FAKE_NULL can occr,
    //    and that the details are in the comments for the MPI_Irecv wrapper.
    MPI_Request virtRequest = new_virt_request((MPI_Request)((intptr_t)MPI_REQUEST_NULL+1));
    mana_mpi_handle real_request_null;
    real_request_null.request = MPI_REQUEST_NULL;
    update_virt_id((mana_mpi_handle){.request = virtRequest}, real_request_null);
    *request = virtRequest;
    retval = MPI_SUCCESS;
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  }
  LOG_PRE_Irecv(&status);
  REPLAY_PRE_Irecv(count,datatype,source,tag,comm);

  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Irecv)(buf, count, realType,
                            source, tag, realComm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_request(*request);
    addPendingRequestToLog(IRECV_REQUEST, NULL, buf, count,
                           datatype, source, tag, comm, *request);
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IRECV_REQUEST);
#endif
  }
  LOG_POST_Irecv(source,tag,comm,&status,request,buf);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Sendrecv = PMPI_Sendrecv
int PMPI_Sendrecv(const void *sendbuf, int sendcount,
                 MPI_Datatype sendtype, int dest,
                 int sendtag, void *recvbuf,
                 int recvcount, MPI_Datatype recvtype, int source,
                 int recvtag, MPI_Comm comm, MPI_Status *status)
{
  int retval;
#if 0
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id(comm).real_comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Sendrecv)(sendbuf, sendcount, sendtype, dest, sendtag,
                               recvbuf, recvcount, recvtype, source, recvtag,
                               realComm, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
#else
  get_fortran_constants();
  MPI_Request reqs[2];
  MPI_Status sts[2];
  // FIXME: The send and receive need to be atomic
  retval = MPI_Isend(sendbuf, sendcount, sendtype, dest,
      sendtag, comm, &reqs[0]);
  if (retval != MPI_SUCCESS) {
    return retval;
  }
  retval = MPI_Irecv(recvbuf, recvcount, recvtype, source,
                     recvtag, comm, &reqs[1]);
  if (retval != MPI_SUCCESS) {
    return retval;
  }
  retval = MPI_Waitall(2, reqs, sts);
  // Set status only when the status is neither MPI_STATUS_IGNORE nor
  // FORTRAN_MPI_STATUS_IGNORE
  if (status != MPI_STATUS_IGNORE && status != FORTRAN_MPI_STATUS_IGNORE) {
    *status = sts[1];
  }
  if (retval == MPI_SUCCESS) {
    // updateLocalRecvs();
  }
#endif
  return retval;
}

#pragma weak MPI_Sendrecv_replace = PMPI_Sendrecv_replace
int PMPI_Sendrecv_replace(void *buf, int count,
                         MPI_Datatype datatype, int dest,
                         int sendtag, int source,
                         int recvtag, MPI_Comm comm, MPI_Status *status)
{
  MPI_Request reqs[2];
  MPI_Status sts[2];

  // Allocate temp buffer
  int type_size, retval;
  MPI_Type_size(datatype, &type_size);
  void* tmpbuf = (void*) malloc(count * type_size);

  // Recv into temp buffer to avoid overwriting
  retval = MPI_Irecv(tmpbuf, count, datatype, source, recvtag, comm, &reqs[0]);
  if (retval != MPI_SUCCESS) {
    free(tmpbuf);
    return retval;
  }

  // Send from original buffer
  retval = MPI_Isend(buf, count, datatype, dest, sendtag, comm, &reqs[1]);
  if (retval != MPI_SUCCESS) {
    free(tmpbuf);
    return retval;
  }

  // Wait on send/recv, then copy from temp into permanent buffer
  retval = MPI_Waitall(2, reqs, sts);
  memcpy(buf, tmpbuf, count * type_size);

  // Set status, free buffer, and return
  if (status != MPI_STATUS_IGNORE && status != FORTRAN_MPI_STATUS_IGNORE) {
    *status = sts[0];
  }
  free(tmpbuf);

  return retval;
}

} // end of: extern "C"
