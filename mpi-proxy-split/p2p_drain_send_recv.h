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

#ifndef _P2P_SEND_RECV_H
#define _P2P_SEND_RECV_H

#include <unordered_set>
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "p2p_log_replay.h"

#ifdef DEBUG_P2P
extern int *g_sendBytesByRank; // Number of bytes sent to other ranks
extern int *g_rsendBytesByRank; // Number of bytes sent to other ranks by MPI_Rsend
extern int *g_bytesSentToUsByRank; // Number of bytes other ranks sent to us
extern int *g_recvBytesByRank; // Number of bytes received from other ranks
#endif
extern int64_t global_sent_messages, global_recv_messages;
extern int64_t local_sent_messages, local_recv_messages;
extern std::unordered_set<MPI_Comm> active_comms;
extern dmtcp::vector<mpi_message_t*> g_message_queue;

// State of a single pending blocking MPI_Recv.
//
// MANA does not support MPI_THREAD_MULTIPLE.  Therefore at most one
// MPI_Recv can be in flight per process at any time, and a single
// global slot suffices to record its parameters.  If MPI_THREAD_MULTIPLE
// support is ever added, this slot will need to become per-thread, and
// the kvdb publish in unblockPendingRecvs() will need to publish a

  // It is read by unblockPendingRecvs() running in the DMTCP coordinator
// thread during pre-suspend.  Declared volatile for cross-thread
// visibility.
typedef struct {
  volatile bool active;
  // The following fields are valid only when active is true.
  int source;     // user-provided value; may be MPI_ANY_SOURCE
  int tag;        // user-provided value; may be MPI_ANY_TAG
  MPI_Comm comm;  // virtual communicator
  int count;      // user-provided count (needed for dummy buffer size)
  MPI_Datatype datatype;  // virtual datatype handle (needed for dummy matching)
} pending_recv_t;

extern pending_recv_t g_pending_recv;

// Set to true at the start of the pending-Recv dummy-injection phase
// (after drainInFlightP2p() returns and global_sent ==
// global_recv has been proven).  Cleared in resetDrainCounters() on
// EVENT_RESUME and EVENT_RESTART.
//
// INVARIANT while true: no real user-issued p2p messages can arrive at
// any rank.  Any MPI_Recv that returns from NEXT_FUNC(Recv) while this
// flag is true has consumed a dummy message injected by
// unblockPendingRecvs() and must be discarded.
//
// The MPI_Recv wrapper reads this flag once, immediately after
// NEXT_FUNC(Recv) returns.  A single post-call read is sufficient:
//
//   - For a REAL message: unblockPendingRecvs() only sets
//     p2p_dummy_phase = true after drainInFlightP2p() exits, which
//     requires this rank's local_recv_messages to have caught up with
//     sent.  The MPI_Recv wrapper increments local_recv_messages
//     AFTER its post-call dummy check.  Therefore, for a real message,
//     the wrapper's post-call read happens-before
//     unblockPendingRecvs sets the flag, and reads false.
//
//   - For a DUMMY message: unblockPendingRecvs sets p2p_dummy_phase
//     = true and then participates in dmtcp_global_barrier
//     ("MPI:P2P-Pending-Recv-Published") BEFORE any rank dispatches
//     any dummy.  Therefore by the time any dummy is in flight, every
//     rank has already set its local p2p_dummy_phase to true, and the
//     receiver's post-call read sees true.
extern volatile bool p2p_dummy_phase;

void initialize_drain_send_recv();
void registerLocalSendsAndRecvs();

// Drain all in-flight point-to-point messages by completing nonblocking
// receives and probing for unexpected messages until global_sent ==
// global_recv.
void drainInFlightP2p();

// Dispatch dummy MPI_Send messages to unblock any rank that is parked
// in blocking MPI_Recv at pre-suspend time.  Must be called after
// drainInFlightP2p() returns (i.e., after all real in-flight messages
// have been accounted for).  See implementation for the full protocol.
void unblockPendingRecvs();

// Single entry point for draining all P2P communications before
// checkpoint: drains in-flight messages, then unblocks pending recvs.
void drainP2p();

int drainRemainingP2pMsgs(int source);
int recvMsgIntoInternalBuffer(MPI_Status status);
bool existsMatchingMsgBuffer(int source, int tag, MPI_Comm comm, int *flag,
                             MPI_Status *status);
int consumeMatchingMsgBuffer(void *buf, int count, MPI_Datatype datatype,
                             int source, int tag, MPI_Comm comm,
                             MPI_Status *mpi_status, int size);
void removePendingSendRequests();
void resetDrainCounters();
int localRankToGlobalRank(int localRank, MPI_Comm localComm);
#endif
