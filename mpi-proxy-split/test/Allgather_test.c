#include <stdio.h>
#include <mpi.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define BUFFER_SIZE 100
#define RUNTIME 30
#define SLEEP_PER_ITERATION 1

int main(int argc, char ** argv)
{
  int rank, comm_size;
  int iterations; clock_t start_time;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  if (rank == 0) {
    printf("Comm. size = %d\n", comm_size);
  }
  assert(comm_size > 0);

  int send_buf[BUFFER_SIZE] = {0};
  start_time = clock();
  iterations = 0;
  
  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
      send_buf[i] = (rank+1)*100 + i + iterations;
    }

    int recv_buf[comm_size * BUFFER_SIZE];
    MPI_Allgather(&send_buf, BUFFER_SIZE, MPI_INT, &recv_buf,
                  BUFFER_SIZE, MPI_INT, MPI_COMM_WORLD);

    int total_rank = 0;
    for(int i = 0; i < comm_size; i++){                                  
      int buf_portion_rank = (recv_buf[i*BUFFER_SIZE]-iterations)/100-1; //Infer rank of this section of buffer from first value
      total_rank+=buf_portion_rank;
      for(int j = 0; j < BUFFER_SIZE; j++){ 
        assert(recv_buf[i*BUFFER_SIZE+j] == (buf_portion_rank+1)*100 + j + iterations);  
      }
    }
    assert(total_rank == (comm_size * (comm_size-1))/2); //Check that each rank is present exactly once

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
    sleep(SLEEP_PER_ITERATION);
    iterations++;
  }
  MPI_Finalize();
  return EXIT_SUCCESS;
}
