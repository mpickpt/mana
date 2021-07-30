#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_plugin.h"
#include "record-replay.h"
#include "mpi_nextfunc.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "global_comm_id.h"
#include "restore_comm_group.h"

using namespace dmtcp_mpi;

#define commit_begin(comm) \
  dmtcp_mpi::TwoPhaseAlgo::instance().commit_begin(comm);
#define commit_finish() \
  dmtcp_mpi::TwoPhaseAlgo::instance().commit_finish();

USER_DEFINED_WRAPPER(int, Bcast,
                     (void *) buffer, (int) count, (MPI_Datatype) datatype,
                     (int) root, (MPI_Comm) comm)
{
  commit_begin(comm);
  int retval;
  int size;
  MPI_Type_size(datatype, &size);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Bcast)(buffer, count, realType, root, realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Ibcast,
                     (void *) buffer, (int) count, (MPI_Datatype) datatype,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Ibcast)(buffer, count, realType,
      root, realComm, request);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Request virtRequest = ADD_NEW_REQUEST(*request);
    *request = virtRequest;
    LOG_CALL(restoreRequests, Ibcast, buffer, count, datatype,
             root, comm, *request);
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IBARRIER_REQUEST);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Barrier, (MPI_Comm) comm)
{
  commit_begin(comm);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Barrier)(realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

EXTERNC
USER_DEFINED_WRAPPER(int, Ibarrier, (MPI_Comm) comm, (MPI_Request *) request)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Ibarrier)(realComm, request);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  if (retval == MPI_SUCCESS && LOGGING()) {
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
  commit_begin(comm);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  get_fortran_constants();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  if (sendbuf == FORTRAN_MPI_IN_PLACE) {
    retval = NEXT_FUNC(Allreduce)(MPI_IN_PLACE, recvbuf, count,
                                  realType, realOp, realComm);
  } else {
    retval = NEXT_FUNC(Allreduce)(sendbuf, recvbuf, count,
                                  realType, realOp, realComm);
  }
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Reduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm)
{
  commit_begin(comm);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Reduce)(sendbuf, recvbuf, count,
                             realType, realOp, root, realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Ireduce,
                     (const void *) sendbuf, (void *) recvbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op,
                     (int) root, (MPI_Comm) comm, (MPI_Request *) request)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Ireduce)(sendbuf, recvbuf, count,
      realType, realOp, root, realComm, request);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Request virtRequest = ADD_NEW_REQUEST(*request);
    *request = virtRequest;
    LOG_CALL(restoreRequests, Ireduce, sendbuf, recvbuf,
        count, datatype, op, root, comm, *request);
#ifdef USE_REQUEST_LOG
    logRequestInfo(*request, IBARRIER_REQUEST);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

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
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Alltoall)(sendbuf, sendcount, realSendType, recvbuf,
      recvcount, realRecvType, realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Alltoall,
                     (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  commit_begin(comm);
  return MPI_Alltoall_internal(sendbuf, sendcount, sendtype,
                               recvbuf, recvcount, recvtype, comm);
  commit_finish();
}

USER_DEFINED_WRAPPER(int, Alltoallv,
                     (const void *) sendbuf, (const int *) sendcounts,
                     (const int *) sdispls, (MPI_Datatype) sendtype,
                     (void *) recvbuf, (const int *) recvcounts,
                     (const int *) rdispls, (MPI_Datatype) recvtype,
                     (MPI_Comm) comm)
{
  commit_begin(comm);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Alltoallv)(sendbuf, sendcounts, sdispls, realSendType,
                                recvbuf, recvcounts, rdispls, realRecvType,
                                realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Gather, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  int retval;
  commit_begin(comm);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Gather)(sendbuf, sendcount, realSendType,
                             recvbuf, recvcount, realRecvType,
                             root, realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Gatherv, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf,
                     (const int*) recvcounts, (const int*) displs,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  commit_begin(comm);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Gatherv)(sendbuf, sendcount, realSendType,
                              recvbuf, recvcounts, displs, realRecvType,
                              root, realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Scatter, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  int retval;
  commit_begin(comm);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Scatter)(sendbuf, sendcount, realSendType,
      recvbuf, recvcount, realRecvType,
      root, realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Scatterv, (const void *) sendbuf,
                     (const int *) sendcounts, (const int *) displs,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (int) root, (MPI_Comm) comm)
{
  commit_begin(comm);
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Scatterv)(sendbuf, sendcounts, displs, realSendType,
      recvbuf, recvcount, realRecvType,
      root, realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Allgather, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf, (int) recvcount,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  int retval;
  commit_begin(comm);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Allgather)(sendbuf, sendcount, realSendType,
                                recvbuf, recvcount, realRecvType,
                                realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Allgatherv, (const void *) sendbuf, (int) sendcount,
                     (MPI_Datatype) sendtype, (void *) recvbuf,
                     (const int*) recvcounts, (const int *) displs,
                     (MPI_Datatype) recvtype, (MPI_Comm) comm)
{
  int retval;
  commit_begin(comm);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Allgatherv)(sendbuf, sendcount, realSendType,
                                 recvbuf, recvcounts, displs, realRecvType,
                                 realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Scan, (const void *) sendbuf, (void *) recvbuf,
                     (int) count, (MPI_Datatype) datatype,
                     (MPI_Op) op, (MPI_Comm) comm)
{
  int retval;
  commit_begin(comm);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Op realOp = VIRTUAL_TO_REAL_TYPE(op);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Scan)(sendbuf, recvbuf, count,
                           realType, realOp, realComm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

// FIXME: Also check the MPI_Cart family, if they use collective communications.
USER_DEFINED_WRAPPER(int, Comm_split, (MPI_Comm) comm, (int) color, (int) key,
    (MPI_Comm *) newcomm)
{
  int retval;
  commit_begin(comm);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Comm_split)(realComm, color, key, newcomm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    *newcomm = virtComm;
    active_comms.insert(virtComm);
    // LOG_CALL(restoreComms, Comm_split, comm, color, key, *newcomm);
    VirtualGlobalCommId::instance().createGlobalId(virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_dup, (MPI_Comm) comm, (MPI_Comm *) newcomm)
{
  int retval;
  commit_begin(comm);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
#ifdef SET_FS_CONTEXT
  SET_LOWER_HALF_FS_CONTEXT();
#else
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
#endif
  retval = NEXT_FUNC(Comm_dup)(realComm, newcomm);
#ifdef SET_FS_CONTEXT
  RESTORE_UPPER_HALF_FS_CONTEXT();
#else
  RETURN_TO_UPPER_HALF();
#endif
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    *newcomm = virtComm;
    active_comms.insert(virtComm);
    // LOG_CALL(restoreComms, Comm_dup, comm, *newcomm);
    VirtualGlobalCommId::instance().createGlobalId(virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  commit_finish();
  return retval;
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
PMPI_IMPL(int, MPI_Comm_split, MPI_Comm comm, int color, int key,
          MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Comm_dup, MPI_Comm comm, MPI_Comm *newcomm)
