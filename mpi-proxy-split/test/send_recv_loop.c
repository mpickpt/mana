// Author: Wes Kendall
// Copyright 2011 www.mpitutorial.com
// This code is provided freely with the tutorials on mpitutorial.com. Feel
// free to modify it for your own use. Any distribution of the code must
// either provide a link to www.mpitutorial.com or keep this header in tact.
//
// MPI_Send, MPI_Recv example. Communicates the number -1 from process 0
// to processe 1.
//
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>

int main(int argc, char** argv) {
  int iteration = 10;
  if (argc > 1) {
    iteration = atoi(argv[1]);
  }

  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // We are assuming at least 2 processes for this task
  if (world_size < 3) {
    fprintf(stderr, "World size must be greater than or equal to 3 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  // rank 0 wait a while then send messages to rank 1
  if (world_rank == 0) {
    int number = 0;
    for (int i = 0; i < iteration; i++) {
      sleep(10);
      MPI_Send(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
      number++;
      MPI_Barrier(MPI_COMM_WORLD);
      printf("Rank 0 have successfully sent %d messages to rank 1\n", i + 1);
      fflush(stdout);
    }
  } else if (world_rank == 1) {
    // rank 1 receive message from 0 first and then 2
    int number = 0;
    for (int i = 0; i < iteration; i++) {
      int recv_number = -1;
      MPI_Recv(&recv_number, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      assert(number == recv_number);
      MPI_Recv(&recv_number, 1, MPI_INT, 2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      assert(number == recv_number);
      number++;
      MPI_Barrier(MPI_COMM_WORLD);
      printf("Rank 1 have successfully received %d messages\n", (i + 1) * 2);
      fflush(stdout);
    }
  } else {
    // rank 2 send messages to rank 1 right away
    int number = 0;
    for (int i = 0; i < iteration; i++) {
      MPI_Send(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
      number++;
      MPI_Barrier(MPI_COMM_WORLD);
      printf("Rank 2 have successfully sent %d messages to rank 1\n", i + 1);
      fflush(stdout);
    }

  }
  MPI_Finalize();
  return 0;
}
