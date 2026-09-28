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

/*
  Synthetic stand-in for communication-heavy applications, for end-to-end
  performance measurement and checkpoint/restart testing under MANA.
  It uses only MPI calls that MANA wraps.

  Profiles (--profile=):
    halo     Generic point-to-point: a 2D/3D halo exchange with Irecv/Isend/
             Waitall (some messages use derived datatypes), an Allreduce on
             the row communicator, and an Ibcast or Ireduce overlapped with
             the compute step.
    vasp     Blocking-collective-heavy: many small Allreduces on world/row/
             column communicators with MPI_SUM, MPI_MAX and a user op, two
             Bcasts, one Alltoallv (FFT transpose), one Allgatherv and one
             Sendrecv per iteration.
    gromacs  Sendrecv-halo-heavy: world is split 3:1 into PP and PME ranks.
             PP ranks do a 3D domain-decomposition halo with MPI_Sendrecv
             plus a coordinate halo with Irecv/Isend/Waitall, and exchange
             coordinates/forces with their PME rank.  PME ranks do an
             Alltoall among themselves.  An energy Allreduce on world runs
             every --energy-every iterations.

  Every message and reduction carries values derived from (rank, iteration),
  which each rank checks and folds into a checksum in a fixed order.  The
  global checksum depends only on the parameters, not on timing or
  --compute-us, so it is the same natively, under MANA, and across
  checkpoint/restart.

  Options:
    --profile=halo|vasp|gromacs   (default halo)
    --iters=N                     main-loop iterations (default 1000)
    --compute-us=X                compute step per iteration (default 0)
    --msg=BYTES                   halo message size (default 4096)
    --neighbors=K                 halo neighbours: 2, 4 or 6 (default 6)
    --allreduce-count=N           vasp Allreduces per iteration (default 30)
    --a2a-bytes=BYTES             vasp Alltoallv bytes per peer (default 4096)
    --energy-every=N              gromacs energy Allreduce period (default 10)
    --churn[=K]                   every K iterations (default 10) create and
                                  free a communicator and a datatype

  Output (rank 0): wall time of the main loop (max over ranks), iterations
  per second, the checksum, PASS or FAIL, and the number of calls per
  second each rank made to each MPI function (average and max over ranks).
*/

#include <mpi.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/************************************************************
 * Parameters and global state
 ************************************************************/
enum { HALO, VASP, GROMACS };
static const char *profile_names[] = { "halo", "vasp", "gromacs" };

static int profile = HALO;
static long iters = 1000;
static double compute_us = 0;
static int msg_bytes = 4096;
static int neighbors = 6;
static int allreduce_count = 30;
static int a2a_bytes = 4096;
static int energy_every = 10;
static int churn = 0;

static int rank, size;
static uint64_t checksum = 0;
static uint64_t errors = 0;
static long compute_iters_per_step = 0;
static volatile double compute_sink = 0;

// Communicators and objects created once at setup.
static MPI_Comm dup_comm, row_comm, col_comm;
static int px, py;        // 2D grid: px rows of py ranks; row = rank / py
static MPI_Datatype vec_type, pair_type;
static MPI_Op user_op;

#define VEC_BLOCKS 64     // vec_type: 64 doubles with stride 2
#define VEC_EXTENT (2 * VEC_BLOCKS - 1)

typedef struct {
  int i;
  double d;
} pair_t;

/************************************************************
 * Call counting
 ************************************************************/
enum {
  C_ALLREDUCE, C_BCAST, C_ALLTOALL, C_ALLTOALLV, C_ALLGATHERV, C_SENDRECV,
  C_ISEND, C_IRECV, C_WAITALL, C_WAIT, C_IBCAST, C_IREDUCE,
  C_COMM_SPLIT, C_COMM_FREE, C_TYPE_CREATE, C_TYPE_FREE, C_NCALLS
};
static const char *call_names[] = {
  "MPI_Allreduce", "MPI_Bcast", "MPI_Alltoall", "MPI_Alltoallv",
  "MPI_Allgatherv", "MPI_Sendrecv", "MPI_Isend", "MPI_Irecv", "MPI_Waitall",
  "MPI_Wait", "MPI_Ibcast", "MPI_Ireduce", "MPI_Comm_split", "MPI_Comm_free",
  "MPI_Type_create", "MPI_Type_free"
};
static long calls[C_NCALLS];
#define COUNT(c) (calls[(c)]++)

/************************************************************
 * Values and checksum
 ************************************************************/
// An integer-valued double, so sums over ranks are exact in any order.
static inline double
val(int src, long it, long j, int salt)
{
  uint64_t x = (uint64_t)(src + 1) * 1000003u + (uint64_t)it * 7919u +
               (uint64_t)j * 131u + (uint64_t)salt * 17u;
  return (double)(x % 999983u);
}

