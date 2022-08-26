#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mpi.h>

/************************************************************************
 * The goal of this file is to _temporarily_ replace all MPI collective
 *   communication calls by point-to-point.  This is used _only_
 *   for debugging, since it adds substantial runtime overhead.
 * This replaces all MPI collective communication calls (calls that
 *   use a communicator to send and receive messages) by subroutines
 *   that use only MPI point-to-point and other non-collective calls.
 * TODO:  Replace tag of 0 by a semi-unique tag to guarantee that
 *   our internal messages are not confused with that of the user.
 *   In principle, we could use MPI_Comm_dup(), but too much overhead,
 *   and it would be more difficult to debug.
 * NOTE:  Do 'google MPI standard 3.1' to see the spec in the standard.
 *        This first version does not consider intracommunicators.
 *        This version does not test for return values.
 ************************************************************************/

// Add '#define ADD_UNDEFINED' if you want to define functions that are
//   also in mpi_unimplemented_wrappers.txt

#define PROLOG_Comm_rank_size \
  int rank; \
  int size; \
  MPI_Comm_rank(comm, &rank); \
  MPI_Comm_size(comm, &size); \
  if (rank < 0 || size < 1) { \
    fprintf(stderr, "Error (aborting): " __FILE__ "(%d):%s\n", \
                    __LINE__, __FUNCTION__); \
    fflush(stderr); \
    abort(); \
  }
#define ABORT() \
    fprintf(stderr, "Error (aborting): " __FILE__ "(%d):%s\n", \
                    __LINE__, __FUNCTION__); \
    fflush(stderr); \
    abort()
#define ACTIVATE_REQUEST(request) \
  MPI_Ibarrier(MPI_COMM_SELF, request)

/*
 * TEMPLATE FOR EACH COLLECTIVE CALL:
....(...) {
  PROLOG_Comm_rank_size;
  if (rank == root) {
    int i;
    for (i = 0; i < size; i++) {
      if (i != root) {
        ...
      }
    }
  } else {
    ...
  }
  return MPI_SUCCESS;
}
 */


#ifdef __cplusplus
extern "C" {
#endif

// MPI standard 3.1:  Section 5.3
int MPI_Barrier(MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  // Does MPI specify that a send/recv count of 0 must be blocking?
  int buffer[1] = {98};
  int count = 1;
  MPI_Datatype datatype = MPI_INT;
  int root = 0;
  if (rank == root) {
    int i;
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Recv(buffer, count, datatype, i, 0, comm, MPI_STATUS_IGNORE);
      }
    }
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Send(buffer, count, datatype, i, 0, comm);
      }
    }
  } else { // else: rank != root
    MPI_Send(buffer, count, datatype, root, 0, comm);
    MPI_Recv(buffer, count, datatype, root, 0, comm, MPI_STATUS_IGNORE);
  }
  return MPI_SUCCESS;
}

// MPI standard 3.1:  Section 5.4
int MPI_Bcast(void* buffer, int count, MPI_Datatype datatype,
              int root, MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  if (rank == root) {
    int i;
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Send(buffer, count, datatype, i, 0, comm);
      }
    }
  } else {
    MPI_Recv(buffer, count, datatype, root, 0, comm, MPI_STATUS_IGNORE);
  }
  return MPI_SUCCESS;
}

// MPI standard 3.1:  Section 5.5
int MPI_Gather(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
               void* recvbuf, int recvcount, MPI_Datatype recvtype,
               int root, MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  int i;
  int inplace = (sendbuf == MPI_IN_PLACE || FORTRAN_MPI_IN_PLACE);
  if (rank != root) {
    MPI_Send(sendbuf, sendcount, sendtype, root, 0, comm);
  } else { // else: rank == root
    MPI_Aint lower_bound;
    MPI_Aint sendextent, recvextent;
    MPI_Type_get_extent(recvtype, &lower_bound, &recvextent);
    if (inplace) {
      sendextent = recvextent;
    } else {
      MPI_Type_get_extent(sendtype, &lower_bound, &sendextent);
    }
    assert(sendextent*sendcount == recvextent*recvcount);
    if (!inplace) {
     memcpy(recvbuf + rank*recvextent*recvcount, sendbuf, sendextent*sendcount);
    } // NOTE: if inplace, MPI guarantees that the root data is already correct
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Recv(recvbuf + i*recvcount*recvextent, recvcount, recvtype,
                 i, 0, comm, MPI_STATUS_IGNORE);
      }
    }
  }
  return MPI_SUCCESS;
}

