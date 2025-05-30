/****************************************************************************
 *   Copyright (C) 2019-2022 by Illio Suardi, Chirag Singh, Twinkle Jain    *
 *   illio@u.nus.edu, chirag.singh@memverge.com, jain.t@northeastern.edu    *
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

#include "mpi_plugin.h"
#include "mpi_files.h"
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_nextfunc.h"
#include "record-replay.h"
#include "virtual_id.h"
#include "seq_num.h"

using namespace dmtcp_mpi;

std::unordered_map<MPI_File, OpenFileParameters> g_params_map;

extern "C" {

// It's possible that there could be a Fortran to C interface bug in the
// passing of the filename. If there are ever any bugs related to MPI
// opening a file with the wrong name, that's likely the cause
#pragma weak MPI_File_open = PMPI_File_open
int PMPI_File_open(MPI_Comm comm, const char *filename,
                  int amode, MPI_Info info, MPI_File *fh)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = comm}).comm;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_open)(realComm, filename, amode, info, fh);
  RETURN_TO_UPPER_HALF();

  // Save file initialization parameters to restore the file on restart
  if (retval == MPI_SUCCESS) {
    MPI_File virt_file = new_virt_file(*fh);
    // Add file to virtual to real mapping
    *fh = new_virt_file(*fh);

    // Save parameters
    OpenFileParameters params;
    params._mode = amode;
    params._comm = comm;
    params._info = info;
    strcpy(params._filepath, filename);

    // If view isn't set, we can't restore any view
    params._viewSet = 0;

    // Save this file handle to the global map
    g_params_map[virt_file] = params;
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_get_atomicity = PMPI_File_get_atomicity
int PMPI_File_get_atomicity(MPI_File fh, int *flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_get_atomicity)(real_file, flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_set_atomicity = PMPI_File_set_atomicity
int PMPI_File_set_atomicity(MPI_File fh, int flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_set_atomicity)(real_file, flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_set_size = PMPI_File_set_size
int PMPI_File_set_size(MPI_File fh, MPI_Offset size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_set_size)(real_file, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_get_size = PMPI_File_get_size
int PMPI_File_get_size(MPI_File fh, MPI_Offset *size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_get_size)(real_file, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_set_view = PMPI_File_set_view
int PMPI_File_set_view(MPI_File fh, MPI_Offset disp,
                      MPI_Datatype etype, MPI_Datatype filetype,
                      const char *datarep, MPI_Info info)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype realEtype = get_real_id((mana_mpi_handle){.datatype = etype}).datatype;
  MPI_Datatype realFtype = get_real_id((mana_mpi_handle){.datatype = filetype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_set_view)(real_file, disp, realEtype, realFtype,
                                    datarep, info);
  RETURN_TO_UPPER_HALF();

  // Normally, we wait until checkpoint time to update any file characteristics
  // However, we can't call set_view if there is no valid view
  // We also have to make sure that the info is saved (we can't get the info
  // at checkpoint time)
  // File characteristics are saved in the global parameters map, instead of in
  // the lower-half MPI_File struct, because this struct is opaque and non-
  // portable
  if (retval == MPI_SUCCESS) {
    g_params_map[fh]._viewSet = 1;
    g_params_map[fh]._viewInfo = info;
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

// TODO: Use descriptor to save etype and filetype set by File_set_view
#pragma weak MPI_File_get_view = PMPI_File_get_view
int PMPI_File_get_view(MPI_File fh, MPI_Offset* disp,
                      MPI_Datatype *etype, MPI_Datatype *filetype,
                      char *datarep)
{
  int retval = MPI_SUCCESS;
  /*
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype realEtype;
  MPI_Datatype realFtype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_get_view)(real_file, disp, &realEtype, &realFtype,
                                    datarep);
  RETURN_TO_UPPER_HALF();
  *etype = REAL_TO_VIRTUAL_TYPE(realEtype);
  *filetype = REAL_TO_VIRTUAL_TYPE(realFtype);
  DMTCP_PLUGIN_ENABLE_CKPT();
  */
  return retval;
}

