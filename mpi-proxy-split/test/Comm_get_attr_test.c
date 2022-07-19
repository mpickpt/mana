/*
  Test for the MPI_Comm_get_attr method

  Defaults to 30 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Comm_dup.html
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SLEEP_PER_ITERATION 1

int
main(int argc, char **argv)
{
  // Parse runtime argument
  int max_iterations = 30; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  void *v;
  int flag;
  int vval;
  int rank, size;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_TAG_UB, &v, &flag);
    assert(flag);
    vval = *(int *)v;
    assert(vval >= 32767);
    printf("Got MPI_TAG_UB\n");
    fflush(stdout);

    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_HOST, &v, &flag);
    assert(flag);
    vval = *(int *)v;
    assert(!((vval < 0 || vval >= size) && vval != MPI_PROC_NULL));
    printf("Got HOST\n");
    fflush(stdout);

    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_IO, &v, &flag);
    assert(flag);
    vval = *(int *)v;
    assert(!((vval < 0 || vval >= size) && vval != MPI_ANY_SOURCE &&
             vval != MPI_PROC_NULL));
    printf("Got MPI_IO\n");
    fflush(stdout);

    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, &v, &flag);
    vval = *(int *)v;
    assert(!flag || !(vval < 0 || vval > 1));
    printf("Got MPI_WTIME_IS_GLOBAL\n");
    fflush(stdout);

    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_APPNUM, &v, &flag);
    vval = *(int *)v;
    assert(!flag || vval >= 0);
    printf("Got MPI_APPNUM\n");
    fflush(stdout);

    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_UNIVERSE_SIZE, &v, &flag);
    vval = *(int *)v;
    assert(!flag || vval >= size);
    printf("Got MPI_UNIVERSE_SIZE\n");
    fflush(stdout);

    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_LASTUSEDCODE, &v, &flag);
    vval = *(int *)v;
    assert(!flag || vval >= MPI_ERR_LASTCODE);
    printf("Got MPI_LASTUSEDCODE\n");
    fflush(stdout);

    sleep(SLEEP_PER_ITERATION);
  }
  MPI_Finalize();
  return 0;
}
