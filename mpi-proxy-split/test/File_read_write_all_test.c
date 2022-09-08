/*
  Test for the MPI_File_read_at_all and MPI_File_write_at_all methods

  Defaults to 1000000 iterations
  Intended to be run with mana_test.py

  Run with >2 ranks for non-trivial results

*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 8

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 1000000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int access_mode, ret, rank, size;
  MPI_Comm comm;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  comm = MPI_COMM_WORLD;

  // Open file
  MPI_File handle;
  access_mode = MPI_MODE_CREATE | MPI_MODE_RDWR | MPI_MODE_DELETE_ON_CLOSE;
  ret = MPI_File_open(comm, "file.tmp", access_mode, MPI_INFO_NULL, &handle);
  assert(ret == MPI_SUCCESS);

  // Initialize input/output buffers
  int writebuf[BUF_SIZE];
  int readbuf[BUF_SIZE];

  ret = MPI_File_set_size(handle, BUF_SIZE*size);

  for (int iterations = 0; iterations < max_iterations; iterations++) {
      for (int i = 0; i < BUF_SIZE; i++) {
        writebuf[i] = i+rank;
      }
      // Write to file
      ret = MPI_File_write_at_all(handle, (MPI_Offset)rank*sizeof(int)*BUF_SIZE,
                                  writebuf, BUF_SIZE, MPI_INT, MPI_STATUS_IGNORE);
      assert(ret == MPI_SUCCESS);

      // Read from file and check results
      ret = MPI_File_read_at_all(handle, (MPI_Offset)rank*sizeof(int)*BUF_SIZE,
                                  readbuf, BUF_SIZE, MPI_INT, MPI_STATUS_IGNORE);
      assert(ret == MPI_SUCCESS);

      // Update buffer for next iteration
      for (int i = 0; i < BUF_SIZE; i++) {
          assert(readbuf[i] == writebuf[i]);
      }

      for (int i = 0; i < BUF_SIZE; i++) {
          writebuf[i]++;
      }
      fprintf(stderr, "Iteration %d complete\n", iterations);
      fflush(stderr);
  }
  ret = MPI_File_close(&handle);
  assert(ret == MPI_SUCCESS);

  MPI_Finalize();
  return 0;
}
