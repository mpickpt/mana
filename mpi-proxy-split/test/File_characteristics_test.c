/*
  Test for the MPI_File_set_view, MPI_File_get_view, MPI_File_get_atomicity,
  and MPI_File_set_atomicity

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
  char writebuf[BUF_SIZE];
  char recv_rep[16];
  const char* datareps[3] = {"native", "internal", "external32"};
  for (int i = 0; i < BUF_SIZE; i++) {
      writebuf[i] = i;
  }
  MPI_Datatype etype = MPI_CHAR;
  MPI_Datatype ftype = MPI_CHAR;
  MPI_Offset disp = 0;
  MPI_File_write(handle, writebuf, BUF_SIZE, MPI_INT, MPI_STATUS_IGNORE);
  int flag = 1;

  for (int iterations = 0; iterations < max_iterations; iterations++) {
      // Set view and atomicity
      ret = MPI_File_set_view(handle, (MPI_Offset)iterations, MPI_INT, MPI_INT,
                              datareps[iterations % 3], MPI_INFO_NULL);
      assert(ret == MPI_SUCCESS);
      ret = MPI_File_set_atomicity(handle, (iterations & 1));
      assert(ret == MPI_SUCCESS);

      sleep(5);

      // Check view and atomicity
      ret = MPI_File_get_view(handle, &disp, &etype, &ftype, recv_rep);
      assert(ret == MPI_SUCCESS);

      ret = MPI_File_get_atomicity(handle, &flag);
      assert(ret == MPI_SUCCESS);

      assert(disp == iterations);
      assert(etype == MPI_INT);
      assert(ftype == MPI_INT);

      assert(flag == (iterations & 1));

      // Update variables for next iteration
      etype = MPI_CHAR;
      ftype = MPI_CHAR;
      disp = 0;

      fprintf(stderr, "Iteration %d complete\n", iterations);
      fflush(stderr);
  }
  ret = MPI_File_close(&handle);
  assert(ret == MPI_SUCCESS);

  MPI_Finalize();
  return 0;
}
