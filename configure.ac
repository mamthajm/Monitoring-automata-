#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([FULL-PACKAGE-NAME], [VERSION], [BUG-REPORT-ADDRESS])
AC_CONFIG_SRCDIR([src/monitor-automata.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE


# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_CC_STDC

# Use the C language and compiler for the following checks
#AC_LANG([C])


AX_LIBXML2_CHECK()

# Checks for libraries.

PKG_CHECK_MODULES([ZMQ], [zmq], [have_libzmq=yes], [have_libzmq=no])
AM_CONDITIONAL([ZMQ],  [test "$have_libzmq" = "yes"])

PKG_CHECK_MODULES([CZMQ], [czmq], [have_libczmq=yes], [have_libczmq=no])
AM_CONDITIONAL([CZMQ],  [test "$have_libczmq" = "yes"])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdint.h stdlib.h string.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_INT64_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([gettimeofday strndup])


AC_CONFIG_FILES([Makefile
				 src/Makefile])
AC_OUTPUT
