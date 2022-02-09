// Author: Wes Kendall
// Copyright 2011 www.mpitutorial.com
// This code is provided freely with the tutorials on mpitutorial.com. Feel
// free to modify it for your own use. Any distribution of the code must
// either provide a link to www.mpitutorial.com or keep this header in tact.
//
// Ping pong example with MPI_Send and MPI_Recv. Two processes ping pong a
// number back and forth, incrementing it until it reaches a given value.
//
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

int main(int argc, char** argv) {
  const int PING_PONG_LIMIT = 100;

  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // We are assuming at least 2 processes for this task
  if (world_size % 2 != 0) {
    fprintf(stderr, "World size must be a multiple of two for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  int ping_pong_count = 0;
  int partner_rank = world_rank % 2 == 0 ?
                     (world_rank + 1) : (world_rank - 1);
  while (ping_pong_count < PING_PONG_LIMIT) {
    if ((world_rank + ping_pong_count) % 2) {
      // Increment the ping pong count before you send it
      ping_pong_count++;
      printf("[%d] Sending %d to %d\n",
             world_rank, ping_pong_count, partner_rank); fflush(stdout);
      MPI_Send(&ping_pong_count, 1, MPI_INT, partner_rank, 0, MPI_COMM_WORLD);
      printf("[%d] Sent ping_pong_count %d to %d\n",
             world_rank, ping_pong_count, partner_rank); fflush(stdout);
    } else {
      printf("[%d] Receiving from %d\n",
             world_rank, partner_rank); fflush(stdout);
      MPI_Recv(&ping_pong_count, 1, MPI_INT, partner_rank, 0, MPI_COMM_WORLD,
               MPI_STATUS_IGNORE);
      assert((world_rank + ping_pong_count) % 2 != 0);
      printf("[%d] Received ping_pong_count %d from %d\n",
             world_rank, ping_pong_count, partner_rank); fflush(stdout);
    }
    sleep(5);
  }
  MPI_Finalize();
}