int MPI_Gatherv(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
                void* recvbuf, const int recvcounts[], const int displs[],
                MPI_Datatype recvtype, int root, MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  int i;
  int inplace = (sendbuf == MPI_IN_PLACE || FORTRAN_MPI_IN_PLACE);
  if (rank != root) {
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Send(sendbuf, sendcount, sendtype, i, 0, comm);
      }
    }
  } else { // else: rank == root
    MPI_Aint lower_bound;
    MPI_Aint sendextent, recvextent;
    MPI_Type_get_extent(recvtype, &lower_bound, &recvextent);
    if (inplace) {
      sendextent = recvextent;
    } else {
      MPI_Type_get_extent(sendtype, &lower_bound, &sendextent);
    }
    if (!inplace) {
      // NOTE: if inplace, MPI guarantees that the root data is already correct
      memcpy(recvbuf + displs[root]*recvextent, sendbuf, sendextent*sendcount);
    }
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Recv(recvbuf + displs[i]*recvextent /* displs[i] == i*recvcount */,
                 recvcounts[i], recvtype, i, 0, comm, MPI_STATUS_IGNORE);
      }
    }
  }
  return MPI_SUCCESS;
}

// MPI standard 3.1:  Section 5.6
int MPI_Scatter(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
                void* recvbuf, int recvcount, MPI_Datatype recvtype,
                int root, MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  int i;
  int inplace = (sendbuf == MPI_IN_PLACE || FORTRAN_MPI_IN_PLACE);
  if (inplace) { // if true, MPI says to ignore recvcount/recvtype
    recvcount = sendcount;
    recvtype = sendtype;
  }
  if (rank == root) {
    MPI_Aint lower_bound;
    MPI_Aint sendextent, recvextent;
    MPI_Type_get_extent(sendtype, &lower_bound, &sendextent);
    if (inplace) {
      recvextent = sendextent;
    } else {
      MPI_Type_get_extent(recvtype, &lower_bound, &recvextent);
    }
    assert(sendextent*sendcount == recvextent*recvcount);
    if (!inplace) {
      memcpy(recvbuf,
             sendbuf + rank*sendextent*sendcount,
             sendextent*sendcount);
    } // NOTE: if inplace, MPI guarantees that the root data is already correct
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Send(sendbuf + i*sendcount*sendextent, sendcount, sendtype,
                 i, 0, comm);
      }
    }
  } else { // else: rank != root
    for (i = 0; i < size; i++) {
      if (i != root) {
       MPI_Recv(recvbuf, recvcount, recvtype, root, 0, comm, MPI_STATUS_IGNORE);
      }
    }
  }
  return MPI_SUCCESS;
}

int MPI_Scatterv(const void* sendbuf, const int sendcounts[],
                 const int displs[], MPI_Datatype sendtype,
                 void* recvbuf, int recvcount, MPI_Datatype recvtype,
                 int root, MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  int i;
  int inplace = (sendbuf == MPI_IN_PLACE || FORTRAN_MPI_IN_PLACE);
  if (rank == root) {
    MPI_Aint lower_bound;
    MPI_Aint sendextent;
    MPI_Type_get_extent(sendtype, &lower_bound, &sendextent);
    if (!inplace) {
      memcpy(recvbuf, sendbuf + displs[root]*sendextent,
             sendextent*sendcounts[root]);
    } // NOTE: if inplace, MPI guarantees that the root data is already correct
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Send(sendbuf + displs[i]*sendextent /* displs[i] == i*sendcount */,
                 sendcounts[i], sendtype,
                 i, 0, comm);
      }
    }
  } else { // else: rank != root
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Recv(recvbuf, recvcount, recvtype, i, 0, comm, MPI_STATUS_IGNORE);
      }
    }
  }
  return MPI_SUCCESS;
}

