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

// Communicators and groups, which MANA rebuilds at restart.  Communicators
// made once (MPI_Comm_split, MPI_Comm_dup, MPI_Comm_create, MPI_Cart_create,
// MPI_Cart_sub) are used in every iteration, and each iteration also makes
// and frees a temporary one and works with its group.  Every communicator
// must keep its members, in the same rank order.  The Cartesian topology is
// tested in cartesian.c.

#include "mana_test.h"

#define N 8
#define MAXP 64

static long it;

static int
index_of(const int *members, int n, int rank)
{
  for (int i = 0; i < n; i++) {
    if (members[i] == rank) {
      return i;
    }
  }
  return MPI_UNDEFINED;
}

// Checks that 'comm' has the world ranks 'members' in rank order, and runs
// an MPI_Allreduce and an MPI_Bcast whose results depend on them.
static void
use_comm(const char *name, int id, MPI_Comm comm, const int *members, int n)
{
  int me = index_of(members, n, mt_rank);
  int size, rank;
  MT_MPI(MPI_Comm_size(comm, &size));
  MT_MPI(MPI_Comm_rank(comm, &rank));
  MT_CHECK(size == n && rank == me, "iteration %ld %s: rank %d of %d, "
           "not %d of %d", it, name, rank, size, me, n);

  // Weighted by the rank, so that the sum also depends on the rank order.
  long mine = (me + 1L) * mt_value(mt_rank, it, id), sum, want = 0;
  for (int i = 0; i < n; i++) {
    want += (i + 1L) * mt_value(members[i], it, id);
  }
  MT_MPI(MPI_Allreduce(&mine, &sum, 1, MPI_LONG, MPI_SUM, comm));
  MT_CHECK(sum == want, "iteration %ld %s allreduce: %ld, not %ld", it, name,
           sum, want);

  int root = (int)((it + id) % n);
  int buf[N];
  for (int i = 0; i < N; i++) {
    buf[i] = me == root ? mt_value(mt_rank, it, 100 * id + i) : -1;
  }
  MT_MPI(MPI_Bcast(buf, N, MPI_INT, root, comm));
  for (int i = 0; i < N; i++) {
    MT_CHECK(buf[i] == mt_value(members[root], it, 100 * id + i),
             "iteration %ld %s bcast from %d word %d: %d", it, name, root, i,
             buf[i]);
  }
}

static void
use_group(MPI_Comm comm, const int *members, int n, MPI_Group world_group)
{
  MPI_Group group, same, reversed;
  int ranks[MAXP], out[MAXP], size, rank, result;
  MT_MPI(MPI_Comm_group(comm, &group));
  MT_MPI(MPI_Group_size(group, &size));
  MT_MPI(MPI_Group_rank(group, &rank));
  MT_CHECK(size == n && rank == index_of(members, n, mt_rank),
           "iteration %ld group: rank %d of %d", it, rank, size);

  for (int i = 0; i < n; i++) {
    ranks[i] = i;
  }
  MT_MPI(MPI_Group_translate_ranks(group, n, ranks, world_group, out));
  for (int i = 0; i < n; i++) {
    MT_CHECK(out[i] == members[i], "iteration %ld: rank %d is %d in world, "
             "not %d", it, i, out[i], members[i]);
  }
  for (int r = 0; r < mt_size; r++) {
    ranks[r] = r;
  }
  MT_MPI(MPI_Group_translate_ranks(world_group, mt_size, ranks, group, out));
  for (int r = 0; r < mt_size; r++) {
    MT_CHECK(out[r] == index_of(members, n, r), "iteration %ld: world rank "
             "%d is %d in the group", it, r, out[r]);
  }

  MT_MPI(MPI_Group_incl(world_group, n, members, &same));
  MT_MPI(MPI_Group_compare(group, same, &result));
  MT_CHECK(result == MPI_IDENT, "iteration %ld: compare %d", it, result);
  for (int i = 0; i < n; i++) {
    ranks[i] = members[n - 1 - i];
  }
  MT_MPI(MPI_Group_incl(world_group, n, ranks, &reversed));
  MT_MPI(MPI_Group_compare(group, reversed, &result));
  MT_CHECK(result == (n > 1 ? MPI_SIMILAR : MPI_IDENT),
           "iteration %ld: compare reversed %d", it, result);
  MT_MPI(MPI_Group_compare(group, world_group, &result));
  MT_CHECK(n == mt_size || result == MPI_UNEQUAL,
           "iteration %ld: compare world %d", it, result);
  MT_MPI(MPI_Group_free(&reversed));
  MT_MPI(MPI_Group_free(&same));
  MT_MPI(MPI_Group_free(&group));
}

