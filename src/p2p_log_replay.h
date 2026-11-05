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
  // An MPI_Issend's: its sender's world rank, to ack when it is received,
  // and the ack's tag (see publishSsendNotices()).  Otherwise ack_to is -1.
  int ack_to;
  int ack_tag;
} mpi_message_t;


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

// Saves a nonblocking send/recv call in the virtual-ID entry of request 'rq'.
extern void addPendingRequestToLog(mpi_req_t , const void* , void* , int ,
                                   MPI_Datatype , int , int ,
                                   MPI_Comm, MPI_Request);

// remove finished send/recv call from the pending calls
extern void clearPendingRequestFromLog(MPI_Request req);

// Replaces the call of a pending request; it keeps its place in the log.
extern void replacePendingCall(MPI_Request req,
                               const mpi_nonblocking_call_t *call);

// Counts a send in the later_sends of each pending MPI_Issend with its
// envelope.  Call it before the send enters the lower half.
extern void countSendAfterIssends(MPI_Comm comm, int dest, int tag);

// Returns the pending requests in posting order (not handle order).  MPI
// matches receives in that order, so the drain and restart replay use it.
extern std::vector<MPI_Request> pendingRequestsInPostingOrder();

// Returns the type of a pending request, or UNKNOW_REQUEST if the request is
// not (or no longer) pending.
extern mpi_req_t pendingRequestType(MPI_Request req);

// Copies the call of a pending request to *call.  Returns false if the
// request is not (or no longer) pending.
extern bool getPendingCall(MPI_Request req, mpi_nonblocking_call_t *call);

// Returns true if a pending MPI_Isend/MPI_Irecv uses the datatype.
extern bool pendingCallUsesDatatype(MPI_Datatype type);

#endif // ifndef _P2P_LOG_REPLAY_H