// MPI standard 3.1:  Section 5.7
//   Implementations based on 'man MPI_Allgather' for Open MPI
int MPI_Allgather(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
                  void* recvbuf, int recvcount, MPI_Datatype recvtype,
                  MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  int root;
  for (root = 0; root < size; root++) {
    MPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype,
               root, comm);
  }
  return MPI_SUCCESS;
}

int MPI_Allgatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                   void *recvbuf, const int recvcounts[], const int displs[],
                   MPI_Datatype recvtype, MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  int root;
  for (root = 0; root < size; root++) {
    MPI_Gatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts,
                displs, recvtype, root, comm);
  }
  return MPI_SUCCESS;
}

// MPI standard 3.1:  Section 5.8
int MPI_Alltoall(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
                 void* recvbuf, int recvcount, MPI_Datatype recvtype,
                 MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  int i;
  int inplace = (sendbuf == MPI_IN_PLACE || FORTRAN_MPI_IN_PLACE);
  if (inplace) { // if true, MPI says to ignore recvcount/recvtype
    recvcount = sendcount;
    recvtype = sendtype;
  }
  MPI_Aint lower_bound;
  MPI_Aint sendextent, recvextent;
  MPI_Type_get_extent(sendtype, &lower_bound, &sendextent);
  if (inplace) {
    recvextent = sendextent;
  } else {
    MPI_Type_get_extent(recvtype, &lower_bound, &recvextent);
  }
  assert(sendextent*sendcount == recvextent*recvcount);
  // Phase 1: Send to higher ranks, recv from lower ranks to avoid deadlock
  for (i = 0; i < size; i++) {
    if (rank == i) {
      if (!inplace) {
        // NOTE: if inplace, MPI guarantees that rank's data is already correct
        memcpy(recvbuf + i*recvextent*recvcount,
               sendbuf + i*sendextent*sendcount,
               sendextent*sendcount);
      }
    } else if (rank < i) {
      MPI_Send(sendbuf + i*sendcount*sendextent, sendcount, sendtype,
               i, 0, comm);
    } else { // rank > i
      MPI_Recv(recvbuf + i*recvextent*recvcount, recvcount, recvtype,
               i, 0, comm, MPI_STATUS_IGNORE);
    }
  }
  // Phase 2: Send to lower ranks, recv from higher ranks to avoid deadlock
  for (i = 0; i < size; i++) {
    // NOTE: if i == rank, we handled it during Phase 1
    if (rank > i) {
      MPI_Send(sendbuf + i*sendcount*sendextent, sendcount, sendtype,
               i, 0, comm);
    } else if (rank < i) {
      MPI_Recv(recvbuf + i*recvextent*recvcount, recvcount, recvtype,
               i, 0, comm, MPI_STATUS_IGNORE);
    }
  }
  return MPI_SUCCESS;
}

