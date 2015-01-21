# DO NOT EDIT! GENERATED AUTOMATICALLY!
# Copyright (C) 2002-2013 Free Software Foundation, Inc.
#
# This file is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This file is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this file.  If not, see <http://www.gnu.org/licenses/>.
#
# As a special exception to the GNU General Public License,
# this file may be distributed as part of a program that
# contains a configuration script generated by Autoconf, under
# the same distribution terms as the rest of that program.
#
# Generated by gnulib-tool.
#
# This file represents the compiled summary of the specification in
# gnulib-cache.m4. It lists the computed macro invocations that need
# to be invoked from configure.ac.
# In projects that use version control, this file can be treated like
# other built files.


# This macro should be invoked from ./configure.in, in the section
# "Checks for programs", right after AC_PROG_CC, and certainly before
# any checks for libraries, header files, types and library functions.
AC_DEFUN([gl_EARLY],
[
  m4_pattern_forbid([^gl_[A-Z]])dnl the gnulib macro namespace
  m4_pattern_allow([^gl_ES$])dnl a valid locale name
  m4_pattern_allow([^gl_LIBOBJS$])dnl a variable
  m4_pattern_allow([^gl_LTLIBOBJS$])dnl a variable
  AC_REQUIRE([gl_PROG_AR_RANLIB])
  AC_REQUIRE([AM_PROG_CC_C_O])
  # Code from module alloca-opt:
  # Code from module arpa_inet:
  # Code from module base64:
  # Code from module btowc:
  # Code from module c-ctype:
  # Code from module configmake:
  # Code from module dosname:
  # Code from module environ:
  # Code from module errno:
  # Code from module euidaccess:
  # Code from module extensions:
  AC_REQUIRE([gl_USE_SYSTEM_EXTENSIONS])
  # Code from module extern-inline:
  # Code from module fcntl-h:
  # Code from module fd-hook:
  # Code from module float:
  # Code from module fseek:
  # Code from module fseeko:
  AC_REQUIRE([AC_FUNC_FSEEKO])
  # Code from module fstat:
  # Code from module fsusage:
  # Code from module ftell:
  # Code from module ftello:
  AC_REQUIRE([AC_FUNC_FSEEKO])
  # Code from module ftruncate:
  # Code from module full-read:
  # Code from module full-write:
  # Code from module getaddrinfo:
  # Code from module getgroups:
  # Code from module getopt-gnu:
  # Code from module getopt-posix:
  # Code from module gettext-h:
  # Code from module gettimeofday:
  # Code from module group-member:
  # Code from module havelib:
  # Code from module hostent:
  # Code from module include_next:
  # Code from module inet_ntop:
  # Code from module inet_pton:
  # Code from module langinfo:
  # Code from module largefile:
  AC_REQUIRE([AC_SYS_LARGEFILE])
  # Code from module localcharset:
  # Code from module locale:
  # Code from module localeconv:
  # Code from module lock:
  # Code from module lseek:
  # Code from module lstat:
  # Code from module malloc-gnu:
  # Code from module malloc-posix:
  # Code from module mbrtowc:
  # Code from module mbsinit:
  # Code from module mbtowc:
  # Code from module memchr:
  # Code from module mkdtemp:
  # Code from module msvc-inval:
  # Code from module msvc-nothrow:
  # Code from module multiarch:
  # Code from module netdb:
  # Code from module netinet_in:
  # Code from module nl_langinfo:
  # Code from module nocrash:
  # Code from module pathmax:
  # Code from module physmem:
  # Code from module raise:
  # Code from module read:
  # Code from module regex:
  # Code from module root-uid:
  # Code from module safe-read:
  # Code from module safe-write:
  # Code from module secure_getenv:
  # Code from module servent:
  # Code from module signal-h:
  # Code from module size_max:
  # Code from module snippet/_Noreturn:
  # Code from module snippet/arg-nonnull:
  # Code from module snippet/c++defs:
  # Code from module snippet/warn-on-use:
  # Code from module snprintf:
  # Code from module socketlib:
  # Code from module sockets:
  # Code from module socklen:
  # Code from module ssize_t:
  # Code from module stat:
  # Code from module stdalign:
  # Code from module stdbool:
  # Code from module stddef:
  # Code from module stdint:
  # Code from module stdio:
  # Code from module stdlib:
  # Code from module streq:
  # Code from module string:
  # Code from module sys_socket:
  # Code from module sys_stat:
  # Code from module sys_time:
  # Code from module sys_types:
  # Code from module sys_uio:
  # Code from module tempname:
  # Code from module threadlib:
  gl_THREADLIB_EARLY
  # Code from module time:
  # Code from module unistd:
  # Code from module vasnprintf:
  # Code from module verify:
  # Code from module wchar:
  # Code from module wcrtomb:
  # Code from module wctype-h:
  # Code from module write:
  # Code from module xalloc-oversized:
  # Code from module xsize:
])

