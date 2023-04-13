#include <stdio.h>
#include <mpi.h>
#include <dlfcn.h>

# define EXTERNC extern "C"

#define NEXT_FNC(func)                                                       \
  ({                                                                         \
    static __typeof__(&func)_real_ ## func = (__typeof__(&func)) - 1;        \
    if (_real_ ## func == (__typeof__(&func)) - 1) {          \
      _real_ ## func = (__typeof__(&func))dlsym(RTLD_NEXT, # func); \
    }                                                                        \
    _real_ ## func;                                                          \
  })

int Bcast_counter = 0;
int Ireduce_counter = 0;
int Ibcast_counter = 0;


/*
 * Wrapper functions
 */
int MPI_Ireduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
                MPI_Op op, int root, MPI_Comm comm, MPI_Request *request) {
  Ireduce_counter++;
  int ds = 0;
  int buf_size = 0;
  int retval;
  int comm_rank = -1;
  int comm_size = -1;
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  fprintf(
  stdout,
  "MPI_Ireduce: Comm Rank: %d & Comm Size: %d & Datatype: %d & Count: %d & "
  "Buffer Size: %d & Send Buffer Address: %p & Send Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Recv Buffer Address: %p "
  "& Recv Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Ireduce "
  "Counter: %d \n",comm_rank, comm_size,
  datatype, count, buf_size, sendbuf, *((unsigned char *)sendbuf),
  *((unsigned char *)sendbuf + 1), *((unsigned char *)sendbuf + 2),
  *((unsigned char *)sendbuf + 3), *((unsigned char *)sendbuf + 4),
  *((unsigned char *)sendbuf + 5), *((unsigned char *)sendbuf + 6),
  *((unsigned char *)sendbuf + 7), recvbuf, *((unsigned char *)recvbuf),
  *((unsigned char *)recvbuf + 1), *((unsigned char *)recvbuf + 2),
  *((unsigned char *)recvbuf + 3), *((unsigned char *)recvbuf + 4),
  *((unsigned char *)recvbuf + 5), *((unsigned char *)recvbuf + 6),
  *((unsigned char *)recvbuf + 7), Ireduce_counter);
  retval = NEXT_FNC(MPI_Ireduce)(sendbuf, recvbuf, count, datatype, op, root, comm, request);
  return retval;
}

int MPI_Ibcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm, MPI_Request *request) {
  Ibcast_counter++;
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  int retval;

  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;

  fprintf(
    stdout,
    "MPI_Ibcast: Comm Rank: %d "
    "& Comm Size: %d & Root: %d & Datatype:%d & Count: %d & Buffer Size: %d & Buffer Address: %p "
    "& Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Ibcast Counter: "
    "%d\n",
    comm_rank, comm_size, root, datatype, count, buf_size, buffer, *((unsigned char *)buffer),
    *((unsigned char *)buffer + 1), *((unsigned char *)buffer + 2),
    *((unsigned char *)buffer + 3), *((unsigned char *)buffer + 4),
    *((unsigned char *)buffer + 5), *((unsigned char *)buffer + 6),
    *((unsigned char *)buffer + 7), Ibcast_counter);

  retval = NEXT_FNC(MPI_Ibcast)(buffer, count, datatype, root, comm, request);
  return retval;
}

int MPI_Bcast(void *buffer, int count, MPI_Datatype datatype, int root, 
               MPI_Comm comm ) {
  Bcast_counter++;
  int comm_rank = -1;
  int comm_size = -1;
  int ds = 0;
  int buf_size = 0;
  int retval;
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);
  MPI_Type_size(datatype, &ds);
  buf_size = count * ds;
  fprintf(
    stdout,
    "MPI_Bcast: Comm Rank: %d & Comm Size: %d & Root: %d & Datatype: %d & Count: %d & Buffer Size: %d & Buffer Address: %p "
    "& Buffer: %02x %02x %02x %02x %02x %02x %02x %02x & Bcast Counter: "
    "%d \n", comm_rank, comm_size, root, datatype, count, buf_size, buffer, *((unsigned char *)buffer),
    *((unsigned char *)buffer + 1), *((unsigned char *)buffer + 2),
    *((unsigned char *)buffer + 3), *((unsigned char *)buffer + 4),
    *((unsigned char *)buffer + 5), *((unsigned char *)buffer + 6),
    *((unsigned char *)buffer + 7), Bcast_counter);
  retval = NEXT_FNC(MPI_Bcast)(buffer, count, datatype, root, comm);
  return retval;
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

// __attribute__((constructor)) void init_mpi_ireduce_wrapper() {
//   fprintf(stdout, "+++++++++++++++++++\n");
//   fprintf(stdout, "Wrappers Initiated\n");
//   fprintf(stdout, "+++++++++++++++++++\n");
// }

