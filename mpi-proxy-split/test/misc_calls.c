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

// Calls that the other tests do not cover: MPI_Get_processor_name;
// MPI_Alloc_mem and MPI_Free_mem, for a buffer allocated at start (which
// must keep its content across checkpoints and restarts) and one per
// iteration; MPI_Allreduce with MPI_MINLOC and MPI_MAXLOC on MPI_DOUBLE_INT
// and MPI_2INT; MPI_Wtime; MPI_Initialized and MPI_Finalized; the tool
// information interface (MPI_T); and the variables that the MPI library
// defines (MPI_UNWEIGHTED, ...), which a program can use only by address.

#include <stdio.h>
#include "mana_test.h"

#define N 64  // ints in each buffer
#define K 8   // pairs in each reduction

struct double_int {
  double value;
  int index;
};
struct int_int {
  int value;
  int index;
};

// Pair k of 'rank'.  For even k, all ranks have the same value, so MINLOC
// and MAXLOC must return the smallest index.
static int
pair_value(int rank, long it, int k)
{
  return (int)((rank * (k % 2) + it + k) % 3);
}

static int
pair_index(int rank, int k)
{
  return rank * 100 + k;
}

// The result of MINLOC (sign 1) or MAXLOC (sign -1) for pair k.
static struct int_int
expected_pair(long it, int k, int sign)
{
  struct int_int best = {pair_value(0, it, k), pair_index(0, k)};
  for (int r = 1; r < mt_size; r++) {
    int v = pair_value(r, it, k);
    if (sign * v < sign * best.value) {
      best.value = v;
      best.index = pair_index(r, k);
    }
  }
  return best;
}

static void
check_minloc_maxloc(long it)
{
  struct double_int din[K], dmin[K], dmax[K];
  struct int_int iin[K], imin[K], imax[K];
  for (int k = 0; k < K; k++) {
    din[k].value = pair_value(mt_rank, it, k) + 0.25;
    din[k].index = pair_index(mt_rank, k);
    iin[k].value = pair_value(mt_rank, it, k);
    iin[k].index = pair_index(mt_rank, k);
  }
  MT_MPI(MPI_Allreduce(din, dmin, K, MPI_DOUBLE_INT, MPI_MINLOC,
                       MPI_COMM_WORLD));
  MT_MPI(MPI_Allreduce(din, dmax, K, MPI_DOUBLE_INT, MPI_MAXLOC,
                       MPI_COMM_WORLD));
  MT_MPI(MPI_Allreduce(iin, imin, K, MPI_2INT, MPI_MINLOC, MPI_COMM_WORLD));
  MT_MPI(MPI_Allreduce(iin, imax, K, MPI_2INT, MPI_MAXLOC, MPI_COMM_WORLD));
  for (int k = 0; k < K; k++) {
    struct int_int min = expected_pair(it, k, 1);
    struct int_int max = expected_pair(it, k, -1);
    MT_CHECK(dmin[k].value == min.value + 0.25 && dmin[k].index == min.index,
             "iteration %ld: DOUBLE_INT MINLOC %d: (%g, %d)", it, k,
             dmin[k].value, dmin[k].index);
    MT_CHECK(dmax[k].value == max.value + 0.25 && dmax[k].index == max.index,
             "iteration %ld: DOUBLE_INT MAXLOC %d: (%g, %d)", it, k,
             dmax[k].value, dmax[k].index);
    MT_CHECK(imin[k].value == min.value && imin[k].index == min.index,
             "iteration %ld: 2INT MINLOC %d: (%d, %d)", it, k, imin[k].value,
             imin[k].index);
    MT_CHECK(imax[k].value == max.value && imax[k].index == max.index,
             "iteration %ld: 2INT MAXLOC %d: (%d, %d)", it, k, imax[k].value,
             imax[k].index);
  }
}

// The calls a program makes to look for a control variable (MANA has none),
// between MPI_T_init_thread() at start and MPI_T_finalize() at the end.
// (MPICH 5.0.1 crashes in a later collective if MPI_T_finalize() ends the
// interface while MPI runs.)
static void
check_tools_interface(long it)
{
  int num = -1, index = -1;
  MT_MPI(MPI_T_cvar_get_num(&num));
  MT_CHECK(num >= 0, "iteration %ld: MPI_T_cvar_get_num: %d", it, num);
  MT_MPI(MPI_T_pvar_get_num(&num));
  MT_CHECK(num >= 0, "iteration %ld: MPI_T_pvar_get_num: %d", it, num);
  int rc = MPI_T_cvar_get_index("MANA_TEST_NO_SUCH_VARIABLE", &index);
  MT_CHECK(rc == MPI_T_ERR_INVALID_NAME,
           "iteration %ld: MPI_T_cvar_get_index: %d", it, rc);
}

