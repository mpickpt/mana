#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mpi.h>

// NOTE:  Do 'google MPI standard 3.1' to see the spec in the standard.
//        This first version does not consider intracommunicators.
//        This version does not test for return values.

#define PROLOG_rank_size \
  int rank; \
  int size; \
  MPI_Comm_rank(comm, &rank); \
  MPI_Comm_size(comm, &size); \
  if (rank <= 1) { \
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
  PROLOG_rank_size;
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


extern "C" {

// MPI standard 3.1:  Section 5.3
int MPI_Barrier(MPI_Comm comm) {
  PROLOG_rank_size;
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
  PROLOG_rank_size;
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
  PROLOG_rank_size;
  int i;
  int inplace = (sendbuf == MPI_IN_PLACE);
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
    assert(sendextent*sendcount == recvextent*recvcount);
    if (!inplace) {
      memcpy(recvbuf + rank*recvextent, sendbuf, sendextent*sendcount);
    } // NOTE: if inplace, MPI guarantees that the root data is already correct
    for (i = 0; i < size; i++) {
      if (i != rank) {
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
  PROLOG_rank_size;
  int i;
  int inplace = (sendbuf == MPI_IN_PLACE);
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
      memcpy(recvbuf + displs[root], sendbuf, sendextent*sendcount);
    } // NOTE: if inplace, MPI guarantees that the root data is already correct
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
  PROLOG_rank_size;
  int i;
  int inplace = (recvbuf == MPI_IN_PLACE);
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
        MPI_Recv(recvbuf, recvcount, recvtype, i, 0, comm, MPI_STATUS_IGNORE);
      }
    }
  }
  return MPI_SUCCESS;
}

