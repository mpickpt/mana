/*
  Test for the MPI_Scatter method

  Must run with 3 ranks
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

  int send[3], recv;
  int rank, size, i;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (size != 3) {
    printf("Run with 3 processes\n");
    exit(1);
  }
  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  if (rank == 0) {
    for (i = 0; i < size; i++) {
      send[i] = i + 1;
    }
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int ret =
      MPI_Scatter(&send, 1, MPI_INT, &recv, 1, MPI_INT, 0, MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);

    printf("rank = %d \t recv = %d\n", rank, recv);
    fflush(stdout);
    assert(recv == rank + 1);
    MPI_Finalize();

    if (rank == 0) {
      for (i = 0; i < size; i++) {
        send[i] = i + 1;
      }
    }
    recv = 0;
  }
  return 0;
}
