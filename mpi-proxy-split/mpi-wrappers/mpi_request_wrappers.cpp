/****************************************************************************
 *   Copyright (C) 2019-2022 by Gene Cooperman, Illio Suardi, Rohan Garg,   *
 *   Yao Xu                                                                 *
 *   gene@ccs.neu.edu, illio@u.nus.edu, rohgarg@ccs.neu.edu,                *
 *   xu.yao1@northeastern.edu                                               *
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

#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "record-replay.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "mpi_plugin.h"
#include "mpi_nextfunc.h"
#include "virtual_id.h"
// To support MANA_P2P_LOG and MANA_P2P_REPLAY:
#include "p2p-deterministic.h"

extern int p2p_deterministic_skip_save_request;

extern "C" {

int MPI_Test_internal(MPI_Request *request, int *flag, MPI_Status *status,
                      bool isRealRequest)
{
  int retval;
  MPI_Request real_request;
  if (isRealRequest) {
    real_request = *request;
  } else {
    real_request = get_real_id((mana_mpi_handle){.request = *request}).request;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  // MPI_Test can change the *request argument
  retval = NEXT_FUNC(Test)(&real_request, flag, status);

  RETURN_TO_UPPER_HALF();
  return retval;
}

#pragma weak MPI_Test = PMPI_Test
int PMPI_Test(MPI_Request* request, int* flag, MPI_Status* status)
{
  int retval;
  if (*request == MPI_REQUEST_NULL) {
    // *request might be in read-only memory. So we can't overwrite it with
    // MPI_REQUEST_NULL later.
    *flag = true;
    return MPI_SUCCESS;
  }
  LOG_PRE_Test(status);
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Status statusBuffer;
  MPI_Status *statusPtr = status;
  if (statusPtr == MPI_STATUS_IGNORE ||
      statusPtr == FORTRAN_MPI_STATUS_IGNORE) {
    statusPtr = &statusBuffer;
  }
  MPI_Request real_request;
  real_request = get_real_id((mana_mpi_handle){.request = *request}).request;
  if (*request != MPI_REQUEST_NULL && real_request == MPI_REQUEST_NULL) {
    *flag = 1;
    free_virt_id((mana_mpi_handle){.request = *request});
    *request = MPI_REQUEST_NULL;
    DMTCP_PLUGIN_ENABLE_CKPT();
    // FIXME: We should also fill in the status
    return MPI_SUCCESS;
  }

  retval = MPI_Test_internal(&real_request, flag, statusPtr, true);
  // Updating global counter of recv bytes
  // FIXME: This if statement should be merged into
  // clearPendingRequestFromLog()
  if (*flag && *request != MPI_REQUEST_NULL
      && g_nonblocking_calls.find(*request) != g_nonblocking_calls.end()
      && g_nonblocking_calls[*request]->type == IRECV_REQUEST) {
    local_recv_messages++;
#ifdef DEBUG_P2P
    int count = 0;
    int size = 0;
    MPI_Get_count(statusPtr, MPI_BYTE, &count);
    MPI_Type_size(MPI_BYTE, &size);
    JASSERT(size == 1)(size);
    MPI_Comm comm = g_nonblocking_calls[*request]->comm;
    int worldRank = localRankToGlobalRank(statusPtr->MPI_SOURCE, comm);
    g_recvBytesByRank[worldRank] += count * size;
    // For debugging
#if 0
    printf("rank %d received %d bytes from rank %d\n", g_world_rank, count * size, worldRank);
    fflush(stdout);
#endif
#endif
  }
  LOG_POST_Test(request, statusPtr);
  if (retval == MPI_SUCCESS && *flag && MPI_LOGGING()) {
    clearPendingRequestFromLog(*request);
    free_virt_id((mana_mpi_handle){.request = *request});
    LOG_REMOVE_REQUEST(*request); // remove from record-replay log
    *request = MPI_REQUEST_NULL;
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Testall = PMPI_Testall
int PMPI_Testall(int count, MPI_Request *array_of_requests, int *flag,
                MPI_Status *array_of_statuses)
{
  // NOTE: See MPI_Testany below for the rationale for these variables.
  int local_count = count;
  MPI_Request *local_array_of_requests = array_of_requests;
  int *local_flag = flag;
  MPI_Status *local_array_of_statuses = array_of_statuses;

  int retval = MPI_SUCCESS;
  bool incomplete = false;
  // FIXME: Perhaps use Testall directly? But then, need to take care of
  // the services requests
  for (int i = 0; i < count; i++) {
    // FIXME: Ideally, we should only check FORTRAN_MPI_STATUS_IGNORE
    //        in the Fortran wrapper.
    if (local_array_of_statuses != MPI_STATUSES_IGNORE &&
        local_array_of_statuses != FORTRAN_MPI_STATUSES_IGNORE) {
      retval = MPI_Test(&local_array_of_requests[i], local_flag,
                        &local_array_of_statuses[i]);
    } else {
      retval = MPI_Test(&local_array_of_requests[i], local_flag,
                        MPI_STATUS_IGNORE);
    }
    if (retval != MPI_SUCCESS) {
      *local_flag = 0;
      break;
    }
    if (*local_flag == 0) {
      incomplete = true;
    }
  }
  if (incomplete) {
    *local_flag = 0;
  }
  return retval;
}

#pragma weak MPI_Testany = PMPI_Testany
int PMPI_Testany(int count, MPI_Request *array_of_requests, int *index,
                int *flag, MPI_Status *status)
{
  // FIXME:  Revise this note if definition if FORTRAM_MPI_STATUS_IGNORE
  //         fixes the problem.
  // NOTE: We're seeing a weird bug with the Fortran-to-C interface when Nimrod
  // is being run with MANA, where it seems like a Fortran routine is passing
  // these arguments in registers instead of on the stack, which causes the
  // values inside to be corrupted when a function call returns. This seems to
  // only affect functions that pass an array from Fortran to C - namely
  // Testall, Testany, Testsome, Waitall, Waitany and Waitsome. We use a
  // temporary workaround below.
  int local_count = count;
  MPI_Request *local_array_of_requests = array_of_requests;
  int *local_index = index;
  int *local_flag = flag;
  MPI_Status *local_status = status;

  int retval = MPI_SUCCESS;
  *local_flag = 1;
  *local_index = MPI_UNDEFINED;
  for (int i = 0; i < local_count; i++) {
    if (local_array_of_requests[i] == MPI_REQUEST_NULL) {
      continue;
    }
    retval = MPI_Test(&local_array_of_requests[i], local_flag, local_status);
    if (retval != MPI_SUCCESS) {
      break;
    }
    if (*local_flag) {
      *local_index = i;
      break;
    }
  }
  return retval;
}

#pragma weak MPI_Waitall = PMPI_Waitall
int PMPI_Waitall(int count, MPI_Request *array_of_requests,
                MPI_Status *array_of_statuses)
{
  // FIXME: Revisit this wrapper - call get_real_id on array
  int retval = MPI_SUCCESS;
#if 0
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Waitall)(count, array_of_requests, array_of_statuses);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    for (int i = 0; i < count; i++) {
      clearPendingRequestFromLog(&array_of_requests[i]);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
#else
  // NOTE: See MPI_Testany above for the rationale for these variables.
  int local_count = count;
  MPI_Request *local_array_of_requests = array_of_requests;
  MPI_Status *local_array_of_statuses = array_of_statuses;

  get_fortran_constants();
  for (int i = 0; i < count; i++) {
    /* FIXME: Is there a chance it gets a valid C address, which we shouldn't
     * ignore?  Ideally, we should only check FORTRAN_MPI_STATUSES_IGNORE
     * in the Fortran wrapper.
     */
    if (local_array_of_statuses != MPI_STATUSES_IGNORE &&
        local_array_of_statuses != FORTRAN_MPI_STATUSES_IGNORE) {
      retval = MPI_Wait(&local_array_of_requests[i],
                        &local_array_of_statuses[i]);
    } else {
      retval = MPI_Wait(&local_array_of_requests[i], MPI_STATUS_IGNORE);
    }
    if (retval != MPI_SUCCESS) {
      break;
    }
  }
