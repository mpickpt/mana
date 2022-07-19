/*
  Test for the MPI_Allgather method

  Run with 4 ranks
  Defaults to 5 iterations
  Intended to be run with mana_test.py

  Source: http://hpc.mines.edu/examples/examples/mpi/c_ex04.c
*/

#include <assert.h>
#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_RANKS 4

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 5; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int i, myid, numprocs;
  int source;
  int buffer[NUM_RANKS];
  int expected_output[NUM_RANKS];
  MPI_Status status;
  MPI_Request request;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  if (myid == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  assert(numprocs == NUM_RANKS);

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    source = iterations % NUM_RANKS;
    if (myid == source) {
      for (i = 0; i < NUM_RANKS; i++)
        buffer[i] = i + iterations;
    }
    for (i = 0; i < NUM_RANKS; i++) {
      expected_output[i] = i + iterations;
    }
    int ret = MPI_Bcast(buffer, NUM_RANKS, MPI_INT, source, MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);
    printf("[Rank = %d]", myid);
    for (i = 0; i < NUM_RANKS; i++) {
      assert(buffer[i] == expected_output[i]);
      printf(" %d", buffer[i]);
    }
    printf("\n");
    fflush(stdout);
  }
  MPI_Finalize();
}
