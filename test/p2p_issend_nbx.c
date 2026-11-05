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

// The NBX algorithm (MPI_Issend, MPI_Iprobe with MPI_ANY_SOURCE, then
// MPI_Ibarrier once the sends complete), as in PETSc's
// PetscCommBuildTwoSided().  Each iteration, every rank sends a message to
// each of its targets, which change with the iteration, and receives until
// the MPI_Ibarrier completes.  A completed MPI_Issend means that its
// message was received, so each rank must have received one message from
// every rank that targets it.  A rank tests its requests every ms but probes
// only every POLL_MS, so checkpoints find messages unreceived; if the drain
// completes their MPI_Issends, the MPI_Ibarrier can complete before the
// next probe.

#include "mana_test.h"

#define N 16
#define TARGETS 2
#define TAG 5  // TAG + 1 in odd iterations: a rank may start the next one
#define POLL_MS 20

static int
target(int rank, long it, int k)
{
  int d = (int)((k == 0 ? it + rank : 3 * it + 2 * rank + 1) %
                (mt_size - 1));
  return (rank + 1 + d) % mt_size;
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_issend_nbx");
  MT_CHECK(mt_size >= 2, "needs 2 ranks or more, not %d", mt_size);
  int out[TARGETS][N], in[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    int tag = TAG + (int)(it % 2);
    int expected = 0;
    for (int rank = 0; rank < mt_size; rank++) {
      for (int k = 0; k < TARGETS; k++) {
        expected += target(rank, it, k) == mt_rank;
      }
    }
    MPI_Request requests[TARGETS];
    for (int k = 0; k < TARGETS; k++) {
      for (int i = 0; i < N; i++) {
        out[k][i] = mt_value(mt_rank, it, k * N + i);
      }
      MT_MPI(MPI_Issend(out[k], N, MPI_INT, target(mt_rank, it, k), tag,
                        MPI_COMM_WORLD, &requests[k]));
    }
    int received = 0, in_barrier = 0, done = 0, passes = 0;
    MPI_Request barrier;
    while (!done) {
      int flag = 0;
      MPI_Status status;
      if (++passes % POLL_MS == 0) {
        MT_MPI(MPI_Iprobe(MPI_ANY_SOURCE, tag, MPI_COMM_WORLD, &flag,
                          &status));
      }
      if (flag) {
        int source = status.MPI_SOURCE;
        MT_MPI(MPI_Recv(in, N, MPI_INT, source, tag, MPI_COMM_WORLD,
                        MPI_STATUS_IGNORE));
        int k = in[0] == mt_value(source, it, 0) ? 0 : 1;
        for (int i = 0; i < N; i++) {
          MT_CHECK(in[i] == mt_value(source, it, k * N + i),
                   "iteration %ld: from rank %d, word %d: %d", it, source, i,
                   in[i]);
        }
        received++;
      } else {
        usleep(1000);
      }
      if (in_barrier) {
        MT_MPI(MPI_Test(&barrier, &done, MPI_STATUS_IGNORE));
      } else {
        int sent;
        MT_MPI(MPI_Testall(TARGETS, requests, &sent, MPI_STATUSES_IGNORE));
        if (sent) {
          MT_MPI(MPI_Ibarrier(MPI_COMM_WORLD, &barrier));
          in_barrier = 1;
        }
      }
    }
    MT_CHECK(received == expected, "iteration %ld: received %d messages, "
             "not %d", it, received, expected);
  }
  mt_finish(it);
  return 0;
}
