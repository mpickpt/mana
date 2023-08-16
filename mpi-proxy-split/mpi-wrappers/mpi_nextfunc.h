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

#include "lower_half_api.h"
#include "split_process.h"
#include "mpi_plugin.h"

#define EAT(x)
#define REM(x) x
#define STRIP(x) EAT x
#define PAIR(x) REM x

/* This counts the number of args */
#define NARGS_SEQ(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, N, ...) N
#define NARGS(...) NARGS_SEQ(__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

/* This will let macros expand before concating them */
#define PRIMITIVE_CAT(x, y) x ## y
#define CAT(x, y) PRIMITIVE_CAT(x, y)

/* This will call a macro on each argument passed in */
#define APPLY(macro, ...) CAT(APPLY_, NARGS(__VA_ARGS__))(macro, __VA_ARGS__)
#define APPLY_1(m, x1) m(x1)
#define APPLY_2(m, x1, x2) m(x1), m(x2)
#define APPLY_3(m, x1, x2, x3) m(x1), m(x2), m(x3)
#define APPLY_4(m, x1, x2, x3, x4) m(x1), m(x2), m(x3), m(x4)
#define APPLY_5(m, x1, x2, x3, x4, x5) m(x1), m(x2), m(x3), m(x4), m(x5)
#define APPLY_6(m, x1, x2, x3, x4, x5, x6) m(x1), m(x2), m(x3),                \
                                           m(x4), m(x5), m(x6)
#define APPLY_7(m, x1, x2, x3, x4, x5, x6, x7) m(x1), m(x2), m(x3),            \
                                               m(x4), m(x5), m(x6), m(x7)
#define APPLY_8(m, x1, x2, x3, x4, x5, x6, x7, x8) m(x1), m(x2), m(x3),        \
                                                   m(x4), m(x5), m(x6),        \
                                                   m(x7), m(x8)
#define APPLY_9(m, x1, x2, x3, x4, x5, x6, x7, x8, x9) m(x1), m(x2), m(x3),    \
                                                   m(x4), m(x5), m(x6),        \
                                                   m(x7), m(x8), m(x9)
#define APPLY_10(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10) m(x1), m(x2),     \
                                                   m(x3), m(x4), m(x5), m(x6), \
                                                   m(x7), m(x8), m(x9)
#define APPLY_11(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11) m(x1),       \
                                                   m(x2), m(x3), m(x4), m(x5), \
                                                   m(x6), m(x7), m(x8), m(x9)  \
                                                   m(x10), m(x11)
#define APPLY_12(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) m(x1),  \
                                                   m(x2), m(x3), m(x4), m(x5), \
                                                   m(x6), m(x7), m(x8), m(x9), \
                                                   m(x10), m(x11), m(x12)
// FIXME: We need to get rid of the static here. The lower half (and hence, the
// addresses of the functions) might change post restart; so, we cannot simply
// remember the old addresses. We need to update the addresses post restart.
#ifndef NEXT_FUNC
# define NEXT_FUNC(func)                                                       \
  ({                                                                           \
    static __typeof__(&MPI_##func)_real_MPI_## func =                          \
                                                (__typeof__(&MPI_##func)) - 1; \
    if (_real_MPI_ ## func == (__typeof__(&MPI_##func)) - 1) {                 \
      _real_MPI_ ## func = (__typeof__(&MPI_##func))pdlsym(MPI_Fnc_##func);    \
    }                                                                          \
    _real_MPI_ ## func;                                                        \
  })
#endif // ifndef NEXT_FUNC

// Convenience macro to define simple wrapper functions
#define DEFINE_FNC(rettype, name, args...)                                     \
  EXTERNC rettype MPI_##name(APPLY(PAIR, args))                                \
  {                                                                            \
    rettype retval;                                                            \
    DMTCP_PLUGIN_DISABLE_CKPT();                                               \
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);                                           \
    retval = NEXT_FUNC(name)(APPLY(STRIP, args));                              \
    RETURN_TO_UPPER_HALF();                                                    \
    DMTCP_PLUGIN_ENABLE_CKPT();                                                \
    return retval;                                                             \
  }

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

#define USER_DEFINED_WRAPPER(rettype, name, args...)                           \
  EXTERNC rettype MPI_##name(APPLY(PAIR, args))

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
extern void *FORTRAN_MPI_UNWEIGHTED;
extern void *FORTRAN_MPI_WEIGHTS_EMPTY;
extern void *FORTRAN_CONSTANTS_END;
#endif // #ifndef _MPI_NEXTFUNC_H
