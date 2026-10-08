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

// MPI_Ssend, then MPI_Allreduce.  Pair 0's receiver waits 2 ms and the
// others 200 ms, so a checkpoint usually finds pair 0 in MPI_Allreduce and
// the other senders in MPI_Ssend: the Collective Clock must let them finish
// MPI_Ssend and reach MPI_Allreduce.

#include "mana_test.h"

#define N 32

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_ssend_collective");
  MT_CHECK(mt_size % 2 == 0, "needs an even number of ranks, not %d", mt_size);
  int sender = mt_rank % 2 == 0;
  int partner = sender ? mt_rank + 1 : mt_rank - 1;
  int wait_ms = mt_rank / 2 == 0 ? 2 : 200;
  int out[N], in[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (sender) {
      for (int i = 0; i < N; i++) {
        out[i] = mt_value(mt_rank, it, i);
      }
      MT_MPI(MPI_Ssend(out, N, MPI_INT, partner, 1, MPI_COMM_WORLD));
    } else {
      for (int ms = 0; ms < wait_ms; ms++) {
        usleep(1000);
      }
      MT_MPI(MPI_Recv(in, N, MPI_INT, partner, 1, MPI_COMM_WORLD,
                      MPI_STATUS_IGNORE));
      for (int i = 0; i < N; i++) {
        MT_CHECK(in[i] == mt_value(partner, it, i),
                 "iteration %ld word %d: %d", it, i, in[i]);
      }
    }
    long sum;
    MT_MPI(MPI_Allreduce(&it, &sum, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD));
    MT_CHECK(sum == it * mt_size, "iteration %ld: sum %ld", it, sum);
  }
  mt_finish(it);
  return 0;
}
