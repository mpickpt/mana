#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include "mpi_logger_utils.h"

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
int Send_counter = 0;
int Isend_counter = 0;
int Recv_counter = 0;
int Irecv_counter = 0;
int Cart_sub_counter = 0;

/**
* MPI initializing calls
*/
int MPI_Init_thread(int *argc, char ***argv, int required, int *provided) {
  #if ENABLE_LOGGER_PRINT
  //@todo: log it
  char *result = NULL;
  size_t len = 0;
  for (int i = 0; i < *argc; i++) {
      len += strlen(argv[i][0]) + 1; // add 1 for space or null terminator
  }
  result = (char*)malloc(len * sizeof(char));
  result[0] = '\0';
  for (int i = 0; i < *argc; i++) {
      strcat(result, argv[i][0]);
      strcat(result, " ");
  }
  fprintf(stdout, "MPI_Init_thread: argc: %d, argv: %s, required: %d, provided: %d\n", *argc, result, required, *provided);
  free(result);
  #endif
  int retval;
  retval = NEXT_FNC(MPI_Init_thread)(argc, argv, required, provided);
  return retval;
}

int MPI_Group_rank(MPI_Group group, int *rank) {
  #if ENABLE_LOGGER_PRINT
  int size;
  MPI_Group_size(group, &size);
  fprintf(stdout, "MPI_Group_rank: group size: %d, rank: %d\n", size, *rank);
  #endif
  int retval;
  retval = NEXT_FNC(MPI_Group_rank)(group, rank);
  return retval;
}

int MPI_Barrier(MPI_Comm comm) {
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  fprintf(stdout, "MPI_Barrier: Comm rank: %d, Comm size: %d\n", comm_rank, comm_size);
  #endif
  int retval;
  retval = NEXT_FNC(MPI_Barrier)(comm);
  return retval;
}

int MPI_Cart_sub(MPI_Comm comm, const int remain_dims[], MPI_Comm *newcomm) {
  Cart_sub_counter++;
  int retval;
  retval = NEXT_FNC(MPI_Cart_sub)(comm, remain_dims, newcomm);
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  int new_comm_rank = -1;
  int new_comm_size = -1;
  MPI_Comm_rank(*newcomm, &new_comm_rank);
  MPI_Comm_size(*newcomm, &new_comm_size);
  int dim_size = sizeof(remain_dims) / sizeof(int);

  char str[100] = "";

  for (int i = 0; i < dim_size; i++) {
    char temp[10];
    sprintf(temp, "%d", remain_dims[i]);
    strcat(str, temp);
    if (i < dim_size - 1) {
      strcat(str, ", ");
    }
  }

  fprintf(stdout,
    "MPI_Cart_sub: Comm rank: %d, Comm size: %d, sub Comm rank: %d, sub Comm size: %d, remain dims: %s\n", 
    comm_rank, comm_size, new_comm_rank, new_comm_size, str);
  #endif

  return retval;
}

/*
 * P2P calls wrapper function
 */
int MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
  Send_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  int checksum = -1;
  char dtstr[30];
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;

  get_datatype_string(datatype, dtstr);

  fprintf(
    stdout,
    "MPI_Send: Comm Rank: %d & "
    "Comm Size: %d & Datatype: %s-%d & Buffer Size: %d & Buffer Address: %p & Buffer: %02x %02x %02x "
    "%02x %02x %02x %02x %02x & Dest: %d & Tag: %d & Send Counter: %d\n",
    comm_rank, comm_size,
    dtstr, datatype, buf_size, buf, *((unsigned char *)buf), *((unsigned char *)buf + 1),
    *((unsigned char *)buf + 2), *((unsigned char *)buf + 3),
    *((unsigned char *)buf + 4), *((unsigned char *)buf + 5),
    *((unsigned char *)buf + 6), *((unsigned char *)buf + 7), dest, tag,
    Send_counter);

  #endif
  int retval;
  retval = NEXT_FNC(MPI_Send)(buf, count, datatype, dest, tag, comm);
  return retval;
}

