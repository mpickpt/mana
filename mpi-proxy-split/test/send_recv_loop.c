/*
  Test for the MPI_Send and MPI_Recv methods

  Must run with >2 ranks
  Defaults to 5 iterations
  Intended to be run with mana_test.py

  Source: www.mpitutorial.com
*/
#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SLEEP_PER_ITERATION 5
#define MESSAGES_PER_ITERATION 10

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
  int world_rank, world_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (world_rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  // We are assuming at least 3 processes for this task
  if (world_size < 3) {
    fprintf(stderr, "World size must be greater than or equal to 3 for %s\n",
            argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    // rank 0 wait a while then send messages to rank 1
    int number = 0;
    if (world_rank == 0) {
      sleep(SLEEP_PER_ITERATION);
      for (int i = 0; i < MESSAGES_PER_ITERATION; i++) {
        int ret = MPI_Send(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        assert(ret == MPI_SUCCESS);
        number++;
        ret = MPI_Barrier(MPI_COMM_WORLD);
        assert(ret == MPI_SUCCESS);
        printf("Rank 0 have successfully sent %d messages to rank 1\n", i + 1);
        fflush(stdout);
      }
    } else if (world_rank == 1) {
      // rank 1 receive message from 0 first and then 2
      int number = 0;
      for (int i = 0; i < MESSAGES_PER_ITERATION; i++) {
        int recv_number = -1;
        MPI_Recv(&recv_number, 1, MPI_INT, 0, 0, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        assert(number == recv_number);
        MPI_Recv(&recv_number, 1, MPI_INT, 2, 0, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        assert(number == recv_number);
        number++;
        MPI_Barrier(MPI_COMM_WORLD);
        printf("Rank 1 have successfully received %d messages\n", (i + 1) * 2);
        fflush(stdout);
      }
    } else {
      // rank 2 send messages to rank 1 right away
      int number = 0;
      for (int i = 0; i < MESSAGES_PER_ITERATION; i++) {
        int ret = MPI_Send(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        assert(ret == MPI_SUCCESS);
        number++;
        ret = MPI_Barrier(MPI_COMM_WORLD);
        assert(ret == MPI_SUCCESS);
        printf("Rank 2 have successfully sent %d messages to rank 1\n", i + 1);
        fflush(stdout);
      }
    }
  }
  MPI_Finalize();
  return 0;
}
