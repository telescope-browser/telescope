bin_PROGRAMS =		telescope

EXTRA_telescope_SOURCES = compat/ohash.h compat/queue.h compat/imsg.h contrib

telescope_SOURCES =	cmd.c			\
			cmd.gen.c		\
			cmd.h			\
			compat.h		\
			compl.c			\
			compl.h			\
			defaults.c		\
			defaults.h		\
			downloads.c		\
			emoji-matcher.c		\
			fs.c			\
			gen-emoji-matcher.sh	\
			gencmd.awk		\
			help.c			\
			hist.c			\
			keymap.c		\
			keymap.h		\
			mime.c			\
			minibuffer.c		\
			minibuffer.h		\
			net.c			\
			pages.c			\
			pages.h			\
			parse.y			\
			parser.c		\
			parser.h		\
			parser_gemtext.c	\
			parser_gophermap.c	\
			parser_textpatch.c	\
			parser_textplain.c	\
			sandbox.c		\
			session.c		\
			session.h		\
			telescope.c		\
			telescope.h		\
			tofu.c			\
			ui.c			\
			ui.h			\
			utf8.c			\
			utf8.h			\
			util.c			\
			wrap.c

# phos bundled files
telescope_SOURCES +=	phos/phos.h	\
			phos/phos_uri.c

noinst_PROGRAMS =	pagebundler
pagebundler_SOURCES =	pagebundler.c

pagebundler$(EXEEXT): pagebundler.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ pagebundler.c

BUILT_SOURCES =		cmd.gen.c compile_flags.txt emoji-matcher.c pages.c

CLEANFILES =		cmd.gen.c compile_flags.txt emoji-matcher.c pages.c \
			parse.c

LDADD =			$(LIBOBJS)

EXTRA_DIST =		ChangeLog LICENSE README.md data/emoji.txt pages/*.gmi

dist_man1_MANS =	telescope.1

cmd.gen.c: $(srcdir)/cmd.h $(srcdir)/gencmd.awk
	${AWK} -f $(srcdir)/gencmd.awk < $(srcdir)/cmd.h > $@

emoji-matcher.c: $(srcdir)/data/emoji.txt $(srcdir)/gen-emoji-matcher.sh
	$(srcdir)/gen-emoji-matcher.sh $(srcdir)/data/emoji.txt > $@

compile_flags.txt:
	printf "%s\n" ${CFLAGS} > compile_flags.txt

PAGES =	$(srcdir)/pages/about_about.gmi		\
	$(srcdir)/pages/about_blank.gmi		\
	$(srcdir)/pages/about_crash.gmi		\
	$(srcdir)/pages/about_help.gmi		\
	$(srcdir)/pages/about_license.gmi	\
	$(srcdir)/pages/about_new.gmi
pages.c: pagebundler $(srcdir)/pages.h ${PAGES}
	echo > $@
	echo "#include \"pages.h\"" >> $@
	./pagebundler -f $(srcdir)/pages/about_about.gmi   -v about_about   >> $@
	./pagebundler -f $(srcdir)/pages/about_blank.gmi   -v about_blank   >> $@
	./pagebundler -f $(srcdir)/pages/about_crash.gmi   -v about_crash   >> $@
	./pagebundler -f $(srcdir)/pages/about_help.gmi    -v about_help    >> $@
	./pagebundler -f $(srcdir)/pages/about_license.gmi -v about_license >> $@
	./pagebundler -f $(srcdir)/pages/about_new.gmi     -v about_new     >> $@
	./pagebundler -f $(srcdir)/pages/bookmarks.gmi     -v bookmarks     >> $@
