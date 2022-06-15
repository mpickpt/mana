/*
  Source: http://hpc.mines.edu/examples/examples/mpi/c_ex0NUM_RANKS.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

#define RUNTIME 30
#define SLEEP_PER_ITERATION 5
#define NUM_RANKS 4
/************************************************************
This is a simple broadcast program in MPI
************************************************************/

int main(argc,argv)
int argc;
char *argv[];
{
    int i,myid, numprocs;
    int source;
    int buffer[NUM_RANKS];
    int expected_output[NUM_RANKS];
    MPI_Status status;
    MPI_Request request;
    int iterations; clock_t start_time;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);

    assert(numprocs == NUM_RANKS);

    start_time = clock();
    iterations = 0;
    
    for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
      source = iterations % NUM_RANKS;
      if(myid == source){
        for(i=0; i<NUM_RANKS; i++)
          buffer[i] = i+iterations;
      }
      for (i = 0; i < NUM_RANKS; i++)
      {
          expected_output[i] = i+iterations;
      }
      MPI_Bcast(buffer,NUM_RANKS,MPI_INT,source,MPI_COMM_WORLD);
      printf("[Rank = %d]", myid);
      for(i=0;i<NUM_RANKS;i++)
      {
        assert(buffer[i] == expected_output[i]);
        printf(" %d", buffer[i]);
      }
      printf("\n");fflush(stdout);
      iterations++;
      sleep(SLEEP_PER_ITERATION);
    }
    MPI_Finalize();
}
