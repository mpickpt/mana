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

// The Fortran bindings of the attribute calls.  The generated wrappers
// (mpi_fortran_wrappers.cpp) pass their arguments on to the C calls, which
// is wrong here.  A Fortran attribute value is an integer, not an address.
// A Fortran callback takes all its arguments by reference, and an IERROR
// argument instead of a return value.  MPI_KEYVAL_CREATE, MPI_ATTR_PUT and
// MPI_ATTR_GET (MPI-1) use INTEGERs; the others use
// INTEGER(KIND=MPI_ADDRESS_KIND).

#include <mpi.h>
#include <stdint.h>
#include "dmtcp.h"
#include "fortran_handles.h"

// The callbacks of MPI_COMM_CREATE_KEYVAL (MPI_Aint values) or of
// MPI_KEYVAL_CREATE (MPI_Fint values).
typedef void (FortranCopyFn)(MPI_Fint *oldcomm, MPI_Fint *keyval,
                             void *extra_state, void *attribute_val_in,
                             void *attribute_val_out, MPI_Fint *flag,
                             MPI_Fint *ierr);
typedef void (FortranDeleteFn)(MPI_Fint *comm, MPI_Fint *keyval,
                               void *attribute_val, void *extra_state,
                               MPI_Fint *ierr);

// A keyval that Fortran made.  Its C keyval has f_copy() and f_delete() as
// callbacks and this as its extra state.  It lives as long as the process:
// a freed keyval can still have attributes, and programs make few keyvals.
// 'extra_state' is the address of the program's EXTRA_STATE, as in MPICH,
// so the callbacks see the variable's value when they run.
struct FortranKeyval {
  FortranCopyFn *copy;
  FortranDeleteFn *del;
  void *extra_state;
  bool mpi1;  // made by MPI_KEYVAL_CREATE
};

static int
f_copy(MPI_Comm oldcomm, int keyval, void *extra_state, void *in, void *out,
       int *flag)
{
  FortranKeyval *k = (FortranKeyval*)extra_state;
  MPI_Fint fcomm = mana_c2f(MANA_COMM_KIND, oldcomm);
  MPI_Fint fkeyval = keyval;
  MPI_Fint fflag = 0;
  MPI_Fint ierr = MPI_SUCCESS;
  if (k->mpi1) {
    MPI_Fint value_in = (MPI_Fint)(intptr_t)in;
    MPI_Fint value_out = 0;
    k->copy(&fcomm, &fkeyval, k->extra_state, &value_in, &value_out, &fflag,
            &ierr);
    *(void**)out = (void*)(intptr_t)value_out;
  } else {
    MPI_Aint value_in = (MPI_Aint)in;
    MPI_Aint value_out = 0;
    k->copy(&fcomm, &fkeyval, k->extra_state, &value_in, &value_out, &fflag,
            &ierr);
    *(void**)out = (void*)value_out;
  }
  *flag = fflag != 0;
  return ierr;
}

static int
f_delete(MPI_Comm comm, int keyval, void *value, void *extra_state)
{
  FortranKeyval *k = (FortranKeyval*)extra_state;
  MPI_Fint fcomm = mana_c2f(MANA_COMM_KIND, comm);
  MPI_Fint fkeyval = keyval;
  MPI_Fint ierr = MPI_SUCCESS;
  if (k->mpi1) {
    MPI_Fint fvalue = (MPI_Fint)(intptr_t)value;
    k->del(&fcomm, &fkeyval, &fvalue, k->extra_state, &ierr);
  } else {
    MPI_Aint fvalue = (MPI_Aint)value;
    k->del(&fcomm, &fkeyval, &fvalue, k->extra_state, &ierr);
  }
  return ierr;
}

static const int predefined_keyvals[] = {
  MPI_TAG_UB, MPI_HOST, MPI_IO, MPI_WTIME_IS_GLOBAL, MPI_UNIVERSE_SIZE,
  MPI_LASTUSEDCODE, MPI_APPNUM
};

