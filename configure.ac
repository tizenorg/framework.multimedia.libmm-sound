AC_PREREQ(2.52)

AC_INIT([avsystem], [1.0])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_HEADERS([config.h:config.hin])

AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
AC_PROG_CC
AC_C_CONST
AM_PROG_CC_C_O
dnl AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_STAT
AC_FUNC_VPRINTF
AC_HEADER_STDBOOL
AC_HEADER_STDC
AC_HEADER_TIME
AC_PROG_GCC_TRADITIONAL
AC_PROG_LIBTOOL

# Checks for libraries.
# systemd - libraries
PKG_CHECK_MODULES(SYSTEMD_DAEMON, libsystemd-daemon)
AC_SUBST(SYSTEMD_DAEMON_CFLAGS)
AC_SUBST(SYSTEMD_DAEMON_LIBS)

PKG_CHECK_MODULES(AVSYSTEM, avsysaudio)
AC_SUBST(AVSYSTEM_CFLAGS)
AC_SUBST(AVSYSTEM_LIBS)

PKG_CHECK_MODULES(MMCOMMON, mm-common)
AC_SUBST(MMCOMMON_CFLAGS)
AC_SUBST(MMCOMMON_LIBS)

if [test "x$IS_SDK" = "xyes"];
then
echo "SDK build does not use mm-log"
else
PKG_CHECK_MODULES(MMLOG, mm-log)
AC_SUBST(MMLOG_CFLAGS)
AC_SUBST(MMLOG_LIBS)
fi

PKG_CHECK_MODULES(MMSESSION, mm-session)
AC_SUBST(MMSESSION_CFLAGS)
AC_SUBST(MMSESSION_LIBS)

PKG_CHECK_MODULES(AUDIOSESSIONMGR, audio-session-mgr)
AC_SUBST(AUDIOSESSIONMGR_CFLAGS)
AC_SUBST(AUDIOSESSIONMGR_LIBS)

PKG_CHECK_MODULES(GLIB2, glib-2.0 gthread-2.0)
AC_SUBST(GLIB2_CFLAGS)
AC_SUBST(GLIB2_LIBS)

PKG_CHECK_MODULES(VCONF, vconf)
AC_SUBST(VCONF_CFLAGS)
AC_SUBST(VCONF_LIBS)

PKG_CHECK_MODULES(HEYNOTI, heynoti)
AC_SUBST(HEYNOTI_CFLAGS)
AC_SUBST(HEYNOTI_LIBS)

AC_ARG_ENABLE(pulse, AC_HELP_STRING([--enable-pulse], [enable pulseaudio client]),
[
 case "${enableval}" in
	 yes) USE_PULSE=yes ;;
	 no)  USE_PULSE=no ;;
	 *)   AC_MSG_ERROR(bad value ${enableval} for --enable-pulse) ;;
 esac
 ],[USE_PULSE=no])
if test "x$USE_PULSE" = "xyes"; then
PKG_CHECK_MODULES(PULSE, libpulse)
AC_SUBST(PULSE_CFLAGS)
AC_SUBST(PULSE_LIBS)
fi
AM_CONDITIONAL([USE_PULSE], [test "x$USE_PULSE" = "xyes"])

dnl use security --------------------------------------------------------------------------
AC_ARG_ENABLE(security, AC_HELP_STRING([--enable-security], [using security]),
[
 case "${enableval}" in
    yes) USE_SECURITY=yes ;;
     no)  USE_SECURITY=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --enable-security) ;;
 esac
 ],[USE_SECURITY=no])

if test "x$USE_SECURITY" = "xyes"; then
PKG_CHECK_MODULES(SECURITY, security-server)
AC_SUBST(SECURITY_CFLAGS)
AC_SUBST(SECURITY_LIBS)
fi
AM_CONDITIONAL(USE_SECURITY, test "x$USE_SECURITY" = "xyes")
dnl end --------------------------------------------------------------------


# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h memory.h stdlib.h string.h sys/time.h unistd.h errno.h sys/types.h sys/stat.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([memset select])
AC_CONFIG_FILES([
common/Makefile
Makefile
server/Makefile
server/plugin/Makefile
server/plugin/wav/Makefile
server/plugin/keytone/Makefile
server/plugin/tone/Makefile
pkgconfig/Makefile
pkgconfig/mm-sound.pc
pkgconfig/mm-keysound.pc
testsuite/Makefile
init/Makefile
])
AC_OUTPUT
