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

#ifndef _ASYNC_COMM_H
#define _ASYNC_COMM_H

#include <mpi.h>
#include <unordered_map>
#include "dmtcp.h"
#include "dmtcpalloc.h"

// Struct to store the metadata of an async MPI send/recv call
typedef struct __record
{
  MPI_Fncs fnc;  // function that created this request 
  void *params[10];
  int num_params;
} record_t;

extern std::unordered_map<MPI_Request, record_t*> g_async_calls;
extern int g_world_rank; // Global rank of the current process
extern int g_world_size; // Total number of ranks in the current computation

// Fetches the MPI rank and world size; also, verifies that MPI rank and
// world size match the globally stored values in the plugin
void getLocalRankInfo();

// Restores the state of MPI P2P communication by replaying any pending
// MPI_Isend and MPI_Irecv requests post restart
void replayMpiAsyncCommOnRestart();

#if 0
// Saves the async call and params to a global map indexed by the MPI_Request 'req'
template<typename.. Targs>
extern void addPendingRequestToLog(MPI_Fncs fnc, MPI_Request req, 
                                   Targs... args);

// remove finished send/recv call from the global map
extern void clearPendingRequestFromLog(MPI_Request req);
#endif

void *copy_param(const void *param, size_t size);

// Handle one simple argument
template<typename T>
void addArgs(record_t *record, int idx, const T arg)
{
  record->params[idx] = copy_param((void*)&arg, sizeof(arg));
  record->num_params = idx + 1;
}

// Handle list of arguments
template<typename T, typename... Targs>
void addArgs(record_t *record, int idx, const T car, const Targs... cdr)
{
  addArgs(record, idx, car);
  addArgs(record, idx + 1, cdr...);
}

template<typename... Targs>
record_t *record_fnc(MPI_Fncs fnc, Targs... args) {
  record_t *record = (record_t*) JALLOC_HELPER_MALLOC(sizeof(record_t));
  record->fnc = fnc;
  addArgs(record, 0, args...);
  return record;
}

void free_record(record_t *record);
#endif // ifndef _ASYNC_COMM_H
