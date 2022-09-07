#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
  MPI_Init(&argc, &argv);
  
  int size, rank, buf;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  
  if(size != 2){
    printf("The application works only with 2 processes.\n");
    fflush(stdout);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }
   
  if(rank==0){
    buf = 1;
    MPI_Request req;
    MPI_Status stat;
    
    #if 0
    MPI_Irecv(&buf, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &req);
    MPI_Cancel(&req);
    printf("Posted Irecv and cancelled request.\n");fflush(stdout);
    
    #else
    MPI_Isend(&buf, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &req);
    MPI_Cancel(&req);
    printf("Posted Isend and cancelled request.\n");fflush(stdout);

    #endif

    // Sleep
    printf("Sleeping for 30 seconds.\n");fflush(stdout);
    sleep(30);

    MPI_Wait(&req, &stat);
  } 
  MPI_Barrier(MPI_COMM_WORLD);
  if(rank==0){
    printf("Finishing application.\n");fflush(stdout);
  }
  return 0;
}
