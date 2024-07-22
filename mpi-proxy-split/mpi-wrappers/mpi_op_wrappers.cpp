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


USER_DEFINED_WRAPPER(int, Op_create,
                     (MPI_User_function *) user_fn, (int) commute,
                     (MPI_Op *) op)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Op_create)(user_fn, commute, op);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    *op = new_virt_op(*op);
    mana_op_desc *op_desc = (mana_op_desc*)get_virt_id_desc({.op = *op});
    op_desc->user_fn = user_fn;
    op_desc->commute = commute;
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Op_free, (MPI_Op*) op)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Op real_op = MPI_OP_NULL;
  if (op) {
    real_op = get_real_id({.op = *op}).op;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Op_free)(&real_op);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    free_virt_id({.op = *op});
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Reduce_local,
                     (const void *) inbuf, (void *) inoutbuf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Op) op)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype real_datatype = get_real_id({.datatype = datatype}).datatype;
  MPI_Op real_op = get_real_id({.op = op}).op;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Reduce_local)(inbuf, inoutbuf, count, real_datatype, real_op);
  RETURN_TO_UPPER_HALF();
  // This is non-blocking.  No need to log it.
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}


PMPI_IMPL(int, MPI_Op_create, MPI_User_function *user_fn,
          int commute, MPI_Op *op)
PMPI_IMPL(int, MPI_Op_free, MPI_Op *op)
PMPI_IMPL(int, MPI_Reduce_local, const void *inbuf, void *inoutbuf, int count,
          MPI_Datatype datatype, MPI_Op op)
