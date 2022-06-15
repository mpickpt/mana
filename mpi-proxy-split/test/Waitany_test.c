/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Waitany.html
*/
#include <mpi.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#define BUFFER_SIZE 100
#define RUNTIME 30
#define SLEEP_PER_ITERATION 5
#define NUM_RANKS 4

int main(int argc, char *argv[])
{
    int rank, size;
    int i, index;
    int buffer[NUM_RANKS * BUFFER_SIZE];
    MPI_Request request[NUM_RANKS];
    MPI_Status status;
    int iterations; clock_t start_time;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (size != NUM_RANKS)
    {
        printf("Please run with %d processes.\n", NUM_RANKS);fflush(stdout);
        MPI_Finalize();
        return 1;
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    start_time = clock();
    iterations = 0;
    for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
        if (rank == 0)
        {
            for (i=0; i<size * BUFFER_SIZE; i++)
                buffer[i] = i/BUFFER_SIZE + iterations;
            for (i=0; i<size-1; i++)
            {
                MPI_Isend(&buffer[i*BUFFER_SIZE], BUFFER_SIZE, MPI_INT, i+1, 123+iterations, MPI_COMM_WORLD,
                        &request[i]);
            }
            MPI_Waitany(size-1, request, &index, &status);
            sleep(SLEEP_PER_ITERATION);
        }
        else
        {
            sleep(SLEEP_PER_ITERATION);
            MPI_Recv(buffer, BUFFER_SIZE, MPI_INT, 0, 123+iterations, MPI_COMM_WORLD, &status);
            printf("%d: buffer[0] = %d\n", rank, buffer[0]);fflush(stdout);
            assert(buffer[0] == rank - 1 + iterations);
        }
        memset(buffer, 0, BUFFER_SIZE * sizeof(int));
        iterations++;
    }
    
    MPI_Finalize();
    return 0;
}
