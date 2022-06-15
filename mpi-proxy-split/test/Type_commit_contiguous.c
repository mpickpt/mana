/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Type_commit.html
*/
#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <string.h>

#define BUFFER_SIZE 100
#define RUNTIME 30
#define SLEEP_PER_ITERATION 5
#define NUM_RANKS 4


int main(int argc, char *argv[])
{
  int myrank;
  MPI_Status status;
  MPI_Datatype type;
  int buffer[BUFFER_SIZE];
  int buf[BUFFER_SIZE];
  int iterations; clock_t start_time;

  MPI_Init(&argc, &argv);

  MPI_Type_contiguous( BUFFER_SIZE, MPI_INT, &type );
  MPI_Type_commit(&type);
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

  start_time = clock();
  iterations = 0;

  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
    for(int i = 0; i < BUFFER_SIZE; i++){
      buffer[i] = i + iterations;
    }
    if (myrank == 0)
    {
      MPI_Send(buffer, 1, type, 1, 123+iterations, MPI_COMM_WORLD);
    }
    else if (myrank == 1)
    {
      MPI_Recv(buf, 1, type, 0, 123+iterations, MPI_COMM_WORLD, &status);
      for (int i = 0; i < BUFFER_SIZE ; i++) {        
        fflush(stdout);
        assert(buffer[i] == buf[i]);
      }
    }
    printf("Iteration %d completed\n", iterations);
    fflush(stdout);
    iterations++;
    sleep(SLEEP_PER_ITERATION);
  }
  MPI_Finalize();
  return 0;
}
