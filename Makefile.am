bin_PROGRAMS =		telescope

telescope_SOURCES =	cmd.c		\
			cmd.gen.c	\
			cmd.h		\
			compat.h	\
			compat/*.[ch]	\
			compl.c		\
			compl.h		\
			defaults.c	\
			defaults.h	\
			emoji-matcher.c	\
			fs.c		\
			gemini.c	\
			gemtext.c	\
			gen-emoji-matcher.sh \
			gencmd.awk	\
			hist.c		\
			keymap.c	\
			mime.c		\
			minibuffer.c	\
			minibuffer.h	\
			pages.c		\
			pages.h		\
			parse.y		\
			parser.c	\
			parser.h	\
			sandbox.c	\
			telescope.c	\
			telescope.h	\
			textplain.c	\
			tofu.c		\
			ui.c		\
			ui.h		\
			utf8.c		\
			utf8.h		\
			util.c		\
			wrap.c

# phos bundled files
telescope_SOURCES +=	phos/phos.h	\
			phos/phos_uri.c

BUILT_SOURCES =		cmd.gen.c compile_flags.txt emoji-matcher.c

CLEANFILES =		cmd.gen.c compile_flags.txt emoji-matcher.c

LDADD =			$(LIBOBJS)

EXTRA_DIST =		ChangeLog LICENSE README.md data/emoji.txt

dist_man1_MANS =	telescope.1

cmd.gen.c: $(srcdir)/cmd.h $(srcdir)/gencmd.awk
	${AWK} -f $(srcdir)/gencmd.awk < $(srcdir)/cmd.h > $@

emoji-matcher.c: $(srcdir)/data/emoji.txt $(srcdir)/gen-emoji-matcher.sh
	$(srcdir)/gen-emoji-matcher.sh > $@

compile_flags.txt:
	printf "%s\n" ${CFLAGS} > compile_flags.txt
