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

#include <mpi.h>
#include "jassert.h"
#include "jconvert.h"

#define USES_MPI_Fnc_strings
#include "record-replay.h"
#include "virtual_id.h"
#include "p2p_log_replay.h"

using namespace dmtcp_mpi;


static int restoreCartCreate(MpiRecord& rec);
static int restoreCartMap(MpiRecord& rec);
static int restoreCartShift(MpiRecord& rec);
static int restoreCartSub(MpiRecord& rec);

void
restoreMpiLogState()
{
  JASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS)
          .Text("Failed to restore MPI state");
}

int
dmtcp_mpi::restoreCarts(MpiRecord &rec)
{
  int rc = -1;
  JTRACE("Restoring MPI cartesian");
  switch (rec.getType()) {
    case GENERATE_ENUM(Cart_create):
      JTRACE("restoreCartCreate");
      rc = restoreCartCreate(rec);
      break;
    case GENERATE_ENUM(Cart_map):
      JTRACE("restoreCartMap");
      rc = restoreCartMap(rec);
      break;
    case GENERATE_ENUM(Cart_shift):
      JTRACE("restoreCartShift");
      rc = restoreCartShift(rec);
      break;
    case GENERATE_ENUM(Cart_sub):
      JTRACE("restoreCartSub");
      rc = restoreCartSub(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

void MpiRecordReplay::printRecords(bool print)
{
  JNOTE("Printing _records");
  for(MpiRecord* record : _records) {
    int fnc_idx = record->getType();
    if (print) {
      printf("%s\n", MPI_Fnc_strings[fnc_idx]);
    } else {
      JNOTE("") (MPI_Fnc_strings[fnc_idx]);
    }
  }
}

static int
restoreCartCreate(MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = (MPI_Comm)(int)rec.args(0);
  int ndims = rec.args(1);
  int *dims = rec.args(2);
  int *periods = rec.args(3);
  int reorder = rec.args(4);
  MPI_Comm newcomm = MPI_COMM_NULL;
  retval = FNC_CALL(Cart_create, rec)(comm, ndims, dims,
                                      periods, reorder, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = (MPI_Comm)(int)rec.args(5);
    update_virt_id((mana_mpi_handle){.comm = virtComm}, (mana_mpi_handle){.comm = newcomm});
  }
  return retval;
}

static int
restoreCartMap(MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = (MPI_Comm)(int)rec.args(0);
  int ndims = rec.args(1);
  int *dims = rec.args(2);
  int *periods = rec.args(3);
  int newrank = -1;
  retval = FNC_CALL(Cart_map, rec)(comm, ndims, dims, periods, &newrank);
  if (retval == MPI_SUCCESS) {
    // FIXME: Virtualize rank?
    int oldrank = rec.args(4);
    JASSERT(newrank == oldrank)(oldrank)(newrank).Text("Different ranks");
  }
  return retval;
}

static int
restoreCartShift(MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = (MPI_Comm)(int)rec.args(0);
  int direction = rec.args(1);
  int disp = rec.args(2);
  int rank_source = -1;
  int rank_dest = -1;
  retval = FNC_CALL(Cart_shift, rec)(comm, direction,
                                     disp, &rank_source, &rank_dest);
  if (retval == MPI_SUCCESS) {
    // FIXME: Virtualize rank?
    int oldsrc = rec.args(3);
    int olddest = rec.args(4);
    JASSERT(oldsrc == rank_source && olddest == rank_dest)
           (oldsrc)(olddest)(rank_source)(rank_dest).Text("Different ranks");
  }
  return retval;
}

static int
restoreCartSub(MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = (MPI_Comm)(int)rec.args(0);
  // int ndims = rec.args(1);
  int *remain_dims = rec.args(2);
  MPI_Comm newcomm = MPI_COMM_NULL;
  // LOG_CALL(restoreCarts, Cart_sub, comm, ndims, rs, virtComm);
  retval = FNC_CALL(Cart_sub, rec)(comm, remain_dims, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = (MPI_Comm)(int)rec.args(3);
    update_virt_id((mana_mpi_handle){.comm = virtComm}, (mana_mpi_handle){.comm = newcomm});
  }
  return retval;
}
