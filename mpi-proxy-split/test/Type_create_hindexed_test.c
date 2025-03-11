/*
  Test for the MPI_Type_create_hindexed method

  Due to MANA implementatino, also tests MPI_Type_hindexed

  Run with 2 ranks
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Inspired by:
    http://mpi.deino.net/mpi_functions/MPI_Type_create_hindexed.html
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

  MPI_Datatype type, type2;
  int blocklen[3] = { 2, 3, 1 };
  MPI_Aint displacement[3] = { 0, 7*sizeof(int), 18*sizeof(int) };

  MPI_Type_contiguous(3, MPI_INT, &type2);
  MPI_Type_commit(&type2);
  MPI_Type_create_hindexed(3, blocklen, displacement, type2, &type);
  MPI_Type_commit(&type);

  int buffer[21];

  for (int i = 0; i < max_iterations; i++) {
    if (i % 100 == 0) {
        fprintf(stderr, "Iteration: %d complete\n", i);
        fflush(stderr);
    }
    for (int j = 0; j < 21; j++) {
        buffer[j] = i + j;
    }

    if (rank == 0) {
      int ret = MPI_Send(buffer, 1, type, 1, 123 + i, MPI_COMM_WORLD);
      assert(ret == MPI_SUCCESS);
    } else {
      memset(buffer, 0, 21*sizeof(int));
      int ret = MPI_Recv(buffer, 1, type, 0, 123 + i, MPI_COMM_WORLD,
                         MPI_STATUS_IGNORE);
      assert(ret == MPI_SUCCESS);
      fflush(stderr);
      for (int j = 0; j < 21; j++) {
        fflush(stderr);
        if (j != 6 && j != 16 && j != 17) {
          assert(buffer[j] == i+j);
        }
        else {
          assert(buffer[j] == 0);
        }
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
  MPI_Type_free(&type);
  MPI_Type_free(&type2);
  MPI_Finalize();
  return EXIT_SUCCESS;
}
