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

// Non-blocking collectives with rotating roots: MPI_Ibarrier, MPI_Ibcast,
// MPI_Ireduce, MPI_Iallreduce, MPI_Ireduce_scatter, MPI_Iscan,
// MPI_Ialltoall(v), MPI_Iallgather(v), MPI_Igather(v) and MPI_Iscatter(v).
// Each iteration completes them in another way: MPI_Wait at once; MPI_Wait
// after a sleep, so that a checkpoint finds them pending; MPI_Test until
// done; or all of them together with MPI_Waitall.  One rank per iteration is
// late, so the others wait for it to post.  The v-variants send 1 to MAXV
// words per rank, which change with the iteration.

#include "mana_test.h"

#define N 16    // Words per rank in the other calls
#define MAXV 3  // Words per rank in the v-variants: 1 to MAXV

enum { WAIT, SLEEP_WAIT, TEST, WAITALL, NMODES };

enum { IBARRIER, IBCAST, IREDUCE, IALLREDUCE, IREDUCE_SCATTER, ISCAN,
       IALLTOALL, IALLTOALLV, IALLGATHER, IALLGATHERV, IGATHER, IGATHERV,
       ISCATTER, ISCATTERV, NOPS };

static const char *names[NOPS] = {
  "ibarrier", "ibcast", "ireduce", "iallreduce", "ireduce_scatter", "iscan",
  "ialltoall", "ialltoallv", "iallgather", "iallgatherv", "igather",
  "igatherv", "iscatter", "iscatterv"
};

static long it;

// The buffers and count arrays of each call.  MPI reads the count arrays of
// a v-variant until the call completes.
static int *sendbuf[NOPS], *recvbuf[NOPS];
static int *scounts[NOPS], *sdispls[NOPS], *rcounts[NOPS], *rdispls[NOPS];
static long lsend[N], lrecv[N];  // MPI_Ireduce uses MPI_LONG

// The value that 'rank' sends at 'index' in call 'op'.
static int
value(int op, int rank, int index)
{
  return mt_value(rank, it, op * 1000 + index);
}

// The words that rank 'from' sends to rank 'to' in a v-variant.
static int
vcount(int from, int to)
{
  return 1 + (int)((from + 2 * to + it) % MAXV);
}

static int
root(int op)
{
  return (int)((it + op + it / NMODES) % mt_size);
}

// Sets counts[r] = count(r) and displs to their prefix sums.
static void
layout(int *counts, int *displs, int (*count)(int))
{
  for (int r = 0, d = 0; r < mt_size; d += counts[r], r++) {
    counts[r] = count(r);
    displs[r] = d;
  }
}

static int to_me(int r) { return vcount(r, mt_rank); }
static int from_me(int r) { return vcount(mt_rank, r); }
static int own(int r) { return vcount(r, r); }

