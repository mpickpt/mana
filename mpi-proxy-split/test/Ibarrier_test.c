/*
  Test for the Ibarrier_test method

  Run with >2 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 5
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <getopt.h>

#define SLEEP_PER_ITERATION 5

#define MPI_TEST
#ifndef MPI_TEST
# define MPI_WAIT
#endif

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

    int i,iter,myid, numprocs, flag;
    int iterations;
    MPI_Status status;
    MPI_Request request = MPI_REQUEST_NULL;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);

    if(myid == 0){
        printf("Running test for %d iterations\n", max_iterations);
    }

    for(int iterations = 0; iterations < max_iterations; iterations++){

        MPI_Ibarrier(MPI_COMM_WORLD, &request);
        MPI_Test(&request, &flag, &status);
#ifdef MPI_TEST
        while (1) {
          int flag = 0;
          MPI_Test(&request, &flag, &status);
          if (flag) { break; }
        }
#endif
#ifdef MPI_WAIT
        MPI_Wait(&request, &status);
#endif

        sleep(SLEEP_PER_ITERATION);
    }
    MPI_Finalize();
}
