/*
  Test for the MPI_Comm_dup method
  Also tests MPI_Comm_create, MPI_Comm group methods

  Run with >2 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 5

  Source: http://mpi.deino.net/mpi_functions/MPI_Comm_dup.html
*/

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#define SLEEP_PER_ITERATION 5

int main(int argc, char* argv[] )
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

  MPI_Comm dup_comm_world, world_comm;
  MPI_Group world_group;
  int world_rank, world_size, rank, size;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );
  MPI_Comm_size( MPI_COMM_WORLD, &world_size );

  if(world_rank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {

    MPI_Comm_dup( MPI_COMM_WORLD, &dup_comm_world );
    /* Exercise Comm_create by creating an equivalent
      to dup_comm_world (sans attributes) */
    MPI_Comm_group( dup_comm_world, &world_group );
    MPI_Comm_create( dup_comm_world, world_group, &world_comm );
    MPI_Comm_rank( world_comm, &rank );
    if (rank != world_rank) {
        printf( "incorrect rank in world comm: %d\n", rank );fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 3001 );
    }
    printf("[Rank %d] \n", rank);fflush(stdout);

    sleep(SLEEP_PER_ITERATION);
  }
  MPI_Finalize();
  return 0;
}
