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
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_plugin.h"
#include "record-replay.h"
#include "mpi_nextfunc.h"
#include "seq_num.h"
#include "virtual-ids.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"

#ifdef MPI_COLLECTIVE_P2P
# include "mpi_collective_p2p.c"
#endif

// Returns true if the environment variable MPI_COLLECTIVE_P2P
//   was set when the MANA plugin was compiled.
//   MPI collective calls will be translated to use MPI_Send/Recv.
bool
isUsingCollectiveToP2p() {
#ifdef MPI_COLLECTIVE_P2P
  return true;
#else
  return false;
#endif
}

using namespace dmtcp_mpi;

#ifndef MPI_COLLECTIVE_P2P
#ifdef NO_BARRIER_BCAST
USER_DEFINED_WRAPPER(int, Bcast,
                     (void *) buffer, (int) count, (MPI_Datatype) datatype,
                     (int) root, (MPI_Comm) comm)
{
  int rank, size;
  int retval = MPI_SUCCESS;
  MPI_Comm_rank(comm, &rank);
  // FIXME: If replacing MPI_Bcast with MPI_Send/Recv is slow,
  // we should still call MPI_Bcast, but treat it as MPI_Send/Recv
  // at checkpoint time. Which means we need to drain MPI_Bcast
  // messages.
  // FIXME: It should be faster to call MPI_Isend/Irecv at once
  // and test requests later.
  if (rank == root) { // sender
    MPI_Comm_size(comm, &size);
    int dest;
    for (dest = 0; dest < size; dest++) {
      if (dest == root) { continue; }
      retval = MPI_Send(buffer, count, datatype, dest, 0, comm);
      if (retval != MPI_SUCCESS) { return retval; }
    }
  } else { // receiver
    retval = MPI_Recv(buffer, count, datatype, root,
                      0, comm, MPI_STATUS_IGNORE);
  }
  return retval;
}
#else
USER_DEFINED_WRAPPER(int, Bcast,
                     (void *) buffer, (int) count, (MPI_Datatype) datatype,
                     (int) root, (MPI_Comm) comm)
{
  bool passthrough = true;
  commit_begin(comm, passthrough);
  int retval;
#if 0 // for debugging
  int size;
  MPI_Type_size(datatype, &size);
  printf("Rank %d: MPI_Bcast sending %d bytes\n", g_world_rank, count * size);
  fflush(stdout);
#endif
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Bcast)(buffer, count, realType, root, realComm);
  // Call lower-half Barrier in the critical section to wait until all rank in the
  // communictor finished.
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}
#endif

USER_DEFINED_WRAPPER(int, Ibcast,
                     (void *) buffer, (int) count, (MPI_Datatype) datatype,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Ibcast)(buffer, count, realType,
      root, realComm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    MPI_Request virtRequest = ADD_NEW_REQUEST(*request);
    *request = virtRequest;
    LOG_CALL(restoreRequests, Ibcast, buffer, count, datatype,
             root, comm, *request);
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IBCAST_REQUEST);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Barrier, (MPI_Comm) comm)
{
  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Barrier)(realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

EXTERNC
USER_DEFINED_WRAPPER(int, Ibarrier, (MPI_Comm) comm, (MPI_Request *) request)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Ibarrier)(realComm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    MPI_Request virtRequest = ADD_NEW_REQUEST(*request);
    *request = virtRequest;
    LOG_CALL(restoreRequests, Ibarrier, comm, *request);
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IBARRIER_REQUEST);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

