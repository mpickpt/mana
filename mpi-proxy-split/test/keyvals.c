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

// The lifetime of keyvals and the callbacks of attributes, which MANA keeps
// in the upper half.  At start, MPI_COMM_WORLD gets attributes with a copy
// callback, with MPI_COMM_DUP_FN and with MPI_COMM_NULL_COPY_FN, and
// MPI_COMM_SELF gets three attributes whose keyvals are freed at once.  Each
// iteration then checks, across checkpoints and restarts:
// - MPI_Comm_dup() copies the attributes that the copy callbacks return;
// - a keyval freed while an attribute uses it keeps its number, and freeing
//   the communicator calls its delete callback;
// - delete callbacks that free their own keyval in MPI_Comm_free();
// - errors of delete callbacks and of unknown keyvals;
// - MPI_APPNUM, MPI_UNIVERSE_SIZE and MPI_LASTUSEDCODE keep their launch
//   values;
// - MPI_Keyval_create() and MPI_Keyval_free().
// At the end, MPI_Finalize() must delete MPI_COMM_SELF's attributes, in the
// reverse order in which they were set.

#include <stdint.h>
#include "mana_test.h"

#define NSELF 3   // attributes on MPI_COMM_SELF
#define NFREE 4   // keyvals that free themselves in MPI_Comm_free()

static int extra_state;
static long copied;      // calls of copy_fn()
static long deleted;     // values deleted by delete_fn()
static int fail_delete;  // delete_maybe_fails() returns an error
static int finalizing;   // MPI_Finalize() was called
static char self_order[NSELF + 1];  // the deletions in MPI_Finalize()
static MPI_Comm bare;    // a duplicate of MPI_COMM_WORLD without attributes

// Returns the old value plus one, so that the new value shows it ran.
static int
copy_fn(MPI_Comm comm, int keyval, void *extra, void *in, void *out,
        int *flag)
{
  MT_CHECK(extra == &extra_state, "keyval %d: extra state %p", keyval,
           extra);
  copied++;
  *(void **)out = (void *)((intptr_t)in + 1);
  *flag = 1;
  return MPI_SUCCESS;
}

static int
delete_fn(MPI_Comm comm, int keyval, void *value, void *extra)
{
  MT_CHECK(extra == &extra_state, "keyval %d: extra state %p", keyval,
           extra);
  deleted++;
  return MPI_SUCCESS;
}

static int
delete_frees_keyval(MPI_Comm comm, int keyval, void *value, void *extra)
{
  deleted++;
  MT_MPI(MPI_Comm_free_keyval(&keyval));
  return MPI_SUCCESS;
}

static int
delete_maybe_fails(MPI_Comm comm, int keyval, void *value, void *extra)
{
  return fail_delete ? MPI_ERR_OTHER : MPI_SUCCESS;
}

// The extra state is the attribute's place in the order of setting.
static int
delete_self(MPI_Comm comm, int keyval, void *value, void *extra)
{
  if (finalizing) {
    size_t n = strlen(self_order);
    if (n < NSELF) {
      self_order[n] = '0' + (int)(intptr_t)extra;
    }
  }
  return MPI_SUCCESS;
}

// Values are stored in the attribute itself, not behind it.
static void *
attr_value(int index, long it)
{
  return (void *)(intptr_t)mt_value(mt_rank, it, index);
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

static void
check_no_attr(MPI_Comm comm, int keyval, const char *what, long it)
{
  void *value = NULL;
  int flag = 0;
  MT_MPI(MPI_Comm_get_attr(comm, keyval, &value, &flag));
  MT_CHECK(!flag, "iteration %ld: %s: has value %p", it, what, value);
}

// MPI_Comm_dup() copies what the copy callbacks return; freeing the copy
// deletes the copied values.
static void
check_dup(int k_copy, int k_dupfn, int k_nullcopy, long it)
{
  MPI_Comm dup;
  long copied_before = copied;
  long deleted_before = deleted;
  MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &dup));
  MT_CHECK(copied == copied_before + 1, "iteration %ld: %ld copy calls",
           it, copied - copied_before);
  check_attr(dup, k_copy, (void *)((intptr_t)attr_value(0, 0) + 1),
             "copy callback", it);
  check_attr(dup, k_dupfn, attr_value(1, 0), "MPI_COMM_DUP_FN", it);
  check_no_attr(dup, k_nullcopy, "MPI_COMM_NULL_COPY_FN", it);
  MT_MPI(MPI_Comm_free(&dup));
  MT_CHECK(deleted == deleted_before + 2, "iteration %ld: %ld values deleted"
           " with the copy, expected 2", it, deleted - deleted_before);
}

