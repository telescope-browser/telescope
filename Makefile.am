SUBDIRS = test

bin_PROGRAMS =		telescope

EXTRA_telescope_SOURCES = compat/ohash.h compat/queue.h compat/imsg.h contrib

telescope_SOURCES =	cmd.c			\
			cmd.gen.c		\
			cmd.h			\
			compat.h		\
			compl.c			\
			compl.h			\
			control.c		\
			control.h		\
			defaults.c		\
			defaults.h		\
			downloads.c		\
			emoji-matcher.c		\
			fs.c			\
			fs.h			\
			gencmd.awk		\
			genemoji.sh		\
			help.c			\
			hist.c			\
			iri.c			\
			iri.h			\
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
			utils.c			\
			utils.h			\
			wrap.c

noinst_PROGRAMS =	pagebundler
pagebundler_SOURCES =	pagebundler.c

# Override implicit rule since we have to use HOSTCC and not CC.
pagebundler$(EXEEXT): pagebundler.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $(srcdir)/pagebundler.c

# ugly; builds bundled libgrapheme if needed
$(srcdir)/libgrapheme/libgrapheme.a:
	${MAKE} -C $(srcdir)/libgrapheme libgrapheme.a

clean-local:
	test -n "$(LIBGRAPHEME)" && ${MAKE} -C libgrapheme clean || true

BUILT_SOURCES =		cmd.gen.c emoji-matcher.c pages.c

CLEANFILES =		cmd.gen.c emoji-matcher.c pages.c parse.c

LDADD =			$(LIBOBJS) $(LIBGRAPHEME)
EXTRA_telescope_DEPENDENCIES = $(LIBGRAPHEME)

EXTRA_DIST =		ChangeLog LICENSE README.md data/emoji.txt pages/*.gmi

dist_man1_MANS =	telescope.1

cmd.gen.c: $(srcdir)/cmd.h $(srcdir)/gencmd.awk
	${AWK} -f $(srcdir)/gencmd.awk < $(srcdir)/cmd.h > $@

emoji-matcher.c: $(srcdir)/data/emoji.txt $(srcdir)/genemoji.sh
	$(srcdir)/genemoji.sh $(srcdir)/data/emoji.txt > $@

PAGES =	$(builddir)/pages/about_about.gmi	\
	$(builddir)/pages/about_blank.gmi	\
	$(builddir)/pages/about_crash.gmi	\
	$(builddir)/pages/about_help.gmi	\
	$(builddir)/pages/about_license.gmi	\
	$(builddir)/pages/about_new.gmi
pages.c: pagebundler $(srcdir)/pages.h ${PAGES}
	echo > $@
	echo "#include \"pages.h\"" >> $@
	./pagebundler $(builddir)/pages/about_about.gmi   >> $@
	./pagebundler $(builddir)/pages/about_blank.gmi   >> $@
	./pagebundler $(builddir)/pages/about_crash.gmi   >> $@
	./pagebundler $(builddir)/pages/about_help.gmi    >> $@
	./pagebundler $(builddir)/pages/about_license.gmi >> $@
	./pagebundler $(builddir)/pages/about_new.gmi     >> $@
	./pagebundler $(builddir)/pages/bookmarks.gmi     >> $@
