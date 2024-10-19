/*
 * Copyright (c) 2021, 2024 Omar Polo <op@omarpolo.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "compat.h"

#include <sys/time.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grapheme.h>

#include "certs.h"
#include "cmd.h"
#include "defaults.h"
#include "ev.h"
#include "fs.h"
#include "hist.h"
#include "iri.h"
#include "keymap.h"
#include "minibuffer.h"
#include "session.h"
#include "ui.h"
#include "utf8.h"
#include "utils.h"
#include "xwrapper.h"

#define nitems(x) (sizeof(x)/sizeof(x[0]))

static void		*minibuffer_metadata(void);
static const char	*minibuffer_compl_text(void);
static void		 minibuffer_hist_save_entry(void);
static void		 yornp_self_insert(void);
static void		 yornp_abort(void);
static void		 read_self_insert(void);
static void		 read_abort(void);
static void		 read_select(const char *);
static void		 handle_clear_echoarea(int, int, void *);

static unsigned long	clechotimer;
static struct timeval	clechotv = { 5, 0 };

static void (*yornp_cb)(int, void *);
static void *yornp_data;

static void (*read_cb)(const char*, struct tab *);
static struct tab *read_data;

struct hist *eecmd_history;
struct hist *ir_history;
struct hist *lu_history;
struct hist *read_history;

struct ministate ministate;

struct buffer minibufferwin;

int in_minibuffer;

static int
codepoint_isgraph(uint32_t cp)
{
	if (cp < INT8_MAX)
		return isgraph((unsigned char)cp);
	return 1;
}

static inline int
matches(char **words, size_t len, struct line *l)
{
	size_t	i;
	int	lm, am;

	for (i = 0; i < len; ++i) {
		lm = am = 0;

		if (strcasestr(l->line, words[i]) != NULL)
			lm = 1;
		if (l->alt != NULL &&
		    strcasestr(l->alt, words[i]) != NULL)
			am = 1;

		if (!lm && !am)
			return 0;
	}

	return 1;
}

/*
 * Recompute the visible completions.  If add is 1, don't consider the
 * ones already hidden.
 */
void
recompute_completions(int add)
{
	static char	 buf[GEMINI_URL_LEN];
	const char	*text;
	char		*input, **ap, *words[10];
	size_t		 len = 0;
	struct line	*l;
	struct vline	*vl;
	struct buffer	*b;

	if (in_minibuffer != MB_COMPREAD)
		return;

	if (!ministate.editing)
		text = hist_cur(ministate.hist);
	else
		text = ministate.buf;

	strlcpy(buf, text, sizeof(buf));
	input = buf;

	/* tokenize the input */
	for (ap = words; ap < words + nitems(words) &&
	    (*ap = strsep(&input, " ")) != NULL;) {
		if (**ap != '\0')
			ap++, len++;
	}

	b = &ministate.compl.buffer;
	TAILQ_FOREACH(l, &b->head, lines) {
		l->type = LINE_COMPL;
		if (add && l->flags & L_HIDDEN)
			continue;
		if (matches(words, len, l)) {
			if (l->flags & L_HIDDEN)
				b->line_max++;
			l->flags &= ~L_HIDDEN;
		} else {
			if (!(l->flags & L_HIDDEN))
				b->line_max--;
			l->flags |= L_HIDDEN;
		}
	}

	if (b->current_line == NULL)
		b->current_line = TAILQ_FIRST(&b->vhead);
	b->current_line = adjust_line(b->current_line, b);
	vl = b->current_line;
	if (ministate.compl.must_select && vl != NULL)
		vl->parent->type = LINE_COMPL_CURRENT;
}

int
minibuffer_insert_current_candidate(void)
{
	struct vline *vl;

	vl = ministate.compl.buffer.current_line;
	if (vl == NULL || vl->parent->flags & L_HIDDEN)
		return -1;

	minibuffer_taint_hist();
	strlcpy(ministate.buf, vl->parent->line, sizeof(ministate.buf));
	ministate.buffer.point_offset = strlen(ministate.buf);
	ministate.vline.len = strlen(ministate.buf);

	return 0;
}

static void *
minibuffer_metadata(void)
{
	struct vline	*vl;

	vl = ministate.compl.buffer.current_line;

	if (vl == NULL || vl->parent->flags & L_HIDDEN)
		return NULL;

	return vl->parent->data;
}