// A keyval freed while an attribute uses it keeps its number, and its delete
// callback runs when the communicator is freed.
static void
check_freed_in_use(long it)
{
  MPI_Comm dup;
  int keyval, number, other;
  long deleted_before = deleted;
  MT_MPI(MPI_Comm_dup(bare, &dup));
  MT_MPI(MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, delete_fn, &keyval,
                                &extra_state));
  number = keyval;
  MT_MPI(MPI_Comm_set_attr(dup, keyval, attr_value(2, it)));
  MT_MPI(MPI_Comm_free_keyval(&keyval));
  MT_CHECK(keyval == MPI_KEYVAL_INVALID, "iteration %ld: keyval %d after"
           " MPI_Comm_free_keyval", it, keyval);
  MT_MPI(MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, delete_fn, &other,
                                &extra_state));
  MT_CHECK(other != number, "iteration %ld: keyval %d reused while in use",
           it, number);
  MT_MPI(MPI_Comm_free_keyval(&other));
  MT_MPI(MPI_Comm_free(&dup));
  MT_CHECK(deleted == deleted_before + 1, "iteration %ld: %ld values deleted"
           " with the freed keyval, expected 1", it, deleted - deleted_before);
}

// Delete callbacks that free their own keyval in MPI_Comm_free().
static void
check_callbacks_free_keyvals(long it)
{
  MPI_Comm dup;
  long deleted_before = deleted;
  MT_MPI(MPI_Comm_dup(bare, &dup));
  for (int i = 0; i < NFREE; i++) {
    int keyval;
    MT_MPI(MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, delete_frees_keyval,
                                  &keyval, NULL));
    MT_MPI(MPI_Comm_set_attr(dup, keyval, attr_value(3, it)));
  }
  MT_MPI(MPI_Comm_free(&dup));
  MT_CHECK(deleted == deleted_before + NFREE, "iteration %ld: %ld values"
           " deleted by callbacks that free their keyval, expected %d", it,
           deleted - deleted_before, NFREE);
}

// An error of a delete callback is the error of the call; an unknown keyval
// is an error.
static void
check_errors(long it)
{
  MPI_Comm dup;
  int keyval, size, bad;
  MT_MPI(MPI_Comm_dup(bare, &dup));
  MT_MPI(MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, delete_maybe_fails,
                                &keyval, NULL));
  MT_MPI(MPI_Comm_set_attr(dup, keyval, attr_value(4, it)));
  fail_delete = 1;
  MT_CHECK(MPI_Comm_set_attr(dup, keyval, attr_value(5, it)) != MPI_SUCCESS,
           "iteration %ld: %s", it, "MPI_Comm_set_attr ignored the error");
  check_attr(dup, keyval, attr_value(4, it), "value after a failed set", it);
  MT_CHECK(MPI_Comm_delete_attr(dup, keyval) != MPI_SUCCESS,
           "iteration %ld: %s", it, "MPI_Comm_delete_attr ignored the error");
  MT_CHECK(MPI_Comm_free(&dup) != MPI_SUCCESS, "iteration %ld: %s", it,
           "MPI_Comm_free ignored the error");
  MT_MPI(MPI_Comm_size(dup, &size));
  fail_delete = 0;
  MT_MPI(MPI_Comm_free(&dup));
  MT_MPI(MPI_Comm_free_keyval(&keyval));
  bad = 123456789;
  MT_CHECK(MPI_Comm_delete_attr(MPI_COMM_WORLD, bad) != MPI_SUCCESS,
           "iteration %ld: %s", it, "unknown keyval in MPI_Comm_delete_attr");
  MT_CHECK(MPI_Comm_free_keyval(&bad) != MPI_SUCCESS, "iteration %ld: %s",
           it, "unknown keyval in MPI_Comm_free_keyval");
}

// The values and flags of the predefined attributes at launch.
static const int predefined[] = {MPI_APPNUM, MPI_UNIVERSE_SIZE,
                                 MPI_LASTUSEDCODE};
static const char *predefined_names[] = {"MPI_APPNUM", "MPI_UNIVERSE_SIZE",
                                         "MPI_LASTUSEDCODE"};
#define NPREDEFINED 3
static int launch_values[NPREDEFINED];
static int launch_flags[NPREDEFINED];

static void
get_predefined(int i, int *value, int *flag)
{
  int *p = NULL;
  *flag = 0;
  MT_MPI(MPI_Comm_get_attr(MPI_COMM_WORLD, predefined[i], &p, flag));
  *value = *flag ? *p : 0;
}

