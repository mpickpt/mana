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

#ifndef MANA_FORTRAN_HANDLES_H
#define MANA_FORTRAN_HANDLES_H

#include <mpi.h>
#include <vector>

#include "virtual_id.h"

// Conversions of handles for the Fortran wrappers.  A Fortran handle is an
// integer (MPI_Fint).  With MPICH, a C handle is an integer too, with the
// same value.  Otherwise (Open MPI, and MPI_File in MPICH), the Fortran
// handle of a virtual handle is the virtual handle itself, which has 32
// bits, and that of a predefined constant is the MPI library's (mpif.h's),
// which init_predefined_virt_ids() finds.  MANA's Fortran wrappers create no
// errhandler or info, so those are predefined constants.

template <typename Handle>
static inline Handle
mana_f2c(int kind, MPI_Fint fortran)
{
  if (sizeof(Handle) == sizeof(MPI_Fint)) {
    return (Handle)(intptr_t)fortran;
  }
  return (Handle)(intptr_t)fortran_to_handle(kind, fortran);
}

template <typename Handle>
static inline MPI_Fint
mana_c2f(int kind, Handle handle)
{
  if (sizeof(Handle) == sizeof(MPI_Fint)) {
    return (MPI_Fint)(intptr_t)handle;
  }
  return handle_to_fortran(kind, handle_key(handle));
}

// Returns the C handles of the Fortran handles fortran[0..n): 'fortran'
// itself if they are the same, or else 'buffer', which it fills.
template <typename Handle>
static inline Handle*
mana_f2c_array(int kind, MPI_Fint *fortran, int n,
               std::vector<Handle> *buffer)
{
  if (sizeof(Handle) == sizeof(MPI_Fint)) {
    return (Handle*)fortran;
  }
  buffer->resize(n > 0 ? n : 0);
  for (int i = 0; i < n; i++) {
    (*buffer)[i] = mana_f2c<Handle>(kind, fortran[i]);
  }
  return buffer->data();
}

// Copies the C handles back to the Fortran handles that mana_f2c_array()
// converted.
template <typename Handle>
static inline void
mana_c2f_array(int kind, const Handle *handles, MPI_Fint *fortran, int n)
{
  if (sizeof(Handle) == sizeof(MPI_Fint)) {
    return;  // The same array
  }
  for (int i = 0; i < n; i++) {
    fortran[i] = mana_c2f(kind, handles[i]);
  }
}

#endif  // MANA_FORTRAN_HANDLES_H
