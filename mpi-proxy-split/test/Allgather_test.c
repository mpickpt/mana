#include <stdio.h>
#include <mpi.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#define BUFFER_SIZE 100
#define LOOP_COUNT 1000

int main(int argc, char ** argv)
{
  int rank, comm_size;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  if (rank == 0) {
    printf("Comm. size = %d\n", comm_size);
  }
  assert(comm_size > 0);
  int send_buf[BUFFER_SIZE] = {0};
  for (int count = 0; count < LOOP_COUNT; count++) {
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
      send_buf[i] = (rank+1)*100 + i;
    }

    int recv_buf[comm_size * BUFFER_SIZE];
    MPI_Allgather(&send_buf, BUFFER_SIZE, MPI_INT, &recv_buf,
                  BUFFER_SIZE, MPI_INT, MPI_COMM_WORLD);

    for (int i = 0; i < BUFFER_SIZE; i++)
    {
      assert(recv_buf[rank * BUFFER_SIZE + i] == send_buf[i]);
    }
    printf("[Rank = %d]: received correctly!\n", rank);
    fflush(stdout);
    for (int i = 0; i < comm_size * BUFFER_SIZE ; i++)
    {
      #ifdef DEBUG
        printf("[Rank = %d]: receive buffer[%d] = %d\n",rank, i, recv_buf[i]);
        fflush(stdout);
      #endif

      // clear the buffer
      recv_buf[i] = 0;
    }
    sleep(1);
  }
  MPI_Finalize();
  return EXIT_SUCCESS;
}
