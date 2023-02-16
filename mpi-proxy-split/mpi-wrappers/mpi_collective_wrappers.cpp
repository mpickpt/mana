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
 *  License in the files COPYING and COPYING.LESSER.  If not, getsee           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
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

#define B4B_ALLREDUCE 0

int Allreduce_counter = 0;

#if defined B4B_ALLREDUCE && B4B_ALLREDUCE == 1
extern int g_world_rank;
extern int g_world_size;

int
is_file_exists(char *fname)
{
  return access(fname, F_OK) == 0;
}

int
touch(const char *filename)
{
  int fd = open(filename, O_CREAT | S_IRUSR | S_IWUSR);

  if (fd == -1) {
    perror("Unable to touch file");
    return 0;
  }

  close(fd);
  return 1;
}

extern int write_wrapper(char *filename,
                         void *buffer,
                         unsigned long int total_bytes_to_copied);

void
dump_Allreduce_trace(int event,
                     MPI_Datatype datatype,
                     MPI_Op op,
                     MPI_Comm comm,
                     int count,
                     const void *sendbuf,
                     void *recvbuf)
{
  char filename[30];
  char *delimiter = "$**$";
  sprintf(filename, "%d-allreduce.trace", g_world_rank);

  int rc = 0;
  rc += write_wrapper(filename, &Allreduce_counter, sizeof(int));
  rc += write_wrapper(filename, &event, sizeof(int));
  rc += write_wrapper(filename, &datatype, sizeof(MPI_Datatype));
  rc += write_wrapper(filename, &op, sizeof(MPI_Op));
  rc += write_wrapper(filename, &comm, sizeof(MPI_Comm));

  int comm_size = -1;
  MPI_Comm_size(comm, &comm_size);
  rc += write_wrapper(filename, &comm_size, sizeof(int));
  int comm_rank = -1;
  MPI_Comm_rank(comm, &comm_rank);
  rc += write_wrapper(filename, &comm_rank, sizeof(int));

  rc += write_wrapper(filename, &count, sizeof(int));

  int s = 0;
  MPI_Type_size(datatype, &s);
  int bufs = count * s;
  rc += write_wrapper(filename, &bufs, sizeof(int));
  rc += write_wrapper(filename, (void *)sendbuf, bufs);
  rc += write_wrapper(filename, recvbuf, bufs);
  rc += write_wrapper(filename, delimiter, 4 * sizeof(char));

  time_t my_time = time(NULL);
  char *time_str = ctime(&my_time);
  time_str[strlen(time_str) - 1] = '\0';

  if (rc != 12) {
    fprintf(
      stdout,
      "\n%s [%d]  -->  Failed to save the record for the Allreduce_counter: %d",
      time_str, g_world_rank, Allreduce_counter);
    fflush(stdout);
  }
}

#endif