int MPI_Alltoallv(const void* sendbuf, const int sendcounts[],
                  const int sdispls[], MPI_Datatype sendtype,
                  void* recvbuf, const int recvcounts[],
                  const int rdispls[], MPI_Datatype recvtype,
                  MPI_Comm comm) {

  PROLOG_Comm_rank_size;
  int i;
  int inplace = (sendbuf == MPI_IN_PLACE || FORTRAN_MPI_IN_PLACE);
  if (inplace) { // if true, MPI says to ignore recvcount/recvtype
    recvcounts = sendcounts;
    recvtype = sendtype;
    // rdisps will not be used.
  }
  MPI_Aint lower_bound;
  MPI_Aint sendextent, recvextent;
  MPI_Type_get_extent(sendtype, &lower_bound, &sendextent);
  if (inplace) {
    recvextent = sendextent;
  } else {
    MPI_Type_get_extent(recvtype, &lower_bound, &recvextent);
  }
  // FIXME:  Add assert: Sum of sendcounts[]*extent(sendtype) == SAME_FOR_RECV
  // assert(sendextent*sendcount == recvextent*recvcount);
  const int *displs = (inplace ? sdispls : rdispls);
  // Phase 1: Send to higher ranks, recv from lower ranks to avoid deadlock
  for (i = 0; i < size; i++) {
    if (rank == i) {
      if (!inplace) {
        // NOTE: if inplace, MPI guarantees that rank's data is already correct
        memcpy(recvbuf + rdispls[i]*recvextent /* i*recvextent*recvcount */,
               sendbuf + sdispls[i]*sendextent /* i*sendextent*sendcount */,
               sendextent*sendcounts[i]);
      }
    } else if (rank < i) {
      MPI_Send(sendbuf + sdispls[i]*sendextent /* sdispls[i] == i*sendcount */,
               sendcounts[i], sendtype,
               i, 0, comm);
    } else { // rank > i
      MPI_Recv(recvbuf + displs[i]*recvextent /* rdispls[i] == i*recvcount */,
               recvcounts[i], recvtype,
               i, 0, comm, MPI_STATUS_IGNORE);
    }
  }
  // Phase 2: Send to lower ranks, recv from higher ranks to avoid deadlock
  for (i = 0; i < size; i++) {
    // NOTE: if i == rank, we handled it during Phase 1
    if (rank > i) {
      MPI_Send(sendbuf + sdispls[i]*sendextent /* sdispls[i] == i*sendcount */,
               sendcounts[i], sendtype,
               i, 0, comm);
    } else if (rank < i) {
      MPI_Recv(recvbuf + displs[i]*recvextent /* rdispls[i] == i*recvcount */,
               recvcounts[i], recvtype,
               i, 0, comm, MPI_STATUS_IGNORE);
    }
  }
  return MPI_SUCCESS;
}

#ifdef ADD_UNDEFINED
int MPI_Alltoallw(const void* sendbuf, const int sendcounts[],
                  const int sdispls[], const MPI_Datatype sendtypes[],
                  void* recvbuf, const int recvcounts[], const int rdispls[],
                  const MPI_Datatype recvtypes[], MPI_Comm comm) {
  fprintf(stderr, "%s not implemented\n", __FUNCTION__);
  ABORT();
  return -1;
}
#endif

// MPI standard 3.1:  Section 5.9
/* NOTE:  MPI-3.1 standard (Section 5.9.1):
 *   It is strongly recommended that MPI_REDUCE be implemented so that the
 *   same result be obtained whenever the function is applied on the same
 *   arguments, appearing in the same order.
 */
// Predefined Reduction Operatoins (Section 5.9.2)
/*
 * MPI_MAX
 * MPI_MIN
 * MPI_SUM
 * MPI_PROD
 * MPI_LAND
 * MPI_BAND
 * MPI_LOR
 * MPI_BOR
 * MPI_LXOR
 * MPI_BXOR
 * MPI_MAXLOC
 * MPI_MINLOC
 */
int MPI_Reduce(const void* sendbuf, void* recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
  PROLOG_Comm_rank_size;
  MPI_Aint lower_bound;
  MPI_Aint extent;
  MPI_Type_get_extent(datatype, &lower_bound, &extent);
  int inplace = (sendbuf == MPI_IN_PLACE || FORTRAN_MPI_IN_PLACE);
  if (inplace && rank == root) {
    sendbuf = recvbuf;
  }

  if (rank == root) {
    // Gather data into tmp_sendbuf at root
    int i;
    char *tmp_sendbuf = (char *)malloc(count * extent * size);
    MPI_Gather(sendbuf, count, datatype, tmp_sendbuf, count, datatype,
               root, comm);
    // Initialize tmp_recvbuf from sendbuf at rank 0
    char *tmp_recvbuf = (char *)malloc(count * extent);
    memcpy(tmp_recvbuf, tmp_sendbuf, count * extent);
    // Everything is local now; locally reduce tmp_recvbuf, copy to recvbuf
    for (i = 1; i < size; i++) { // skip i=0; already initialized
      MPI_Reduce_local(tmp_sendbuf + i*extent*count /* inbuf */,
                       tmp_recvbuf /* inoutbuf */, count, datatype, op);
    }
    memcpy(recvbuf, tmp_recvbuf, count * extent);
    free(tmp_sendbuf);
    free(tmp_recvbuf);
  } else { // else: rank != root
    // Gather data into tmp_sendbuf at root
    MPI_Gather(sendbuf, count, datatype, NULL, 0, datatype,
               root, comm);
  }
  return MPI_SUCCESS;
}

