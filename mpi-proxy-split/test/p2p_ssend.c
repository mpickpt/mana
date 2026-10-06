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

// MPI_Ssend around a ring; with -w, checkpoints find senders in MPI_Ssend.

#include "mana_test.h"

#define N 32

int
main(int argc, char **argv)
{
  int senders_wait = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-w") == 0) {
      senders_wait = 1;
    }
  }
  mt_init(&argc, &argv, senders_wait ? "p2p_ssend_wait" : "p2p_ssend");
  MT_CHECK(mt_size % 2 == 0, "needs an even number of ranks, not %d", mt_size);
  int right = (mt_rank + 1) % mt_size;
  int left = (mt_rank + mt_size - 1) % mt_size;
  int sends_first = mt_rank % 2 == 0;
  int out[N], in[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    for (int i = 0; i < N; i++) {
      out[i] = mt_value(mt_rank, it, i);
    }
    if (sends_first != senders_wait) {
      usleep(2000);  // Without -w, receivers wait; with -w, senders do
    }
    MT_MPI(MPI_Ssend(out, N, MPI_INT, MPI_PROC_NULL, 1, MPI_COMM_WORLD));
    if (sends_first) {
      MT_MPI(MPI_Ssend(out, N, MPI_INT, right, 1, MPI_COMM_WORLD));
    }
    MT_MPI(MPI_Recv(in, N, MPI_INT, left, 1, MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE));
    if (!sends_first) {
      MT_MPI(MPI_Ssend(out, N, MPI_INT, right, 1, MPI_COMM_WORLD));
    }
    for (int i = 0; i < N; i++) {
      MT_CHECK(in[i] == mt_value(left, it, i), "iteration %ld word %d: %d",
               it, i, in[i]);
    }
  }
  mt_finish(it);
  return 0;
}
