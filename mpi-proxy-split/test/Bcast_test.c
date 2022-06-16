/*
  Test for the MPI_Allgather method

  Run with >2 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 5

  Source: http://hpc.mines.edu/examples/examples/mpi/c_ex0NUM_RANKS.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#define NUM_RANKS 4
/************************************************************
This is a simple broadcast program in MPI
************************************************************/

int main(argc,argv)
int argc;
char *argv[];
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

    int i,myid, numprocs;
    int source;
    int buffer[NUM_RANKS];
    int expected_output[NUM_RANKS];
    MPI_Status status;
    MPI_Request request;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);

    if(myid == 0){
        printf("Running test for %d iterations\n", max_iterations);
    }

    assert(numprocs == NUM_RANKS);

    for (int iterations = 0; iterations < max_iterations; iterations++) {

      source = iterations % NUM_RANKS;
      if(myid == source){
        for(i=0; i<NUM_RANKS; i++)
          buffer[i] = i+iterations;
      }
      for (i = 0; i < NUM_RANKS; i++)
      {
          expected_output[i] = i+iterations;
      }
      MPI_Bcast(buffer,NUM_RANKS,MPI_INT,source,MPI_COMM_WORLD);
      printf("[Rank = %d]", myid);
      for(i=0;i<NUM_RANKS;i++)
      {
        assert(buffer[i] == expected_output[i]);
        printf(" %d", buffer[i]);
      }
      printf("\n");fflush(stdout);
    }
    MPI_Finalize();
}
