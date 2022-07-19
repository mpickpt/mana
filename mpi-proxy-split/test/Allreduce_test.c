/*
  Test for the MPI_Allreduce method

  Run with >2 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Allreduce.html
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 1000

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int *in, *out, *sol;
  int i, fnderr = 0;
  int rank, size;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  in = (int *)malloc(BUFFER_SIZE * sizeof(int));
  out = (int *)malloc(BUFFER_SIZE * sizeof(int));
  sol = (int *)malloc(BUFFER_SIZE * sizeof(int));

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
    fflush(stdout);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (i = 0; i < BUFFER_SIZE; i++) {
      *(in + i) = i;
      *(sol + i) = i * size;
      *(out + i) = 0;
    }
    int ret =
      MPI_Allreduce(in, out, BUFFER_SIZE, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);
    for (i = 0; i < BUFFER_SIZE; i++) {
      printf("[Rank = %d] at index = %d: In = %d, Out = %d, Expected Out = %d\
            \n",
             rank, i, *(in + i), *(out + i), *(sol + i));
      fflush(stdout);

      assert((*(out + i) == *(sol + i)));
      if (*(out + i) != *(sol + i)) {
        fnderr++;
      }
    }
    if (fnderr) {
      fprintf(stderr, "(%d) Error for type MPI_INT and op MPI_SUM\n", rank);
      fflush(stderr);
    }
  }
  free(in);
  free(out);
  free(sol);
  MPI_Finalize();
  return EXIT_SUCCESS;
}
