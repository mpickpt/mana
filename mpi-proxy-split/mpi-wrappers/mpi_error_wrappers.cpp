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

DEFINE_FNC(int, Error_class, (int) errorcode, (int *) errorclass);
DEFINE_FNC(int, Error_string, (int) errorcode, (char *) string,
           (int *) resultlen);

PMPI_IMPL(int, MPI_Error_class, int errorcode, int *errorclass)
PMPI_IMPL(int, MPI_Error_string, int errorcode, char *string,
          int *resultlen)

