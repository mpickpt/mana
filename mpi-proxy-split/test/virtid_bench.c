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
  Microbenchmark of the per-call cost that MANA adds to wrapped MPI calls.
  Run it natively (virtid_bench.exe) and under MANA (virtid_bench.mana.exe)
  and subtract.

  Groups:
    a: handle translation, meant for 1 rank: MPI_Comm_size on
       MPI_COMM_WORLD and on a dup'd comm, MPI_Type_size on a derived
       datatype, and MPI_Isend/MPI_Irecv to self + MPI_Waitall.
    b: small blocking collectives, all ranks.
    c: MPI_Sendrecv: exchange latency with 2 ranks, or a 1D ring exchange
       with both neighbours with more ranks.  Also timed as
       Isend+Irecv+Waitall and as MPI_Sendrecv_replace.

  Usage:
    virtid_bench [--group=a,b,c] [--iters=N] [--coll-iters=N]
                 [--p2p-iters=N] [--warmup=N] [--only=SUBSTRING]

  Output: one CSV line per test, printed by rank 0:
    RESULT,<group>,<test>,<mean over ranks>,<unit>,<max over ranks>
  Timing uses clock_gettime(), not the wrapped MPI_Wtime.
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int rank, size;
static long iters_a = 10000000;
static long iters_coll = 100000;
static long iters_p2p = 100000;
static long warmup = 1000;
static const char *only = NULL;
static int errors = 0;

// True if the test 'name' was selected with --only=<substring>.
static int
selected(const char *name)
{
  return only == NULL || strstr(name, only) != NULL;
}

static double
now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e9 + ts.tv_nsec;
}

