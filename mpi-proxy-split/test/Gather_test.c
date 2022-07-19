/*
  Test for the MPI_Gather method

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

  int send, recv[3];
  int rank, size;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (size != 3 && rank == 0) {
    printf("please run with 3 processes!\n");
    exit(1);
  }
  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  send = rank + 1;
  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int ret =
      MPI_Gather(&send, 1, MPI_INT, &recv, 1, MPI_INT, 0, MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);

    if (rank == 0) {
      printf("recv = %d %d %d\n", recv[0], recv[1], recv[2]);
      for (int i = 0; i < 3; i++) {
        assert(recv[i] == i + 1);
      }
    }
    fflush(stdout);
    for (int i = 0; i < 3; i++) {
      recv[i] = 0;
    }
  }

  MPI_Finalize();
  return 0;
}
