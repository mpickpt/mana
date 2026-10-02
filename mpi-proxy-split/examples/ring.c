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

// ring: a small MPI program to try MANA with.
//
// Each step passes a token once around a ring of all ranks (MPI_Send and
// MPI_Recv) and sums one value per rank (MPI_Allreduce).  Rank 0 prints one
// line per step.  Every rank checks every value that it gets: on a wrong
// one, it prints what it expected, and the job ends with status 1.
//
// usage: ring [-n STEPS] [-s SECONDS]
//   -n  the number of steps (default 60)
//   -s  the seconds to wait after each step (default 1)
//
// 'make' builds ring with the MPI compiler, as for any MPI program.  To
// run it under MANA, from MANA's directory:
//
//   bin/mana_coordinator
//   mpirun -np 4 bin/mana_launch mpi-proxy-split/examples/ring
//   bin/mana_status --checkpoint        (in another shell, while it runs)
//
// Then stop the job (Ctrl-C), and restart it from the checkpoint:
//
//   bin/mana_coordinator
//   mpirun -np 4 bin/mana_restart
//
// After the restart, the steps go on from the checkpoint, not from step 1.

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static int rank, size;

static void
check(int step, const char *what, int got, int expected)
{
  if (got != expected) {
    fprintf(stderr, "ring: rank %d, step %d: %s is %d, expected %d\n", rank,
            step, what, got, expected);
    MPI_Abort(MPI_COMM_WORLD, 1);
    exit(1);  // Some MPI libraries return from MPI_Abort before the job ends.
  }
}

int
main(int argc, char **argv)
{
  int steps = 60;
  double seconds = 1.0;
  int opt;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  opterr = 0;  // Only rank 0 prints the usage.
  while ((opt = getopt(argc, argv, "n:s:")) != -1) {
    if (opt == 'n') {
      steps = atoi(optarg);
    } else if (opt == 's') {
      seconds = atof(optarg);
    } else {
      if (rank == 0) {
        fprintf(stderr, "usage: %s [-n STEPS] [-s SECONDS]\n", argv[0]);
      }
      MPI_Finalize();
      return 2;
    }
  }

  int right = (rank + 1) % size;
  int left = (rank + size - 1) % size;
  for (int step = 1; step <= steps; step++) {
    // The token starts at rank 0 as the step number, and each rank r > 0
    // adds r to it.  So rank r gets step + 1 + 2 + ... + (r - 1).
    int token = step;
    if (size > 1 && rank == 0) {
      MPI_Send(&token, 1, MPI_INT, right, 0, MPI_COMM_WORLD);
      MPI_Recv(&token, 1, MPI_INT, left, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      check(step, "the token", token, step + size * (size - 1) / 2);
    } else if (size > 1) {
      MPI_Recv(&token, 1, MPI_INT, left, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      check(step, "the token", token, step + rank * (rank - 1) / 2);
      token += rank;
      MPI_Send(&token, 1, MPI_INT, right, 0, MPI_COMM_WORLD);
    }

    // Each rank gives step + rank.
    int mine = step + rank, sum = 0;
    MPI_Allreduce(&mine, &sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    check(step, "the sum", sum, size * step + size * (size - 1) / 2);

    if (rank == 0) {
      printf("ring: step %d of %d: token %d, sum %d: ok\n", step, steps, token,
             sum);
      fflush(stdout);
    }
    struct timespec wait = {(time_t)seconds,
                            (long)((seconds - (time_t)seconds) * 1e9)};
    nanosleep(&wait, NULL);
  }
  if (rank == 0) {
    printf("ring: done, %d step%s on %d rank%s\n", steps,
           steps == 1 ? "" : "s", size, size == 1 ? "" : "s");
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
