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
#include "virtual-ids.h"
#include "seq_num.h"

using namespace dmtcp_mpi;

std::unordered_map<MPI_File, OpenFileParameters> g_params_map;

// It's possible that there could be a Fortran to C interface bug in the
// passing of the filename. If there are ever any bugs related to MPI
// opening a file with the wrong name, that's likely the cause
USER_DEFINED_WRAPPER(int, File_open, (MPI_Comm) comm, (const char *) filename,
                     (int) amode, (MPI_Info) info, (MPI_File *) fh)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_open)(realComm, filename, amode, info, fh);
  RETURN_TO_UPPER_HALF();

  // Save file initialization parameters to restore the file on restart
  if (retval == MPI_SUCCESS) {
    // Add file to virtual to real mapping
    MPI_File virtFile = ADD_NEW_FILE(*fh);
    *fh = virtFile;

    // Save parameters
    OpenFileParameters params;
    params._mode = amode;
    params._comm = comm;
    params._info = info;
    strcpy(params._filepath, filename);

    // If view isn't set, we can't restore any view
    params._viewSet = 0;

    // Save this file handle to the global map
    g_params_map[virtFile] = params;
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_get_atomicity, (MPI_File) fh, (int*) flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_get_atomicity)(realFile, flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_set_atomicity, (MPI_File) fh, (int) flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_set_atomicity)(realFile, flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_set_size, (MPI_File) fh, (MPI_Offset) size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_set_size)(realFile, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_get_size, (MPI_File) fh, (MPI_Offset *) size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_get_size)(realFile, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_set_view, (MPI_File) fh, (MPI_Offset) disp,
                     (MPI_Datatype) etype, (MPI_Datatype) filetype,
                     (const char*) datarep, (MPI_Info) info)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realEtype = VIRTUAL_TO_REAL_TYPE(etype);
  MPI_Datatype realFtype = VIRTUAL_TO_REAL_TYPE(filetype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_set_view)(realFile, disp, realEtype, realFtype,
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

USER_DEFINED_WRAPPER(int, File_get_view, (MPI_File) fh, (MPI_Offset*) disp,
                     (MPI_Datatype*) etype, (MPI_Datatype*) filetype,
                     (char*) datarep)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realEtype;
  MPI_Datatype realFtype;
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_get_view)(realFile, disp, &realEtype, &realFtype,
                                    datarep);
  RETURN_TO_UPPER_HALF();
  *etype = REAL_TO_VIRTUAL_TYPE(realEtype);
  *filetype = REAL_TO_VIRTUAL_TYPE(realFtype);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_read, (MPI_File) fh, (void*) buf, (int) count,
                     (MPI_Datatype) datatype, (MPI_Status*) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_read)(realFile, buf, count, realType, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_read_at, (MPI_File) fh, (MPI_Offset) offset,
                     (void*) buf, (int) count, (MPI_Datatype) datatype,
                     (MPI_Status*) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_read_at)(realFile, offset, buf, count, realType,
                                   status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_read_at_all, (MPI_File) fh, (MPI_Offset) offset,
                     (void*) buf, (int) count, (MPI_Datatype) datatype,
                     (MPI_Status*) status)
{
  // FIXME:
  // This function is both blocking and collective. However, we believe that
  // this function is both rarely called enough and fast enough to avoid
  // requiring the trivial barrier. If the app begins to hang at this call,
  // add commit_begin() and commit_finish() to the start/end of this wrapper.
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_read_at_all)(realFile, offset, buf, count, realType,
                                       status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_read_all, (MPI_File) fh, (void*) buf,
                    (int) count, (MPI_Datatype) datatype, (MPI_Status *) status)
{
  // FIXME:
  // This function is both blocking and collective. However, we believe that
  // this function is both rarely called enough and fast enough to avoid
  // requiring the trivial barrier. If the app begins to hang at this call,
  // add commit_begin() and commit_finish() to the start/end of this wrapper.
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_read_all)(realFile, buf, count, realType, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_write, (MPI_File) fh, (const void*) buf,
                     (int) count, (MPI_Datatype) datatype, (MPI_Status*) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_write)(realFile, buf, count, realType, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_write_at, (MPI_File) fh, (MPI_Offset) offset,
                     (const void*) buf, (int) count, (MPI_Datatype) datatype,
                     (MPI_Status*) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_write_at)(realFile, offset, buf, count, realType,
                                    status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_write_at_all, (MPI_File) fh, (MPI_Offset) offset,
                     (const void*) buf, (int) count, (MPI_Datatype) datatype,
                     (MPI_Status*) status)
{
  // FIXME: See File_read_at_all (the same applies here)
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_write_at_all)(realFile, offset, buf, count,
                                        realType, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_write_all, (MPI_File) fh, (const void*) buf,
                     (int) count, (MPI_Datatype) datatype, (MPI_Status*) status)
{
  // FIXME: See File_read_all (the same applies here)
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_write_all)(realFile, buf, count, realType, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_sync, (MPI_File) fh)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_sync)(realFile);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_get_position, (MPI_File) fh,
                     (MPI_Offset*) offset)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_get_position)(realFile, offset);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_seek, (MPI_File) fh, (MPI_Offset) offset,
                     (int) whence)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(fh);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_seek)(realFile, offset, whence);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_close, (MPI_File*) fh)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(*fh);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_close)(&realFile);
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

