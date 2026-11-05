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

// An MPI_Ssend after MPI_Sends with the same envelope.  Each iteration, the
// sender MPI_Sends messages 0 and 1, MPI_Ssends message 2, all with tag 1,
// then sends a marker.  The receiver must not see the marker before it posts
// the receive of message 2.  It receives the messages with MPI_Recv, with
// MPI_Irecv and MPI_Wait, or (every third iteration) with MPI_Irecvs posted
// before it waits; then the marker may come at any time.

#include "mana_test.h"

#define N 32
#define K 3          // Messages; the last one is the MPI_Ssend's
#define WAIT_MS 40   // Before each receive

static void
wait_without_marker(int partner, long it, int k)
{
  for (int ms = 0; ms < WAIT_MS; ms++) {
    int flag;
    MT_MPI(MPI_Iprobe(partner, 2, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE));
    MT_CHECK(!flag, "iteration %ld: MPI_Ssend returned before message %d's "
             "receive", it, k);
    usleep(1000);
  }
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_ssend_order");
  MT_CHECK(mt_size % 2 == 0, "needs an even number of ranks, not %d", mt_size);
  int sender = mt_rank % 2 == 0;
  int partner = sender ? mt_rank + 1 : mt_rank - 1;
  int buf[K][N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (sender) {
      for (int k = 0; k < K; k++) {
        for (int i = 0; i < N; i++) {
          buf[k][i] = mt_value(mt_rank, it, k * N + i);
        }
        if (k < K - 1) {
          MT_MPI(MPI_Send(buf[k], N, MPI_INT, partner, 1, MPI_COMM_WORLD));
        } else {
          MT_MPI(MPI_Ssend(buf[k], N, MPI_INT, partner, 1, MPI_COMM_WORLD));
        }
      }
      MT_MPI(MPI_Send(&it, 1, MPI_LONG, partner, 2, MPI_COMM_WORLD));
      continue;
    }
    memset(buf, 0, sizeof(buf));
    int how = (int)(it % 3);
    if (how == 2) {
      MPI_Request requests[K];
      for (int k = 0; k < K; k++) {
        MT_MPI(MPI_Irecv(buf[k], N, MPI_INT, partner, 1, MPI_COMM_WORLD,
                         &requests[k]));
      }
      for (int ms = 0; ms < K * WAIT_MS; ms++) {
        usleep(1000);
      }
      MT_MPI(MPI_Waitall(K, requests, MPI_STATUSES_IGNORE));
    } else {
      for (int k = 0; k < K; k++) {
        wait_without_marker(partner, it, k);
        if (how == 0) {
          MT_MPI(MPI_Recv(buf[k], N, MPI_INT, partner, 1, MPI_COMM_WORLD,
                          MPI_STATUS_IGNORE));
        } else {
          MPI_Request request;
          MT_MPI(MPI_Irecv(buf[k], N, MPI_INT, partner, 1, MPI_COMM_WORLD,
                           &request));
          MT_MPI(MPI_Wait(&request, MPI_STATUS_IGNORE));
        }
      }
    }
    long marker;
    MT_MPI(MPI_Recv(&marker, 1, MPI_LONG, partner, 2, MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE));
    MT_CHECK(marker == it, "iteration %ld: marker %ld", it, marker);
    for (int k = 0; k < K; k++) {
      for (int i = 0; i < N; i++) {
        MT_CHECK(buf[k][i] == mt_value(partner, it, k * N + i),
                 "iteration %ld message %d word %d: %d", it, k, i,
                 buf[k][i]);
      }
    }
  }
  mt_finish(it);
  return 0;
}
