#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define BUFFER_SIZE 3
#define RUNTIME 30
#define SLEEP_PER_ITERATION 5

int main( int argc, char *argv[] )
{
  int key[BUFFER_SIZE], attrval[BUFFER_SIZE];
  int i;
  int flag;
  MPI_Comm comm;
  int iterations; clock_t start_time;

  MPI_Init( &argc, &argv );
  comm = MPI_COMM_WORLD;
  /* Create key values */
  for (i=0; i<BUFFER_SIZE; i++) {
    MPI_Comm_create_keyval(MPI_NULL_COPY_FN, MPI_NULL_DELETE_FN,
        &key[i], (void *)0 );
  }

  start_time = clock();
  iterations = 0;

  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
    int *val;
    for(int i = 0; i < BUFFER_SIZE; i++){
      attrval[i] = 100+i+iterations;
    }
    for (i = 0; i < BUFFER_SIZE; i++) {
      MPI_Comm_set_attr(comm, key[i], &attrval[i]);
      MPI_Comm_get_attr(comm, key[i], &val, &flag);
      printf("keyval: %d, attrval: %d\n", key[i], *val);
      fflush(stdout);
    }

    printf("Will now sleep for %d seconds ...\n", SLEEP_PER_ITERATION);
    fflush(stdout);
    sleep(SLEEP_PER_ITERATION);

    for (i = 0; i < BUFFER_SIZE; i++) {
      MPI_Comm_get_attr(comm, key[i], &val, &flag);
      printf("keyval: %d, attrval: %d\n", key[i], *val);
      fflush(stdout);
    }

    iterations++;
  }

  for (i=0; i<BUFFER_SIZE; i++) {
    MPI_Comm_free_keyval( &key[i] );
  }

  MPI_Finalize();
  return 0;
}
