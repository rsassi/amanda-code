# onceonly.m4 serial 6
dnl Copyright (C) 2002-2003, 2005-2006, 2008 Free Software Foundation, Inc.
dnl This file is free software, distributed under the terms of the GNU
dnl General Public License.  As a special exception to the GNU General
dnl Public License, this file may be distributed as part of a program
dnl that contains a configuration script generated by Autoconf, under
dnl the same distribution terms as the rest of that program.

dnl This file defines some "once only" variants of standard autoconf macros.
dnl   AC_CHECK_HEADERS_ONCE          like  AC_CHECK_HEADERS
dnl   AC_CHECK_FUNCS_ONCE            like  AC_CHECK_FUNCS
dnl   AC_CHECK_DECLS_ONCE            like  AC_CHECK_DECLS
dnl   AC_REQUIRE([AC_FUNC_STRCOLL])  like  AC_FUNC_STRCOLL
dnl The advantage is that the check for each of the headers/functions/decls
dnl will be put only once into the 'configure' file. It keeps the size of
dnl the 'configure' file down, and avoids redundant output when 'configure'
dnl is run.
dnl The drawback is that the checks cannot be conditionalized. If you write
dnl   if some_condition; then gl_CHECK_HEADERS(stdlib.h); fi
dnl inside an AC_DEFUNed function, the gl_CHECK_HEADERS macro call expands to
dnl empty, and the check will be inserted before the body of the AC_DEFUNed
dnl function.

dnl The original code implemented AC_CHECK_HEADERS_ONCE and AC_CHECK_FUNCS_ONCE
dnl in terms of AC_DEFUN and AC_REQUIRE. This implementation uses diversions to
dnl named sections DEFAULTS and INIT_PREPARE in order to check all requested
dnl headers at once, thus reducing the size of 'configure'. It is known to work
dnl with autoconf 2.57..2.62 at least . The size reduction is ca. 9%.

dnl Autoconf version 2.59 plus gnulib is required; this file is not needed
dnl with Autoconf 2.60 or greater. But note that autoconf's implementation of
dnl AC_CHECK_DECLS_ONCE expects a comma-separated list of symbols as first
dnl argument!
AC_PREREQ([2.59])

# AC_CHECK_HEADERS_ONCE(HEADER1 HEADER2 ...) is a once-only variant of
# AC_CHECK_HEADERS(HEADER1 HEADER2 ...).
AC_DEFUN([AC_CHECK_HEADERS_ONCE], [
  :
  m4_foreach_w([gl_HEADER_NAME], [$1], [
    AC_DEFUN([gl_CHECK_HEADER_]m4_quote(translit(gl_HEADER_NAME,
                                                 [./-], [___])), [
      m4_divert_text([INIT_PREPARE],
        [gl_header_list="$gl_header_list gl_HEADER_NAME"])
      gl_HEADERS_EXPANSION
      AH_TEMPLATE(AS_TR_CPP([HAVE_]m4_defn([gl_HEADER_NAME])),
        [Define to 1 if you have the <]m4_defn([gl_HEADER_NAME])[> header file.])
    ])
    AC_REQUIRE([gl_CHECK_HEADER_]m4_quote(translit(gl_HEADER_NAME,
                                                   [./-], [___])))
  ])
])
m4_define([gl_HEADERS_EXPANSION], [
  m4_divert_text([DEFAULTS], [gl_header_list=])
  AC_CHECK_HEADERS([$gl_header_list])
  m4_define([gl_HEADERS_EXPANSION], [])
])

# AC_CHECK_FUNCS_ONCE(FUNC1 FUNC2 ...) is a once-only variant of
# AC_CHECK_FUNCS(FUNC1 FUNC2 ...).
AC_DEFUN([AC_CHECK_FUNCS_ONCE], [
  :
  m4_foreach_w([gl_FUNC_NAME], [$1], [
    AC_DEFUN([gl_CHECK_FUNC_]m4_defn([gl_FUNC_NAME]), [
      m4_divert_text([INIT_PREPARE],
        [gl_func_list="$gl_func_list gl_FUNC_NAME"])
      gl_FUNCS_EXPANSION
      AH_TEMPLATE(AS_TR_CPP([HAVE_]m4_defn([gl_FUNC_NAME])),
        [Define to 1 if you have the `]m4_defn([gl_FUNC_NAME])[' function.])
    ])
    AC_REQUIRE([gl_CHECK_FUNC_]m4_defn([gl_FUNC_NAME]))
  ])
])
m4_define([gl_FUNCS_EXPANSION], [
  m4_divert_text([DEFAULTS], [gl_func_list=])
  AC_CHECK_FUNCS([$gl_func_list])
  m4_define([gl_FUNCS_EXPANSION], [])
])

# AC_CHECK_DECLS_ONCE(DECL1 DECL2 ...) is a once-only variant of
# AC_CHECK_DECLS(DECL1, DECL2, ...).
AC_DEFUN([AC_CHECK_DECLS_ONCE], [
  :
  m4_foreach_w([gl_DECL_NAME], [$1], [
    AC_DEFUN([gl_CHECK_DECL_]m4_defn([gl_DECL_NAME]), [
      AC_CHECK_DECLS(m4_defn([gl_DECL_NAME]))
    ])
    AC_REQUIRE([gl_CHECK_DECL_]m4_defn([gl_DECL_NAME]))
  ])
])
