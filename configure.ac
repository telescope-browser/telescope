AC_INIT([telescope], [0.10-current], [telescope@omarpolo.com],
    [telescope], [gemini://telescope.omarpolo.com])
AC_CONFIG_AUX_DIR(etc)
AC_CONFIG_LIBOBJ_DIR(compat)
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_YACC

# AC_PROC_YACC sets the environment variable $YACC for a suitable program to
# use, but doesn't check to see if it exists.  Test for this -- if it's not
# present, error out.
AC_CHECK_PROG(found_yacc, $YACC, yes, no)
AS_IF([test "x$found_yacc" = "xno"], [AC_MSG_ERROR("yacc/bison not found")])

PKG_PROG_PKG_CONFIG

AC_ARG_VAR(HOSTCC, [The C compiler on the host.])
AC_ARG_VAR(HOSTCFLAGS, [CFLAGS for the host compiler])

test -z "${HOSTCC}"     && HOSTCC='$(CC)'
test -z "${HOSTCFLAGS}" && HOSTCFLAGS='$(CFLAGS)'

AC_MSG_CHECKING([platform])
case "$host_os" in
	*darwin*)
		PLATFORM=darwin
		;;
	*linux*)
		PLATFORM=linux
		;;
	*freebsd*)
		PLATFORM=freebsd
		;;
	*openbsd*)
		PLATFORM=openbsd
		;;
	*)
		PLATFORM=unknown
		;;
esac
AC_MSG_RESULT($PLATFORM)
AC_SUBST(PLATFORM)

AC_ARG_WITH([Werror],
	AS_HELP_STRING([--with-Werror],
		[Add the compiler flag -Werror]))

AC_ARG_WITH([libimsg],
	AS_HELP_STRING([--with-libimsg],
		[Build with imsg-compat library (default: disabled)]))

AC_ARG_WITH([libbsd],
	AS_HELP_STRING([--with-libbsd],
		[Build with libbsd library (default: disabled)]))

AC_ARG_WITH([default-editor],
	AS_HELP_STRING([--with-default-editor],
		[Set the default editor to use (default: ed)]),
		[DEFAULT_EDITOR=$withval],
		[DEFAULT_EDITOR=ed])
AC_MSG_NOTICE([Using default-editor: $DEFAULT_EDITOR])
AC_DEFINE_UNQUOTED([DEFAULT_EDITOR], ["$DEFAULT_EDITOR"], [The default editor])

DEFAULT_OPENER=xdg-open
AS_IF([test "x$platform" = "xdarwin"], [
	DEFAULT_OPENER=open
])

AC_ARG_WITH([default-opener],
	AS_HELP_STRING([--with-default-opener],
		[Set the default program to use (default: $DEFAULT_OPENER)]),
		[DEFAULT_OPENER=$withval])
AC_MSG_NOTICE([Using default-opener: $DEFAULT_OPENER])
AC_DEFINE_UNQUOTED([DEFAULT_OPENER], ["$DEFAULT_OPENER"], [The default opener])

AC_CHECK_HEADERS([ \
	libutil.h \
	util.h \
])

AS_IF([test "x$with_libbsd" = "xyes"], [
	PKG_CHECK_MODULES([libbsd], [libbsd-overlay libbsd-ctor], [
		CFLAGS="$libbsd_CFLAGS $CFLAGS"
		LIBS="$libbsd_LIBS $LIBS"
	])
])

if test "x$PLATFORM" = "xdarwin"; then
	export LDFLAGS="-L/opt/homebrew/opt/libressl/lib $LDFLAGS"
	export CPPFLAGS="-I/opt/homebrew/opt/libressl/include $CPPFLAGS"
	export PKG_CONFIG_PATH="/opt/homebrew/opt/libressl/lib/pkgconfig $PKG_CONFIG_PATH"
fi

AC_REPLACE_FUNCS([
	asprintf \
	err \
	explicit_bzero \
	fgetln \
	fparseln \
	freezero \
	getdtablecount \
	getdtablesize \
	getprogname \
	memmem \
	reallocarray \
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

AC_CHECK_DECL(timespecsub,
    [AC_DEFINE([HAVE_TIMESPECSUB], 1, [have timespecsub])], [],
    [#include <sys/time.h>])

AC_CHECK_DECL(timercmp,
    [AC_DEFINE([HAVE_TIMERCMP], 1, [have timercmp])], [],
    [#include <sys/time.h>])

dnl can't be used as r-value.
AC_MSG_CHECKING([for TIMEVAL_TO_TIMESPEC])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#include <sys/time.h>
], [
	struct timeval a = {0, 0};
	struct timespec b;
	TIMEVAL_TO_TIMESPEC(&a, &b);
	return (int)b.tv_sec;
])], [
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_TIMEVAL_TO_TIMESPEC], 1, [have TIMEVAL_TO_TIMESPEC])
], AC_MSG_RESULT(no))

