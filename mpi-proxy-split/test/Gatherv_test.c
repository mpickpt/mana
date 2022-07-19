/*
  Test for the MPI_Gatherv method

  Must run with 4 ranks
  Defaults to 10000 iterations
  Intended to be run with mana_test.py
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int buffer[6];
  int rank, size, i;
  int receive_counts[4] = { 0, 1, 2, 3 };
  int receive_displacements[4] = { 0, 0, 1, 3 };

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if (size != 4) {
    if (rank == 0) {
      printf("Please run with 4 processes\n");
      fflush(stdout);
    }
    MPI_Finalize();
    return 0;
  }

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (i = 0; i < rank; i++) {
      buffer[i] = rank;
    }
    int expected_buf[6] = { 1, 2, 2, 3, 3, 3 };
    int ret = MPI_Gatherv(buffer, rank, MPI_INT, buffer, receive_counts,
                          receive_displacements, MPI_INT, 0, MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);
    if (rank == 0) {
      for (i = 0; i < 6; i++) {
        printf("buffer[%d] = %d, expected = %d\n", i, buffer[i],
               expected_buf[i]);
        assert(buffer[i] == expected_buf[i]);
      }
      printf("\n");
      fflush(stdout);
    }
  }
  MPI_Finalize();
  return 0;
}