USER_DEFINED_WRAPPER(int, Allreduce,
                     (const void *) sendbuf, (void *) recvbuf,
                     (int) count, (MPI_Datatype) datatype,
                     (MPI_Op) op, (MPI_Comm) comm)
{
  Allreduce_counter++;
#if defined B4B_ALLREDUCE && B4B_ALLREDUCE == 1
  int dummy = 0;
  while (dummy)
    ;

  char *s = getenv("DUMP_ALLREDUCE_TRACE");
  int dump_trace = (s != NULL) ? atoi(s) : -1;

  s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  if (dump_trace == 1 && Allreduce_counter > dump_trace_from)
    dump_Allreduce_trace(1, datatype, op, comm, count, sendbuf, recvbuf);


  time_t my_time = time(NULL);
  char *time_str = ctime(&my_time);

  int comm_size = -1;
  MPI_Comm_size(comm, &comm_size);

  if (g_world_rank == 0) {
    my_time = time(NULL);
    time_str = ctime(&my_time);
    time_str[strlen(time_str) - 1] = '\0';

    fprintf(stdout, "\n%s [%d]  -->  Comm Size: %d & Allreduce_counter: %d",
            time_str, g_world_rank, comm_size, Allreduce_counter);
    fflush(stdout);
  }

  s = getenv("STOP_AT_ALLREDUCE_COUNTER");
  int stop_at = (s != NULL) ? atoi(s) : -1;
  if (Allreduce_counter == stop_at && comm_size == g_world_size) {
    my_time = time(NULL);
    time_str = ctime(&my_time);
    time_str[strlen(time_str) - 1] = '\0';

    fprintf(stdout, "\n%s [%d]  -->  Inside \"Allreduce_counter == %d\" block",
            time_str, g_world_rank, stop_at);
    fprintf(stdout, "\n%s [%d]  -->  Executing while() loop...", time_str,
            g_world_rank);
    fflush(stdout);

    MPI_Barrier(comm);

    if (g_world_rank == 0)
      touch("/global/cfs/cdirs/cr/malviyat/exp-setup/do_checkpoint");

    while (
      is_file_exists("/global/cfs/cdirs/cr/malviyat/exp-setup/wait_in_while")) {
      sleep(1);
    }

    if (g_world_rank == 0)
      remove("/global/cfs/cdirs/cr/malviyat/exp-setup/do_checkpoint");

    time_str = ctime(&my_time);
    time_str[strlen(time_str) - 1] = '\0';

    fprintf(stdout, "\n%s [%d]  -->  stop_at: %d", time_str, g_world_rank,
            stop_at);
    fprintf(stdout, "\n%s [%d]  -->  while() loop terminated", time_str,
            g_world_rank);
    fflush(stdout);
  }
#endif

  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  get_fortran_constants();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  // FIXME: Ideally, we should only check FORTRAN_MPI_IN_PLACE
  //        in the Fortran wrapper.
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    retval = NEXT_FUNC(Allreduce)(MPI_IN_PLACE, recvbuf, count,
        realType, realOp, realComm);
  } else {
    retval = NEXT_FUNC(Allreduce)(sendbuf, recvbuf, count,
        realType, realOp, realComm);
  }
  RETURN_TO_UPPER_HALF();
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
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Reduce)(sendbuf, recvbuf, count,
                             realType, realOp, root, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish(comm, passthrough);
  return retval;
}

static int Ireduce_counter = 0;

USER_DEFINED_WRAPPER(int, Ireduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  Ireduce_counter++;

  int dummy = 0;
  while (dummy);

  time_t my_time = time(NULL);
  char* time_str = ctime(&my_time);

  int comm_size = -1;
  MPI_Comm_size(comm, &comm_size);

  if (g_world_rank == 0) {
    my_time = time(NULL);
    time_str = ctime(&my_time);
    time_str[strlen(time_str) - 1] = '\0';

    fprintf(stdout, "\n%s [%d]  -->  Comm Size: %d & Ireduce_counter: %d", time_str, g_world_rank, comm_size, Ireduce_counter);
    fflush(stdout);
  }

  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Ireduce)(sendbuf, recvbuf, count,
      realType, realOp, root, realComm, request);
  RETURN_TO_UPPER_HALF();
  
  //MPI_Status s;
  MPI_Wait(request, MPI_STATUS_IGNORE);

  /*
  if (s == MPI_ERR_REQUEST || s == MPI_ERR_ARG) {
    my_time = time(NULL);
    time_str = ctime(&my_time);
    time_str[strlen(time_str) - 1] = '\0';

    fprintf(stdout, "\n%s [%d]  -->  Comm Size: %d & Ireduce_counter: %d & Got error from the MPI_Wait() API", time_str, g_world_rank, comm_size, Ireduce_counter);
    fflush(stdout);
  }
  */

  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    MPI_Request virtRequest = ADD_NEW_REQUEST(*request);
    *request = virtRequest;
    LOG_CALL(restoreRequests, Ireduce, sendbuf, recvbuf,
        count, datatype, op, root, comm, *request);
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IREDUCE_REQUSET);
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
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
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
    VirtualGlobalCommId::instance().createGlobalId(virtComm);
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
    VirtualGlobalCommId::instance().createGlobalId(virtComm);
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
