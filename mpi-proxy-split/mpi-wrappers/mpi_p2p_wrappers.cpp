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

USER_DEFINED_WRAPPER(int, Send,
                     (const void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) dest, (int) tag, (MPI_Comm) comm)
{
  int retval;
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  local_sent_messages++;
  MPI_Comm realComm = get_real_id((mana_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_handle){.datatype = datatype}).datatype;
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

USER_DEFINED_WRAPPER(int, Isend,
                     (const void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) dest, (int) tag,
                     (MPI_Comm) comm, (MPI_Request *) request)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  local_sent_messages++;
  MPI_Comm realComm = get_real_id((mana_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_handle){.datatype = datatype}).datatype;
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

USER_DEFINED_WRAPPER(int, Rsend, (const void*) ibuf, (int) count,
                     (MPI_Datatype) datatype, (int) dest,
                     (int) tag, (MPI_Comm) comm)
{
  int retval;
  while (mana_state == CKPT_P2P) {
    usleep(100);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  local_sent_messages++;
  MPI_Comm realComm = get_real_id((mana_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_handle){.datatype = datatype}).datatype;
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

USER_DEFINED_WRAPPER(int, Recv,
                     (void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) source, (int) tag,
                     (MPI_Comm) comm, (MPI_Status *) status)
{
  int retval;
  int flag = 0;
  // while (mana_state == CKPT_P2P) {
  //   usleep(100);
  // }
  local_recv_messages++;
  if (mana_state == RUNNING &&
      existsMatchingMsgBuffer(source, tag, comm, &flag, status)) {
    int type_size;
    retval = MPI_Type_size(datatype, &type_size);
    int msg_size = type_size * count;
    consumeMatchingMsgBuffer(buf, count, datatype, source, tag, comm,
                             status, msg_size);
    retval = MPI_SUCCESS;
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  }
  while (!flag) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = get_real_id((mana_handle){.comm = comm}).comm;
    MPI_Datatype realType = get_real_id((mana_handle){.datatype = datatype}).datatype;
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    for (int i = 0; i < 1000; i++) {
      NEXT_FUNC(Iprobe)(source, tag, realComm, &flag, status);
      if (flag) {
        retval = NEXT_FUNC(Recv)(buf, count, realType, source, tag, realComm,
                                 status);
        break; // We're done now.  MPI_Recv is finished.
      }
    }
    if (flag) { break; } // Break if we're done.
    // If msg not available yet, slow down and don't hog the CPU core.
    const struct timespec microsecond = {.tv_sec = 0, .tv_nsec = 1000000 };
    nanosleep( &microsecond, NULL );
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
  }
#ifdef DEBUG_P2P
  // FIXME: Move the recv counter code from MPI_Test wrapper to here.
#endif
  return retval;
}

USER_DEFINED_WRAPPER(int, Irecv,
                     (void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) source, (int) tag,
                     (MPI_Comm) comm, (MPI_Request *) request)
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
    mana_handle real_request_null;
    real_request_null.request = MPI_REQUEST_NULL;
    update_virt_id((mana_handle){.request = virtRequest}, real_request_null);
    *request = virtRequest;
    retval = MPI_SUCCESS;
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  }
  LOG_PRE_Irecv(&status);
  REPLAY_PRE_Irecv(count,datatype,source,tag,comm);

  MPI_Comm realComm = get_real_id((mana_handle){.comm = comm}).comm;
  MPI_Datatype realType = get_real_id((mana_handle){.datatype = datatype}).datatype;
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

// FIXME: Move this to mpi_collective_wrappers.cpp and reimplement
USER_DEFINED_WRAPPER(int, Sendrecv, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (int) dest,
                     (int) sendtag, (void *) recvbuf,
                     (int) recvcount, (MPI_Datatype) recvtype, (int) source,
                     (int) recvtag, (MPI_Comm) comm, (MPI_Status *) status)
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

USER_DEFINED_WRAPPER(int, Sendrecv_replace, (void *) buf, (int) count,
                     (MPI_Datatype) datatype, (int) dest,
                     (int) sendtag, (int) source,
                     (int) recvtag, (MPI_Comm) comm, (MPI_Status *) status)
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


PMPI_IMPL(int, MPI_Send, const void *buf, int count, MPI_Datatype datatype,
          int dest, int tag, MPI_Comm comm)
PMPI_IMPL(int, MPI_Isend, const void *buf, int count, MPI_Datatype datatype,
          int dest, int tag, MPI_Comm comm, MPI_Request* request)
PMPI_IMPL(int, MPI_Recv, void *buf, int count, MPI_Datatype datatype,
          int source, int tag, MPI_Comm comm, MPI_Status *status)
PMPI_IMPL(int, MPI_Irecv, void *buf, int count, MPI_Datatype datatype,
          int source, int tag, MPI_Comm comm, MPI_Request *request)
PMPI_IMPL(int, MPI_Sendrecv, const void *sendbuf, int sendcount,
          MPI_Datatype sendtype, int dest, int sendtag, void *recvbuf,
          int recvcount, MPI_Datatype recvtype, int source, int recvtag,
          MPI_Comm comm, MPI_Status *status)
PMPI_IMPL(int, MPI_Sendrecv_replace, void * buf, int count,
          MPI_Datatype datatype, int dest, int sendtag, int source,
          int recvtag, MPI_Comm comm, MPI_Status *status)
PMPI_IMPL(int, MPI_Rsend, const void *ibuf, int count, MPI_Datatype datatype,
          int dest, int tag, MPI_Comm comm)
