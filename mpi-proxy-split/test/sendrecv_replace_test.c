/*
  Test for the MPI_Sendrecv_replace method

  Must run with 3 ranks
  Defaults to 100000 iterations
  Intended to be run with mana_test.py

*/
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 3

int main(int argc, char** argv) {
  // Parse runtime argument
  int max_iterations = 100000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int buf[BUFFER_SIZE];
  int exp[BUFFER_SIZE];
  int retval;

  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank and size
  int rank;
  retval = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  assert(retval == MPI_SUCCESS);
  int world_size;
  retval = MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  assert(retval == MPI_SUCCESS);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  // We are assuming 3 processes for this task
  if (world_size != 3) {
    fprintf(stderr, "World size must be equal to 3 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  MPI_Status status;

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (int i = 0; i < BUFFER_SIZE; i++) {
      buf[i] = iterations + rank + i;
      exp[i] = iterations + ((rank+1)%3) + i;
    }
    int dst = (rank+1) % 3;
    int src = (rank+2) % 3;
    int tag = 123;
    retval = MPI_Sendrecv_replace(buf, BUFFER_SIZE, MPI_INT, src,
                                  iterations+tag, dst, iterations+tag,
                                  MPI_COMM_WORLD, &status);
    assert(retval == MPI_SUCCESS);
    for (int i = 0; i < BUFFER_SIZE; i++) {
      assert(exp[i] == buf[i]);
    }

  }
  MPI_Finalize();
  return 0;
}
