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

#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"

#include "mpi_plugin.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"
// To support MANA_P2P_LOG and MANA_P2P_REPLAY:
#include "p2p-deterministic.h"

extern int p2p_deterministic_skip_save_request;

USER_DEFINED_WRAPPER(int, Send,
                     (const void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) dest, (int) tag, (MPI_Comm) comm)
{
  int retval;
#if 0
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Send)(buf, count, realType, dest, tag, realComm);
  RETURN_TO_UPPER_HALF();
  updateLocalSends(count);
  DMTCP_PLUGIN_ENABLE_CKPT();
#else
  MPI_Request req;
  MPI_Status st;
  retval = MPI_Isend(buf, count, datatype, dest, tag, comm, &req);
  if (retval != MPI_SUCCESS) {
    return retval;
  }
  p2p_deterministic_skip_save_request = 1;
  retval = MPI_Wait(&req, &st);
  p2p_deterministic_skip_save_request = 0;
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
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Isend)(buf, count, realType, dest, tag, realComm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    // Updating global counter of send bytes
    int size;
    MPI_Type_size(datatype, &size);
    int worldRank = localRankToGlobalRank(dest, comm);
    g_sendBytesByRank[worldRank] += count * size;
    // For debugging
#if 0
    printf("rank %d sends %d bytes to rank %d\n", g_world_rank, count * size, worldRank);
    fflush(stdout);
#endif
    // Virtualize request
    MPI_Request virtRequest = ADD_NEW_REQUEST(*request);
    *request = virtRequest;
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
  // FIXME: Implement this wrapper with MPI_Irsend
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Rsend)(ibuf, count, realType, dest, tag, realComm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    // Updating global counter of send bytes
    int size;
    MPI_Type_size(datatype, &size);
    int worldRank = localRankToGlobalRank(dest, comm);
    g_sendBytesByRank[worldRank] += count * size;
    g_rsendBytesByRank[worldRank] += count * size;
    // For debugging
#if 0
    printf("rank %d rsends %d bytes to rank %d\n", g_world_rank, count * size, worldRank);
    fflush(stdout);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Recv,
                     (void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) source, (int) tag,
                     (MPI_Comm) comm, (MPI_Status *) status)
{
  int retval;
#if 0
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Recv)(buf, count, realType, source, tag, realComm, status);
  RETURN_TO_UPPER_HALF();
#else
  MPI_Request req;
  retval = MPI_Irecv(buf, count, datatype, source, tag, comm, &req);
  if (retval != MPI_SUCCESS) {
    return retval;
  }
  p2p_deterministic_skip_save_request = 0;
  retval = MPI_Wait(&req, status);
#endif
  // updateLocalRecvs();
#if 0
  DMTCP_PLUGIN_ENABLE_CKPT();
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
  int size = 0;
  MPI_Status status;

  retval = MPI_Type_size(datatype, &size);
  size = size * count;

  DMTCP_PLUGIN_DISABLE_CKPT();

  if (mana_state == RUNNING &&
      isBufferedPacket(source, tag, comm, &flag, &status)) {
    consumeBufferedPacket(buf, count, datatype, source, tag, comm,
                          &status, size);

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
    MPI_Request virtRequest = ADD_NEW_REQUEST(MPI_REQUEST_NULL);
    UPDATE_REQUEST_MAP(virtRequest, MPI_REQUEST_NULL);
    *request = virtRequest;
    retval = MPI_SUCCESS;
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  }
  LOG_PRE_Irecv(&status);
  REPLAY_PRE_Irecv(count,datatype,source,tag,comm);

  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Irecv)(buf, count, realType,
                            source, tag, realComm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    MPI_Request virtRequest = ADD_NEW_REQUEST(*request);
    *request = virtRequest;
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
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
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
