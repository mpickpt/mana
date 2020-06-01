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

USER_DEFINED_WRAPPER(int, Comm_size, (MPI_Comm) comm, (int *) world_size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_size)(realComm, world_size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_rank, (MPI_Comm) comm, (int *) world_rank)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_rank)(realComm, world_rank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_create, (MPI_Comm) comm, (MPI_Group) group,
                     (MPI_Comm *) newcomm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_create)(realComm, realGroup, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    *newcomm = virtComm;
    LOG_CALL(restoreComms, Comm_create, &comm, &group, &virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Abort, (MPI_Comm) comm, (int) errorcode)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Abort)(comm, errorcode);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_split, (MPI_Comm) comm, (int) color, (int) key,
                     (MPI_Comm *) newcomm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_split)(realComm, color, key, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    *newcomm = virtComm;
    LOG_CALL(restoreComms, Comm_split, &comm, &color, &key, &virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_dup, (MPI_Comm) comm, (MPI_Comm *) newcomm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_dup)(realComm, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    *newcomm = virtComm;
    LOG_CALL(restoreComms, Comm_dup, &comm, &virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_compare,
                     (MPI_Comm) comm1, (MPI_Comm) comm2, (int*) result)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm1 = VIRTUAL_TO_REAL_COMM(comm1);
  MPI_Comm realComm2 = VIRTUAL_TO_REAL_COMM(comm2);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_compare)(realComm1, realComm2, result);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_free, (MPI_Comm *) comm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(*comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_free)(&realComm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    // NOTE: We cannot remove the old comm from the map, since
    // we'll need to replay this call to reconstruct any other comms that
    // might have been created using this comm.
    //
    // realComm = REMOVE_OLD_COMM(*comm);
    // CLEAR_COMM_LOGS(*comm);
    LOG_CALL(restoreComms, Comm_free, comm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_set_errhandler,
                     (MPI_Comm) comm, (MPI_Errhandler) errhandler)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_set_errhandler)(realComm, errhandler);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    LOG_CALL(restoreComms, Comm_set_errhandler, &comm, &errhandler);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Topo_test,
                     (MPI_Comm) comm, (int *) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Topo_test)(realComm, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_split_type, (MPI_Comm) comm, (int) split_type,
                     (int) key, (MPI_Info) inf, (MPI_Comm*) newcomm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_split_type)(realComm, split_type, key, inf, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    *newcomm = virtComm;
    LOG_CALL(restoreComms, Comm_split_type, &comm,
             &split_type, &key, &inf, &virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Attr_get, (MPI_Comm) comm, (int) keyval,
                     (void*) attribute_val, (int*) flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Attr_get)(realComm, keyval, attribute_val, flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Attr_delete, (MPI_Comm) comm, (int) keyval)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Attr_delete)(realComm, keyval);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    LOG_CALL(restoreComms, Attr_delete, &comm, &keyval);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Attr_put, (MPI_Comm) comm,
                     (int) keyval, (void*) attribute_val)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Attr_put)(realComm, keyval, attribute_val);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    uint64_t val = (uint64_t)attribute_val;
    LOG_CALL(restoreComms, Attr_put, &comm, &keyval, &val);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

PMPI_IMPL(int, MPI_Comm_size, MPI_Comm comm, int *world_size)
PMPI_IMPL(int, MPI_Comm_rank, MPI_Group group, int *world_rank)
PMPI_IMPL(int, MPI_Abort, MPI_Comm comm, int errorcode)
PMPI_IMPL(int, MPI_Comm_split, MPI_Comm comm, int color, int key,
          MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Comm_dup, MPI_Comm comm, MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Comm_create, MPI_Comm comm, MPI_Group group,
          MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Comm_compare, MPI_Comm comm1, MPI_Comm comm2, int *result)
PMPI_IMPL(int, MPI_Comm_free, MPI_Comm *comm)
PMPI_IMPL(int, MPI_Comm_set_errhandler, MPI_Comm comm,
          MPI_Errhandler errhandler)
PMPI_IMPL(int, MPI_Topo_test, MPI_Comm comm, int* status)
PMPI_IMPL(int, MPI_Comm_split_type, MPI_Comm comm, int split_type, int key,
          MPI_Info info, MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Attr_get, MPI_Comm comm, int keyval,
          void *attribute_val, int *flag)
PMPI_IMPL(int, MPI_Attr_delete, MPI_Comm comm, int keyval)
PMPI_IMPL(int, MPI_Attr_put, MPI_Comm comm, int keyval, void *attribute_val)
