/*
  Test for the MPI_File_open and MPI_File_close methods

  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Source: https://www.rookiehpc.com/mpi/docs/mpi_file_open.php
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int access_mode;
  MPI_Comm comm;

  MPI_Init(&argc, &argv);
  comm = MPI_COMM_WORLD;

  MPI_File handle;
  access_mode = MPI_MODE_CREATE | MPI_MODE_EXCL | MPI_MODE_RDWR |
                MPI_MODE_UNIQUE_OPEN | MPI_MODE_DELETE_ON_CLOSE;

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    int ret;
    ret = MPI_File_open(comm, "file.tmp", access_mode, MPI_INFO_NULL, &handle);
    assert(ret == MPI_SUCCESS);
    printf("File opened successfully.\n");
    fflush(stdout);

    ret = MPI_File_close(&handle);
    assert(ret == MPI_SUCCESS);
    printf("File closed successfully.\n");
  }

  MPI_Finalize();
  return 0;
}
