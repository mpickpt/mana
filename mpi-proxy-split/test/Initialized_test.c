/*
  Test for the MPI_Initialized method

  Run with >1 ranks for non-trivial results
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

  int flag, error;

  flag = 0;
  MPI_Initialized(&flag);
  if (flag) {
    printf("MPI_Initialized returned true before MPI_Init.\n");
    assert(0);
  }

  MPI_Init(&argc, &argv);

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    flag = 0;
    MPI_Initialized(&flag);
    if (!flag) {
      printf("MPI_Initialized returned false after MPI_Init.\n");
      fflush(stdout);
      MPI_Abort(MPI_COMM_WORLD, error);
      assert(0);
    }
  }

  MPI_Finalize();
  return 0;
}
