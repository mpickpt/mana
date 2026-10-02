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

// Cartesian topology, which MANA must rebuild at restart: a 2-D grid made
// once by MPI_Cart_create (dimension 0 periodic, dimension 1 not) and its
// rows made by MPI_Cart_sub.  Each iteration checks the topology queries and
// exchanges values with the neighbours that MPI_Cart_shift returns.

#include "mana_test.h"

#define N 8

static long it;

// The coordinate 'disp' steps from 'coord' along a dimension of size 'n', or
// -1 past the edge of a non-periodic one.
static int
step(int coord, int disp, int n, int periodic)
{
  int c = coord + disp;
  if (periodic) {
    return (c % n + n) % n;
  }
  return c >= 0 && c < n ? c : -1;
}

// Checks MPI_Cart_shift along 'dim' of 'comm', then sends to the destination
// and receives from the source in one MPI_Sendrecv; at the ends of a
// non-periodic dimension, one of them is MPI_PROC_NULL.  'base' is the world
// rank of rank 0.
static void
shift(const char *name, MPI_Comm comm, int base, int dim, int disp,
      int want_src, int want_dst, int salt)
{
  int me, src, dst;
  MT_MPI(MPI_Comm_rank(comm, &me));
  MT_MPI(MPI_Cart_shift(comm, dim, disp, &src, &dst));
  MT_CHECK(src == want_src && dst == want_dst, "iteration %ld %s dim %d "
           "disp %d: %d -> %d, not %d -> %d", it, name, dim, disp, src, dst,
           want_src, want_dst);

  int sbuf[N], rbuf[N];
  MPI_Status status;
  for (int i = 0; i < N; i++) {
    sbuf[i] = mt_value(base + me, it, salt + i);
    rbuf[i] = -1;
  }
  MT_MPI(MPI_Sendrecv(sbuf, N, MPI_INT, dst, dim, rbuf, N, MPI_INT, src, dim,
                      comm, &status));
  if (src != MPI_PROC_NULL) {
    int count;
    MPI_Get_count(&status, MPI_INT, &count);
    MT_CHECK(status.MPI_SOURCE == src && status.MPI_TAG == dim &&
             count == N, "iteration %ld %s dim %d: source %d tag %d count %d",
             it, name, dim, status.MPI_SOURCE, status.MPI_TAG, count);
  }
  for (int i = 0; i < N; i++) {
    int want = src == MPI_PROC_NULL ? -1 : mt_value(base + src, it, salt + i);
    MT_CHECK(rbuf[i] == want, "iteration %ld %s dim %d word %d: %d, not %d",
             it, name, dim, i, rbuf[i], want);
  }
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "cartesian");
  int dims[2] = {0, 0}, periods[2] = {1, 0}, remain[2] = {0, 1};
  MPI_Comm cart, row_comm;
  MT_MPI(MPI_Dims_create(mt_size, 2, dims));
  MT_MPI(MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &cart));
  MT_MPI(MPI_Cart_sub(cart, remain, &row_comm));
  // Without reordering, rank r is at (r / dims[1], r % dims[1]).
  int row = mt_rank / dims[1], col = mt_rank % dims[1];

  for (it = 0; mt_continue(it); it++) {
    if (mt_rank == it % mt_size) {
      usleep(2000);
    }
    int ndims, d[2], p[2], c[2], rank;
    MT_MPI(MPI_Cartdim_get(cart, &ndims));
    MT_MPI(MPI_Cart_get(cart, 2, d, p, c));
    MT_CHECK(ndims == 2 && d[0] == dims[0] && d[1] == dims[1] &&
             p[0] == 1 && p[1] == 0 && c[0] == row && c[1] == col,
             "iteration %ld: cart %d dims %dx%d periods %d,%d at (%d, %d)",
             it, ndims, d[0], d[1], p[0], p[1], c[0], c[1]);
    int r = (int)(it % mt_size);
    MT_MPI(MPI_Cart_coords(cart, r, 2, c));
    MT_CHECK(c[0] == r / dims[1] && c[1] == r % dims[1],
             "iteration %ld: rank %d at (%d, %d)", it, r, c[0], c[1]);
    MT_MPI(MPI_Cart_rank(cart, c, &rank));
    MT_CHECK(rank == r, "iteration %ld: rank at (%d, %d): %d, not %d", it,
             c[0], c[1], rank, r);

    int disp = 1 + (int)(it % dims[0]);
    int src = step(row, -disp, dims[0], 1), dst = step(row, disp, dims[0], 1);
    shift("cart", cart, 0, 0, disp, src * dims[1] + col,
          dst * dims[1] + col, 0);
    disp = it % 2 ? -1 : 1;
    src = step(col, -disp, dims[1], 0);
    dst = step(col, disp, dims[1], 0);
    shift("cart", cart, 0, 1, disp,
          src < 0 ? MPI_PROC_NULL : row * dims[1] + src,
          dst < 0 ? MPI_PROC_NULL : row * dims[1] + dst, N);

    // A row keeps dimension 1, so its ranks are the columns.
    MT_MPI(MPI_Cartdim_get(row_comm, &ndims));
    MT_MPI(MPI_Cart_get(row_comm, 1, d, p, c));
    MT_CHECK(ndims == 1 && d[0] == dims[1] && p[0] == 0 && c[0] == col,
             "iteration %ld: row %d dims %d period %d at %d", it, ndims,
             d[0], p[0], c[0]);
    shift("row", row_comm, row * dims[1], 0, disp,
          src < 0 ? MPI_PROC_NULL : src, dst < 0 ? MPI_PROC_NULL : dst,
          2 * N);
  }
  MPI_Comm_free(&row_comm);
  MPI_Comm_free(&cart);
  mt_finish(it);
  return 0;
}
