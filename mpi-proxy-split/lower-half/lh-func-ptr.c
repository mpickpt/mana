#include <mpi.h>
#include <stddef.h>
#include "lower-half-api.h"

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
