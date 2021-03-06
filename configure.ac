AC_INIT([telescope], [0.1], [telescope@omarpolo.com], [telescope], [gemini://telescope.omarpolo.com])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_PROG_CC

PKG_PROG_PKG_CONFIG

AC_SEARCH_LIBS([initscr], [ncursesw ncurses], [], [
	AC_MSG_ERROR([requires either ncursesw or ncurses library])
])

AC_CHECK_LIB(tls, tls_init, [], [
	AC_MSG_ERROR([requires libtls])
])

AC_CHECK_LIB(event, event_init, [], [
	AC_MSG_ERROR([requires libevent])
])

IMSG_LDFLAGS=-lutil
AC_SUBST([IMSG_LDFLAGS])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
])

AC_OUTPUT