static void
compare(const char *what, MPI_Comm a, MPI_Comm b, int want)
{
  int result;
  MT_MPI(MPI_Comm_compare(a, b, &result));
  MT_CHECK(result == want, "iteration %ld: compare %s: %d, not %d", it, what,
           result, want);
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "communicators");
  MT_CHECK(mt_size <= MAXP, "%d ranks, at most %d", mt_size, MAXP);
  int world[MAXP], parity[MAXP], created[MAXP], row[MAXP];
  int nparity = 0, ncreated = 0;
  for (int r = 0; r < mt_size; r++) {
    world[r] = r;
    if (r % 2 == mt_rank % 2) {
      parity[nparity++] = r;
    }
  }
  // MPI_Comm_create: all ranks but rank 1, in reverse order.
  for (int r = mt_size - 1; r >= 0; r--) {
    if (r != 1) {
      created[ncreated++] = r;
    }
  }

  MPI_Comm parity_comm, dup_comm, created_comm, cart_comm, row_comm;
  MPI_Group world_group, created_group;
  MT_MPI(MPI_Comm_split(MPI_COMM_WORLD, mt_rank % 2, mt_rank, &parity_comm));
  MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &dup_comm));
  MT_MPI(MPI_Comm_group(MPI_COMM_WORLD, &world_group));
  MT_MPI(MPI_Group_incl(world_group, ncreated, created, &created_group));
  MT_MPI(MPI_Comm_create(MPI_COMM_WORLD, created_group, &created_comm));
  MT_CHECK((created_comm == MPI_COMM_NULL) == (mt_rank == 1),
           "MPI_Comm_create: %s", "wrong members");
  int dims[2] = {0, 0}, periods[2] = {1, 0}, remain[2] = {0, 1};
  MT_MPI(MPI_Dims_create(mt_size, 2, dims));
  MT_MPI(MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &cart_comm));
  MT_MPI(MPI_Cart_sub(cart_comm, remain, &row_comm));
  for (int j = 0; j < dims[1]; j++) {
    row[j] = mt_rank / dims[1] * dims[1] + j;
  }

  for (it = 0; mt_continue(it); it++) {
    if (mt_rank == it % mt_size) {
      usleep(2000);
    }
    use_comm("world", 0, MPI_COMM_WORLD, world, mt_size);
    use_comm("parity", 1, parity_comm, parity, nparity);
    use_comm("dup", 2, dup_comm, world, mt_size);
    if (created_comm != MPI_COMM_NULL) {
      int result;
      use_comm("created", 3, created_comm, created, ncreated);
      use_group(created_comm, created, ncreated, world_group);
      MPI_Group group;
      MT_MPI(MPI_Comm_group(created_comm, &group));
      MT_MPI(MPI_Group_compare(group, created_group, &result));
      MT_CHECK(result == MPI_IDENT, "iteration %ld: created group %d", it,
               result);
      MT_MPI(MPI_Group_free(&group));
    }
    use_comm("cart", 4, cart_comm, world, mt_size);
    use_comm("row", 5, row_comm, row, dims[1]);
    compare("world, dup", MPI_COMM_WORLD, dup_comm, MPI_CONGRUENT);
    compare("world, cart", MPI_COMM_WORLD, cart_comm, MPI_CONGRUENT);
    compare("dup, dup", dup_comm, dup_comm, MPI_IDENT);
    if (mt_size > 1) {
      compare("world, parity", MPI_COMM_WORLD, parity_comm, MPI_UNEQUAL);
    }

    // A temporary communicator: alternately a split of the world (in
    // reverse order, without the ranks of color 2) and a dup of parity_comm.
    MPI_Comm temp;
    int members[MAXP], n = 0;
    if (it % 2 == 0) {
      int color = (int)((mt_rank + it / 2) % 3);
      MT_MPI(MPI_Comm_split(MPI_COMM_WORLD, color == 2 ? MPI_UNDEFINED : color,
                            -mt_rank, &temp));
      for (int r = mt_size - 1; r >= 0; r--) {
        if ((r + it / 2) % 3 == color) {
          members[n++] = r;
        }
      }
      MT_CHECK((temp == MPI_COMM_NULL) == (color == 2),
               "iteration %ld: split, color %d", it, color);
    } else {
      MT_MPI(MPI_Comm_dup(parity_comm, &temp));
      memcpy(members, parity, sizeof(parity));
      n = nparity;
      compare("parity, temp", parity_comm, temp, MPI_CONGRUENT);
    }
    if (temp != MPI_COMM_NULL) {
      use_comm("temp", 6, temp, members, n);
      use_group(temp, members, n, world_group);
      MT_MPI(MPI_Comm_free(&temp));
    }
  }
  MPI_Group_free(&created_group);
  MPI_Group_free(&world_group);
  MPI_Comm_free(&row_comm);
  MPI_Comm_free(&cart_comm);
  if (created_comm != MPI_COMM_NULL) {
    MPI_Comm_free(&created_comm);
  }
  MPI_Comm_free(&dup_comm);
  MPI_Comm_free(&parity_comm);
  mt_finish(it);
  return 0;
}
