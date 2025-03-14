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

int MPI_Comm_group(MPI_Comm comm, MPI_Group *group)
{
  int retval = MPI_SUCCESS;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Comm_group)(real_comm, group);
  RETURN_TO_UPPER_HALF();
  if (*group == lh_info->MANA_GROUP_NULL) {
    *group = MPI_GROUP_NULL;
  } else {
    *group = new_virt_group(*group);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Group_size(MPI_Group group, int *size)
{
  int retval = MPI_SUCCESS;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group real_group = get_real_id((mana_mpi_handle){.group = group}).group;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Group_size)(real_group, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Group_free(MPI_Group *group)
{
  int retval = MPI_SUCCESS;
  DMTCP_PLUGIN_DISABLE_CKPT();
  free_virt_id((mana_mpi_handle){.group = *group});
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Group_compare(MPI_Group group1, MPI_Group group2, int *result)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group real_group1 = get_real_id((mana_mpi_handle){.group = group1}).group;
  MPI_Group real_group2 = get_real_id((mana_mpi_handle){.group = group2}).group;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Group_compare)(real_group1, real_group2, result);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Group_rank(MPI_Group group, int *rank)
{
  int retval = MPI_SUCCESS;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group real_group = get_real_id((mana_mpi_handle){.group = group}).group;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Group_rank)(real_group, rank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Group_incl(MPI_Group group, int n, const int* ranks, MPI_Group * newgroup)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group real_group = get_real_id((mana_mpi_handle){.group = group}).group;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Group_incl)(real_group, n, ranks, newgroup);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    if (*newgroup == lh_info->MANA_GROUP_NULL) {
      *newgroup = MPI_GROUP_NULL;
    } else {
      *newgroup = new_virt_group(*newgroup);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Group_translate_ranks(MPI_Group group1, int n, const int ranks1[],
                          MPI_Group group2, int ranks2[])
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group real_group1 = get_real_id((mana_mpi_handle){.group = group1}).group;
  MPI_Group real_group2 = get_real_id((mana_mpi_handle){.group = group2}).group;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Group_translate_ranks)(real_group1, n, ranks1,
                                            real_group2, ranks2);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

} // end of: extern "C"
