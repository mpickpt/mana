#include <mpi.h>
#include <stddef.h>
#include "lower_half_api.h"

static void* MPI_Fnc_Ptrs[] = {
  NULL,
  FOREACH_FNC(GENERATE_FNC_PTR)
  NULL,
};

void* lh_dlsym(enum MPI_Fncs fnc) {
  if (fnc < MPI_Fnc_NULL || fnc > MPI_Fnc_Invalid) {
    return NULL;
  }
  return MPI_Fnc_Ptrs[fnc];
}

static void*  mpi_constants[] = {
  NULL,
  FOREACH_CONSTANT(GENERATE_CONSTANT_VALUE)
  NULL,
};

void*
get_lh_mpi_constant(enum MPI_Constants constant)
{
  if (constant < LH_MPI_Constant_NULL ||
      constant > LH_MPI_Constant_Invalid) {
    return NULL;
  }
  return mpi_constants[constant];
}
