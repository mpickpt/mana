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

// MANA's MPI tool information interface (MPI_T) has no control or
// performance variables: the MPI library's would be the lower half's, which
// a restart replaces.  Programs that look a variable up (the OSU benchmarks
// do) find none and go on.

#include <dlfcn.h>
#include <mpi.h>
#include <stdlib.h>

// MPI_T_init_thread() calls minus MPI_T_finalize() calls.
static int mpi_t_inits = 0;

#define RETURN_IF_NOT_INITIALIZED() \
  if (__atomic_load_n(&mpi_t_inits, __ATOMIC_RELAXED) == 0) { \
    return MPI_T_ERR_NOT_INITIALIZED; \
  }

extern "C" {

#pragma weak MPI_T_init_thread = PMPI_T_init_thread
int PMPI_T_init_thread(int required, int *provided)
{
  *provided = required;
  __atomic_add_fetch(&mpi_t_inits, 1, __ATOMIC_RELAXED);
  return MPI_SUCCESS;
}

#pragma weak MPI_T_finalize = PMPI_T_finalize
int PMPI_T_finalize(void)
{
  RETURN_IF_NOT_INITIALIZED();
  __atomic_sub_fetch(&mpi_t_inits, 1, __ATOMIC_RELAXED);
  return MPI_SUCCESS;
}

#pragma weak MPI_T_enum_get_info = PMPI_T_enum_get_info
int PMPI_T_enum_get_info(MPI_T_enum enumtype, int *num, char *name,
                         int *name_len)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_enum_get_item = PMPI_T_enum_get_item
int PMPI_T_enum_get_item(MPI_T_enum enumtype, int indx, int *value,
                         char *name, int *name_len)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_cvar_get_num = PMPI_T_cvar_get_num
int PMPI_T_cvar_get_num(int *num_cvar)
{
  RETURN_IF_NOT_INITIALIZED();
  *num_cvar = 0;
  return MPI_SUCCESS;
}

#pragma weak MPI_T_cvar_get_info = PMPI_T_cvar_get_info
int PMPI_T_cvar_get_info(int cvar_index, char *name, int *name_len,
                         int *verbosity, MPI_Datatype *datatype,
                         MPI_T_enum *enumtype, char *desc, int *desc_len,
                         int *bind, int *scope)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_INDEX;
}

#pragma weak MPI_T_cvar_get_index = PMPI_T_cvar_get_index
int PMPI_T_cvar_get_index(const char *name, int *cvar_index)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_NAME;
}

#pragma weak MPI_T_cvar_handle_alloc = PMPI_T_cvar_handle_alloc
int PMPI_T_cvar_handle_alloc(int cvar_index, void *obj_handle,
                             MPI_T_cvar_handle *handle, int *count)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_INDEX;
}

#pragma weak MPI_T_cvar_handle_free = PMPI_T_cvar_handle_free
int PMPI_T_cvar_handle_free(MPI_T_cvar_handle *handle)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_cvar_read = PMPI_T_cvar_read
int PMPI_T_cvar_read(MPI_T_cvar_handle handle, void *buf)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_cvar_write = PMPI_T_cvar_write
int PMPI_T_cvar_write(MPI_T_cvar_handle handle, const void *buf)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_pvar_get_num = PMPI_T_pvar_get_num
int PMPI_T_pvar_get_num(int *num_pvar)
{
  RETURN_IF_NOT_INITIALIZED();
  *num_pvar = 0;
  return MPI_SUCCESS;
}

#pragma weak MPI_T_pvar_get_info = PMPI_T_pvar_get_info
int PMPI_T_pvar_get_info(int pvar_index, char *name, int *name_len,
                         int *verbosity, int *var_class,
                         MPI_Datatype *datatype, MPI_T_enum *enumtype,
                         char *desc, int *desc_len, int *bind, int *readonly,
                         int *continuous, int *atomic)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_INDEX;
}

#pragma weak MPI_T_pvar_get_index = PMPI_T_pvar_get_index
int PMPI_T_pvar_get_index(const char *name, int var_class, int *pvar_index)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_NAME;
}

// A session holds no handles, but a tool may still create one.
#pragma weak MPI_T_pvar_session_create = PMPI_T_pvar_session_create
int PMPI_T_pvar_session_create(MPI_T_pvar_session *session)
{
  RETURN_IF_NOT_INITIALIZED();
  *session = (MPI_T_pvar_session)malloc(1);
  return *session != MPI_T_PVAR_SESSION_NULL ? MPI_SUCCESS
                                             : MPI_T_ERR_OUT_OF_SESSIONS;
}

