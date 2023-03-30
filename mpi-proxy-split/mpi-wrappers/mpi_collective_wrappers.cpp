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

#define B4B_PRINT 0

using namespace dmtcp_mpi;

extern int g_world_rank;
extern int g_world_size;

int DO_BUFFER_XOR = 9999;

int Gatherv_counter = 0;
int Allgather_counter = 0; 
int Allgatherv_counter = 0; 
int Reduce_counter = 0; 
int Ireduce_counter = 0; 
int Alltoall_counter = 0;
int Alltoallv_counter = 0;
int Bcast_counter = 0;
int Ibcast_counter = 0;
int Allreduce_counter = 0; 

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
  Bcast_counter++;

#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  int checksum = -1;
  char dtstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(datatype, &ds);
    buf_size = count * ds;
    get_datatype_string(datatype, dtstr);

    if (DO_BUFFER_XOR == 0 && buf_size > 8)
      checksum = get_buffer_checksum((int *) buffer, buf_size);

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Bcast-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Datatype: %s-%d & Count: %d & Buffer Size: %d & Buffer Address: %p "
      "& Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Bcast Counter: "
      "%d & Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size, root,
      dtstr, datatype, count, buf_size, buffer, *((unsigned char *)buffer),
      *((unsigned char *)buffer + 1), *((unsigned char *)buffer + 2),
      *((unsigned char *)buffer + 3), *((unsigned char *)buffer + 4),
      *((unsigned char *)buffer + 5), *((unsigned char *)buffer + 6),
      *((unsigned char *)buffer + 7), Bcast_counter, checksum);
  }
#endif
  /* ************************************************* */

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

  /* ************************************************* */
#if defined B4B_PRINT && B4B_PRINT == 1
  if (Allreduce_counter > dump_trace_from) {
    if (DO_BUFFER_XOR == 0 && buf_size > 8) {
      checksum = get_buffer_checksum((int *) buffer, buf_size);
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Bcast-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Bcast-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Datatype: %s-%d & Count: %d & Buffer Size: %d & Buffer Address: %p "
      "& Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Bcast Counter: "
      "%d & Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size, root,
      dtstr, datatype, count, buf_size, buffer, *((unsigned char *)buffer),
      *((unsigned char *)buffer + 1), *((unsigned char *)buffer + 2),
      *((unsigned char *)buffer + 3), *((unsigned char *)buffer + 4),
      *((unsigned char *)buffer + 5), *((unsigned char *)buffer + 6),
      *((unsigned char *)buffer + 7), Bcast_counter, checksum);
  }
#endif
  return retval;
}
#endif

USER_DEFINED_WRAPPER(int, Ibcast,
                     (void *) buffer, (int) count, (MPI_Datatype) datatype,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  Ibcast_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  int checksum = -1;
  char dtstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(datatype, &ds);
    buf_size = count * ds;
    get_datatype_string(datatype, dtstr);

    if (DO_BUFFER_XOR == 0 && buf_size > 8)
      checksum = get_buffer_checksum((int *) buffer, buf_size);

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Ibcast-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Datatype: %s-%d & Count: %d & Buffer Size: %d & Buffer Address: %p "
      "& Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Ibcast Counter: "
      "%d & Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size, root,
      dtstr, datatype, count, buf_size, buffer, *((unsigned char *)buffer),
      *((unsigned char *)buffer + 1), *((unsigned char *)buffer + 2),
      *((unsigned char *)buffer + 3), *((unsigned char *)buffer + 4),
      *((unsigned char *)buffer + 5), *((unsigned char *)buffer + 6),
      *((unsigned char *)buffer + 7), Ibcast_counter, checksum);
  }
#endif
  /* ************************************************* */

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

/* ************************************************* */
#if defined B4B_PRINT && B4B_PRINT == 1
  if (Allreduce_counter > dump_trace_from) {
    if (DO_BUFFER_XOR == 0 && buf_size > 8) {
      checksum = get_buffer_checksum((int *) buffer, buf_size);
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Ibcast-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Ibcast-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Datatype: %s-%d & Count: %d & Buffer Size: %d & Buffer Address: %p "
      "& Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Ibcast Counter: "
      "%d & Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size, root,
      dtstr, datatype, count, buf_size, buffer, *((unsigned char *)buffer),
      *((unsigned char *)buffer + 1), *((unsigned char *)buffer + 2),
      *((unsigned char *)buffer + 3), *((unsigned char *)buffer + 4),
      *((unsigned char *)buffer + 5), *((unsigned char *)buffer + 6),
      *((unsigned char *)buffer + 7), Ibcast_counter, checksum);
  }
#endif
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

