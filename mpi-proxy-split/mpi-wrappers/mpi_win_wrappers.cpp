#include "mpi_plugin.h"
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "mpi_nextfunc.h"

extern "C" {

#pragma weak MPI_Alloc_mem = PMPI_Alloc_mem
int PMPI_Alloc_mem(MPI_Aint size, MPI_Info info, void *baseptr)
{
  // Since memory allocated by the lower half will be discarded during
  // checkpoint, we need to translate MPI_Alloc_mem and MPI_Free_mem
  // to malloc and free. This may slow down the program.
  *(void**)baseptr = malloc(size * sizeof(MPI_Aint));
  return MPI_SUCCESS;
}

#pragma weak MPI_Free_mem = PMPI_Free_mem
int PMPI_Free_mem(void *baseptr)
{
  // Since memory allocated by the lower half will be discarded during
  // checkpoint, we need to translate MPI_Alloc_mem and MPI_Free_mem
  // to malloc and free. This may slow down the program.
  free(baseptr);
  return MPI_SUCCESS;
}

} // end of: extern "C"
