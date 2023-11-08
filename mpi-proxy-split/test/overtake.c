#include <mpi.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define MSG_SIZE 1024 * 1024 * 1024 / sizeof(int)

int main (int argc, char **argv) {
  int rank;
  int *bufs[10];
  int size;
  MPI_Request reqs[10];
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (size != 2) {
    printf("This test case requires 2 MPI ranks.\n");
    MPI_Abort();
  }
  
  if (rank == 0) {
    for (int i = 0; i < 10; i++) {
      bufs[i] = (int*) malloc(sizeof(int) * MSG_SIZE);
      *bufs[i] = i;
    }
  } else {
    for (int i = 0; i < 10; i++) {
      bufs[i] = (int*) malloc(sizeof(int) * MSG_SIZE);
      *bufs[i] = 0;
    }
  }

  int round = 0;
  while (1) {
    printf("round %d\n", round++);
    fflush(stdout);
    for (int i = 0; i < 10; i++) {
      if (i == 5) {
        // checkpoitn here
        printf("process %d sleeping\n", rank);
        fflush(stdout);
        sleep(5);
      }
      if (rank == 0) {
        MPI_Isend(bufs[i], MSG_SIZE, MPI_INT, 1, 0, MPI_COMM_WORLD, &reqs[i]);
        printf("Isend %d initiated\n", i);
        fflush(stdout);
      } else {
        MPI_Irecv(bufs[i], MSG_SIZE, MPI_INT, 0, 0, MPI_COMM_WORLD, &reqs[i]);
        printf("Irecv %d initiated\n", i);
        fflush(stdout);
      }
    }
    MPI_Waitall(10, reqs, MPI_STATUS_IGNORE);
    if (rank == 1) {
      for (int i = 0; i < 10; i++) {
        printf("recv buf %d: %d\n", i, *bufs[i]);
        fflush(stdout);
      }
    }
  }
  for (int i = 0; i < 10; i++) {
    free(bufs[i]);
  }
  MPI_Finalize();
  return 0;
}
