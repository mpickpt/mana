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

// The application thread and, during a checkpoint, the drain (the checkpoint
// thread) both count received messages.  Release: MPI_Recv's store to
// g_pending_recv.state must be visible before its count.
static inline void
count_received_message()
{
  __atomic_fetch_add(&local_recv_messages, 1, __ATOMIC_RELEASE);
}
extern std::unordered_set<MPI_Comm> active_comms;
extern dmtcp::vector<mpi_message_t*> g_message_queue;

// State of the single pending blocking MPI_Recv.  MANA does not support
// MPI_THREAD_MULTIPLE; supporting it would need one slot per thread.
//
// 'state' is changed by the MPI_Recv wrapper and by the P2P drain
// (checkpoint thread, pre-suspend):
//   IDLE:   no MPI_Recv is in the lower half.
//   ACTIVE: an MPI_Recv is in, or entering, the lower half; the fields
//           below describe it.
//   CLOSED: set when the P2P drain begins or after a dummy.  No MPI_Recv may
//           enter the lower half until resetDrainCounters() sets IDLE.
// Both threads leave IDLE by compare-and-swap, so exactly one wins: the
// MPI_Recv enters the lower half and gets a dummy, or it waits in the upper
// half until the checkpoint is over.
enum { PENDING_RECV_IDLE, PENDING_RECV_ACTIVE, PENDING_RECV_CLOSED };

typedef struct {
  int state;
  // The following fields are valid only when state is PENDING_RECV_ACTIVE.
  int source;     // user-provided value; may be MPI_ANY_SOURCE
  int tag;        // user-provided value; may be MPI_ANY_TAG
  MPI_Comm comm;  // virtual communicator
  int count;      // user-provided count (needed for dummy buffer size)
  MPI_Datatype datatype;  // virtual datatype handle (for the size of the dummy)
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

// How MPI_Send, MPI_Rsend and MPI_Recv wait (MANA_P2P_WAIT, read at MPI_Init
// and kept after restart).  POLLING (default): MPI_Isend/MPI_Irecv, then
// MANA's MPI_Wait (an MPI_Test loop); no thread blocks in the lower half.
// BLOCKING: in the lower half; a blocked MPI_Recv gets a dummy at checkpoint.
enum p2p_wait_t { P2P_WAIT_BLOCKING, P2P_WAIT_POLLING };
extern p2p_wait_t g_p2p_wait;

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
// checkpoint: drains in-flight messages, then unblocks pending recvs.  If
// drainHasNoBarrier(), it also completes the pending non-blocking
// collectives, and it uses no barrier (see drainWithoutBarriers()).
void drainP2p();

// True with MANA_P2P_WAIT=polling, and with blocking if the lower half has a
// TLS for the checkpoint thread (MPI_THREAD_MULTIPLE).
bool drainHasNoBarrier();

// What the drain did on this rank at one checkpoint (times in
// microseconds); see reportDrainStats().
struct DrainStats {
  uint64_t t_collective;     // Collective Clock drain, NBCs, barrier
  uint64_t t_inflight;       // drainInFlightP2p()
  uint64_t t_register;       //   exchanging the send/recv counters
  uint64_t t_complete;       //   completing pending MPI_Isend/MPI_Irecv
  uint64_t t_probe;          //   probing communicators, buffering messages
  uint64_t t_isends;         //   completing the remaining MPI_Isends
  uint64_t t_done;           //   polling: waiting until all ranks are done
  uint64_t t_unblock;        // unblockPendingRecvs()
  uint64_t t_publish;        //   publishing whether blocked in MPI_Recv
  uint64_t t_published;      //   barrier after publishing
  uint64_t t_post;           //   posting the dummy to its sender
  uint64_t t_posted;         //   barrier after posting
  uint64_t t_dispatch;       //   sending the dummies posted to this rank
  uint64_t t_dispatched;     //   barrier after sending
  uint64_t t_wait_lower_half;  // wait_for_threads_to_leave_lower_half()
  int64_t iterations;        // rounds of the in-flight drain
  int64_t comms_probed;
  int64_t iprobes;
  int64_t drained_msgs;      // moved to MANA's buffer
  int64_t drained_bytes;
  int64_t irecvs_completed;  // pending MPI_Irecvs that received a message
  int64_t isends_completed;
  int64_t blocked;           // ranks blocked in MPI_Recv
  int64_t dummies;           // dummy messages sent
  int64_t kvdb_requests;     // requests to the coordinator's database
  int64_t done_polls;        // polling: polls of the "done" counter
  int64_t barriers;          // global barriers
};
extern DrainStats g_drain_stats;
uint64_t drainStatsNow();   // microseconds
void resetDrainStats();
void reportDrainStats();

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
