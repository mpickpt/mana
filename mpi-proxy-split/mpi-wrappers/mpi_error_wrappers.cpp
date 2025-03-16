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

extern "C" {

#pragma weak MPI_Error_class = PMPI_Error_class
int PMPI_Error_class(int errorcode, int *errorclass)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Error_class)(errorcode, errorclass);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Error_string = PMPI_Error_string
int PMPI_Error_string(int errorcode, char *string, int *resultlen)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Error_string)(errorcode, string, resultlen);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

} // end of: extern "C"
