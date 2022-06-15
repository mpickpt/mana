/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Group_size.html
*/
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

#define RUNTIME 30
#define SLEEP_PER_ITERATION 5

int main( int argc, char **argv )
{
  int errs=0, toterr;
  MPI_Group basegroup;
  MPI_Comm comm;
  int grp_rank, rank, grp_size, size;
  int worldrank;
  int iterations; clock_t start_time;

  MPI_Init(NULL, NULL);
  MPI_Comm_rank( MPI_COMM_WORLD, &worldrank );
  comm = MPI_COMM_WORLD;
  MPI_Comm_group( comm, &basegroup );
  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &size );

  start_time = clock();
  iterations = 0;
  
  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {

    /* Get the basic information on this group */
    MPI_Group_rank( basegroup, &grp_rank );
    if (grp_rank != rank) {
      errs++;
      fprintf( stdout, "group rank %d != comm rank %d\n",
                      grp_rank, rank );fflush(stdout);
      fflush(stdout);
    }
    MPI_Group_size( basegroup, &grp_size );
    if (grp_size != size) {
      errs++;
      fprintf( stdout, "group size %d != comm size %d\n", grp_size, size );
      fflush(stdout);
    }
    assert(errs == 0);
    if (rank == 0) {
      printf("Test passed!\n");
      fflush(stdout);
    }

    iterations++;
    sleep(SLEEP_PER_ITERATION);
  }
  return 0;
}
