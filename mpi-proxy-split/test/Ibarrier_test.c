/*
  Example code for Bcast (not used here):
   https://mpitutorial.com/tutorials/mpi-broadcast-and-collective-communication/
   https://github.com/mpitutorial/mpitutorial/blob/gh-pages/tutorials/mpi-broadcast-and-collective-communication/code/my_bcast.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>
#include <math.h>
#include <assert.h>
#include <time.h>

#define RUNTIME 30
#define SLEEP_PER_ITERATION 5

int main(argc,argv)
int argc;
char *argv[];
{
    int i,iter,myid, numprocs, flag;
    int iterations;
    clock_t start_time;
    MPI_Status status;
    MPI_Request request = MPI_REQUEST_NULL; 

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);

    start_time = clock();
    iterations = 0;

    for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
        MPI_Ibarrier(MPI_COMM_WORLD, &request);
        MPI_Test(&request, &flag, &status);
        if(iterations % 2 == 0){
                while(!flag){
                        MPI_Test(&request, &flag, &status);
                }
        }
        else{
                MPI_Wait(&request, &status);
        }

        iterations++;
        sleep(SLEEP_PER_ITERATION);
    }
    MPI_Finalize();
}
