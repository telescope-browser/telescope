AC_INIT([telescope], [0.1], [telescope@omarpolo.com], [telescope], [gemini://telescope.omarpolo.com])
AC_CONFIG_LIBOBJ_DIR(compat)
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])
AC_PROG_CC

PKG_PROG_PKG_CONFIG

AC_REPLACE_FUNCS([
	asprintf \
	err \
	freezero \
	getdtablecount \
	getdtablesize \
	getprogname \
	recallocarray \
	strlcat \
	strlcpy \
	strtonum \
	setproctitle \
])

AC_CHECK_DECL(PR_SET_NAME, AC_DEFINE([HAVE_PR_SET_NAME], 1, [pr_set_name]), [],
	[[#include <sys/prctl.h>]])

AC_SEARCH_LIBS([initscr], [ncursesw ncurses], [], [
	AC_MSG_ERROR([requires either ncursesw or ncurses library])
])

AC_CHECK_LIB(tls, tls_init, [], [
	AC_MSG_ERROR([requires libtls])
])

AC_CHECK_LIB(event, event_init, [], [
	AC_MSG_ERROR([requires libevent])
])

AC_CHECK_LIB(util, imsg_init, [], [
	AC_LIBOBJ(imsg)
	AC_LIBOBJ(imsg-buffer)
])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
])

AC_OUTPUT