int MPI_Isend(const void *buf, int count, MPI_Datatype datatype, int dest, int tag,
              MPI_Comm comm, MPI_Request *request) {
    Isend_counter++;
    #if ENABLE_LOGGER_PRINT
    int comm_rank = -1;
    int comm_size = -1;
    int ds = 0;
    int buf_size = 0;
    char dtstr[30];
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    MPI_Type_size(datatype, &ds);
    buf_size = count * ds;

    get_datatype_string(datatype, dtstr);

    fprintf(
      stdout,
      "MPI_Isend: Comm Rank: %d & "
      "Comm Size: %d & Datatype: %s-%d & Buffer Size: %d & Buffer Address: %p & Buffer: %02x %02x %02x "
      "%02x %02x %02x %02x %02x & Dest: %d & Tag: %d & Isend Counter: %d\n",
      comm_rank, comm_size,
      dtstr, datatype, buf_size, buf, *((unsigned char *)buf), *((unsigned char *)buf + 1),
      *((unsigned char *)buf + 2), *((unsigned char *)buf + 3),
      *((unsigned char *)buf + 4), *((unsigned char *)buf + 5),
      *((unsigned char *)buf + 6), *((unsigned char *)buf + 7), dest, tag,
      Isend_counter);
    #endif
    int retval;
    retval = NEXT_FNC(MPI_Isend)(buf, count, datatype, dest, tag, comm, request);
    return retval;
}

int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag,
             MPI_Comm comm, MPI_Status *status) {
  Recv_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  int checksum = -1;
  char dtstr[30];

  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  get_datatype_string(datatype, dtstr);
  
  fprintf(
    stdout,
    "MPI_Recv: Comm Rank: %d & "
    "Comm Size: %d & Datatype: %s-%d & Buffer Size: %d & Buffer Address: %p & Buffer: %02x %02x %02x "
    "%02x %02x %02x %02x %02x & Source: %d & Tag: %d & Recv Counter: %d\n",
    comm_rank, comm_size,
    dtstr, datatype, buf_size, buf, *((unsigned char *)buf), *((unsigned char *)buf + 1),
    *((unsigned char *)buf + 2), *((unsigned char *)buf + 3),
    *((unsigned char *)buf + 4), *((unsigned char *)buf + 5),
    *((unsigned char *)buf + 6), *((unsigned char *)buf + 7), source, tag,
    Recv_counter);
  #endif
  int retval;
  retval = NEXT_FNC(MPI_Recv)(buf, count, datatype, source, tag, comm, status);
  return retval;
}

int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source,
              int tag, MPI_Comm comm, MPI_Request * request) {
  Irecv_counter ++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  char dtstr[30];
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  get_datatype_string(datatype, dtstr);

  fprintf(
  stdout,
  "MPI_Irecv: Comm Rank: %d & "
  "Comm Size: %d & Datatype: %s-%d & Buffer Size: %d & Buffer Address: %p & Buffer: %02x %02x %02x "
  "%02x %02x %02x %02x %02x & Source: %d & Tag: %d & Irecv Counter: %d\n",
  comm_rank, comm_size,
  dtstr, datatype, buf_size, buf, *((unsigned char *)buf), *((unsigned char *)buf + 1),
  *((unsigned char *)buf + 2), *((unsigned char *)buf + 3),
  *((unsigned char *)buf + 4), *((unsigned char *)buf + 5),
  *((unsigned char *)buf + 6), *((unsigned char *)buf + 7), source, tag,
  Irecv_counter);
#endif
  #endif
  int retval;
  retval = NEXT_FNC(MPI_Irecv)(buf, count, datatype, source, tag, comm, request);
  return retval;
}

/*
 * Collective calls wrapper functions
 */