// Returns the C keyval of the Fortran keyval 'keyval', and sets
// '*predefined' if it is a predefined attribute, whose C value is the
// address of an int.  MPICH forms the Fortran keyval of a predefined
// attribute by adding one to the C keyval (mpi.h).
static int
c_keyval(MPI_Fint keyval, bool *predefined)
{
  for (int k : predefined_keyvals) {
#ifdef MPICH
    int fortran_keyval = k + 1;
#else
    int fortran_keyval = k;
#endif
    if (keyval == fortran_keyval) {
      *predefined = true;
      return k;
    }
  }
  *predefined = false;
  return keyval;
}

static int
create_keyval(FortranCopyFn *copy, FortranDeleteFn *del, MPI_Fint *keyval,
              void *extra_state, bool mpi1)
{
  FortranKeyval *k = new FortranKeyval{copy, del, extra_state, mpi1};
  int ckeyval;
  int retval = MPI_Comm_create_keyval(f_copy, f_delete, &ckeyval, k);
  if (retval == MPI_SUCCESS) {
    *keyval = ckeyval;
  } else {
    delete k;
  }
  return retval;
}

// Gets the attribute of 'keyval' on 'comm' as a Fortran value.
static int
get_attr(MPI_Comm comm, MPI_Fint keyval, MPI_Aint *value, MPI_Fint *flag)
{
  bool predefined;
  void *cvalue;
  int cflag;
  int retval = MPI_Comm_get_attr(comm, c_keyval(keyval, &predefined),
                                 &cvalue, &cflag);
  if (retval == MPI_SUCCESS) {
    *flag = cflag;
    if (cflag) {
      *value = predefined ? *(int*)cvalue : (MPI_Aint)cvalue;
    }
  }
  return retval;
}

EXTERNC int mpi_comm_create_keyval_ (FortranCopyFn *comm_copy_attr_fn,
                                     FortranDeleteFn *comm_delete_attr_fn,
                                     MPI_Fint *comm_keyval,
                                     MPI_Aint *extra_state, MPI_Fint *ierr) {
  *ierr = create_keyval(comm_copy_attr_fn, comm_delete_attr_fn, comm_keyval,
                        extra_state, false);
  return *ierr;
}

EXTERNC int mpi_keyval_create_ (FortranCopyFn *copy_fn,
                                FortranDeleteFn *delete_fn, MPI_Fint *keyval,
                                MPI_Fint *extra_state, MPI_Fint *ierr) {
  *ierr = create_keyval(copy_fn, delete_fn, keyval, extra_state, true);
  return *ierr;
}

EXTERNC int mpi_comm_free_keyval_ (MPI_Fint *comm_keyval, MPI_Fint *ierr) {
  int keyval = *comm_keyval;
  *ierr = MPI_Comm_free_keyval(&keyval);
  *comm_keyval = keyval;
  return *ierr;
}

EXTERNC int mpi_keyval_free_ (MPI_Fint *keyval, MPI_Fint *ierr) {
  return mpi_comm_free_keyval_(keyval, ierr);
}

EXTERNC int mpi_comm_set_attr_ (MPI_Fint *comm, MPI_Fint *comm_keyval,
                                MPI_Aint *attribute_val, MPI_Fint *ierr) {
  bool predefined;
  MPI_Comm c_comm = mana_f2c<MPI_Comm>(MANA_COMM_KIND, *comm);
  *ierr = MPI_Comm_set_attr(c_comm, c_keyval(*comm_keyval, &predefined),
                            (void*)*attribute_val);
  return *ierr;
}

EXTERNC int mpi_attr_put_ (MPI_Fint *comm, MPI_Fint *keyval,
                           MPI_Fint *attribute_val, MPI_Fint *ierr) {
  bool predefined;
  MPI_Comm c_comm = mana_f2c<MPI_Comm>(MANA_COMM_KIND, *comm);
  *ierr = MPI_Comm_set_attr(c_comm, c_keyval(*keyval, &predefined),
                            (void*)(intptr_t)*attribute_val);
  return *ierr;
}

