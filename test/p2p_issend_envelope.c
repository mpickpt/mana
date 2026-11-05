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

// MPI_Issend between MPI_Isends with the same envelope.  Each iteration, the
// sender posts messages 0 (MPI_Isend), 1 (MPI_Issend) and 2 (MPI_Isend), all
// with tag 1, waits for the MPI_Issend, and sends a marker.  The receiver
// must not see the marker before it receives message 1, and it receives
// message 2 only after the marker: an ack on message 0 makes the MPI_Issend
// complete early, and one on message 2 never.

#include "mana_test.h"

#define N 32
#define K 3          // Messages; message 1 is the MPI_Issend's
#define WAIT_MS 50   // Before receiving messages 0 and 1

static void
wait_without_marker(int partner, long it, int k)
{
  for (int ms = 0; ms < WAIT_MS; ms++) {
    int flag;
    MT_MPI(MPI_Iprobe(partner, 2, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE));
    MT_CHECK(!flag, "iteration %ld: MPI_Issend completed before message "
             "%d's receive", it, k);
    usleep(1000);
  }
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_issend_envelope");
  MT_CHECK(mt_size % 2 == 0, "needs an even number of ranks, not %d", mt_size);
  int sender = mt_rank % 2 == 0;
  int partner = sender ? mt_rank + 1 : mt_rank - 1;
  int buf[K][N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (sender) {
      MPI_Request requests[K];
      for (int k = 0; k < K; k++) {
        for (int i = 0; i < N; i++) {
          buf[k][i] = mt_value(mt_rank, it, k * N + i);
        }
        if (k == 1) {
          MT_MPI(MPI_Issend(buf[k], N, MPI_INT, partner, 1, MPI_COMM_WORLD,
                            &requests[k]));
        } else {
          MT_MPI(MPI_Isend(buf[k], N, MPI_INT, partner, 1, MPI_COMM_WORLD,
                           &requests[k]));
        }
      }
      MT_MPI(MPI_Wait(&requests[1], MPI_STATUS_IGNORE));
      MT_MPI(MPI_Send(&it, 1, MPI_LONG, partner, 2, MPI_COMM_WORLD));
      MT_MPI(MPI_Waitall(K, requests, MPI_STATUSES_IGNORE));
      continue;
    }
    memset(buf, 0, sizeof(buf));
    for (int k = 0; k < 2; k++) {
      wait_without_marker(partner, it, k);
      MT_MPI(MPI_Recv(buf[k], N, MPI_INT, partner, 1, MPI_COMM_WORLD,
                      MPI_STATUS_IGNORE));
    }
    long marker;
    MT_MPI(MPI_Recv(&marker, 1, MPI_LONG, partner, 2, MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE));
    MT_CHECK(marker == it, "iteration %ld: marker %ld", it, marker);
    MT_MPI(MPI_Recv(buf[2], N, MPI_INT, partner, 1, MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE));
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
