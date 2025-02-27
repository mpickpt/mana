/*
 * FILE: unsync_Finalize.c 
 * Description: This test triggers an *unsynchronized call* 
 *          to `MPI_Finalize` by intentionally delaying 
 *          the finalize call in rank 0. 
 *          If correct synchronization mechanism is not employed 
 *          in MPI_Finalize wrapper, it causes premature termination.
 */

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[]){
  int rank, size;
  MPI_Init(&argc, &argv);
  
  // get rank and comm size
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // requires atleast 2 processes
  if (size < 2) {
    printf("ERROR: requires at least 2 processes!\n");
    return 1;
  }

  printf("[%d/%d]: Says Hello!\n", rank, size);
  
  // wait for all the processes to say hello!
  MPI_Barrier(MPI_COMM_WORLD);

  // Rank 0: sleep for 4 sec before saying goodbye
  if(rank == 0){
    printf("Rank 0 sleeping for 4 sec\n");
    sleep(4);
    printf("Rank %d says: Goodbye!\n", rank);
  }
  else{
    printf("Rank %d says: Goodbye!\n", rank);
  }
  
  MPI_Finalize();
}
