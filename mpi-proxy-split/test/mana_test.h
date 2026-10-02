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

// Helpers for the end-to-end tests run by autotest.py.  A test loops until it
// is told to stop, checks every value it receives with MT_CHECK(), and calls
// MPI_Abort() on the first wrong one.  It stops after -n ITERATIONS or, if
// the environment variable MT_CONTROL names a directory, at the iteration
// written in MT_CONTROL/stop.  With MT_CONTROL, each rank also prints
// "<name>: rank R: iteration I" at most every 0.1 s.

#ifndef MANA_TEST_H
#define MANA_TEST_H

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *mt_name;
static int mt_rank, mt_size;
static long mt_iterations = -1;  // -1: no stop yet
static char mt_stop_file[4096];   // empty: no MT_CONTROL

#define MT_CHECK(cond, ...)                                                  \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s: rank %d: %s:%d: failed: %s: ", mt_name, mt_rank,  \
              __FILE__, __LINE__, #cond);                                    \
      fprintf(stderr, __VA_ARGS__);                                          \
      fprintf(stderr, "\n");                                                 \
      fflush(stderr);                                                        \
      MPI_Abort(MPI_COMM_WORLD, 1);                                          \
      exit(1); /* MPICH's MPI_Abort may return before the job is killed */   \
    }                                                                        \
  } while (0)

#define MT_MPI(call) MT_CHECK((call) == MPI_SUCCESS, "%s", "MPI error")

// Initializes MPI and reads '-n ITERATIONS'; other arguments stay in argv.
static void
mt_init(int *argc, char ***argv, const char *name)
{
  mt_name = name;
  MPI_Init(argc, argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &mt_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mt_size);
  for (int i = 1; i + 1 < *argc; i++) {
    if (strcmp((*argv)[i], "-n") == 0) {
      mt_iterations = atol((*argv)[i + 1]);
      memmove(&(*argv)[i], &(*argv)[i + 2],
              (*argc - i - 1) * sizeof(char *));
      *argc -= 2;
      break;
    }
  }
  const char *control = getenv("MT_CONTROL");
  if (control != NULL) {
    snprintf(mt_stop_file, sizeof(mt_stop_file), "%s/stop", control);
  }
}

// Called before each iteration; returns 0 when the test must stop.
static int
mt_continue(long iteration)
{
  static struct timespec last;
  struct timespec now;

  if (mt_stop_file[0] != '\0') {
    clock_gettime(CLOCK_MONOTONIC, &now);
    double dt = (now.tv_sec - last.tv_sec) +
                (now.tv_nsec - last.tv_nsec) / 1e9;
    if (dt >= 0.1 || dt < 0) {  // dt < 0: restarted on another node
      last = now;
      printf("%s: rank %d: iteration %ld\n", mt_name, mt_rank, iteration);
      fflush(stdout);
      FILE *f = mt_iterations < 0 ? fopen(mt_stop_file, "r") : NULL;
      if (f != NULL) {
        long stop;
        if (fscanf(f, "%ld", &stop) == 1) {
          MT_CHECK(stop >= iteration, "told to stop at iteration %ld", stop);
          mt_iterations = stop;
        }
        fclose(f);
      }
    }
  }
  return mt_iterations < 0 || iteration < mt_iterations;
}

// Rank 0 prints "<name>: PASS" when all ranks get here.
static void
mt_finish(long iterations)
{
  MPI_Barrier(MPI_COMM_WORLD);
  if (mt_rank == 0) {
    printf("%s: PASS (%ld iterations)\n", mt_name, iterations);
    fflush(stdout);
  }
  MPI_Finalize();
}

// A value that depends on who sent it, when, and where in the buffer.
static inline int
mt_value(int rank, long iteration, int index)
{
  return (int)(rank * 1000003L + iteration * 7919L + index);
}

#endif  // MANA_TEST_H
