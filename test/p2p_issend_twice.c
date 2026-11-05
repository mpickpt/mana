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

// Three MPI_Issends: messages 0 and 1 with one envelope (tag 1), message 2
// with tag 3, which the receiver receives first.  The sender completes them
// with MPI_Testany and sends a marker with the index of each; the receiver
// checks that each marker comes after the receive it reports.  With -w
// (p2p_issend_waitall), the sender completes them with MPI_Waitall and sends
// one marker, which must come after the last receive.

#include "mana_test.h"

#define N 32
#define K 3          // Messages
#define WAIT_MS 30   // Before each receive

static const int order[K] = {2, 0, 1};  // The receiver's
static const int tags[K] = {1, 1, 3};

static void
wait_without_marker(int partner, long it, int k)
{
  for (int ms = 0; ms < WAIT_MS; ms++) {
    int flag;
    MT_MPI(MPI_Iprobe(partner, 2, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE));
    MT_CHECK(!flag, "iteration %ld: an MPI_Issend completed before message "
             "%d's receive", it, k);
    usleep(1000);
  }
}

int
main(int argc, char **argv)
{
  int waitall = argc > 1 && strcmp(argv[1], "-w") == 0;
  mt_init(&argc, &argv, waitall ? "p2p_issend_waitall" : "p2p_issend_twice");
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
        MT_MPI(MPI_Issend(buf[k], N, MPI_INT, partner, tags[k],
                          MPI_COMM_WORLD, &requests[k]));
      }
      if (waitall) {
        MT_MPI(MPI_Waitall(K, requests, MPI_STATUSES_IGNORE));
        int done = -1;
        MT_MPI(MPI_Send(&done, 1, MPI_INT, partner, 2, MPI_COMM_WORLD));
        continue;
      }
      for (int completed = 0; completed < K;) {
        int index, flag;
        MT_MPI(MPI_Testany(K, requests, &index, &flag, MPI_STATUS_IGNORE));
        if (flag && index != MPI_UNDEFINED) {
          MT_MPI(MPI_Send(&index, 1, MPI_INT, partner, 2, MPI_COMM_WORLD));
          completed++;
        } else {
          usleep(100);
        }
      }
      continue;
    }
    memset(buf, 0, sizeof(buf));
    for (int j = 0; j < K; j++) {
      int k = order[j];
      wait_without_marker(partner, it, k);
      MT_MPI(MPI_Recv(buf[k], N, MPI_INT, partner, tags[k], MPI_COMM_WORLD,
                      MPI_STATUS_IGNORE));
      if (!waitall || j == K - 1) {
        int index;
        MT_MPI(MPI_Recv(&index, 1, MPI_INT, partner, 2, MPI_COMM_WORLD,
                        MPI_STATUS_IGNORE));
        MT_CHECK(index == (waitall ? -1 : k), "iteration %ld: marker %d "
                 "after message %d's receive", it, index, k);
      }
    }
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