static const char *
minibuffer_compl_text(void)
{
	struct vline	*vl;

	if (!ministate.editing)
		return hist_cur(ministate.hist);

	vl = ministate.compl.buffer.current_line;
	if (vl == NULL || vl->parent->flags & L_HIDDEN ||
	    vl->parent->type == LINE_COMPL || vl->parent->line == NULL)
		return ministate.buf;
	return vl->parent->line;
}

static void
minibuffer_hist_save_entry(void)
{
	if (ministate.hist == NULL)
		return;

	hist_append(ministate.hist, minibuffer_compl_text());
}

/*
 * taint the minibuffer cache: if we're currently showing a history
 * element, copy that to the current buf and reset the "history
 * navigation" thing.
 */
void
minibuffer_taint_hist(void)
{
	if (ministate.editing)
		return;

	ministate.editing = 1;
	strlcpy(ministate.buf, hist_cur(ministate.hist),
	    sizeof(ministate.buf));
	ministate.buffer.point_offset = 0;
	ministate.buffer.current_line->parent->line = ministate.buf;
}

void
minibuffer_confirm(void)
{
	if (!in_minibuffer || ministate.donefn == NULL)
		return;

	minibuffer_taint_hist();
	ministate.donefn(minibuffer_compl_text());
}

void
minibuffer_self_insert(void)
{
	char	*c, tmp[5] = {0};
	size_t	len;

	minibuffer_taint_hist();

	if (thiskey.cp == 0)
		return;

	len = grapheme_encode_utf8(thiskey.cp, tmp, sizeof(tmp));

	c = ministate.buffer.current_line->parent->line
	    + ministate.buffer.current_line->from
	    + ministate.buffer.point_offset;
	if (c + len > ministate.buf + sizeof(ministate.buf) - 1)
		return;

	memmove(c + len, c, strlen(c)+1);
	memcpy(c, tmp, len);
	ministate.buffer.point_offset += len;

	recompute_completions(1);
}