EXTERNC int mpi_comm_get_attr_ (MPI_Fint *comm, MPI_Fint *comm_keyval,
                                MPI_Aint *attribute_val, MPI_Fint *flag,
                                MPI_Fint *ierr) {
  MPI_Comm c_comm = mana_f2c<MPI_Comm>(MANA_COMM_KIND, *comm);
  *ierr = get_attr(c_comm, *comm_keyval, attribute_val, flag);
  return *ierr;
}

EXTERNC int mpi_attr_get_ (MPI_Fint *comm, MPI_Fint *keyval,
                           MPI_Fint *attribute_val, MPI_Fint *flag,
                           MPI_Fint *ierr) {
  MPI_Aint value;
  MPI_Comm c_comm = mana_f2c<MPI_Comm>(MANA_COMM_KIND, *comm);
  *ierr = get_attr(c_comm, *keyval, &value, flag);
  if (*ierr == MPI_SUCCESS && *flag) {
    *attribute_val = (MPI_Fint)value;
  }
  return *ierr;
}

EXTERNC int mpi_comm_delete_attr_ (MPI_Fint *comm, MPI_Fint *comm_keyval,
                                   MPI_Fint *ierr) {
  bool predefined;
  MPI_Comm c_comm = mana_f2c<MPI_Comm>(MANA_COMM_KIND, *comm);
  *ierr = MPI_Comm_delete_attr(c_comm, c_keyval(*comm_keyval, &predefined));
  return *ierr;
}

EXTERNC int mpi_attr_delete_ (MPI_Fint *comm, MPI_Fint *keyval,
                              MPI_Fint *ierr) {
  return mpi_comm_delete_attr_(comm, keyval, ierr);
}

// MPI's predefined Fortran callbacks.  The MPI library defines them in its
// Fortran library, which libmpistub.so replaces.
EXTERNC void mpi_comm_null_copy_fn_ (MPI_Fint *oldcomm, MPI_Fint *keyval,
                                     MPI_Aint *extra_state, MPI_Aint *in,
                                     MPI_Aint *out, MPI_Fint *flag,
                                     MPI_Fint *ierr) {
  *flag = 0;
  *ierr = MPI_SUCCESS;
}

EXTERNC void mpi_comm_dup_fn_ (MPI_Fint *oldcomm, MPI_Fint *keyval,
                               MPI_Aint *extra_state, MPI_Aint *in,
                               MPI_Aint *out, MPI_Fint *flag,
                               MPI_Fint *ierr) {
  *out = *in;
  *flag = 1;
  *ierr = MPI_SUCCESS;
}

EXTERNC void mpi_comm_null_delete_fn_ (MPI_Fint *comm, MPI_Fint *keyval,
                                       MPI_Aint *attribute_val,
                                       MPI_Aint *extra_state,
                                       MPI_Fint *ierr) {
  *ierr = MPI_SUCCESS;
}

EXTERNC void mpi_null_copy_fn_ (MPI_Fint *oldcomm, MPI_Fint *keyval,
                                MPI_Fint *extra_state, MPI_Fint *in,
                                MPI_Fint *out, MPI_Fint *flag,
                                MPI_Fint *ierr) {
  *flag = 0;
  *ierr = MPI_SUCCESS;
}

EXTERNC void mpi_dup_fn_ (MPI_Fint *oldcomm, MPI_Fint *keyval,
                          MPI_Fint *extra_state, MPI_Fint *in, MPI_Fint *out,
                          MPI_Fint *flag, MPI_Fint *ierr) {
  *out = *in;
  *flag = 1;
  *ierr = MPI_SUCCESS;
}

EXTERNC void mpi_null_delete_fn_ (MPI_Fint *comm, MPI_Fint *keyval,
                                  MPI_Fint *attribute_val,
                                  MPI_Fint *extra_state, MPI_Fint *ierr) {
  *ierr = MPI_SUCCESS;
}
