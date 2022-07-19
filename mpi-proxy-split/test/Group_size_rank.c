/*
  Test for the MPI_Group methods

  Run with >2 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Group_size.html
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int errs = 0, toterr;
  MPI_Group basegroup;
  MPI_Comm comm;
  int grp_rank, rank, grp_size, size;
  int worldrank;

  MPI_Init(NULL, NULL);
  MPI_Comm_rank(MPI_COMM_WORLD, &worldrank);
  comm = MPI_COMM_WORLD;
  MPI_Comm_group(comm, &basegroup);
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    /* Get the basic information on this group */
    int ret = MPI_Group_rank(basegroup, &grp_rank);
    assert(ret == MPI_SUCCESS);
    if (grp_rank != rank) {
      errs++;
      fprintf(stdout, "group rank %d != comm rank %d\n", grp_rank, rank);
      fflush(stdout);
    }
    ret = MPI_Group_size(basegroup, &grp_size);
    assert(ret == MPI_SUCCESS);
    if (grp_size != size) {
      errs++;
      fprintf(stdout, "group size %d != comm size %d\n", grp_size, size);
      fflush(stdout);
    }
    assert(errs == 0);
    if (rank == 0) {
      printf("Test passed!\n");
      fflush(stdout);
    }
  }
  return 0;
}
