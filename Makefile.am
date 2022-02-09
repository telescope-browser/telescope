SUBDIRS = test

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
			fs.c			\
			fs.h			\
			gencmd.awk		\
			help.c			\
			hist.c			\
			keymap.c		\
			keymap.h		\
			mcache.c		\
			mcache.h		\
			mime.c			\
			minibuffer.c		\
			minibuffer.h		\
			net.c			\
			pages.c			\
			pages.h			\
			parse.y			\
			parser.h		\
			parser/parser.c			\
			parser/parser_gemtext.c		\
			parser/parser_gophermap.c	\
			parser/parser_textpatch.c	\
			parser/parser_textplain.c	\
			sandbox.c		\
			session.c		\
			session.h		\
			telescope.c		\
			telescope.h		\
			tofu.c			\
			u/emoji-matcher.c	\
			u/genemoji.sh		\
			u/utf8.c		\
			u/wrap.c		\
			ui.c			\
			ui.h			\
			utf8.h			\
			utils.c			\
			utils.h	

# phos bundled files
telescope_SOURCES +=	phos/phos.h	\
			phos/phos_uri.c

noinst_PROGRAMS =	pagebundler
pagebundler_SOURCES =	pagebundler.c

pagebundler$(EXEEXT): pagebundler.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $(srcdir)/pagebundler.c

BUILT_SOURCES =		cmd.gen.c compile_flags.txt u/emoji-matcher.c pages.c

CLEANFILES =		cmd.gen.c compile_flags.txt u/emoji-matcher.c pages.c \
			parse.c

LDADD =			$(LIBOBJS)

EXTRA_DIST =		ChangeLog LICENSE README.md data/emoji.txt pages/*.gmi

dist_man1_MANS =	telescope.1

cmd.gen.c: $(srcdir)/cmd.h $(srcdir)/gencmd.awk
	${AWK} -f $(srcdir)/gencmd.awk < $(srcdir)/cmd.h > $@

u/emoji-matcher.c: $(srcdir)/data/emoji.txt $(srcdir)/u/genemoji.sh
	$(srcdir)/u/genemoji.sh $(srcdir)/data/emoji.txt > $@

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
	./pagebundler $(srcdir)/pages/about_about.gmi   >> $@
	./pagebundler $(srcdir)/pages/about_blank.gmi   >> $@
	./pagebundler $(srcdir)/pages/about_crash.gmi   >> $@
	./pagebundler $(srcdir)/pages/about_help.gmi    >> $@
	./pagebundler $(srcdir)/pages/about_license.gmi >> $@
	./pagebundler $(srcdir)/pages/about_new.gmi     >> $@
	./pagebundler $(srcdir)/pages/bookmarks.gmi     >> $@
