/*
  Test for the MPI_Comm_get_attr method

  Run with -i [iterations] for specific number of iterations, defaults to 30

  Source: http://mpi.deino.net/mpi_functions/MPI_Comm_dup.html
*/

#include "mpi.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>

#define SLEEP_PER_ITERATION 1

int main( int argc, char **argv)
{
    //Parse runtime argument
    int opt, max_iterations;
    max_iterations = 30;
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
            -i [iterations]: Set test iterations (default 30)\n");
            return 1;
        }
    }

    void *v;
    int flag;
    int vval;
    int rank, size;

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    if(rank == 0){
        printf("Running test for %d iterations\n", max_iterations);
    }

    for (int iterations = 0; iterations < max_iterations; iterations++) {
        MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_TAG_UB, &v, &flag );
        assert(flag);
        vval = *(int*)v;
        assert(vval >= 32767);
        printf("Got MPI_TAG_UB\n"); fflush(stdout);

        MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_HOST, &v, &flag );
        assert(flag);
        vval = *(int*)v;
        assert(!((vval < 0 || vval >= size) && vval != MPI_PROC_NULL));
        printf("Got HOST\n"); fflush(stdout);

        MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_IO, &v, &flag );
        assert(flag);
        vval = *(int*)v;
        assert(!((vval < 0 || vval >= size) &&
            vval != MPI_ANY_SOURCE && vval != MPI_PROC_NULL));
        printf("Got MPI_IO\n"); fflush(stdout);

        MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, &v, &flag );
        vval = *(int*)v;
        assert(!flag || !(vval < 0 || vval > 1));
        printf("Got MPI_WTIME_IS_GLOBAL\n"); fflush(stdout);

        MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_APPNUM, &v, &flag );
        vval = *(int*)v;
        assert(!flag || vval>=0 );
        printf("Got MPI_APPNUM\n"); fflush(stdout);

        MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_UNIVERSE_SIZE, &v, &flag );
        vval = *(int*)v;
        assert(!flag || vval >= size);
        printf("Got MPI_UNIVERSE_SIZE\n"); fflush(stdout);

        MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_LASTUSEDCODE, &v, &flag );
        vval = *(int*)v;
        assert(!flag || vval >= MPI_ERR_LASTCODE);
        printf("Got MPI_LASTUSEDCODE\n"); fflush(stdout);

        sleep(SLEEP_PER_ITERATION);
    }
    MPI_Finalize( );
    return 0;
}

