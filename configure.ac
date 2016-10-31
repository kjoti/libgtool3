#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(libgtool3, 1.4.0)
AC_CONFIG_SRCDIR([gtool3.h])
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_LIBTOOL

#
AC_C_BIGENDIAN
AC_SYS_LARGEFILE

# Checks for libraries.
AC_CHECK_LIB(m, sin)

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])
AC_CHECK_HEADERS([glob.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_CHECK_TYPES(uint32_t)

# Checks for library functions.
AC_FUNC_FSEEKO
#AC_FUNC_MALLOC
AC_FUNC_MEMCMP
#AC_FUNC_REALLOC
AC_FUNC_STAT
AC_FUNC_STRTOD
AC_CHECK_FUNCS([sysconf])
AC_CHECK_FUNCS([memset strchr strdup strerror strtol])
AC_CHECK_FUNCS([pow sqrt round ilogb scalbn])
AC_CHECK_FUNCS([glob])
AC_OUTPUT
