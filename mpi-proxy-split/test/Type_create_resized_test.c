/*
  Test for the MPI_Type_create_resized method

  Run with 2 ranks
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Inspired by:
    http://mpi.deino.net/mpi_functions/MPI_Type_create_resized.html
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

  int rank, comm_size, ret;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  if (rank == 0) {
    printf("Comm. size = %d\n", comm_size);
    printf("Running test for %d iterations\n", max_iterations);
  }
  fflush(stdout);
  assert(comm_size == 2);

  MPI_Datatype newtype;
  ret = MPI_Type_create_resized(MPI_INT, 0, 3 * sizeof(int), &newtype);
  assert(ret == MPI_SUCCESS);
  ret = MPI_Type_commit(&newtype);
  assert(ret == MPI_SUCCESS);
  int buf[BUFFER_SIZE*3];
  memset(buf, 0, BUFFER_SIZE*3*sizeof(int));

  for (int i = 0; i < max_iterations; i++) {
    if (i % 100 == 99) {
      printf("Completed %d iterations\n", i + 1);
      fflush(stdout);
    }
    if (rank == 0) {
      for (int k = 0; k < BUFFER_SIZE; k++) {
        buf[3*k] = i+k;
      }
      ret = MPI_Send(buf, BUFFER_SIZE, newtype, 1, 123+i, MPI_COMM_WORLD);
      assert(ret == MPI_SUCCESS);
    } else {
      memset(buf, -1, sizeof(int)*3*BUFFER_SIZE);
      int ret = MPI_Recv(buf, BUFFER_SIZE, newtype, 0, 123+i, MPI_COMM_WORLD,
                          MPI_STATUS_IGNORE);
      assert(ret == MPI_SUCCESS);
      for (int k = 0; k < BUFFER_SIZE; k++) {
        assert(buf[3*k] == i+k);
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
  MPI_Type_free(&newtype);
  MPI_Finalize();
  return EXIT_SUCCESS;
}
