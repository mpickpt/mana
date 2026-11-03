/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License in the files COPYING and COPYING.LESSER.  If not, see           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef _MPI_NEXTFUNC_H
#define _MPI_NEXTFUNC_H

#include "mpi_plugin.h"
#include "lower-half-api.h"

#ifndef NEXT_FUNC
# define NEXT_FUNC(func)                                                       \
  ({                                                                           \
    (__typeof__(&MPI_##func))pdlsym(MPI_Fnc_##func);                           \
  })
#endif // ifndef NEXT_FUNC

// Fortran MPI named constants
// MPI 3.1 standard:
//   The constants that cannot be used in initialization expressions or
//   assignments in Fortran are as follows:
EXTERNC void get_fortran_constants();
extern void *FORTRAN_MPI_BOTTOM;
extern void *FORTRAN_MPI_STATUS_IGNORE;
extern void *FORTRAN_MPI_STATUSES_IGNORE;
extern void *FORTRAN_MPI_ERRCODES_IGNORE;
extern void *FORTRAN_MPI_IN_PLACE;
extern void *FORTRAN_MPI_ARGV_NULL;
extern void *FORTRAN_MPI_ARGVS_NULL;
extern void *FORTRAN_CONSTANTS_END;
#endif // #ifndef _MPI_NEXTFUNC_H