int MPI_Allreduce(const void* sendbuf, void* recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
  MPI_Reduce(sendbuf, recvbuf, count, datatype, op, 0 /* root */, comm);
  MPI_Bcast(recvbuf, count, datatype, 0 /* root */, comm);
  return MPI_SUCCESS;
}

#ifdef ADD_UNDEFINED
// MPI standard 3.1:  Section 5.10
int MPI_Reduce_scatter_block(const void* sendbuf, void* recvbuf,
                             int recvcount, MPI_Datatype datatype, MPI_Op op,
                             MPI_Comm comm) {
  fprintf(stderr, "%s not implemented\n", __FUNCTION__);
  ABORT();
  return -1;
}
#endif

int MPI_Reduce_scatter(const void* sendbuf, void* recvbuf,
                       const int recvcounts[], MPI_Datatype datatype, MPI_Op op,
                       MPI_Comm comm) {
  fprintf(stderr, "%s not implemented\n", __FUNCTION__);
  ABORT();
  return -1;
}

// MPI standard 3.1:  Section 5.11
int MPI_Scan(const void* sendbuf, void* recvbuf, int count,
             MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
  PROLOG_Comm_rank_size;

  // Get MPI datatype size to allocate buffers
  int root = 0;
  MPI_Aint lower_bound;
  MPI_Aint extent;
  MPI_Type_get_extent(datatype, &lower_bound, &extent);
  int retval;

  // Deal with buffers in place
  int inplace = (sendbuf == MPI_IN_PLACE || FORTRAN_MPI_IN_PLACE);
  if (inplace) {
    sendbuf = recvbuf;
  }

  // Root (rank 0) performs reduction
  if (rank == root) {
    // Allocate buffer to gather each rank's data, and gather data
    char *tmp_buf = (char*) malloc(count * extent * size);
    retval = MPI_Gather(sendbuf, count, datatype, tmp_buf, count, datatype, 0,
                        comm);
    if (retval != MPI_SUCCESS) return retval;

    // Matrix of data must be transposed for reduce to be successful
    // This does not relate to the datatype being processed
    // This is simply a result of the alignment required for the Reduce method
    char *temp=(char*) malloc(extent);
    for (int i = 0; i < count; i++) {
      for (int j = i; j < size; j++) {
        memcpy(temp, tmp_buf+(extent*i+extent*count*j), extent);
        memcpy(tmp_buf+(extent*i+extent*count*j),
               tmp_buf+(extent*i*count+extent*j), extent);
        memcpy(tmp_buf+(extent*i*count+extent*j), temp, extent);
      }
    }

    memcpy(recvbuf, sendbuf, count*extent); // Setup root output
    for (int i = 1; i < size; i++) {        // Perform reductions
      MPI_Reduce_local(tmp_buf+count*extent*(i-1),
                                    tmp_buf+count*extent*i, count,
                                    datatype, op);
      retval = MPI_Send(tmp_buf+count*extent*i, count, datatype, i, 0, comm);
    }
    free(temp);
    free(tmp_buf);

    if(retval != MPI_SUCCESS) return retval;
  }
  else{
    // Send data to root
    retval = MPI_Gather(sendbuf, count, datatype, NULL, 0, datatype, 0, comm);
    if (retval != MPI_SUCCESS) return retval;
    // Receive reduced output from root
    retval = MPI_Recv(recvbuf, count, datatype, 0, 0, comm, MPI_STATUS_IGNORE);
    if (retval != MPI_SUCCESS) return retval;
  }
  return MPI_SUCCESS;
}
#ifdef ADD_UNDEFINED
int MPI_Exscan(const void* sendbuf, void* recvbuf, int count,
             MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
  fprintf(stderr, "%s not implemented\n", __FUNCTION__);
  ABORT();
  return -1;
}
#endif

/********************************************************************
 * Non-blocking variants of MPI calls
 * Here, we immediately call the blocking variant.  There is a danger
 * of causing deadlock by doing this.  We can incrementally replace
 * these based on the patterns in the blocking calls, as needed.
 ********************************************************************/
