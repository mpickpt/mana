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

// Collectives on MPI_COMM_SELF, with a different number on each rank.  In each
// iteration, rank r calls MPI_Barrier(MPI_COMM_SELF) r + 1 times.  Then all
// ranks check the iteration with an MPI_Allreduce on MPI_COMM_WORLD and sleep.
// Each rank has its own clock for MPI_COMM_SELF.  If the drain merged the
// counts of all ranks, the drain would never stop.

#include "mana_test.h"

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "comm_self");
  long it;
  for (it = 0; mt_continue(it); it++) {
    for (int i = 0; i <= mt_rank; i++) {
      MT_MPI(MPI_Barrier(MPI_COMM_SELF));
    }
    long sum = 0;
    MT_MPI(MPI_Allreduce(&it, &sum, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD));
    MT_CHECK(sum == it * mt_size, "iteration %ld: sum %ld", it, sum);
    usleep(1000);
  }
  mt_finish(it);
  return 0;
}
