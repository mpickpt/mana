/*
  Test for the MPI_Alloc_mem method

  Run with -i [iterations] for specific number of iterations, defaults to 5
*/

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <mpi.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#define CHAR_COUNT 100
#define SLEEP_PER_ITERATION 2

int main( int argc, char *argv[] )
{
  //Parse runtime argument
  int opt, max_iterations;
  max_iterations = 5;
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
          -i [iterations]: Set test iterations (default 5)\n");
        return 1;
    }
  }

  char *mem;
  int rank, ret;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if(rank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    ret = MPI_Alloc_mem(CHAR_COUNT * sizeof(char), MPI_INFO_NULL, &mem);
    assert(ret == MPI_SUCCESS);
    printf("Rank %d allocated %d chars at %p\n", rank, CHAR_COUNT, &mem);
    fflush(stdout);

    sleep(SLEEP_PER_ITERATION);

    ret = MPI_Free_mem(mem);
    assert(ret == MPI_SUCCESS);
    printf("Rank %d freed %p\n", rank, &mem);
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
