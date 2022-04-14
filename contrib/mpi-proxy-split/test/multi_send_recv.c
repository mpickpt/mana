#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

int main(int argc, char** argv) {
  int data = 0;
  int recv_buf = 0;
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // We are assuming 2 processes for this task
  if (world_size != 2) {
    fprintf(stderr, "World size must be 2 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  while (1) {
    MPI_Barrier(MPI_COMM_WORLD);
    // For rank 0, send data 3 time. After each send, increment the data by 1.
    if (world_rank == 0) {
      printf("Rank %d sending integer %d to %d\n", world_rank, data, data + 2);
      fflush(stdout);
      MPI_Send(&data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
      data++;
      MPI_Send(&data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
      data++;
      MPI_Send(&data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
      data++;
    }

    printf("Rank %d sleeping\n", world_rank);
    fflush(stdout);
    sleep(5);

    // For rank 1, recv data 3 time. After each recv, increment the data by 1.
    // Also chech the data received.
    if (world_rank == 1) {
      printf("Rank %d recving integer %d to %d\n", world_rank, data, data + 2);
      fflush(stdout);
      MPI_Recv(&recv_buf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      assert(recv_buf == data);
      data++;
      MPI_Recv(&recv_buf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      assert(recv_buf == data);
      data++;
      MPI_Recv(&recv_buf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      assert(recv_buf == data);
      data++;
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
  MPI_Finalize();
}
