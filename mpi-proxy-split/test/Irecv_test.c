#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

int main(int argc, char** argv) {
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int myrank;
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // We are assuming at least 2 processes for this task
  if (world_size < 2) {
    fprintf(stderr, "World size must be greater than 1 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  int rank = 0;
  int number = 11223344;
  int i;

  for (i = 0; i < 100; i++) {
    
    for (rank = 0; rank < world_size; rank++)
    {
      if (rank == myrank)
        continue;
      MPI_Send(&number, 1, MPI_INT, rank, 0, MPI_COMM_WORLD);
      assert(number == 11223344);
      printf("%d sent %d to %d\n", myrank, number, rank);
      fflush(stdout);
    }
  
    MPI_Request reqs[world_size];
    for (rank = 0; rank < world_size; rank++)
    {
      if (rank == myrank)
        continue;
      MPI_Irecv(&number, 1, MPI_INT, rank, 0, MPI_COMM_WORLD, &reqs[rank]);
    }
    printf("%d sleeping\n", myrank);
    fflush(stdout);
    sleep(1);
    for (rank = 0; rank < world_size; rank++)
    {
      if (rank == myrank)
        continue;
      int flag = 0;
      int first_test = 1;
      while (!flag) {
        if (first_test) {
          printf("%d testing request from %d\n", myrank, rank);
          fflush(stdout);
          first_test = 0;
        }
        MPI_Test(&reqs[rank], &flag, MPI_STATUS_IGNORE);
        if (flag) {
          printf("%d completed request from %d\n", myrank, rank);
          fflush(stdout);
        }
      }
    }
  }

  MPI_Finalize();
}

