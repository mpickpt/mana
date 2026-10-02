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

// Rank 0 calls MPI_Abort(MPI_COMM_WORLD, 3) while rank 1 waits in
// MPI_Barrier: the job must end, with a nonzero status.

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
  int rank;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Barrier(MPI_COMM_WORLD);
  if (rank == 0) {
    printf("abort: calling MPI_Abort\n");
    fflush(stdout);
    // Let rank 1 enter MPI_Barrier, and mpirun pass on the line (the abort
    // can kill mpirun before it does).
    usleep(200000);
    MPI_Abort(MPI_COMM_WORLD, 3);
    exit(3);  // MPICH's MPI_Abort may return before the job is killed.
  }
  MPI_Barrier(MPI_COMM_WORLD);
  fprintf(stderr, "abort: rank %d: MPI_Barrier returned\n", rank);
  return 1;
}
