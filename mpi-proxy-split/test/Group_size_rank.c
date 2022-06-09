/*
  Test for the MPI_Group methods

  Run with >2 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 10000

  Source: http://mpi.deino.net/mpi_functions/MPI_Group_size.html
*/

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <string.h>

int main( int argc, char **argv )
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

  int errs=0, toterr;
  MPI_Group basegroup;
  MPI_Comm comm;
  int grp_rank, rank, grp_size, size;
  int worldrank;

  MPI_Init(NULL, NULL);
  MPI_Comm_rank( MPI_COMM_WORLD, &worldrank );
  comm = MPI_COMM_WORLD;
  MPI_Comm_group( comm, &basegroup );
  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &size );

  if(rank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations<max_iterations; iterations++) {
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
  }
  return 0;
}
