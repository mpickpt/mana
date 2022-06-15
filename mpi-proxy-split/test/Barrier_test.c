#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define BUFFER_SIZE 100
#define RUNTIME 30
#define SLEEP_PER_ITERATION 1

int main(int argc, char *argv[])
{
    int rank, nprocs;
    int iterations; clock_t start_time;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);

    start_time = clock();
    iterations = 0;
    
    for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
        printf("Entering  %d of %d\n", rank, nprocs);fflush(stdout);
        sleep(rank*SLEEP_PER_ITERATION);
        MPI_Barrier(MPI_COMM_WORLD);
        printf("Everyone should be entered by now. If not then Test Failed!\n");
        fflush(stdout);
        sleep(SLEEP_PER_ITERATION);
        iterations+=rank+1;
    }
    
    MPI_Finalize();
    return 0;
}
