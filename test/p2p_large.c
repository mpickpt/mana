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

// Large point-to-point messages (1 to 8 MB, sent with the rendezvous
// protocol) between two ranks: an exchange both ways with MPI_Isend and
// MPI_Irecv, then a blocking one that rank 0 starts.  One rank waits before
// sending, so that the other one waits on a large receive when a
// checkpoint comes.

#include "mana_test.h"

#define MB (1 << 20)
#define MAXWORDS (8 * MB / (int)sizeof(int))

// About 1 to 8 MB, in words.
static int
words(long it)
{
  return MB / (int)sizeof(int) * (int)(1 + it % 8) - (int)(it % 7);
}

// Word i of a message is mt_value(sender, it, index + i), which is linear
// in i: this is faster than calling mt_value() for each word.
static void
fill(int *buf, int n, long it, int index)
{
  unsigned v = (unsigned)mt_value(mt_rank, it, index);
  for (int i = 0; i < n; i++) {
    buf[i] = (int)(v + i);
  }
}

static void
check(const int *buf, int n, int from, int tag, long it, int index,
      MPI_Status *status)
{
  int count;
  MPI_Get_count(status, MPI_INT, &count);
  MT_CHECK(status->MPI_SOURCE == from && status->MPI_TAG == tag &&
           count == n, "iteration %ld: source %d tag %d count %d of %d", it,
           status->MPI_SOURCE, status->MPI_TAG, count, n);
  unsigned v = (unsigned)mt_value(from, it, index);
  int i = 0;
  while (i < n && buf[i] == (int)(v + i)) {
    i++;
  }
  MT_CHECK(i == n, "iteration %ld tag %d word %d of %d: %d, not %d", it,
           tag, i, n, buf[i], mt_value(from, it, index + i));
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_large");
  MT_CHECK(mt_size == 2, "needs 2 ranks, not %d", mt_size);
  int other = 1 - mt_rank;
  int *sbuf = malloc(MAXWORDS * sizeof(int));
  int *rbuf = malloc(MAXWORDS * sizeof(int));
  MT_CHECK(sbuf != NULL && rbuf != NULL, "%s", "out of memory");
  long it;
  for (it = 0; mt_continue(it); it++) {
    MPI_Request req[2];
    MPI_Status st[2];
    int n = words(it);
    fill(sbuf, n, it, 0);
    MT_MPI(MPI_Irecv(rbuf, MAXWORDS, MPI_INT, other, 1, MPI_COMM_WORLD,
                     &req[0]));
    if (it % 2 == mt_rank) {
      usleep(1000);
    }
    MT_MPI(MPI_Isend(sbuf, n, MPI_INT, other, 1, MPI_COMM_WORLD, &req[1]));
    MT_MPI(MPI_Waitall(2, req, st));
    check(rbuf, n, other, 1, it, 0, &st[0]);

    // Words from MAXWORDS on: values unlike those of the first exchange.
    n = words(it + 3);
    if (mt_rank == 0) {
      fill(sbuf, n, it, MAXWORDS);
      MT_MPI(MPI_Send(sbuf, n, MPI_INT, 1, 2, MPI_COMM_WORLD));
      MT_MPI(MPI_Recv(rbuf, MAXWORDS, MPI_INT, 1, 3, MPI_COMM_WORLD, &st[0]));
      check(rbuf, n, 1, 3, it, MAXWORDS, &st[0]);
    } else {
      MT_MPI(MPI_Recv(rbuf, MAXWORDS, MPI_INT, 0, 2, MPI_COMM_WORLD, &st[0]));
      check(rbuf, n, 0, 2, it, MAXWORDS, &st[0]);
      fill(sbuf, n, it, MAXWORDS);
      MT_MPI(MPI_Send(sbuf, n, MPI_INT, 0, 3, MPI_COMM_WORLD));
    }
  }
  free(sbuf);
  free(rbuf);
  mt_finish(it);
  return 0;
}
