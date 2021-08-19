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
    LOG_CALL(restoreTypes, Type_free, *type);
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
      LOG_CALL(restoreTypes, Type_commit, *type);
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
    LOG_CALL(restoreTypes, Type_contiguous, count, oldtype, virtType);
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
    LOG_CALL(restoreTypes, Type_vector, count, blocklength,
             stride, oldtype, virtType);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

//       int MPI_Type_create_struct(int count,
//                                const int array_of_blocklengths[],
//                                const MPI_Aint array_of_displacements[],
//                                const MPI_Datatype array_of_types[],
//                                MPI_Datatype *newtype)

USER_DEFINED_WRAPPER(int, Type_create_struct, (int) count,
                     (const int*) array_of_blocklengths,
                     (const MPI_Aint*) array_of_displacements,
                     (const MPI_Datatype*) array_of_types, (MPI_Datatype*) newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realTypes[count];
  for (int i = 0; i < count; i++) {
    realTypes[i] = VIRTUAL_TO_REAL_TYPE(array_of_types[i]);
  }
  //MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(oldtype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Type_create_struct)(count, array_of_blocklengths,
                                   array_of_displacements,
                                   realTypes, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Datatype virtType = ADD_NEW_TYPE(*newtype);
    *newtype = virtType;
    FncArg bs = CREATE_LOG_BUF(array_of_blocklengths, count * sizeof(int));
    FncArg ds = CREATE_LOG_BUF(array_of_displacements, count * sizeof(MPI_Aint));
    FncArg ts = CREATE_LOG_BUF(array_of_types, count * sizeof(MPI_Datatype));
    LOG_CALL(restoreTypes, Type_create_struct, count, bs, ds, ts, virtType);
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
    LOG_CALL(restoreTypes, Type_indexed, count, bs, ds, oldtype, virtType);
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
PMPI_IMPL(int, MPI_Type_create_struct, int count, const int array_of_blocklengths[],
          const MPI_Aint array_of_displacements[], const MPI_Datatype array_of_types[],
          MPI_Datatype *newtype)
PMPI_IMPL(int, MPI_Type_indexed, int count, const int array_of_blocklengths[],
          const int array_of_displacements[], MPI_Datatype oldtype,
          MPI_Datatype *newtype)
PMPI_IMPL(int, MPI_Pack_size, int incount, MPI_Datatype datatype,
          MPI_Comm comm, int *size)
PMPI_IMPL(int, MPI_Pack, const void *inbuf, int incount, MPI_Datatype datatype,
          void *outbuf, int outsize, int *position, MPI_Comm comm)
