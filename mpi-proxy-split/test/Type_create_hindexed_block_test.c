/*
  Test for the MPI_Type_create_hindexed_block method

  Run with 2 ranks
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Inspired by:
    https://rookiehpc.github.io/mpi/docs/
        mpi_type_create_hindexed_block/index.html
*/

#define _POSIX_C_SOURCE 199309L

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

  MPI_Datatype corner_type;
  MPI_Aint displacements[4] = { 0, 2 * sizeof(int), 6 * sizeof(int), 8 * sizeof(int)};
  MPI_Type_create_hindexed_block(4, 1, displacements, MPI_INT, &corner_type);
  MPI_Type_commit(&corner_type);
  int buf[9];
  int recvbuf[4];

  for (int i = 0; i < max_iterations; i++) {
    if (i % 100 == 99) {
      printf("Completed %d iterations\n", i + 1);
      fflush(stdout);
    }
    if (rank == 0) {
      for (int k = 0; k < 9; k++) {
        buf[k] = i+k;
      }
      ret = MPI_Send(buf, 1, corner_type, 1, 123+i, MPI_COMM_WORLD);
      assert(ret == MPI_SUCCESS);
    } else {
      memset(recvbuf, 0, 4*sizeof(int));
      int ret = MPI_Recv(recvbuf, 4, MPI_INT, 0, 123+i, MPI_COMM_WORLD,
                          MPI_STATUS_IGNORE);
      assert(ret == MPI_SUCCESS);
      assert(recvbuf[0] == i);
      assert(recvbuf[1] == i+2);
      assert(recvbuf[2] == i+6);
      assert(recvbuf[3] == i+8);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }
  MPI_Type_free(&corner_type);
  MPI_Finalize();
  return EXIT_SUCCESS;
}
