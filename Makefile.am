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

pagebundler$(EXEEXT): pagebundler.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $(srcdir)/pagebundler.c

BUILT_SOURCES =		cmd.gen.c compile_flags.txt u/emoji-matcher.c pages.c

CLEANFILES =		cmd.gen.c compile_flags.txt u/emoji-matcher.c pages.c \
			parse.c

AM_CPPFLAGS =		-I$(top_srcdir)/phos -I$(top_srcdir)/include
LDADD =			$(LIBOBJS)

EXTRA_DIST =		ChangeLog LICENSE README.md data/emoji.txt pages/*.gmi

dist_man1_MANS =	telescope.1

cmd.gen.c: $(srcdir)/include/cmd.h $(srcdir)/gencmd.awk
	${AWK} -f $(srcdir)/gencmd.awk < $(srcdir)/include/cmd.h > $@

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
pages.c: pagebundler $(srcdir)/include/pages.h ${PAGES}
	echo > $@
	echo "#include \"include/pages.h\"" >> $@
	./pagebundler $(srcdir)/pages/about_about.gmi   >> $@
	./pagebundler $(srcdir)/pages/about_blank.gmi   >> $@
	./pagebundler $(srcdir)/pages/about_crash.gmi   >> $@
	./pagebundler $(srcdir)/pages/about_help.gmi    >> $@
	./pagebundler $(srcdir)/pages/about_license.gmi >> $@
	./pagebundler $(srcdir)/pages/about_new.gmi     >> $@
	./pagebundler $(srcdir)/pages/bookmarks.gmi     >> $@
