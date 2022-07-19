/*
  Test for the MPI_Barrier method

  Run with >1 ranks for non-trivial results
  Defaults to 5 iterations
  Intended to be run with mana_test.py
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 100
#define SLEEP_PER_ITERATION 1

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 5; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int rank, nprocs;
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    printf("Entering  %d of %d\n", rank, nprocs);
    fflush(stdout);
    sleep(rank * SLEEP_PER_ITERATION);
    int ret = MPI_Barrier(MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);
    printf("Everyone should be entered by now. If not then Test Failed!\n");
    fflush(stdout);
  }

  MPI_Finalize();
  return 0;
}
