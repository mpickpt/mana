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
#include "lower_half_ckpt.h"

#include "mpi_plugin.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "mpi_nextfunc.h"
#include "virtual_id.h"

extern "C" {

static int
MPI_Irecv_internal(void *buf, int count, MPI_Datatype datatype,
                   int source, int tag, MPI_Comm comm, MPI_Request *request);

// MPI_Send with MANA_P2P_WAIT=polling (see g_p2p_wait): MPI_Isend, then
// MANA's MPI_Wait.
static int
send_by_polling(const void *buf, int count, MPI_Datatype datatype, int dest,
                int tag, MPI_Comm comm)
{
  MPI_Request request;
  int retval = PMPI_Isend(buf, count, datatype, dest, tag, comm, &request);
  if (retval != MPI_SUCCESS) {
    return retval;
  }
  return PMPI_Wait(&request, MPI_STATUS_IGNORE);
}

// MPI_Recv with MANA_P2P_WAIT=polling: MPI_Irecv, then MANA's MPI_Wait.  A
// message in MANA's buffer completes the request at once.
static int
recv_by_polling(void *buf, int count, MPI_Datatype datatype, int source,
                int tag, MPI_Comm comm, MPI_Status *status)
{
  // As in MPI_Send: don't start a receive while the P2P drain runs.
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  MPI_Request request;
  LOWER_HALF_DISABLE_CKPT();
  int retval = MPI_Irecv_internal(buf, count, datatype, source, tag, comm,
                                  &request);
  LOWER_HALF_ENABLE_CKPT();
  if (retval != MPI_SUCCESS) {
    return retval;
  }
  return PMPI_Wait(&request, status);
}

#pragma weak MPI_Send = PMPI_Send
int PMPI_Send(const void *buf, int count, MPI_Datatype datatype,
             int dest, int tag, MPI_Comm comm)
{
  if (g_p2p_wait == P2P_WAIT_POLLING) {
    return send_by_polling(buf, count, datatype, dest, tag, comm);
  }
  int retval;
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  LOWER_HALF_DISABLE_CKPT();
  // A message to MPI_PROC_NULL is never received: don't count it.
  if (dest != MPI_PROC_NULL) {
    local_sent_messages++;
  }
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Send)(buf, count, realType, dest, tag, realComm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
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

// The body of MPI_Isend.  The caller has waited out the P2P drain (see
// MPI_Isend) and called LOWER_HALF_DISABLE_CKPT().
static int
MPI_Isend_internal(const void *buf, int count, MPI_Datatype datatype,
                   int dest, int tag, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  if (dest != MPI_PROC_NULL) {
    local_sent_messages++;
  }
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
  return retval;
}

#pragma weak MPI_Isend = PMPI_Isend
int PMPI_Isend(const void *buf, int count, MPI_Datatype datatype,
              int dest, int tag,
              MPI_Comm comm, MPI_Request *request)
{
  int retval;
  // As in MPI_Send: don't start a send while the P2P drain runs; the drain
  // might not count it, and it would be in flight in the checkpoint image.
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  LOWER_HALF_DISABLE_CKPT();
  retval = MPI_Isend_internal(buf, count, datatype, dest, tag, comm, request);
  LOWER_HALF_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Rsend = PMPI_Rsend
int PMPI_Rsend(const void* ibuf, int count,
              MPI_Datatype datatype, int dest,
              int tag, MPI_Comm comm)
{
  // A ready send may be done as a standard one; with MANA_P2P_WAIT=polling,
  // no thread may wait in the lower half (see DMTCP_EVENT_PRESUSPEND).
  if (g_p2p_wait == P2P_WAIT_POLLING) {
    return send_by_polling(ibuf, count, datatype, dest, tag, comm);
  }
  int retval;
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  LOWER_HALF_DISABLE_CKPT();
  if (dest != MPI_PROC_NULL) {
    local_sent_messages++;
  }
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
  LOWER_HALF_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Recv = PMPI_Recv
int PMPI_Recv(void *buf, int count, MPI_Datatype datatype,
             int source, int tag, MPI_Comm comm, MPI_Status *status)
{
  // With MANA_P2P_WAIT=polling, see recv_by_polling().  Otherwise:
  //   1. Serve a matching message from MANA's buffer, if any.
  //   2. Publish the receive in g_pending_recv (IDLE -> ACTIVE), so that a
  //      checkpoint's unblockPendingRecvs() sends this rank a dummy.
  //   3. Call NEXT_FUNC(Recv), then read p2p_dummy_phase once, before
  //      LOWER_HALF_ENABLE_CKPT() (resume and restart clear it).  Its
  //      declaration explains why one read suffices.
  //   4. On a dummy, wait for the checkpoint to end and retry.
  // Steps 1-3 run inside LOWER_HALF_DISABLE_CKPT().  Pre-suspend still runs
  // while we block: it waits for threads to leave the lower half only after
  // sending the dummies.  The single g_pending_recv slot assumes no
  // MPI_THREAD_MULTIPLE.

  get_fortran_constants();  // For FORTRAN_MPI_STATUS_IGNORE
  if (g_p2p_wait == P2P_WAIT_POLLING) {
    return recv_by_polling(buf, count, datatype, source, tag, comm, status);
  }
  int retval = MPI_SUCCESS;
  int flag = 0;
  // A receive from MPI_PROC_NULL returns at once.  Don't publish it in
  // g_pending_recv: the drain would take it for a blocked receive.
  if (source == MPI_PROC_NULL) {
    MPI_Status local_status;
    LOWER_HALF_DISABLE_CKPT();
    MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
    MPI_Datatype realType =
      get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    retval = NEXT_FUNC(Recv)(buf, count, realType, source, tag, realComm,
                             &local_status);
    RETURN_TO_UPPER_HALF();
    LOWER_HALF_ENABLE_CKPT();
    if (status != MPI_STATUS_IGNORE && status != FORTRAN_MPI_STATUS_IGNORE) {
      *status = local_status;
    }
    return retval;
  }

retry:
  // As in MPI_Send: don't start a receive while the P2P drain runs; it
  // could take a later message ahead of one that the drain buffers.
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  LOWER_HALF_DISABLE_CKPT();
  // Step 1: serve from the MANA-internal buffer if a matching message
  // was drained during a previous pre-suspend cycle.
  // The buffer functions write a status, and 'status' may be
  // MPI_STATUS_IGNORE: use a local one.
  MPI_Status buffered_status;
  if (existsMatchingMsgBuffer(source, tag, comm, &flag, &buffered_status)) {
    int type_size;
    MPI_Type_size(datatype, &type_size);
    int msg_size = type_size * count;
    consumeMatchingMsgBuffer(buf, count, datatype, source, tag, comm,
                             &buffered_status, msg_size);
    // Don't count the message in local_recv_messages: the P2P drain counted
    // it when it moved it to the buffer (recvMsgIntoInternalBuffer()).
    if (status != MPI_STATUS_IGNORE && status != FORTRAN_MPI_STATUS_IGNORE) {
      *status = buffered_status;
    }
    LOWER_HALF_ENABLE_CKPT();
    return MPI_SUCCESS;
  }

  // Step 2: publish the pending-Recv slot.  The compare-and-swap publishes
  // the fields and decides the race with unblockPendingRecvs(), which
  // moves the state from IDLE to CLOSED.
  g_pending_recv.source = source;
  g_pending_recv.tag = tag;
  g_pending_recv.comm = comm;
  g_pending_recv.count = count;
  g_pending_recv.datatype = datatype;
  int idle = PENDING_RECV_IDLE;
  if (!__atomic_compare_exchange_n(&g_pending_recv.state, &idle,
                                   PENDING_RECV_ACTIVE, false,
                                   __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
    // CLOSED: the P2P drain has begun, so no dummy would come.  Wait in the
    // upper half for the checkpoint to end, then retry (the message may be
    // in MANA's buffer by then).
    LOWER_HALF_ENABLE_CKPT();
    while (mana_state != RUNNING) {
      usleep(100);
    }
    goto retry;
  }

  // Step 3: resolve virtual handles and call into the lower half, where we
  // may block until the message, or a dummy, arrives.
  MPI_Status local_status;
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Recv)(buf, count, realType, source, tag, realComm,
                           &local_status);
  RETURN_TO_UPPER_HALF();
  bool dummy = p2p_dummy_phase;
  if (dummy) {
    // Do NOT increment local_recv_messages (the dummy bypassed the
    // MPI_Send wrapper on the sender, so global counters stay balanced
    // only if we also skip the increment here).
    __atomic_store_n(&g_pending_recv.state, PENDING_RECV_CLOSED,
                     __ATOMIC_RELEASE);
  } else {
    // Go back to IDLE before counting the message: once the drain has
    // counted it, unblockPendingRecvs() must not see this MPI_Recv as
    // blocked (no MPI_Recv would take its dummy).
    __atomic_store_n(&g_pending_recv.state, PENDING_RECV_IDLE,
                     __ATOMIC_RELEASE);
    if (source != MPI_PROC_NULL) {
      count_received_message();
    }
  }
  LOWER_HALF_ENABLE_CKPT();

  // Step 4: act on the classification.
  if (dummy) {
    // Discard the dummy and wait, outside LOWER_HALF_DISABLE_CKPT(), for
    // the checkpoint to end.  Resume and restart call resetDrainCounters()
    // (clearing p2p_dummy_phase, reopening the slot) before RUNNING.
    while (mana_state != RUNNING) {
      usleep(100);
    }
    goto retry;
  }

  // Real message.
  if (status != MPI_STATUS_IGNORE && status != FORTRAN_MPI_STATUS_IGNORE) {
    *status = local_status;
  }
  return retval;
}

// The body of MPI_Irecv.  The caller has called LOWER_HALF_DISABLE_CKPT().
static int
MPI_Irecv_internal(void *buf, int count, MPI_Datatype datatype,
                   int source, int tag, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  int flag = 0;
  MPI_Status status;

  if (existsMatchingMsgBuffer(source, tag, comm, &flag, &status)) {
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
    // The request keeps the message's status (complete_virt_request()).
    MPI_Request virtRequest = new_virt_request((MPI_Request)((intptr_t)MPI_REQUEST_NULL+1));
    complete_virt_request(virtRequest, &status);
    *request = virtRequest;
    retval = MPI_SUCCESS;
    return retval;
  }

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
  return retval;
}

#pragma weak MPI_Irecv = PMPI_Irecv
int PMPI_Irecv(void *buf, int count, MPI_Datatype datatype,
              int source, int tag, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  // As in MPI_Recv.
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  LOWER_HALF_DISABLE_CKPT();
  retval = MPI_Irecv_internal(buf, count, datatype, source, tag, comm,
                              request);
  LOWER_HALF_ENABLE_CKPT();
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
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id(comm).real_comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Sendrecv)(sendbuf, sendcount, sendtype, dest, sendtag,
                               recvbuf, recvcount, recvtype, source, recvtag,
                               realComm, status);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
#else
  get_fortran_constants();
  MPI_Request reqs[2];
  MPI_Status sts[2];
  // As in MPI_Isend, don't start the send while the P2P drain runs.
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  // FIXME: The send and receive need to be atomic
  // Post both requests under one LOWER_HALF_DISABLE_CKPT().
  LOWER_HALF_DISABLE_CKPT();
  retval = MPI_Isend_internal(sendbuf, sendcount, sendtype, dest,
                              sendtag, comm, &reqs[0]);
  if (retval == MPI_SUCCESS) {
    retval = MPI_Irecv_internal(recvbuf, recvcount, recvtype, source,
                                recvtag, comm, &reqs[1]);
  }
  LOWER_HALF_ENABLE_CKPT();
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

  // As in MPI_Sendrecv: wait out the P2P drain, then post both requests
  // under one LOWER_HALF_DISABLE_CKPT().
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  LOWER_HALF_DISABLE_CKPT();
  // Recv into temp buffer to avoid overwriting
  retval = MPI_Irecv_internal(tmpbuf, count, datatype, source, recvtag, comm,
                              &reqs[0]);
  if (retval == MPI_SUCCESS) {
    // Send from original buffer
    retval = MPI_Isend_internal(buf, count, datatype, dest, sendtag, comm,
                                &reqs[1]);
  }
  LOWER_HALF_ENABLE_CKPT();
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
