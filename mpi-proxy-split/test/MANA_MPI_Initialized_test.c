#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * MANA Initializes MPI in the lower half, even before 
 * user applications calls for MPI_Init in its code.
 * Therefore, a call to MPI_Initialized in user-applciation
 * before calling MPI_Init will return 'TRUE'
 * (which is incorrect behavior since we want MANA to be 
 * an invisible layer between MPI-library and user-applciation).
 *
 * Therefore, we have added a patch to mitigate this and
 * this test is created to detect future regression.
 *
 */
int main(int argc, char *argv[]) {
  int flag;
  int rank, size;
  
  // check if MPI is initialized before MPI_Init 
  MPI_Initialized(&flag);
  if (flag) {
    fprintf(stderr, "ERROR: MPI should not be initialized before MPI_Init!\n");
    exit(EXIT_FAILURE);
  }
  
  // Initialize MPI env
  MPI_Init(NULL, NULL);
  
  // check if MPI is initialized after MPI_Init 
  MPI_Initialized(&flag);
  if (!flag) {
    fprintf(stderr, "ERROR: MPI should be initialized after MPI_Init!\n");
    exit(EXIT_FAILURE);
  }

  // making MPI-function call to conduct operational sanity test
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  printf("[%d/%d]: Hello World!\n", rank, size);
  fflush(stdout);

  // Finalize MPI env
  MPI_Finalize();

  return 0;

}

