check_PROGRAMS =	gmparser gmiparser

gmparser_SOURCES =	gmparser.c				\
			$(top_srcdir)/include/compat.h		\
			$(top_srcdir)/include/parser.h		\
			$(top_srcdir)/parser/parser.c		\
			$(top_srcdir)/parser/parser_gophermap.c	\
			$(top_srcdir)/utils.c

gmiparser_SOURCES =	gmiparser.c				\
			$(top_srcdir)/include/compat.h		\
			$(top_srcdir)/include/parser.h		\
			$(top_srcdir)/parser/parser.c		\
			$(top_srcdir)/parser/parser_gemtext.c	\
			$(top_srcdir)/utils.c

gmparser_CFLAGS =	-I$(top_srcdir)/phos -I$(top_srcdir)/include
gmiparser_CFLAGS =	-I$(top_srcdir)/phos -I$(top_srcdir)/include

EXTRA_DIST =		test-gmparser	\
			gm-00		\
			gm-01		\
			gmi-00

CLEANFILES =		serialized.*

TESTS =	test-gmparser