static void
check_library_variables(void)
{
  MT_CHECK(MPI_UNWEIGHTED != NULL && MPI_WEIGHTS_EMPTY != NULL &&
           MPI_UNWEIGHTED != MPI_WEIGHTS_EMPTY,
           "MPI_UNWEIGHTED %p, MPI_WEIGHTS_EMPTY %p", (void *)MPI_UNWEIGHTED,
           (void *)MPI_WEIGHTS_EMPTY);
  // NULL in C programs; only their addresses can be checked.
  MT_CHECK((void *)&MPI_F_STATUS_IGNORE != (void *)&MPI_F_STATUSES_IGNORE,
           "MPI_F_STATUS_IGNORE and MPI_F_STATUSES_IGNORE are one variable");
}

int
main(int argc, char **argv)
{
  int flag = -1;
  MPI_Initialized(&flag);
  int initialized_before = flag;
  mt_init(&argc, &argv, "misc_calls");
  MT_CHECK(initialized_before == 0, "MPI_Initialized before MPI_Init: %d",
           initialized_before);
  check_library_variables();
  int provided = -1;
  MT_MPI(MPI_T_init_thread(MPI_THREAD_SINGLE, &provided));
  int right = (mt_rank + 1) % mt_size;
  int left = (mt_rank + mt_size - 1) % mt_size;
  int *kept;  // filled in each iteration, checked in the next one
  MT_MPI(MPI_Alloc_mem(N * sizeof(int), MPI_INFO_NULL, &kept));
  long it;
  for (it = 0; mt_continue(it); it++) {
    double t0 = MPI_Wtime();
    if (it % mt_size == mt_rank) {
      usleep(2000);
    }
    char name[MPI_MAX_PROCESSOR_NAME];
    int len = -1;
    memset(name, 0, sizeof(name));
    MT_MPI(MPI_Get_processor_name(name, &len));
    MT_CHECK(len > 0 && len < MPI_MAX_PROCESSOR_NAME &&
             strlen(name) == (size_t)len, "iteration %ld: name '%s' len %d",
             it, name, len);

    for (int i = 0; i < N; i++) {
      if (it > 0) {
        MT_CHECK(kept[i] == mt_value(mt_rank, it - 1, i),
                 "iteration %ld: kept word %d: %d", it, i, kept[i]);
      }
      kept[i] = mt_value(mt_rank, it, i);
    }
    int *recv;
    MT_MPI(MPI_Alloc_mem(N * sizeof(int), MPI_INFO_NULL, &recv));
    memset(recv, 0xff, N * sizeof(int));
    MT_MPI(MPI_Sendrecv(kept, N, MPI_INT, right, 1, recv, N, MPI_INT, left, 1,
                        MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    for (int i = 0; i < N; i++) {
      MT_CHECK(recv[i] == mt_value(left, it, i),
               "iteration %ld: received word %d: %d", it, i, recv[i]);
    }
    MT_MPI(MPI_Free_mem(recv));

    check_minloc_maxloc(it);
    check_tools_interface(it);

    flag = -1;
    MT_MPI(MPI_Initialized(&flag));
    MT_CHECK(flag == 1, "iteration %ld: MPI_Initialized: %d", it, flag);
    flag = -1;
    MT_MPI(MPI_Finalized(&flag));
    MT_CHECK(flag == 0, "iteration %ld: MPI_Finalized: %d", it, flag);

    double t1 = MPI_Wtime();
    usleep(1000);
    double t2 = MPI_Wtime();
    MT_CHECK(t0 <= t1 && t2 - t1 >= 0.0009, "iteration %ld: MPI_Wtime %.6f,"
             " %.6f, %.6f", it, t0, t1, t2);
  }
  MT_MPI(MPI_Free_mem(kept));
  mt_finish(it);
  flag = -1;
  MPI_Finalized(&flag);
  if (flag != 1) {
    fprintf(stderr, "misc_calls: rank %d: MPI_Finalized after MPI_Finalize:"
            " %d\n", mt_rank, flag);
    return 1;
  }
  if (MPI_T_finalize() != MPI_SUCCESS) {
    fprintf(stderr, "misc_calls: rank %d: MPI_T_finalize failed\n", mt_rank);
    return 1;
  }
  return 0;
}
