/*
  Test for the MPI_File_set_size and MPI_File_get_size methods

  Defaults to 10 iterations
  Intended to be run with mana_test.py

*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 4096

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 10; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int access_mode, ret;
  MPI_Comm comm;

  MPI_Init(&argc, &argv);
  comm = MPI_COMM_WORLD;

  // Open file
  MPI_File handle;
  access_mode = MPI_MODE_CREATE | MPI_MODE_RDWR | MPI_MODE_DELETE_ON_CLOSE;
  ret = MPI_File_open(comm, "file.tmp", access_mode, MPI_INFO_NULL, &handle);
  assert(ret == MPI_SUCCESS);

  // Initialize buffers and other local variables
  MPI_Offset size = 0;

  for (int iterations = 0; iterations < max_iterations; iterations++) {
      // Set size either larger or smaller than current size
      ret = MPI_File_set_size(handle, BUF_SIZE / ((iterations & 1) + 1));
      assert(ret == MPI_SUCCESS);

      sleep(5);

      // Read back current size
      MPI_File_get_size(handle, &size);
      assert(size == BUF_SIZE / ((iterations & 1) + 1));

      fprintf(stderr, "Iteration %d complete\n", iterations);
      fflush(stderr);
  }
  ret = MPI_File_close(&handle);
  assert(ret == MPI_SUCCESS);

  MPI_Finalize();
  return 0;
}
