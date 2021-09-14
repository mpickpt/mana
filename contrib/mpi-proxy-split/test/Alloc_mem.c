#include "mpi.h"
#include <stdio.h>
#include <unistd.h>

int main( int argc, char *argv[] )
{
  char *mem;
  int rank;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Alloc_mem(100 * sizeof(char), MPI_INFO_NULL, &mem);
  printf("Rank %d allocated 100 chars at %p\n", rank, &mem);
  fflush(stdout);
  sleep(5);
  MPI_Free_mem(mem);
  printf("Rank %d freed %p\n", rank, &mem);
  fflush(stdout);
  MPI_Finalize();
  return 0;
}
