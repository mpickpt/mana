/*
  Test for the MPI_Waitany method

  Must run with NUM_RANKS (4) processes
  Defaults to 5 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Waitany.html
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 100
#define SLEEP_PER_ITERATION 5
#define NUM_RANKS 4

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 5; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int rank, size;
  int i, index;
  int buffer[NUM_RANKS * BUFFER_SIZE];
  MPI_Request request[NUM_RANKS];
  MPI_Status status;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (size != NUM_RANKS) {
    printf("Please run with %d processes.\n", NUM_RANKS);
    fflush(stdout);
    MPI_Finalize();
    return 1;
  }
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    if (rank == 0) {
      for (i = 0; i < size * BUFFER_SIZE; i++)
        buffer[i] = i / BUFFER_SIZE + iterations;
      for (i = 0; i < size - 1; i++) {
        int ret =
          MPI_Isend(&buffer[i * BUFFER_SIZE], BUFFER_SIZE, MPI_INT, i + 1,
                    123 + iterations, MPI_COMM_WORLD, &request[i]);
        assert(ret == MPI_SUCCESS);
      }
      MPI_Waitany(size - 1, request, &index, &status);
      sleep(SLEEP_PER_ITERATION);
    } else {
      sleep(SLEEP_PER_ITERATION);
      int ret = MPI_Recv(buffer, BUFFER_SIZE, MPI_INT, 0, 123 + iterations,
                         MPI_COMM_WORLD, &status);
      assert(ret == MPI_SUCCESS);
      printf("%d: buffer[0] = %d\n", rank, buffer[0]);
      fflush(stdout);
      assert(buffer[0] == rank - 1 + iterations);
    }
    memset(buffer, 0, BUFFER_SIZE * sizeof(int));
  }

  MPI_Finalize();
  return 0;
}
