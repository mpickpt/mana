/*
  Test for the async P2P methods with large payload

  Must run with 2 ranks
  Defaults to 5 iterations
  Intended to be run with mana_test.py
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MSG_SIZE 256 * 1024 // large message
#define SLEEP_PER_ITERATION 5

int
main(int argc, char **argv)
{
  // Parse runtime argument
  int max_iterations = 5; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  char *data = malloc(MSG_SIZE);
  int counter = 0;
  char *recv_buf = malloc(MSG_SIZE);
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_size, world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Request req;

  if (world_rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  // We are assuming 2 processes for this task
  if (world_size != 2) {
    fprintf(stderr, "World size must be 2 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (int i = 0; i < MSG_SIZE; i++) {
      data[i] = i + iterations;
    }
    if (world_rank == 0) {
      printf("Rank 0 sleeping\n");
      fflush(stdout);
      sleep(SLEEP_PER_ITERATION);
      printf("Rank 0 draining from rank 1\n");
      fflush(stdout);
      int ret =
        MPI_Irecv(recv_buf, MSG_SIZE, MPI_BYTE, 1, 0, MPI_COMM_WORLD, &req);
      assert(ret == MPI_SUCCESS);
      ret = MPI_Wait(&req, MPI_STATUSES_IGNORE);
      assert(ret == MPI_SUCCESS);
      printf("Rank 0 drained the message\n");
      assert(memcmp(data, recv_buf, MSG_SIZE) == 0);
      fflush(stdout);
    }

    if (world_rank == 1) {
      printf("Rank 1 sending to rank 0\n");
      fflush(stdout);
      int ret = MPI_Isend(data, MSG_SIZE, MPI_BYTE, 0, 0, MPI_COMM_WORLD, &req);
      assert(ret == MPI_SUCCESS);
      ret = MPI_Wait(&req, MPI_STATUSES_IGNORE);
      assert(ret == MPI_SUCCESS);
    }
  }

  free(data);
  free(recv_buf);
  MPI_Finalize();
}
