#include "mpi_plugin.h"
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "mpi_nextfunc.h"
#include "record-replay.h"
#include "virtual-ids.h"

using namespace dmtcp_mpi;

USER_DEFINED_WRAPPER(int, Type_size, (MPI_Datatype) datatype, (int *) size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Type_size)(realType, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Type_free, (MPI_Datatype *) type)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(*type);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Type_free)(&realType);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    // NOTE: We cannot remove the old type, since we'll need
    // to replay this call to reconstruct any new type that might
    // have been created using this type.
    //
    // realType = REMOVE_OLD_TYPE(*type);
    LOG_CALL(restoreTypes, Type_free, type);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Type_commit, (MPI_Datatype *) type)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(*type);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Type_commit)(&realType);
  RETURN_TO_UPPER_HALF();
  if (retval != MPI_SUCCESS) {
    realType = REMOVE_OLD_TYPE(*type);
  } else {
    if (LOGGING()) {
      LOG_CALL(restoreTypes, Type_commit, type);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Type_contiguous, (int) count, (MPI_Datatype) oldtype,
                     (MPI_Datatype *) newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(oldtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Type_contiguous)(count, realType, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Datatype virtType = ADD_NEW_TYPE(*newtype);
    *newtype = virtType;
    LOG_CALL(restoreTypes, Type_contiguous, &count, &oldtype, &virtType);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Type_vector, (int) count, (int) blocklength,
                    (int) stride, (MPI_Datatype) oldtype,
                    (MPI_Datatype*) newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(oldtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Type_vector)(count, blocklength,
                                  stride, realType, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Datatype virtType = ADD_NEW_TYPE(*newtype);
    *newtype = virtType;
    LOG_CALL(restoreTypes, Type_vector, &count, &blocklength,
             &stride, &oldtype, &virtType);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Type_indexed, (int) count,
                     (const int*) array_of_blocklengths,
                     (const int*) array_of_displacements,
                     (MPI_Datatype) oldtype, (MPI_Datatype*) newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(oldtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Type_indexed)(count, array_of_blocklengths,
                                   array_of_displacements,
                                   realType, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Datatype virtType = ADD_NEW_TYPE(*newtype);
    *newtype = virtType;
    FncArg bs = CREATE_LOG_BUF(array_of_blocklengths, count * sizeof(int));
    FncArg ds = CREATE_LOG_BUF(array_of_displacements, count * sizeof(int));
    LOG_CALL(restoreTypes, Type_indexed, &count, &bs, &ds, &oldtype, &virtType);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Pack_size, (int) incount,
                     (MPI_Datatype) datatype, (MPI_Comm) comm,
                     (int*) size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Pack_size)(incount, realType, realComm, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Pack, (const void*) inbuf, (int) incount,
                     (MPI_Datatype) datatype, (void*) outbuf, (int) outsize,
                     (int*) position, (MPI_Comm) comm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Pack)(inbuf, incount, realType, outbuf,
                           outsize, position, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

PMPI_IMPL(int, MPI_Type_size, MPI_Datatype datatype, int *size)
PMPI_IMPL(int, MPI_Type_commit, MPI_Datatype *type)
PMPI_IMPL(int, MPI_Type_contiguous, int count, MPI_Datatype oldtype,
          MPI_Datatype *newtype)
PMPI_IMPL(int, MPI_Type_free, MPI_Datatype *type)
PMPI_IMPL(int, MPI_Type_vector, int count, int blocklength,
          int stride, MPI_Datatype oldtype, MPI_Datatype *newtype)
PMPI_IMPL(int, MPI_Type_indexed, int count, const int array_of_blocklengths[],
          const int array_of_displacements[], MPI_Datatype oldtype,
          MPI_Datatype *newtype)
PMPI_IMPL(int, MPI_Pack_size, int incount, MPI_Datatype datatype,
          MPI_Comm comm, int *size)
PMPI_IMPL(int, MPI_Pack, const void *inbuf, int incount, MPI_Datatype datatype,
          void *outbuf, int outsize, int *position, MPI_Comm comm)
