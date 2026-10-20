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

// An MPI_Ssend to a rank that waits in MPI_Recv for another rank.  Each
// iteration, rank 1 MPI_Ssends to rank 0, then sends a marker; rank 2 sends
// to rank 0 after 100 ms.  Rank 0 receives from rank 2 first (with
// MANA_P2P_WAIT=blocking, a checkpoint sends that MPI_Recv a dummy), then
// from rank 1: it must not see the marker before.

#include "mana_test.h"

#define N 32
#define WAIT_MS 100  // Rank 2's delay; checkpoints find rank 0 waiting

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_ssend_blocked");
  MT_CHECK(mt_size == 3, "needs 3 ranks, not %d", mt_size);
  int buf[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (mt_rank != 0) {
      for (int i = 0; i < N; i++) {
        buf[i] = mt_value(mt_rank, it, i);
      }
      if (mt_rank == 1) {
        MT_MPI(MPI_Ssend(buf, N, MPI_INT, 0, 1, MPI_COMM_WORLD));
        MT_MPI(MPI_Send(&it, 1, MPI_LONG, 0, 3, MPI_COMM_WORLD));
      } else {
        usleep(WAIT_MS * 1000);
        MT_MPI(MPI_Send(buf, N, MPI_INT, 0, 2, MPI_COMM_WORLD));
      }
      continue;
    }
    MT_MPI(MPI_Recv(buf, N, MPI_INT, 2, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    for (int i = 0; i < N; i++) {
      MT_CHECK(buf[i] == mt_value(2, it, i), "iteration %ld from 2 word %d: "
               "%d", it, i, buf[i]);
    }
    int flag;
    MT_MPI(MPI_Iprobe(1, 3, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE));
    MT_CHECK(!flag, "iteration %ld: MPI_Ssend returned before its receive",
             it);
    MT_MPI(MPI_Recv(buf, N, MPI_INT, 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    for (int i = 0; i < N; i++) {
      MT_CHECK(buf[i] == mt_value(1, it, i), "iteration %ld from 1 word %d: "
               "%d", it, i, buf[i]);
    }
    long marker;
    MT_MPI(MPI_Recv(&marker, 1, MPI_LONG, 1, 3, MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE));
    MT_CHECK(marker == it, "iteration %ld: marker %ld", it, marker);
  }
  mt_finish(it);
  return 0;
}
