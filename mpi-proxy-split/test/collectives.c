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

// Blocking collectives on MPI_COMM_WORLD and on its two halves (made once by
// MPI_Comm_split).  The root rotates with the iteration.  In each iteration
// one rank is late for one of the collectives, so that a checkpoint finds
// the others waiting inside it.

#include "mana_test.h"

#define N 8     // words per rank in each collective
#define MAXP 8  // the buffers have room for this many ranks
#define WORDS (MAXP * (N + 1))

enum { BARRIER, BCAST, REDUCE, ALLREDUCE, ALLREDUCE_MAX, ALLREDUCE_USER,
       GATHER, GATHERV, SCATTER, SCATTERV, ALLGATHER, ALLGATHERV, ALLTOALL,
       ALLTOALLV, SCAN, REDUCE_SCATTER, NOPS };

typedef struct {
  MPI_Comm comm;
  int id;  // 0: MPI_COMM_WORLD, 1: a half
  int size;
  int rank;
  int base;  // world rank of rank 0
} Comm;

typedef struct {  // x -> a * x + b, laid out as MPI_2INT
  int a, b;
} Affine;

static long it;
static MPI_Op compose_op;

// Word i of what rank 'src' of 'c' contributes to collective 'op'.
static int
val(const Comm *c, int op, int src, int i)
{
  return mt_value(c->base + src, it, (c->id * NOPS + op) * 128 + i);
}

// Number of words that rank 'src' sends to 'dst' in a "v" collective.
static int
cnt(int src, int dst)
{
  return 1 + (int)((src + 2 * dst + it) % N);
}

static long
sum(const Comm *c, int op, int nranks, int i)
{
  long s = 0;
  for (int j = 0; j < nranks; j++) {
    s += val(c, op, j, i);
  }
  return s;
}

static Affine
affine(const Comm *c, int src, int i)
{
  Affine f = {val(c, ALLREDUCE_USER, src, i) | 1,
              val(c, ALLREDUCE_USER, src, N + i)};
  return f;
}

// inout = in o inout.  Composition is not commutative, so the result shows
// whether MPI applied the ranks' values in rank order.
static void
compose(void *in, void *inout, int *len, MPI_Datatype *type)
{
  Affine *f = in, *g = inout;
  for (int i = 0; i < *len; i++) {
    g[i].b = (int)((unsigned)f[i].a * (unsigned)g[i].b + (unsigned)f[i].b);
    g[i].a = (int)((unsigned)f[i].a * (unsigned)g[i].a);
  }
}

static void
check(const Comm *c, const char *what, int i, long got, long want)
{
  MT_CHECK(got == want, "iteration %ld comm %d.%d %s word %d: %ld, not %ld",
           it, c->id, c->base, what, i, got, want);
}

// Checks 'total' words of 'buf': word i of block j is val(c, op, j,
// first + i), and words outside the blocks are untouched (-1).
static void
check_blocks(const Comm *c, const char *what, const int *buf, int total,
             const int *counts, const int *displs, int op, int first)
{
  int want[WORDS + 1];
  for (int k = 0; k < total; k++) {
    want[k] = -1;
  }
  for (int j = 0; j < c->size; j++) {
    for (int i = 0; i < counts[j]; i++) {
      want[displs[j] + i] = val(c, op, j, first + i);
    }
  }
  for (int k = 0; k < total; k++) {
    check(c, what, k, buf[k], want[k]);
  }
}

static void
fill(int *buf, int n)
{
  for (int i = 0; i < n; i++) {
    buf[i] = -1;
  }
}

// Lays out blocks of 'counts' words with one unused word after each;
// returns the total size.
static int
gapped(const int *counts, int *displs, int n)
{
  int total = 0;
  for (int j = 0; j < n; j++) {
    displs[j] = total;
    total += counts[j] + 1;
  }
  return total;
}

static void
maybe_late(const Comm *c, int op)
{
  if (mt_rank == it % mt_size &&
      (it / mt_size) % (2 * NOPS) == c->id * NOPS + op) {
    usleep(3000);
  }
}

