/*
  Test for the MPI_Scan method

  Run with >1 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int rank, size, recv;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int ret = MPI_Scan(&rank, &recv, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);
    int expected_sum = 0;
    for (int i = 0; i <= rank; i++) {
      expected_sum += i;
    }
    printf("[Rank %d] => recveived sum = %d, expected sum = %d\n", rank, recv,
           expected_sum);
    fflush(stdout);
    assert(recv == expected_sum);
    recv = 0;
  }

  MPI_Finalize();
}