void
sensible_self_insert(void)
{
	if (thiskey.meta ||
	    (!codepoint_isgraph(thiskey.key) && thiskey.key != ' ')) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

void
eecmd_select(const char *t)
{
	struct cmd	*cmd;

	for (cmd = cmds; cmd->cmd != NULL; ++cmd) {
		if (!strcmp(cmd->cmd, t)) {
			minibuffer_hist_save_entry();
			exit_minibuffer();
			cmd->fn(current_buffer());
			return;
		}
	}

	message("No match");
}

void
ir_select_gemini(const char *text)
{
	static struct iri	iri;
	char		 buf[1025];
	struct tab	*tab = current_tab;

	minibuffer_hist_save_entry();

	if (iri_parse(NULL, hist_cur(tab->hist), &iri) == -1)
		goto err;
	if (iri_setquery(&iri, text) == -1)
		goto err;
	if (iri_unparse(&iri, buf, sizeof(buf)) == -1)
		goto err;

	exit_minibuffer();
	load_url_in_tab(tab, buf, NULL, LU_MODE_NOCACHE);
	return;

 err:
	message("Failed to select URL.");
}

void
ir_select_reply(const char *text)
{
	static struct iri iri;
	char		 buf[1025] = {0};
	struct tab	*tab = current_tab;

	minibuffer_hist_save_entry();

	/* a bit ugly but... */
	iri_parse(NULL, tab->last_input_url, &iri);
	iri_setquery(&iri, text);
	iri_unparse(&iri, buf, sizeof(buf));

	exit_minibuffer();
	load_url_in_tab(tab, buf, NULL, LU_MODE_NOCACHE);
}

void
ir_select_gopher(const char *text)
{
	minibuffer_hist_save_entry();
	gopher_send_search_req(current_tab, text);
	exit_minibuffer();
}

void
lu_select(const char *text)
{
	char url[GEMINI_URL_LEN+1];

	minibuffer_hist_save_entry();
	humanify_url(text, hist_cur(current_tab->hist), url, sizeof(url));

	exit_minibuffer();
	load_url_in_tab(current_tab, url, NULL, LU_MODE_NOCACHE);
}

void
bp_select(const char *url)
{
	if (*url != '\0') {
		if (bookmark_page(url) == -1)
			message("failed to bookmark page: %s",
			    strerror(errno));
		else
			message("Bookmarked");
	} else
		message("Abort.");
	exit_minibuffer();
}

void
ts_select(const char *text)
{
	struct tab	*tab;

	if ((tab = minibuffer_metadata()) == NULL) {
		message("No tab selected");
		return;
	}

	exit_minibuffer();
	switch_to_tab(tab);
}

void
ls_select(const char *text)
{
	struct line	*l;

	if ((l = minibuffer_metadata()) == NULL) {
		message("No link selected");
		return;
	}

	exit_minibuffer();
	load_url_in_tab(current_tab, l->alt, NULL, LU_MODE_NOCACHE);
}

static inline void
jump_to_line(struct line *l)
{
	struct vline	*vl;
	struct buffer	*buffer;

	buffer = current_buffer();

	TAILQ_FOREACH(vl, &buffer->vhead, vlines) {
		if (vl->parent == l)
			break;
	}

	if (vl == NULL)
		message("Ops, %s error!  Please report to %s",
		    __func__, PACKAGE_BUGREPORT);
	else {
		buffer->top_line = vl;
		buffer->current_line = vl;
	}
}

void
swiper_select(const char *text)
{
	struct line	*l;

	if ((l = minibuffer_metadata()) == NULL) {
		message("No line selected");
		return;
	}

	exit_minibuffer();
	jump_to_line(l);
}

void
toc_select(const char *text)
{
	struct line	*l;

	if ((l = minibuffer_metadata()) == NULL) {
		message("No line selected");
		return;
	}

	exit_minibuffer();
	jump_to_line(l);
}

static void
save_cert_for_site_cb(int r, void *data)
{
	struct tab		*tab = data;

	cert_save_for(tab->client_cert, &tab->iri, r);
}

void
uc_select(const char *name)
{
	if ((current_tab->client_cert = ccert(name)) == NULL) {
		message("Certificate %s not found", name);
		return;
	}

	exit_minibuffer();

	yornp("Remember for future sessions too?", save_cert_for_site_cb,
	    current_tab);
}

void
search_select(const char *text)
{
	static struct iri	 iri;
	static char		 buf[1025];

	/* a bit ugly but... */
	if (iri_parse(NULL, default_search_engine, &iri) == -1) {
		message("default-search-engine is a malformed IRI.");
		exit_minibuffer();
		return;
	}
	iri_setquery(&iri, text);
	iri_unparse(&iri, buf, sizeof(buf));

	exit_minibuffer();
	load_url_in_tab(current_tab, buf, NULL, LU_MODE_NOCACHE);
}

static void
yornp_self_insert(void)
{
	if (thiskey.key != 'y' && thiskey.key != 'n') {
		message("Please answer y or n");
		return;
	}

	exit_minibuffer();
	yornp_cb(thiskey.key == 'y', yornp_data);
}

static void
yornp_abort(void)
{
	exit_minibuffer();
	yornp_cb(0, yornp_data);
}

static void
read_self_insert(void)
{
	if (thiskey.meta || !codepoint_isgraph(thiskey.cp)) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

static void
read_abort(void)
{
	exit_minibuffer();
	read_cb(NULL, read_data);
}

static void
read_select(const char *text)
{
	exit_minibuffer();
	minibuffer_hist_save_entry();
	read_cb(text, read_data);
}

/*
 * TODO: we should collect this asynchronously...
 */
static inline void
populate_compl_buffer(complfn *fn, void *data)
{
	const char	*s, *descr;
	struct line	*l;
	struct buffer	*b;
	void		*linedata;

	b = &ministate.compl.buffer;

	linedata = NULL;
	descr = NULL;
	while ((s = fn(&data, &linedata, &descr)) != NULL) {
		l = xcalloc(1, sizeof(*l));

		l->type = LINE_COMPL;
		l->data = linedata;
		l->alt = (char*)descr;
		l->line = xstrdup(s);

		TAILQ_INSERT_TAIL(&b->head, l, lines);

		linedata = NULL;
		descr = NULL;
	}

	if ((l = TAILQ_FIRST(&b->head)) != NULL &&
	    ministate.compl.must_select)
		l->type = LINE_COMPL_CURRENT;
}

void
enter_minibuffer(struct minibuffer *minibuffer, const char *fmt, ...)
{
	va_list	 ap;

	va_start(ap, fmt);
	vsnprintf(ministate.prompt, sizeof(ministate.prompt), fmt, ap);
	va_end(ap);

	ministate.compl.must_select = minibuffer->must_select;
	ministate.compl.fn = minibuffer->complfn;
	ministate.compl.data = minibuffer->compldata;

	in_minibuffer = minibuffer->complfn == NULL ? MB_READ : MB_COMPREAD;
	if (in_minibuffer == MB_COMPREAD) {
		populate_compl_buffer(minibuffer->complfn,
		    minibuffer->compldata);
		ui_schedule_redraw();
	}

	base_map = &minibuffer_map;
	current_map = &minibuffer_map;

	base_map->unhandled_input = minibuffer->self_insert;

	ministate.donefn = minibuffer->done;
	ministate.abortfn = minibuffer->abort;
	if (ministate.abortfn == NULL)
		ministate.abortfn = exit_minibuffer;

	if (minibuffer->input) {
		strlcpy(ministate.buf, minibuffer->input,
		    sizeof(ministate.buf));
		ministate.buffer.point_offset = strlen(ministate.buf);
		ministate.vline.len = strlen(ministate.buf);
	} else {
		ministate.buf[0] = '\0';
		ministate.buffer.point_offset = 0;
		ministate.vline.len = 0;
	}

	ministate.buffer.current_line = &ministate.vline;
	ministate.buffer.current_line->parent->line = ministate.buf;

	ministate.editing = 1;
	ministate.hist = minibuffer->history;
	if (ministate.hist)
		hist_seek_start(ministate.hist);
}

void
exit_minibuffer(void)
{
	if (in_minibuffer == MB_COMPREAD) {
		erase_buffer(&ministate.compl.buffer);
		ui_schedule_redraw();
	}

	in_minibuffer = 0;
	base_map = &global_map;
	current_map = &global_map;
}

void
yornp(const char *prompt, void (*fn)(int, void*), void *data)
{
	struct minibuffer m = {
		.self_insert = yornp_self_insert,
		.abort = yornp_abort,
	};

	if (in_minibuffer) {
		fn(0, data);
		return;
	}

	yornp_cb = fn;
	yornp_data = data;
	enter_minibuffer(&m, "%s (y or n)", prompt);
}

void
minibuffer_read(const char *prompt, void (*fn)(const char *, struct tab *),
    struct tab *data, const char *input)
{
	struct minibuffer m = {
		.self_insert = read_self_insert,
		.done = read_select,
		.abort = read_abort,
		.history = read_history,
		.input = input,
	};

	if (in_minibuffer)
		return;

	read_cb = fn;
	read_data = data;
	enter_minibuffer(&m, "%s: ", prompt);
}

static void
handle_clear_echoarea(int fd, int ev, void *d)
{
	free(ministate.curmesg);
	ministate.curmesg = NULL;

	ui_after_message_hook();
}

void
vmessage(const char *fmt, va_list ap)
{
	ev_timer_cancel(clechotimer);
	clechotimer = 0;

	free(ministate.curmesg);
	ministate.curmesg = NULL;

	if (fmt != NULL) {
		clechotimer = ev_timer(&clechotv, handle_clear_echoarea,
		    NULL);

		/* TODO: what to do if the allocation fails here? */
		if (vasprintf(&ministate.curmesg, fmt, ap) == -1)
			ministate.curmesg = NULL;
	}

	ui_after_message_hook();
}

void
message(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmessage(fmt, ap);
	va_end(ap);
}

void
minibuffer_init(void)
{
	if ((eecmd_history = hist_new(HIST_WRAP)) == NULL ||
	    (ir_history = hist_new(HIST_WRAP)) == NULL ||
	    (lu_history = hist_new(HIST_WRAP)) == NULL ||
	    (read_history = hist_new(HIST_WRAP)) == NULL)
		err(1, "hist_new");

	TAILQ_INIT(&ministate.compl.buffer.head);
	TAILQ_INIT(&ministate.compl.buffer.vhead);

	ministate.line.type = LINE_TEXT;
	ministate.vline.parent = &ministate.line;
	ministate.buffer.mode = "*minibuffer*";
	ministate.buffer.current_line = &ministate.vline;
}
