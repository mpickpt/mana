/*
  Test for the MPI_Type_vector method

  Run with 2 ranks
  Run with -i [iterations] for specific number of iterations, defaults to 10000
  Inspired by:
    https://rookiehpc.github.io/mpi/docs/mpi_type_create_hvector/index.html
*/

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <mpi.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#define BUFFER_SIZE 100

int main(int argc, char ** argv)
{
  // Parse runtime argument
  int opt, max_iterations;
  max_iterations = 10000;
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
          -i [iterations]: Set test iterations (default 10000)\n");
        return 1;
    }
  }
  int rank, comm_size;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  if (rank == 0) {
    printf("Comm. size = %d\n", comm_size);
    printf("Running test for %d iterations\n", max_iterations);
  }
  fflush(stdout);
  assert(comm_size == 2);

  MPI_Datatype column_type;
  MPI_Type_vector(BUFFER_SIZE, 1,
                   BUFFER_SIZE, MPI_INT, &column_type);
  MPI_Type_commit(&column_type);
  int buffer[BUFFER_SIZE][BUFFER_SIZE];
  int recvbuf[BUFFER_SIZE];
  int expected_output[BUFFER_SIZE][BUFFER_SIZE];
  for(int i = 0; i < max_iterations; i++) {
      if(i % 100 == 99) {
          printf("Completed %d iterations\n", i+1); fflush(stdout);
      }
      for(int j = 0; j < BUFFER_SIZE; j++) {
          for(int k=0; k < BUFFER_SIZE; k++) {
              buffer[j][k] = j+k+i;
              expected_output[j][k] = j+k+i;
          }
      }

      if(rank == 0) {
          MPI_Send(&buffer[0][1], 1, column_type, 1, 0, MPI_COMM_WORLD);
      }
      else{
          MPI_Recv(&recvbuf, BUFFER_SIZE, MPI_INT, 0, 0, MPI_COMM_WORLD,
                   MPI_STATUS_IGNORE);
          for(int i = 0; i < BUFFER_SIZE; i++) {
              assert(recvbuf[i] == buffer[i][1]);
          }
      }
  }
  MPI_Finalize();
  return EXIT_SUCCESS;
}