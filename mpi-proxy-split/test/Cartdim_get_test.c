/*
  Test for the MPI_Cart_dim method

  Must run with 12 ranks
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Cart_coords.html
*/
#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

/* A two-dimensional torus of 12 processes in a 4x3 grid */
int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int rank, size;
  MPI_Comm comm;
  int dim[2], period[2], reorder;
  int coord[2], id;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (size != 12) {
    printf("Please run with 12 processes.\n");
    fflush(stdout);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  dim[0] = 4;
  dim[1] = 3;
  period[0] = 1;
  period[1] = 0;
  reorder = 1;
  int ret = MPI_Cart_create(MPI_COMM_WORLD, 2, dim, period, reorder, &comm);
  assert(ret == MPI_SUCCESS);
  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int ndims = 0;
    ret = MPI_Cartdim_get(comm, &ndims);
    assert(ret == MPI_SUCCESS);
    if (rank == 0) {
      printf("number of dimensions = %d\n", ndims);
      fflush(stdout);
    }
    assert(ndims == 2);
  }

  MPI_Finalize();
  return 0;
}
