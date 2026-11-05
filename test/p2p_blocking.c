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

// Blocking point-to-point in a ring.  Each rank sends K eager messages to
// its right neighbour, then receives K from its left one.  One rank per
// iteration waits before sending, so the others are blocked in MPI_Recv and
// messages are in flight when a checkpoint comes.

#include "mana_test.h"

#define K 4
#define N 64

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_blocking");
  int right = (mt_rank + 1) % mt_size;
  int left = (mt_rank + mt_size - 1) % mt_size;
  int buf[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (it % mt_size == mt_rank) {
      usleep(2000);
    }
    for (int k = 0; k < K; k++) {
      for (int i = 0; i < N; i++) {
        buf[i] = mt_value(mt_rank, it, k * N + i);
      }
      MT_MPI(MPI_Send(buf, N, MPI_INT, right, k, MPI_COMM_WORLD));
    }
    for (int k = 0; k < K; k++) {
      MPI_Status status;
      // Message 0 is received with MPI_STATUS_IGNORE, the others with a
      // status that must describe them.
      MT_MPI(MPI_Recv(buf, N, MPI_INT, left, k, MPI_COMM_WORLD,
                      k == 0 ? MPI_STATUS_IGNORE : &status));
      if (k > 0) {
        int count;
        MPI_Get_count(&status, MPI_INT, &count);
        MT_CHECK(status.MPI_SOURCE == left && status.MPI_TAG == k &&
                 count == N, "status: source %d tag %d count %d",
                 status.MPI_SOURCE, status.MPI_TAG, count);
      }
      for (int i = 0; i < N; i++) {
        MT_CHECK(buf[i] == mt_value(left, it, k * N + i),
                 "iteration %ld message %d word %d: %d", it, k, i, buf[i]);
      }
    }
  }
  mt_finish(it);
  return 0;
}
