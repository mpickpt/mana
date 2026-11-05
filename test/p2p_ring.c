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

// One message per iteration around a ring: each rank sends to its right
// neighbour, then receives from its left one; one rank per iteration waits
// before receiving.  Consecutive messages between two ranks have the same
// envelope, so a receive that takes a later message ahead of an earlier
// one (e.g. one that a checkpoint moved into MANA's buffer) is caught.

#include "mana_test.h"

#define N 32

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_ring");
  int right = (mt_rank + 1) % mt_size;
  int left = (mt_rank + mt_size - 1) % mt_size;
  int out[N], in[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    MPI_Status status;
    for (int i = 0; i < N; i++) {
      out[i] = mt_value(mt_rank, it, i);
    }
    MT_MPI(MPI_Send(out, N, MPI_INT, right, 1, MPI_COMM_WORLD));
    if (it % mt_size == mt_rank) {
      usleep(2000);
    }
    MT_MPI(MPI_Recv(in, N, MPI_INT, left, 1, MPI_COMM_WORLD, &status));
    for (int i = 0; i < N; i++) {
      MT_CHECK(in[i] == mt_value(left, it, i), "iteration %ld word %d: %d",
               it, i, in[i]);
    }
  }
  mt_finish(it);
  return 0;
}
