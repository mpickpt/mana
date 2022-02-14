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

#ifndef _MANA_COORD_PROTO_
#define _MANA_COORD_PROTO_

// Key-value database containing the counts of sends, receives, and unserviced
// sends for each rank
// Mapping is (rank -> send_recv_totals_t)
#define MPI_SEND_RECV_DB  "SR_DB"

// Key-value database containing the metadata of unserviced sends for each rank
// Mapping is (rank -> mpi_call_params_t)
#define MPI_US_DB         "US_DB"

// Database containing the counts of wrappers (send, isend, recv, irecv)
// executed for each rank (useful while debugging)
// Mapping is (rank -> wr_counts_t)
#define MPI_WRAPPER_DB    "WR_DB"

typedef enum __phase_t
{
  ST_ERROR = -1,
  ST_UNKNOWN, /* State 0 shouldn't be confused with a state used in the algo. */
  IN_TRIVIAL_BARRIER,
  PHASE_1,
  IN_CS,
  IS_READY,
} phase_t;

typedef enum __query_t
{
  NONE = -1,
  Q_UNKNOWN, /* State 0 shouldn't be confused with a state used in the algo. */
  Q_FAILED,
  INTENT,
  FREE_PASS,
  WAIT_STRAGGLER,
  SAFE_TO_CHECKPOINT,
  Q_MAX
} query_t;

// Struct to encapsulate the checkpointing state of a rank
typedef struct __rank_state_t
{
  int rank;       // MPI rank
  MPI_Comm comm;  // MPI communicator object
  phase_t st;     // Checkpointing state of the MPI rank
} rank_state_t;

// Struct to store the number of times send, isend, recv, and irecv wrappers
// were executed
typedef struct __wr_counts
{
  int sendCount;     // Number of times MPI_Send wrapper was called
  int isendCount;    // Number of times MPI_Isend wrapper was called
  int recvCount;     // Number of times MPI_Recv wrapper was called
  int irecvCount;    // Number of times MPI_Irecv wrapper was called
  int sendrecvCount; // Number of times MPI_Sendrecv wrapper was called
} wr_counts_t;

// Struct to store the MPI send/recv counts of a rank
typedef struct __send_recv_totals
{
  int rank;         // MPI rank
  uint64_t sends;   // Number of completed sends
  uint64_t recvs;   // Number of completed receives
  uint64_t sendCounts;  // Number of completed send counts (MPI argument)
  uint64_t recvCounts;  // Number of completed recv counts (MPI argument)
  int countSends;   // Number of unserviced sends
} send_recv_totals_t;

#endif // ifndef _MANA_COORD_PROTO_