# This macro should be invoked from ./configure.in, in the section
# "Check for header files, types and library functions".
AC_DEFUN([gl_INIT],
[
  AM_CONDITIONAL([GL_COND_LIBTOOL], [true])
  gl_cond_libtool=true
  gl_m4_base='config/gnulib'
  m4_pushdef([AC_LIBOBJ], m4_defn([gl_LIBOBJ]))
  m4_pushdef([AC_REPLACE_FUNCS], m4_defn([gl_REPLACE_FUNCS]))
  m4_pushdef([AC_LIBSOURCES], m4_defn([gl_LIBSOURCES]))
  m4_pushdef([gl_LIBSOURCES_LIST], [])
  m4_pushdef([gl_LIBSOURCES_DIR], [])
  gl_COMMON
  gl_source_base='gnulib'
  gl_FUNC_ALLOCA
  gl_HEADER_ARPA_INET
  AC_PROG_MKDIR_P
  gl_FUNC_BASE64
  gl_FUNC_BTOWC
  if test $HAVE_BTOWC = 0 || test $REPLACE_BTOWC = 1; then
    AC_LIBOBJ([btowc])
    gl_PREREQ_BTOWC
  fi
  gl_WCHAR_MODULE_INDICATOR([btowc])
  gl_CONFIGMAKE_PREP
  gl_ENVIRON
  gl_UNISTD_MODULE_INDICATOR([environ])
  gl_HEADER_ERRNO_H
  gl_FUNC_EUIDACCESS
  if test $HAVE_EUIDACCESS = 0; then
    AC_LIBOBJ([euidaccess])
    gl_PREREQ_EUIDACCESS
  fi
  gl_UNISTD_MODULE_INDICATOR([euidaccess])
  AC_REQUIRE([gl_EXTERN_INLINE])
  gl_FCNTL_H
  gl_FLOAT_H
  if test $REPLACE_FLOAT_LDBL = 1; then
    AC_LIBOBJ([float])
  fi
  if test $REPLACE_ITOLD = 1; then
    AC_LIBOBJ([itold])
  fi
  gl_FUNC_FSEEK
  if test $REPLACE_FSEEK = 1; then
    AC_LIBOBJ([fseek])
  fi
  gl_STDIO_MODULE_INDICATOR([fseek])
  gl_FUNC_FSEEKO
  if test $HAVE_FSEEKO = 0 || test $REPLACE_FSEEKO = 1; then
    AC_LIBOBJ([fseeko])
    gl_PREREQ_FSEEKO
  fi
  gl_STDIO_MODULE_INDICATOR([fseeko])
  gl_FUNC_FSTAT
  if test $REPLACE_FSTAT = 1; then
    AC_LIBOBJ([fstat])
    gl_PREREQ_FSTAT
  fi
  gl_SYS_STAT_MODULE_INDICATOR([fstat])
  gl_FSUSAGE
  if test $gl_cv_fs_space = yes; then
    AC_LIBOBJ([fsusage])
    gl_PREREQ_FSUSAGE_EXTRA
  fi
  gl_FUNC_FTELL
  if test $REPLACE_FTELL = 1; then
    AC_LIBOBJ([ftell])
  fi
  gl_STDIO_MODULE_INDICATOR([ftell])
  gl_FUNC_FTELLO
  if test $HAVE_FTELLO = 0 || test $REPLACE_FTELLO = 1; then
    AC_LIBOBJ([ftello])
    gl_PREREQ_FTELLO
  fi
  gl_STDIO_MODULE_INDICATOR([ftello])
  gl_FUNC_FTRUNCATE
  if test $HAVE_FTRUNCATE = 0 || test $REPLACE_FTRUNCATE = 1; then
    AC_LIBOBJ([ftruncate])
    gl_PREREQ_FTRUNCATE
  fi
  gl_UNISTD_MODULE_INDICATOR([ftruncate])
  gl_GETADDRINFO
  if test $HAVE_GETADDRINFO = 0; then
    AC_LIBOBJ([getaddrinfo])
  fi
  if test $HAVE_DECL_GAI_STRERROR = 0 || test $REPLACE_GAI_STRERROR = 1; then
    AC_LIBOBJ([gai_strerror])
  fi
  gl_NETDB_MODULE_INDICATOR([getaddrinfo])
  gl_FUNC_GETGROUPS
  if test $HAVE_GETGROUPS = 0 || test $REPLACE_GETGROUPS = 1; then
    AC_LIBOBJ([getgroups])
  fi
  gl_UNISTD_MODULE_INDICATOR([getgroups])
  gl_FUNC_GETOPT_GNU
  if test $REPLACE_GETOPT = 1; then
    AC_LIBOBJ([getopt])
    AC_LIBOBJ([getopt1])
    gl_PREREQ_GETOPT
    dnl Arrange for unistd.h to include getopt.h.
    GNULIB_GL_UNISTD_H_GETOPT=1
  fi
  AC_SUBST([GNULIB_GL_UNISTD_H_GETOPT])
  gl_MODULE_INDICATOR_FOR_TESTS([getopt-gnu])
  gl_FUNC_GETOPT_POSIX
  if test $REPLACE_GETOPT = 1; then
    AC_LIBOBJ([getopt])
    AC_LIBOBJ([getopt1])
    gl_PREREQ_GETOPT
    dnl Arrange for unistd.h to include getopt.h.
    GNULIB_GL_UNISTD_H_GETOPT=1
  fi
  AC_SUBST([GNULIB_GL_UNISTD_H_GETOPT])
  AC_SUBST([LIBINTL])
  AC_SUBST([LTLIBINTL])
  gl_FUNC_GETTIMEOFDAY
  if test $HAVE_GETTIMEOFDAY = 0 || test $REPLACE_GETTIMEOFDAY = 1; then
    AC_LIBOBJ([gettimeofday])
    gl_PREREQ_GETTIMEOFDAY
  fi
  gl_SYS_TIME_MODULE_INDICATOR([gettimeofday])
  gl_FUNC_GROUP_MEMBER
  if test $HAVE_GROUP_MEMBER = 0; then
    AC_LIBOBJ([group-member])
    gl_PREREQ_GROUP_MEMBER
  fi
  gl_UNISTD_MODULE_INDICATOR([group-member])
  gl_HOSTENT
  gl_FUNC_INET_NTOP
  if test $HAVE_INET_NTOP = 0 || test $REPLACE_INET_NTOP = 1; then
    AC_LIBOBJ([inet_ntop])
    gl_PREREQ_INET_NTOP
  fi
  gl_ARPA_INET_MODULE_INDICATOR([inet_ntop])
  gl_FUNC_INET_PTON
  if test $HAVE_INET_PTON = 0 || test $REPLACE_INET_NTOP = 1; then
    AC_LIBOBJ([inet_pton])
    gl_PREREQ_INET_PTON
  fi
  gl_ARPA_INET_MODULE_INDICATOR([inet_pton])
  gl_LANGINFO_H
  AC_REQUIRE([gl_LARGEFILE])
  gl_LOCALCHARSET
  LOCALCHARSET_TESTS_ENVIRONMENT="CHARSETALIASDIR=\"\$(abs_top_builddir)/$gl_source_base\""
  AC_SUBST([LOCALCHARSET_TESTS_ENVIRONMENT])
  gl_LOCALE_H
  gl_FUNC_LOCALECONV
  if test $REPLACE_LOCALECONV = 1; then
    AC_LIBOBJ([localeconv])
    gl_PREREQ_LOCALECONV
  fi
  gl_LOCALE_MODULE_INDICATOR([localeconv])
  gl_LOCK
  gl_FUNC_LSEEK
  if test $REPLACE_LSEEK = 1; then
    AC_LIBOBJ([lseek])
  fi
  gl_UNISTD_MODULE_INDICATOR([lseek])
  gl_FUNC_LSTAT
  if test $REPLACE_LSTAT = 1; then
    AC_LIBOBJ([lstat])
    gl_PREREQ_LSTAT
  fi
  gl_SYS_STAT_MODULE_INDICATOR([lstat])
  gl_FUNC_MALLOC_GNU
  if test $REPLACE_MALLOC = 1; then
    AC_LIBOBJ([malloc])
  fi
  gl_MODULE_INDICATOR([malloc-gnu])
  gl_FUNC_MALLOC_POSIX
  if test $REPLACE_MALLOC = 1; then
    AC_LIBOBJ([malloc])
  fi
  gl_STDLIB_MODULE_INDICATOR([malloc-posix])
  gl_FUNC_MBRTOWC
  if test $HAVE_MBRTOWC = 0 || test $REPLACE_MBRTOWC = 1; then
    AC_LIBOBJ([mbrtowc])
    gl_PREREQ_MBRTOWC
  fi
  gl_WCHAR_MODULE_INDICATOR([mbrtowc])
  gl_FUNC_MBSINIT
  if test $HAVE_MBSINIT = 0 || test $REPLACE_MBSINIT = 1; then
    AC_LIBOBJ([mbsinit])
    gl_PREREQ_MBSINIT
  fi
  gl_WCHAR_MODULE_INDICATOR([mbsinit])
  gl_FUNC_MBTOWC
  if test $REPLACE_MBTOWC = 1; then
    AC_LIBOBJ([mbtowc])
    gl_PREREQ_MBTOWC
  fi
  gl_STDLIB_MODULE_INDICATOR([mbtowc])
  gl_FUNC_MEMCHR
  if test $HAVE_MEMCHR = 0 || test $REPLACE_MEMCHR = 1; then
    AC_LIBOBJ([memchr])
    gl_PREREQ_MEMCHR
  fi
  gl_STRING_MODULE_INDICATOR([memchr])
  gl_FUNC_MKDTEMP
  if test $HAVE_MKDTEMP = 0; then
    AC_LIBOBJ([mkdtemp])
    gl_PREREQ_MKDTEMP
  fi
  gl_STDLIB_MODULE_INDICATOR([mkdtemp])
  gl_MSVC_INVAL
  if test $HAVE_MSVC_INVALID_PARAMETER_HANDLER = 1; then
    AC_LIBOBJ([msvc-inval])
  fi
  gl_MSVC_NOTHROW
  if test $HAVE_MSVC_INVALID_PARAMETER_HANDLER = 1; then
    AC_LIBOBJ([msvc-nothrow])
  fi
  gl_MULTIARCH
  gl_HEADER_NETDB
  gl_HEADER_NETINET_IN
  AC_PROG_MKDIR_P
  gl_FUNC_NL_LANGINFO
  if test $HAVE_NL_LANGINFO = 0 || test $REPLACE_NL_LANGINFO = 1; then
    AC_LIBOBJ([nl_langinfo])
  fi
  gl_LANGINFO_MODULE_INDICATOR([nl_langinfo])
  gl_PATHMAX
  gl_PHYSMEM
  gl_FUNC_RAISE
  if test $HAVE_RAISE = 0 || test $REPLACE_RAISE = 1; then
    AC_LIBOBJ([raise])
    gl_PREREQ_RAISE
  fi
  gl_SIGNAL_MODULE_INDICATOR([raise])
  gl_FUNC_READ
  if test $REPLACE_READ = 1; then
    AC_LIBOBJ([read])
    gl_PREREQ_READ
  fi
  gl_UNISTD_MODULE_INDICATOR([read])
  gl_REGEX
  if test $ac_use_included_regex = yes; then
    AC_LIBOBJ([regex])
    gl_PREREQ_REGEX
  fi
  gl_PREREQ_SAFE_READ
  gl_PREREQ_SAFE_WRITE
  gl_FUNC_SECURE_GETENV
  if test $HAVE_SECURE_GETENV = 0; then
    AC_LIBOBJ([secure_getenv])
    gl_PREREQ_SECURE_GETENV
  fi
  gl_STDLIB_MODULE_INDICATOR([secure_getenv])
  gl_SERVENT
  gl_SIGNAL_H
  gl_SIZE_MAX
  gl_FUNC_SNPRINTF
  gl_STDIO_MODULE_INDICATOR([snprintf])
  gl_MODULE_INDICATOR([snprintf])
  gl_SOCKETLIB
  gl_SOCKETS
  gl_TYPE_SOCKLEN_T
  gt_TYPE_SSIZE_T
  gl_FUNC_STAT
  if test $REPLACE_STAT = 1; then
    AC_LIBOBJ([stat])
    gl_PREREQ_STAT
  fi
  gl_SYS_STAT_MODULE_INDICATOR([stat])
  gl_STDALIGN_H
  AM_STDBOOL_H
  gl_STDDEF_H
  gl_STDINT_H
  gl_STDIO_H
  gl_STDLIB_H
  gl_HEADER_STRING_H
  gl_HEADER_SYS_SOCKET
  AC_PROG_MKDIR_P
  gl_HEADER_SYS_STAT_H
  AC_PROG_MKDIR_P
  gl_HEADER_SYS_TIME_H
  AC_PROG_MKDIR_P
  gl_SYS_TYPES_H
  AC_PROG_MKDIR_P
  gl_HEADER_SYS_UIO
  AC_PROG_MKDIR_P
  gl_FUNC_GEN_TEMPNAME
  gl_THREADLIB
  gl_HEADER_TIME_H
  gl_UNISTD_H
  gl_FUNC_VASNPRINTF
  gl_WCHAR_H
  gl_FUNC_WCRTOMB
  if test $HAVE_WCRTOMB = 0 || test $REPLACE_WCRTOMB = 1; then
    AC_LIBOBJ([wcrtomb])
    gl_PREREQ_WCRTOMB
  fi
  gl_WCHAR_MODULE_INDICATOR([wcrtomb])
  gl_WCTYPE_H
  gl_FUNC_WRITE
  if test $REPLACE_WRITE = 1; then
    AC_LIBOBJ([write])
    gl_PREREQ_WRITE
  fi
  gl_UNISTD_MODULE_INDICATOR([write])
  gl_XSIZE
  # End of code from modules
  m4_ifval(gl_LIBSOURCES_LIST, [
    m4_syscmd([test ! -d ]m4_defn([gl_LIBSOURCES_DIR])[ ||
      for gl_file in ]gl_LIBSOURCES_LIST[ ; do
        if test ! -r ]m4_defn([gl_LIBSOURCES_DIR])[/$gl_file ; then
          echo "missing file ]m4_defn([gl_LIBSOURCES_DIR])[/$gl_file" >&2
          exit 1
        fi
      done])dnl
      m4_if(m4_sysval, [0], [],
        [AC_FATAL([expected source file, required through AC_LIBSOURCES, not found])])
  ])
  m4_popdef([gl_LIBSOURCES_DIR])
  m4_popdef([gl_LIBSOURCES_LIST])
  m4_popdef([AC_LIBSOURCES])
  m4_popdef([AC_REPLACE_FUNCS])
  m4_popdef([AC_LIBOBJ])
  AC_CONFIG_COMMANDS_PRE([
    gl_libobjs=
    gl_ltlibobjs=
    if test -n "$gl_LIBOBJS"; then
      # Remove the extension.
      sed_drop_objext='s/\.o$//;s/\.obj$//'
      for i in `for i in $gl_LIBOBJS; do echo "$i"; done | sed -e "$sed_drop_objext" | sort | uniq`; do
        gl_libobjs="$gl_libobjs $i.$ac_objext"
        gl_ltlibobjs="$gl_ltlibobjs $i.lo"
      done
    fi
    AC_SUBST([gl_LIBOBJS], [$gl_libobjs])
    AC_SUBST([gl_LTLIBOBJS], [$gl_ltlibobjs])
  ])
  gltests_libdeps=
  gltests_ltlibdeps=
  m4_pushdef([AC_LIBOBJ], m4_defn([gltests_LIBOBJ]))
  m4_pushdef([AC_REPLACE_FUNCS], m4_defn([gltests_REPLACE_FUNCS]))
  m4_pushdef([AC_LIBSOURCES], m4_defn([gltests_LIBSOURCES]))
  m4_pushdef([gltests_LIBSOURCES_LIST], [])
  m4_pushdef([gltests_LIBSOURCES_DIR], [])
  gl_COMMON
  gl_source_base='tests'
changequote(,)dnl
  gltests_WITNESS=IN_`echo "${PACKAGE-$PACKAGE_TARNAME}" | LC_ALL=C tr abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ | LC_ALL=C sed -e 's/[^A-Z0-9_]/_/g'`_GNULIB_TESTS
changequote([, ])dnl
  AC_SUBST([gltests_WITNESS])
  gl_module_indicator_condition=$gltests_WITNESS
  m4_pushdef([gl_MODULE_INDICATOR_CONDITION], [$gl_module_indicator_condition])
  m4_popdef([gl_MODULE_INDICATOR_CONDITION])
  m4_ifval(gltests_LIBSOURCES_LIST, [
    m4_syscmd([test ! -d ]m4_defn([gltests_LIBSOURCES_DIR])[ ||
      for gl_file in ]gltests_LIBSOURCES_LIST[ ; do
        if test ! -r ]m4_defn([gltests_LIBSOURCES_DIR])[/$gl_file ; then
          echo "missing file ]m4_defn([gltests_LIBSOURCES_DIR])[/$gl_file" >&2
          exit 1
        fi
      done])dnl
      m4_if(m4_sysval, [0], [],
        [AC_FATAL([expected source file, required through AC_LIBSOURCES, not found])])
  ])
  m4_popdef([gltests_LIBSOURCES_DIR])
  m4_popdef([gltests_LIBSOURCES_LIST])
  m4_popdef([AC_LIBSOURCES])
  m4_popdef([AC_REPLACE_FUNCS])
  m4_popdef([AC_LIBOBJ])
  AC_CONFIG_COMMANDS_PRE([
    gltests_libobjs=
    gltests_ltlibobjs=
    if test -n "$gltests_LIBOBJS"; then
      # Remove the extension.
      sed_drop_objext='s/\.o$//;s/\.obj$//'
      for i in `for i in $gltests_LIBOBJS; do echo "$i"; done | sed -e "$sed_drop_objext" | sort | uniq`; do
        gltests_libobjs="$gltests_libobjs $i.$ac_objext"
        gltests_ltlibobjs="$gltests_ltlibobjs $i.lo"
      done
    fi
    AC_SUBST([gltests_LIBOBJS], [$gltests_libobjs])
    AC_SUBST([gltests_LTLIBOBJS], [$gltests_ltlibobjs])
  ])
])

