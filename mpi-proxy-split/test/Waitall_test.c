/*
  Test for the MPI_Waitall method

  Must run with NUM_RANKS (4) processes
  Run with -i [iterations] for specific number of iterations, defaults to 5

  Source: http://mpi.deino.net/mpi_functions/MPI_Waitall.html
*/

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

#define BUFFER_SIZE 100
#define SLEEP_PER_ITERATION 5
#define NUM_RANKS 4

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

    int rank, size;
    int i;
    int buffer[NUM_RANKS * BUFFER_SIZE];
    MPI_Request request[NUM_RANKS];
    MPI_Status status[NUM_RANKS];

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (size != NUM_RANKS)
    {
        printf("Please run with %d processes.\n", NUM_RANKS);fflush(stdout);
        MPI_Finalize();
        return 1;
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if(rank == 0){
        printf("Running test for %d iterations\n", max_iterations);
    }

    for(int iterations = 0; iterations < max_iterations; iterations++){

        if (rank == 0)
        {
            for (i=0; i<size * BUFFER_SIZE; i++)
                buffer[i] = i/BUFFER_SIZE + iterations;
            for (i=0; i<size-1; i++)
            {
                MPI_Isend(&buffer[i*BUFFER_SIZE], BUFFER_SIZE, MPI_INT, i+1, 123+iterations, MPI_COMM_WORLD,
                        &request[i]);
            }
            MPI_Waitall(size-1, request, status);
            sleep(SLEEP_PER_ITERATION);
        }
        else
        {
            sleep(SLEEP_PER_ITERATION);
            MPI_Recv(buffer, BUFFER_SIZE, MPI_INT, 0, 123+iterations, MPI_COMM_WORLD, &status[0]);
            printf("%d: buffer[0] = %d\n", rank, buffer[0]);fflush(stdout);
            assert(buffer[0] == rank - 1 + iterations);
        }
    }

    MPI_Finalize();
    return 0;
}
