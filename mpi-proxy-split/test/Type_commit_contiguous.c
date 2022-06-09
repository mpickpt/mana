/*
  Test for the MPI_Type_commit method

  Run with >1 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 10000

  Source: http://mpi.deino.net/mpi_functions/MPI_Type_commit.html
*/

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define BUFFER_SIZE 100
#define NUM_RANKS 4

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

  int myrank;
  MPI_Status status;
  MPI_Datatype type;
  int buffer[BUFFER_SIZE];
  int buf[BUFFER_SIZE];

  MPI_Init(&argc, &argv);

  MPI_Type_contiguous( BUFFER_SIZE, MPI_INT, &type );
  MPI_Type_commit(&type);
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

  if(myrank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++){

    for(int i = 0; i < BUFFER_SIZE; i++){
      buffer[i] = i + iterations;
    }
    if (myrank == 0)
    {
      MPI_Send(buffer, 1, type, 1, 123+iterations, MPI_COMM_WORLD);
    }
    else if (myrank == 1)
    {
      MPI_Recv(buf, 1, type, 0, 123+iterations, MPI_COMM_WORLD, &status);
      for (int i = 0; i < BUFFER_SIZE ; i++) {
        fflush(stdout);
        assert(buffer[i] == buf[i]);
      }
    }
    printf("Iteration %d completed\n", iterations);
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
