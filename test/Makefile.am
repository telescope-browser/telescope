check_PROGRAMS =	gmparser gmiparser iritest evtest mailcap

gmparser_SOURCES =	gmparser.c				\
			$(top_srcdir)/compat.h			\
			$(top_srcdir)/hist.c			\
			$(top_srcdir)/hist.h			\
			$(top_srcdir)/iri.c			\
			$(top_srcdir)/iri.h			\
			$(top_srcdir)/parser.c			\
			$(top_srcdir)/parser.h			\
			$(top_srcdir)/parser_gophermap.c 	\
			$(top_srcdir)/xwrapper.c 		\
			$(top_srcdir)/xwrapper.h

gmiparser_SOURCES =	gmiparser.c				\
			$(top_srcdir)/compat.h			\
			$(top_srcdir)/hist.c			\
			$(top_srcdir)/hist.h			\
			$(top_srcdir)/parser.c			\
			$(top_srcdir)/parser.h			\
			$(top_srcdir)/parser_gemtext.c 		\
			$(top_srcdir)/xwrapper.c 		\
			$(top_srcdir)/xwrapper.h

iritest_SOURCES =	iritest.c				\
			$(top_srcdir)/iri.c			\
			$(top_srcdir)/iri.h

evtest_SOURCES =	evtest.c				\
			$(top_srcdir)/ev.c			\
			$(top_srcdir)/ev.h			\
			$(top_srcdir)/xwrapper.c 		\
			$(top_srcdir)/xwrapper.h

mailcap_SOURCES =	$(top_srcdir)/test/mailcap.c		\
			$(top_srcdir)/mailcap.c			\
			$(top_srcdir)/mailcap.h			\
			$(top_srcdir)/xwrapper.c 		\
			$(top_srcdir)/xwrapper.h

AM_CFLAGS =		-I$(top_srcdir) -I$(top_srcdir)/compat

LDADD =			$(LIBOBJS)

EXTRA_DIST =		test-gmparser	\
			gm-00		\
			gm-01		\
			gmi-00		\
			mc.exp.empty	\
			mc.exp.many	\
			mc.exp.simple	\
			mc.test.empty	\
			mc.test.many	\
			mc.test.simple

CLEANFILES =		serialized.*

TESTS =	test-gmparser test-mailcap iritest evtest
