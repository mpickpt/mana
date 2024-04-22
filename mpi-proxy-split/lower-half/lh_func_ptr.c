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
  printf("MPI function: %s, table addr: %p, function adr: %p\n", MPI_Fnc_strings[fnc], &MPI_Fnc_Ptrs, MPI_Fnc_Ptrs[fnc]);
  return MPI_Fnc_Ptrs[fnc];
}
