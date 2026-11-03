/*
 * mpitrace: log the buffers and sizes of MPI point-to-point and collective
 * calls, so they can be matched against the allocations cudatrace logged.
 *
 * Loaded with LD_PRELOAD.  Each wrapper logs, then calls the PMPI_ version.
 * Nonblocking calls are logged when they are posted.
 *
 * Output: $ALLOC_TRACE_DIR/mpi-<host>-<pid>.csv (default: current directory)
 *   t_ns,rank,api,sendbuf,recvbuf,bytes,peer
 * bytes is what this rank sends (or receives, for receives); for the "v"
 * collectives it is the sum over all ranks.  peer is the destination, source
 * or root, -1 when there is none.  An MPI_IN_PLACE buffer is logged as 0.
 */
#define _GNU_SOURCE
#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static FILE *out;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int rank = -1;

static unsigned long long
now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static void
open_log(void)
{
  const char *dir = getenv("ALLOC_TRACE_DIR");
  char host[64] = "host", path[4096];
  gethostname(host, sizeof host);
  snprintf(path, sizeof path, "%s/mpi-%s-%d.csv", dir ? dir : ".", host,
           getpid());
  out = fopen(path, "w");
  if (!out) {
    perror(path);
    return;
  }
  setvbuf(out, NULL, _IOFBF, 1 << 20);
  fprintf(out, "t_ns,rank,api,sendbuf,recvbuf,bytes,peer\n");
  PMPI_Comm_rank(MPI_COMM_WORLD, &rank);
}

static unsigned long long
nbytes(long long count, MPI_Datatype type)
{
  int size = 0;
  if (type != MPI_DATATYPE_NULL)
    PMPI_Type_size(type, &size);
  return (unsigned long long)count * size;
}

// Sum of a "v" collective's counts over the communicator.
static long long
total(const int counts[], MPI_Comm comm)
{
  int n, inter;
  long long sum = 0;
  PMPI_Comm_test_inter(comm, &inter);
  if (inter)
    PMPI_Comm_remote_size(comm, &n);
  else
    PMPI_Comm_size(comm, &n);
  for (int i = 0; i < n; i++)
    sum += counts[i];
  return sum;
}

static unsigned long long
addr(const void *buf)
{
  return buf == MPI_IN_PLACE ? 0 : (unsigned long long)buf;
}

static void
emit(const char *api, const void *sendbuf, const void *recvbuf,
     unsigned long long bytes, int peer)
{
  if (!out)
    return;
  pthread_mutex_lock(&lock);
  fprintf(out, "%llu,%d,%s,%#llx,%#llx,%llu,%d\n", now_ns(), rank, api,
          addr(sendbuf), addr(recvbuf), bytes, peer);
  pthread_mutex_unlock(&lock);
}

int
MPI_Init(int *argc, char ***argv)
{
  int rc = PMPI_Init(argc, argv);
  open_log();
  return rc;
}

int
MPI_Init_thread(int *argc, char ***argv, int required, int *provided)
{
  int rc = PMPI_Init_thread(argc, argv, required, provided);
  open_log();
  return rc;
}

int
MPI_Finalize(void)
{
  if (out) {
    pthread_mutex_lock(&lock);
    fclose(out);
    out = NULL;
    pthread_mutex_unlock(&lock);
  }
  return PMPI_Finalize();
}

// Point to point.
int
MPI_Send(const void *buf, int count, MPI_Datatype type, int dest, int tag,
         MPI_Comm comm)
{
  emit("MPI_Send", buf, NULL, nbytes(count, type), dest);
  return PMPI_Send(buf, count, type, dest, tag, comm);
}

int
MPI_Ssend(const void *buf, int count, MPI_Datatype type, int dest, int tag,
          MPI_Comm comm)
{
  emit("MPI_Ssend", buf, NULL, nbytes(count, type), dest);
  return PMPI_Ssend(buf, count, type, dest, tag, comm);
}

int
MPI_Isend(const void *buf, int count, MPI_Datatype type, int dest, int tag,
          MPI_Comm comm, MPI_Request *req)
{
  emit("MPI_Isend", buf, NULL, nbytes(count, type), dest);
  return PMPI_Isend(buf, count, type, dest, tag, comm, req);
}

int
MPI_Recv(void *buf, int count, MPI_Datatype type, int src, int tag,
         MPI_Comm comm, MPI_Status *status)
{
  emit("MPI_Recv", NULL, buf, nbytes(count, type), src);
  return PMPI_Recv(buf, count, type, src, tag, comm, status);
}

int
MPI_Irecv(void *buf, int count, MPI_Datatype type, int src, int tag,
          MPI_Comm comm, MPI_Request *req)
{
  emit("MPI_Irecv", NULL, buf, nbytes(count, type), src);
  return PMPI_Irecv(buf, count, type, src, tag, comm, req);
}