#pragma weak MPI_T_pvar_session_free = PMPI_T_pvar_session_free
int PMPI_T_pvar_session_free(MPI_T_pvar_session *session)
{
  RETURN_IF_NOT_INITIALIZED();
  if (*session == MPI_T_PVAR_SESSION_NULL) {
    return MPI_T_ERR_INVALID_SESSION;
  }
  free(*session);
  *session = MPI_T_PVAR_SESSION_NULL;
  return MPI_SUCCESS;
}

#pragma weak MPI_T_pvar_handle_alloc = PMPI_T_pvar_handle_alloc
int PMPI_T_pvar_handle_alloc(MPI_T_pvar_session session, int pvar_index,
                             void *obj_handle, MPI_T_pvar_handle *handle,
                             int *count)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_INDEX;
}

#pragma weak MPI_T_pvar_handle_free = PMPI_T_pvar_handle_free
int PMPI_T_pvar_handle_free(MPI_T_pvar_session session,
                            MPI_T_pvar_handle *handle)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

// MPI_T_PVAR_ALL_HANDLES names all of a session's handles: none here.  It
// is a variable of the MPI library (here libmpistub.so), looked up at the
// call: a program that dlopen()s the MPI library loads it after libmana.so.
static bool
is_all_handles(MPI_T_pvar_handle handle)
{
  void *all = dlsym(RTLD_DEFAULT, "MPI_T_PVAR_ALL_HANDLES");
  return all != NULL && handle == *(MPI_T_pvar_handle *)all;
}

#pragma weak MPI_T_pvar_start = PMPI_T_pvar_start
int PMPI_T_pvar_start(MPI_T_pvar_session session, MPI_T_pvar_handle handle)
{
  RETURN_IF_NOT_INITIALIZED();
  return is_all_handles(handle) ? MPI_SUCCESS : MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_pvar_stop = PMPI_T_pvar_stop
int PMPI_T_pvar_stop(MPI_T_pvar_session session, MPI_T_pvar_handle handle)
{
  RETURN_IF_NOT_INITIALIZED();
  return is_all_handles(handle) ? MPI_SUCCESS : MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_pvar_reset = PMPI_T_pvar_reset
int PMPI_T_pvar_reset(MPI_T_pvar_session session, MPI_T_pvar_handle handle)
{
  RETURN_IF_NOT_INITIALIZED();
  return is_all_handles(handle) ? MPI_SUCCESS : MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_pvar_read = PMPI_T_pvar_read
int PMPI_T_pvar_read(MPI_T_pvar_session session, MPI_T_pvar_handle handle,
                     void *buf)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_pvar_write = PMPI_T_pvar_write
int PMPI_T_pvar_write(MPI_T_pvar_session session, MPI_T_pvar_handle handle,
                      const void *buf)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_pvar_readreset = PMPI_T_pvar_readreset
int PMPI_T_pvar_readreset(MPI_T_pvar_session session,
                          MPI_T_pvar_handle handle, void *buf)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_HANDLE;
}

#pragma weak MPI_T_category_get_num = PMPI_T_category_get_num
int PMPI_T_category_get_num(int *num_cat)
{
  RETURN_IF_NOT_INITIALIZED();
  *num_cat = 0;
  return MPI_SUCCESS;
}

#pragma weak MPI_T_category_get_info = PMPI_T_category_get_info
int PMPI_T_category_get_info(int cat_index, char *name, int *name_len,
                             char *desc, int *desc_len, int *num_cvars,
                             int *num_pvars, int *num_categories)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_INDEX;
}

#pragma weak MPI_T_category_get_index = PMPI_T_category_get_index
int PMPI_T_category_get_index(const char *name, int *cat_index)
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_NAME;
}

#pragma weak MPI_T_category_get_cvars = PMPI_T_category_get_cvars
int PMPI_T_category_get_cvars(int cat_index, int len, int indices[])
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_INDEX;
}

#pragma weak MPI_T_category_get_pvars = PMPI_T_category_get_pvars
int PMPI_T_category_get_pvars(int cat_index, int len, int indices[])
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_INDEX;
}

#pragma weak MPI_T_category_get_categories = PMPI_T_category_get_categories
int PMPI_T_category_get_categories(int cat_index, int len, int indices[])
{
  RETURN_IF_NOT_INITIALIZED();
  return MPI_T_ERR_INVALID_INDEX;
}

#pragma weak MPI_T_category_changed = PMPI_T_category_changed
int PMPI_T_category_changed(int *update_number)
{
  RETURN_IF_NOT_INITIALIZED();
  *update_number = 0;
  return MPI_SUCCESS;
}

}  // extern "C"
