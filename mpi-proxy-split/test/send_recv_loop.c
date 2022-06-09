/*
  Test for the MPI_Send and MPI_Recv methods

  Must run with >2 ranks
  Run with -i [iterations] for specific number of iterations, defaults to 5

    Source: www.mpitutorial.com
*/
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <getopt.h>
#include <string.h>

#define SLEEP_PER_ITERATION 5
#define MESSAGES_PER_ITERATION 10

int main(int argc, char** argv) {
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

  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if(world_rank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  // We are assuming at least 2 processes for this task
  if (world_size < 3) {
    fprintf(stderr, "World size must be greater than \
      or equal to 3 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  for(int iterations = 0; iterations < max_iterations; iterations++){
    // rank 0 wait a while then send messages to rank 1
    int number = 0;
    if (world_rank == 0) {
      sleep(SLEEP_PER_ITERATION);
      for (int i = 0; i < MESSAGES_PER_ITERATION; i++) {
        MPI_Send(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        number++;
        MPI_Barrier(MPI_COMM_WORLD);
        printf("Rank 0 have successfully sent %d messages to rank 1\n", i + 1);
        fflush(stdout);
      }
    } else if (world_rank == 1) {
      // rank 1 receive message from 0 first and then 2
      int number = 0;
      for (int i = 0; i < MESSAGES_PER_ITERATION; i++) {
        int recv_number = -1;
        MPI_Recv(&recv_number, 1, MPI_INT, 0,
          0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        assert(number == recv_number);
        MPI_Recv(&recv_number, 1, MPI_INT, 2,
          0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        assert(number == recv_number);
        number++;
        MPI_Barrier(MPI_COMM_WORLD);
        printf("Rank 1 have successfully received %d messages\n", (i + 1) * 2);
        fflush(stdout);
      }
    } else {
      // rank 2 send messages to rank 1 right away
      int number = 0;
      for (int i = 0; i < MESSAGES_PER_ITERATION; i++) {
        MPI_Send(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        number++;
        MPI_Barrier(MPI_COMM_WORLD);
        printf("Rank 2 have successfully sent %d messages to rank 1\n", i + 1);
        fflush(stdout);
      }
    }
  }
  MPI_Finalize();
  return 0;
}
