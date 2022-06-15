// Author: Wesley Bland
// Copyright 2015 www.mpitutorial.com
// This code is provided freely with the tutorials on mpitutorial.com. Feel
// free to modify it for your own use. Any distribution of the code must
// either provide a link to www.mpitutorial.com or keep this header intact.
//
// Example using MPI_Comm_split to divide a communicator into subcommunicators
//

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

#define RUNTIME 30
#define SLEEP_PER_ITERATION 5
#define NUM_RANKS 4

int main(int argc, char **argv) {
  MPI_Init(NULL, NULL);

  // Get the rank and size in the original communicator
  int world_rank, world_size;
  int iterations; clock_t start_time;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (world_size % NUM_RANKS != 0) {
    fprintf(stderr, "World size should be multiple of %d  for %s\n", NUM_RANKS, argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  int color = world_rank / NUM_RANKS; // Determine color based on row

  // Split the communicator based on the color
  // and use the original rank for ordering
  MPI_Comm row_comm;
  start_time = clock();
  iterations = 0;

  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
    MPI_Comm_split(MPI_COMM_WORLD, color, world_rank, &row_comm);

    int row_rank, row_size;
    MPI_Comm_rank(row_comm, &row_rank);
    MPI_Comm_size(row_comm, &row_size);
    assert( row_size == NUM_RANKS);
    assert( row_rank == world_rank % NUM_RANKS);
    printf("WORLD RANK/SIZE: %d/%d --- ROW RANK/SIZE: %d/%d\n",
          world_rank, world_size, row_rank, row_size);
    fflush(stdout);

    iterations++;
    sleep(SLEEP_PER_ITERATION);
  }

  MPI_Comm_free(&row_comm);

  MPI_Finalize();
}
