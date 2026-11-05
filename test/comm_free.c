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

// Communicators that their members free at different times.  Each iteration
// makes a duplicate of MPI_COMM_WORLD and a split of the ranks by parity,
// checks both, and frees them.  Rank r frees each one r * DELAY us after rank
// 0, and the ranks spend most of the time asleep.  Thus a checkpoint can come
// between the frees of two ranks.  After a restart, every rank must have the
// same communicators as the other members.

#include "mana_test.h"

#define DELAY 200

static long it;

// Checks that 'comm' has 'n' ranks, with the world rank 'first' as rank 0
// and the others in steps of 'step', with an MPI_Allreduce.
static void
use_comm(const char *name, MPI_Comm comm, int first, int step, int n)
{
  int size, rank;
  MT_MPI(MPI_Comm_size(comm, &size));
  MT_MPI(MPI_Comm_rank(comm, &rank));
  MT_CHECK(size == n && first + rank * step == mt_rank,
           "iteration %ld %s: rank %d of %d", it, name, rank, size);
  long mine = mt_value(mt_rank, it, 0), sum, want = 0;
  for (int i = 0; i < n; i++) {
    want += mt_value(first + i * step, it, 0);
  }
  MT_MPI(MPI_Allreduce(&mine, &sum, 1, MPI_LONG, MPI_SUM, comm));
  MT_CHECK(sum == want, "iteration %ld %s: sum %ld, not %ld", it, name, sum,
           want);
}

// Frees 'comm' after mt_rank * DELAY us.  The rank then sleeps for the rest
// of (mt_size - 1) * DELAY us, so that all ranks sleep equally long here.
static void
free_late(MPI_Comm *comm)
{
  usleep(DELAY * mt_rank);
  MT_MPI(MPI_Comm_free(comm));
  MT_CHECK(*comm == MPI_COMM_NULL, "iteration %ld: MPI_Comm_free left the "
           "handle", it);
  usleep(DELAY * (mt_size - 1 - mt_rank));
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "comm_free");
  int parity = mt_rank % 2;
  int nparity = (mt_size - parity + 1) / 2;
  for (it = 0; mt_continue(it); it++) {
    MPI_Comm dup, split;
    MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &dup));
    MT_MPI(MPI_Comm_split(MPI_COMM_WORLD, parity, mt_rank, &split));
    use_comm("dup", dup, 0, 1, mt_size);
    use_comm("split", split, parity, 2, nparity);
    free_late(&dup);
    free_late(&split);
  }
  mt_finish(it);
  return 0;
}
