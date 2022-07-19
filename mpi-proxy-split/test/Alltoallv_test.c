/*
  Test for the MPI_Alltoallv method

  Run with >2 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Source: http://mpi.deino.net/mpi_functions/MPI_Alltoallv.html
*/
#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 100

/*
   This program tests MPI_Alltoallv by having processor i send different
   amounts of data to each processor.
   The first test sends i items to processor i from all processors.
 */
int
main(int argc, char **argv)
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  MPI_Comm comm;
  int *sbuf, *rbuf;
  int rank, size;
  int *sendcounts, *recvcounts, *rdispls, *sdispls;
  int i, j, *p, err;

  MPI_Init(&argc, &argv);
  err = 0;
  comm = MPI_COMM_WORLD;
  /* Create the buffer */
  MPI_Comm_size(comm, &size);
  MPI_Comm_rank(comm, &rank);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  sbuf = (int *)malloc(size * size * sizeof(int));
  rbuf = (int *)malloc(size * size * sizeof(int));
  if (!sbuf || !rbuf) {
    fprintf(stderr, "Could not allocated buffers!\n");
    MPI_Abort(comm, 1);
  }
  /* Load up the buffers */
  for (i = 0; i < size * size; i++) {
    sbuf[i] = i + BUFFER_SIZE * rank;
    rbuf[i] = -i;
  }
  /* Create and load the arguments to alltoallv */
  sendcounts = (int *)malloc(size * sizeof(int));
  recvcounts = (int *)malloc(size * sizeof(int));
  rdispls = (int *)malloc(size * sizeof(int));
  sdispls = (int *)malloc(size * sizeof(int));
  if (!sendcounts || !recvcounts || !rdispls || !sdispls) {
    fprintf(stderr, "Could not allocate arg items!\n");
    fflush(stderr);
    MPI_Abort(comm, 1);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (i = 0; i < size; i++) {
      sendcounts[i] = i;
      recvcounts[i] = rank;
      rdispls[i] = i * rank;
      sdispls[i] = (i * (i + 1)) / 2;
    }
    int ret = MPI_Alltoallv(sbuf, sendcounts, sdispls, MPI_INT, rbuf,
                            recvcounts, rdispls, MPI_INT, comm);
    assert(ret == MPI_SUCCESS);
    /* Check rbuf */
    for (i = 0; i < size; i++) {
      p = rbuf + rdispls[i];
      for (j = 0; j < rank; j++) {
        if (p[j] != i * BUFFER_SIZE + (rank * (rank + 1)) / 2 + j) {
          int exp = (i * (i + 1)) / 2 + j;
          fprintf(stderr, "[%d] got %d expected %d for %dth\n", rank, p[j], exp,
                  j);
          fflush(stderr);
          err++;
          assert(p[j] == i * BUFFER_SIZE + (rank * (rank + 1)) / 2 + j);
        } else {
          int expected = i * BUFFER_SIZE + (rank * (rank + 1)) / 2 + j;
          fprintf(stdout, "[%d]=> got %d expected %d for %dth\n", rank, p[j],
                  expected, j);
          fflush(stdout);
        }
      }
    }
  }

  free(sdispls);
  free(rdispls);
  free(recvcounts);
  free(sendcounts);
  free(rbuf);
  free(sbuf);
  MPI_Finalize();
  return 0;
}
