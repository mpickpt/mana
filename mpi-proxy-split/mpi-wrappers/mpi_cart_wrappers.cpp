/****************************************************************************
  *  Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
  *  gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
  *                                                                         *
  * This file is part of DMTCP.                                             *
  *                                                                         *
  * DMTCP is free software: you can redistribute it and/or                  *
  * modify it under the terms of the GNU Lesser General Public License as   *
  * published by the Free Software Foundation, either version 3 of the      *
  * License, or (at your option) any later version.                         *
  *                                                                         *
  * DMTCP is distributed in the hope that it will be useful,                *
  * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
  * GNU Lesser General Public License for more details.                     *
  *                                                                         *
  * You should have received a copy of the GNU Lesser General Public        *
  * License in the files COPYING and COPYING.LESSER.  If not, see           *
  * <http://www.gnu.org/licenses/>.                                         *
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
#ifdef SINGLE_CART_REORDER
#include "two-phase-algo.h"
#include "seq_num.h"
#include "../cartesian.h"
#endif
#include "p2p_drain_send_recv.h"

using namespace dmtcp_mpi;

extern "C" {

#pragma weak MPI_Cart_coords = PMPI_Cart_coords
int PMPI_Cart_coords(MPI_Comm comm, int rank, int maxdims, int *coords)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Cart_coords)(realComm, rank, maxdims, coords);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Cart_get = PMPI_Cart_get
int PMPI_Cart_get(MPI_Comm comm, int maxdims, int *dims, int *periods, int *coords)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Cart_get)(realComm, maxdims, dims, periods, coords);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Cart_map = PMPI_Cart_map
int PMPI_Cart_map(MPI_Comm comm, int ndims, const int *dims, const int *periods,
                 int  *newrank)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  // FIXME: Need to virtualize this newrank??
  retval = NEXT_FUNC(Cart_map)(realComm, ndims, dims, periods, newrank);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    FncArg ds = CREATE_LOG_BUF(dims, ndims  *sizeof(int));
    FncArg ps = CREATE_LOG_BUF(periods, ndims  *sizeof(int));
    LOG_CALL(restoreCarts, Cart_map, comm, ndims, ds, ps, newrank);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Cart_rank = PMPI_Cart_rank
int PMPI_Cart_rank(MPI_Comm comm, const int *coords, int *rank)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Cart_rank)(realComm, coords, rank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Cart_shift = PMPI_Cart_shift
int PMPI_Cart_shift(MPI_Comm comm, int direction, int disp, int *rank_source,
                   int *rank_dest)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Cart_shift)(realComm, direction,
                                 disp, rank_source, rank_dest);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    LOG_CALL(restoreCarts, Cart_shift, comm, direction,
             disp, *rank_source, *rank_dest);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Cart_sub = PMPI_Cart_sub
int PMPI_Cart_sub(MPI_Comm comm, const int *remain_dims, MPI_Comm *new_comm)
{
  int retval;

  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Cart_sub)(realComm, remain_dims, new_comm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    int ndims = 0;
    MPI_Cartdim_get(comm, &ndims);
    *new_comm = new_virt_comm(*new_comm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Cartdim_get = PMPI_Cartdim_get
int PMPI_Cartdim_get(MPI_Comm comm, int *ndims)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Cartdim_get)(realComm, ndims);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Dims_create = PMPI_Dims_create
int PMPI_Dims_create(int nnodes, int ndims, int *dims)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Dims_create)(nnodes, ndims, dims);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#ifdef SINGLE_CART_REORDER
// This variable holds the cartesian properties and is only used at the time of
// checkpoint (DMTCP_EVENT_PRECHECKPOINT event in mpi_plugin.cpp).
CartesianProperties g_cartesian_properties = { .comm_old_size = -1,
                                               .comm_cart_size = -1,
                                               .comm_old_rank = -1,
                                               .comm_cart_rank = -1 };

#pragma weak MPI_Cart_create = PMPI_Cart_create
int PMPI_Cart_create(MPI_Comm old_comm, int ndims,
                    const int *dims, const int *periods, int reorder,
                    MPI_Comm *comm_cart)
{
  JWARNING(g_cartesian_properties.comm_old_size == -1)
    .Text("MPI_Cart_create() called more than once. Current implementation "
          "only supports one cartesian communicator.");

  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = get_real_id(old_comm).comm;
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    retval = NEXT_FUNC(Cart_create)(realComm, ndims, dims, periods, reorder,
                                    comm_cart);
    RETURN_TO_UPPER_HALF();
    g_cartesian_properties.ndims = ndims;
    g_cartesian_properties.reorder = reorder;
    for (int i = 0; i < ndims; i++) {
      g_cartesian_properties.dimensions[i] = dims[i];
      g_cartesian_properties.periods[i] = periods[i];
    }
    MPI_Comm_size(old_comm, &g_cartesian_properties.comm_old_size);
    MPI_Comm_size(*comm_cart, &g_cartesian_properties.comm_cart_size);
    MPI_Comm_rank(old_comm, &g_cartesian_properties.comm_old_rank);
    MPI_Comm_rank(*comm_cart, &g_cartesian_properties.comm_cart_rank);
    MPI_Cart_coords(*comm_cart, g_cartesian_properties.comm_cart_rank,
                    g_cartesian_properties.ndims,
                    g_cartesian_properties.coordinates);

    if (retval == MPI_SUCCESS && MPI_LOGGING()) {
      *comm_cart = new_virt_comm(*comm_cart);
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(old_comm, realBarrierCb);
}
#else

#pragma weak MPI_Cart_create = PMPI_Cart_create
int PMPI_Cart_create(MPI_Comm old_comm, int ndims,
                    const int *dims, const int *periods, int reorder,
                    MPI_Comm *comm_cart)
{
  int retval;
  // The MPI library assigns the cartesian coordinates naively
  // (in the increasing rank order) with no reordering as opposed to optimal
  // reordering based on the physical topology. For example, if there are
  // 6 ranks (0-5) exist, then without reordering, the two-dimensional
  // coordinates will be following:
  // (0, 0) -> rank 0
  // (0, 1) -> rank 1
  // .
  // .
  // (2, 2) -> rank 5
  // While no reordering ensures the same rank to coordinates mapping
  // on the restart,  it can incur some overhead as MPI can no longer
  // optimize the ranks' ordering. For now, we focus on the correctness across
  // checkpoint-restart. Therefore, we enforce the reorder variable
  // to be false.
  //FIXME: Handle the case when reorder variable is true.
  JWARNING (reorder == false) .Text ("We are enforcing reorder to false as "
                                     "the current implementation does not "
                                     "support reordered ranks.");
  reorder = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = old_comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Cart_create)(realComm, ndims, dims,
                                  periods, reorder, comm_cart);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && MPI_LOGGING()) {
    *comm_cart = new_virt_comm(*comm_cart);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#endif

} // end of: extern "C"