// MPI standard 3.1:  Section 5.12

int MPI_Ibarrier(MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Barrier(comm);
}

// FIXME:  This works for most applications, but there is a theoretical danger
//         of deadlock.  We could implement using MPI_{Isend,Irecv} to fix that.
int MPI_Ibcast(void* buffer, int count, MPI_Datatype datatype,
               int root, MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Bcast(buffer, count, datatype, root, comm);
}

#ifdef ADD_UNDEFINED
int MPI_Igather(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
                void* recvbuf, int recvcount, MPI_Datatype recvtype,
                int root, MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Gather(sendbuf, sendcount, sendtype,
                    recvbuf, recvcount, recvtype, root, comm);
}

int MPI_Iscatter(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
                 void* recvbuf, int recvcount, MPI_Datatype recvtype,
                 int root, MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Scatter(sendbuf, sendcount, sendtype,
                     recvbuf, recvcount, recvtype, root, comm);
}

int MPI_Iallgather(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
                   void* recvbuf, int recvcount, MPI_Datatype recvtype,
                   MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount,
                       recvtype, comm);
}

int MPI_Iallgatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                     void *recvbuf, const int recvcounts[], const int displs[],
                     MPI_Datatype recvtype, MPI_Comm comm,
                     MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Allgatherv(sendbuf, sendcount, sendtype,
                        recvbuf, recvcounts, displs, recvtype, comm);
}

int MPI_Ialltoall(const void* sendbuf, int sendcount, MPI_Datatype sendtype,
          void* recvbuf, int recvcount, MPI_Datatype recvtype,
          MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Alltoall(sendbuf, sendcount, sendtype,
                      recvbuf, recvcount, recvtype, comm);
}

int MPI_Ialltoallv(const void* sendbuf, const int sendcounts[],
                   const int sdispls[], MPI_Datatype sendtype,
                   void* recvbuf, const int recvcounts[],
                   const int rdispls[], MPI_Datatype recvtype,
                   MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Alltoallv(sendbuf, sendcounts, sdispls, sendtype,
                   recvbuf, recvcounts, rdispls, recvtype, comm);
}

int MPI_Ialltoallw(const void* sendbuf, const int sendcounts[],
                   const int sdispls[], const MPI_Datatype sendtypes[],
                   void* recvbuf, const int recvcounts[], const int rdispls[],
                   const MPI_Datatype recvtypes[], MPI_Comm comm,
                   MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Alltoallw(sendbuf, sendcounts, sdispls, sendtypes,
                        recvbuf, recvcounts, rdispls,
                        recvtypes, comm);
}
#endif

// FIXME:  This works for most applications, but there is a theoretical danger
//         of deadlock.  We could implement using MPI_{Isend,Irecv} to fix that.
int MPI_Ireduce(const void* sendbuf, void* recvbuf, int count,
                MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm,
                MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, comm);
}

#ifdef ADD_UNDEFINED
int MPI_Iallreduce(const void* sendbuf, void* recvbuf, int count,
                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
}

int MPI_Ireduce_scatter_block(const void* sendbuf, void* recvbuf,
                              int recvcount, MPI_Datatype datatype, MPI_Op op,
                              MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Reduce_scatter_block(sendbuf, recvbuf, recvcount, datatype,
                                   op, comm);
}

int MPI_Ireduce_scatter(const void* sendbuf, void* recvbuf,
                        const int recvcounts[], MPI_Datatype datatype,
                        MPI_Op op, MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Reduce_scatter(sendbuf, recvbuf, recvcounts, datatype,
                            op, comm);
}

int MPI_Iscan(const void* sendbuf, void* recvbuf, int count,
              MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
              MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Scan(sendbuf, recvbuf, count, datatype, op, comm);
}

int MPI_Iexscan(const void* sendbuf, void* recvbuf, int count,
                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm,
                MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Exscan(sendbuf, recvbuf, count, datatype, op, comm);
}
#endif  // #ifdef ADD_UNDEFINED

#ifdef __cplusplus
} // end of: extern "C"
#endif
