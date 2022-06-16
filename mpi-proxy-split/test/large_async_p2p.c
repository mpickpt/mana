// For testing, checkpoint this during the 5-second sleep between
// MPI_Isend (rank 1) and MPI_Irecv (rank 0).
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#define MSG_SIZE 256*1024 // large message
// #define MSG_SIZE 64 // small message

int main(int argc, char** argv) {
  void *data = malloc(MSG_SIZE);
  int counter = 0;
  void *recv_buf = malloc(MSG_SIZE);
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Request req;

  // We are assuming 3 processes for this task
  if (world_size != 2) {
    fprintf(stderr, "World size must be 2 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  if (world_rank == 0) {
    printf("Rank 0 sleeping\n");
    fflush(stdout);
    sleep(5);
    printf("Rank 0 draining from rank 1\n");
    fflush(stdout);
    MPI_Irecv(recv_buf, MSG_SIZE, MPI_BYTE, 1, 0, MPI_COMM_WORLD, &req);
    MPI_Wait(&req, MPI_STATUSES_IGNORE);
    printf("Rank 0 drained the message\n");
    fflush(stdout);
  }

  if (world_rank == 1) {
    printf("Rank 1 sending to rank 0\n");
    fflush(stdout);
    MPI_Isend(data, MSG_SIZE, MPI_BYTE, 0, 0, MPI_COMM_WORLD, &req);
    MPI_Wait(&req, MPI_STATUSES_IGNORE);
  }
  MPI_Finalize();
}