int MPI_Ireduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
                MPI_Op op, int root, MPI_Comm comm, MPI_Request *request) {
  Ireduce_counter++;
  #if ENABLE_LOGGER_PRINT
  int ds = 0;
  int buf_size = 0;
  int comm_rank = -1;
  int comm_size = -1;
  char dtstr[30], opstr[30];
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  get_datatype_string(datatype, dtstr);
  get_op_string(op, opstr); 
  fprintf(
  stdout,
  "MPI_Ireduce: Comm Rank: %d & Comm Size: %d & Root: %d & Operation: %s & Datatype: %s-%d & Count: %d & "
  "Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p "
  "& Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Ireduce "
  "Counter: %d \n",comm_rank, comm_size, root, opstr, dtstr,
  datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
  *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
  *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
  *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
  *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
  *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
  *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
  *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
  *((unsigned char *)recvbuf + 7), Ireduce_counter);
  #endif
  int retval;
  retval = NEXT_FNC(MPI_Ireduce)(sendbuf, recvbuf, count, datatype, op, root, comm, request);
  return retval;
}

int MPI_Ibcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm, MPI_Request *request) {
  Ibcast_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  char dtstr[30];

  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  get_datatype_string(datatype, dtstr);

  fprintf(
    stdout,
    "MPI_Ibcast: Comm Rank: %d "
    "& Comm Size: %d & Root: %d & Datatype:%s-%d & Count: %d & Buffer Size: %d & Buffer Address: %p "
    "& Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Ibcast Counter: "
    "%d\n",
    comm_rank, comm_size, root, dtstr, datatype, count, buf_size, buffer, *((unsigned char *)buffer),
    *((unsigned char *)buffer + 1), *((unsigned char *)buffer + 2),
    *((unsigned char *)buffer + 3), *((unsigned char *)buffer + 4),
    *((unsigned char *)buffer + 5), *((unsigned char *)buffer + 6),
    *((unsigned char *)buffer + 7), Ibcast_counter);
  #endif
  int retval;
  retval = NEXT_FNC(MPI_Ibcast)(buffer, count, datatype, root, comm, request);
  return retval;
}

int MPI_Bcast(void *buffer, int count, MPI_Datatype datatype, int root, 
               MPI_Comm comm ) {
  Bcast_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  char dtstr[30];
  get_datatype_string(datatype, dtstr);

  fprintf(
    stdout,
    "MPI_Bcast: Comm Rank: %d & Comm Size: %d & Root: %d & Datatype: %s-%d & Count: %d & Buffer Size: %d & Buffer Address: %p "
    "& Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Bcast Counter: "
    "%d \n", comm_rank, comm_size, root, dtstr, datatype, count, buf_size, buffer, *((unsigned char *)buffer),
    *((unsigned char *)buffer + 1), *((unsigned char *)buffer + 2),
    *((unsigned char *)buffer + 3), *((unsigned char *)buffer + 4),
    *((unsigned char *)buffer + 5), *((unsigned char *)buffer + 6),
    *((unsigned char *)buffer + 7), Bcast_counter);
  #endif
  int retval;  
  retval = NEXT_FNC(MPI_Bcast)(buffer, count, datatype, root, comm);
  return retval;
}

int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm)
{
  Allreduce_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  char dtstr[30], opstr[30];
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  get_datatype_string(datatype, dtstr);
  get_op_string(op, opstr);
  fprintf(
      stdout,
      "MPI_Allreduce: Comm Rank: %d "
      "& Comm Size: %d & Operation: %s & Datatype: %s-%d & Count: %d & Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p & Recv "
      "Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Allreduce Counter: %d\n",
      comm_rank, comm_size,
      opstr, dtstr, datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
      *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
      *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
      *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
      *((unsigned char *)recvbuf + 7), Allreduce_counter);
  #endif
  int retval = 0;
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  retval = NEXT_FNC(MPI_Allreduce)(sendbuf, recvbuf, count, datatype, op, comm);
  return retval;
}

