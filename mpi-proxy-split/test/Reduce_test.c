/*
  Test for the MPI_Reduce method

  Run with >1 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Reduce.html
*/
#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 100

/* A simple test of Reduce with all choices of root process */
int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int errs = 0;
  int rank, size, root;
  int *sendbuf, *recvbuf, i;
  int minsize = 2, count;
  MPI_Comm comm;
  MPI_Init(&argc, &argv);

  comm = MPI_COMM_WORLD;
  /* Determine the sender and receiver */
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (count = 1; count < 10; count = count * 2) {
      sendbuf = (int *)malloc(count * sizeof(int));
      recvbuf = (int *)malloc(count * sizeof(int));
      for (root = 0; root < size; root++) {
        for (i = 0; i < count; i++)
          sendbuf[i] = i + iterations;
        for (i = 0; i < count; i++)
          recvbuf[i] = -1;
        int ret =
          MPI_Reduce(sendbuf, recvbuf, count, MPI_INT, MPI_SUM, root, comm);
        assert(ret == MPI_SUCCESS);
        if (rank == root) {
          for (i = 0; i < count; i++) {
            printf("[Rank = %d]: recvd = %d, expected = %d\n", rank, recvbuf[i],
                   (i + iterations) * size);
            fflush(stdout);
            assert(recvbuf[i] == (i + iterations) * size);
          }
        }
      }
      free(sendbuf);
      free(recvbuf);
    }
  }
  MPI_Finalize();
  return EXIT_SUCCESS;
}
