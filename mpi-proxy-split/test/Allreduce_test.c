/*
  Test for the MPI_Allreduce method

  Run with >2 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 10000

  Source: http://mpi.deino.net/mpi_functions/MPI_Allreduce.html
*/

#include <mpi.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#define BUFFER_SIZE 1000

int main(int argc, char *argv[])
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

  int *in, *out, *sol;
  int i, fnderr=0;
  int rank, size;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  in = (int *)malloc(BUFFER_SIZE * sizeof(int));
  out = (int *)malloc(BUFFER_SIZE * sizeof(int));
  sol = (int *)malloc(BUFFER_SIZE * sizeof(int));

  if(rank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  for(int i = 0; i < max_iterations; i++) {
    for (i=0; i<BUFFER_SIZE; i++)
    {
      *(in + i) = i;
      *(sol + i) = i*size;
      *(out + i) = 0;
    }
    MPI_Allreduce(in, out, max_iterations, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    for (i=0; i<BUFFER_SIZE; i++)
    {
      #ifdef DEBUG
        printf("[Rank = %d] at index = %d: In = %d, Out = %d, Expected Out = %d\
              \n", rank, i, *(in + i), *(out + i), *(sol + i));
        fflush(stdout);
      #endif

      assert((*(out + i) == *(sol + i)));
      if (*(out + i) != *(sol + i))
      {
        fnderr++;
      }
    }
    if (fnderr)
    {
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