# Like AC_LIBOBJ, except that the module name goes
# into gl_LIBOBJS instead of into LIBOBJS.
AC_DEFUN([gl_LIBOBJ], [
  AS_LITERAL_IF([$1], [gl_LIBSOURCES([$1.c])])dnl
  gl_LIBOBJS="$gl_LIBOBJS $1.$ac_objext"
])

# Like AC_REPLACE_FUNCS, except that the module name goes
# into gl_LIBOBJS instead of into LIBOBJS.
AC_DEFUN([gl_REPLACE_FUNCS], [
  m4_foreach_w([gl_NAME], [$1], [AC_LIBSOURCES(gl_NAME[.c])])dnl
  AC_CHECK_FUNCS([$1], , [gl_LIBOBJ($ac_func)])
])

# Like AC_LIBSOURCES, except the directory where the source file is
# expected is derived from the gnulib-tool parameterization,
# and alloca is special cased (for the alloca-opt module).
# We could also entirely rely on EXTRA_lib..._SOURCES.
AC_DEFUN([gl_LIBSOURCES], [
  m4_foreach([_gl_NAME], [$1], [
    m4_if(_gl_NAME, [alloca.c], [], [
      m4_define([gl_LIBSOURCES_DIR], [gnulib])
      m4_append([gl_LIBSOURCES_LIST], _gl_NAME, [ ])
    ])
  ])
])

