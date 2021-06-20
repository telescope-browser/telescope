bin_PROGRAMS =		telescope

telescope_SOURCES =	cmd.c		\
			cmd.h		\
			cmd.gen.h	\
			compat.h	\
			compat/*.[ch]	\
			defaults.c	\
			fs.c		\
			gemini.c	\
			gemtext.c	\
			gencmd.awk	\
			hist.c		\
			keymap.c	\
			mime.c		\
			pages.c		\
			parse.y		\
			parser.c	\
			sandbox.c	\
			telescope.c	\
			telescope.h	\
			textplain.c	\
			tofu.c		\
			ui.c		\
			utf8.c		\
			util.c		\
			wrap.c

# phos bundled files
telescope_SOURCES +=	phos/phos.h	\
			phos/phos_uri.c

BUILT_SOURCES =		cmd.gen.h compile_flags.txt

CLEANFILES =		cmd.gen.h compile_flags.txt

LDADD =			$(LIBOBJS)

dist_doc_DATA =		README.md LICENSE

dist_man1_MANS =	telescope.1

cmd.gen.h: $(srcdir)/cmd.h $(srcdir)/gencmd.awk
	${AWK} -f $(srcdir)/gencmd.awk < $(srcdir)/cmd.h > $@

compile_flags.txt:
	printf "%s\n" ${CFLAGS} > compile_flags.txt
