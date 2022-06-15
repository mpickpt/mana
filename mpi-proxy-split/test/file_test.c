/*
 * Source: https://www.rookiehpc.com/mpi/docs/mpi_file_open.php
 */
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define RUNTIME 30
#define SLEEP_PER_ITERATION 5

int main (int argc, char *argv[])
{
  int access_mode;
  MPI_Comm comm;
  int iterations; clock_t start_time;

  MPI_Init( &argc, &argv );
  comm = MPI_COMM_WORLD;

  MPI_File handle;
  access_mode = MPI_MODE_CREATE
              | MPI_MODE_EXCL
              | MPI_MODE_RDWR
       	      | MPI_MODE_UNIQUE_OPEN
              | MPI_MODE_DELETE_ON_CLOSE;

  start_time = clock();
  iterations = 0;
  
  for (clock_t t = clock(); t-start_time < (RUNTIME-(iterations * SLEEP_PER_ITERATION)) * CLOCKS_PER_SEC; t = clock()) {
    MPI_File_open(comm, "file.tmp", access_mode, MPI_INFO_NULL, &handle); 
    printf("File opened successfully.\n");
    fflush(stdout);

    MPI_File_close(&handle);
    printf("File closed successfully.\n");

    iterations++;
    sleep(SLEEP_PER_ITERATION);
  }

  MPI_Finalize();

  return 0;
}

