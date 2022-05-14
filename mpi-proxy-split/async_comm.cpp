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
#include <pthread.h>
#include <unordered_map>
#include <execinfo.h>
#include <string.h>

#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"

#include "mpi_plugin.h"
#include "mpi_nextfunc.h"
#include "async_comm.h"
#include "p2p_drain_send_recv.h"
#include "virtual-ids.h"

using namespace dmtcp;

std::unordered_map<MPI_Request, record_t*> g_async_calls;
int g_world_rank = -1; // Global rank of the current process
int g_world_size = -1; // Total number of ranks in the current computation

void
getLocalRankInfo()
{
  if (g_world_rank == -1) {
    JASSERT(MPI_Comm_rank(MPI_COMM_WORLD, &g_world_rank) == MPI_SUCCESS &&
        g_world_rank != -1);
  }
  if (g_world_size == -1) {
    JASSERT(MPI_Comm_size(MPI_COMM_WORLD, &g_world_size) == MPI_SUCCESS &&
        g_world_size != -1);
  }
}

void *copy_param(const void *param, size_t size)
{
  void *copy = JALLOC_HELPER_MALLOC(size);
  memcpy(copy, param, size);
  return copy;
}

void free_record(record_t *record) {
  for (int i = 0; i < record->num_params; i++) {
    JALLOC_HELPER_FREE(record->params[i]);
  }
  JALLOC_HELPER_FREE(record);
}

void replayMpiAsyncCommOnRestart() {
  MPI_Request request;
  record_t *call = NULL;
  JTRACE("Replaying unserviced isend/irecv calls");

  for (std::pair<MPI_Request, record_t*> it : g_async_calls) {
    int retval = 0;
    request = it.first;
    call = it.second;
    MPI_Request realRequest;
    switch (call->fnc) {
      case GENERATE_ENUM(Irecv):
        JTRACE("Replaying Irecv call");
        void *recvbuf = *(void**) call->params[0];
        int count = *(int*) call->params[1];
        MPI_Datatype type = *(MPI_Datatype*) call->params[2];
        MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(type);
        int source = *(int*) call->params[3];
        int tag = *(int*) call->params[4];
        MPI_Comm comm = *(MPI_Comm*) call->params[5];
        MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
        JUMP_TO_LOWER_HALF(lh_info.fsaddr);
        retval = NEXT_FUNC(Irecv)(recvbuf, count, realType, source, tag,
                                  realComm, &realRequest);
        RETURN_TO_UPPER_HALF();
        JASSERT(retval == MPI_SUCCESS).Text("Error while replaying recv");
        break;
      default:
        JWARNING(false)(MPI_Fnc_strings[call->fnc])
                .Text("Unhandled replay call");
        break;
    }
    UPDATE_REQUEST_MAP(request, realRequest);
  }
}
