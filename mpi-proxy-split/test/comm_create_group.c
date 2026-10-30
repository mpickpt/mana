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

// MPI_Comm_create_group on a part of MPI_COMM_WORLD.  Only the members of the
// group call it.  So MANA must not count the call on the clock of
// MPI_COMM_WORLD.  Otherwise the counts of that clock would differ between the
// members and the other ranks, and the drain would never stop.  In each
// iteration, the even ranks make a communicator of the even ranks.  They check
// it with an MPI_Allreduce and free it.  Then all ranks check the iteration
// with an MPI_Allreduce on MPI_COMM_WORLD and sleep.

#include "mana_test.h"

#define MAXRANKS 64

static long it;

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "comm_create_group");
  MT_CHECK(mt_size <= MAXRANKS, "more than %d ranks", MAXRANKS);
  int even[MAXRANKS], n = 0;
  for (int r = 0; r < mt_size; r += 2) {
    even[n++] = r;
  }
  MPI_Group world_group, even_group;
  MT_MPI(MPI_Comm_group(MPI_COMM_WORLD, &world_group));
  MT_MPI(MPI_Group_incl(world_group, n, even, &even_group));

  for (it = 0; mt_continue(it); it++) {
    if (mt_rank % 2 == 0) {
      MPI_Comm comm;
      int size, rank;
      MT_MPI(MPI_Comm_create_group(MPI_COMM_WORLD, even_group, 7, &comm));
      MT_MPI(MPI_Comm_size(comm, &size));
      MT_MPI(MPI_Comm_rank(comm, &rank));
      MT_CHECK(size == n && rank == mt_rank / 2,
               "iteration %ld: rank %d of %d", it, rank, size);
      long mine = mt_value(mt_rank, it, 0), sum, want = 0;
      for (int i = 0; i < n; i++) {
        want += mt_value(even[i], it, 0);
      }
      MT_MPI(MPI_Allreduce(&mine, &sum, 1, MPI_LONG, MPI_SUM, comm));
      MT_CHECK(sum == want, "iteration %ld: sum %ld, not %ld", it, sum, want);
      MT_MPI(MPI_Comm_free(&comm));
    }
    long sum = 0;
    MT_MPI(MPI_Allreduce(&it, &sum, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD));
    MT_CHECK(sum == it * mt_size, "iteration %ld: world sum %ld", it, sum);
    usleep(1000);
  }
  MPI_Group_free(&even_group);
  MPI_Group_free(&world_group);
  mt_finish(it);
  return 0;
}
