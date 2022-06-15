#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <mpi.h>

#define LOOP_COUNT 50
#define CHAR_COUNT 100

int main( int argc, char *argv[] )
{
  char *mem;
  int rank, ret;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  for (int i = 0; i < LOOP_COUNT; i++) {
    ret = MPI_Alloc_mem(CHAR_COUNT * sizeof(char), MPI_INFO_NULL, &mem);
    assert(ret == MPI_SUCCESS);
    printf("Rank %d allocated %d chars at %p\n", rank, CHAR_COUNT, &mem);
    fflush(stdout);

    sleep(2);

    ret = MPI_Free_mem(mem);
    assert(ret == MPI_SUCCESS);
    printf("Rank %d freed %p\n", rank, &mem);
    fflush(stdout);
  }
  MPI_Finalize();
  return 0;
}
