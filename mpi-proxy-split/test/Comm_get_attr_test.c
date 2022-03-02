#include "mpi.h"
#include <stdio.h>

int main( int argc, char **argv)
{
    void *v;
    int flag;
    int vval;
    int rank, size;

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_TAG_UB, &v, &flag );
    if (!flag) {
        fprintf( stderr, "Could not get TAG_UB\n" );fflush(stderr);
    }
    else {
        vval = *(int*)v;
        if (vval < 32767) {
            fprintf( stderr, "Got too-small value (%d) for TAG_UB\n", vval );fflush(stderr);
        }
    }

    MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_HOST, &v, &flag );
    if (!flag) {
        fprintf( stderr, "Could not get HOST\n" );fflush(stderr);
    }
    else {
        vval = *(int*)v;
        if ((vval < 0 || vval >= size) && vval != MPI_PROC_NULL) {
            fprintf( stderr, "Got invalid value %d for HOST\n", vval );fflush(stderr);
        }
    }

    MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_IO, &v, &flag );
    if (!flag) {
        fprintf( stderr, "Could not get IO\n" );fflush(stderr);
    }
    else {
        vval = *(int*)v;
        if ((vval < 0 || vval >= size) && vval != MPI_ANY_SOURCE && vval != MPI_PROC_NULL) {
            fprintf( stderr, "Got invalid value %d for IO\n", vval );fflush(stderr);
        }
    }

    MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, &v, &flag );
    if (flag) {
        /* Wtime need not be set */
        vval = *(int*)v;
        if (vval < 0 || vval > 1) {
            fprintf( stderr, "Invalid value for WTIME_IS_GLOBAL (got %d)\n", vval );fflush(stderr);
        }
    }

    MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_APPNUM, &v, &flag );
    /* appnum need not be set */
    if (flag) {
        vval = *(int *)v;
        if (vval < 0) {
            fprintf( stderr, "MPI_APPNUM is defined as %d but must be nonnegative\n", vval );fflush(stderr);
        }
    }

    MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_UNIVERSE_SIZE, &v, &flag );
    /* MPI_UNIVERSE_SIZE need not be set */
    if (flag) {
        /* But if it is set, it must be at least the size of comm_world */
        vval = *(int *)v;
        if (vval < size) {
            fprintf( stderr, "MPI_UNIVERSE_SIZE = %d, less than comm world (%d)\n", vval, size );fflush(stderr);
        }
    }

    MPI_Comm_get_attr( MPI_COMM_WORLD, MPI_LASTUSEDCODE, &v, &flag );
    /* Last used code must be defined and >= MPI_ERR_LASTCODE */
    if (flag) {
        vval = *(int*)v;
        if (vval < MPI_ERR_LASTCODE) {
            fprintf( stderr, "MPI_LASTUSEDCODE points to an integer (%d) smaller than MPI_ERR_LASTCODE (%d)\n", vval, MPI_ERR_LASTCODE );fflush(stderr);
        }
    }
    else {
        fprintf( stderr, "MPI_LASTUSECODE is not defined\n" );fflush(stderr);
    }
    MPI_Finalize( );
    return 0;
}

