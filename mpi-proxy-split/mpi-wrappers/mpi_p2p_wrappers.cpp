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
#include "record-replay.h"
// To support MANA_P2P_LOG and MANA_P2P_REPLAY:
#include "p2p-deterministic.h"

extern int p2p_deterministic_skip_save_request;

#define B4B_SEND_RECV 0

#if defined B4B_SEND_RECV && B4B_SEND_RECV == 1

extern int g_world_rank;
extern int g_world_size;

void
get_datatype_string(MPI_Datatype datatype, char *buf)
{
  switch (datatype) {
    case MPI_CHAR:
      sprintf(buf, "MPI_CHAR\0");
      break;
    case MPI_SIGNED_CHAR:
      sprintf(buf, "MPI_SIGNED_CHAR\0");
      break;
    case MPI_UNSIGNED_CHAR:
      sprintf(buf, "MPI_UNSIGNED_CHAR\0");
      break;
    case MPI_BYTE:
      sprintf(buf, "MPI_BYTE\0");
      break;
    case MPI_WCHAR:
      sprintf(buf, "MPI_WCHAR\0");
      break;
    case MPI_SHORT:
      sprintf(buf, "MPI_SHORT\0");
      break;
    case MPI_UNSIGNED_SHORT:
      sprintf(buf, "MPI_UNSIGNED_SHORT\0");
      break;
    case MPI_INT:
      sprintf(buf, "MPI_INT\0");
      break;
    case MPI_UNSIGNED:
      sprintf(buf, "MPI_UNSIGNED\0");
      break;
    case MPI_LONG:
      sprintf(buf, "MPI_LONG\0");
      break;
    case MPI_UNSIGNED_LONG:
      sprintf(buf, "MPI_UNSIGNED_LONG\0");
      break;
    case MPI_FLOAT:
      sprintf(buf, "MPI_FLOAT\0");
      break;
    case MPI_DOUBLE:
      sprintf(buf, "MPI_DOUBLE\0");
      break;
    case MPI_LONG_DOUBLE:
      sprintf(buf, "MPI_LONG_DOUBLE\0");
      break;
    case MPI_LONG_LONG_INT:
      sprintf(buf, "MPI_LONG_LONG_INT or MPI_LONG_LONG\0");
      break;
    case MPI_UNSIGNED_LONG_LONG:
      sprintf(buf, "MPI_UNSIGNED_LONG_LONG\0");
      break;
    default:
      sprintf(buf, "UNKNOWN\0");
      break;
  }
}

int
write_wrapper(char *filename,
              void *buffer,
              unsigned long int total_bytes_to_copied)
{
  int fd;
  unsigned long int bytes_copied = 0, bytes_copied_so_far = 0;

  fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);

  // Validate file descriptor
  if (fd == -1) {
    perror("\nInvalid file descriptor!");
    return 0;
  }

  // Seek the cursor to the end
  lseek(fd, 0, SEEK_END);

#if 0
  fprintf(stdout, "\nwrite_wrapper() - Cursor position before writing: %d", (int) lseek(fd, 0, SEEK_CUR));
#endif

  bytes_copied = write(fd, (char *)buffer, total_bytes_to_copied);
  // Check whether we are able to write into file properly or not
  if (bytes_copied == -1) {
    perror("\nUnable to write into file!");
    return 0;
  }
  bytes_copied_so_far += bytes_copied;

  while (bytes_copied_so_far < total_bytes_to_copied) {
    bytes_copied = write(fd, (char *)buffer + bytes_copied_so_far,
                         total_bytes_to_copied - bytes_copied_so_far);
    if (bytes_copied == -1)
      break;

    bytes_copied_so_far += bytes_copied;
  }

#if 0
  fprintf(stdout, "\nwrite_wrapper() - Cursor position after writing: %d", (int) lseek(fd, 0, SEEK_CUR));
  fprintf(stdout, "\nwrite_wrapper() - %lu bytes out of %lu bytes copied.", bytes_copied_so_far, total_bytes_to_copied);
#endif

  close(fd);

  return (bytes_copied_so_far == total_bytes_to_copied);
}

