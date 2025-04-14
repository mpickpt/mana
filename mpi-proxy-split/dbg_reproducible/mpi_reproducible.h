#ifndef _MPI_REPRODUCIBLE_H
#define _MPI_REPRODUCIBLE_H

#include <mpi.h>

#define EXTERNC extern "C"

#define NEXT_FNC(func)                                             \
  ({                                                               \
    static __typeof__(&func) _real_##func = (__typeof__(&func))-1; \
    if (_real_##func == (__typeof__(&func))-1) {                   \
      _real_##func = (__typeof__(&func))dlsym(RTLD_NEXT, #func);   \
    }                                                              \
    _real_##func;                                                  \
  })

EXTERNC void get_fortran_constants();
extern void *FORTRAN_MPI_BOTTOM;
extern void *FORTRAN_MPI_STATUS_IGNORE;
extern void *FORTRAN_MPI_STATUSES_IGNORE;
extern void *FORTRAN_MPI_ERRCODES_IGNORE;
extern void *FORTRAN_MPI_IN_PLACE;
extern void *FORTRAN_MPI_ARGV_NULL;
extern void *FORTRAN_MPI_ARGVS_NULL;
extern void *FORTRAN_MPI_UNWEIGHTED;
extern void *FORTRAN_MPI_WEIGHTS_EMPTY;
extern void *FORTRAN_CONSTANTS_END;

#endif
