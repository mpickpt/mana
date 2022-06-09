/*
  Test for the MPI_Comm_split method

  Must run with an even multiple of NUM_RANKS (default 4) ranks
  Run with -i [iterations] for specific number of iterations, defaults to 60

  Source: www.mpitutorial.com
*/

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <getopt.h>

#define NUM_RANKS 4
#define SLEEP_PER_ITERATION 0.5

int main(int argc, char **argv) {
  //Parse runtime argument
    int opt, max_iterations;
    max_iterations = 60;
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
            -i [iterations]: Set test iterations (default 60)\n");
            return 1;
        }
    }

  MPI_Init(NULL, NULL);

  // Get the rank and size in the original communicator
  int world_rank, world_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if(world_rank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  if (world_size % NUM_RANKS != 0) {
    fprintf(stderr, "World size should be multiple \
      of %d for %s\n", NUM_RANKS, argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  int color = world_rank / NUM_RANKS; // Determine color based on row

  // Split the communicator based on the color
  // and use the original rank for ordering
  MPI_Comm row_comm;
  for (int iterations = 0; iterations < max_iterations; iterations++) {
    MPI_Comm_split(MPI_COMM_WORLD, color, world_rank, &row_comm);

    int row_rank, row_size;
    MPI_Comm_rank(row_comm, &row_rank);
    MPI_Comm_size(row_comm, &row_size);
    assert( row_size == NUM_RANKS);
    assert( row_rank == world_rank % NUM_RANKS);
    printf("WORLD RANK/SIZE: %d/%d --- ROW RANK/SIZE: %d/%d\n",
          world_rank, world_size, row_rank, row_size);
    fflush(stdout);

    sleep(SLEEP_PER_ITERATION);
  }

  MPI_Comm_free(&row_comm);

  MPI_Finalize();
}
