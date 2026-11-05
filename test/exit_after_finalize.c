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

// Runs barriers for SECONDS (time for a checkpoint), calls MPI_Finalize, and
// then rank 0 alone waits 2 s and prints: the process manager must not kill
// it when the other ranks have exited.
// usage: exit_after_finalize SECONDS

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
  int rank;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  double seconds = argc > 1 ? atof(argv[1]) : 4;
  time_t end = time(NULL) + (time_t)seconds;
  int stop = 0;
  while (!stop) {
    MPI_Barrier(MPI_COMM_WORLD);
    usleep(10000);
    stop = time(NULL) >= end;
    MPI_Bcast(&stop, 1, MPI_INT, 0, MPI_COMM_WORLD);
  }
  MPI_Finalize();
  if (rank == 0) {
    sleep(2);
    printf("exit_after_finalize: PASS\n");
    fflush(stdout);
  }
  return 0;
}
