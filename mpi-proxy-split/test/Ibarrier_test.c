/*
  Test for the Ibarrier_test method

  Run with >2 ranks for non-trivial results
  Defaults to 5 iterations
  Intended to be run with mana_test.py

*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SLEEP_PER_ITERATION 5

#define MPI_TEST
#ifndef MPI_TEST
#define MPI_WAIT
#endif

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 5; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int i, iter, myid, numprocs, flag;
  int iterations;
  MPI_Status status;
  MPI_Request request = MPI_REQUEST_NULL;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  if (myid == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int ret = MPI_Ibarrier(MPI_COMM_WORLD, &request);
    assert(ret == MPI_SUCCESS);
    ret = MPI_Test(&request, &flag, &status);
    assert(ret == MPI_SUCCESS);
#ifdef MPI_TEST
    while (1) {
      int flag = 0;
      MPI_Test(&request, &flag, &status);
      if (flag) {
        break;
      }
    }
#endif
#ifdef MPI_WAIT
    MPI_Wait(&request, &status);
#endif

    sleep(SLEEP_PER_ITERATION);
  }
  MPI_Finalize();
}