/*******************************************************************************
 * This version, MPI_Allreduce_reproducible, can be called from
 * the MPI_Allreduce wrapper and returned.  If desired, it could be
 * called selectively on certain sizes or certain types or certain op's.
 * 
 * This function is only meaningful for floating-point datatypes, as
 * floating-point operations are non-associative.
 * 
 * On the Reproducibility of MPI Reduction Operations
 * https://www.mcs.anl.gov/papers/P4093-0713_1.pdf
 * 
 * An optimisation of allreduce communication in message-passing systems
 * https://www.sciencedirect.com/science/article/pii/S0167819121000612
 * 
 * MPI standard:
 * Advice to users. Some applications may not be able to ignore the
 * non-associative nature of floating-point operations or may use
 * user-defined operations (see Section 5.9.5) that require a special
 * reduction order and cannot be treated as associative. Such applications
 * should enforce the order of evaluation explicitly. For example, in the
 * case of operations that require a strict left-to-right (or right-to-left
 * evaluation order, this could be done by gathering all operands at a single
 * process (e.g., with MPI_GATHER), applying the reduction operation in the
 * desired order (e.g., with MPI_REDUCE_LOCAL), and if needed, broadcast or
 * scatter the result to the other processes (e.g., with MPI_BCAST). (End
 * of advice to users.)
 * 
 * Note: MPI_Waitany and MPI_Scan and MPI_Allreduce can receive messages
 * non-deterministically.
 ******************************************************************************/

int
MPI_Allreduce_reproducible(const void *sendbuf,
                           void *recvbuf,
                           int count,
                           MPI_Datatype datatype,
                           MPI_Op op,
                           MPI_Comm comm,
                           int dump_trace_from)
{
#define MAX_ALL_SENDBUF_SIZE (1024 * 1024 * 16) /* 15 MB */
  // We use 'static' becuase we don't want the overhead of the compiler
  // initializing these to zero each time the function is called.

  static unsigned char tmpbuf[MAX_ALL_SENDBUF_SIZE];

  int root = 0;
  int comm_rank;
  int comm_size;
  int type_size;

  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &type_size);

  JASSERT(count * comm_size * type_size <= MAX_ALL_SENDBUF_SIZE);
  JASSERT(sendbuf != FORTRAN_MPI_IN_PLACE && sendbuf != MPI_IN_PLACE)
    .Text("MANA: MPI_Allreduce_reproducible: MPI_IN_PLACE not yet supported.");

  // Gather the operands from all ranks in the comm
  MPI_Gather(sendbuf, count, datatype, tmpbuf, count, datatype, 0, comm);

  // Perform the local reduction operation on the root rank
  if (comm_rank == root) {
    memset(recvbuf, 0, count * type_size);
    memcpy(recvbuf, tmpbuf + (count * type_size * 0), count * type_size);
    for (int i = 1; i < comm_size; i++) {
      MPI_Reduce_local(tmpbuf + (count * type_size * i), recvbuf, count,
                       datatype, op);
    }
  }

  // Broadcat the local reduction operation result in the comm
  int rc = MPI_Bcast(recvbuf, count, datatype, 0, comm);

  return rc;
}

USER_DEFINED_WRAPPER(int, Allreduce,
                     (const void *) sendbuf, (void *) recvbuf,
                     (int) count, (MPI_Datatype) datatype,
                     (MPI_Op) op, (MPI_Comm) comm)
{
  char *s = getenv("MANA_USE_ALLREDUCE_REPRODUCIBLE");
  int use_allreduce_reproducible = (s != NULL) ? atoi(s) : 0;

  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  get_fortran_constants();
  if (use_allreduce_reproducible)
    retval = MPI_Allreduce_reproducible(sendbuf, recvbuf, count, datatype, op,
                                        comm, 0);
  else {
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
    MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
    // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
    if (sendbuf == FORTRAN_MPI_IN_PLACE) {
      sendbuf = MPI_IN_PLACE;
    }
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval =
      NEXT_FUNC(Allreduce)(sendbuf, recvbuf, count, realType, realOp, realComm);
    RETURN_TO_UPPER_HALF();
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Reduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm)
{
  bool passthrough = true;
  commit_begin(comm, passthrough);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Reduce)(sendbuf, recvbuf, count,
                             realType, realOp, root, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Ireduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Ireduce)(sendbuf, recvbuf, count,
      realType, realOp, root, realComm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    MPI_Request virtRequest = ADD_NEW_REQUEST(*request);
    *request = virtRequest;
    LOG_CALL(restoreRequests, Ireduce, sendbuf, recvbuf,
        count, datatype, op, root, comm, *request);
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IREDUCE_REQUEST);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Reduce_scatter,
                     (const void *) sendbuf, (void *) recvbuf,
                     (const int) recvcounts[], (MPI_Datatype) datatype,
                     (MPI_Op) op, (MPI_Comm) comm)
{
  bool passthrough = true;
  commit_begin(comm, passthrough);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Reduce_scatter)(sendbuf, recvbuf, recvcounts,
                                     realType, realOp, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}
