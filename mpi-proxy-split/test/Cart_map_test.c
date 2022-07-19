/*
  Test for the MPI_Cart_map method

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

  int errs = 0;
  int dims[2];
  int periods[2];
  int size, rank, newrank;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  /* This defines a cartision grid with a single point */
  periods[0] = 1;
  dims[0] = 1;
  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int ret = MPI_Cart_map(MPI_COMM_WORLD, 1, dims, periods, &newrank);
    assert(ret == MPI_SUCCESS);
    if (rank > 0) {
      if (newrank != MPI_UNDEFINED) {
        errs++;
        printf("rank outside of input communicator not UNDEFINED\n");
        fflush(stdout);
      }
    } else {
      if (rank != newrank) {
        errs++;
        printf("Newrank not defined and should be 0\n");
        fflush(stdout);
      }
    }
  }
  MPI_Finalize();
  assert(errs == 0);
  return 0;
}