static void
collectives(const Comm *c)
{
  int n = c->size, me = c->rank;
  int root = (int)((it + c->id) % n);
  int sbuf[WORDS + 1], rbuf[WORDS + 1];
  long lsend[WORDS], lrecv[WORDS];
  int counts[MAXP], displs[MAXP], scounts[MAXP], sdispls[MAXP];
  int total;

  maybe_late(c, BARRIER);
  MT_MPI(MPI_Barrier(c->comm));

  maybe_late(c, BCAST);
  for (int i = 0; i < N; i++) {
    rbuf[i] = me == root ? val(c, BCAST, root, i) : -1;
  }
  MT_MPI(MPI_Bcast(rbuf, N, MPI_INT, root, c->comm));
  for (int i = 0; i < N; i++) {
    check(c, "bcast", i, rbuf[i], val(c, BCAST, root, i));
  }

  maybe_late(c, REDUCE);
  for (int i = 0; i < N; i++) {
    lsend[i] = val(c, REDUCE, me, i);
    lrecv[i] = -1;
  }
  MT_MPI(MPI_Reduce(lsend, lrecv, N, MPI_LONG, MPI_SUM, root, c->comm));
  for (int i = 0; me == root && i < N; i++) {
    check(c, "reduce", i, lrecv[i], sum(c, REDUCE, n, i));
  }

  maybe_late(c, ALLREDUCE);
  for (int i = 0; i < N; i++) {
    lsend[i] = val(c, ALLREDUCE, me, i);
  }
  MT_MPI(MPI_Allreduce(lsend, lrecv, N, MPI_LONG, MPI_SUM, c->comm));
  for (int i = 0; i < N; i++) {
    check(c, "allreduce sum", i, lrecv[i], sum(c, ALLREDUCE, n, i));
  }

  // The rank with the largest value changes from word to word.
  maybe_late(c, ALLREDUCE_MAX);
  for (int i = 0; i < N; i++) {
    lsend[i] = val(c, ALLREDUCE_MAX, me, i) % 997;
  }
  MT_MPI(MPI_Allreduce(lsend, lrecv, N, MPI_LONG, MPI_MAX, c->comm));
  for (int i = 0; i < N; i++) {
    long max = 0;
    for (int j = 0; j < n; j++) {
      long v = val(c, ALLREDUCE_MAX, j, i) % 997;
      max = j == 0 || v > max ? v : max;
    }
    check(c, "allreduce max", i, lrecv[i], max);
  }

  maybe_late(c, ALLREDUCE_USER);
  Affine fsend[N], frecv[N];
  for (int i = 0; i < N; i++) {
    fsend[i] = affine(c, me, i);
  }
  MT_MPI(MPI_Allreduce(fsend, frecv, N, MPI_2INT, compose_op, c->comm));
  for (int i = 0; i < N; i++) {
    Affine want = affine(c, n - 1, i);
    for (int j = n - 2; j >= 0; j--) {
      Affine f = affine(c, j, i);
      int one = 1;
      compose(&f, &want, &one, NULL);
    }
    check(c, "allreduce user op a", i, frecv[i].a, want.a);
    check(c, "allreduce user op b", i, frecv[i].b, want.b);
  }

  maybe_late(c, GATHER);
  for (int j = 0; j < n; j++) {
    counts[j] = N;
    displs[j] = j * N;
  }
  for (int i = 0; i < N; i++) {
    sbuf[i] = val(c, GATHER, me, i);
  }
  fill(rbuf, n * N + 1);
  MT_MPI(MPI_Gather(sbuf, N, MPI_INT, rbuf, N, MPI_INT, root, c->comm));
  if (me == root) {
    check_blocks(c, "gather", rbuf, n * N + 1, counts, displs, GATHER, 0);
  }

  maybe_late(c, GATHERV);
  for (int j = 0; j < n; j++) {
    counts[j] = cnt(j, root);
  }
  total = gapped(counts, displs, n);
  for (int i = 0; i < counts[me]; i++) {
    sbuf[i] = val(c, GATHERV, me, i);
  }
  fill(rbuf, total);
  MT_MPI(MPI_Gatherv(sbuf, counts[me], MPI_INT, rbuf, counts, displs,
                     MPI_INT, root, c->comm));
  if (me == root) {
    check_blocks(c, "gatherv", rbuf, total, counts, displs, GATHERV, 0);
  }

  maybe_late(c, SCATTER);
  for (int k = 0; me == root && k < n * N; k++) {
    sbuf[k] = val(c, SCATTER, root, k);
  }
  fill(rbuf, N + 1);
  MT_MPI(MPI_Scatter(sbuf, N, MPI_INT, rbuf, N, MPI_INT, root, c->comm));
  for (int i = 0; i <= N; i++) {
    check(c, "scatter", i, rbuf[i],
          i < N ? val(c, SCATTER, root, me * N + i) : -1);
  }

  maybe_late(c, SCATTERV);
  for (int j = 0; j < n; j++) {
    scounts[j] = cnt(root, j);
  }
  gapped(scounts, sdispls, n);
  for (int j = 0; me == root && j < n; j++) {
    for (int i = 0; i < scounts[j]; i++) {
      sbuf[sdispls[j] + i] = val(c, SCATTERV, root, j * N + i);
    }
  }
  fill(rbuf, N + 1);
  MT_MPI(MPI_Scatterv(sbuf, scounts, sdispls, MPI_INT, rbuf, scounts[me],
                      MPI_INT, root, c->comm));
  for (int i = 0; i <= N; i++) {
    check(c, "scatterv", i, rbuf[i],
          i < scounts[me] ? val(c, SCATTERV, root, me * N + i) : -1);
  }

  maybe_late(c, ALLGATHER);
  for (int j = 0; j < n; j++) {
    counts[j] = N;
    displs[j] = j * N;
  }
  for (int i = 0; i < N; i++) {
    sbuf[i] = val(c, ALLGATHER, me, i);
  }
  fill(rbuf, n * N + 1);
  MT_MPI(MPI_Allgather(sbuf, N, MPI_INT, rbuf, N, MPI_INT, c->comm));
  check_blocks(c, "allgather", rbuf, n * N + 1, counts, displs, ALLGATHER,
               0);

  maybe_late(c, ALLGATHERV);
  for (int j = 0; j < n; j++) {
    counts[j] = cnt(j, root);
  }
  total = gapped(counts, displs, n);
  for (int i = 0; i < counts[me]; i++) {
    sbuf[i] = val(c, ALLGATHERV, me, i);
  }
  fill(rbuf, total);
  MT_MPI(MPI_Allgatherv(sbuf, counts[me], MPI_INT, rbuf, counts, displs,
                        MPI_INT, c->comm));
  check_blocks(c, "allgatherv", rbuf, total, counts, displs, ALLGATHERV, 0);

  // Rank j sends val(c, op, j, d * N + i) to rank d.
  maybe_late(c, ALLTOALL);
  for (int j = 0; j < n; j++) {
    counts[j] = N;
    displs[j] = j * N;
  }
  for (int k = 0; k < n * N; k++) {
    sbuf[k] = val(c, ALLTOALL, me, k);
  }
  fill(rbuf, n * N + 1);
  MT_MPI(MPI_Alltoall(sbuf, N, MPI_INT, rbuf, N, MPI_INT, c->comm));
  check_blocks(c, "alltoall", rbuf, n * N + 1, counts, displs, ALLTOALL,
               me * N);

  maybe_late(c, ALLTOALLV);
  for (int j = 0; j < n; j++) {
    scounts[j] = cnt(me, j);
    counts[j] = cnt(j, me);
  }
  gapped(scounts, sdispls, n);
  total = gapped(counts, displs, n);
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < scounts[j]; i++) {
      sbuf[sdispls[j] + i] = val(c, ALLTOALLV, me, j * N + i);
    }
  }
  fill(rbuf, total);
  MT_MPI(MPI_Alltoallv(sbuf, scounts, sdispls, MPI_INT, rbuf, counts, displs,
                       MPI_INT, c->comm));
  check_blocks(c, "alltoallv", rbuf, total, counts, displs, ALLTOALLV,
               me * N);

  maybe_late(c, SCAN);
  for (int i = 0; i < N; i++) {
    lsend[i] = val(c, SCAN, me, i);
  }
  MT_MPI(MPI_Scan(lsend, lrecv, N, MPI_LONG, MPI_SUM, c->comm));
  for (int i = 0; i < N; i++) {
    check(c, "scan", i, lrecv[i], sum(c, SCAN, me + 1, i));
  }

  maybe_late(c, REDUCE_SCATTER);
  int offset = 0;
  total = 0;
  for (int j = 0; j < n; j++) {
    counts[j] = cnt(j, root);
    offset += j < me ? counts[j] : 0;
    total += counts[j];
  }
  for (int k = 0; k < total; k++) {
    lsend[k] = val(c, REDUCE_SCATTER, me, k);
  }
  for (int i = 0; i <= N; i++) {
    lrecv[i] = -1;
  }
  MT_MPI(MPI_Reduce_scatter(lsend, lrecv, counts, MPI_LONG, MPI_SUM,
                            c->comm));
  for (int i = 0; i <= N; i++) {
    check(c, "reduce_scatter", i, lrecv[i],
          i < counts[me] ? sum(c, REDUCE_SCATTER, n, offset + i) : -1);
  }
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "collectives");
  MT_CHECK(mt_size <= MAXP, "%d ranks, at most %d", mt_size, MAXP);
  Comm world = {MPI_COMM_WORLD, 0, mt_size, mt_rank, 0};
  int lower = mt_size / 2;  // size of the lower half
  int upper = mt_rank >= lower;
  Comm half = {MPI_COMM_NULL, 1, upper ? mt_size - lower : lower,
               upper ? mt_rank - lower : mt_rank, upper ? lower : 0};
  MT_MPI(MPI_Comm_split(MPI_COMM_WORLD, upper, 0, &half.comm));
  int size, rank;
  MPI_Comm_size(half.comm, &size);
  MPI_Comm_rank(half.comm, &rank);
  MT_CHECK(size == half.size && rank == half.rank, "half: rank %d of %d",
           rank, size);
  MT_MPI(MPI_Op_create(compose, 0, &compose_op));
  for (it = 0; mt_continue(it); it++) {
    collectives(&world);
    collectives(&half);
  }
  MPI_Op_free(&compose_op);
  MPI_Comm_free(&half.comm);
  mt_finish(it);
  return 0;
}