# Like AC_LIBOBJ, except that the module name goes
# into gltests_LIBOBJS instead of into LIBOBJS.
AC_DEFUN([gltests_LIBOBJ], [
  AS_LITERAL_IF([$1], [gltests_LIBSOURCES([$1.c])])dnl
  gltests_LIBOBJS="$gltests_LIBOBJS $1.$ac_objext"
])

# Like AC_REPLACE_FUNCS, except that the module name goes
# into gltests_LIBOBJS instead of into LIBOBJS.
AC_DEFUN([gltests_REPLACE_FUNCS], [
  m4_foreach_w([gl_NAME], [$1], [AC_LIBSOURCES(gl_NAME[.c])])dnl
  AC_CHECK_FUNCS([$1], , [gltests_LIBOBJ($ac_func)])
])

# Like AC_LIBSOURCES, except the directory where the source file is
# expected is derived from the gnulib-tool parameterization,
# and alloca is special cased (for the alloca-opt module).
# We could also entirely rely on EXTRA_lib..._SOURCES.
AC_DEFUN([gltests_LIBSOURCES], [
  m4_foreach([_gl_NAME], [$1], [
    m4_if(_gl_NAME, [alloca.c], [], [
      m4_define([gltests_LIBSOURCES_DIR], [tests])
      m4_append([gltests_LIBSOURCES_LIST], _gl_NAME, [ ])
    ])
  ])
])

