/*
  Test for the MPI_Alltoall method

  Run with >2 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Alltoall.html
*/

#include <assert.h>
#include <errno.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations;
  max_iterations = 10000;
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int rank, size;
  int chunk = 100;
  int i;
  int *sb;
  int *rb;
  int status, gstatus, ret;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  for (i = 1; i < argc; ++i) {
    if (argv[i][0] != '-')
      continue;
    switch (argv[i][1]) {
      case 'm':
        chunk = atoi(argv[++i]);
        break;
      default:
        fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
        fflush(stderr);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
  }
  sb = (int *)malloc(size * chunk * sizeof(int));
  if (!sb) {
    perror("can't allocate send buffer");
    fflush(stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }
  rb = (int *)malloc(size * chunk * sizeof(int));
  if (!rb) {
    perror("can't allocate recv buffer");
    fflush(stderr);
    free(sb);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }
  for (i = 0; i < size * chunk; ++i) {
    sb[i] = rank + 1;
    rb[i] = 0;
  }
  for (int j = 0; j < max_iterations; j++) {
    status =
      MPI_Alltoall(sb, chunk, MPI_INT, rb, chunk, MPI_INT, MPI_COMM_WORLD);
#ifdef DEBUG
    printf("[Rank = %d] Status = %d, size = %d, chunk = %d\n", rank, status,
           size, chunk);
    fflush(stdout);
#endif
    ret = MPI_Allreduce(&status, &gstatus, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);
    for (i = 0; i < size * chunk; ++i) {
      assert(rb[i] == (int)(i / chunk) + 1);
    }
    if (rank == 0) {
      if (gstatus != 0) {
        printf("all_to_all returned %d\n", gstatus);
        fflush(stdout);
        assert(gstatus == MPI_SUCCESS);
      }
    }
    for (i = 0; i < size * chunk; ++i) {
      assert(rb[i] == (int)(i / chunk) + 1);
      rb[i] = 0; // clear the recv buffer
    }
#ifdef DEBUG
    printf("[Rank %d]: Test Passed!\n", rank);
    fflush(stdout);
#endif
  }

  free(sb);
  free(rb);
  MPI_Finalize();
  return (EXIT_SUCCESS);
}