int MPI_Reduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
               MPI_Op op, int root, MPI_Comm comm)
{
  Reduce_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  char dtstr[30], opstr[30];
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  get_datatype_string(datatype, dtstr);
  get_op_string(op, opstr);
  fprintf(
        stdout,
        "MPI_Reduce: Comm Rank: %d "
        "& Comm Size: %d & Root: %d & Operation: %s & Datatype: %s-%d & Count: %d & "
        "Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p "
        "& Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Reduce "
        "Counter: %d\n",
        comm_rank, comm_size, root,
        opstr, dtstr, datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
        *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
        *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
        *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
        *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
        *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
        *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
        *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
        *((unsigned char *)recvbuf + 7), Reduce_counter);
  #endif
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  int retval = 0;
  retval = NEXT_FNC(MPI_Reduce)(sendbuf, recvbuf, count, datatype, op, root, comm);
  return retval;
}

int MPI_Alltoall(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                 void *recvbuf, int recvcount, MPI_Datatype recvtype,
                 MPI_Comm comm)
{
  Alltoall_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  char sdtstr[30], rdtstr[30];

  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(sendtype, &ds);
  sbuf_size = sendcount * ds;
  get_datatype_string(sendtype, sdtstr);

  MPI_Type_size(recvtype, &ds);
  rbuf_size = recvcount * ds;
  get_datatype_string(recvtype, rdtstr);

  fprintf(
      stdout,
      "MPI_Alltoall: Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Alltoall Counter: %d\n",
      comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, recvcount, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Alltoall_counter);
  #endif
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  int retval;
  retval = NEXT_FNC(MPI_Alltoall)(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
}

int MPI_Alltoallv(const void *sendbuf, const int *sendcounts,
                  const int *sdispls, MPI_Datatype sendtype, void *recvbuf,
                  const int *recvcounts, const int *rdispls, MPI_Datatype recvtype,
                  MPI_Comm comm)
{
  Alltoallv_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  char sdtstr[30], rdtstr[30];

  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(sendtype, &ds);
  sbuf_size = (*sendcounts) * ds;
  get_datatype_string(sendtype, sdtstr);

  MPI_Type_size(recvtype, &ds);
  rbuf_size = *recvcounts * ds;
  get_datatype_string(recvtype, rdtstr);

  fprintf(
    stdout,
    "MPI_Alltoallv: Comm Rank: %d "
    "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
    "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
    "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
    "%02x %02x %02x %02x %02x %02x %02x %02x & Alltoallv Counter: %d\n",
    comm_rank, comm_size,
    sdtstr, sendtype, (*sendcounts), sbuf_size, sendbuf, *((unsigned char *)sendbuf),
    *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
    *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
    *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
    *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size, recvbuf, 
    *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
    *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
    *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
    *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
    Alltoallv_counter);
  
  #endif
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  int retval;
  retval = NEXT_FNC(MPI_Alltoallv)(sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls, recvtype, comm);
  return retval;
}

int MPI_Gatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                void *recvbuf, const int *recvcounts, const int *displs,
                MPI_Datatype recvtype, int root, MPI_Comm comm)
{
  Gatherv_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  char sdtstr[30], rdtstr[30];
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(sendtype, &ds);
  sbuf_size = sendcount * ds;
  get_datatype_string(sendtype, sdtstr);

  MPI_Type_size(recvtype, &ds);
  rbuf_size = *recvcounts * ds;
  get_datatype_string(recvtype, rdtstr);
  fprintf(
        stdout,
        "MPI_Gatherv: Comm Rank: %d "
        "& Comm Size: %d & Root: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Gatherv Counter: %d\n",
        comm_rank, comm_size,root,
        sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
        *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
        *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
        *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
        *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size,recvbuf,
        *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
        *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
        *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
        *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
        Gatherv_counter);
  #endif
  int retval;
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  retval = NEXT_FNC(MPI_Gatherv)(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm);
  return retval;
}

