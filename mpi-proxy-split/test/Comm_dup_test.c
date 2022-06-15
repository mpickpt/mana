/*
  Source: http://mpi.deino.net/mpi_functions/MPI_Comm_dup.html
*/
#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define RUNTIME 30
#define SLEEP_PER_ITERATION 5

int main(int argc, char* argv[] )
{
  MPI_Comm dup_comm_world, world_comm;
  MPI_Group world_group;
  int world_rank, world_size, rank, size;
  int iterations; clock_t start_time;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );
  MPI_Comm_size( MPI_COMM_WORLD, &world_size );

  start_time = clock();
  iterations = 0;

  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
    MPI_Comm_dup( MPI_COMM_WORLD, &dup_comm_world );
    /* Exercise Comm_create by creating an equivalent to dup_comm_world (sans attributes) */
    MPI_Comm_group( dup_comm_world, &world_group );
    MPI_Comm_create( dup_comm_world, world_group, &world_comm );
    MPI_Comm_rank( world_comm, &rank );
    if (rank != world_rank) {
        printf( "incorrect rank in world comm: %d\n", rank );fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 3001 );
    }
    printf("[Rank %d] \n", rank);fflush(stdout);

    iterations++;
    sleep(SLEEP_PER_ITERATION);
  }
  MPI_Finalize();
  return 0;
}
