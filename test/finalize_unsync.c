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

// Runs barriers and small MPI_Allreduce()s for SECONDS (time for a
// checkpoint), then the ranks call MPI_Finalize at different times: rank r
// first waits r * 300 ms.  After MPI_Finalize, rank 0 prints
// "finalize_unsync: PASS".  Every rank must exit with status 0.
// usage: finalize_unsync SECONDS

#include <stdio.h>
#include <time.h>
#include "mana_test.h"

#define K 4

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "finalize_unsync");
  double seconds = argc > 1 ? atof(argv[1]) : 4;
  time_t end = time(NULL) + (time_t)seconds;
  int stop = 0;
  long it;
  for (it = 0; !stop; it++) {
    MT_MPI(MPI_Barrier(MPI_COMM_WORLD));
    int in[K], sum[K];
    for (int k = 0; k < K; k++) {
      in[k] = mt_value(mt_rank, it, k);
    }
    MT_MPI(MPI_Allreduce(in, sum, K, MPI_INT, MPI_SUM, MPI_COMM_WORLD));
    for (int k = 0; k < K; k++) {
      int expected = 0;
      for (int r = 0; r < mt_size; r++) {
        expected += mt_value(r, it, k);
      }
      MT_CHECK(sum[k] == expected, "iteration %ld: sum %d: %d", it, k,
               sum[k]);
    }
    // All ranks stop in the same iteration.
    int late = time(NULL) >= end;
    MT_MPI(MPI_Allreduce(&late, &stop, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD));
    usleep(10000);
  }
  usleep(mt_rank * 300000);
  MT_MPI(MPI_Finalize());
  if (mt_rank == 0) {
    printf("finalize_unsync: PASS (%ld iterations)\n", it);
    fflush(stdout);
  }
  return 0;
}