void
dump_Send_Recv_trace(int api,
                     int counter,
                     const void *buf,
                     int count,
                     MPI_Datatype datatype,
                     int src_dest,
                     int tag,
                     MPI_Comm comm,
                     MPI_Request *request,
                     MPI_Status *status)
{
  char filename[30];
  char *delimiter = "$**$";
  sprintf(filename, "rank-%d-send-recv.trace", g_world_rank);

  int dummy = 0;
  while (dummy)
    ;

  int rc = 0;
  rc += write_wrapper(filename, &api, sizeof(int));
  rc += write_wrapper(filename, &counter, sizeof(int));
  rc += write_wrapper(filename, &datatype, sizeof(MPI_Datatype));
  rc += write_wrapper(filename, &src_dest, sizeof(int));
  rc += write_wrapper(filename, &tag, sizeof(int));
  rc += write_wrapper(filename, &comm, sizeof(MPI_Comm));
  int comm_size = -1;
  MPI_Comm_size(comm, &comm_size);
  rc += write_wrapper(filename, &comm_size, sizeof(int));

  int comm_rank = -1;
  MPI_Comm_rank(comm, &comm_rank);
  rc += write_wrapper(filename, &comm_rank, sizeof(int));

  rc += write_wrapper(filename, &count, sizeof(int));

  int ds = 0;
  MPI_Type_size(datatype, &ds);
  int buf_size = count * ds;
  rc += write_wrapper(filename, &buf_size, sizeof(int));
  rc += write_wrapper(filename, (void *)buf, buf_size);
  rc += write_wrapper(filename, delimiter, 4 * sizeof(char));

  /*  MPI_Request r = MPI_REQUEST_NULL;
    if (request != NULL)
      r = *request;
    rc += write_wrapper(filename, &r, sizeof(MPI_Request));

    MPI_Status *s = MPI_STATUS_IGNORE;
    if (status != NULL)
      s = status;
    rc += write_wrapper(filename, s, sizeof(MPI_Status));
  */

  time_t my_time = time(NULL);
  char *time_str = ctime(&my_time);
  time_str[strlen(time_str) - 1] = '\0';

  if (rc != 12) {
    fprintf(
      stdout,
      "\n%s [%d]  -->  Failed to save the api %d record for the counter: %d",
      time_str, g_world_rank, api, counter);
    fflush(stdout);
  }
}

static int Send_counter = 0;
#endif

static bool send_called_me = false;
static bool recv_called_me = false;

