check_PROGRAMS =	gmparser

gmparser_SOURCES =	gmparser.c				\
			$(top_srcdir)/compat.h			\
			$(top_srcdir)/parser.c			\
			$(top_srcdir)/parser.h			\
			$(top_srcdir)/parser_gophermap.c	\
			$(top_srcdir)/utils.c

gmparser_CFLAGS =	-I$(top_srcdir)

EXTRA_DIST =		test-gmparser	\
			gm-00		\
			gm-01

CLEANFILES =		*.serialized

TESTS =	test-gmparser
