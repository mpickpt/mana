/*
  Test for the MPI_Alloc_mem method

  Defaults to 15 iterations
  Intended to be run with mana_test.py
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHAR_COUNT 100
#define SLEEP_PER_ITERATION 2

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 15; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  char *mem;
  int rank, ret;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    ret = MPI_Alloc_mem(CHAR_COUNT * sizeof(char), MPI_INFO_NULL, &mem);
    assert(ret == MPI_SUCCESS);
    printf("Rank %d allocated %d chars at %p\n", rank, CHAR_COUNT, &mem);
    fflush(stdout);

    sleep(SLEEP_PER_ITERATION);

    ret = MPI_Free_mem(mem);
    assert(ret == MPI_SUCCESS);
    printf("Rank %d freed %p\n", rank, &mem);
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
