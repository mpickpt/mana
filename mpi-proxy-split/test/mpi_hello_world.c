// Author: Wes Kendall
// Copyright 2011 www.mpitutorial.com
// This code is provided freely with the tutorials on mpitutorial.com. Feel
// free to modify it for your own use. Any distribution of the code must
// either provide a link to www.mpitutorial.com or keep this header intact.
//
// An intro MPI hello world program that uses MPI_Init, MPI_Comm_size,
// MPI_Comm_rank, MPI_Finalize, and MPI_Get_processor_name.
//
#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/mman.h>
int
main(int argc, char **argv)
{
  void *ret = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  printf("ret: %p\n", ret);
  // Initialize the MPI environment. The two arguments to MPI Init are not
  // currently used by MPI implementations, but are there in case future
  // implementations might need the arguments.
  MPI_Init(NULL, NULL);

  // Get the number of processes
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // Get the rank of the process
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int original_rank = world_rank, original_size = world_size;
  // Get the name of the processor
  char processor_name[MPI_MAX_PROCESSOR_NAME] = { "test" };
  // int name_len;
  // MPI_Get_processor_name(processor_name, &name_len);

  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  assert(world_rank == original_rank);
  assert(world_size == original_size);
  // Print off a hello world message
  printf("Hello world from processor %s, rank %d out of %d processors\n",
         processor_name, world_rank, world_size);
  fflush(stdout);

  printf("Will now sleep for 500 seconds ...\n");
  fflush(stdout);
  unsigned int remaining = 300;
  while (remaining > 0) {
    remaining = sleep(remaining);
    if (remaining > 0) {
      printf("Signal received; continuing sleep for %d seconds.\n", remaining);
      fflush(stdout);
    }
  }
  printf("**** %s is now exiting.\n", argv[0]);

  // Finalize the MPI environment. No more MPI calls can be made after this
  MPI_Finalize();
}
