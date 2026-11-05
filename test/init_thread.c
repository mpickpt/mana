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

// MPI_Init_thread must return MPI_SUCCESS and set 'provided' to a valid level
// (MANA provides at most MPI_THREAD_FUNNELED).

#include <mpi.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
  int provided = 12345;
  int rc = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  int rank, initialized = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Initialized(&initialized);
  int ok = rc == MPI_SUCCESS && initialized &&
           provided >= MPI_THREAD_SINGLE && provided <= MPI_THREAD_MULTIPLE;
  int all_ok;
  MPI_Allreduce(&ok, &all_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD);
  if (rank == 0) {
    printf("init_thread: %s (return %d, provided %d)\n",
           all_ok ? "PASS" : "FAIL", rc, provided);
    fflush(stdout);
  }
  MPI_Finalize();
  return all_ok ? 0 : 1;
}
