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
#include "virtual_id.h"

using namespace dmtcp_mpi;

extern "C" {

#pragma weak MPI_Type_size = PMPI_Type_size
int PMPI_Type_size(MPI_Datatype datatype, int *size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_size)(real_datatype, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_free = PMPI_Type_free
int PMPI_Type_free(MPI_Datatype *type)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = *type}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_free)(&real_datatype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    LOG_CALL(restoreTypes, Type_free, *type);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_commit = PMPI_Type_commit
int PMPI_Type_commit(MPI_Datatype *type)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = *type}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_commit)(&real_datatype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    LOG_CALL(restoreTypes, Type_commit, *type);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_contiguous = PMPI_Type_contiguous
int PMPI_Type_contiguous(int count, MPI_Datatype oldtype, MPI_Datatype *newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = oldtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_contiguous)(count, real_datatype, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    *newtype = new_virt_datatype(*newtype);
    LOG_CALL(restoreTypes, Type_contiguous, count, oldtype, *newtype);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_create_hvector = PMPI_Type_create_hvector
int PMPI_Type_create_hvector(int count, int blocklength, MPI_Aint stride,
                            MPI_Datatype oldtype, MPI_Datatype *newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = oldtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_create_hvector)(count, blocklength,
                                  stride, real_datatype, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    *newtype = new_virt_datatype(*newtype);
    LOG_CALL(restoreTypes, Type_create_hvector, count, blocklength,
             stride, oldtype, *newtype);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_vector = PMPI_Type_vector
int PMPI_Type_vector(int count, int blocklength, int stride, MPI_Datatype oldtype,
                    MPI_Datatype *newtype)
{
  int size;
  int retval = MPI_Type_size(oldtype, &size);
  if(retval != MPI_SUCCESS) {
    return retval;
  }

  return PMPI_Type_create_hvector(count, blocklength, stride*size, oldtype, newtype);
}

//       int PMPI_Type_create_struct(int count,
//                                const int array_of_blocklengths[],
//                                const MPI_Aint array_of_displacements[],
//                                const MPI_Datatype array_of_types[],
//                                MPI_Datat

#pragma weak MPI_Type_create_struct = PMPI_Type_create_struct
int PMPI_Type_create_struct(int count, const int *array_of_blocklengths,
                           const MPI_Aint *array_of_displacements,
                           const MPI_Datatype *array_of_types, MPI_Datatype *newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatypes[count];
  for (int i = 0; i < count; i++) {
    real_datatypes[i] = get_real_id((mana_mpi_handle){.datatype = array_of_types[i]}).datatype;
  }
  //MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = oldtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_create_struct)(count, array_of_blocklengths,
                                   array_of_displacements,
                                   real_datatypes, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    *newtype = new_virt_datatype(*newtype);
    FncArg bs = CREATE_LOG_BUF(array_of_blocklengths, count * sizeof(int));
    FncArg ds = CREATE_LOG_BUF(array_of_displacements, count * sizeof(MPI_Aint));
    FncArg ts = CREATE_LOG_BUF(array_of_types, count * sizeof(MPI_Datatype));
    LOG_CALL(restoreTypes, Type_create_struct, count, bs, ds, ts, *newtype);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

// Perlmutter cray_mpich both implement MPI 3.1. However, they use different
// APIs. We use MPICH_NUMVERSION (3.4a2) to differentiate the cray-mpich on Cori
// and Perlmuttter. This ad-hoc workaround should be removed once the cray-mpich
// on Perlmutter is fixed to use the right API.
#pragma weak MPI_Type_struct = PMPI_Type_struct
#ifdef MPICH_NUMVERSION
#if MPICH_NUMVERSION < MPICH_CALC_VERSION(3,4,0,0,2) && defined(CRAY_MPICH_VERSION)
int PMPI_Type_struct(int count,
                    const int *array_of_blocklengths,
                    const MPI_Aint *array_of_displacements,
                    const MPI_Datatype *array_of_types, MPI_Datatype *newtype)
#else
int PMPI_Type_struct(int count,
                    int *array_of_blocklengths,
                    MPI_Aint *array_of_displacements,
                    MPI_Datatype *array_of_types, MPI_Datatype *newtype)
#endif
#else
int PMPI_Type_struct(int count,
                    int *array_of_blocklengths,
                    MPI_Aint *array_of_displacements,
                    MPI_Datatype *array_of_types, MPI_Datatype *newtype)
#endif
{
  return MPI_Type_create_struct(count, array_of_blocklengths,
                                array_of_displacements, array_of_types, newtype
                                );
}

#pragma weak MPI_Type_create_hindexed = PMPI_Type_create_hindexed
int PMPI_Type_create_hindexed(int count, const int *array_of_blocklengths,
                             const MPI_Aint *array_of_displacements,
                             MPI_Datatype oldtype, MPI_Datatype *newtype)
{
#ifdef MPICH_NUMVERSION
#if MPICH_NUMVERSION < MPICH_CALC_VERSION(3,4,0,0,2) && defined(CRAY_MPICH_VERSION)
  return MPI_Type_create_hindexed(count, array_of_blocklengths, array_of_displacements,
                           oldtype, newtype);
#else
  int *non_const_bl_arr = (int*)malloc(count * sizeof(int));
  MPI_Aint* non_const_disp_arr = (MPI_Aint*) malloc(count * sizeof(MPI_Aint));
  memcpy(non_const_bl_arr, array_of_blocklengths, count * sizeof(int));
  memcpy(non_const_disp_arr, array_of_displacements, count * sizeof(MPI_Aint));
  int ret = MPI_Type_create_hindexed(count, non_const_bl_arr, non_const_disp_arr,
                              oldtype, newtype);
  free(non_const_bl_arr);
  free(non_const_disp_arr);
  return ret;
#endif
#else
  int *non_const_bl_arr = (int*)malloc(count * sizeof(int));
  MPI_Aint* non_const_disp_arr = (MPI_Aint*) malloc(count * sizeof(MPI_Aint));
  memcpy(non_const_bl_arr, array_of_blocklengths, count * sizeof(int));
  memcpy(non_const_disp_arr, array_of_displacements, count * sizeof(MPI_Aint));
  int ret = MPI_Type_create_hindexed(count, non_const_bl_arr, non_const_disp_arr,
                              oldtype, newtype);
  free(non_const_bl_arr);
  free(non_const_disp_arr);
  return ret;
#endif
}

#pragma weak MPI_Type_create_hindexed_block = PMPI_Type_create_hindexed_block
int PMPI_Type_create_hindexed_block(int count, int blocklength,
                                   const MPI_Aint *array_of_displacements,
                                   MPI_Datatype oldtype, MPI_Datatype *newtype)
{
  int array_of_blocklengths[count];
  for (int i = 0; i < count; i++) {
    array_of_blocklengths[i] = blocklength;
  }
#ifdef MPICH_NUMVERSION
#if MPICH_NUMVERSION < MPICH_CALC_VERSION(3,4,0,0,2) && defined(CRAY_MPICH_VERSION)
  return MPI_Type_create_hindexed(count, array_of_blocklengths, array_of_displacements,
                           oldtype, newtype);
#else
  MPI_Aint* non_const_disp_arr = (MPI_Aint*) malloc(count * sizeof(MPI_Aint));
  memcpy(non_const_disp_arr, array_of_displacements, count * sizeof(MPI_Aint));
  int ret =  MPI_Type_create_hindexed(count, array_of_blocklengths, non_const_disp_arr,
                           oldtype, newtype);
  free(non_const_disp_arr);
  return ret;
#endif
#else
  MPI_Aint* non_const_disp_arr = (MPI_Aint*) malloc(count * sizeof(MPI_Aint));
  memcpy(non_const_disp_arr, array_of_displacements, count * sizeof(MPI_Aint));
  int ret =  MPI_Type_create_hindexed(count, array_of_blocklengths, non_const_disp_arr,
                           oldtype, newtype);
  free(non_const_disp_arr);
  return ret;
#endif
}

#pragma weak MPI_Type_hindexed_block = PMPI_Type_hindexed_block
int PMPI_Type_hindexed_block(int count, int blocklength,
                            const MPI_Aint *array_of_displacements,
                            MPI_Datatype oldtype, MPI_Datatype *newtype)
{
  return MPI_Type_create_hindexed_block(count, blocklength,
                                        array_of_displacements, oldtype,
                                        newtype);
}

#pragma weak MPI_Type_indexed = PMPI_Type_indexed
int PMPI_Type_indexed(int count, const int *array_of_blocklengths,
                     const int *array_of_displacements,
                     MPI_Datatype oldtype, MPI_Datatype *newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = oldtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_indexed)(count, array_of_blocklengths,
                                   array_of_displacements,
                                   real_datatype, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    *newtype = new_virt_datatype(*newtype);
    FncArg bs = CREATE_LOG_BUF(array_of_blocklengths, count * sizeof(int));
    FncArg ds = CREATE_LOG_BUF(array_of_displacements, count * sizeof(int));
    LOG_CALL(restoreTypes, Type_indexed, count, bs, ds, oldtype, *newtype);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_dup = PMPI_Type_dup
int PMPI_Type_dup(MPI_Datatype oldtype, MPI_Datatype *newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = oldtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_dup)(real_datatype, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    *newtype = new_virt_datatype(*newtype);
    LOG_CALL(restoreTypes, Type_dup, oldtype, *newtype);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_create_resized = PMPI_Type_create_resized
int PMPI_Type_create_resized(MPI_Datatype oldtype, MPI_Aint lb, MPI_Aint extent,
                            MPI_Datatype *newtype)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = oldtype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_create_resized)(real_datatype, lb, extent, newtype);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    *newtype = new_virt_datatype(*newtype);
    LOG_CALL(restoreTypes, Type_create_resized, oldtype, lb, extent, *newtype);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_get_extent = PMPI_Type_get_extent
int PMPI_Type_get_extent(MPI_Datatype datatype, MPI_Aint *lb, MPI_Aint *extent)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Type_get_extent)(real_datatype, lb, extent);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Pack_size = PMPI_Pack_size
int PMPI_Pack_size(int incount, MPI_Datatype datatype, MPI_Comm comm, int *size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Pack_size)(incount, real_datatype, realComm, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Pack = PMPI_Pack
int PMPI_Pack(const void *inbuf, int incount, MPI_Datatype datatype, 
             void *outbuf, int outsize, int *position, MPI_Comm comm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Pack)(inbuf, incount, real_datatype, outbuf,
                           outsize, position, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Type_get_name = PMPI_Type_get_name
int PMPI_Type_get_name(MPI_Datatype datatype, char *type_name, int *resultlen)
{
   int retval;
   DMTCP_PLUGIN_DISABLE_CKPT();
   MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
   JUMP_TO_LOWER_HALF(lh_info->fsaddr);
   retval = NEXT_FUNC(Type_get_name)(real_datatype, type_name, resultlen);
   RETURN_TO_UPPER_HALF(); 
   DMTCP_PLUGIN_ENABLE_CKPT();
   return retval;
}

#pragma weak MPI_Type_size_x = PMPI_Type_size_x
int PMPI_Type_size_x(MPI_Datatype type, MPI_Count *size)
{
   int retval;
   DMTCP_PLUGIN_DISABLE_CKPT();
   MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = type}).datatype;
   JUMP_TO_LOWER_HALF(lh_info->fsaddr);
   retval = NEXT_FUNC(Type_size_x)(real_datatype, size);
   RETURN_TO_UPPER_HALF(); 
   DMTCP_PLUGIN_ENABLE_CKPT();
   return retval;
}

} // end of: extern "C"
