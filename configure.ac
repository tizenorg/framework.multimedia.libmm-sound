AC_PREREQ(2.52)

AC_INIT([libmm-sound], [1.0])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_HEADERS([config.h:config.hin])

AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
m4_pattern_allow([AM_PROG_AR])
AM_PROG_AR
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

PKG_CHECK_MODULES(GIO, gio-2.0)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(VCONF, vconf)
AC_SUBST(VCONF_CFLAGS)
AC_SUBST(VCONF_LIBS)

AC_ARG_ENABLE(bluetooth, AC_HELP_STRING([--enable-bluetooth], [enable bluetooth client]),
[
 case "${enableval}" in
	 yes) SUPPORT_BT_SCO=yes ;;
	 no)  SUPPORT_BT_SCO=no ;;
	 *)   AC_MSG_ERROR(bad value ${enableval} for --enable-bluetooth) ;;
 esac
 ],[SUPPORT_BT_SCO=no])
if test "x$SUPPORT_BT_SCO" = "xyes"; then
PKG_CHECK_MODULES(BLUETOOTH, capi-network-bluetooth)
AC_SUBST(BLUETOOTH_CFLAGS)
AC_SUBST(BLUETOOTH_LIBS)
fi
AM_CONDITIONAL([SUPPORT_BT_SCO], [test "x$SUPPORT_BT_SCO" = "xyes"])

AC_ARG_ENABLE(hfp, AC_HELP_STRING([--enable-hfp], [enable hfp api]),
[
 case "${enableval}" in
	 yes) USE_HFP=yes ;;
	 no)  USE_HFP=no ;;
	 *)   AC_MSG_ERROR(bad value ${enableval} for --enable-hfp) ;;
 esac
 ],[USE_HFP=no])
if test "x$USE_HFP" = "xyes"; then
PKG_CHECK_MODULES(BLUETOOTH_API, bluetooth-api)
AC_SUBST(BLUETOOTH_API_CFLAGS)
AC_SUBST(BLUETOOTH_API_LIBS)
fi

AC_ARG_ENABLE(pulse, AC_HELP_STRING([--enable-pulse], [enable pulseaudio client]),
[
 case "${enableval}" in
	 yes) USE_PULSE=yes ;;
	 no)  USE_PULSE=no ;;
	 *)   AC_MSG_ERROR(bad value ${enableval} for --enable-pulse) ;;
 esac
 ],[USE_PULSE=no])
if test "x$USE_PULSE" = "xyes"; then
PKG_CHECK_MODULES(PASIMPLE, libpulse-simple)
AC_SUBST(PASIMPLE_CFLAGS)
AC_SUBST(PASIMPLE_LIBS)
PKG_CHECK_MODULES(PA, libpulse)
AC_SUBST(PA_CFLAGS)
AC_SUBST(PA_LIBS)
fi
AM_CONDITIONAL([USE_PULSE], [test "x$USE_PULSE" = "xyes"])

AC_ARG_ENABLE(ogg, AC_HELP_STRING([--enable-ogg], [enable ogg client]),
[
 case "${enableval}" in
	 yes) OGG_SUPPORT=yes ;;
	 no)  OGG_SUPPORT=no ;;
	 *)   AC_MSG_ERROR(bad value ${enableval} for --enable-ogg) ;;
 esac
 ],[OGG_SUPPORT=no])
if test "x$OGG_SUPPORT" = "xyes"; then
PKG_CHECK_MODULES(TREMOLO, libtremolo)
AC_SUBST(TREMOLO_CFLAGS)
AC_SUBST(TREMOLO_LIBS)
fi
AM_CONDITIONAL([OGG_SUPPORT], [test "x$OGG_SUPPORT" = "xyes"])

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
server/plugin/ogg/Makefile
server/plugin/keytone/Makefile
server/plugin/tone/Makefile
pkgconfig/Makefile
pkgconfig/mm-sound.pc
pkgconfig/mm-keysound.pc
pkgconfig/mm-bootsound.pc
testsuite/Makefile
])
AC_OUTPUT
