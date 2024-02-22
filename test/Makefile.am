check_PROGRAMS =	gmparser gmiparser iritest evtest

gmparser_SOURCES =	gmparser.c				\
			$(top_srcdir)/compat.h			\
			$(top_srcdir)/hist.c			\
			$(top_srcdir)/hist.h			\
			$(top_srcdir)/iri.c			\
			$(top_srcdir)/iri.h			\
			$(top_srcdir)/parser.c			\
			$(top_srcdir)/parser.h			\
			$(top_srcdir)/parser_gophermap.c

gmiparser_SOURCES =	gmiparser.c				\
			$(top_srcdir)/compat.h			\
			$(top_srcdir)/hist.c			\
			$(top_srcdir)/hist.h			\
			$(top_srcdir)/parser.c			\
			$(top_srcdir)/parser.h			\
			$(top_srcdir)/parser_gemtext.c

iritest_SOURCES =	iritest.c				\
			$(top_srcdir)/iri.c			\
			$(top_srcdir)/iri.h

evtest_SOURCES =	evtest.c				\
			$(top_srcdir)/ev.c			\
			$(top_srcdir)/ev.h

gmparser_CFLAGS =	-I$(top_srcdir)
gmiparser_CFLAGS =	-I$(top_srcdir)
iritest_CFLAGS =	-I$(top_srcdir)
evtest_CFLAGS =		-I$(top_srcdir)

LDADD =			$(LIBOBJS)

EXTRA_DIST =		test-gmparser	\
			gm-00		\
			gm-01		\
			gmi-00

CLEANFILES =		serialized.*

TESTS =	test-gmparser iritest evtest
