dnl Use 'autoreconf' to generate configure
AC_INIT([MANA],
        m4_esyscmd([tr -d '\n' < VERSION]),
        [https://github.com/mpickpt/mana/issues],
        [mana],
        [https://github.com/mpickpt/mana])

AC_PREREQ([2.60])
AC_PROG_INSTALL
dnl Due to a strange misfeature in autotools, AC_PROG_CXX will succeed if there
dnl is no CXX compiler and if AC_PROG_CXX is invoked _after_ AC_PROG_CC.
dnl So, we are invoking AC_PROG_CXX _before_ AC_PROG_CC, to take advantage
dnl of an undocumented autotools feature.
AC_PROG_CXX
AC_PROG_CC
AC_CONFIG_MACRO_DIR([m4])
AX_CXX_COMPILE_STDCXX([20], [noext], [mandatory])

AX_CHECK_COMPILE_FLAG([-std=gnu11],
                      [CFLAGS="${CFLAGS} -std=gnu11"],
                      [echo "C compiler cannot compile gnu11 code"; exit -1])

dnl gfortran-10 errors out if argument mismatch
dnl See if Fotran compiler supports -fallow-argument-mismatch for legacy code
AC_LANG_PUSH([Fortran])
AX_CHECK_COMPILE_FLAG([-fallow-argument-mismatch],
                      [FFLAGS="${FFLAGS} -fallow-argument-mismatch"],
                      [])
AC_LANG_POP([Fortran])

dnl MANA builds its wrappers and the lower half with the compiler wrappers
dnl of the MPI library.  The Cray Programming Environment, which sets
dnl CRAYPE_VERSION, names them cc, CC and ftn.
AC_ARG_VAR([MPICC], [MPI C compiler command])
AC_ARG_VAR([MPICXX], [MPI C++ compiler command])
AC_ARG_VAR([MPIFC], [MPI Fortran compiler command])
if test -n "$CRAYPE_VERSION"; then
  mana_mpicc=cc mana_mpicxx=CC mana_mpifc=ftn
else
  mana_mpicc=mpicc mana_mpicxx="mpic++ mpicxx" mana_mpifc="mpifort mpif90"
fi
AC_CHECK_PROGS([MPICC], [$mana_mpicc])
AC_CHECK_PROGS([MPICXX], [$mana_mpicxx])
AC_CHECK_PROGS([MPIFC], [$mana_mpifc])
if test -z "$MPICC" || test -z "$MPICXX" || test -z "$MPIFC"; then
  AC_MSG_ERROR([cannot find the MPI compilers.
Put the bin directory of the MPI library first in PATH,
or set MPICC, MPICXX and MPIFC.])
fi

AC_MSG_CHECKING([whether $MPICC builds an MPI program])
AC_LANG_PUSH([C])
mana_save_CC=$CC
CC=$MPICC
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <mpi.h>]],
                                [[MPI_Init(0, 0); MPI_Finalize();]])],
               [AC_MSG_RESULT([yes])],
               [AC_MSG_RESULT([no])
                AC_MSG_ERROR([$MPICC cannot build an MPI program.
Set MPICC to the C compiler of the MPI library.])])
CC=$mana_save_CC
AC_LANG_POP([C])

AC_CHECK_FILE([dmtcp/configure], [],
  [AC_MSG_ERROR([no, dmtcp/configure does not exist;
                      Did you do:  git submodule init && git submodule update])
  ])

ac_configure_args="$ac_configure_args --disable-dlsym-wrapper"
AC_CONFIG_SUBDIRS([dmtcp])

AC_CONFIG_FILES([Makefile \
                 manpages/Makefile \
                 mpi-proxy-split/Makefile_config])

dnl Autoconf manual says option checking is set to warn ("yes") by
dnl  by default.  But it's actually set to "no".
dnl So, we enforce our own choice ("fatal") if autoconf won't cooperate.
enable_option_checking=fatal
if test -n "$ac_unrecognized_opts"; then
  case $enable_option_checking in
    no) ;;
    fatal) { AS_ECHO(["$as_me: error: unrecognized options: $ac_unrecognized_opts"]) >&2
   { (exit 1); exit 1; }; } ;;
    *)     AS_ECHO(["$as_me: WARNING: unrecognized options: $ac_unrecognized_opts"]) >&2 ;;
  esac
fi

AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
                            [Use debugging flags "-Wall -g3 -O0" on DMTCP libs
                             (default is disabled)])],
            [use_debug=$enableval],
            [use_debug=no])

if test "$use_debug" = "yes"; then
  CFLAGS="$CFLAGS -Wall -g3 -O0 -DDEBUG"
  CPPFLAGS="$CPPFLAGS -Wall -g3 -O0 -DDEBUG"
  CXXFLAGS="$CXXFLAGS -Wall -g3 -O0 -DDEBUG"
fi

AC_SUBST([FFLAGS], [$FFLAGS])

# Keeping these DMTCP flags only for convenience.
AC_ARG_ENABLE([quiet])
AC_ARG_ENABLE([fsgsbase-override])

#check for pandoc
AC_PATH_PROG(PANDOC, [pandoc], [no], [/usr/bin:/bin])

AC_OUTPUT
