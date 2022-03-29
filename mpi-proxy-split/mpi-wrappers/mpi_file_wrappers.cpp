/****************************************************************************
 *   Copyright (C) 2019-2022 by Illio Suardi                                *
 *   illio@u.nus.edu                                                        *
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
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_nextfunc.h"
#include "record-replay.h"
#include "virtual-ids.h"

using namespace dmtcp_mpi;

USER_DEFINED_WRAPPER(int, File_open, (MPI_Comm) comm, (const char *) filename,
                     (int) amode, (MPI_Info) info, (MPI_File *) fh)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(File_open)(realComm, filename, amode, info, fh);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

DEFINE_FNC(int, File_close, (MPI_File *) fh);
DEFINE_FNC(int, File_get_size, (MPI_File) fh, (MPI_Offset *) size);
DEFINE_FNC(int, File_read_at, (MPI_File) fh, (MPI_Offset) offset, (void *) buf,
           (int) count, (MPI_Datatype) datatype, (MPI_Status *) status);

PMPI_IMPL(int, MPI_File_open, MPI_Comm comm, const char *filename, int amode,
          MPI_Info info, MPI_File *fh)
PMPI_IMPL(int, MPI_File_close, MPI_File *fh)
PMPI_IMPL(int, MPI_File_get_size, MPI_File fh, MPI_Offset *size)
PMPI_IMPL(int, MPI_File_read_at, MPI_File fh, MPI_Offset offset, void *buf,
          int count, MPI_Datatype datatype, MPI_Status *status)

