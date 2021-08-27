AC_INIT([telescope], [0.5], [telescope@omarpolo.com], [telescope], [gemini://telescope.omarpolo.com])
AC_CONFIG_LIBOBJ_DIR(compat)
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])
AC_PROG_CC_C99
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_YACC

PKG_PROG_PKG_CONFIG

AC_ARG_WITH([libimsg],
	AS_HELP_STRING([--with-libimsg],
		[Build with imsg-compat library (default: disabled)]))

AC_ARG_WITH([libbsd],
	AS_HELP_STRING([--with-libbsd],
		[Build with libbsd library (default: disabled)]))

AS_IF([test "x$with_libbsd" = "xyes"], [
	PKG_CHECK_MODULES([libbsd], [libbsd-overlay libbsd-ctor], [
		CFLAGS="$libbsd_CFLAGS $CFLAGS"
		LIBS="$libbsd_LIBS $LIBS"
	])
])

AC_REPLACE_FUNCS([
	asprintf \
	err \
	explicit_bzero \
	freezero \
	getdtablecount \
	getdtablesize \
	getprogname \
	memmem \
	recallocarray \
	strcasestr \
	strlcat \
	strlcpy \
	strsep \
	strtonum \
	setproctitle \
])

AC_MSG_CHECKING([for sys/queue.h with TAILQ_FOREACH_SAFE])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#include <sys/queue.h>
#include <stddef.h>
], [
	TAILQ_HEAD(tailhead, entry) head;
	struct entry {
		TAILQ_ENTRY(entry) entries;
	} *np, *nt;
	TAILQ_INIT(&head);
	TAILQ_FOREACH_SAFE(np, &head, entries, nt) {
		/* nop */ ;
	}
	return 0;
])], [
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_QUEUE_H], 1, [QUEUE_H])
], AC_MSG_RESULT(no))

AC_CHECK_DECL(PR_SET_NAME, AC_DEFINE([HAVE_PR_SET_NAME], 1, [pr_set_name]), [],
	[[#include <sys/prctl.h>]])

AC_SEARCH_LIBS([initscr], [ncursesw ncurses], [], [
	AC_MSG_ERROR([requires either ncursesw or ncurses library])
])

AC_SEARCH_LIBS([keyname], [ncursesw ncurses tinfow tinfo], [], [
	AC_MSG_ERROR([requires either ncursesw or ncurses library with terminfo support])
])

AC_CHECK_LIB(tls, tls_init, [], [
	AC_MSG_ERROR([requires libtls])
])

case "$host_os" in
	*openbsd*)
		AC_CHECK_LIB([event], [event_init], [],
			[AC_MSG_ERROR([requires libevent])])
		;;
	*)
		PKG_CHECK_MODULES([libevent2], [libevent_core >= 2], [
			AC_DEFINE([HAVE_EVENT2], 1, [1 if using event2])
			CFLAGS="$libevent2_CFLAGS $CFLAGS"
			LIBS="$libevent2_LIBS $LIBS"
		], [AC_MSG_ERROR([requires libevent])])
		;;
esac

AS_IF([test "x$with_libimsg" = "xyes"], [
	PKG_CHECK_MODULES([libimsg], [libimsg], [
		CFLAGS="$libimsg_CFLAGS $CFLAGS"
		LIBS="$libimsg_LIBS $LIBS"
	])
])

AC_SEARCH_LIBS([imsg_init], [util], [
		AC_DEFINE([HAVE_IMSG], 1,
			[Define to 1 if you have imsg functions (-lutil or -limsg).])
	], [
		AC_LIBOBJ(imsg)
		AC_LIBOBJ(imsg-buffer)
	]
)

AC_CHECK_LIB(util, ohash_init, [], [
	AC_LIBOBJ(ohash)
	AC_LIBOBJ(fmt_scaled)
])

AC_CHECK_FUNCS([asr_run])

# check compiler flags
AC_DEFUN([CC_ADD_CHECK_FLAGS], [
	AC_MSG_CHECKING([if $CC supports $1 flag])
	old_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS $1"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[])],
		AC_MSG_RESULT(yes),
		AC_MSG_RESULT(no)
		CFLAGS="$old_CFLAGS")
])
CC_ADD_CHECK_FLAGS([-Wall])
CC_ADD_CHECK_FLAGS([-Wextra])
CC_ADD_CHECK_FLAGS([-Wmissing-prototypes])
CC_ADD_CHECK_FLAGS([-Wstrict-prototypes])
CC_ADD_CHECK_FLAGS([-Wwrite-strings])
CC_ADD_CHECK_FLAGS([-Wno-unused-parameter])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
	pages/about_new.gmi
	pages/about_crash.gmi
])

AC_OUTPUT
