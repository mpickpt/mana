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

// Point-to-point with MPI_PROC_NULL.  Each iteration, real messages go around
// a ring (some are in flight when a checkpoint comes), and every rank sends
// to MPI_PROC_NULL (MPI_Send, MPI_Isend); every fourth iteration it also
// receives from MPI_PROC_NULL (MPI_Recv, MPI_Irecv, MPI_Sendrecv).  The
// checkpoint's drain must neither wait for the messages to MPI_PROC_NULL nor
// count the ones from it.

#include "mana_test.h"

#define N 32

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_proc_null");
  int right = (mt_rank + 1) % mt_size;
  int left = (mt_rank + mt_size - 1) % mt_size;
  int out[N], in[N], none[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    MPI_Request req;
    MPI_Status status;
    for (int i = 0; i < N; i++) {
      out[i] = mt_value(mt_rank, it, i);
    }
    MT_MPI(MPI_Send(out, N, MPI_INT, right, 1, MPI_COMM_WORLD));
    MT_MPI(MPI_Send(out, N, MPI_INT, MPI_PROC_NULL, 1, MPI_COMM_WORLD));
    MT_MPI(MPI_Isend(out, N, MPI_INT, MPI_PROC_NULL, 2, MPI_COMM_WORLD, &req));
    MT_MPI(MPI_Wait(&req, MPI_STATUS_IGNORE));
    if (it % 4 == 0) {
      MT_MPI(MPI_Irecv(none, N, MPI_INT, MPI_PROC_NULL, 3, MPI_COMM_WORLD,
                       &req));
      MT_MPI(MPI_Wait(&req, MPI_STATUS_IGNORE));
      MT_MPI(MPI_Recv(none, N, MPI_INT, MPI_PROC_NULL, 4, MPI_COMM_WORLD,
                      MPI_STATUS_IGNORE));
      MT_MPI(MPI_Sendrecv(out, N, MPI_INT, MPI_PROC_NULL, 5, none, N,
                          MPI_INT, MPI_PROC_NULL, 5, MPI_COMM_WORLD,
                          MPI_STATUS_IGNORE));
    }
    if (it % mt_size == mt_rank) {
      usleep(2000);  // The others wait for this rank's message
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