USER_DEFINED_WRAPPER(int, Allreduce,
                     (const void *) sendbuf, (void *) recvbuf,
                     (int) count, (MPI_Datatype) datatype,
                     (MPI_Op) op, (MPI_Comm) comm)
{
  Allreduce_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  int schecksum = -1;
  int rchecksum = -1;
  char dtstr[30], opstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(datatype, &ds);
    buf_size = count * ds;
    get_datatype_string(datatype, dtstr);
    get_op_string(op, opstr);

    if (DO_BUFFER_XOR == 0 && buf_size > 8) {
      schecksum = get_buffer_checksum((int *) sendbuf, buf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, buf_size);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Allreduce-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Operation: %s & Datatype: %s-%d & Count: %d & Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p & Recv "
      "Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Allreduce Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      opstr, dtstr, datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
      *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
      *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
      *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
      *((unsigned char *)recvbuf + 7), Allreduce_counter, schecksum, rchecksum);
  }
#endif
  /* ************************************************* */

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

  /* ************************************************* */
#if defined B4B_PRINT && B4B_PRINT == 1
  if (Allreduce_counter > dump_trace_from) {
    if (DO_BUFFER_XOR == 0 && buf_size > 8) {
      schecksum = get_buffer_checksum((int *) sendbuf, buf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, buf_size);
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Allreduce-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Allreduce-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Operation: %s & Datatype: %s-%d & Count: %d & Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p & Recv "
      "Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Allreduce Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      opstr, dtstr, datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
      *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
      *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
      *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
      *((unsigned char *)recvbuf + 7), Allreduce_counter, schecksum, rchecksum);
  }
#endif
  return retval;
}

USER_DEFINED_WRAPPER(int, Reduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm)
{
  Reduce_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  int schecksum = -1;
  int rchecksum = -1;
  char dtstr[30], opstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(datatype, &ds);
    buf_size = count * ds;
    get_datatype_string(datatype, dtstr);
    get_op_string(op, opstr);
    
    if (DO_BUFFER_XOR == 0 && buf_size > 8) {
      schecksum = get_buffer_checksum((int *) sendbuf, buf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, buf_size);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Reduce-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Operation: %s & Datatype: %s-%d & Count: %d & "
      "Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p "
      "& Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Reduce "
      "Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size, root,
      opstr, dtstr, datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
      *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
      *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
      *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
      *((unsigned char *)recvbuf + 7), Reduce_counter, schecksum, rchecksum);
  }
#endif
  /* ************************************************* */

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
#if defined B4B_PRINT && B4B_PRINT == 1
  /* ************************************************* */
  if (Allreduce_counter > dump_trace_from) {
    if (DO_BUFFER_XOR == 0 && buf_size > 8) {
      schecksum = get_buffer_checksum((int *) sendbuf, buf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, buf_size);
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Reduce-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Reduce-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Operation: %s & Datatype: %s-%d & Count: %d & "
      "Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p "
      "& Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Reduce "
      "Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size, root,
      opstr, dtstr, datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
      *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
      *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
      *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
      *((unsigned char *)recvbuf + 7), Reduce_counter, schecksum, rchecksum);
  }
#endif
  return retval;
}

USER_DEFINED_WRAPPER(int, Ireduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  Ireduce_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  char dtstr[30], opstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(datatype, &ds);
    buf_size = count * ds;
    get_datatype_string(datatype, dtstr);
    get_op_string(op, opstr);

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Ireduce-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Operation: %s & Datatype: %s-%d & Count: %d & "
      "Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p "
      "& Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Ireduce "
      "Counter: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size, root,
      opstr, dtstr, datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
      *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
      *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
      *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
      *((unsigned char *)recvbuf + 7), Ireduce_counter);
  }
#endif
  /* ************************************************* */

  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
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
    logRequestInfo(*request, IREDUCE_REQUSET);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  /* ************************************************* */
#if defined B4B_PRINT && B4B_PRINT == 1
  if (Allreduce_counter > dump_trace_from) {
    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Ireduce-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Operation: %s & Datatype: %s-%d & Count: %d & "
      "Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p "
      "& Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Ireduce "
      "Counter: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size, root,
      opstr, dtstr, datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
      *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
      *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
      *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
      *((unsigned char *)recvbuf + 7), Ireduce_counter);
  }
#endif
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
  Alltoall_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  int schecksum = -1;
  int rchecksum = -1;
  char sdtstr[30], rdtstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(sendtype, &ds);
    sbuf_size = sendcount * ds;
    get_datatype_string(sendtype, sdtstr);

    MPI_Type_size(recvtype, &ds);
    rbuf_size = recvcount * ds;
    get_datatype_string(recvtype, rdtstr);

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Alltoall-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Alltoall Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, recvcount, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Alltoall_counter, schecksum, rchecksum);
  }
#endif
  /* ************************************************* */

  bool passthrough = false;
  commit_begin(comm, passthrough);
  int retval;
  retval = MPI_Alltoall_internal(sendbuf, sendcount, sendtype,
                                 recvbuf, recvcount, recvtype, comm);
  commit_finish(comm, passthrough);

  /* ************************************************* */
#if defined B4B_PRINT && B4B_PRINT == 1
  if (Allreduce_counter > dump_trace_from) {

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
      DO_BUFFER_XOR++;
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Alltoall-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Alltoall-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Alltoall Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, recvcount, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Alltoall_counter, schecksum, rchecksum);
  }
#endif
  return retval;
}