static void
post(int op, MPI_Request *req)
{
  int *s = sendbuf[op], *r = recvbuf[op];
  int rt = root(op);
  for (int i = 0; i < mt_size * N; i++) {
    s[i] = value(op, mt_rank, i);
    r[i] = -1;
  }
  switch (op) {
  case IBARRIER:
    MT_MPI(MPI_Ibarrier(MPI_COMM_WORLD, req));
    break;
  case IBCAST:
    MT_MPI(MPI_Ibcast(mt_rank == rt ? s : r, N, MPI_INT, rt, MPI_COMM_WORLD,
                      req));
    break;
  case IREDUCE:
    for (int i = 0; i < N; i++) {
      lsend[i] = value(op, mt_rank, i);
      lrecv[i] = -1;
    }
    MT_MPI(MPI_Ireduce(lsend, lrecv, N, MPI_LONG, MPI_SUM, rt,
                       MPI_COMM_WORLD, req));
    break;
  case IALLREDUCE:
    MT_MPI(MPI_Iallreduce(s, r, N, MPI_INT, MPI_SUM, MPI_COMM_WORLD, req));
    break;
  case IREDUCE_SCATTER:
    layout(rcounts[op], rdispls[op], own);
    MT_MPI(MPI_Ireduce_scatter(s, r, rcounts[op], MPI_INT, MPI_SUM,
                               MPI_COMM_WORLD, req));
    break;
  case ISCAN:
    MT_MPI(MPI_Iscan(s, r, N, MPI_INT, MPI_SUM, MPI_COMM_WORLD, req));
    break;
  case IALLTOALL:
    MT_MPI(MPI_Ialltoall(s, N, MPI_INT, r, N, MPI_INT, MPI_COMM_WORLD, req));
    break;
  case IALLTOALLV:
    layout(scounts[op], sdispls[op], from_me);
    layout(rcounts[op], rdispls[op], to_me);
    MT_MPI(MPI_Ialltoallv(s, scounts[op], sdispls[op], MPI_INT, r,
                          rcounts[op], rdispls[op], MPI_INT, MPI_COMM_WORLD,
                          req));
    break;
  case IALLGATHER:
    MT_MPI(MPI_Iallgather(s, N, MPI_INT, r, N, MPI_INT, MPI_COMM_WORLD, req));
    break;
  case IALLGATHERV:
    layout(rcounts[op], rdispls[op], own);
    MT_MPI(MPI_Iallgatherv(s, own(mt_rank), MPI_INT, r, rcounts[op],
                           rdispls[op], MPI_INT, MPI_COMM_WORLD, req));
    break;
  case IGATHER:
    MT_MPI(MPI_Igather(s, N, MPI_INT, r, N, MPI_INT, rt, MPI_COMM_WORLD,
                       req));
    break;
  case IGATHERV:
    layout(rcounts[op], rdispls[op], own);
    MT_MPI(MPI_Igatherv(s, own(mt_rank), MPI_INT, r, rcounts[op],
                        rdispls[op], MPI_INT, rt, MPI_COMM_WORLD, req));
    break;
  case ISCATTER:
    MT_MPI(MPI_Iscatter(s, N, MPI_INT, r, N, MPI_INT, rt, MPI_COMM_WORLD,
                        req));
    break;
  case ISCATTERV:
    layout(scounts[op], sdispls[op], own);
    MT_MPI(MPI_Iscatterv(s, scounts[op], sdispls[op], MPI_INT, r,
                         own(mt_rank), MPI_INT, rt, MPI_COMM_WORLD, req));
    break;
  }
}

// Checks word i of call op: 'got' must be 'want'.
#define CHECK_WORD(got, want)                                                \
  MT_CHECK((got) == (want), "iteration %ld mode %ld %s word %d: %ld, not %ld", \
           it, it % NMODES, names[op], i, (long)(got), (long)(want))

