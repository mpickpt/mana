/*
  Test for the MPI_Allgather method

  Run with >2 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py
*/

#define _POSIX_C_SOURCE 199309L

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 100

int
main(int argc, char **argv)
{
  // Parse runtime argument
  int max_iterations;
  max_iterations = 10000;
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }
  int rank, comm_size;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  if (rank == 0) {
    printf("Comm. size = %d\n", comm_size);
    printf("Running test for %d iterations\n", max_iterations);
  }
  fflush(stdout);
  assert(comm_size > 0);

  int send_buf[BUFFER_SIZE] = { 0 };
  int *recv_buf = (int *)malloc(comm_size * BUFFER_SIZE * sizeof(int));
  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (int i = 0; i < BUFFER_SIZE; i++) {
      send_buf[i] = (rank + 1) * 100 + i + iterations;
    }

    int ret = MPI_Allgather(send_buf, BUFFER_SIZE, MPI_INT, recv_buf,
                            BUFFER_SIZE, MPI_INT, MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);

    int total_rank = 0;
    for (int i = 0; i < comm_size; i++) {
      // Infer rank of this section of buffer from first value
      int buf_portion_rank = (recv_buf[i * BUFFER_SIZE] - iterations) / 100 - 1;
      total_rank += buf_portion_rank;
      for (int j = 0; j < BUFFER_SIZE; j++) {
        assert(recv_buf[i * BUFFER_SIZE + j] ==
               (buf_portion_rank + 1) * 100 + j + iterations);
      }
    }

    // Check that each rank is present exactly once
    assert(total_rank == (comm_size * (comm_size - 1)) / 2);

    printf("[Rank = %d]: received correctly!\n", rank);
    fflush(stdout);
    for (int i = 0; i < comm_size * BUFFER_SIZE; i++) {
#ifdef DEBUG
      printf("[Rank = %d]: receive buffer[%d] = %d\n", rank, i, recv_buf[i]);
      fflush(stdout);
#endif

      // clear the buffer
      recv_buf[i] = 0;
    }
  }

  free(recv_buf);
  MPI_Finalize();
  return EXIT_SUCCESS;
}
