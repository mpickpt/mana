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


USER_DEFINED_WRAPPER(int, Op_create,
                     (MPI_User_function *) user_fn, (int) commute,
                     (MPI_Op *) op)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Op_create)(user_fn, commute, op);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Op virtOp = ADD_NEW_OP(*op);
    *op = virtOp;
    LOG_CALL(restoreOps, Op_create, user_fn, commute, virtOp);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Op_free, (MPI_Op*) op)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Op realOp = MPI_OP_NULL;
  if (op) {
    realOp = VIRTUAL_TO_REAL_OP(*op);
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Op_free)(&realOp);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    // NOTE: We cannot remove the old op, since we'll need
    // to replay this call to reconstruct any new op that might
    // have been created using this op.
    //
    // realOp = REMOVE_OLD_OP(*op);
    LOG_CALL(restoreOps, Op_free, *op);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}


PMPI_IMPL(int, MPI_Op_create, MPI_User_function *user_fn,
          int commute, MPI_Op *op)
PMPI_IMPL(int, MPI_Op_free, MPI_Op *op)
