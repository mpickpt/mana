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

// A probed MPI_Ssend message stays probed: once MPI_Iprobe finds it, every
// later MPI_Iprobe finds it, with the same count, until it is received, even
// when a checkpoint drains it meanwhile.

#include "mana_test.h"

#define MAXLEN 1024
#define WAIT_MS 100  // Checkpoints find the message probed, not received

static int
msg_len(int rank, long it)
{
  return 1 + (int)((rank * 31 + it * 7) % MAXLEN);
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "p2p_ssend_probe");
  MT_CHECK(mt_size % 2 == 0, "needs an even number of ranks, not %d", mt_size);
  int sender = mt_rank % 2 == 0;
  int partner = sender ? mt_rank + 1 : mt_rank - 1;
  int buf[MAXLEN];
  long it;
  for (it = 0; mt_continue(it); it++) {
    int len = msg_len(sender ? mt_rank : partner, it);
    if (sender) {
      for (int i = 0; i < len; i++) {
        buf[i] = mt_value(mt_rank, it, i);
      }
      MT_MPI(MPI_Ssend(buf, len, MPI_INT, partner, 1, MPI_COMM_WORLD));
      continue;
    }
    int flag = 0;
    MPI_Status status;
    while (!flag) {
      MT_MPI(MPI_Iprobe(partner, 1, MPI_COMM_WORLD, &flag, &status));
    }
    for (int ms = 0; ms < WAIT_MS; ms++) {
      int count;
      MPI_Get_count(&status, MPI_INT, &count);
      MT_CHECK(flag && count == len,
               "iteration %ld: probed message lost (flag %d count %d, not %d)",
               it, flag, count, len);
      usleep(1000);
      MT_MPI(MPI_Iprobe(partner, 1, MPI_COMM_WORLD, &flag, &status));
    }
    MT_MPI(MPI_Recv(buf, len, MPI_INT, partner, 1, MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE));
    for (int i = 0; i < len; i++) {
      MT_CHECK(buf[i] == mt_value(partner, it, i), "iteration %ld word %d: %d",
               it, i, buf[i]);
    }
  }
  mt_finish(it);
  return 0;
}