int MPI_Scatterv(const void* sendbuf, const int sendcounts[],
                 const int displs[], MPI_Datatype sendtype,
                 void* recvbuf, int recvcount, MPI_Datatype recvtype,
                 int root, MPI_Comm comm) {
  PROLOG_rank_size;
  int i;
  int inplace = (recvbuf == MPI_IN_PLACE);
  if (rank == root) {
    MPI_Aint lower_bound;
    MPI_Aint sendextent;
    if (!inplace) {
      memcpy(recvbuf, sendbuf + displs[i], sendextent*sendcounts[i]);
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
  PROLOG_rank_size;
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
  PROLOG_rank_size;
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
  PROLOG_rank_size;
  int i;
  int inplace = (recvbuf == MPI_IN_PLACE);
  if (inplace) { // if true, MPI says to ignore recvcount/recvtype
    recvcount = sendcount;
    recvtype = sendtype;
  }
  MPI_Aint lower_bound;
  MPI_Aint sendextent, recvextent;
  if (inplace) {
    recvextent = sendextent;
  } else {
    MPI_Type_get_extent(recvtype, &lower_bound, &recvextent);
  }
  assert(sendextent*sendcount == recvextent*recvcount);
  for (i = 0; i < size; i++) {
    if (i == rank) {
      if (!inplace) {
        memcpy(recvbuf + i*recvextent*recvcount,
               sendbuf + i*sendextent*sendcount,
               sendextent*sendcount);
      } // NOTE: if inplace, MPI guarantees that rank's data is already correct
    } else { // else: i != rank
      MPI_Send(sendbuf + i*sendcount*sendextent, sendcount, sendtype,
               i, 0, comm);
    }
  }
  for (i = 0; i < size; i++) {
    // NOTE: if i == rank, we handled it during MPI_Send
    if (i != rank) {
      MPI_Recv(recvbuf + i*recvcount*recvextent, recvcount, recvtype,
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

  PROLOG_rank_size;
  int i;
  int inplace = (recvbuf == MPI_IN_PLACE);
  if (inplace) { // if true, MPI says to ignore recvcount/recvtype
    recvcounts = sendcounts;
    recvtype = sendtype;
    // rdisps will not be used.
  }
  MPI_Aint lower_bound;
  MPI_Aint sendextent, recvextent;
  if (inplace) {
    recvextent = sendextent;
  } else {
    MPI_Type_get_extent(recvtype, &lower_bound, &recvextent);
  }
  // FIXME:  Add assert: Sum of sendcounts[]*extent(sendtype) == SAME_FOR_RECV
  // assert(sendextent*sendcount == recvextent*recvcount);
  for (i = 0; i < size; i++) {
    if (i == rank) {
      if (!inplace) {
        memcpy(recvbuf + rdispls[i] /* i*recvextent*recvcount */,
               sendbuf + sdispls[i] /* i*sendextent*sendcount */,
               sendextent*sendcounts[i]);
      } // NOTE: if inplace, MPI guarantees that rank's data is already correct
    } else { // else: i != rank
      MPI_Send(sendbuf + sdispls[i]*sendextent /* sdispls[i] == i*sendcount */,
               sendcounts[i], sendtype,
               i, 0, comm);
    }
  }
  for (i = 0; i < size; i++) {
    // NOTE: if i == rank, we handled it during MPI_Send
    if (i != rank) {
      const int *displs = (inplace ? sdispls : rdispls);
      MPI_Recv(recvbuf + displs[i]*recvextent /* rdispls[i] == i*recvcount */,
               recvcounts[i], recvtype,
               i, 0, comm, MPI_STATUS_IGNORE);
    }
  }
  return MPI_SUCCESS;
}

int MPI_Alltoallw(const void* sendbuf, const int sendcounts[],
                  const int sdispls[], const MPI_Datatype sendtypes[],
                  void* recvbuf, const int recvcounts[], const int rdispls[],
                  const MPI_Datatype recvtypes[], MPI_Comm comm) {
  fprintf(stderr, "%s not implemented\n", __FUNCTION__);
  ABORT();
  return -1;
}

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
#if 1
// This version depends on the availability of MPI_Reduce_local.
// MANA doesn't yet support MPI_Reduce_local.
int MPI_Reduce(const void* sendbuf, void* recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
  PROLOG_rank_size;
  MPI_Aint lower_bound;
  MPI_Aint extent;
  MPI_Type_get_extent(datatype, &lower_bound, &extent);
  int inplace = (sendbuf == MPI_IN_PLACE);
  if (inplace) {
    sendbuf = recvbuf;
  } else if (rank == root) {
    memcpy(recvbuf, sendbuf, count * extent);
  }
  char *tmp_recvbuf = NULL;
  if (rank == root) {
    int i;
    tmp_recvbuf = (char *)malloc(count * extent);
    MPI_Gather(sendbuf, count, datatype, tmp_recvbuf, count, datatype,
               root, comm);
    for (i = 0; i < size; i++) {
      if (i != root) {
        MPI_Reduce_local(tmp_recvbuf + i*extent*count /* inbuf */,
                         recvbuf /* inoutbuf */, count, datatype, op);
      }
    }
    free(tmp_recvbuf);
  } else { // else: rank != root
    MPI_Gather(sendbuf, count, datatype, NULL, 0, datatype,
               root, comm);
  }
  return MPI_SUCCESS;
}
#else
// FIXME:  This wuld still need to implement MPI_Reduce over two processes
int MPI_Reduce(const void* sendbuf, void* recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
  PROLOG_rank_size;
  MPI_Aint lower_bound;
  MPI_Aint extent;
  MPI_Type_get_extent(datatype, &lower_bound, &extent);
  int inplace = (sendbuf == MPI_IN_PLACE);
  if (inplace && rank == root) {
    memcpy(recvbuf, sendbuf, count * extent);
  }
  // It would have been nice if MPI had a way to use 'op' locally on array.
  int other_root = (root != 0 ? 0 : 1);
  char *tmp_recvbuf = NULL;
  if (rank == other_root) {
    tmp_recvbuf = malloc(count * extent);
    MPI_Gather(sendbuf, count, datatype, tmp_recvbuf, count, datatype,
               other_root, comm);
  } else {
    MPI_Gather(sendbuf, count, datatype, NULL, 0, datatype,
               other_root, comm);
  }
  if (rank == root || rank == other_root) {
    MPI_Group group, tmpgroup;
    MPI_Comm tmpcomm;
    MPI_Comm_group(comm, &group);
    int ranks[] = {root, other_root};
    int i;
    MPI_Group_incl(group, 2, ranks, &tmpgroup);
    MPI_Comm_create(comm, tmpgroup, &tmpcomm);
    for (i = 0; i < size; i++) {
      if (rank == other_root) {
        MPI_Reduce(tmp_recvbuf + i*extent*count, recvbuf, count,
                   datatype, op, 0 /* root for tmpcomm */, tmpcomm)
      } else if (rank == root) {
        MPI_Reduce(tmp_recvbuf + i*extent*count, recvbuf, count,
                   datatype, op, 0 /* root for tmpcomm */, tmpcomm)
      }
    }
    if (rank == other_root) {
      free(tmp_recvbuf);
    }
    MPI_Comm_free(&tmpcomm);
  }
  return MPI_SUCCESS;
}
#endif

int MPI_Allreduce(const void* sendbuf, void* recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
  MPI_Reduce(sendbuf, recvbuf, count, datatype, op, 0 /* root */, comm);
  MPI_Bcast(recvbuf, count, datatype, 0 /* root */, comm);
  return MPI_SUCCESS;
}

// MPI standard 3.1:  Section 5.10
int MPI_Reduce_scatter_block(const void* sendbuf, void* recvbuf,
                             int recvcount, MPI_Datatype datatype, MPI_Op op,
                             MPI_Comm comm) {
  fprintf(stderr, "%s not implemented\n", __FUNCTION__);
  ABORT();
  return -1;
}
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
  fprintf(stderr, "%s not implemented\n", __FUNCTION__);
  ABORT();
  return -1;
}
int MPI_Exscan(const void* sendbuf, void* recvbuf, int count,
             MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
  fprintf(stderr, "%s not implemented\n", __FUNCTION__);
  ABORT();
  return -1;
}

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

int MPI_Ibcast(void* buffer, int count, MPI_Datatype datatype,
               int root, MPI_Comm comm, MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Bcast(buffer, count, datatype, root, comm);
}

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

int MPI_Ireduce(const void* sendbuf, void* recvbuf, int count,
                MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm,
                MPI_Request *request) {
  ACTIVATE_REQUEST(request);
  return MPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, comm);
}

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

} // end of: extern "C"
