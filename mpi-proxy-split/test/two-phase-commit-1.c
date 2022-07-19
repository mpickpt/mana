/*
  Test for the two phase commit algorithm

  Must run with >2 ranks
  Defaults to 30 iterations
  Intended to be run with mana_test.py
*/

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SLEEP_PER_ITERATION 1

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 30;
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int provided, flag, claimed;
  int comm1_counter = 0;
  int comm2_counter = 0;

  int ret = MPI_Init(0, 0);
  if (ret != MPI_SUCCESS) {
    printf("MPI_Init failed\n");
    exit(1);
  }

  int rank, nprocs;
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }
  printf("Hello, world.  I am %d of %d\n", rank, nprocs);
  fflush(stdout);
  if (nprocs < 3) {
    printf("This test needs at least 3 ranks.\n");
    MPI_Finalize();
    return 1;
  }
  int group1_ranks[] = { 0, 1 };
  int group2_ranks[10000];
  int i, j;
  for (i = 1; i < nprocs; i++) {
    group2_ranks[i - 1] = i;
  }

  MPI_Group world_group;
  MPI_Group group1;
  MPI_Group group2;
  MPI_Comm_group(MPI_COMM_WORLD, &world_group);
  MPI_Group_incl(world_group, 2, group1_ranks, &group1);
  MPI_Group_incl(world_group, nprocs - 1, group2_ranks, &group2);

  MPI_Comm comm1; // Set to MPI_COMM_NULL by default.
  MPI_Comm comm2; // Set to MPI_COMM_NULL by default.
  MPI_Comm_create(MPI_COMM_WORLD, group1, &comm1);
  MPI_Comm_create(MPI_COMM_WORLD, group2, &comm2);
  printf("rank: %d, group1: %x, comm1: %x\n", rank, group1, comm1);
  printf("rank: %d, group2: %x, comm2: %x\n", rank, group2, comm2);

  //===============================================================
  // Coll. 1:    === |
  //             | | |
  // Coll. 2:    | ===
  //             | | |
  // Coll. 3:    === |
  //             | | |
  // Coll. 4:    | ===
  //
  // RULE TO FIX (This is one part of the full rule. See two-phase-commit-2.c):
  // If you're blocked in Phase 1, you only get a free pass if someone
  // else in your communicator is in the critical section.
  // If you're blocked in Phase 2, you always get the free pass

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    if (comm1 != MPI_COMM_NULL) {
      comm1_counter++;
      printf("Rank %d entering comm1, iteration %d\n", rank, comm1_counter);
      fflush(stdout);
      MPI_Barrier(comm1);
      printf("Rank %d leaving comm1, iteration %d\n", rank, comm1_counter);
      fflush(stdout);
      sleep(SLEEP_PER_ITERATION);
    }
    if (comm2 != MPI_COMM_NULL) {
      comm2_counter++;
      printf("Rank %d entering comm2, iteration %d\n", rank, comm2_counter);
      fflush(stdout);
      MPI_Barrier(comm2);
      printf("Rank %d leaving comm2, iteration %d\n", rank, comm2_counter);
      fflush(stdout);
      sleep(SLEEP_PER_ITERATION);
    }
  }

  MPI_Finalize();
  return 0;
}