USER_DEFINED_WRAPPER(int, Send,
                     (const void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) dest, (int) tag, (MPI_Comm) comm)
{
#if defined B4B_SEND_RECV && B4B_SEND_RECV == 1
  Send_counter++;

  int dummy = 0;
  while (dummy)
    ;

  char *s = getenv("DUMP_SEND_RECV_TRACE");
  int dump_trace = (s != NULL) ? atoi(s) : -1;

  s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  if (dump_trace == 1 && Allreduce_counter > dump_trace_from)
    dump_Send_Recv_trace(10, Send_counter, buf, count, datatype, dest, tag,
                         comm, NULL, NULL);

  time_t my_time = time(NULL);
  char *time_str = ctime(&my_time);

  if (Allreduce_counter > dump_trace_from) {
    my_time = time(NULL);
    time_str = ctime(&my_time);
    time_str[strlen(time_str) - 1] = '\0';

    int comm_size = -1;
    MPI_Comm_size(comm, &comm_size);

    int ds = 0;
    MPI_Type_size(datatype, &ds);
    int buf_size = count * ds;

    char dtstr[30];
    get_datatype_string(datatype, dtstr);

    fprintf(stdout,
            "\n%s [Rank-%d]  -->  Comm: %d & Comm Size: %d & Datatype: %s & "
            "Buffer Size: %d & Dest: %d & Tag: %d & Send Counter: %d",
            time_str, g_world_rank, comm, comm_size, dtstr, buf_size, dest, tag,
            Send_counter);
    fflush(stdout);
  }
#endif

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
  send_called_me = true;
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

#if defined B4B_SEND_RECV && B4B_SEND_RECV == 1
static int Isend_counter = 0;
#endif

USER_DEFINED_WRAPPER(int, Isend,
                     (const void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) dest, (int) tag,
                     (MPI_Comm) comm, (MPI_Request *) request)
{
  if (send_called_me == true) {
    send_called_me = false;
  } else {
#if defined B4B_SEND_RECV && B4B_SEND_RECV == 1
    Isend_counter++;

    int dummy = 0;
    while (dummy)
      ;

    char *s = getenv("DUMP_SEND_RECV_TRACE");
    int dump_trace = (s != NULL) ? atoi(s) : -1;

    s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
    int dump_trace_from = (s != NULL) ? atoi(s) : -1;

    if (dump_trace == 1 && Allreduce_counter > dump_trace_from)
      dump_Send_Recv_trace(11, Isend_counter, buf, count, datatype, dest, tag,
                           comm, request, NULL);

    time_t my_time = time(NULL);
    char *time_str = ctime(&my_time);

    if (Allreduce_counter > dump_trace_from) {
      my_time = time(NULL);
      time_str = ctime(&my_time);
      time_str[strlen(time_str) - 1] = '\0';

      int comm_size = -1;
      MPI_Comm_size(comm, &comm_size);

      int ds = 0;
      MPI_Type_size(datatype, &ds);
      int buf_size = count * ds;

      char dtstr[30];
      get_datatype_string(datatype, dtstr);

      fprintf(stdout,
              "\n%s [Rank-%d]  -->  Comm: %d & Comm Size: %d & Datatype: %s & "
              "Buffer Size: %d & Dest: %d & Tag: %d & Isend Counter: %d",
              time_str, g_world_rank, comm, comm_size, dtstr, buf_size, dest,
              tag, Isend_counter);
      fflush(stdout);
    }
#endif
  }

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

#if defined B4B_SEND_RECV && B4B_SEND_RECV == 1
static int Recv_counter = 0;
#endif

USER_DEFINED_WRAPPER(int, Recv,
                     (void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) source, (int) tag,
                     (MPI_Comm) comm, (MPI_Status *) status)
{
#if defined B4B_SEND_RECV && B4B_SEND_RECV == 1
  Recv_counter++;

  int dummy = 0;
  while (dummy)
    ;

  char *s = getenv("DUMP_SEND_RECV_TRACE");
  int dump_trace = (s != NULL) ? atoi(s) : -1;

  s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
  int dump_trace_from = (s != NULL) ? atoi(s) : -1;

  if (dump_trace == 1 && Allreduce_counter > dump_trace_from)
    dump_Send_Recv_trace(20, Recv_counter, buf, count, datatype, source, tag,
                         comm, NULL, status);

  time_t my_time = time(NULL);
  char *time_str = ctime(&my_time);

  if (Allreduce_counter > dump_trace_from) {
    my_time = time(NULL);
    time_str = ctime(&my_time);
    time_str[strlen(time_str) - 1] = '\0';

    int comm_size = -1;
    MPI_Comm_size(comm, &comm_size);

    int ds = 0;
    MPI_Type_size(datatype, &ds);
    int buf_size = count * ds;

    char dtstr[30];
    get_datatype_string(datatype, dtstr);

    fprintf(stdout,
            "\n%s [Rank-%d]  -->  Comm: %d & Comm Size: %d & Datatype: %s & "
            "Buffer Size: %d & Source: %d & Tag: %d & Recv Counter: %d",
            time_str, g_world_rank, comm, comm_size, dtstr, buf_size, source,
            tag, Recv_counter);
    fflush(stdout);
  }
#endif

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
  recv_called_me=true;
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

#if defined B4B_SEND_RECV && B4B_SEND_RECV == 1
static int Irecv_counter = 0;
#endif

USER_DEFINED_WRAPPER(int, Irecv,
                     (void *) buf, (int) count, (MPI_Datatype) datatype,
                     (int) source, (int) tag,
                     (MPI_Comm) comm, (MPI_Request *) request)
{
  if (recv_called_me == true) {
    recv_called_me = false;
  } else {
#if defined B4B_SEND_RECV && B4B_SEND_RECV == 1
    Irecv_counter++;

    int dummy = 0;
    while (dummy)
      ;

    char *s = getenv("DUMP_SEND_RECV_TRACE");
    int dump_trace = (s != NULL) ? atoi(s) : -1;

    s = getenv("DUMP_TRACE_FROM_ALLREDUCE_COUNTER");
    int dump_trace_from = (s != NULL) ? atoi(s) : -1;

    if (dump_trace == 1 && Allreduce_counter > dump_trace_from)
      dump_Send_Recv_trace(21, Irecv_counter, buf, count, datatype, source, tag,
                           comm, request, NULL);

    time_t my_time = time(NULL);
    char *time_str = ctime(&my_time);

    int comm_size = -1;
    MPI_Comm_size(comm, &comm_size);

    if (Allreduce_counter > dump_trace_from) {
      my_time = time(NULL);
      time_str = ctime(&my_time);
      time_str[strlen(time_str) - 1] = '\0';

      int comm_size = -1;
      MPI_Comm_size(comm, &comm_size);

      int ds = 0;
      MPI_Type_size(datatype, &ds);
      int buf_size = count * ds;

      char dtstr[30];
      get_datatype_string(datatype, dtstr);

      fprintf(stdout,
              "\n%s [Rank-%d]  -->  Comm: %d & Comm Size: %d & Datatype: %s & "
              "Buffer Size: %d & Source: %d & Tag: %d & Irecv Counter: %d",
              time_str, g_world_rank, comm, comm_size, dtstr, buf_size, source,
              tag, Irecv_counter);
      fflush(stdout);
    }
#endif
  }

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
    *request = MPI_REQUEST_NULL;
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
