/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Reduce.html
*/
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

#define BUFFER_SIZE 100
#define RUNTIME 30
#define SLEEP_PER_ITERATION 5

/* A simple test of Reduce with all choices of root process */
int main( int argc, char *argv[] )
{
  int errs = 0;
  int rank, size, root;
  int *sendbuf, *recvbuf, i;
  int minsize = 2, count;
  MPI_Comm comm;
  int iterations; clock_t start_time;

  MPI_Init( &argc, &argv );

  comm = MPI_COMM_WORLD;
  /* Determine the sender and receiver */
  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &size );

  start_time = clock();
  iterations = 0;
  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
    for (count = 1; count < 10; count = count * 2) {
      sendbuf = (int *)malloc( count * sizeof(int) );
      recvbuf = (int *)malloc( count * sizeof(int) );
      for (root = 0; root < size; root ++) {
        for (i=0; i<count; i++) sendbuf[i] = i+iterations;
        for (i=0; i<count; i++) recvbuf[i] = -1;
        MPI_Reduce( sendbuf, recvbuf, count, MPI_INT, MPI_SUM, root, comm );
        if (rank == root) {
          for (i=0; i<count; i++) {
            printf("[Rank = %d]: recvd = %d, expected = %d\n",
                  rank, recvbuf[i], (i+iterations) * size);
            fflush(stdout);
            assert(recvbuf[i] == (i+iterations) * size);
          }
        }
      }
      free( sendbuf );
      free( recvbuf );
    }
    iterations++;
    sleep(SLEEP_PER_ITERATION);
  }
  MPI_Finalize();
  return errs;
}
