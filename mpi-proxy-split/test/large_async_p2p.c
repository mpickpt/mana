// For testing, checkpoint this during the 5-second sleep between
// MPI_Isend (rank 1) and MPI_Irecv (rank 0).
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define MSG_SIZE 256*1024 // large message
#define RUNTIME 30
#define SLEEP_PER_ITERATION 5

int main(int argc, char** argv) {
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
  int iterations; clock_t start_time;

  // We are assuming 2 processes for this task
  if (world_size != 2) {
    fprintf(stderr, "World size must be 2 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  start_time = clock();
  iterations = 0;
  
  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
    for(int i = 0; i < MSG_SIZE; i++){
      data[i] = i+iterations;
    }
    if (world_rank == 0) {
      printf("Rank 0 sleeping\n");
      fflush(stdout);
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
    iterations++;
    sleep(SLEEP_PER_ITERATION);
  }

  free( data );
  free( recv_buf );
  MPI_Finalize();
}
