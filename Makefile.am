SUBDIRS = test

bin_PROGRAMS =		telescope

EXTRA_telescope_SOURCES = compat/ohash.h compat/queue.h compat/imsg.h contrib

telescope_SOURCES =	cmd.c			\
			cmd.gen.c		\
			compl.c			\
			control.c		\
			defaults.c		\
			downloads.c		\
			fs.c			\
			gencmd.awk		\
			help.c			\
			hist.c			\
			include/cmd.h		\
			include/compat.h	\
			include/compl.h		\
			include/control.h	\
			include/defaults.h	\
			include/fs.h		\
			include/keymap.h	\
			include/mcache.h	\
			include/minibuffer.h	\
			include/pages.h		\
			include/parser.h	\
			include/session.h	\
			include/telescope.h	\
			include/ui.h		\
			include/utf8.h		\
			include/utils.h		\
			iri.c			\
			iri.h			\
			keymap.c		\
			mcache.c		\
			mime.c			\
			minibuffer.c		\
			net.c			\
			pages.c			\
			parse.y			\
			parser/parser.c			\
			parser/parser_gemtext.c		\
			parser/parser_gophermap.c	\
			parser/parser_textpatch.c	\
			parser/parser_textplain.c	\
			sandbox.c		\
			session.c		\
			telescope.c		\
			tofu.c			\
			u/emoji-matcher.c	\
			u/genemoji.sh		\
			u/utf8.c		\
			u/wrap.c		\
			ui.c			\
			utils.c

# phos bundled files
telescope_SOURCES +=	phos/phos.h	\
			phos/phos_uri.c

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

BUILT_SOURCES =		cmd.gen.c u/emoji-matcher.c pages.c

CLEANFILES =		cmd.gen.c u/emoji-matcher.c pages.c \
			parse.c

AM_CPPFLAGS =		-I$(top_srcdir)/phos -I$(top_srcdir)/include
LDADD =			$(LIBOBJS) $(LIBGRAPHEME)
EXTRA_telescope_DEPENDENCIES = $(LIBGRAPHEME)

EXTRA_DIST =		ChangeLog LICENSE README.md data/emoji.txt pages/*.gmi

dist_man1_MANS =	telescope.1

cmd.gen.c: $(srcdir)/include/cmd.h $(srcdir)/gencmd.awk
	${AWK} -f $(srcdir)/gencmd.awk < $(srcdir)/include/cmd.h > $@

u/emoji-matcher.c: $(srcdir)/data/emoji.txt $(srcdir)/u/genemoji.sh
	$(srcdir)/u/genemoji.sh $(srcdir)/data/emoji.txt > $@

PAGES =	$(builddir)/pages/about_about.gmi	\
	$(builddir)/pages/about_blank.gmi	\
	$(builddir)/pages/about_crash.gmi	\
	$(builddir)/pages/about_help.gmi	\
	$(builddir)/pages/about_license.gmi	\
	$(builddir)/pages/about_new.gmi
pages.c: pagebundler $(srcdir)/include/pages.h ${PAGES}
	echo > $@
	echo "#include \"include/pages.h\"" >> $@
	./pagebundler $(builddir)/pages/about_about.gmi   >> $@
	./pagebundler $(builddir)/pages/about_blank.gmi   >> $@
	./pagebundler $(builddir)/pages/about_crash.gmi   >> $@
	./pagebundler $(builddir)/pages/about_help.gmi    >> $@
	./pagebundler $(builddir)/pages/about_license.gmi >> $@
	./pagebundler $(builddir)/pages/about_new.gmi     >> $@
	./pagebundler $(builddir)/pages/bookmarks.gmi     >> $@
