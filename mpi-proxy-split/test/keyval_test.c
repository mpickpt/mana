/*
  Test for the MPI_Comm keyval methods

  Must run with >2 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 5
*/

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <string.h>

#define BUFFER_SIZE 3
#define SLEEP_PER_ITERATION 5

int main( int argc, char *argv[] )
{
  //Parse runtime argument
  int opt, max_iterations;
  max_iterations = 5;
  while ((opt = getopt(argc, argv, "i:")) != -1) {
    switch(opt)
    {
      case 'i':
        if(optarg != NULL){
          char* optarg_end;
          max_iterations = strtol(optarg, &optarg_end, 10);
          if(max_iterations != 0 && optarg_end - optarg == strlen(optarg))
            break;
        }
      default:
        fprintf(stderr, "Unrecognized argument received \n\
          -i [iterations]: Set test iterations (default 5)\n");
        return 1;
    }
  }

  int key[BUFFER_SIZE], attrval[BUFFER_SIZE];
  int i;
  int flag;
  MPI_Comm comm;

  MPI_Init( &argc, &argv );
  comm = MPI_COMM_WORLD;
  /* Create key values */
  for (i=0; i<BUFFER_SIZE; i++) {
    MPI_Comm_create_keyval(MPI_NULL_COPY_FN, MPI_NULL_DELETE_FN,
        &key[i], (void *)0 );
  }

  for(int iterations = 0; iterations < max_iterations; iterations++){
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
  }

  for (i=0; i<BUFFER_SIZE; i++) {
    MPI_Comm_free_keyval( &key[i] );
  }

  MPI_Finalize();
  return 0;
}