USER_DEFINED_WRAPPER(int, Alltoallv,
                     (const void *) sendbuf, (const int *) sendcounts,
                     (const int *) sdispls, (MPI_Datatype) sendtype,
                     (void *) recvbuf, (const int *) recvcounts,
                     (const int *) rdispls, (MPI_Datatype) recvtype,
                     (MPI_Comm) comm)
{
  Alltoallv_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  int schecksum = -1;
  int rchecksum = -1;
  char sdtstr[30], rdtstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(sendtype, &ds);
    sbuf_size = (*sendcounts) * ds;
    get_datatype_string(sendtype, sdtstr);

    MPI_Type_size(recvtype, &ds);
    rbuf_size = *recvcounts * ds;
    get_datatype_string(recvtype, rdtstr);

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Alltoallv-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Alltoallv Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      sdtstr, sendtype, (*sendcounts), sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size, recvbuf, 
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Alltoallv_counter, schecksum, rchecksum);
  }
#endif
  /* ************************************************* */

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

  /* ************************************************* */
#if defined B4B_PRINT && B4B_PRINT == 1
  if (Allreduce_counter > dump_trace_from) {

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
      DO_BUFFER_XOR++;
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Alltoallv-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Alltoallv-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Alltoallv Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      sdtstr, sendtype, *sendcounts, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Alltoallv_counter, schecksum, rchecksum);
  }
#endif
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
  Gatherv_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  int schecksum = -1;
  int rchecksum = -1;
  char sdtstr[30], rdtstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(sendtype, &ds);
    sbuf_size = sendcount * ds;
    get_datatype_string(sendtype, sdtstr);

    MPI_Type_size(recvtype, &ds);
    rbuf_size = *recvcounts * ds;
    get_datatype_string(recvtype, rdtstr);

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Gatherv-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Gatherv Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,root,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size,recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Gatherv_counter, schecksum, rchecksum);
  }
#endif
  /* ************************************************* */

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

  /* ************************************************* */
#if defined B4B_PRINT && B4B_PRINT == 1
  if (Allreduce_counter > dump_trace_from) {

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
      DO_BUFFER_XOR++;
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Gatherv-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Gatherv-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Root: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Gatherv Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,root,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Gatherv_counter, schecksum, rchecksum);
  }
#endif
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
  Allgather_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  int schecksum = -1;
  int rchecksum = -1;
  char sdtstr[30], rdtstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(sendtype, &ds);
    sbuf_size = sendcount * ds;
    get_datatype_string(sendtype, sdtstr);

    MPI_Type_size(recvtype, &ds);
    rbuf_size = recvcount * ds;
    get_datatype_string(recvtype, rdtstr);

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Allgather-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Allgather Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, recvcount, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Allgather_counter, schecksum, rchecksum);
  }
#endif
  /* ************************************************* */

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
#if defined B4B_PRINT && B4B_PRINT == 1
  /* ************************************************* */
  if (Allreduce_counter > dump_trace_from) {

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
      DO_BUFFER_XOR++;
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Allgather-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Allgather-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Allgather Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, recvcount, rbuf_size, recvbuf, 
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Allgather_counter, schecksum, rchecksum);
  }
#endif
  return retval;
}

USER_DEFINED_WRAPPER(int, Allgatherv, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf,
                     (const int*) recvcounts, (const int *) displs,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  Allgatherv_counter++;
#if defined B4B_PRINT && B4B_PRINT == 1
  char *s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  int schecksum = -1;
  int rchecksum = -1;
  char sdtstr[30], rdtstr[30];

  if (Allreduce_counter > dump_trace_from) {
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(sendtype, &ds);
    sbuf_size = sendcount * ds;
    get_datatype_string(sendtype, sdtstr);

    MPI_Type_size(recvtype, &ds);
    rbuf_size = *recvcounts * ds;
    get_datatype_string(recvtype, rdtstr);

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Allgatherv-Before -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Allgatherv Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Allgatherv_counter, schecksum, rchecksum);
  }
#endif
  /* ************************************************* */

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
#if defined B4B_PRINT && B4B_PRINT == 1
  /* ************************************************* */
  if (Allreduce_counter > dump_trace_from) {

    if (DO_BUFFER_XOR < 1000 && sbuf_size > 0) {
      schecksum = get_buffer_checksum((int *) sendbuf, sbuf_size);
      rchecksum = get_buffer_checksum((int *) recvbuf, rbuf_size);
      DO_BUFFER_XOR++;
      fprintf(stdout, "\n[WorldRank-%d] -> %lu -> Allgatherv-After -> %d",
              g_world_rank, (unsigned long)time(NULL), DO_BUFFER_XOR);
    }

    fprintf(
      stdout,
      "\n[WorldRank-%d] -> %lu -> Allgatherv-After -> Comm: %d & Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Allgatherv Counter: %d & Send Checksum: %d & Recv Checksum: %d",
      g_world_rank, (unsigned long)time(NULL), VirtualGlobalCommId::instance().getGlobalId(comm), comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size, recvbuf, 
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Allgatherv_counter, schecksum, rchecksum);
  }
#endif
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
