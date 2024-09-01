SUBDIRS = test

bin_PROGRAMS =		telescope telescope-identity

EXTRA_telescope_SOURCES = compat/ohash.h compat/queue.h compat/imsg.h contrib \
			keys

telescope_SOURCES =	bufio.c			\
			bufio.h			\
			certs.c			\
			certs.h			\
			cmd.c			\
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
			ev.c			\
			ev.h			\
			exec.c			\
			exec.h			\
			fs.c			\
			fs.h			\
			gencmd.awk		\
			genemoji.sh		\
			help.c			\
			hist.c			\
			imsgev.c		\
			imsgev.h		\
			iri.c			\
			iri.h			\
			keymap.c		\
			keymap.h		\
			mailcap.c		\
			mailcap.h		\
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
			tofu.h			\
			ui.c			\
			ui.h			\
			utf8.c			\
			utf8.h			\
			utils.c			\
			utils.h			\
			wrap.c 			\
			xwrapper.c 		\
			xwrapper.h

telescope_identity_SOURCES =			\
			certs.c			\
			certs.h			\
			fs.c			\
			fs.h			\
			hist.c			\
			hist.h			\
			identity.c		\
			parser.c		\
			parser.h 		\
			xwrapper.c 		\
			xwrapper.h

noinst_PROGRAMS =	pagebundler
pagebundler_SOURCES =	pagebundler.c

EXCLUDE_FROM_COCCI=	bufio.c 		\
			certs.c 		\
			cmd-gen.c 		\
			emoji-matcher.c 	\
			ev.c 			\
			hist.c 			\
			pages.c			\
			parse.c 		\
			xwrapper.c
EXTS= 			.c .y

SPATCH_FILES = $(foreach ext,$(EXTS),$(filter %$(ext), 			\
	       $(filter-out $(EXCLUDE_FROM_COCCI), 			\
	       $(telescope_SOURCES), $(telescope_identity_SOURCES), 	\
	       $(wildcard test/*.c))))
.PHONY: cocci
cocci:
	spatch  --sp-file ./contrib/coccinelle/wrap.cocci \
		--in-place $(SPATCH_FILES)

# Override implicit rule since we have to use HOSTCC and not CC.
pagebundler$(EXEEXT): pagebundler.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $(srcdir)/pagebundler.c

# ugly; builds bundled libgrapheme if needed
.NOTPARALLEL:
.NOPARALLEL:
$(LIBGRAPHEME):
	${MAKE} -C $(srcdir)/libgrapheme libgrapheme.a

clean-local:
	test -n "$(LIBGRAPHEME)" && ${MAKE} -C libgrapheme clean || true

BUILT_SOURCES =		cmd.gen.c emoji-matcher.c pages.c

CLEANFILES =		cmd.gen.c emoji-matcher.c pages.c parse.c

LDADD =			$(LIBOBJS) $(LIBGRAPHEME)
EXTRA_telescope_DEPENDENCIES = $(LIBGRAPHEME)

EXTRA_DIST =		ChangeLog	\
			LICENSE		\
			README.md	\
			data/emoji.txt	\
			libgrapheme	\
			pages/*.gmi

dist_man1_MANS =	telescope.1 telescope-identity.1

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
	echo "#include \"pages.h\"" > $@
	./pagebundler $(builddir)/pages/about_about.gmi   >> $@
	./pagebundler $(builddir)/pages/about_blank.gmi   >> $@
	./pagebundler $(builddir)/pages/about_crash.gmi   >> $@
	./pagebundler $(builddir)/pages/about_help.gmi    >> $@
	./pagebundler $(builddir)/pages/about_license.gmi >> $@
	./pagebundler $(builddir)/pages/about_new.gmi     >> $@
	./pagebundler $(builddir)/pages/bookmarks.gmi     >> $@

# --- maintainer targets ---

PUBKEY =	missing
PRIVKEY =	missing

# Ensure that, before we release, that the files in libgrapheme don't include
# any compiled files, etc.  We don't want these as part of a release tarball.
dist-hook:
	${MAKE} -C $(distdir)/libgrapheme clean

release: dist
	sha256 ${distdir}.tar.gz > ${distdir}.tar.gz.sha256
	signify -S -e -m ${distdir}.tar.gz.sha256 -s ${PRIVKEY}
	signify -C -p ${PUBKEY} -x ${distdir}.tar.gz.sha256.sig
