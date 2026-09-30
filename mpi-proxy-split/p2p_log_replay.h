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

#ifndef _P2P_COMM_H
#define _P2P_COMM_H

#include <mpi.h>
#include <stdint.h>
#include <vector>
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "virtual_id.h"

#define REAL_REQUEST_LOG_LEVEL 7
#define STACK_TRACK_LEVEL 7

#ifdef DEBUG
// #define USE_REQUEST_LOG
#endif

// Struct to store and return the MPI message (data) during draining and
// resuming, also used by p2p_drain_send_recv.h
typedef struct __mpi_message
{
  void *buf;
  int count;
  MPI_Datatype datatype;
  int size;
  MPI_Comm comm;
  MPI_Status status;
} mpi_message_t;

// Struct to store request type and backtrace information for debugging
typedef struct __request_info
{
  mpi_req_t type;
  MPI_Request real_request[REAL_REQUEST_LOG_LEVEL];
  int update_counter;
  void *backtrace[STACK_TRACK_LEVEL];
} request_info_t;


extern int g_world_rank; // Global rank of the current process
extern int g_world_size; // Total number of ranks in the current computation

// Fetches the MPI rank and world size; also, verifies that MPI rank and
// world size match the globally stored values in the plugin
extern void getLocalRankInfo();

// Sets the name of the checkpoint directory of the current process to
// "ckpt_rank_<RANK>", where RANK is the MPI rank of the process.
extern void updateCkptDirByRank();

// Restores the state of MPI P2P communication by replaying any pending
// MPI_Isend and MPI_Irecv requests post restart
extern void replayMpiP2pOnRestart();

// Saves the nonblocking send/recv call of the given type and params with the
// (virtual) MPI_Request 'rq', in the request's virtual-ID table entry
extern void addPendingRequestToLog(mpi_req_t , const void* , void* , int ,
                                   MPI_Datatype , int , int ,
                                   MPI_Comm, MPI_Request);

// remove finished send/recv call from the pending calls
extern void clearPendingRequestFromLog(MPI_Request req);

// Returns the pending requests in the order they were posted.  MPI matches
// receives in posting order, so the drain and the restart replay must follow
// it; the order of the (reused) virtual request handles doesn't.
extern std::vector<MPI_Request> pendingRequestsInPostingOrder();

// Returns the type of a pending request, or UNKNOW_REQUEST if the request is
// not (or no longer) pending.
extern mpi_req_t pendingRequestType(MPI_Request req);

// Copies the call of a pending request to *call.  Returns false if the
// request is not (or no longer) pending.
extern bool getPendingCall(MPI_Request req, mpi_nonblocking_call_t *call);

// Returns true if a pending MPI_Isend/MPI_Irecv uses the datatype.
extern bool pendingCallUsesDatatype(MPI_Datatype type);

// Log the creation or update of a virtual request
extern void logRequestInfo(MPI_Request request, mpi_req_t req_type);

// Lookup a request's info in the request_log
extern request_info_t* lookupRequestInfo(MPI_Request request);

#endif // ifndef _P2P_LOG_REPLAY_H
