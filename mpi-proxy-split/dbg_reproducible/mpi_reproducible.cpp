#include "mpi_reproducible.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>


/* We use 'static' becuase we don't want the overhead of the compiler
initializing these to zero each time the function is called. */
#define MAX_ALLREDUCE_SENDBUF_SIZE (1024 * 1024 * 16) /* 15 MB */

int
MPI_Init_thread(int *argc, char ***argv, int required, int *provided)
{

  int retval = NEXT_FNC(MPI_Init_thread)(argc, argv, required, provided);
  return retval;
}

void touch(const char *filename) {
    struct stat st;
    // Check if the file already exists
    int result = stat(filename, &st);
    if (result == 0) {
        
    } else {
        // If the file does not exist, create an empty file
        FILE *file = fopen(filename, "w");
        if (file != NULL) {
            fclose(file);
            result = 0;
        } else {
            result = -1;
        }
    }

}

int
MPI_Allreduce(const void *sendbuf,
              void *recvbuf,
              int count,
              MPI_Datatype datatype,
              MPI_Op op,
              MPI_Comm comm)
{
  static unsigned char tmpbuf[MAX_ALLREDUCE_SENDBUF_SIZE];

touch("/global/cfs/cdirs/cr/malviyat/exp-setup/PdO4-test-native-reproducible/reproducible.txt");

  int retval;
  int root = 0;
  int comm_rank;
  int comm_size;
  int type_size;

  retval = NEXT_FNC(MPI_Comm_rank)(comm, &comm_rank);
  retval = NEXT_FNC(MPI_Comm_size)(comm, &comm_size);
  retval = NEXT_FNC(MPI_Type_size)(datatype, &type_size);

    fprintf(stdout,
            "\nMPI_Allreduce_reproducible: using ");
    fflush(stdout);
  if (count * comm_size * type_size > MAX_ALLREDUCE_SENDBUF_SIZE) {
    fprintf(stderr,
            "\nMPI_Allreduce_reproducible: Insufficient tmp send buffer.");
    fflush(stderr);
    exit(-1);
  }

  if (sendbuf != FORTRAN_MPI_IN_PLACE || sendbuf == MPI_IN_PLACE) {
    fprintf(stderr,
            "\nMPI_Allreduce_reproducible: MPI_IN_PLACE not yet supported.");
    fflush(stderr);
    exit(-1);
  }

  // Gather the operands from all ranks in the comm
  retval = NEXT_FNC(MPI_Gather)(sendbuf, count, datatype, tmpbuf, count,
                                datatype, 0, comm);

  // Perform the local reduction operation on the root rank
  if (comm_rank == root) {
    memset(recvbuf, 0, count * type_size);
    memcpy(recvbuf, tmpbuf + (count * type_size * 0), count * type_size);
    for (int i = 1; i < comm_size; i++) {
      retval = NEXT_FNC(MPI_Reduce_local)(tmpbuf + (count * type_size * i),
                                          recvbuf, count, datatype, op);
    }
  }

  // Broadcat the local reduction operation result in the comm
  retval = NEXT_FNC(MPI_Bcast)(recvbuf, count, datatype, 0, comm);
  return retval;
}

EXTERNC int
mpi_init_thread_(int *required, int *provided, int *ierr)
{
  int argc = 0;
  char **argv;
  *ierr = MPI_Init_thread(&argc, &argv, *required, provided);
  return *ierr;
}

EXTERNC int
mpi_allreduce_(const void *sendbuf,
               void *recvbuf,
               int *count,
               MPI_Datatype *datatype,
               MPI_Op *op,
               MPI_Comm *comm,
               int *ierr)
{
  fprintf(stdout,
            "\nMPI_Allreduce_reproducible__: using ");
  fflush(stdout);

  *ierr = MPI_Allreduce(sendbuf, recvbuf, *count, *datatype, *op, *comm);
  return *ierr;
}
