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
#include "lower_half_ckpt.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_plugin.h"
#include "record-replay.h"
#include "mpi_nextfunc.h"
#include "seq_num.h"
#include "virtual_id.h"
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

extern "C" {

#ifndef MPI_COLLECTIVE_P2P
#pragma weak MPI_Bcast = PMPI_Bcast
int PMPI_Bcast(void *buffer, int count, MPI_Datatype datatype,
              int root, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Bcast)(buffer, count, real_datatype, root, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Ibcast = PMPI_Ibcast
int PMPI_Ibcast(void *buffer, int count, MPI_Datatype datatype,
               int root, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Ibcast)(buffer, count, real_datatype,
      root, real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    // A checkpoint completes it first (see
    // complete_pending_nonblocking_collectives() in seq_num.cpp).
    MPI_Request virtRequest = new_virt_collective_request(*request);
    *request = virtRequest;
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IBCAST_REQUEST);
#endif
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Barrier = PMPI_Barrier
int PMPI_Barrier(MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Barrier)(real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Ibarrier = PMPI_Ibarrier
int PMPI_Ibarrier(MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Ibarrier)(real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    MPI_Request virtRequest = new_virt_collective_request(*request);
    *request = virtRequest;
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IBARRIER_REQUEST);
#endif
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Allreduce = PMPI_Allreduce
int PMPI_Allreduce(const void * sendbuf, void * recvbuf,
              int count, MPI_Datatype datatype,
              MPI_Op op, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  get_fortran_constants();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype =
    get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Op real_op = get_real_id((mana_mpi_handle){.op = op}).op;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Allreduce)(sendbuf, recvbuf, count, real_datatype,
                                real_op, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Reduce = PMPI_Reduce
int PMPI_Reduce(const void *sendbuf, void *recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Op real_op = get_real_id((mana_mpi_handle){.op = op}).op;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Reduce)(sendbuf, recvbuf, count,
                             real_datatype, real_op, root, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Ireduce = PMPI_Ireduce
int PMPI_Ireduce(const void *sendbuf, void *recvbuf, int count,
                MPI_Datatype datatype, MPI_Op op,
                int root, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Op real_op = get_real_id((mana_mpi_handle){.op = op}).op;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Ireduce)(sendbuf, recvbuf, count,
      real_datatype, real_op, root, real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    MPI_Request virtRequest = new_virt_collective_request(*request);
    *request = virtRequest;
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IREDUCE_REQUEST);
#endif
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Reduce_scatter = PMPI_Reduce_scatter
int PMPI_Reduce_scatter(const void *sendbuf, void *recvbuf,
                       const int recvcounts[], MPI_Datatype datatype,
                       MPI_Op op, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Op real_op = get_real_id((mana_mpi_handle){.op = op}).op;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Reduce_scatter)(sendbuf, recvbuf, recvcounts,
                                     real_datatype, real_op, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
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
#ifndef MPI_ALLTOALL_RENDEZVOUS
int
MPI_Alltoall_internal(const void *sendbuf, int sendcount,
                      MPI_Datatype sendtype, void *recvbuf, int recvcount,
                      MPI_Datatype recvtype, MPI_Comm comm)
{
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Alltoall)(sendbuf, sendcount, realSendType, recvbuf,
      recvcount, realRecvType, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  return retval;
}
#else
// We are having a hanging issue running user programs under certain situations. In
// order to prevent it there is a temporary workaround provided by Yao Xu. To
// manually implement an MPI_Alltoall_internal call forcing rendezvous (MPI_Issend),
// the hanging can be prevented without noticable performance burden.

/*
 ( For MPI_Alltoall_internal, the following copyright of ANL aplies.
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory [ of MPICH distribution ]
 */

int
MPI_Alltoall_internal(const void *sendbuf, int sendcount,
                      MPI_Datatype sendtype, void *recvbuf, int recvcount,
                      MPI_Datatype recvtype, MPI_Comm comm)
{
  static int PMPI_ALLTOALL_TAG = 0;
  int retval, comm_size, rank;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Aint rlb, slb, recvtype_extent,sendtype_extent;
  MPI_Type_get_extent(sendtype, &slb, &sendtype_extent);
  MPI_Type_get_extent(recvtype, &rlb, &recvtype_extent);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }

  // With our MPI_Alltoall implementation forcing rendezvous
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  int ii, ss, bblock;
  int i;
  int dst;
  bblock = comm_size;
  MPI_Request *reqarray = (MPI_Request *) malloc(2 * bblock * sizeof(MPI_Request *));
  MPI_Status *starray = (MPI_Status *) malloc(2 * bblock * sizeof(MPI_Status));
  for (ii = 0; ii < comm_size; ii += bblock) {
    ss = comm_size - ii < bblock ? comm_size - ii : bblock;
    for (i = 0; i < ss; i++) {
      dst = (rank + i + ii) % comm_size;
      NEXT_FUNC(Irecv)(recvbuf + dst * recvcount * recvtype_extent, recvcount, realRecvType,
          dst, MPI_ALLTOALL_TAG, real_comm, &reqarray[i]);
    }
    for (i = 0; i < ss; i++) {
      dst = (rank - i - ii + comm_size) % comm_size;
      // MPI_Issend starts a nonblocking synchronous send
      NEXT_FUNC(Issend)(sendbuf + dst * sendcount * sendtype_extent, sendcount, realSendType,
          dst, MPI_ALLTOALL_TAG, real_comm, &reqarray[i + ss]);
    }
  }
  int flag = 0;
  while (!flag) {
    flag = 1;
    int status_flag = 0;
    for (i = 0; i < 2 * ss; i++) {
      retval = NEXT_FUNC(Request_get_status)(reqarray[i], &status_flag, &starray[i]);
      flag &= status_flag;
    }
  }
  retval = NEXT_FUNC(Waitall)(2 * ss, reqarray, starray);
  free(reqarray);
  free(starray);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  return retval;
}
#endif

#ifndef MPI_COLLECTIVE_P2P
#pragma weak MPI_Alltoall = PMPI_Alltoall
int PMPI_Alltoall(const void *sendbuf, int sendcount,
                 MPI_Datatype sendtype, void *recvbuf, int recvcount,
                 MPI_Datatype recvtype, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  // sendbuf can propagate MPI_IN_PLACE and FORTRAN_MPI_IN_PLACE
  retval = MPI_Alltoall_internal(sendbuf, sendcount, sendtype,
                                 recvbuf, recvcount, recvtype, comm);
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Alltoallv = PMPI_Alltoallv
int PMPI_Alltoallv(const void *sendbuf, const int *sendcounts,
                  const int *sdispls, MPI_Datatype sendtype,
                  void *recvbuf, const int *recvcounts,
                  const int *rdispls, MPI_Datatype recvtype,
                  MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Alltoallv)(sendbuf, sendcounts, sdispls, realSendType,
                                recvbuf, recvcounts, rdispls, realRecvType,
                                real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Gather = PMPI_Gather
int PMPI_Gather(const void *sendbuf, int sendcount,
               MPI_Datatype sendtype, void *recvbuf, int recvcount,
               MPI_Datatype recvtype, int root, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Gather)(sendbuf, sendcount, realSendType,
                             recvbuf, recvcount, realRecvType,
                             root, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Gatherv = PMPI_Gatherv
int PMPI_Gatherv(const void *sendbuf, int sendcount,
                MPI_Datatype sendtype, void *recvbuf,
                const int *recvcounts, const int *displs,
                MPI_Datatype recvtype, int root, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Gatherv)(sendbuf, sendcount, realSendType,
                              recvbuf, recvcounts, displs, realRecvType,
                              root, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Scatter = PMPI_Scatter
int PMPI_Scatter(const void *sendbuf, int sendcount,
                MPI_Datatype sendtype, void *recvbuf, int recvcount,
                MPI_Datatype recvtype, int root, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (recvbuf == FORTRAN_MPI_IN_PLACE) {
    recvbuf = MPI_IN_PLACE;
  }
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Scatter)(sendbuf, sendcount, realSendType,
                              recvbuf, recvcount, realRecvType,
                              root, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Scatterv = PMPI_Scatterv
int PMPI_Scatterv(const void *sendbuf,
                 const int *sendcounts, const int *displs,
                 MPI_Datatype sendtype, void *recvbuf, int recvcount,
                 MPI_Datatype recvtype, int root, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (recvbuf == FORTRAN_MPI_IN_PLACE) {
    recvbuf = MPI_IN_PLACE;
  }
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Scatterv)(sendbuf, sendcounts, displs, realSendType,
                               recvbuf, recvcount, realRecvType,
                               root, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Allgather = PMPI_Allgather
int PMPI_Allgather(const void *sendbuf, int sendcount,
                  MPI_Datatype sendtype, void *recvbuf, int recvcount,
                  MPI_Datatype recvtype, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Allgather)(sendbuf, sendcount, realSendType,
                                recvbuf, recvcount, realRecvType,
                                real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Allgatherv = PMPI_Allgatherv
int PMPI_Allgatherv(const void *sendbuf, int sendcount,
                   MPI_Datatype sendtype, void *recvbuf,
                   const int *recvcounts, const int *displs,
                   MPI_Datatype recvtype, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType = get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType = get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Allgatherv)(sendbuf, sendcount, realSendType,
                                 recvbuf, recvcounts, displs, realRecvType,
                                 real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Scan = PMPI_Scan
int PMPI_Scan(const void *sendbuf, void *recvbuf,
             int count, MPI_Datatype datatype,
             MPI_Op op, MPI_Comm comm)
{
  commit_begin(comm);
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  MPI_Op real_op = get_real_id((mana_mpi_handle){.op = op}).op;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Scan)(sendbuf, recvbuf, count,
                           real_datatype, real_op, real_comm);
  RETURN_TO_UPPER_HALF();
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}
#endif // #ifndef MPI_COLLECTIVE_P2P

// Non-blocking collectives.  Like MPI_Ibcast, MPI_Ireduce and MPI_Ibarrier,
// they tick the Collective Clock and mark their request so that a checkpoint
// completes them first.  MPI_COLLECTIVE_P2P has no version of them.
#pragma weak MPI_Iallreduce = PMPI_Iallreduce
int PMPI_Iallreduce(const void *sendbuf, void *recvbuf, int count,
                    MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                    MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype =
    get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Op real_op = get_real_id((mana_mpi_handle){.op = op}).op;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Iallreduce)(sendbuf, recvbuf, count, real_datatype,
                                 real_op, real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Ireduce_scatter = PMPI_Ireduce_scatter
int PMPI_Ireduce_scatter(const void *sendbuf, void *recvbuf,
                         const int recvcounts[], MPI_Datatype datatype,
                         MPI_Op op, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype =
    get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Op real_op = get_real_id((mana_mpi_handle){.op = op}).op;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Ireduce_scatter)(sendbuf, recvbuf, recvcounts,
                                      real_datatype, real_op, real_comm,
                                      request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Iscan = PMPI_Iscan
int PMPI_Iscan(const void *sendbuf, void *recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
               MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype real_datatype =
    get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Op real_op = get_real_id((mana_mpi_handle){.op = op}).op;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Iscan)(sendbuf, recvbuf, count, real_datatype, real_op,
                            real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Ialltoall = PMPI_Ialltoall
int PMPI_Ialltoall(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                   void *recvbuf, int recvcount, MPI_Datatype recvtype,
                   MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType =
    get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType =
    get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Ialltoall)(sendbuf, sendcount, realSendType, recvbuf,
                                recvcount, realRecvType, real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Ialltoallv = PMPI_Ialltoallv
int PMPI_Ialltoallv(const void *sendbuf, const int sendcounts[],
                    const int sdispls[], MPI_Datatype sendtype, void *recvbuf,
                    const int recvcounts[], const int rdispls[],
                    MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType =
    get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType =
    get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Ialltoallv)(sendbuf, sendcounts, sdispls, realSendType,
                                 recvbuf, recvcounts, rdispls, realRecvType,
                                 real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Iallgather = PMPI_Iallgather
int PMPI_Iallgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                    void *recvbuf, int recvcount, MPI_Datatype recvtype,
                    MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType =
    get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType =
    get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Iallgather)(sendbuf, sendcount, realSendType, recvbuf,
                                 recvcount, realRecvType, real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Iallgatherv = PMPI_Iallgatherv
int PMPI_Iallgatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                     void *recvbuf, const int recvcounts[], const int displs[],
                     MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType =
    get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType =
    get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Iallgatherv)(sendbuf, sendcount, realSendType, recvbuf,
                                  recvcounts, displs, realRecvType, real_comm,
                                  request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Igather = PMPI_Igather
int PMPI_Igather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                 void *recvbuf, int recvcount, MPI_Datatype recvtype, int root,
                 MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType =
    get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType =
    get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Igather)(sendbuf, sendcount, realSendType, recvbuf,
                              recvcount, realRecvType, root, real_comm,
                              request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Igatherv = PMPI_Igatherv
int PMPI_Igatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                  void *recvbuf, const int recvcounts[], const int displs[],
                  MPI_Datatype recvtype, int root, MPI_Comm comm,
                  MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType =
    get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType =
    get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Igatherv)(sendbuf, sendcount, realSendType, recvbuf,
                               recvcounts, displs, realRecvType, root,
                               real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Iscatter = PMPI_Iscatter
int PMPI_Iscatter(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                  void *recvbuf, int recvcount, MPI_Datatype recvtype, int root,
                  MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType =
    get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType =
    get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (recvbuf == FORTRAN_MPI_IN_PLACE) {
    recvbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Iscatter)(sendbuf, sendcount, realSendType, recvbuf,
                               recvcount, realRecvType, root, real_comm,
                               request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Iscatterv = PMPI_Iscatterv
int PMPI_Iscatterv(const void *sendbuf, const int sendcounts[],
                   const int displs[], MPI_Datatype sendtype, void *recvbuf,
                   int recvcount, MPI_Datatype recvtype, int root,
                   MPI_Comm comm, MPI_Request *request)
{
  int retval;
  commit_begin(comm);
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  MPI_Datatype realSendType =
    get_real_id((mana_mpi_handle){.datatype = sendtype}).datatype;
  MPI_Datatype realRecvType =
    get_real_id((mana_mpi_handle){.datatype = recvtype}).datatype;
  // FIXME: Ideally, check FORTRAN_MPI_IN_PLACE only in the Fortran wrapper.
  get_fortran_constants();
  if (recvbuf == FORTRAN_MPI_IN_PLACE) {
    recvbuf = MPI_IN_PLACE;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Iscatterv)(sendbuf, sendcounts, displs, realSendType,
                                recvbuf, recvcount, realRecvType, root,
                                real_comm, request);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *request = new_virt_collective_request(*request);
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

// FIXME: Also check the MPI_Cart family, if they use collective communications.
#pragma weak MPI_Comm_split = PMPI_Comm_split
int PMPI_Comm_split(MPI_Comm comm, int color, int key, MPI_Comm *newcomm)
{
  commit_begin(comm);
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Comm_split)(real_comm, color, key, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    if (*newcomm == lh_info->MANA_COMM_NULL) {
      *newcomm = MPI_COMM_NULL;
    } else {
      *newcomm = new_virt_comm(*newcomm);
    }
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

#pragma weak MPI_Comm_dup = PMPI_Comm_dup
int PMPI_Comm_dup(MPI_Comm comm, MPI_Comm *newcomm)
{
  commit_begin(comm);
  int retval;
  LOWER_HALF_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Comm_dup)(real_comm, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    if (*newcomm == lh_info->MANA_COMM_NULL) {
      *newcomm = MPI_COMM_NULL;
    } else {
      *newcomm = new_virt_comm(*newcomm);
    }
  }
  LOWER_HALF_ENABLE_CKPT();
  commit_finish(comm);
  return retval;
}

} // end of: extern "C"