#endif // #ifndef MPI_COLLECTIVE_P2P

// NOTE:  This C++ function in needed by p2p_drain_send_recv.cpp
//        both when MPI_COLLECTIVE_P2P is not defined and when it's defined.
//        With MPI_COLLECTIVE_P2P, p2p_drain_send_recv.cpp will need this
//        at checkpoint time, to make a direct call to the lower half, as part
//        of draining the point-to-point MPI calls.  p2p_drain_send_recv.cpp
//        cannot use the C version in mpi-wrappers/mpi_collective_p2p.c,
//        which would generate extra point-to-point MPI calls.
int
MPI_Alltoall_internal(const void *sendbuf, int sendcount,
                      MPI_Datatype sendtype, void *recvbuf, int recvcount,
                      MPI_Datatype recvtype, MPI_Comm comm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Alltoall)(sendbuf, sendcount, realSendType, recvbuf,
      recvcount, realRecvType, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#ifndef MPI_COLLECTIVE_P2P
USER_DEFINED_WRAPPER(int, Alltoall,
                     (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  // sendbuf can propagate MPI_IN_PLACE and FORTRAN_MPI_IN_PLACE
  retval = MPI_Alltoall_internal(sendbuf, sendcount, sendtype,
                                 recvbuf, recvcount, recvtype, comm);
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Alltoallv,
                     (const void *) sendbuf, (const int *) sendcounts,
                     (const int *) sdispls, (MPI_Datatype) sendtype,
                     (void *) recvbuf, (const int *) recvcounts,
                     (const int *) rdispls, (MPI_Datatype) recvtype,
                     (MPI_Comm) comm)
{
  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Alltoallv)(sendbuf, sendcounts, sdispls, realSendType,
                                recvbuf, recvcounts, rdispls, realRecvType,
                                realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Gather, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  bool passthrough = true;
  commit_begin(comm, passthrough);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Gather)(sendbuf, sendcount, realSendType,
                             recvbuf, recvcount, realRecvType,
                             root, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Gatherv, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf,
                     (const int*) recvcounts, (const int*) displs,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  bool passthrough = true;
  commit_begin(comm, passthrough);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Gatherv)(sendbuf, sendcount, realSendType,
                              recvbuf, recvcounts, displs, realRecvType,
                              root, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Scatter, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  bool passthrough = true;
  commit_begin(comm, passthrough);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (recvbuf == FORTRAN_MPI_IN_PLACE) {
    recvbuf = MPI_IN_PLACE;
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Scatter)(sendbuf, sendcount, realSendType,
                              recvbuf, recvcount, realRecvType,
                              root, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Scatterv, (const void *) sendbuf,
                     (const int *) sendcounts, (const int *) displs,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  bool passthrough = true;
  commit_begin(comm, passthrough);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (recvbuf == FORTRAN_MPI_IN_PLACE) {
    recvbuf = MPI_IN_PLACE;
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Scatterv)(sendbuf, sendcounts, displs, realSendType,
                               recvbuf, recvcount, realRecvType,
                               root, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Allgather, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Allgather)(sendbuf, sendcount, realSendType,
                                recvbuf, recvcount, realRecvType,
                                realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Allgatherv, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf,
                     (const int*) recvcounts, (const int *) displs,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Allgatherv)(sendbuf, sendcount, realSendType,
                                 recvbuf, recvcounts, displs, realRecvType,
                                 realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Scan, (const void *) sendbuf, (void *) recvbuf,
                     (int) count, (MPI_Datatype) datatype,
                     (MPI_Op) op, (MPI_Comm) comm)
{
  bool passthrough = true;
  commit_begin(comm, passthrough);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  MPI_Op realOp = VIRTUAL_TO_REAL_TYPE(op);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Scan)(sendbuf, recvbuf, count,
                           realType, realOp, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}
#endif // #ifndef MPI_COLLECTIVE_P2P

// FIXME: Also check the MPI_Cart family, if they use collective communications.
USER_DEFINED_WRAPPER(int, Comm_split, (MPI_Comm) comm, (int) color, (int) key,
    (MPI_Comm *) newcomm)
{
  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_split)(realComm, color, key, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    // VirtualGlobalCommId::instance().createGlobalId(virtComm); TODO 
    *newcomm = virtComm;
    active_comms.insert(virtComm);
    LOG_CALL(restoreComms, Comm_split, comm, color, key, *newcomm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_dup, (MPI_Comm) comm, (MPI_Comm *) newcomm)
{
  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_dup)(realComm, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    // VirtualGlobalCommId::instance().createGlobalId(virtComm); TODO
    *newcomm = virtComm;
    active_comms.insert(virtComm);
    LOG_CALL(restoreComms, Comm_dup, comm, *newcomm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}


#ifndef MPI_COLLECTIVE_P2P
PMPI_IMPL(int, MPI_Bcast, void *buffer, int count, MPI_Datatype datatype,
          int root, MPI_Comm comm)
PMPI_IMPL(int, MPI_Ibcast, void *buffer, int count, MPI_Datatype datatype,
          int root, MPI_Comm comm, MPI_Request *request)
PMPI_IMPL(int, MPI_Barrier, MPI_Comm comm)
PMPI_IMPL(int, MPI_Ibarrier, MPI_Comm comm, MPI_Request * request)
PMPI_IMPL(int, MPI_Allreduce, const void *sendbuf, void *recvbuf, int count,
          MPI_Datatype datatype, MPI_Op op, MPI_Comm comm)
PMPI_IMPL(int, MPI_Reduce, const void *sendbuf, void *recvbuf, int count,
          MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm)
PMPI_IMPL(int, MPI_Ireduce, const void *sendbuf, void *recvbuf, int count,
          MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm,
          MPI_Request *request)
PMPI_IMPL(int, MPI_Alltoall, const void *sendbuf, int sendcount,
          MPI_Datatype sendtype, void *recvbuf, int recvcount,
          MPI_Datatype recvtype, MPI_Comm comm)
PMPI_IMPL(int, MPI_Alltoallv, const void *sendbuf, const int sendcounts[],
          const int sdispls[], MPI_Datatype sendtype, void *recvbuf,
          const int recvcounts[], const int rdispls[], MPI_Datatype recvtype,
          MPI_Comm comm)
PMPI_IMPL(int, MPI_Allgather, const void *sendbuf, int sendcount,
          MPI_Datatype sendtype, void *recvbuf, int recvcount,
          MPI_Datatype recvtype, MPI_Comm comm)
PMPI_IMPL(int, MPI_Allgatherv, const void * sendbuf, int sendcount,
          MPI_Datatype sendtype, void *recvbuf, const int *recvcount,
          const int *displs, MPI_Datatype recvtype, MPI_Comm comm)
PMPI_IMPL(int, MPI_Gather, const void *sendbuf, int sendcount,
          MPI_Datatype sendtype, void *recvbuf, int recvcount,
          MPI_Datatype recvtype, int root, MPI_Comm comm)
PMPI_IMPL(int, MPI_Gatherv, const void *sendbuf, int sendcount,
          MPI_Datatype sendtype, void *recvbuf, const int recvcounts[],
          const int displs[], MPI_Datatype recvtype, int root, MPI_Comm comm)
PMPI_IMPL(int, MPI_Scatter, const void *sendbuf, int sendcount,
          MPI_Datatype sendtype, void *recvbuf, int recvcount,
          MPI_Datatype recvtype, int root, MPI_Comm comm)
PMPI_IMPL(int, MPI_Scatterv, const void *sendbuf, const int sendcounts[],
          const int displs[], MPI_Datatype sendtype, void *recvbuf,
          int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm)
PMPI_IMPL(int, MPI_Scan, const void *sendbuf, void *recvbuf, int count,
          MPI_Datatype datatype, MPI_Op op, MPI_Comm comm)
#endif // #ifndef MPI_COLLECTIVE_P2P
PMPI_IMPL(int, MPI_Comm_split, MPI_Comm comm, int color, int key,
          MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Comm_dup, MPI_Comm comm, MPI_Comm *newcomm)