#pragma weak MPI_File_read = PMPI_File_read
int PMPI_File_read(MPI_File fh, void *buf, int count,
                  MPI_Datatype datatype, MPI_Status *status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_read)(real_file, buf, count, real_datatype, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_read_at = PMPI_File_read_at
int PMPI_File_read_at(MPI_File fh, MPI_Offset offset,
                     void *buf, int count, MPI_Datatype datatype,
                     MPI_Status *status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_read_at)(real_file, offset, buf, count, real_datatype,
                                   status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_read_at_all = PMPI_File_read_at_all
int PMPI_File_read_at_all(MPI_File fh, MPI_Offset offset,
                         void *buf, int count, MPI_Datatype datatype,
                         MPI_Status *status)
{
  // FIXME:
  // This function is both blocking and collective. However, we believe that
  // this function is both rarely called enough and fast enough to avoid
  // requiring the trivial barrier. If the app begins to hang at this call,
  // add commit_begin() and commit_finish() to the start/end of this wrapper.
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_read_at_all)(real_file, offset, buf, count, real_datatype,
                                       status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_read_all = PMPI_File_read_all
int PMPI_File_read_all(MPI_File fh, void *buf,
                      int count, MPI_Datatype datatype, MPI_Status *status)
{
  // FIXME:
  // This function is both blocking and collective. However, we believe that
  // this function is both rarely called enough and fast enough to avoid
  // requiring the trivial barrier. If the app begins to hang at this call,
  // add commit_begin() and commit_finish() to the start/end of this wrapper.
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_read_all)(real_file, buf, count, real_datatype, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_write = PMPI_File_write
int PMPI_File_write(MPI_File fh, const void *buf,
                   int count, MPI_Datatype datatype, MPI_Status *status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_write)(real_file, buf, count, real_datatype, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_write_at = PMPI_File_write_at
int PMPI_File_write_at(MPI_File fh, MPI_Offset offset,
                      const void *buf, int count, MPI_Datatype datatype,
                      MPI_Status *status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_write_at)(real_file, offset, buf, count, real_datatype,
                                    status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_write_at_all = PMPI_File_write_at_all
int PMPI_File_write_at_all(MPI_File fh, MPI_Offset offset,
                          const void *buf, int count, MPI_Datatype datatype,
                          MPI_Status *status)
{
  // FIXME: See File_read_at_all (the same applies here)
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_write_at_all)(real_file, offset, buf, count,
                                        real_datatype, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_write_all = PMPI_File_write_all
int PMPI_File_write_all(MPI_File fh, const void *buf,
                       int count, MPI_Datatype datatype, MPI_Status *status)
{
  // FIXME: See File_read_all (the same applies here)
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  MPI_Datatype real_datatype = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_write_all)(real_file, buf, count, real_datatype, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_sync = PMPI_File_sync
int PMPI_File_sync(MPI_File fh)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_sync)(real_file);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_get_position = PMPI_File_get_position
int PMPI_File_get_position(MPI_File fh, MPI_Offset* offset)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_get_position)(real_file, offset);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_seek = PMPI_File_seek
int PMPI_File_seek(MPI_File fh, MPI_Offset offset, int whence)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = fh}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_seek)(real_file, offset, whence);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_close = PMPI_File_close
int PMPI_File_close(MPI_File *fh)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = *fh}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_close)(&real_file);
  RETURN_TO_UPPER_HALF();

  // Remove closed file from virtual to real mapping
  // We don't care if the underlying file is still open with another handle
  // or file descriptor, we just don't want to accidentally restore this
  // handle (which is unique to this process only)
  if (retval == MPI_SUCCESS) {
    g_params_map.erase(*fh);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_delete = PMPI_File_delete
int PMPI_File_delete(const char *filename, MPI_Info info)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_delete)(filename, info);
  RETURN_TO_UPPER_HALF();

  // MPI_File_delete results in an error if the file is opened by any other
  // process. So, it is fair to assume that the rank has already called
  // MPI_File_close to close the file. Also, this function results in a
  // MPI_ERR_NO_SUCH_FILE if the file doesn't exist. Therefore, we can safely
  // skip erasing the file handle from the g_params_map.

  // FIXME: if there are stale entries in the g_params_map then one can use find
  // and erase the file handle key from the g_params_map here.
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}


#pragma weak MPI_File_set_errhandler = PMPI_File_set_errhandler
int PMPI_File_set_errhandler(MPI_File file, MPI_Errhandler errhandler)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = file}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_set_errhandler)(real_file, errhandler);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_File_get_errhandler = PMPI_File_get_errhandler
int PMPI_File_get_errhandler(MPI_File file, MPI_Errhandler *errhandler)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File real_file = get_real_id((mana_mpi_handle){.file = file}).file;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(File_get_errhandler)(real_file, errhandler);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

} // end of: extern "C"
