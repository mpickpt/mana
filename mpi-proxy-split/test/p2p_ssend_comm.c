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

// As p2p_ssend_sync, in a communicator whose ranks are in the reverse order
// of MPI_COMM_WORLD's: a rank's rank in it is not its world rank.

#include "mana_test.h"

#define N 32
#define WAIT_MS 100  // The receiver waits; checkpoints find the sender waiting

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_ssend_comm");
  MT_CHECK(mt_size % 2 == 0, "needs an even number of ranks, not %d", mt_size);
  MPI_Comm comm;
  MT_MPI(MPI_Comm_split(MPI_COMM_WORLD, 0, mt_size - 1 - mt_rank, &comm));
  int rank;
  MT_MPI(MPI_Comm_rank(comm, &rank));
  MT_CHECK(rank == mt_size - 1 - mt_rank, "rank %d in the reversed comm",
           rank);
  int sender = rank % 2 == 0;
  int partner = sender ? rank + 1 : rank - 1;
  int out[N], in[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (sender) {
      for (int i = 0; i < N; i++) {
        out[i] = mt_value(rank, it, i);
      }
      MT_MPI(MPI_Ssend(out, N, MPI_INT, partner, 1, comm));
      MT_MPI(MPI_Send(&it, 1, MPI_LONG, partner, 2, comm));
      continue;
    }
    for (int ms = 0; ms < WAIT_MS; ms++) {
      int flag;
      MT_MPI(MPI_Iprobe(partner, 2, comm, &flag, MPI_STATUS_IGNORE));
      MT_CHECK(!flag, "iteration %ld: MPI_Ssend returned before its receive",
               it);
      usleep(1000);
    }
    MT_MPI(MPI_Recv(in, N, MPI_INT, partner, 1, comm, MPI_STATUS_IGNORE));
    long marker;
    MT_MPI(MPI_Recv(&marker, 1, MPI_LONG, partner, 2, comm,
                    MPI_STATUS_IGNORE));
    MT_CHECK(marker == it, "iteration %ld: marker %ld", it, marker);
    for (int i = 0; i < N; i++) {
      MT_CHECK(in[i] == mt_value(partner, it, i), "iteration %ld word %d: %d",
               it, i, in[i]);
    }
  }
  MT_MPI(MPI_Comm_free(&comm));
  mt_finish(it);
  return 0;
}
