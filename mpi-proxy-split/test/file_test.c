/*
  Test for the MPI_File_open and MPI_File_close methods

  Run with -i [iterations] for specific number of iterations, defaults to 10000

  Source: https://www.rookiehpc.com/mpi/docs/mpi_file_open.php
*/

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

int main (int argc, char *argv[])
{
  //Parse runtime argument
  int opt, max_iterations;
  max_iterations = 10000;
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
          -i [iterations]: Set test iterations (default 10000)\n");
          return 1;
      }
  }

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

  for (int iterations = 0; iterations < max_iterations; iterations++) {

    MPI_File_open(comm, "file.tmp", access_mode, MPI_INFO_NULL, &handle);
    printf("File opened successfully.\n");
    fflush(stdout);

    MPI_File_close(&handle);
    printf("File closed successfully.\n");
  }

  MPI_Finalize();

  return 0;
}