AC_CHECK_HEADER([endian.h], [AC_DEFINE(HAVE_ENDIAN_H, 1, [have endian.h])], [
	AC_CHECK_HEADER([sys/endian.h],
		[AC_DEFINE(HAVE_SYS_ENDIAN_H, 1, [have sys/endian.h])], [
		AC_CHECK_HEADERS([libkern/OSByteOrder.h],
			[AC_DEFINE(HAVE_LIBKERN_OSBYTEORDER_H, 1, [have OSByteOrder.h])],
			[AC_MSG_ERROR([can't find compatible endian.h header])],
			[#include <machine/endian.h>])
	])
])

AC_CHECK_DECL(PR_SET_NAME, AC_DEFINE([HAVE_PR_SET_NAME], 1, [pr_set_name]), [],
	[[#include <sys/prctl.h>]])

AC_SEARCH_LIBS([initscr], [ncursesw ncurses], [], [
	AC_MSG_ERROR([requires either ncursesw or ncurses library])
])

AC_SEARCH_LIBS([keyname], [ncursesw ncurses tinfow tinfo], [], [
	AC_MSG_ERROR([requires either ncursesw or ncurses library with terminfo support])
])

AC_SEARCH_LIBS([tls_init], [tls], [found_tls=yes], [found_tls=no])

AS_IF([test "x$found_tls" = "xno"], [
	PKG_CHECK_MODULES([libtls], [libtls], [
		CFLAGS="$libtls_CFLAGS $CFLAGS"
		LIBS="$libtls_LIBS $LIBS"
	], [AC_MSG_ERROR([libtls not found.])])
])

PKG_CHECK_MODULES([libgrapheme], [libgrapheme], [
	AC_SUBST([LIBGRAPHEME], [])
	CFLAGS="$libgrapheme_CFLAGS $CFLAGS"
	LIBS="$libgrapheme_LIBS $LIBS"
], [
	# build the bundled version.
	AC_SUBST([LIBGRAPHEME], "$srcdir/libgrapheme/libgrapheme.a")
	CFLAGS="-I$srcdir/libgrapheme $CFLAGS"
])

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

AC_SEARCH_LIBS([ohash_init], [util], [], [
	AC_LIBOBJ(ohash)
	AC_LIBOBJ(fmt_scaled)
])

AC_CHECK_FUNCS([asr_run])

AC_SEARCH_LIBS([RAND_add], [crypto], [:], [
	AC_MSG_ERROR([can't find libcrypto])
])

AS_IF([test "x$PLATFORM" != "xdarwin"], [
	AC_SEARCH_LIBS([SSL_CTX_new], [ssl], [:], [
		AC_MSG_ERROR([can't find libssl])
	])
])

# Sanity check for Libre/OpenSSL headers vs library mismatch
AC_MSG_CHECKING([whether libcrypto headers match the library])
AC_RUN_IFELSE([AC_LANG_PROGRAM([[
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
]], [[
	return (OpenSSL_version_num() != OPENSSL_VERSION_NUMBER);
]])], [
	AC_MSG_RESULT([yes])
], [
	AC_MSG_RESULT([no])
	AC_MSG_ERROR([libcrypto headers do not match the library.])
], [
	AC_MSG_WARN([skip (due to cross-compiling)])
])

AC_CHECK_HEADERS([linux/landlock.h])

dnl after all the function checks, add optional support for -Werror
AS_IF([test "x$with_Werror" = "xyes"], [
	CFLAGS="$CFLAGS -Werror"
])

# check compiler flags
AC_DEFUN([CC_ADD_CHECK_FLAGS], [
	AC_MSG_CHECKING([if $CC supports $1 flag])
	old_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -Werror $1"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[])], [
		AC_MSG_RESULT(yes)
		CFLAGS="$old_CFLAGS $1"
	], [
		AC_MSG_RESULT(no)
		CFLAGS="$old_CFLAGS"
	])
])
CC_ADD_CHECK_FLAGS([-Wall])
CC_ADD_CHECK_FLAGS([-Wextra])
CC_ADD_CHECK_FLAGS([-Wmissing-declarations])
CC_ADD_CHECK_FLAGS([-Wmissing-prototypes])
CC_ADD_CHECK_FLAGS([-Wstrict-prototypes])
CC_ADD_CHECK_FLAGS([-Wwrite-strings])
CC_ADD_CHECK_FLAGS([-Wno-unused-parameter])
CC_ADD_CHECK_FLAGS([-Wno-format-truncation])
CC_ADD_CHECK_FLAGS([-Wno-pointer-sign])
CC_ADD_CHECK_FLAGS([-Wno-unused-result])
CC_ADD_CHECK_FLAGS([-Wpointer-arith])
CC_ADD_CHECK_FLAGS([-Wsign-compare])
CC_ADD_CHECK_FLAGS([-Wcast-align])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
	test/Makefile
	pages/about_about.gmi
	pages/about_blank.gmi
	pages/about_crash.gmi
	pages/about_help.gmi
	pages/about_license.gmi
	pages/about_new.gmi
	pages/bookmarks.gmi
])

AC_OUTPUT
