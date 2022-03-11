/*
 * Source: https://www.rookiehpc.com/mpi/docs/mpi_file_open.php
 */
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main (int argc, char *argv[])
{
  int access_mode;
  MPI_Comm comm;

  MPI_Init( &argc, &argv );
  comm = MPI_COMM_WORLD;

  MPI_File handle;
  access_mode = MPI_MODE_CREATE
              | MPI_MODE_EXCL
              | MPI_MODE_RDWR
       	      | MPI_MODE_UNIQUE_OPEN
              | MPI_MODE_DELETE_ON_CLOSE;

  MPI_File_open(comm, "file.tmp", access_mode, MPI_INFO_NULL, &handle); 
  printf("File opened successfully.\n");
  fflush(stdout);

  MPI_File_close(&handle);
  printf("File closed successfully.\n");

  MPI_Finalize();

  return 0;
}

