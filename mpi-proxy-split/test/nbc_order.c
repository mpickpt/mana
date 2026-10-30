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

// Non-blocking collectives on two communicators with the same ranks, which
// different ranks start in a different order.  MPI sets the order of
// collectives only on each communicator, so this program is correct.  X is a
// copy of MPI_COMM_WORLD.  Y has the same ranks in reverse order.  Even ranks
// start an MPI_Ibarrier on X, sleep, and then start one on Y.  Odd ranks start
// on Y first.  A checkpoint during the sleep must make each rank start the
// barrier that the other ranks started.

#include "mana_test.h"

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "nbc_order");
  MPI_Comm x, y;
  MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &x));
  MT_MPI(MPI_Comm_split(MPI_COMM_WORLD, 0, mt_size - 1 - mt_rank, &y));
  long it;
  for (it = 0; mt_continue(it); it++) {
    MPI_Comm first = mt_rank % 2 ? y : x, second = mt_rank % 2 ? x : y;
    MPI_Request requests[2];
    MT_MPI(MPI_Ibarrier(first, &requests[0]));
    usleep(2000);
    MT_MPI(MPI_Ibarrier(second, &requests[1]));
    MT_MPI(MPI_Waitall(2, requests, MPI_STATUSES_IGNORE));
  }
  MPI_Comm_free(&y);
  MPI_Comm_free(&x);
  mt_finish(it);
  return 0;
}
