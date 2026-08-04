dnl configure.ac --- autoconf input file for systemtap testsuite
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 5.6, systemtap@sourceware.org, systemtap)

AC_PREREQ(2.59)
AC_CONFIG_AUX_DIR(..)
AM_INIT_AUTOMAKE([dejagnu foreign])
AM_MAINTAINER_MODE

# The configure above passed us several python-related variables. Make
# sure these get substituted in the Makefile.
AC_SUBST([python3], [${python3}])
AC_SUBST([py3execdir], [${py3execdir}])

AC_CONFIG_FILES(Makefile)
AC_OUTPUT