USER_DEFINED_WRAPPER(int, File_delete, (const char *) filename, (MPI_Info) info)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
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


USER_DEFINED_WRAPPER(int, File_set_errhandler, (MPI_File) file,
                     (MPI_Errhandler) errhandler)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(file);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_set_errhandler)(realFile, errhandler);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, File_get_errhandler, (MPI_File) file,
                     (MPI_Errhandler *) errhandler)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_File realFile = VIRTUAL_TO_REAL_FILE(file);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_get_errhandler)(realFile, errhandler);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}


PMPI_IMPL(int, MPI_File_open, MPI_Comm comm, const char *filename, int amode,
          MPI_Info info, MPI_File *fh)
PMPI_IMPL(int, MPI_File_get_atomicity, MPI_File fh, int* flag)
PMPI_IMPL(int, MPI_File_set_atomicity, MPI_File fh, int flag)
PMPI_IMPL(int, MPI_File_set_size, MPI_File fh, MPI_Offset size)
PMPI_IMPL(int, MPI_File_get_size, MPI_File fh, MPI_Offset* size)
PMPI_IMPL(int, MPI_File_set_view, MPI_File fh, MPI_Offset disp,
          MPI_Datatype etype, MPI_Datatype filetype, const char* datarep,
          MPI_Info info)
PMPI_IMPL(int, MPI_File_get_view, MPI_File fh, MPI_Offset* disp,
          MPI_Datatype* etype, MPI_Datatype* filetype, char* datarep)
PMPI_IMPL(int, MPI_File_read, MPI_File fh, void* buf, int count,
          MPI_Datatype datatype, MPI_Status* status)
PMPI_IMPL(int, MPI_File_read_all, MPI_File fh, void* buf, int count,
          MPI_Datatype datatype, MPI_Status* status)
PMPI_IMPL(int, MPI_File_read_at, MPI_File fh, MPI_Offset offset, void* buf,
          int count, MPI_Datatype datatype, MPI_Status* status)
PMPI_IMPL(int, MPI_File_read_at_all, MPI_File fh, MPI_Offset offset, void* buf,
          int count, MPI_Datatype datatype, MPI_Status* status)
PMPI_IMPL(int, MPI_File_write, MPI_File fh, const void* buf, int count,
          MPI_Datatype datatype, MPI_Status* status)
PMPI_IMPL(int, MPI_File_write_all, MPI_File fh, const void* buf, int count,
          MPI_Datatype datatype, MPI_Status* status)
PMPI_IMPL(int, MPI_File_write_at, MPI_File fh, MPI_Offset offset,
          const void* buf, int count, MPI_Datatype datatype, MPI_Status* status)
PMPI_IMPL(int, MPI_File_write_at_all, MPI_File fh, MPI_Offset offset,
          const void* buf, int count, MPI_Datatype datatype, MPI_Status* status)
PMPI_IMPL(int, MPI_File_sync, MPI_File fh)
PMPI_IMPL(int, MPI_File_get_position, MPI_File fh, MPI_Offset* offset)
PMPI_IMPL(int, MPI_File_seek, MPI_File fh, MPI_Offset offset, int whence)
PMPI_IMPL(int, MPI_File_close, MPI_File* fh)
PMPI_IMPL(int, MPI_File_set_errhandler, MPI_File file,
          MPI_Errhandler errhandler)
PMPI_IMPL(int, MPI_File_get_errhandler, MPI_File file,
          MPI_Errhandler *errhandler)
PMPI_IMPL(int, MPI_File_delete, const char *filename, MPI_Info info)