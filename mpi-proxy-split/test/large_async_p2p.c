/*
  Test for the async P2P methods with large payload

  Must run with 2 ranks
  Run with -i [iterations] for specific number of iterations, defaults to 5
*/

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <getopt.h>

#define MSG_SIZE 256*1024 // large message
#define SLEEP_PER_ITERATION 5

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

  char *data = malloc(MSG_SIZE);
  int counter = 0;
  char *recv_buf = malloc(MSG_SIZE);
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Request req;

  if(world_rank == 0){
    printf("Running test for %d iterations\n", max_iterations);
  }

  // We are assuming 2 processes for this task
  if (world_size != 2) {
    fprintf(stderr, "World size must be 2 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  for(int iterations = 0; iterations < max_iterations; iterations++){
    for(int i = 0; i < MSG_SIZE; i++){
      data[i] = i+iterations;
    }
    if (world_rank == 0) {
      printf("Rank 0 sleeping\n");
      fflush(stdout);
      sleep(SLEEP_PER_ITERATION);
      printf("Rank 0 draining from rank 1\n");
      fflush(stdout);
      MPI_Irecv(recv_buf, MSG_SIZE, MPI_BYTE, 1, 0, MPI_COMM_WORLD, &req);
      MPI_Wait(&req, MPI_STATUSES_IGNORE);
      printf("Rank 0 drained the message\n");
      assert(memcmp(data, recv_buf, MSG_SIZE) == 0);
      fflush(stdout);
    }

    if (world_rank == 1) {
      printf("Rank 1 sending to rank 0\n");
      fflush(stdout);
      MPI_Isend(data, MSG_SIZE, MPI_BYTE, 0, 0, MPI_COMM_WORLD, &req);
      MPI_Wait(&req, MPI_STATUSES_IGNORE);
    }
  }

  free( data );
  free( recv_buf );
  MPI_Finalize();
}
