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

// An MPI_Issend and its MPI_Irecv on a copy of MPI_COMM_WORLD, which both
// ranks free before they wait for the requests.  MPI completes the requests
// after the free.  Even ranks send to the next odd rank.  A checkpoint
// usually comes while the requests are pending.

#include "mana_test.h"

#define N 1024
#define SLEEP_US 20000  // Between the free and the wait

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_issend_comm_free");
  MT_CHECK(mt_size % 2 == 0, "needs an even number of ranks, not %d", mt_size);
  int sender = mt_rank % 2 == 0;
  int partner = sender ? mt_rank + 1 : mt_rank - 1;
  int buf[N];
  long it;
  for (it = 0; mt_continue(it); it++) {
    MPI_Comm c;
    MPI_Request request;
    MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &c));
    if (sender) {
      for (int i = 0; i < N; i++) {
        buf[i] = mt_value(mt_rank, it, i);
      }
      MT_MPI(MPI_Issend(buf, N, MPI_INT, partner, 0, c, &request));
    } else {
      MT_MPI(MPI_Irecv(buf, N, MPI_INT, partner, 0, c, &request));
    }
    MT_MPI(MPI_Comm_free(&c));
    usleep(SLEEP_US);
    MT_MPI(MPI_Wait(&request, MPI_STATUS_IGNORE));
    if (!sender) {
      for (int i = 0; i < N; i++) {
        MT_CHECK(buf[i] == mt_value(partner, it, i),
                 "iteration %ld: buf[%d] = %d", it, i, buf[i]);
      }
    }
  }
  mt_finish(it);
  return 0;
}
