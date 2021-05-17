#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_plugin.h"
#include "drain_send_recv_packets.h"
#include "mpi_nextfunc.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"

USER_DEFINED_WRAPPER(int, Bcast,
                     (void *) buffer, (int) count, (MPI_Datatype) datatype,
                     (int) root, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Bcast)(buffer, count, realType, root, realComm);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Ibcast,
                     (void *) buffer, (int) count, (MPI_Datatype) datatype,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Ibcast)(buffer, count, realType,
                               root, realComm, request);
    RETURN_TO_UPPER_HALF();
    addPendingIbcastToLog(IBCAST_REQUEST, buffer, count, datatype,
                           root, comm, request);
    DMTCP_PLUGIN_ENABLE_CKPT();
    // FIXME: Examine this logic
    /* int flag = 0; */
    /* MPI_Status st; */
    /* if (MPI_Request_get_status(*request, &flag, &st) == MPI_SUCCESS && flag) { */
    /*   clearPendingRequestFromLog(request, *request); */
    /* } */
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

#if 1
USER_DEFINED_WRAPPER(int, Barrier, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Barrier)(realComm);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}
#else
USER_DEFINED_WRAPPER(int, Barrier, (MPI_Comm) comm)
{
  JTRACE("Invoking 2PC for MPI_Barrier");
  dmtcp_mpi::TwoPhaseAlgo::instance().commit_begin(comm);

  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Barrier)(realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  dmtcp_mpi::TwoPhaseAlgo::instance().commit_finish();
  return retval;
}
#endif

// FIXME: We need to create a test case for this
USER_DEFINED_WRAPPER(int, Ibarrier, (MPI_Comm) comm, (MPI_Request *) request)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Ibarrier)(realComm, request);
    RETURN_TO_UPPER_HALF();
    addPendingIbarrierToLog(IBARRIER_REQUEST, comm, request);
    DMTCP_PLUGIN_ENABLE_CKPT();
    /* int flag = 0; */
    /* MPI_Status st; */
    /* if (MPI_Request_get_status(*request, &flag, &st) == MPI_SUCCESS && flag) { */
    /*   clearPendingRequestFromLog(request, *request); */
    /* } */
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Allreduce,
                     (const void *) sendbuf, (void *) recvbuf,
                     (int) count, (MPI_Datatype) datatype,
                     (MPI_Op) op, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
    MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Allreduce)(sendbuf, recvbuf, count,
                                  realType, realOp, realComm);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Reduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Ireduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
    MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Ireduce)(sendbuf, recvbuf, count,
                               realType, realOp, root, realComm, request);
    RETURN_TO_UPPER_HALF();
    addPendingIreduceToLog(IREDUCE_REQUEST, sendbuf, recvbuf, count, 
                           datatype, op, root, comm, request);
    DMTCP_PLUGIN_ENABLE_CKPT();
    /* int flag = 0; */
    /* MPI_Status st; */
    /* if (MPI_Request_get_status(*request, &flag, &st) == MPI_SUCCESS && flag) { */
    /*   clearPendingRequestFromLog(request, *request); */
    /* } */
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Alltoall,
                     (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Alltoallv,
                     (const void *) sendbuf, (const int *) sendcounts,
                     (const int *) sdispls, (MPI_Datatype) sendtype,
                     (void *) recvbuf, (const int *) recvcounts,
                     (const int *) rdispls, (MPI_Datatype) recvtype,
                     (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Gather, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Gatherv, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf,
                     (const int*) recvcounts, (const int*) displs,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Scatter, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Scatterv, (const void *) sendbuf,
                     (const int *) sendcounts, (const int *) displs,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Allgather, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Allgatherv, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf,
                     (const int*) recvcounts, (const int *) displs,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Scan, (const void *) sendbuf, (void *) recvbuf,
                     (int) count, (MPI_Datatype) datatype,
                     (MPI_Op) op, (MPI_Comm) comm)
{
  std::function<int()> realBarrierCb = [=]() {
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
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

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
