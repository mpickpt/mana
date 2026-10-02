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

// Communicator attributes, which MANA keeps in the upper half.  MPI_TAG_UB
// must be set, and a message with that tag must arrive.  A keyval made at
// start holds an attribute on MPI_COMM_WORLD and one on a duplicate of it;
// they must keep their values across checkpoints and restarts, and change
// every few iterations.  Every few iterations, a short-lived keyval is made,
// set, deleted and freed.  A delete callback counts the deleted values.

#include <stdint.h>
#include "mana_test.h"

#define CHANGE 5  // the attributes change every CHANGE iterations

static int extra_state;
static long deleted;  // values deleted by delete_fn()

static int
delete_fn(MPI_Comm comm, int keyval, void *value, void *extra)
{
  MT_CHECK(extra == &extra_state, "keyval %d: extra state %p", keyval,
           extra);
  deleted++;
  return MPI_SUCCESS;
}

// Values are stored in the attribute itself, not behind it.
static void *
attr_value(int comm, long it)
{
  return (void *)(intptr_t)mt_value(mt_rank, it, comm);
}

static void
check_attr(MPI_Comm comm, int keyval, void *expected, const char *what,
           long it)
{
  void *value = NULL;
  int flag = 0;
  MT_MPI(MPI_Comm_get_attr(comm, keyval, &value, &flag));
  MT_CHECK(flag && value == expected, "iteration %ld: %s: flag %d, value %p"
           " instead of %p", it, what, flag, value, expected);
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "attributes");
  int other = (mt_rank + 1) % mt_size;
  int prev = (mt_rank + mt_size - 1) % mt_size;
  MPI_Comm dup;
  int keyval;
  MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &dup));
  MT_MPI(MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, delete_fn, &keyval,
                                &extra_state));
  MT_MPI(MPI_Comm_set_attr(MPI_COMM_WORLD, keyval, attr_value(0, 0)));
  MT_MPI(MPI_Comm_set_attr(dup, keyval, attr_value(1, 0)));
  long expected_deleted = 0;
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (it % mt_size == mt_rank) {
      usleep(2000);
    }
    int *tag_ub = NULL, flag = 0;
    MT_MPI(MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_TAG_UB, &tag_ub, &flag));
    MT_CHECK(flag && *tag_ub >= 32767, "iteration %ld: MPI_TAG_UB: flag %d,"
             " value %d", it, flag, flag ? *tag_ub : 0);
    int sbuf = mt_value(mt_rank, it, 0), rbuf = -1;
    MPI_Status status;
    MT_MPI(MPI_Sendrecv(&sbuf, 1, MPI_INT, other, *tag_ub, &rbuf, 1, MPI_INT,
                        prev, *tag_ub, dup, &status));
    MT_CHECK(rbuf == mt_value(prev, it, 0) && status.MPI_TAG == *tag_ub,
             "iteration %ld: received %d with tag %d", it, rbuf,
             status.MPI_TAG);

    long set = it - it % CHANGE;  // when the values were last set
    check_attr(MPI_COMM_WORLD, keyval, attr_value(0, set), "world", it);
    check_attr(dup, keyval, attr_value(1, set), "dup", it);
    if (it % CHANGE == CHANGE - 1) {
      // Replacing a value deletes the old one.
      MT_MPI(MPI_Comm_set_attr(MPI_COMM_WORLD, keyval,
                               attr_value(0, it + 1)));
      MT_MPI(MPI_Comm_set_attr(dup, keyval, attr_value(1, it + 1)));
      expected_deleted += 2;
    }

    if (it % 4 == 0) {
      int keyval2;
      void *value;
      MT_MPI(MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, delete_fn,
                                    &keyval2, &extra_state));
      MT_CHECK(keyval2 != keyval, "iteration %ld: keyval %d reused", it,
               keyval);
      MT_MPI(MPI_Comm_get_attr(dup, keyval2, &value, &flag));
      MT_CHECK(!flag, "iteration %ld: %s", it, "new keyval has a value");
      MT_MPI(MPI_Comm_set_attr(dup, keyval2, attr_value(2, it)));
      check_attr(dup, keyval2, attr_value(2, it), "short-lived", it);
      MT_MPI(MPI_Comm_get_attr(MPI_COMM_WORLD, keyval2, &value, &flag));
      MT_CHECK(!flag, "iteration %ld: %s", it, "value on the wrong comm");
      MT_MPI(MPI_Comm_delete_attr(dup, keyval2));
      expected_deleted++;
      MT_MPI(MPI_Comm_get_attr(dup, keyval2, &value, &flag));
      MT_CHECK(!flag, "iteration %ld: %s", it, "deleted value still set");
      MT_MPI(MPI_Comm_free_keyval(&keyval2));
      MT_CHECK(keyval2 == MPI_KEYVAL_INVALID, "iteration %ld: keyval %d"
               " after MPI_Comm_free_keyval", it, keyval2);
    }
    MT_CHECK(deleted == expected_deleted, "iteration %ld: %ld values"
             " deleted, expected %ld", it, deleted, expected_deleted);
    usleep(1000);
  }
  MT_MPI(MPI_Comm_free(&dup));
  mt_finish(it);
  return 0;
}
