/*
  Test for the MPI_Reduce keyval method

  Run with >1 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 10000

    Source: http://mpi.deino.net/mpi_functions/MPI_Reduce.html
*/
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#define BUFFER_SIZE 100

/* A simple test of Reduce with all choices of root process */
int main( int argc, char *argv[] )
{
  //Parse runtime argument
  int opt, max_iterations;
  max_iterations = 10000;
  while ((opt = getopt(argc, argv, "i:")) != -1) {
    switch(opt)
    {
      case 'i':
        if(optarg != NULL){
          char* optarg_end;
          max_iterations = strtol(optarg, &optarg_end, 10);
          if(max_iterations != 0 && optarg_end - optarg == strlen(optarg))
            break;
        }
      default:
        fprintf(stderr, "Unrecognized argument received \n\
          -i [iterations]: Set test iterations (default 10000)\n");
        return 1;
    }
  }

  int errs = 0;
  int rank, size, root;
  int *sendbuf, *recvbuf, i;
  int minsize = 2, count;
  MPI_Comm comm;
  MPI_Init( &argc, &argv );

  comm = MPI_COMM_WORLD;
  /* Determine the sender and receiver */
  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &size );

  if(rank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations<max_iterations; iterations++){
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
  }
  MPI_Finalize();
  return errs;
}