#endif
  return retval;
}

#pragma weak MPI_Waitany = PMPI_Waitany
int PMPI_Waitany(int count, MPI_Request *array_of_requests,
                int *index, MPI_Status *status)
{
  // NOTE: See MPI_Testany above for the rationale for these variables.
  int local_count = count;
  MPI_Request *local_array_of_requests = array_of_requests;
  int *local_index = index;
  MPI_Status *local_status = status;

  int retval = MPI_SUCCESS;
  int flag = 0;
  bool all_null = true;
  *local_index = MPI_UNDEFINED;
  int was_null[count] = {0};
  for (int i = 0; i < count; i++) {
    was_null[i] = local_array_of_requests[i] == MPI_REQUEST_NULL ? 1 : 0;
  }
  while (1) {
    for (int i = 0; i < count; i++) {
      if (local_array_of_requests[i] == MPI_REQUEST_NULL) {
        if (was_null[i]) {
          continue;
        } else {
          *local_index = i;
          return retval;
        }
      }
      all_null = false;
      DMTCP_PLUGIN_DISABLE_CKPT();
      retval = MPI_Test_internal(&local_array_of_requests[i], &flag,
                                 local_status, false);
      if (retval != MPI_SUCCESS) {
        DMTCP_PLUGIN_ENABLE_CKPT();
        return retval;
      }
      if (flag) {
        MPI_Request *request = &local_array_of_requests[i];
        if (*request != MPI_REQUEST_NULL
          && g_nonblocking_calls.find(*request) != g_nonblocking_calls.end()
          && g_nonblocking_calls[*request]->type == IRECV_REQUEST) {
          local_recv_messages++;
#ifdef DEBUG_P2P
          int count = 0;
          int size = 0;
          MPI_Get_count(local_status, MPI_BYTE, &count);
          MPI_Type_size(MPI_BYTE, &size);
          JASSERT(size == 1)(size);
          MPI_Comm comm = g_nonblocking_calls[*request]->comm;
          int worldRank = localRankToGlobalRank(local_status->MPI_SOURCE, comm);
          g_recvBytesByRank[worldRank] += count * size;
#endif
        } else if (*request == MPI_REQUEST_NULL) {
          if (!was_null[i]) {
            *local_index = i;
            return retval;
          }
        }

        if (MPI_LOGGING()) {
          clearPendingRequestFromLog(local_array_of_requests[i]);
          free_virt_id((mana_mpi_handle){.request = local_array_of_requests[i]});
          local_array_of_requests[i] = MPI_REQUEST_NULL;
        }

        *local_index = i;

        DMTCP_PLUGIN_ENABLE_CKPT();
        return retval;
      }

      DMTCP_PLUGIN_ENABLE_CKPT();
    }
    if (all_null) {
      return retval;
    }
  }
}

