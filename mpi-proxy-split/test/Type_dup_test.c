/*
  Test for the MPI_Type_dup method

  Run with 2 ranks
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Inspired by:
    https://rookiehpc.github.io/mpi/docs/mpi_type_create_hvector/index.html
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
  int max_iterations = 10000; // default
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
  assert(comm_size == 2);

  MPI_Datatype column_type[2];
  MPI_Type_vector(BUFFER_SIZE, 1, BUFFER_SIZE, MPI_INT, &column_type[0]);
  MPI_Type_commit(&column_type[0]);
  int buffer[BUFFER_SIZE][BUFFER_SIZE];
  int recvbuf[BUFFER_SIZE];
  int expected_output[BUFFER_SIZE][BUFFER_SIZE];
  for (int i = 0; i < max_iterations; i++) {
    MPI_Type_dup(column_type[i & 1], &column_type[(i+1) & 1]);
    MPI_Type_free(&column_type[i & 1]);
    if (i % 100 == 99) {
      printf("Completed %d iterations\n", i + 1);
      fflush(stdout);
    }
    for (int j = 0; j < BUFFER_SIZE; j++) {
      for (int k = 0; k < BUFFER_SIZE; k++) {
        buffer[j][k] = j + k + i;
        expected_output[j][k] = j + k + i;
      }
    }

    if (rank == 0) {
      int ret = MPI_Send(&buffer[0][1], 1, column_type[(i+1) & 1], 1, 0,
                         MPI_COMM_WORLD);
      assert(ret == MPI_SUCCESS);
    } else {
      int ret = MPI_Recv(&recvbuf, BUFFER_SIZE, MPI_INT, 0, 0, MPI_COMM_WORLD,
                         MPI_STATUS_IGNORE);
      assert(ret == MPI_SUCCESS);
      for (int i = 0; i < BUFFER_SIZE; i++) {
        assert(recvbuf[i] == buffer[i][1]);
      }
    }
  }
  MPI_Finalize();
  return EXIT_SUCCESS;
}