static void
check_predefined(long it)
{
  for (int i = 0; i < NPREDEFINED; i++) {
    int value, flag;
    get_predefined(i, &value, &flag);
    MT_CHECK(flag == launch_flags[i] && value == launch_values[i],
             "iteration %ld: %s: flag %d, value %d; at launch %d, %d", it,
             predefined_names[i], flag, value, launch_flags[i],
             launch_values[i]);
  }
}

// MPI-1's names of MPI_Comm_create_keyval() and MPI_Comm_free_keyval().
static void
check_mpi1_keyval(long it)
{
  int keyval;
  long deleted_before = deleted;
  MT_MPI(MPI_Keyval_create(MPI_NULL_COPY_FN, delete_fn, &keyval,
                           &extra_state));
  MT_MPI(MPI_Comm_set_attr(MPI_COMM_WORLD, keyval, attr_value(6, it)));
  check_attr(MPI_COMM_WORLD, keyval, attr_value(6, it), "MPI-1 keyval", it);
  MT_MPI(MPI_Comm_delete_attr(MPI_COMM_WORLD, keyval));
  MT_MPI(MPI_Keyval_free(&keyval));
  MT_CHECK(keyval == MPI_KEYVAL_INVALID, "iteration %ld: keyval %d after"
           " MPI_Keyval_free", it, keyval);
  MT_CHECK(deleted == deleted_before + 1, "iteration %ld: %ld values deleted"
           " with the MPI-1 keyval, expected 1", it, deleted - deleted_before);
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "keyvals");
  // The errors of check_errors() must return.
  MT_MPI(MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN));
  MT_MPI(MPI_Comm_set_errhandler(MPI_COMM_SELF, MPI_ERRORS_RETURN));
  MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &bare));
  int k_copy, k_dupfn, k_nullcopy;
  MT_MPI(MPI_Comm_create_keyval(copy_fn, delete_fn, &k_copy, &extra_state));
  MT_MPI(MPI_Comm_create_keyval(MPI_COMM_DUP_FN, delete_fn, &k_dupfn,
                                &extra_state));
  MT_MPI(MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, delete_fn,
                                &k_nullcopy, &extra_state));
  MT_MPI(MPI_Comm_set_attr(MPI_COMM_WORLD, k_copy, attr_value(0, 0)));
  MT_MPI(MPI_Comm_set_attr(MPI_COMM_WORLD, k_dupfn, attr_value(1, 0)));
  MT_MPI(MPI_Comm_set_attr(MPI_COMM_WORLD, k_nullcopy, attr_value(2, 0)));
  // As HDF5 does: set an attribute on MPI_COMM_SELF and free its keyval.
  for (int i = 0; i < NSELF; i++) {
    int keyval;
    MT_MPI(MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, delete_self,
                                  &keyval, (void *)(intptr_t)i));
    MT_MPI(MPI_Comm_set_attr(MPI_COMM_SELF, keyval, NULL));
    MT_MPI(MPI_Comm_free_keyval(&keyval));
  }
  for (int i = 0; i < NPREDEFINED; i++) {
    get_predefined(i, &launch_values[i], &launch_flags[i]);
  }
  MT_CHECK(!launch_flags[0] || launch_values[0] == 0, "MPI_APPNUM %d",
           launch_values[0]);

  long it;
  for (it = 0; mt_continue(it); it++) {
    check_attr(MPI_COMM_WORLD, k_copy, attr_value(0, 0), "copied keyval", it);
    check_attr(MPI_COMM_WORLD, k_dupfn, attr_value(1, 0), "MPI_COMM_DUP_FN",
               it);
    check_dup(k_copy, k_dupfn, k_nullcopy, it);
    check_freed_in_use(it);
    check_callbacks_free_keyvals(it);
    check_errors(it);
    check_predefined(it);
    check_mpi1_keyval(it);
    usleep(2000);
  }
  MT_MPI(MPI_Comm_free_keyval(&k_copy));
  MT_MPI(MPI_Comm_free_keyval(&k_dupfn));
  MT_MPI(MPI_Comm_free_keyval(&k_nullcopy));
  MT_MPI(MPI_Comm_free(&bare));
  finalizing = 1;
  mt_finish(it);
  if (strcmp(self_order, "210") != 0) {
    fprintf(stderr, "keyvals: rank %d: MPI_Finalize deleted MPI_COMM_SELF's"
            " attributes in the order '%s', not '210'\n", mt_rank, self_order);
    return 1;
  }
  return 0;
}
