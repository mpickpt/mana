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

// Probing for messages from many senders.  Each iteration, every rank but 0
// sends K messages of different tags and lengths to rank 0, then waits in
// MPI_Barrier.  Rank 0 finds each message with MPI_Iprobe or MPI_Probe
// (MPI_ANY_SOURCE, MPI_ANY_TAG), receives it from the probed source and tag
// with the probed count, and checks it against its header.  The probed
// message must be the next one of its sender.

#include "mana_test.h"

#define K 6
#define MAXLEN 4096  // Words.
#define HEADER 4  // Sender, tag, iteration, message number.

static int
msg_tag(int rank, long it, int k)
{
  return (int)(it % 5) * 256 + k * 16 + rank;
}

static int
msg_len(int rank, long it, int k)
{
  return HEADER + (int)((rank * 31 + k * 577 + it * 7) % (MAXLEN - HEADER));
}

static void
send_messages(long it, int *buf)
{
  for (int k = 0; k < K; k++) {
    int tag = msg_tag(mt_rank, it, k);
    int len = msg_len(mt_rank, it, k);
    buf[0] = mt_rank;
    buf[1] = tag;
    buf[2] = (int)it;
    buf[3] = k;
    for (int i = HEADER; i < len; i++) {
      buf[i] = mt_value(mt_rank, it, k * MAXLEN + i);
    }
    MT_MPI(MPI_Send(buf, len, MPI_INT, 0, tag, MPI_COMM_WORLD));
    if ((it + k + mt_rank) % 3 == 0) {
      usleep(300);
    }
  }
}

// Checks a message that rank 0 received with 'how'; next[s] is the number
// of messages received so far from rank s in this iteration (they arrive in
// order).
static void
check(long it, const char *how, const int *buf, MPI_Status *status,
      int *next)
{
  int count;
  MPI_Get_count(status, MPI_INT, &count);
  int src = status->MPI_SOURCE;
  MT_CHECK(src > 0 && src < mt_size && count >= HEADER,
           "iteration %ld: source %d count %d", it, src, count);
  int k = next[src]++;
  MT_CHECK(buf[0] == src && buf[1] == msg_tag(src, it, k) &&
           buf[2] == (int)it && buf[3] == k,
           "iteration %ld message %d from %d (%s): header %d %d %d %d", it,
           k, src, how, buf[0], buf[1], buf[2], buf[3]);
  MT_CHECK(status->MPI_TAG == buf[1] && count == msg_len(src, it, k),
           "iteration %ld message %d from %d (%s): tag %d count %d", it, k,
           src, how, status->MPI_TAG, count);
  for (int i = HEADER; i < count; i++) {
    MT_CHECK(buf[i] == mt_value(src, it, k * MAXLEN + i),
             "iteration %ld message %d from %d (%s) word %d: %d", it, k, src,
             how, i, buf[i]);
  }
}

static void
receive_messages(long it, int *buf)
{
  int next[mt_size];
  memset(next, 0, sizeof(next));
  for (int j = 0; j < (mt_size - 1) * K; j++) {
    MPI_Status probed, status;
    int flag, count, received;
    if (j % 2 == 0) {
      do {
        MT_MPI(MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag,
                          &probed));
        if (!flag) {
          usleep(50);
        }
      } while (!flag);
    } else {
      MT_MPI(MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &probed));
    }
    MPI_Get_count(&probed, MPI_INT, &count);
    MT_CHECK(count >= HEADER && count <= MAXLEN,
             "iteration %ld: probed count %d", it, count);
    MT_MPI(MPI_Recv(buf, count, MPI_INT, probed.MPI_SOURCE, probed.MPI_TAG,
                    MPI_COMM_WORLD, &status));
    MPI_Get_count(&status, MPI_INT, &received);
    MT_CHECK(status.MPI_SOURCE == probed.MPI_SOURCE &&
             status.MPI_TAG == probed.MPI_TAG && received == count,
             "iteration %ld: probed %d %d %d, received %d %d %d", it,
             probed.MPI_SOURCE, probed.MPI_TAG, count, status.MPI_SOURCE,
             status.MPI_TAG, received);
    check(it, j % 2 == 0 ? "MPI_Iprobe" : "MPI_Probe", buf, &status, next);
  }
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_probe");
  int *buf = malloc(MAXLEN * sizeof(int));
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (mt_rank == 0) {
      if (it % 2 == 0) {
        usleep(1000);
      }
      receive_messages(it, buf);
    } else {
      send_messages(it, buf);
    }
    MT_MPI(MPI_Barrier(MPI_COMM_WORLD));
  }
  free(buf);
  mt_finish(it);
  return 0;
}
