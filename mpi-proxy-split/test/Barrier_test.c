/*
  Test for the MPI_Barrier method

  Run with >1 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 5
*/

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>

#define BUFFER_SIZE 100
#define SLEEP_PER_ITERATION 1

int main(int argc, char *argv[])
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

    int rank, nprocs;
    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);

    if(rank == 0){
        printf("Running test for %d iterations\n", max_iterations);
    }

    for (int iterations = 0; iterations < max_iterations; iterations++) {

        printf("Entering  %d of %d\n", rank, nprocs);fflush(stdout);
        sleep(rank*SLEEP_PER_ITERATION);
        MPI_Barrier(MPI_COMM_WORLD);
        printf("Everyone should be entered by now. If not then Test Failed!\n");
        fflush(stdout);
    }

    MPI_Finalize();
    return 0;
}
