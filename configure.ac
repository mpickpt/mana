dnl Use 'autoreconf' to generate configure
AC_INIT([MANA],
        m4_esyscmd([tr -d '\n' < VERSION]),
        [https://github.com/mpickpt/mana/issues],
        [mana],
        [https://github.com/mpickpt/mana])

AC_PREREQ([2.60])
# TODO(kapil): Add 'subdir-objects after automake 1.16 has been released.
# AM_INIT_AUTOMAKE([foreign])
# AM_MAINTAINER_MODE
AC_CANONICAL_HOST
AC_PROG_INSTALL
# Automake uses 'ar cru' by default.  The 'ar' program then issues a warning.
AC_SUBST([ARFLAGS], [cr])
AC_PROG_RANLIB
AC_PROG_MAKE_SET
AC_PROG_LN_S
AC_PROG_MKDIR_P
dnl Due to a strange misfeature in autotools, AC_PROG_CXX will succeed if there
dnl is no CXX compiler and if AC_PROG_CXX is invoked _after_ AC_PROG_CC.
dnl So, we are invoking AC_PROG_CXX _before_ AC_PROG_CC, to take advantage
dnl of an undocumented autotools feature.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_CONFIG_MACRO_DIR([m4])
AX_CXX_COMPILE_STDCXX([14], [noext], [mandatory])

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

AC_MSG_CHECKING([for Python 3.7 or later])
AC_CACHE_CHECK([for python3 version >= 3.7], ac_cv_python3_version_3_7_or_later,
[
  ac_cv_python3_version_3_7_or_later=no
  if python3 -c 'import sys; exit(0 if sys.version_info >= (3, 7) else 1)' \
      2>/dev/null; then
    ac_cv_python3_version_3_7_or_later=yes
    PYTHON3_BIN="python3"
  fi
])
if test "x$ac_cv_python3_version_3_7_or_later" = "xyes"; then
  AC_MSG_RESULT([yes, found $PYTHON3_BIN])
  AC_SUBST([PYTHON3_BIN])
else
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Python 3.7 or later is required])
fi

AC_CHECK_FILE([dmtcp/configure],
  [AC_MSG_RESULT([yes, dmtcp/configure exists])
   AC_DEFINE([HAVE_DMTCP_CONFIGURE], 1, [Define if dmtcp/configure exists])
  ],
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
                             (default is
                             disabled); also, see --enable-logging])],
            [use_debug=$enableval],
            [use_debug=no])

if test "$use_debug" = "yes"; then
  AC_SUBST([DEBUG], [yes])
  AC_DEFINE([DEBUG],[1],[Use debugging flags "-Wall -g3 -O0"])
  CFLAGS="$CFLAGS -Wall -g3 -O0 -DDEBUG"
  CPPFLAGS="$CPPFLAGS -Wall -g3 -O0 -DDEBUG"
  CXXFLAGS="$CXXFLAGS -Wall -g3 -O0 -DDEBUG"
else
  AC_SUBST([DEBUG], [no])
fi

AC_SUBST([FFLAGS], [$FFLAGS])

# Keeping these DMTCP flags only for convenience.
AC_ARG_ENABLE([timing])
AC_ARG_ENABLE([logging])
AC_ARG_ENABLE([quiet])
AC_ARG_ENABLE([fsgsbase-override])

#check for pandoc
AC_PATH_PROG(PANDOC, [pandoc], [no], [/usr/bin:/bin])

AC_OUTPUT