int
MPI_Sendrecv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
             int dest, int sendtag, void *recvbuf, int recvcount,
             MPI_Datatype recvtype, int src, int recvtag, MPI_Comm comm,
             MPI_Status *status)
{
  emit("MPI_Sendrecv", sendbuf, recvbuf, nbytes(sendcount, sendtype), dest);
  return PMPI_Sendrecv(sendbuf, sendcount, sendtype, dest, sendtag, recvbuf,
                       recvcount, recvtype, src, recvtag, comm, status);
}

// Collectives.
int
MPI_Bcast(void *buf, int count, MPI_Datatype type, int root, MPI_Comm comm)
{
  emit("MPI_Bcast", buf, buf, nbytes(count, type), root);
  return PMPI_Bcast(buf, count, type, root, comm);
}

int
MPI_Ibcast(void *buf, int count, MPI_Datatype type, int root, MPI_Comm comm,
           MPI_Request *req)
{
  emit("MPI_Ibcast", buf, buf, nbytes(count, type), root);
  return PMPI_Ibcast(buf, count, type, root, comm, req);
}

int
MPI_Reduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype type,
           MPI_Op op, int root, MPI_Comm comm)
{
  emit("MPI_Reduce", sendbuf, recvbuf, nbytes(count, type), root);
  return PMPI_Reduce(sendbuf, recvbuf, count, type, op, root, comm);
}

int
MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
              MPI_Datatype type, MPI_Op op, MPI_Comm comm)
{
  emit("MPI_Allreduce", sendbuf, recvbuf, nbytes(count, type), -1);
  return PMPI_Allreduce(sendbuf, recvbuf, count, type, op, comm);
}

int
MPI_Iallreduce(const void *sendbuf, void *recvbuf, int count,
               MPI_Datatype type, MPI_Op op, MPI_Comm comm, MPI_Request *req)
{
  emit("MPI_Iallreduce", sendbuf, recvbuf, nbytes(count, type), -1);
  return PMPI_Iallreduce(sendbuf, recvbuf, count, type, op, comm, req);
}

int
MPI_Reduce_scatter_block(const void *sendbuf, void *recvbuf, int recvcount,
                         MPI_Datatype type, MPI_Op op, MPI_Comm comm)
{
  emit("MPI_Reduce_scatter_block", sendbuf, recvbuf, nbytes(recvcount, type),
       -1);
  return PMPI_Reduce_scatter_block(sendbuf, recvbuf, recvcount, type, op,
                                   comm);
}

int
MPI_Gather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
           void *recvbuf, int recvcount, MPI_Datatype recvtype, int root,
           MPI_Comm comm)
{
  emit("MPI_Gather", sendbuf, recvbuf, nbytes(sendcount, sendtype), root);
  return PMPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount,
                     recvtype, root, comm);
}

int
MPI_Scatter(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
            void *recvbuf, int recvcount, MPI_Datatype recvtype, int root,
            MPI_Comm comm)
{
  emit("MPI_Scatter", sendbuf, recvbuf, nbytes(recvcount, recvtype), root);
  return PMPI_Scatter(sendbuf, sendcount, sendtype, recvbuf, recvcount,
                      recvtype, root, comm);
}

int
MPI_Allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
              void *recvbuf, int recvcount, MPI_Datatype recvtype,
              MPI_Comm comm)
{
  emit("MPI_Allgather", sendbuf, recvbuf, nbytes(sendcount, sendtype), -1);
  return PMPI_Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount,
                        recvtype, comm);
}

int
MPI_Allgatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
               void *recvbuf, const int recvcounts[], const int displs[],
               MPI_Datatype recvtype, MPI_Comm comm)
{
  emit("MPI_Allgatherv", sendbuf, recvbuf,
       nbytes(total(recvcounts, comm), recvtype), -1);
  return PMPI_Allgatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts,
                         displs, recvtype, comm);
}

int
MPI_Alltoall(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
             void *recvbuf, int recvcount, MPI_Datatype recvtype,
             MPI_Comm comm)
{
  int n;
  PMPI_Comm_size(comm, &n);
  emit("MPI_Alltoall", sendbuf, recvbuf,
       nbytes((long long)sendcount * n, sendtype), -1);
  return PMPI_Alltoall(sendbuf, sendcount, sendtype, recvbuf, recvcount,
                       recvtype, comm);
}

int
MPI_Alltoallv(const void *sendbuf, const int sendcounts[],
              const int sdispls[], MPI_Datatype sendtype, void *recvbuf,
              const int recvcounts[], const int rdispls[],
              MPI_Datatype recvtype, MPI_Comm comm)
{
  emit("MPI_Alltoallv", sendbuf, recvbuf,
       nbytes(total(sendcounts, comm), sendtype), -1);
  return PMPI_Alltoallv(sendbuf, sendcounts, sdispls, sendtype, recvbuf,
                        recvcounts, rdispls, recvtype, comm);
}
