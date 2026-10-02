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

// The variables that MPICH's mpi.h declares as the MPI library's.  A program
// that uses one (MPI_UNWEIGHTED, say) loads only if libmpistub.so defines it
// too.  Only their addresses matter, except that MPI_F_STATUS_IGNORE and
// MPI_F_STATUSES_IGNORE are NULL, as in MPICH until its Fortran bindings
// set them.

#include <mpi.h>
#include <stddef.h>

#ifndef MPI_F_STATUS_IGNORE
MPI_Fint *MPI_F_STATUS_IGNORE = NULL;
#endif
#ifndef MPI_F_STATUSES_IGNORE
MPI_Fint *MPI_F_STATUSES_IGNORE = NULL;
#endif

#ifndef MPI_UNWEIGHTED
static int unweighted;
int * const MPI_UNWEIGHTED = &unweighted;
#endif
#ifndef MPI_WEIGHTS_EMPTY
static int weights_empty;
int * const MPI_WEIGHTS_EMPTY = &weights_empty;
#endif

#ifndef MPI_T_PVAR_ALL_HANDLES
static char all_handles;
MPI_T_pvar_handle const MPI_T_PVAR_ALL_HANDLES =
  (MPI_T_pvar_handle)&all_handles;
#endif

#if MPI_VERSION >= 4
#ifndef MPI_F08_STATUS_IGNORE
static MPI_F08_status f08_status_ignore;
MPI_F08_status *MPI_F08_STATUS_IGNORE = &f08_status_ignore;
#endif
#ifndef MPI_F08_STATUSES_IGNORE
static MPI_F08_status f08_statuses_ignore;
MPI_F08_status *MPI_F08_STATUSES_IGNORE = &f08_statuses_ignore;
#endif
#endif
