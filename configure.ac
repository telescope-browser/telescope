AC_INIT([telescope], [0.4], [telescope@omarpolo.com], [telescope], [gemini://telescope.omarpolo.com])
AC_CONFIG_LIBOBJ_DIR(compat)
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])
AC_PROG_CC_C99
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_YACC

PKG_PROG_PKG_CONFIG

AC_REPLACE_FUNCS([
	asprintf \
	err \
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

AC_CHECK_LIB(tls, tls_init, [], [
	AC_MSG_ERROR([requires libtls])
])

AC_CHECK_LIB(event, event_init, [], [
	AC_MSG_ERROR([requires libevent])
])

AC_CHECK_LIB(util, imsg_init, [], [
	AC_LIBOBJ(imsg)
	AC_LIBOBJ(imsg-buffer)
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