# This macro records the list of files which have been installed by
# gnulib-tool and may be removed by future gnulib-tool invocations.
AC_DEFUN([gl_FILE_LIST], [
  build-aux/config.rpath
  build-aux/snippet/_Noreturn.h
  build-aux/snippet/arg-nonnull.h
  build-aux/snippet/c++defs.h
  build-aux/snippet/warn-on-use.h
  lib/alloca.in.h
  lib/arpa_inet.in.h
  lib/asnprintf.c
  lib/base64.c
  lib/base64.h
  lib/btowc.c
  lib/c-ctype.c
  lib/c-ctype.h
  lib/config.charset
  lib/dosname.h
  lib/errno.in.h
  lib/euidaccess.c
  lib/fcntl.in.h
  lib/fd-hook.c
  lib/fd-hook.h
  lib/float+.h
  lib/float.c
  lib/float.in.h
  lib/fseek.c
  lib/fseeko.c
  lib/fstat.c
  lib/fsusage.c
  lib/fsusage.h
  lib/ftell.c
  lib/ftello.c
  lib/ftruncate.c
  lib/full-read.c
  lib/full-read.h
  lib/full-write.c
  lib/full-write.h
  lib/gai_strerror.c
  lib/getaddrinfo.c
  lib/getgroups.c
  lib/getopt.c
  lib/getopt.in.h
  lib/getopt1.c
  lib/getopt_int.h
  lib/gettext.h
  lib/gettimeofday.c
  lib/glthread/lock.c
  lib/glthread/lock.h
  lib/glthread/threadlib.c
  lib/group-member.c
  lib/inet_ntop.c
  lib/inet_pton.c
  lib/itold.c
  lib/langinfo.in.h
  lib/localcharset.c
  lib/localcharset.h
  lib/locale.in.h
  lib/localeconv.c
  lib/lseek.c
  lib/lstat.c
  lib/malloc.c
  lib/mbrtowc.c
  lib/mbsinit.c
  lib/mbtowc-impl.h
  lib/mbtowc.c
  lib/memchr.c
  lib/memchr.valgrind
  lib/mkdtemp.c
  lib/msvc-inval.c
  lib/msvc-inval.h
  lib/msvc-nothrow.c
  lib/msvc-nothrow.h
  lib/netdb.in.h
  lib/netinet_in.in.h
  lib/nl_langinfo.c
  lib/pathmax.h
  lib/physmem.c
  lib/physmem.h
  lib/printf-args.c
  lib/printf-args.h
  lib/printf-parse.c
  lib/printf-parse.h
  lib/raise.c
  lib/read.c
  lib/ref-add.sin
  lib/ref-del.sin
  lib/regcomp.c
  lib/regex.c
  lib/regex.h
  lib/regex_internal.c
  lib/regex_internal.h
  lib/regexec.c
  lib/root-uid.h
  lib/safe-read.c
  lib/safe-read.h
  lib/safe-write.c
  lib/safe-write.h
  lib/secure_getenv.c
  lib/signal.in.h
  lib/size_max.h
  lib/snprintf.c
  lib/sockets.c
  lib/sockets.h
  lib/stat.c
  lib/stdalign.in.h
  lib/stdbool.in.h
  lib/stddef.in.h
  lib/stdint.in.h
  lib/stdio-impl.h
  lib/stdio.in.h
  lib/stdlib.in.h
  lib/streq.h
  lib/string.in.h
  lib/sys_socket.c
  lib/sys_socket.in.h
  lib/sys_stat.in.h
  lib/sys_time.in.h
  lib/sys_types.in.h
  lib/sys_uio.in.h
  lib/tempname.c
  lib/tempname.h
  lib/time.in.h
  lib/unistd.c
  lib/unistd.in.h
  lib/vasnprintf.c
  lib/vasnprintf.h
  lib/verify.h
  lib/w32sock.h
  lib/wchar.in.h
  lib/wcrtomb.c
  lib/wctype-h.c
  lib/wctype.in.h
  lib/write.c
  lib/xalloc-oversized.h
  lib/xsize.c
  lib/xsize.h
  m4/00gnulib.m4
  m4/alloca.m4
  m4/arpa_inet_h.m4
  m4/base64.m4
  m4/btowc.m4
  m4/codeset.m4
  m4/configmake.m4
  m4/eealloc.m4
  m4/environ.m4
  m4/errno_h.m4
  m4/euidaccess.m4
  m4/exponentd.m4
  m4/extensions.m4
  m4/extern-inline.m4
  m4/fcntl-o.m4
  m4/fcntl_h.m4
  m4/float_h.m4
  m4/fseek.m4
  m4/fseeko.m4
  m4/fstat.m4
  m4/fsusage.m4
  m4/ftell.m4
  m4/ftello.m4
  m4/ftruncate.m4
  m4/getaddrinfo.m4
  m4/getgroups.m4
  m4/getopt.m4
  m4/gettimeofday.m4
  m4/glibc21.m4
  m4/gnulib-common.m4
  m4/group-member.m4
  m4/hostent.m4
  m4/include_next.m4
  m4/inet_ntop.m4
  m4/inet_pton.m4
  m4/intmax_t.m4
  m4/inttypes_h.m4
  m4/langinfo_h.m4
  m4/largefile.m4
  m4/lib-ld.m4
  m4/lib-link.m4
  m4/lib-prefix.m4
  m4/localcharset.m4
  m4/locale-fr.m4
  m4/locale-ja.m4
  m4/locale-zh.m4
  m4/locale_h.m4
  m4/localeconv.m4
  m4/lock.m4
  m4/longlong.m4
  m4/lseek.m4
  m4/lstat.m4
  m4/malloc.m4
  m4/math_h.m4
  m4/mbrtowc.m4
  m4/mbsinit.m4
  m4/mbstate_t.m4
  m4/mbtowc.m4
  m4/memchr.m4
  m4/mkdtemp.m4
  m4/mmap-anon.m4
  m4/msvc-inval.m4
  m4/msvc-nothrow.m4
  m4/multiarch.m4
  m4/netdb_h.m4
  m4/netinet_in_h.m4
  m4/nl_langinfo.m4
  m4/nocrash.m4
  m4/off_t.m4
  m4/pathmax.m4
  m4/physmem.m4
  m4/printf.m4
  m4/raise.m4
  m4/read.m4
  m4/regex.m4
  m4/safe-read.m4
  m4/safe-write.m4
  m4/secure_getenv.m4
  m4/servent.m4
  m4/signal_h.m4
  m4/size_max.m4
  m4/snprintf.m4
  m4/socketlib.m4
  m4/sockets.m4
  m4/socklen.m4
  m4/sockpfaf.m4
  m4/ssize_t.m4
  m4/stat.m4
  m4/stdalign.m4
  m4/stdbool.m4
  m4/stddef_h.m4
  m4/stdint.m4
  m4/stdint_h.m4
  m4/stdio_h.m4
  m4/stdlib_h.m4
  m4/string_h.m4
  m4/sys_socket_h.m4
  m4/sys_stat_h.m4
  m4/sys_time_h.m4
  m4/sys_types_h.m4
  m4/sys_uio_h.m4
  m4/tempname.m4
  m4/threadlib.m4
  m4/time_h.m4
  m4/unistd_h.m4
  m4/vasnprintf.m4
  m4/warn-on-use.m4
  m4/wchar_h.m4
  m4/wchar_t.m4
  m4/wcrtomb.m4
  m4/wctype_h.m4
  m4/wint_t.m4
  m4/write.m4
  m4/xsize.m4
])