static inline void
check(double got, double expected)
{
  if (got != expected) {
    errors++;
  }
  checksum = checksum * 6364136223846793005ULL + (uint64_t)got +
             1442695040888963407ULL;
}

static void
user_sum(void *in, void *inout, int *len, MPI_Datatype *type)
{
  double *a = (double *)in;
  double *b = (double *)inout;
  for (int i = 0; i < *len; i++) {
    b[i] += a[i];
  }
}

/************************************************************
 * Compute step
 ************************************************************/
static void
compute(long n)
{
  double x = 1.0;
  for (long i = 0; i < n; i++) {
    x = x * 1.0000001 + 1e-7;
  }
  compute_sink += x;
}

static double
now_s(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Calibrates the compute kernel once; all ranks use the same rate.
static void
calibrate_compute(void)
{
  const long n = 20000000;
  double best = 1e30;
  for (int trial = 0; trial < 3; trial++) {
    double t = now_s();
    compute(n);
    t = now_s() - t;
    if (t < best) {
      best = t;
    }
  }
  double rate = n / (best * 1e6);  // iterations per microsecond
  double max_rate;
  MPI_Allreduce(&rate, &max_rate, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  compute_iters_per_step = (long)(max_rate * compute_us);
  if (rank == 0) {
    printf("compute: %.1f iterations/us, %ld iterations per step\n",
           max_rate, compute_iters_per_step);
  }
}

/************************************************************
 * Process topology helpers
 ************************************************************/
// Factors n into ndims near-equal factors.
static void
factor_dims(int n, int ndims, int *dims)
{
  int primes[32], np = 0;
  for (int d = 0; d < ndims; d++) {
    dims[d] = 1;
  }
  for (int p = 2; n > 1 && p <= n; p++) {
    while (n % p == 0) {
      primes[np++] = p;
      n /= p;
    }
  }
  for (int k = np - 1; k >= 0; k--) {
    int smallest = 0;
    for (int d = 1; d < ndims; d++) {
      if (dims[d] < dims[smallest]) {
        smallest = d;
      }
    }
    dims[smallest] *= primes[k];
  }
}

// Neighbour of 'r' on a periodic grid 'dims' along 'dim' in direction 'sign'.
static int
torus_neighbor(int r, const int *dims, int ndims, int dim, int sign)
{
  int coord[3], stride = 1;
  for (int d = 0; d < ndims; d++) {
    coord[d] = (r / stride) % dims[d];
    stride *= dims[d];
  }
  coord[dim] = (coord[dim] + sign + dims[dim]) % dims[dim];
  int out = 0;
  stride = 1;
  for (int d = 0; d < ndims; d++) {
    out += coord[d] * stride;
    stride *= dims[d];
  }
  return out;
}

// World ranks of the members of the row / column of 'r', in comm order.
static int
row_member(int r, int m)
{
  return (r / py) * py + m;
}

static int
col_member(int r, int m)
{
  return (r % py) + m * py;
}

/************************************************************
 * Setup common to all profiles
 ************************************************************/
static void
common_setup(void)
{
  // 2D grid: py = largest divisor of size <= sqrt(size); columns are the
  // larger dimension.
  py = 1;
  for (int d = 1; d * d <= size; d++) {
    if (size % d == 0) {
      py = d;
    }
  }
  px = size / py;
  MPI_Comm_dup(MPI_COMM_WORLD, &dup_comm);
  MPI_Comm_split(MPI_COMM_WORLD, rank / py, rank, &row_comm);
  MPI_Comm_split(MPI_COMM_WORLD, rank % py, rank, &col_comm);

  MPI_Type_vector(VEC_BLOCKS, 1, 2, MPI_DOUBLE, &vec_type);
  MPI_Type_commit(&vec_type);

  int blocklens[2] = { 1, 1 };
  MPI_Aint displs[2] = { offsetof(pair_t, i), offsetof(pair_t, d) };
  MPI_Datatype types[2] = { MPI_INT, MPI_DOUBLE };
  MPI_Datatype tmp;
  MPI_Type_create_struct(2, blocklens, displs, types, &tmp);
  MPI_Type_create_resized(tmp, 0, sizeof(pair_t), &pair_type);
  MPI_Type_commit(&pair_type);
  MPI_Type_free(&tmp);

  MPI_Op_create(user_sum, 1, &user_op);
}

static void
common_teardown(void)
{
  MPI_Op_free(&user_op);
  MPI_Type_free(&pair_type);
  MPI_Type_free(&vec_type);
  MPI_Comm_free(&col_comm);
  MPI_Comm_free(&row_comm);
  MPI_Comm_free(&dup_comm);
}

/************************************************************
 * Churn: create and free a communicator and a datatype
 ************************************************************/
static void
do_churn(long it)
{
  MPI_Comm tmp_comm;
  COUNT(C_COMM_SPLIT);
  MPI_Comm_split(MPI_COMM_WORLD, rank % 2, rank, &tmp_comm);
  double in = val(rank, it, 0, 9000), out = 0, expected = 0;
  COUNT(C_ALLREDUCE);
  MPI_Allreduce(&in, &out, 1, MPI_DOUBLE, MPI_SUM, tmp_comm);
  for (int r = rank % 2; r < size; r += 2) {
    expected += val(r, it, 0, 9000);
  }
  check(out, expected);
  COUNT(C_COMM_FREE);
  MPI_Comm_free(&tmp_comm);

  MPI_Datatype t;
  COUNT(C_TYPE_CREATE);
  MPI_Type_contiguous(3, MPI_DOUBLE, &t);
  MPI_Type_commit(&t);
  int tsize = 0;
  MPI_Type_size(t, &tsize);
  check(tsize, 3 * sizeof(double));
  double sbuf[3], rbuf[3];
  for (int j = 0; j < 3; j++) {
    sbuf[j] = val(rank, it, j, 9001);
  }
  COUNT(C_SENDRECV);
  MPI_Sendrecv(sbuf, 1, t, rank, 9001, rbuf, 1, t, rank, 9001,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  for (int j = 0; j < 3; j++) {
    check(rbuf[j], sbuf[j]);
  }
  COUNT(C_TYPE_FREE);
  MPI_Type_free(&t);
}

/************************************************************
 * Profile: halo
 ************************************************************/
static int halo_ndims, halo_dims[3];
static int halo_n;              // doubles per message (multiple of VEC_BLOCKS)
static double *halo_sbuf[6], *halo_rbuf[6];

static void
halo_setup(void)
{
  if (neighbors != 2 && neighbors != 4 && neighbors != 6) {
    neighbors = 6;
  }
  halo_ndims = neighbors / 2;
  factor_dims(size, halo_ndims, halo_dims);
  halo_n = msg_bytes / sizeof(double);
  halo_n = (halo_n + VEC_BLOCKS - 1) / VEC_BLOCKS * VEC_BLOCKS;
  for (int d = 0; d < neighbors; d++) {
    // Direction 0 sends from a strided buffer via vec_type.
    size_t n = (d == 0) ? (size_t)halo_n / VEC_BLOCKS * VEC_EXTENT : halo_n;
    halo_sbuf[d] = calloc(n, sizeof(double));
    halo_rbuf[d] = calloc(halo_n, sizeof(double));
  }
}

static void
halo_iteration(long it)
{
  MPI_Request reqs[12];
  int nreq = 0;
  int npairs = halo_n / 2;
  // 1. Halo exchange on the dup'd communicator.
  for (int d = 0; d < neighbors; d++) {
    int dim = d / 2, sign = (d % 2 == 0) ? 1 : -1;
    int src = torus_neighbor(rank, halo_dims, halo_ndims, dim, -sign);
    COUNT(C_IRECV);
    if (d == 1) {
      MPI_Irecv(halo_rbuf[d], npairs, pair_type, src, 100 + d, dup_comm,
                &reqs[nreq++]);
    } else {
      MPI_Irecv(halo_rbuf[d], halo_n, MPI_DOUBLE, src, 100 + d, dup_comm,
                &reqs[nreq++]);
    }
  }
  for (int d = 0; d < neighbors; d++) {
    int dim = d / 2, sign = (d % 2 == 0) ? 1 : -1;
    int dest = torus_neighbor(rank, halo_dims, halo_ndims, dim, sign);
    COUNT(C_ISEND);
    if (d == 0) {
      // Strided send: element e of vec_type covers doubles e*VEC_EXTENT+2i.
      for (int e = 0; e < halo_n / VEC_BLOCKS; e++) {
        for (int i = 0; i < VEC_BLOCKS; i++) {
          halo_sbuf[d][e * VEC_EXTENT + 2 * i] =
            val(rank, it, e * VEC_BLOCKS + i, 100 + d);
        }
      }
      MPI_Isend(halo_sbuf[d], halo_n / VEC_BLOCKS, vec_type, dest, 100 + d,
                dup_comm, &reqs[nreq++]);
    } else if (d == 1) {
      pair_t *p = (pair_t *)halo_sbuf[d];
      for (int j = 0; j < npairs; j++) {
        p[j].i = (int)val(rank, it, 2 * j, 100 + d);
        p[j].d = val(rank, it, 2 * j + 1, 100 + d);
      }
      MPI_Isend(p, npairs, pair_type, dest, 100 + d, dup_comm, &reqs[nreq++]);
    } else {
      for (int j = 0; j < halo_n; j++) {
        halo_sbuf[d][j] = val(rank, it, j, 100 + d);
      }
      MPI_Isend(halo_sbuf[d], halo_n, MPI_DOUBLE, dest, 100 + d, dup_comm,
                &reqs[nreq++]);
    }
  }
  COUNT(C_WAITALL);
  MPI_Waitall(nreq, reqs, MPI_STATUSES_IGNORE);
  for (int d = 0; d < neighbors; d++) {
    int dim = d / 2, sign = (d % 2 == 0) ? 1 : -1;
    int src = torus_neighbor(rank, halo_dims, halo_ndims, dim, -sign);
    if (d == 1) {
      pair_t *p = (pair_t *)halo_rbuf[d];
      for (int j = 0; j < npairs; j++) {
        check(p[j].i, (int)val(src, it, 2 * j, 100 + d));
        check(p[j].d, val(src, it, 2 * j + 1, 100 + d));
      }
    } else {
      for (int j = 0; j < halo_n; j++) {
        check(halo_rbuf[d][j], val(src, it, j, 100 + d));
      }
    }
  }

  // 2. A small Allreduce on the row communicator.
  double in[4], out[4];
  for (int j = 0; j < 4; j++) {
    in[j] = val(rank, it, j, 150);
  }
  COUNT(C_ALLREDUCE);
  MPI_Allreduce(in, out, 4, MPI_DOUBLE, MPI_SUM, row_comm);
  for (int j = 0; j < 4; j++) {
    double expected = 0;
    for (int m = 0; m < py; m++) {
      expected += val(row_member(rank, m), it, j, 150);
    }
    check(out[j], expected);
  }

  // 3. A non-blocking collective overlapped with the compute step.
  double nbc[16], nbc_out[16];
  MPI_Request req;
  int row_rank = rank % py;
  int root = (int)(it % py);
  if (it % 2 == 0) {
    for (int j = 0; j < 16; j++) {
      nbc[j] = (row_rank == root) ? val(rank, it, j, 160) : -1;
    }
    COUNT(C_IBCAST);
    MPI_Ibcast(nbc, 16, MPI_DOUBLE, root, row_comm, &req);
  } else {
    for (int j = 0; j < 16; j++) {
      nbc[j] = val(rank, it, j, 161);
    }
    COUNT(C_IREDUCE);
    MPI_Ireduce(nbc, nbc_out, 16, MPI_DOUBLE, MPI_SUM, root, row_comm, &req);
  }
  compute(compute_iters_per_step);
  COUNT(C_WAIT);
  MPI_Wait(&req, MPI_STATUS_IGNORE);
  if (it % 2 == 0) {
    for (int j = 0; j < 16; j++) {
      check(nbc[j], val(row_member(rank, root), it, j, 160));
    }
  } else if (row_rank == root) {
    for (int j = 0; j < 16; j++) {
      double expected = 0;
      for (int m = 0; m < py; m++) {
        expected += val(row_member(rank, m), it, j, 161);
      }
      check(nbc_out[j], expected);
    }
  }
}

/************************************************************
 * Profile: vasp
 ************************************************************/
static double *a2a_sbuf, *a2a_rbuf;
static int *a2a_scounts, *a2a_sdispls, *a2a_rcounts, *a2a_rdispls;
static double *ag_rbuf;
static int *ag_counts, *ag_displs;

static void
vasp_setup(void)
{
  int base = a2a_bytes / sizeof(double);
  size_t max_count = base / 2 + 3 * (base / 4) + 1;
  a2a_sbuf = malloc(sizeof(double) * max_count * px);
  a2a_rbuf = malloc(sizeof(double) * max_count * px);
  a2a_scounts = malloc(sizeof(int) * px);
  a2a_sdispls = malloc(sizeof(int) * px);
  a2a_rcounts = malloc(sizeof(int) * px);
  a2a_rdispls = malloc(sizeof(int) * px);
  ag_rbuf = malloc(sizeof(double) * 4 * py);
  ag_counts = malloc(sizeof(int) * py);
  ag_displs = malloc(sizeof(int) * py);
}

// Uneven Alltoallv count from column index s to column index d.
static int
a2a_count(int s, int d, long it)
{
  int base = a2a_bytes / sizeof(double);
  return base / 2 + (int)((s * 3 + d * 5 + it) % 4) * (base / 4);
}

static void
vasp_iteration(long it)
{
  // 1. Many small Allreduces on world, row and column communicators.
  MPI_Comm comms[3] = { MPI_COMM_WORLD, row_comm, col_comm };
  int comm_sizes[3] = { size, py, px };
  MPI_Op ops[3] = { MPI_SUM, MPI_MAX, user_op };
  double in[16], out[16];
  for (int k = 0; k < allreduce_count; k++) {
    int c = k % 3, o = (k / 3) % 3;
    int count = 1 + (k * 7) % 16;
    for (int j = 0; j < count; j++) {
      in[j] = val(rank, it, j, 1000 + k);
    }
    COUNT(C_ALLREDUCE);
    MPI_Allreduce(in, out, count, MPI_DOUBLE, ops[o], comms[c]);
    for (int j = 0; j < count; j++) {
      double expected = (o == 1) ? -1 : 0;
      for (int m = 0; m < comm_sizes[c]; m++) {
        int r = (c == 0) ? m : (c == 1) ? row_member(rank, m)
                                        : col_member(rank, m);
        double v = val(r, it, j, 1000 + k);
        if (o == 1) {
          expected = (v > expected) ? v : expected;
        } else {
          expected += v;
        }
      }
      check(out[j], expected);
    }
  }

  // 2. Two Bcasts from different roots on the row communicator.
  double bbuf[512];
  int bcounts[2] = { 8, 512 };  // 64 B and 4 KiB
  for (int b = 0; b < 2; b++) {
    int root = (int)((it + b) % py);
    int root_world = row_member(rank, root);
    for (int j = 0; j < bcounts[b]; j++) {
      bbuf[j] = (rank == root_world) ? val(rank, it, j, 2000 + b) : -1;
    }
    COUNT(C_BCAST);
    MPI_Bcast(bbuf, bcounts[b], MPI_DOUBLE, root, row_comm);
    for (int j = 0; j < bcounts[b]; j++) {
      check(bbuf[j], val(root_world, it, j, 2000 + b));
    }
  }

  // 3. One Alltoallv with uneven counts on the column communicator.
  int me = rank / py;
  int soff = 0, roff = 0;
  for (int p = 0; p < px; p++) {
    a2a_scounts[p] = a2a_count(me, p, it);
    a2a_rcounts[p] = a2a_count(p, me, it);
    a2a_sdispls[p] = soff;
    a2a_rdispls[p] = roff;
    for (int j = 0; j < a2a_scounts[p]; j++) {
      a2a_sbuf[soff + j] = val(rank, it, j, 3000 + p);
    }
    soff += a2a_scounts[p];
    roff += a2a_rcounts[p];
  }
  COUNT(C_ALLTOALLV);
  MPI_Alltoallv(a2a_sbuf, a2a_scounts, a2a_sdispls, MPI_DOUBLE,
                a2a_rbuf, a2a_rcounts, a2a_rdispls, MPI_DOUBLE, col_comm);
  for (int p = 0; p < px; p++) {
    for (int j = 0; j < a2a_rcounts[p]; j++) {
      check(a2a_rbuf[a2a_rdispls[p] + j], val(col_member(rank, p), it, j,
                                              3000 + me));
    }
  }

  // 4. One Allgatherv of small, uneven blocks on the row communicator.
  int row_rank = rank % py;
  double ag_in[4];
  int off = 0;
  for (int m = 0; m < py; m++) {
    ag_counts[m] = 1 + (int)((m + it) % 4);
    ag_displs[m] = off;
    off += ag_counts[m];
  }
  for (int j = 0; j < ag_counts[row_rank]; j++) {
    ag_in[j] = val(rank, it, j, 4000);
  }
  COUNT(C_ALLGATHERV);
  MPI_Allgatherv(ag_in, ag_counts[row_rank], MPI_DOUBLE,
                 ag_rbuf, ag_counts, ag_displs, MPI_DOUBLE, row_comm);
  for (int m = 0; m < py; m++) {
    for (int j = 0; j < ag_counts[m]; j++) {
      check(ag_rbuf[ag_displs[m] + j], val(row_member(rank, m), it, j, 4000));
    }
  }

  // 5. Little point-to-point: one Sendrecv around a world ring.
  double sbuf[8], rbuf[8];
  int right = (rank + 1) % size, left = (rank - 1 + size) % size;
  for (int j = 0; j < 8; j++) {
    sbuf[j] = val(rank, it, j, 5000);
  }
  COUNT(C_SENDRECV);
  MPI_Sendrecv(sbuf, 8, MPI_DOUBLE, right, 500, rbuf, 8, MPI_DOUBLE, left, 500,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  for (int j = 0; j < 8; j++) {
    check(rbuf[j], val(left, it, j, 5000));
  }

  compute(compute_iters_per_step);
}

/************************************************************
 * Profile: gromacs
 ************************************************************/
#define DD_MAX_DOUBLES (8 * 1024 / 8)
#define COORD_HALO_DOUBLES 32     // 256 B per neighbour
#define PME_DOUBLES 256           // 2 KiB coordinates / forces
#define PME_A2A_MAX_DOUBLES (4 * 1024 / 8)

static int is_pme, npme, npp;
static int pp_rank, pme_rank;      // rank in pp_comm or pme_comm
static int *pme_ranks, *pp_ranks;  // world ranks, in comm order
static int my_pme;                 // world rank of this PP rank's PME rank
static int my_npp_clients;         // PP ranks served by this PME rank
static int *my_pp_clients;         // their world ranks
static MPI_Comm pp_comm, pme_comm;
static int pp_dims[3];
static double *gmx_buf, *gmx_buf2;

static int
rank_is_pme(int r)
{
  if (size >= 4) {
    return r % 4 == 3;
  }
  return size >= 2 && r == size - 1;
}

static void
gromacs_setup(void)
{
  is_pme = rank_is_pme(rank);
  pme_ranks = malloc(sizeof(int) * size);
  pp_ranks = malloc(sizeof(int) * size);
  npme = npp = 0;
  for (int r = 0; r < size; r++) {
    if (rank_is_pme(r)) {
      pme_ranks[npme++] = r;
    } else {
      pp_ranks[npp++] = r;
    }
  }
  COUNT(C_COMM_SPLIT);
  MPI_Comm_split(MPI_COMM_WORLD, is_pme, rank,
                 is_pme ? &pme_comm : &pp_comm);
  MPI_Comm_rank(is_pme ? pme_comm : pp_comm, is_pme ? &pme_rank : &pp_rank);
  factor_dims(npp, 3, pp_dims);
  my_pme = -1;
  my_npp_clients = 0;
  my_pp_clients = malloc(sizeof(int) * size);
  for (int i = 0; i < npp; i++) {
    if (npme == 0) {
      break;
    }
    int pme = pme_ranks[i % npme];
    if (pp_ranks[i] == rank) {
      my_pme = pme;
    }
    if (pme == rank) {
      my_pp_clients[my_npp_clients++] = pp_ranks[i];
    }
  }
  size_t n = 2 * DD_MAX_DOUBLES + 6 * COORD_HALO_DOUBLES * 2 +
             (size_t)PME_DOUBLES * size * 2 + PME_A2A_MAX_DOUBLES * size;
  gmx_buf = calloc(n, sizeof(double));
  gmx_buf2 = calloc(n, sizeof(double));
}

static void
gromacs_pp_iteration(long it)
{
  double *coords = gmx_buf2;
  double *forces = gmx_buf2 + PME_DOUBLES;
  MPI_Request pme_reqs[2];

  // Send coordinates to our PME rank and post the receive for forces.
  if (my_pme >= 0) {
    COUNT(C_IRECV);
    MPI_Irecv(forces, PME_DOUBLES, MPI_DOUBLE, my_pme, 201, MPI_COMM_WORLD,
              &pme_reqs[0]);
    for (int j = 0; j < PME_DOUBLES; j++) {
      coords[j] = val(rank, it, j, 7000);
    }
    COUNT(C_ISEND);
    MPI_Isend(coords, PME_DOUBLES, MPI_DOUBLE, my_pme, 200, MPI_COMM_WORLD,
              &pme_reqs[1]);
  }

  // Domain-decomposition halo: one Sendrecv per direction per dimension.
  double *sbuf = gmx_buf, *rbuf = gmx_buf + DD_MAX_DOUBLES;
  for (int dim = 0; dim < 3; dim++) {
    int n = 1024 * (1 + (int)((dim * 3 + it) % 8)) / sizeof(double);
    for (int dir = 0; dir < 2; dir++) {
      int sign = dir == 0 ? 1 : -1;
      int dest = torus_neighbor(pp_rank, pp_dims, 3, dim, sign);
      int src = torus_neighbor(pp_rank, pp_dims, 3, dim, -sign);
      int salt = 6000 + 2 * dim + dir;
      for (int j = 0; j < n; j++) {
        sbuf[j] = val(rank, it, j, salt);
      }
      COUNT(C_SENDRECV);
      MPI_Sendrecv(sbuf, n, MPI_DOUBLE, dest, 400 + 2 * dim + dir,
                   rbuf, n, MPI_DOUBLE, src, 400 + 2 * dim + dir,
                   pp_comm, MPI_STATUS_IGNORE);
      for (int j = 0; j < n; j++) {
        check(rbuf[j], val(pp_ranks[src], it, j, salt));
      }
    }
  }

  // Coordinate halo: Irecv + Isend + Waitall with the 6 torus neighbours.
  MPI_Request reqs[12];
  int nreq = 0;
  double *hs = gmx_buf + 2 * DD_MAX_DOUBLES;
  double *hr = hs + 6 * COORD_HALO_DOUBLES;
  for (int d = 0; d < 6; d++) {
    int src = torus_neighbor(pp_rank, pp_dims, 3, d / 2, d % 2 ? 1 : -1);
    COUNT(C_IRECV);
    MPI_Irecv(hr + d * COORD_HALO_DOUBLES, COORD_HALO_DOUBLES, MPI_DOUBLE,
              src, 300 + d, pp_comm, &reqs[nreq++]);
  }
  for (int d = 0; d < 6; d++) {
    int dest = torus_neighbor(pp_rank, pp_dims, 3, d / 2, d % 2 ? -1 : 1);
    for (int j = 0; j < COORD_HALO_DOUBLES; j++) {
      hs[d * COORD_HALO_DOUBLES + j] = val(rank, it, j, 6100 + d);
    }
    COUNT(C_ISEND);
    MPI_Isend(hs + d * COORD_HALO_DOUBLES, COORD_HALO_DOUBLES, MPI_DOUBLE,
              dest, 300 + d, pp_comm, &reqs[nreq++]);
  }
  COUNT(C_WAITALL);
  MPI_Waitall(nreq, reqs, MPI_STATUSES_IGNORE);
  for (int d = 0; d < 6; d++) {
    int src = torus_neighbor(pp_rank, pp_dims, 3, d / 2, d % 2 ? 1 : -1);
    for (int j = 0; j < COORD_HALO_DOUBLES; j++) {
      check(hr[d * COORD_HALO_DOUBLES + j], val(pp_ranks[src], it, j,
                                                6100 + d));
    }
  }

  compute(compute_iters_per_step);

  // Receive the PME forces.
  if (my_pme >= 0) {
    COUNT(C_WAITALL);
    MPI_Waitall(2, pme_reqs, MPI_STATUSES_IGNORE);
    for (int j = 0; j < PME_DOUBLES; j++) {
      check(forces[j], val(my_pme, it, j, 7100 + rank));
    }
  }
}

static void
gromacs_pme_iteration(long it)
{
  MPI_Request *reqs = malloc(sizeof(MPI_Request) * (my_npp_clients + 1));
  double *coords = gmx_buf;
  double *forces = gmx_buf + (size_t)PME_DOUBLES * size;
  double *a2a_s = gmx_buf + (size_t)PME_DOUBLES * size * 2;
  double *a2a_r = gmx_buf2;

  // Receive coordinates from our PP ranks.
  for (int c = 0; c < my_npp_clients; c++) {
    COUNT(C_IRECV);
    MPI_Irecv(coords + c * PME_DOUBLES, PME_DOUBLES, MPI_DOUBLE,
              my_pp_clients[c], 200, MPI_COMM_WORLD, &reqs[c]);
  }
  COUNT(C_WAITALL);
  MPI_Waitall(my_npp_clients, reqs, MPI_STATUSES_IGNORE);
  for (int c = 0; c < my_npp_clients; c++) {
    for (int j = 0; j < PME_DOUBLES; j++) {
      check(coords[c * PME_DOUBLES + j], val(my_pp_clients[c], it, j, 7000));
    }
  }

  // Alltoall among the PME ranks.
  int n = 1024 * (1 + (int)(it % 4)) / sizeof(double);
  for (int p = 0; p < npme; p++) {
    for (int j = 0; j < n; j++) {
      a2a_s[p * n + j] = val(rank, it, j, 7200 + p);
    }
  }
  COUNT(C_ALLTOALL);
  MPI_Alltoall(a2a_s, n, MPI_DOUBLE, a2a_r, n, MPI_DOUBLE, pme_comm);
  for (int p = 0; p < npme; p++) {
    for (int j = 0; j < n; j++) {
      check(a2a_r[p * n + j], val(pme_ranks[p], it, j, 7200 + pme_rank));
    }
  }

  compute(compute_iters_per_step);

  // Send forces back to our PP ranks.
  for (int c = 0; c < my_npp_clients; c++) {
    for (int j = 0; j < PME_DOUBLES; j++) {
      forces[c * PME_DOUBLES + j] = val(rank, it, j, 7100 + my_pp_clients[c]);
    }
    COUNT(C_ISEND);
    MPI_Isend(forces + c * PME_DOUBLES, PME_DOUBLES, MPI_DOUBLE,
              my_pp_clients[c], 201, MPI_COMM_WORLD, &reqs[c]);
  }
  COUNT(C_WAITALL);
  MPI_Waitall(my_npp_clients, reqs, MPI_STATUSES_IGNORE);
  free(reqs);
}

static void
gromacs_iteration(long it)
{
  if (is_pme) {
    gromacs_pme_iteration(it);
  } else {
    gromacs_pp_iteration(it);
  }
  // Energies: an Allreduce of about 10 doubles on world.
  if (it % energy_every == 0) {
    double in[10], out[10];
    for (int j = 0; j < 10; j++) {
      in[j] = val(rank, it, j, 8000);
    }
    COUNT(C_ALLREDUCE);
    MPI_Allreduce(in, out, 10, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    for (int j = 0; j < 10; j++) {
      double expected = 0;
      for (int r = 0; r < size; r++) {
        expected += val(r, it, j, 8000);
      }
      check(out[j], expected);
    }
  }
}

static void
gromacs_teardown(void)
{
  COUNT(C_COMM_FREE);
  MPI_Comm_free(is_pme ? &pme_comm : &pp_comm);
}

/************************************************************
 * Main
 ************************************************************/
static void
parse_args(int argc, char **argv)
{
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (strncmp(a, "--profile=", 10) == 0) {
      if (strcmp(a + 10, "halo") == 0) {
        profile = HALO;
      } else if (strcmp(a + 10, "vasp") == 0) {
        profile = VASP;
      } else if (strcmp(a + 10, "gromacs") == 0) {
        profile = GROMACS;
      } else {
        fprintf(stderr, "Unknown profile: %s\n", a + 10);
        exit(1);
      }
    } else if (strncmp(a, "--iters=", 8) == 0) {
      iters = atol(a + 8);
    } else if (strncmp(a, "--compute-us=", 13) == 0) {
      compute_us = atof(a + 13);
    } else if (strncmp(a, "--msg=", 6) == 0) {
      msg_bytes = atoi(a + 6);
    } else if (strncmp(a, "--neighbors=", 12) == 0) {
      neighbors = atoi(a + 12);
    } else if (strncmp(a, "--allreduce-count=", 18) == 0) {
      allreduce_count = atoi(a + 18);
    } else if (strncmp(a, "--a2a-bytes=", 12) == 0) {
      a2a_bytes = atoi(a + 12);
    } else if (strncmp(a, "--energy-every=", 15) == 0) {
      energy_every = atoi(a + 15);
    } else if (strcmp(a, "--churn") == 0) {
      churn = 10;
    } else if (strncmp(a, "--churn=", 8) == 0) {
      churn = atoi(a + 8);
    } else {
      fprintf(stderr, "Unknown argument: %s\n", a);
      exit(1);
    }
  }
  if (energy_every < 1) {
    energy_every = 1;
  }
  if (a2a_bytes < 32) {
    a2a_bytes = 32;
  }
  if (msg_bytes < 16) {
    msg_bytes = 16;
  }
}

int
main(int argc, char **argv)
{
  parse_args(argc, argv);
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  calibrate_compute();
  common_setup();
  switch (profile) {
    case HALO: halo_setup(); break;
    case VASP: vasp_setup(); break;
    case GROMACS: gromacs_setup(); break;
  }
  memset(calls, 0, sizeof(calls));

  MPI_Barrier(MPI_COMM_WORLD);
  double t0 = now_s();
  for (long it = 0; it < iters; it++) {
    switch (profile) {
      case HALO: halo_iteration(it); break;
      case VASP: vasp_iteration(it); break;
      case GROMACS: gromacs_iteration(it); break;
    }
    if (churn > 0 && it % churn == churn - 1) {
      do_churn(it);
    }
  }
  double loop_time = now_s() - t0;

  double max_time;
  MPI_Reduce(&loop_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0,
             MPI_COMM_WORLD);
  uint64_t global_checksum = 0, global_errors = 0;
  MPI_Allreduce(&checksum, &global_checksum, 1, MPI_UINT64_T, MPI_SUM,
                MPI_COMM_WORLD);
  MPI_Allreduce(&errors, &global_errors, 1, MPI_UINT64_T, MPI_SUM,
                MPI_COMM_WORLD);

  // Calls per second of main-loop time, per rank.
  double rates[C_NCALLS], sum_rates[C_NCALLS], max_rates[C_NCALLS];
  for (int c = 0; c < C_NCALLS; c++) {
    rates[c] = calls[c] / loop_time;
  }
  MPI_Reduce(rates, sum_rates, C_NCALLS, MPI_DOUBLE, MPI_SUM, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(rates, max_rates, C_NCALLS, MPI_DOUBLE, MPI_MAX, 0,
             MPI_COMM_WORLD);

  if (rank == 0) {
    printf("synthetic_app profile=%s ranks=%d iters=%ld compute_us=%g "
           "churn=%d time_s=%.4f iters_per_s=%.1f checksum=0x%016llx "
           "errors=%llu %s\n",
           profile_names[profile], size, iters, compute_us, churn, max_time,
           iters / max_time, (unsigned long long)global_checksum,
           (unsigned long long)global_errors,
           global_errors == 0 ? "PASS" : "FAIL");
    for (int c = 0; c < C_NCALLS; c++) {
      if (max_rates[c] > 0) {
        printf("calls_per_s,%s,%.1f,%.1f\n", call_names[c],
               sum_rates[c] / size, max_rates[c]);
      }
    }
    fflush(stdout);
  }

  if (profile == GROMACS) {
    gromacs_teardown();
  }
  common_teardown();
  MPI_Finalize();
  return global_errors != 0;
}
