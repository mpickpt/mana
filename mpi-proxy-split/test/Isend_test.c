/*
  Test for the MPI_Isend method

  Must run with >1 ranks
  Defaults to 5 iterations
  Intended to be run with mana_test.py

*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 3
#define SLEEP_PER_ITERATION 5

int
main(int argc, char **argv)
{
  // Parse runtime argument
  int max_iterations = 5; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank and size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (world_rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  // We are assuming at least 2 processes for this task
  if (world_size < 2) {
    fprintf(stderr, "World size must be greater than 1 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  int rank = 0;
  int number = 11223344;
  MPI_Request reqs[world_size];

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (rank = 0; rank < world_size; rank++) {
      if (rank == world_rank)
        continue;
      int ret =
        MPI_Isend(&number, 1, MPI_INT, rank, 0, MPI_COMM_WORLD, &reqs[rank]);
      assert(ret == MPI_SUCCESS);
    }
    printf("%d sleeping\n", world_rank);
    fflush(stdout);
    sleep(SLEEP_PER_ITERATION);
    for (rank = 0; rank < world_size; rank++) {
      if (rank == world_rank)
        continue;
      int flag = 0;
      while (!flag) {
        MPI_Test(&reqs[rank], &flag, MPI_STATUS_IGNORE);
      }
    }

    for (rank = 0; rank < world_size; rank++) {
      if (rank == world_rank)
        continue;
      int ret = MPI_Recv(&number, 1, MPI_INT, rank, 0, MPI_COMM_WORLD,
                         MPI_STATUS_IGNORE);
      assert(ret == MPI_SUCCESS);
      assert(number == 11223344 + iterations);
      printf("%d received %d from %d\n", world_rank, number, rank);
      fflush(stdout);
    }
    number++;
  }
  MPI_Finalize();
}
