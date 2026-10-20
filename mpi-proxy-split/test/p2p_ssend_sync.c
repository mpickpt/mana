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

// MPI_Ssend must not return before its receive is posted, even when a
// checkpoint drains the message: the sender then sends a marker, which the
// receiver must not see before it posts the receive.  Variants, each a test
// of its own:
//   -b  p2p_ssend_burst:   the receiver waits 2 s, so that checkpoints in a
//                          row find the MPI_Ssend still waiting;
//   -r  p2p_ssend_restart: the same, and the harness restarts right after
//                          the checkpoint, before the receive;
//   -l  p2p_ssend_large:   4 MB messages (MPI's rendezvous protocol);
//   -x  p2p_ssend_stress:  the receiver waits 0 to 3 ms, so that some
//                          checkpoints come as the receive starts.

#include "mana_test.h"

#define N 32
#define LARGE_N (1 << 20)
#define WAIT_MS 100  // The receiver waits; checkpoints find the sender waiting
#define LONG_WAIT_MS 2000

int
main(int argc, char **argv)
{
  const char *name = "p2p_ssend_sync";
  int n = N, wait_ms = WAIT_MS, stress = 0;
  if (argc > 1 && strcmp(argv[1], "-b") == 0) {
    name = "p2p_ssend_burst";
    wait_ms = LONG_WAIT_MS;
  } else if (argc > 1 && strcmp(argv[1], "-r") == 0) {
    name = "p2p_ssend_restart";
    wait_ms = LONG_WAIT_MS;
  } else if (argc > 1 && strcmp(argv[1], "-l") == 0) {
    name = "p2p_ssend_large";
    n = LARGE_N;
  } else if (argc > 1 && strcmp(argv[1], "-x") == 0) {
    name = "p2p_ssend_stress";
    stress = 1;
  }
  mt_init(&argc, &argv, name);
  MT_CHECK(mt_size % 2 == 0, "needs an even number of ranks, not %d", mt_size);
  int sender = mt_rank % 2 == 0;
  int partner = sender ? mt_rank + 1 : mt_rank - 1;
  int *out = malloc(n * sizeof(int)), *in = malloc(n * sizeof(int));
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (sender) {
      for (int i = 0; i < n; i++) {
        out[i] = mt_value(mt_rank, it, i);
      }
      MT_MPI(MPI_Ssend(out, n, MPI_INT, partner, 1, MPI_COMM_WORLD));
      MT_MPI(MPI_Send(&it, 1, MPI_LONG, partner, 2, MPI_COMM_WORLD));
      continue;
    }
    if (stress) {
      wait_ms = (int)((it * 7 + mt_rank) % 4);
    }
    for (int ms = 0; ms < wait_ms; ms++) {
      int flag;
      MT_MPI(MPI_Iprobe(partner, 2, MPI_COMM_WORLD, &flag,
                        MPI_STATUS_IGNORE));
      MT_CHECK(!flag, "iteration %ld: MPI_Ssend returned before its receive",
               it);
      usleep(1000);
    }
    MT_MPI(MPI_Recv(in, n, MPI_INT, partner, 1, MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE));
    long marker;
    MT_MPI(MPI_Recv(&marker, 1, MPI_LONG, partner, 2, MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE));
    MT_CHECK(marker == it, "iteration %ld: marker %ld", it, marker);
    for (int i = 0; i < n; i++) {
      MT_CHECK(in[i] == mt_value(partner, it, i), "iteration %ld word %d: %d",
               it, i, in[i]);
    }
  }
  free(out);
  free(in);
  mt_finish(it);
  return 0;
}
