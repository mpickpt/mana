/*
  Test for the MPI_Comm keyval methods

  Must run with >2 ranks for non-trivial results
  Defaults to 5 iterations
  Intended to be run with mana_test.py
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define BUFFER_SIZE 3
#define SLEEP_PER_ITERATION 5

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 5; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int key[BUFFER_SIZE], attrval[BUFFER_SIZE];
  int i;
  int flag;
  MPI_Comm comm;

  MPI_Init(&argc, &argv);
  comm = MPI_COMM_WORLD;
  /* Create key values */
  for (i = 0; i < BUFFER_SIZE; i++) {
    MPI_Comm_create_keyval(MPI_NULL_COPY_FN, MPI_NULL_DELETE_FN, &key[i],
                           (void *)0);
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int *val;
    for (int i = 0; i < BUFFER_SIZE; i++) {
      attrval[i] = 100 + i + iterations;
    }
    for (i = 0; i < BUFFER_SIZE; i++) {
      int ret = MPI_Comm_set_attr(comm, key[i], &attrval[i]);
      assert(ret == MPI_SUCCESS);
      ret = MPI_Comm_get_attr(comm, key[i], &val, &flag);
      assert(ret == MPI_SUCCESS);
      printf("keyval: %d, attrval: %d\n", key[i], *val);
      fflush(stdout);
    }

    printf("Will now sleep for %d seconds ...\n", SLEEP_PER_ITERATION);
    fflush(stdout);
    sleep(SLEEP_PER_ITERATION);

    for (i = 0; i < BUFFER_SIZE; i++) {
      int ret = MPI_Comm_get_attr(comm, key[i], &val, &flag);
      assert(ret == MPI_SUCCESS);
      printf("keyval: %d, attrval: %d\n", key[i], *val);
      fflush(stdout);
    }
  }

  for (i = 0; i < BUFFER_SIZE; i++) {
    MPI_Comm_free_keyval(&key[i]);
  }

  MPI_Finalize();
  return 0;
}