int MPI_Allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                  void *recvbuf, int recvcount, MPI_Datatype recvtype,
                  MPI_Comm comm)
{
  Allgather_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  char sdtstr[30], rdtstr[30];
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(sendtype, &ds);
  sbuf_size = sendcount * ds;
  get_datatype_string(sendtype, sdtstr);

  MPI_Type_size(recvtype, &ds);
  rbuf_size = recvcount * ds;
  get_datatype_string(recvtype, rdtstr);

  fprintf(
      stdout,
      "MPI_Allgather: Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Allgather Counter: %d\n",
      comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, recvcount, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Allgather_counter);
  #endif
  int retval;
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    sendbuf = MPI_IN_PLACE;
  }
  retval = NEXT_FNC(MPI_Allgather)(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
  return retval;
}

int MPI_Allgatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                   void *recvbuf, const int *recvcounts, const int *displs,
                   MPI_Datatype recvtype, MPI_Comm comm)
{
  Allgatherv_counter++;
  #if ENABLE_LOGGER_PRINT
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int sbuf_size = 0;
  int rbuf_size = 0;
  char sdtstr[30], rdtstr[30];
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(sendtype, &ds);
  sbuf_size = sendcount * ds;
  get_datatype_string(sendtype, sdtstr);

  MPI_Type_size(recvtype, &ds);
  rbuf_size = *recvcounts * ds;
  get_datatype_string(recvtype, rdtstr);

  fprintf(
      stdout,
      "MPI_Allgatherv: Comm Rank: %d "
      "& Comm Size: %d & Send Datatype: %s-%d & Send Count: %d & Send Buffer "
      "Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv "
      "Datatype: %s-%d & Recv Count: %d & Recv Buffer Size: %d & Recv Buffer Address: %p & Recv Buffer: "
      "%02x %02x %02x %02x %02x %02x %02x %02x & Allgatherv Counter: %d\n",
      comm_rank, comm_size,
      sdtstr, sendtype, sendcount, sbuf_size, sendbuf, *((unsigned char *)sendbuf),
      *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
      *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
      *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
      *((unsigned char *)sendbuf + 7), rdtstr, recvtype, *recvcounts, rbuf_size, recvbuf,
      *((unsigned char *)recvbuf), *((unsigned char *)recvbuf + 1),
      *((unsigned char *)recvbuf + 2), *((unsigned char *)recvbuf + 3),
      *((unsigned char *)recvbuf + 4), *((unsigned char *)recvbuf + 5),
      *((unsigned char *)recvbuf + 6), *((unsigned char *)recvbuf + 7),
      Allgatherv_counter);
  #endif
  int retval;
  retval = NEXT_FNC(MPI_Allgatherv)(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm);
  return retval;
}

EXTERNC int mpi_init_thread_ (int* required, int* provided, int *ierr) {
  int argc = 0;
  char **argv;
  *ierr = MPI_Init_thread(&argc, &argv, *required, provided);
  return *ierr;
}

EXTERNC int mpi_group_rank_ (MPI_Group* group,  int* rank, int *ierr) {
  *ierr = MPI_Group_rank(*group, rank);
  return *ierr;
}

EXTERNC int mpi_barrier_ (MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Barrier(*comm);
  return *ierr;
}

EXTERNC int mpi_cart_sub_ (MPI_Comm* comm,  const int* remain_dims,  MPI_Comm* new_comm, int *ierr) {
  *ierr = MPI_Cart_sub(*comm, remain_dims, new_comm);
  return *ierr;
}

EXTERNC int mpi_ireduce_ (const void* sendbuf,  void* recvbuf,  int* count,  MPI_Datatype* datatype,  MPI_Op* op,  int* root,  MPI_Comm* comm,  MPI_Request* request, int *ierr) {
  *ierr = MPI_Ireduce(sendbuf, recvbuf, *count, *datatype, *op, *root, *comm, request);
  return *ierr;
}

EXTERNC int mpi_ibcast_ (void* buffer,  int* count,  MPI_Datatype* datatype,  int* root,  MPI_Comm* comm,  MPI_Request* request, int *ierr) {
  *ierr = MPI_Ibcast(buffer, *count, *datatype, *root, *comm, request);
  return *ierr;
}