static void
check(int op)
{
  int *r = recvbuf[op];
  int rt = root(op);
  switch (op) {
  case IBARRIER:
    break;
  case IBCAST:
    for (int i = 0; mt_rank != rt && i < N; i++) {
      CHECK_WORD(r[i], value(op, rt, i));
    }
    break;
  case IREDUCE:
    for (int i = 0; mt_rank == rt && i < N; i++) {
      long want = 0;
      for (int j = 0; j < mt_size; j++) {
        want += value(op, j, i);
      }
      CHECK_WORD(lrecv[i], want);
    }
    break;
  case IALLREDUCE:
  case ISCAN:
    for (int i = 0; i < N; i++) {
      int want = 0;
      int last = op == ISCAN ? mt_rank : mt_size - 1;
      for (int j = 0; j <= last; j++) {
        want += value(op, j, i);
      }
      CHECK_WORD(r[i], want);
    }
    break;
  case IREDUCE_SCATTER:
    for (int i = 0; i < own(mt_rank); i++) {
      int want = 0;
      for (int j = 0; j < mt_size; j++) {
        want += value(op, j, rdispls[op][mt_rank] + i);
      }
      CHECK_WORD(r[i], want);
    }
    break;
  case IALLTOALL:
    for (int j = 0; j < mt_size; j++) {
      for (int i = 0; i < N; i++) {
        CHECK_WORD(r[j * N + i], value(op, j, mt_rank * N + i));
      }
    }
    break;
  case IALLTOALLV:
    for (int j = 0; j < mt_size; j++) {
      // Rank j sent me its words from its displacement for me.
      int from = 0;
      for (int k = 0; k < mt_rank; k++) {
        from += vcount(j, k);
      }
      for (int i = 0; i < vcount(j, mt_rank); i++) {
        CHECK_WORD(r[rdispls[op][j] + i], value(op, j, from + i));
      }
    }
    break;
  case IALLGATHER:
  case IGATHER:
    for (int j = 0; (op == IALLGATHER || mt_rank == rt) && j < mt_size; j++) {
      for (int i = 0; i < N; i++) {
        CHECK_WORD(r[j * N + i], value(op, j, i));
      }
    }
    break;
  case IALLGATHERV:
  case IGATHERV:
    for (int j = 0; (op == IALLGATHERV || mt_rank == rt) && j < mt_size; j++) {
      for (int i = 0; i < own(j); i++) {
        CHECK_WORD(r[rdispls[op][j] + i], value(op, j, i));
      }
    }
    break;
  case ISCATTER:
    for (int i = 0; i < N; i++) {
      CHECK_WORD(r[i], value(op, rt, mt_rank * N + i));
    }
    break;
  case ISCATTERV: {
    int from = 0;
    for (int k = 0; k < mt_rank; k++) {
      from += own(k);
    }
    for (int i = 0; i < own(mt_rank); i++) {
      CHECK_WORD(r[i], value(op, rt, from + i));
    }
    break;
  }
  }
}

static void
complete(MPI_Request *req, int mode, int op)
{
  if (mode == SLEEP_WAIT) {
    usleep(1000 + 1000 * ((mt_rank + it) % 3));
  }
  if (mode == TEST) {
    int flag = 0;
    while (!flag) {
      MT_MPI(MPI_Test(req, &flag, MPI_STATUS_IGNORE));
      if (!flag) {
        usleep(100);
      }
    }
  } else {
    MT_MPI(MPI_Wait(req, MPI_STATUS_IGNORE));
  }
  MT_CHECK(*req == MPI_REQUEST_NULL, "iteration %ld %s: request not freed",
           it, names[op]);
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "nonblocking_collectives");
  for (int op = 0; op < NOPS; op++) {
    sendbuf[op] = malloc(mt_size * N * sizeof(int));
    recvbuf[op] = malloc(mt_size * N * sizeof(int));
    scounts[op] = malloc(mt_size * sizeof(int));
    sdispls[op] = malloc(mt_size * sizeof(int));
    rcounts[op] = malloc(mt_size * sizeof(int));
    rdispls[op] = malloc(mt_size * sizeof(int));
  }
  for (it = 0; mt_continue(it); it++) {
    int mode = it % NMODES;
    if (mt_rank == (it / NMODES) % mt_size) {
      usleep(2000);
    }
    if (mode == WAITALL) {
      MPI_Request reqs[NOPS];
      for (int op = 0; op < NOPS; op++) {
        post(op, &reqs[op]);
      }
      MT_MPI(MPI_Waitall(NOPS, reqs, MPI_STATUSES_IGNORE));
      for (int op = 0; op < NOPS; op++) {
        MT_CHECK(reqs[op] == MPI_REQUEST_NULL,
                 "iteration %ld: %s request not freed", it, names[op]);
      }
    } else {
      for (int op = 0; op < NOPS; op++) {
        MPI_Request req;
        post(op, &req);
        complete(&req, mode, op);
      }
    }
    for (int op = 0; op < NOPS; op++) {
      check(op);
    }
  }
  mt_finish(it);
  return 0;
}
