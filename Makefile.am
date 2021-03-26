bin_PROGRAMS =		telescope

telescope_SOURCES =	cmd.gen.h	\
			compat.h	\
			compat/*.[ch]	\
			fs.c		\
			gemini.c	\
			gemtext.c	\
			gencmd.awk	\
			hash.c		\
			hist.c		\
			keymap.c	\
			mime.c		\
			pages.c		\
			parser.c	\
			sandbox.c	\
			telescope.c	\
			telescope.h	\
			textplain.c	\
			ui.c		\
			url.c		\
			url.h		\
			utf8.c		\
			util.c		\
			wrap.c

CLEANFILES =		cmd.gen.h

LDADD = $(LIBOBJS)

dist_doc_DATA =		README.md LICENSE

cmd.gen.h: ui.c gencmd.awk
	${AWK} -F '[()]' -f gencmd.awk < ui.c > $@
