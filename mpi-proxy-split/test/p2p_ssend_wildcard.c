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

// Wildcard receives of MPI_Ssend and MPI_Send messages.  Each iteration,
// odd ranks MPI_Ssend and even ranks but 0 MPI_Send one message to rank 0,
// which waits, then receives them alternately: with MPI_Probe (MPI_ANY_SOURCE,
// MPI_ANY_TAG) and MPI_Recv from the probed source and tag, which must get
// the probed message; and with MPI_Recv (MPI_ANY_SOURCE, MPI_ANY_TAG), whose
// status must match the message.

#include "mana_test.h"

#define MAXLEN 1024  // Words.
#define HEADER 2     // Sender, iteration.
#define WAIT_MS 100  // Checkpoints find the MPI_Ssends waiting

static int
msg_len(int rank, long it)
{
  return HEADER + (int)((rank * 131 + it * 7) % (MAXLEN - HEADER));
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_ssend_wildcard");
  MT_CHECK(mt_size >= 3, "needs 3 or more ranks, not %d", mt_size);
  int buf[MAXLEN];
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (mt_rank != 0) {
      int len = msg_len(mt_rank, it);
      buf[0] = mt_rank;
      buf[1] = (int)it;
      for (int i = HEADER; i < len; i++) {
        buf[i] = mt_value(mt_rank, it, i);
      }
      if (mt_rank % 2 == 1) {
        MT_MPI(MPI_Ssend(buf, len, MPI_INT, 0, mt_rank, MPI_COMM_WORLD));
      } else {
        MT_MPI(MPI_Send(buf, len, MPI_INT, 0, mt_rank, MPI_COMM_WORLD));
      }
      MT_MPI(MPI_Barrier(MPI_COMM_WORLD));
      continue;
    }
    for (int ms = 0; ms < WAIT_MS; ms++) {
      usleep(1000);
    }
    int seen[mt_size];
    memset(seen, 0, sizeof(seen));
    for (int j = 1; j < mt_size; j++) {
      MPI_Status probed, status;
      int count, received;
      if (j % 2 == 1) {
        MT_MPI(MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                         &probed));
        MPI_Get_count(&probed, MPI_INT, &count);
        MT_MPI(MPI_Recv(buf, count, MPI_INT, probed.MPI_SOURCE,
                        probed.MPI_TAG, MPI_COMM_WORLD, &status));
      } else {
        MT_MPI(MPI_Recv(buf, MAXLEN, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG,
                        MPI_COMM_WORLD, &status));
        probed = status;
        MPI_Get_count(&status, MPI_INT, &count);
      }
      MPI_Get_count(&status, MPI_INT, &received);
      int src = status.MPI_SOURCE;
      MT_CHECK(src == probed.MPI_SOURCE && status.MPI_TAG == probed.MPI_TAG &&
               received == count,
               "iteration %ld: probed %d %d %d, received %d %d %d", it,
               probed.MPI_SOURCE, probed.MPI_TAG, count, src, status.MPI_TAG,
               received);
      MT_CHECK(src > 0 && src < mt_size && !seen[src]++ && status.MPI_TAG == src
               && count == msg_len(src, it) && buf[0] == src &&
               buf[1] == (int)it,
               "iteration %ld: source %d tag %d count %d header %d %d", it,
               src, status.MPI_TAG, count, buf[0], buf[1]);
      for (int i = HEADER; i < count; i++) {
        MT_CHECK(buf[i] == mt_value(src, it, i),
                 "iteration %ld from %d word %d: %d", it, src, i, buf[i]);
      }
    }
    MT_MPI(MPI_Barrier(MPI_COMM_WORLD));
  }
  mt_finish(it);
  return 0;
}
