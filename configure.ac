AC_INIT([telescope], [0.1], [telescope@omarpolo.com], [telescope], [gemini://telescope.omarpolo.com])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_PROG_CC

PKG_PROG_PKG_CONFIG

dnl AX_WITH_CURSES
dnl if test "x$ax_cv_ncursesw" != xyes && test "x$ax_cv_ncurses" != xyes; then
dnl 	AC_MSG_ERROR([requires either NcursesW or Ncurses library])
dnl fi

AC_CHECK_LIB(ncursesw, initscr, [],
	AC_CHECK_LIB(ncurses, initscr, [],
		AC_MSG_ERROR([requires either ncursesw or ncurses library])))

AC_CHECK_LIB(tls, tls_init, [],
	AC_MSG_ERROR([requires libtls]))

AC_CHECK_LIB(event, event_init, [],
	AC_MSG_ERROR([requires libevent]))

IMSG_LDFLAGS=-lutil
AC_SUBST([IMSG_LDFLAGS])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
])

AC_OUTPUT