#pragma weak MPI_Wait = PMPI_Wait
int PMPI_Wait(MPI_Request *request, MPI_Status *status)
{
  int retval;
  if (*request == MPI_REQUEST_NULL) {
    // *request might be in read-only memory. So we can't overwrite it with
    // MPI_REQUEST_NULL later.
    return MPI_SUCCESS;
  }
  int flag = 0;
  MPI_Status statusBuffer;
  MPI_Status *statusPtr = status;
  // FIXME: Ideally, we should only check FORTRAN_MPI_STATUS_IGNORE
  //        in the Fortran wrapper.
  if (statusPtr == MPI_STATUS_IGNORE ||
      statusPtr == FORTRAN_MPI_STATUS_IGNORE) {
    statusPtr = &statusBuffer;
  }
  // FIXME: We translate the virtual request in every iteration.
  // We want to translate it only once, and update the real request
  // after restart if we checkpoint in the while loop.
  // Then MPI_Test_internal should use isRealRequest = true.
  while (!flag) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    retval = MPI_Test_internal(request, &flag, statusPtr, false);
    // Updating global counter of recv bytes
    // FIXME: This if statement should be merged into
    // clearPendingRequestFromLog()
    if (flag && *request != MPI_REQUEST_NULL
        && g_nonblocking_calls.find(*request) != g_nonblocking_calls.end()
        && g_nonblocking_calls[*request]->type == IRECV_REQUEST) {
      local_recv_messages++;
#ifdef DEBUG_P2P
      int count = 0;
      int size = 0;
      MPI_Get_count(statusPtr, MPI_BYTE, &count);
      MPI_Type_size(MPI_BYTE, &size);
      JASSERT(size == 1)(size);
      MPI_Comm comm = g_nonblocking_calls[*request]->comm;
      int worldRank = localRankToGlobalRank(statusPtr->MPI_SOURCE, comm);
      g_recvBytesByRank[worldRank] += count * size;
    // For debugging
#if 0
      printf("rank %d received %d bytes from rank %d\n", g_world_rank, count * size, worldRank);
      fflush(stdout);
#endif
#endif
    }
    if (p2p_deterministic_skip_save_request == 0) {
      if (flag) LOG_POST_Wait(request, statusPtr);
    }
    if (flag && MPI_LOGGING()) {
      clearPendingRequestFromLog(*request); // Remove from g_nonblocking_calls
      free_virt_id((mana_mpi_handle){.request = *request}); // Remove from virtual id
      LOG_REMOVE_REQUEST(*request); // Remove from record-replay log
      *request = MPI_REQUEST_NULL;
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
  }
  return retval;
}

#pragma weak MPI_Probe = PMPI_Probe
int PMPI_Probe(int source, int tag, MPI_Comm comm, MPI_Status *status)
{
  int retval;
  int flag = 0;
  while (!flag) {
    retval = MPI_Iprobe(source, tag, comm, &flag, status);
  }
  return retval;
}

#pragma weak MPI_Iprobe = PMPI_Iprobe
int PMPI_Iprobe(int source, int tag, MPI_Comm comm, int *flag, MPI_Status *status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  // LOG_PRE_Iprobe(status);

  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Iprobe)(source, tag, realComm, flag, status);
  RETURN_TO_UPPER_HALF();
  // LOG_POST_Iprobe(source,tag,comm,status);
  // REPLAY_POST_Iprobe(source,tag,comm,status,flag);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Request_get_status = PMPI_Request_get_status
int PMPI_Request_get_status(MPI_Request request, int *flag, MPI_Status *status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Request real_request = get_real_id((mana_mpi_handle){.request = request}).request;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Request_get_status)(real_request, flag, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Get_elements = PMPI_Get_elements
int PMPI_Get_elements(const MPI_Status *status, MPI_Datatype datatype,
                     int *count)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Get_elements)(status, datatype, count);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Get_elements_x = PMPI_Get_elements_x
int PMPI_Get_elements_x(const MPI_Status *status, MPI_Datatype datatype,
                       MPI_Count *count)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Get_elements_x)(status, datatype, count);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

} // end of: extern "C"