EXTERNC int mpi_bcast_ (void* buffer,  int* count,  MPI_Datatype* datatype,  int* root,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Bcast(buffer, *count, *datatype, *root, *comm);
  return *ierr;
}

EXTERNC int mpi_allreduce_ (const void* sendbuf,  void* recvbuf,  int* count,  MPI_Datatype* datatype,  MPI_Op* op,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Allreduce(sendbuf, recvbuf, *count, *datatype, *op, *comm);
  return *ierr;
}

EXTERNC int mpi_reduce_ (const void* sendbuf,  void* recvbuf,  int* count,  MPI_Datatype* datatype,  MPI_Op* op,  int* root,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Reduce(sendbuf, recvbuf, *count, *datatype, *op, *root, *comm);
  return *ierr;
}

EXTERNC int mpi_alltoall_ (const void* sendbuf,  int* sendcount,  MPI_Datatype* sendtype,  void* recvbuf,  int* recvcount,  MPI_Datatype* recvtype,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Alltoall(sendbuf, *sendcount, *sendtype, recvbuf, *recvcount, *recvtype, *comm);
  return *ierr;
}

EXTERNC int mpi_alltoallv_ (const void* sendbuf,  const int* sendcounts,  const int* sdispls,  MPI_Datatype* sendtype,  void* recvbuf,  const int* recvcounts,  const int* rdispls,  MPI_Datatype* recvtype,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Alltoallv(sendbuf, sendcounts, sdispls, *sendtype, recvbuf, recvcounts, rdispls, *recvtype, *comm);
  return *ierr;
}

EXTERNC int mpi_allgather_ (const void* sendbuf,  int* sendcount,  MPI_Datatype* sendtype,  void* recvbuf,  int* recvcount,  MPI_Datatype* recvtype,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Allgather(sendbuf, *sendcount, *sendtype, recvbuf, *recvcount, *recvtype, *comm);
  return *ierr;
}

EXTERNC int mpi_allgatherv_ (const void*  sendbuf,  int* sendcount,  MPI_Datatype* sendtype,  void* recvbuf,  const int* recvcount,  const int* displs,  MPI_Datatype* recvtype,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Allgatherv(sendbuf, *sendcount, *sendtype, recvbuf, recvcount, displs, *recvtype, *comm);
  return *ierr;
}

EXTERNC int mpi_gatherv_ (const void* sendbuf,  int* sendcount,  MPI_Datatype* sendtype,  void* recvbuf,  const int* recvcounts,  const int* displs,  MPI_Datatype* recvtype,  int* root,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Gatherv(sendbuf, *sendcount, *sendtype, recvbuf, recvcounts, displs, *recvtype, *root, *comm);
  return *ierr;
}

EXTERNC int mpi_send_ (const void* buf,  int* count,  MPI_Datatype* datatype,  int* dest,  int* tag,  MPI_Comm* comm, int *ierr) {
  *ierr = MPI_Send(buf, *count, *datatype, *dest, *tag, *comm);
  return *ierr;
}

EXTERNC int mpi_isend_ (const void* buf,  int* count,  MPI_Datatype* datatype,  int* dest,  int* tag,  MPI_Comm* comm,  MPI_Request* request, int *ierr) {
  *ierr = MPI_Isend(buf, *count, *datatype, *dest, *tag, *comm, request);
  return *ierr;
}

EXTERNC int mpi_recv_ (void* buf,  int* count,  MPI_Datatype* datatype,  int* source,  int* tag,  MPI_Comm* comm,  MPI_Status* status, int *ierr) {
  *ierr = MPI_Recv(buf, *count, *datatype, *source, *tag, *comm, status);
  return *ierr;
}

EXTERNC int mpi_irecv_ (void* buf,  int* count,  MPI_Datatype* datatype,  int* source,  int* tag,  MPI_Comm* comm,  MPI_Request* request, int *ierr) {
  *ierr = MPI_Irecv(buf, *count, *datatype, *source, *tag, *comm, request);
  return *ierr;
}
