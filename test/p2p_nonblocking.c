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

// Nonblocking point-to-point in a ring.  Each rank posts receives from both
// neighbours, waits a little, then sends to both, so that requests are
// pending when a checkpoint comes.  Each iteration completes the requests
// in another way (MPI_Waitall, MPI_Waitany, MPI_Testany, MPI_Test,
// MPI_Testall).  Then MPI_Sendrecv and MPI_Sendrecv_replace exchange with
// the neighbours.

#include "mana_test.h"

#define N 256
#define NREQ 4

enum { WAITALL, WAITALL_IGNORE, WAITANY, TESTANY, TEST, TESTALL, METHODS };

static long it;
static int count;  // Words per message in this iteration.
static int left, right;
static int rbuf[2][N];

// The message with tag T holds mt_value(sender, it, T * N + i), i < count;
// the rest of the receive buffer must stay -1.  rbuf[0] receives tag 0 from
// the left neighbour, rbuf[1] tag 1 from the right one.
static void
fill(int *buf, int tag)
{
  for (int i = 0; i < count; i++) {
    buf[i] = mt_value(mt_rank, it, tag * N + i);
  }
}

static void
check(const int *buf, int from, int tag, MPI_Status *status)
{
  if (status != NULL) {
    int n;
    MPI_Get_count(status, MPI_INT, &n);
    MT_CHECK(status->MPI_SOURCE == from && status->MPI_TAG == tag &&
             n == count, "iteration %ld tag %d: source %d tag %d count %d",
             it, tag, status->MPI_SOURCE, status->MPI_TAG, n);
  }
  for (int i = 0; i < N; i++) {
    int expect = i < count ? mt_value(from, it, tag * N + i) : -1;
    MT_CHECK(buf[i] == expect, "iteration %ld tag %d word %d: %d", it, tag,
             i, buf[i]);
  }
}

// Checks request 'i' (0, 1: receives; 2, 3: sends) once it is complete.
static void
completed(MPI_Request *req, int i, int *done, MPI_Status *status)
{
  MT_CHECK(i >= 0 && i < NREQ && !done[i], "iteration %ld: index %d", it, i);
  MT_CHECK(req[i] == MPI_REQUEST_NULL, "iteration %ld: request %d not null",
           it, i);
  done[i] = 1;
  if (i < 2) {
    check(rbuf[i], i == 0 ? left : right, i, status);
  }
}

static void
complete(MPI_Request *req, int method)
{
  MPI_Status st[NREQ];
  int done[NREQ] = { 0 };
  int i, n, flag;
  switch (method) {
    case WAITALL:
      MT_MPI(MPI_Waitall(NREQ, req, st));
      for (i = 0; i < NREQ; i++) {
        completed(req, i, done, &st[i]);
      }
      break;
    case WAITALL_IGNORE:
      MT_MPI(MPI_Waitall(NREQ, req, MPI_STATUSES_IGNORE));
      for (i = 0; i < NREQ; i++) {
        completed(req, i, done, NULL);
      }
      break;
    case WAITANY:
      for (n = 0; n < NREQ; n++) {
        MT_MPI(MPI_Waitany(NREQ, req, &i, &st[0]));
        completed(req, i, done, &st[0]);
      }
      MT_MPI(MPI_Waitany(NREQ, req, &i, &st[0]));
      MT_CHECK(i == MPI_UNDEFINED, "iteration %ld: index %d", it, i);
      break;
    case TESTANY:
      for (n = 0; n < NREQ;) {
        MT_MPI(MPI_Testany(NREQ, req, &i, &flag, &st[0]));
        if (flag) {
          completed(req, i, done, &st[0]);
          n++;
        } else {
          usleep(50);
        }
      }
      MT_MPI(MPI_Testany(NREQ, req, &i, &flag, &st[0]));
      MT_CHECK(flag && i == MPI_UNDEFINED, "iteration %ld: flag %d index %d",
               it, flag, i);
      break;
    case TEST:
      for (n = 0; n < NREQ;) {
        for (i = 0; i < NREQ; i++) {
          if (!done[i]) {
            MT_MPI(MPI_Test(&req[i], &flag, &st[i]));
            if (flag) {
              completed(req, i, done, &st[i]);
              n++;
            }
          }
        }
        if (n < NREQ) {
          usleep(50);
        }
      }
      break;
    case TESTALL:
      do {
        MT_MPI(MPI_Testall(NREQ, req, &flag, st));
        if (!flag) {
          usleep(50);
        }
      } while (!flag);
      for (i = 0; i < NREQ; i++) {
        completed(req, i, done, &st[i]);
      }
      break;
  }
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_nonblocking");
  right = (mt_rank + 1) % mt_size;
  left = (mt_rank + mt_size - 1) % mt_size;
  int sbuf[2][N], buf[N];
  MPI_Status status;
  for (it = 0; mt_continue(it); it++) {
    count = 1 + (int)(it * 37 % N);
    MPI_Request req[NREQ];
    memset(rbuf, -1, sizeof(rbuf));
    MT_MPI(MPI_Irecv(rbuf[0], N, MPI_INT, left, 0, MPI_COMM_WORLD, &req[0]));
    MT_MPI(MPI_Irecv(rbuf[1], N, MPI_INT, right, 1, MPI_COMM_WORLD,
                     &req[1]));
    usleep(200 + 400 * ((it + mt_rank) % 4));
    fill(sbuf[0], 0);
    fill(sbuf[1], 1);
    MT_MPI(MPI_Isend(sbuf[0], count, MPI_INT, right, 0, MPI_COMM_WORLD,
                     &req[2]));
    MT_MPI(MPI_Isend(sbuf[1], count, MPI_INT, left, 1, MPI_COMM_WORLD,
                     &req[3]));
    complete(req, (int)(it % METHODS));

    fill(sbuf[0], 2);
    memset(buf, -1, sizeof(buf));
    MT_MPI(MPI_Sendrecv(sbuf[0], count, MPI_INT, right, 2, buf, N, MPI_INT,
                        left, 2, MPI_COMM_WORLD, &status));
    check(buf, left, 2, &status);

    memset(buf, -1, sizeof(buf));
    fill(buf, 3);
    MT_MPI(MPI_Sendrecv_replace(buf, count, MPI_INT, left, 3, right, 3,
                                MPI_COMM_WORLD, &status));
    check(buf, right, 3, &status);
  }
  mt_finish(it);
  return 0;
}
