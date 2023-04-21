#ifndef _MPI_LOGGER_UTILS_H
#define _MPI_LOGGER_UTILS_H

#include <mpi.h>

#define EXTERNC extern "C"
#define ENABLE_LOGGER_PRINT true
#define NEXT_FNC(func)                                                       \
  ({                                                                         \
    static __typeof__(&func)_real_ ## func = (__typeof__(&func)) - 1;        \
    if (_real_ ## func == (__typeof__(&func)) - 1) {          \
      _real_ ## func = (__typeof__(&func))dlsym(RTLD_NEXT, # func); \
    }                                                                        \
    _real_ ## func;                                                          \
  })

//@todo: fix tortran wrapper and constants, this could lead to the crash of wrapper
void *FORTRAN_MPI_BOTTOM = NULL;
void *FORTRAN_MPI_STATUS_IGNORE = NULL;
void *FORTRAN_MPI_STATUSES_IGNORE = NULL;
void *FORTRAN_MPI_ERRCODES_IGNORE = NULL;
void *FORTRAN_MPI_IN_PLACE = NULL;
void *FORTRAN_MPI_ARGV_NULL = NULL;
void *FORTRAN_MPI_ARGVS_NULL = NULL;
void *FORTRAN_MPI_UNWEIGHTED = NULL;
void *FORTRAN_MPI_WEIGHTS_EMPTY = NULL;
void *FORTRAN_CONSTANTS_END = NULL;

/*
 * Util functions
*/
void
get_datatype_string(MPI_Datatype datatype, char *buf)
{
  switch (datatype) {
    case MPI_CHAR:
      sprintf(buf, "MPI_CHAR\0");
      break;
    case MPI_SIGNED_CHAR:
      sprintf(buf, "MPI_SIGNED_CHAR\0");
      break;
    case MPI_UNSIGNED_CHAR:
      sprintf(buf, "MPI_UNSIGNED_CHAR\0");
      break;
    case MPI_BYTE:
      sprintf(buf, "MPI_BYTE\0");
      break;
    case MPI_WCHAR:
      sprintf(buf, "MPI_WCHAR\0");
      break;
    case MPI_SHORT:
      sprintf(buf, "MPI_SHORT\0");
      break;
    case MPI_UNSIGNED_SHORT:
      sprintf(buf, "MPI_UNSIGNED_SHORT\0");
      break;
    case MPI_INT:
      sprintf(buf, "MPI_INT\0");
      break;
    case MPI_UNSIGNED:
      sprintf(buf, "MPI_UNSIGNED\0");
      break;
    case MPI_LONG:
      sprintf(buf, "MPI_LONG\0");
      break;
    case MPI_UNSIGNED_LONG:
      sprintf(buf, "MPI_UNSIGNED_LONG\0");
      break;
    case MPI_FLOAT:
      sprintf(buf, "MPI_FLOAT\0");
      break;
    case MPI_DOUBLE:
      sprintf(buf, "MPI_DOUBLE\0");
      break;
    case MPI_LONG_DOUBLE:
      sprintf(buf, "MPI_LONG_DOUBLE\0");
      break;
    case MPI_LONG_LONG_INT:
      sprintf(buf, "MPI_LONG_LONG_INT or MPI_LONG_LONG\0");
      break;
    case MPI_UNSIGNED_LONG_LONG:
      sprintf(buf, "MPI_UNSIGNED_LONG_LONG\0");
      break;
    default:
      sprintf(buf, "USER_DEFINED\0");
      break;
  }
}

void get_op_string(MPI_Op op, char *buf)
{
  switch (op) {
    case MPI_MAX:
      sprintf(buf, "MPI_MAX\0");
      break;
    case MPI_MIN:
      sprintf(buf, "MPI_MIN\0");
      break;
    case MPI_SUM:
      sprintf(buf, "MPI_SUM\0");
      break;
    case MPI_PROD:
      sprintf(buf, "MPI_PROD\0");
      break;
    case MPI_LAND:
      sprintf(buf, "MPI_LAND\0");
      break;
    case MPI_LOR:
      sprintf(buf, "MPI_LOR\0");
      break;
    case MPI_BAND:
      sprintf(buf, "MPI_BAND\0");
      break;
    case MPI_BOR:
      sprintf(buf, "MPI_BOR\0");
      break;
    case MPI_MAXLOC:
      sprintf(buf, "MPI_MAXLOC\0");
      break;
    case MPI_MINLOC:
      sprintf(buf, "MPI_MINLOC\0");
      break;
    default:
      sprintf(buf, "USER_DEFINED\0");
      break;
  }
}
int get_buffer_checksum(int* buffer, int size) { 
  int words = size / 4;
  int checksum = 0;
  int i = 0;
  for (i = 0; i < words; i++) {
    checksum = checksum ^ *(buffer + i);
  }
  return checksum;
}

#endif
