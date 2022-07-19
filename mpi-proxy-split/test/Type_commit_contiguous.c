/*
  Test for the MPI_Type_commit method

  Run with >1 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Type_commit.html
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 100
#define NUM_RANKS 4

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int myrank;
  MPI_Status status;
  MPI_Datatype type;
  int buffer[BUFFER_SIZE];
  int buf[BUFFER_SIZE];

  MPI_Init(&argc, &argv);

  MPI_Type_contiguous(BUFFER_SIZE, MPI_INT, &type);
  MPI_Type_commit(&type);
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

  if (myrank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (int i = 0; i < BUFFER_SIZE; i++) {
      buffer[i] = i + iterations;
    }
    if (myrank == 0) {
      int ret = MPI_Send(buffer, 1, type, 1, 123 + iterations, MPI_COMM_WORLD);
      assert(ret == MPI_SUCCESS);
    } else if (myrank == 1) {
      int ret =
        MPI_Recv(buf, 1, type, 0, 123 + iterations, MPI_COMM_WORLD, &status);
      assert(ret == MPI_SUCCESS);
      for (int i = 0; i < BUFFER_SIZE; i++) {
        assert(buffer[i] == buf[i]);
      }
    }
    printf("Iteration %d completed\n", iterations);
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
