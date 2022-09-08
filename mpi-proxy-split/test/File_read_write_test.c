/*
  Test for the MPI_File_read and MPI_File_write methods
  Also tests MPI_Seek method

  Defaults to 10 iterations
  Intended to be run with mana_test.py

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

  // Initialize input/output buffers
  int writebuf[BUF_SIZE];
  int readbuf[BUF_SIZE];
  for (int i = 0; i < BUF_SIZE; i++) {
      writebuf[i] = i;
  }

  for (int iterations = 0; iterations < max_iterations; iterations++) {
      // Write to file
      ret = MPI_File_write(handle, writebuf, BUF_SIZE, MPI_INT,
                           MPI_STATUS_IGNORE);
      assert(ret == MPI_SUCCESS);
      // Reset file location before checkpoint to test that the offset
      // is correctly being restored
      ret = MPI_File_seek(handle, sizeof(int)*BUF_SIZE*iterations,
                          MPI_SEEK_SET);
      assert(ret == MPI_SUCCESS);

      sleep(5);

      // Read from file and check results
      ret = MPI_File_read(handle, readbuf, BUF_SIZE, MPI_INT,
                          MPI_STATUS_IGNORE);
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
