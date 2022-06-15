#include "mpi.h"
#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

#define RUNTIME 30
#define SLEEP_PER_ITERATION 5

int main( int argc, char **argv)
{
    void *v;
    int flag;
    int vval;
    int rank, size;
    int iterations; clock_t start_time;

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    start_time = clock();
    iterations = 0;
    
    for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
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
        assert(!((vval < 0 || vval >= size) && vval != MPI_ANY_SOURCE && vval != MPI_PROC_NULL));
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

        iterations++;
        sleep(SLEEP_PER_ITERATION);
    }
    MPI_Finalize( );
    return 0;
}