// Reports the mean and the max over all ranks of a per-call time.
static void
report(const char *group, const char *test, double per_call, const char *unit)
{
  double sum = 0, max = 0;
  MPI_Reduce(&per_call, &sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&per_call, &max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  if (rank == 0) {
    printf("RESULT,%s,%s,%.4f,%s,%.4f\n", group, test, sum / size, unit, max);
    fflush(stdout);
  }
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
 * Group a: handle translation
 ************************************************************/
static void
group_a(void)
{
  if (size != 1) {
    if (rank == 0) {
      printf("# group a skipped: it is meant for 1 rank\n");
    }
    return;
  }
  MPI_Comm dup_comm;
  MPI_Datatype vec;
  int s = 0;
  double t;
  long n = iters_a;

  MPI_Comm_dup(MPI_COMM_WORLD, &dup_comm);
  MPI_Type_vector(4, 1, 2, MPI_DOUBLE, &vec);
  MPI_Type_commit(&vec);

  for (long i = 0; i < warmup; i++) {
    MPI_Comm_size(MPI_COMM_WORLD, &s);
  }
  if (selected("comm_size_world")) {
    t = now_ns();
    for (long i = 0; i < n; i++) {
      MPI_Comm_size(MPI_COMM_WORLD, &s);
    }
    report("a", "comm_size_world", (now_ns() - t) / n, "ns");
    if (s != 1) errors++;
  }

  if (selected("comm_size_dup")) {
    t = now_ns();
    for (long i = 0; i < n; i++) {
      MPI_Comm_size(dup_comm, &s);
    }
    report("a", "comm_size_dup", (now_ns() - t) / n, "ns");
    if (s != 1) errors++;
  }

  if (selected("type_size_derived")) {
    t = now_ns();
    for (long i = 0; i < n; i++) {
      MPI_Type_size(vec, &s);
    }
    report("a", "type_size_derived", (now_ns() - t) / n, "ns");
    if (s != 4 * sizeof(double)) errors++;
  }

  // Request creation, completion and freeing: 10x fewer iterations.
  long m = n / 10;
  int sbuf = 0, rbuf = -1;
  MPI_Request reqs[2];
  for (long i = 0; i < warmup; i++) {
    MPI_Irecv(&rbuf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &reqs[0]);
    MPI_Isend(&sbuf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &reqs[1]);
    MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
  }
  if (selected("isend_irecv_waitall_self")) {
    t = now_ns();
    for (long i = 0; i < m; i++) {
      sbuf = (int)i;
      MPI_Irecv(&rbuf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &reqs[0]);
      MPI_Isend(&sbuf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &reqs[1]);
      MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
    }
    report("a", "isend_irecv_waitall_self", (now_ns() - t) / m, "ns");
    if (rbuf != (int)(m - 1)) errors++;
    if (reqs[0] != MPI_REQUEST_NULL || reqs[1] != MPI_REQUEST_NULL) errors++;
  }

  MPI_Type_free(&vec);
  MPI_Comm_free(&dup_comm);
}

/************************************************************
 * Group b: small blocking collectives
 ************************************************************/
static MPI_Comm dup_comm, row_comm, a2a_comm;
static MPI_Op user_op;

static double
time_allreduce(int count, MPI_Comm comm, MPI_Op op)
{
  double in[8], out[8];
  for (int i = 0; i < 8; i++) {
    in[i] = rank + i;
  }
  for (long i = 0; i < warmup; i++) {
    MPI_Allreduce(in, out, count, MPI_DOUBLE, op, comm);
  }
  MPI_Barrier(comm);
  double t = now_ns();
  for (long i = 0; i < iters_coll; i++) {
    MPI_Allreduce(in, out, count, MPI_DOUBLE, op, comm);
  }
  t = (now_ns() - t) / iters_coll / 1000.0;
  int csize;
  MPI_Comm_size(comm, &csize);
  if (out[0] < 0 || (count == 1 && csize == 1 && out[0] != in[0])) errors++;
  return t;
}

static double
time_bcast(int bytes)
{
  char buf[1024];
  memset(buf, rank == 0 ? 7 : 0, sizeof(buf));
  for (long i = 0; i < warmup; i++) {
    MPI_Bcast(buf, bytes, MPI_BYTE, 0, MPI_COMM_WORLD);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  double t = now_ns();
  for (long i = 0; i < iters_coll; i++) {
    MPI_Bcast(buf, bytes, MPI_BYTE, 0, MPI_COMM_WORLD);
  }
  t = (now_ns() - t) / iters_coll / 1000.0;
  if (buf[bytes - 1] != 7) errors++;
  return t;
}

static double
time_alltoall(int bytes, int use_v)
{
  int csize, crank;
  MPI_Comm_size(a2a_comm, &csize);
  MPI_Comm_rank(a2a_comm, &crank);
  char *sbuf = malloc((size_t)bytes * csize);
  char *rbuf = malloc((size_t)bytes * csize);
  int *counts = malloc(sizeof(int) * csize);
  int *displs = malloc(sizeof(int) * csize);
  for (int p = 0; p < csize; p++) {
    memset(sbuf + (size_t)p * bytes, crank, bytes);
    counts[p] = bytes;
    displs[p] = p * bytes;
  }
  long n = iters_coll;
  if (bytes > 64) {
    n /= 4;
  }
  for (long i = 0; i < warmup; i++) {
    if (use_v) {
      MPI_Alltoallv(sbuf, counts, displs, MPI_BYTE,
                    rbuf, counts, displs, MPI_BYTE, a2a_comm);
    } else {
      MPI_Alltoall(sbuf, bytes, MPI_BYTE, rbuf, bytes, MPI_BYTE, a2a_comm);
    }
  }
  MPI_Barrier(a2a_comm);
  double t = now_ns();
  for (long i = 0; i < n; i++) {
    if (use_v) {
      MPI_Alltoallv(sbuf, counts, displs, MPI_BYTE,
                    rbuf, counts, displs, MPI_BYTE, a2a_comm);
    } else {
      MPI_Alltoall(sbuf, bytes, MPI_BYTE, rbuf, bytes, MPI_BYTE, a2a_comm);
    }
  }
  t = (now_ns() - t) / n / 1000.0;
  for (int p = 0; p < csize; p++) {
    if (rbuf[(size_t)p * bytes] != (char)p) errors++;
  }
  free(sbuf);
  free(rbuf);
  free(counts);
  free(displs);
  return t;
}

static double
time_allgather(void)
{
  double in = rank;
  double *out = malloc(sizeof(double) * size);
  for (long i = 0; i < warmup; i++) {
    MPI_Allgather(&in, 1, MPI_DOUBLE, out, 1, MPI_DOUBLE, MPI_COMM_WORLD);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  double t = now_ns();
  for (long i = 0; i < iters_coll; i++) {
    MPI_Allgather(&in, 1, MPI_DOUBLE, out, 1, MPI_DOUBLE, MPI_COMM_WORLD);
  }
  t = (now_ns() - t) / iters_coll / 1000.0;
  for (int p = 0; p < size; p++) {
    if (out[p] != p) errors++;
  }
  free(out);
  return t;
}

static double
time_barrier(void)
{
  for (long i = 0; i < warmup; i++) {
    MPI_Barrier(MPI_COMM_WORLD);
  }
  double t = now_ns();
  for (long i = 0; i < iters_coll; i++) {
    MPI_Barrier(MPI_COMM_WORLD);
  }
  return (now_ns() - t) / iters_coll / 1000.0;
}

static void
group_b(void)
{
  // 2D process grid px * py, px <= py.  Rows have py ranks.
  int px = 1;
  for (int d = 1; d * d <= size; d++) {
    if (size % d == 0) {
      px = d;
    }
  }
  int py = size / px;
  MPI_Comm_dup(MPI_COMM_WORLD, &dup_comm);
  MPI_Comm_split(MPI_COMM_WORLD, rank / py, rank, &row_comm);
  // A sub-communicator of up to 16 ranks for MPI_Alltoall(v).
  MPI_Comm_split(MPI_COMM_WORLD, rank / 16, rank, &a2a_comm);
  MPI_Op_create(user_sum, 1, &user_op);

  const struct {
    const char *name;
    MPI_Comm *comm;
  } comms[] = {
    { "world", NULL }, { "dup", &dup_comm }, { "row", &row_comm },
  };
  char test[128];
  for (int c = 0; c < 3; c++) {
    MPI_Comm comm = comms[c].comm ? *comms[c].comm : MPI_COMM_WORLD;
    for (int useop = 0; useop < 2; useop++) {
      for (int count = 1; count <= 8; count += 7) {
        snprintf(test, sizeof(test), "allreduce_%dd_%s_%s", count,
                 comms[c].name, useop ? "userop" : "sum");
        if (!selected(test)) continue;
        report("b", test,
               time_allreduce(count, comm, useop ? user_op : MPI_SUM), "us");
      }
    }
  }
  if (selected("bcast_8B")) report("b", "bcast_8B", time_bcast(8), "us");
  if (selected("bcast_1KiB")) report("b", "bcast_1KiB", time_bcast(1024), "us");
  if (selected("alltoall_8B")) {
    report("b", "alltoall_8B", time_alltoall(8, 0), "us");
  }
  if (selected("alltoall_1KiB")) {
    report("b", "alltoall_1KiB", time_alltoall(1024, 0), "us");
  }
  if (selected("alltoallv_8B")) {
    report("b", "alltoallv_8B", time_alltoall(8, 1), "us");
  }
  if (selected("alltoallv_1KiB")) {
    report("b", "alltoallv_1KiB", time_alltoall(1024, 1), "us");
  }
  if (selected("allgather_8B")) {
    report("b", "allgather_8B", time_allgather(), "us");
  }
  if (selected("barrier")) report("b", "barrier", time_barrier(), "us");

  MPI_Op_free(&user_op);
  MPI_Comm_free(&a2a_comm);
  MPI_Comm_free(&row_comm);
  MPI_Comm_free(&dup_comm);
}

/************************************************************
 * Group c: MPI_Sendrecv
 ************************************************************/
enum { SENDRECV, ISEND_IRECV_WAITALL, SENDRECV_REPLACE };
static const char *variant_name[] = {
  "sendrecv", "isend_irecv_waitall", "sendrecv_replace"
};

// One exchange: send sbuf to 'dest', receive from 'src' into rbuf.
static void
exchange(int variant, char *sbuf, char *rbuf, int bytes, int dest, int src,
         int tag)
{
  MPI_Request reqs[2];
  switch (variant) {
    case SENDRECV:
      MPI_Sendrecv(sbuf, bytes, MPI_BYTE, dest, tag,
                   rbuf, bytes, MPI_BYTE, src, tag,
                   MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      break;
    case ISEND_IRECV_WAITALL:
      MPI_Irecv(rbuf, bytes, MPI_BYTE, src, tag, MPI_COMM_WORLD, &reqs[0]);
      MPI_Isend(sbuf, bytes, MPI_BYTE, dest, tag, MPI_COMM_WORLD, &reqs[1]);
      MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
      break;
    case SENDRECV_REPLACE:
      memcpy(rbuf, sbuf, bytes);
      MPI_Sendrecv_replace(rbuf, bytes, MPI_BYTE, dest, tag, src, tag,
                           MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      break;
  }
}

static void
group_c(void)
{
  static const int sizes[] = { 8, 1024, 16384 };
  char *sbuf = malloc(16384);
  char *rbuf = malloc(16384);
  char test[128];
  memset(sbuf, rank + 1, 16384);

  if (size == 2) {
    // Latency: both ranks exchange with each other.
    int peer = 1 - rank;
    for (int v = 0; v < 3; v++) {
      for (int s = 0; s < 3; s++) {
        int bytes = sizes[s];
        snprintf(test, sizeof(test), "pair_%s_%dB", variant_name[v], bytes);
        if (!selected(test)) continue;
        for (long i = 0; i < warmup; i++) {
          exchange(v, sbuf, rbuf, bytes, peer, peer, 1);
        }
        MPI_Barrier(MPI_COMM_WORLD);
        double t = now_ns();
        for (long i = 0; i < iters_p2p; i++) {
          exchange(v, sbuf, rbuf, bytes, peer, peer, 1);
        }
        t = (now_ns() - t) / iters_p2p / 1000.0;
        if (rbuf[bytes - 1] != (char)(peer + 1)) errors++;
        report("c", test, t, "us");
      }
    }
  } else if (size > 2) {
    // Ring: each rank exchanges with both neighbours per iteration.
    int right = (rank + 1) % size;
    int left = (rank - 1 + size) % size;
    for (int v = 0; v < 3; v++) {
      for (int s = 0; s < 3; s++) {
        int bytes = sizes[s];
        snprintf(test, sizeof(test), "ring_%s_%dB", variant_name[v], bytes);
        if (!selected(test)) continue;
        for (long i = 0; i < warmup; i++) {
          exchange(v, sbuf, rbuf, bytes, right, left, 2);
          exchange(v, sbuf, rbuf, bytes, left, right, 3);
        }
        MPI_Barrier(MPI_COMM_WORLD);
        double t = now_ns();
        for (long i = 0; i < iters_p2p; i++) {
          exchange(v, sbuf, rbuf, bytes, right, left, 2);
          if (rbuf[bytes - 1] != (char)(left + 1)) errors++;
          exchange(v, sbuf, rbuf, bytes, left, right, 3);
          if (rbuf[bytes - 1] != (char)(right + 1)) errors++;
        }
        t = (now_ns() - t) / iters_p2p / 1000.0;
        report("c", test, t, "us/iter");
      }
    }
  } else if (rank == 0) {
    printf("# group c skipped: it needs at least 2 ranks\n");
  }
  free(sbuf);
  free(rbuf);
}

int
main(int argc, char **argv)
{
  const char *groups = "a,b,c";
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--group=", 8) == 0) {
      groups = argv[i] + 8;
    } else if (strncmp(argv[i], "--iters=", 8) == 0) {
      iters_a = atol(argv[i] + 8);
    } else if (strncmp(argv[i], "--coll-iters=", 13) == 0) {
      iters_coll = atol(argv[i] + 13);
    } else if (strncmp(argv[i], "--p2p-iters=", 12) == 0) {
      iters_p2p = atol(argv[i] + 12);
    } else if (strncmp(argv[i], "--warmup=", 9) == 0) {
      warmup = atol(argv[i] + 9);
    } else if (strncmp(argv[i], "--only=", 7) == 0) {
      only = argv[i] + 7;
    } else {
      fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      return 1;
    }
  }

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (strchr(groups, 'a')) group_a();
  if (strchr(groups, 'b')) group_b();
  if (strchr(groups, 'c')) group_c();

  int total_errors = 0;
  MPI_Allreduce(&errors, &total_errors, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  if (rank == 0) {
    printf("virtid_bench: ranks=%d %s\n", size, total_errors ? "FAIL" : "PASS");
  }
  MPI_Finalize();
  return total_errors != 0;
}
