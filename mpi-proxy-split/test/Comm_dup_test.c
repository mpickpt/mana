/*
  Test for the MPI_Comm_dup method
  Also tests MPI_Comm_create, MPI_Comm group methods

  Run with >2 ranks for non-trivial results
  Defaults to 5 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Comm_dup.html
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SLEEP_PER_ITERATION 5

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 5; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  MPI_Comm dup_comm_world, world_comm;
  MPI_Group world_group;
  int world_rank, world_size, rank, size;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (world_rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int ret = MPI_Comm_dup(MPI_COMM_WORLD, &dup_comm_world);
    assert(ret == MPI_SUCCESS);
    /* Exercise Comm_create by creating an equivalent
      to dup_comm_world (sans attributes) */
    MPI_Comm_group(dup_comm_world, &world_group);
    MPI_Comm_create(dup_comm_world, world_group, &world_comm);

    sleep(SLEEP_PER_ITERATION);

    MPI_Comm_rank(world_comm, &rank);
    if (rank != world_rank) {
      printf("incorrect rank in world comm: %d\n", rank);
      fflush(stdout);
      MPI_Abort(MPI_COMM_WORLD, 3001);
    }
    printf("[Rank %d] \n", rank);
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
