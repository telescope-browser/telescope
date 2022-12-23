/*
 * Copyright (c) 2021 Omar Polo <op@omarpolo.com>
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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "iri.h"
#include "minibuffer.h"
#include "session.h"
#include "ui.h"
#include "utf8.h"
#include "utils.h"

#define nitems(x) (sizeof(x)/sizeof(x[0]))

static void		*minibuffer_metadata(void);
static const char	*minibuffer_compl_text(void);
static void		 minibuffer_hist_save_entry(void);
static void		 yornp_self_insert(void);
static void		 yornp_abort(void);
static void		 read_self_insert(void);
static void		 read_abort(void);
static void		 read_select(void);
static void		 handle_clear_echoarea(int, short, void *);

static struct event	clechoev;
static struct timeval	clechoev_timer = { 5, 0 };

static void (*yornp_cb)(int, struct tab *);
static struct tab *yornp_data;

static void (*read_cb)(const char*, struct tab *);
static struct tab *read_data;

/* XXX: don't forget to init these in minibuffer_init */
struct histhead eecmd_history,
	ir_history,
	lu_history,
	read_history;

struct ministate ministate;

struct buffer minibufferwin;

int in_minibuffer;

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

	if (ministate.hist_cur != NULL)
		text = ministate.hist_cur->h;
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
	TAILQ_FOREACH(l, &b->page.head, lines) {
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
		b->current_line = TAILQ_FIRST(&b->head);
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
	ministate.buffer.cpoff = utf8_cplen(ministate.buf);

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

	if (ministate.hist_cur != NULL)
		return ministate.hist_cur->h;

	vl = ministate.compl.buffer.current_line;
	if (vl == NULL || vl->parent->flags & L_HIDDEN ||
	    vl->parent->type == LINE_COMPL || vl->parent->line == NULL)
		return ministate.buf;
	return vl->parent->line;
}

static void
minibuffer_hist_save_entry(void)
{
	struct hist	*hist;
	const char	*t;

	if (ministate.history == NULL)
		return;

	if ((hist = calloc(1, sizeof(*hist))) == NULL)
		abort();

	t = minibuffer_compl_text();
	strlcpy(hist->h, t, sizeof(hist->h));

	TAILQ_INSERT_TAIL(&ministate.history->head, hist, entries);
	ministate.history->len++;
}

/*
 * taint the minibuffer cache: if we're currently showing a history
 * element, copy that to the current buf and reset the "history
 * navigation" thing.
 */
void
minibuffer_taint_hist(void)
{
	if (ministate.hist_cur == NULL)
		return;

	strlcpy(ministate.buf, ministate.hist_cur->h, sizeof(ministate.buf));
	ministate.hist_cur = NULL;
	ministate.buffer.current_line->parent->line = ministate.buf;
}

void
minibuffer_self_insert(void)
{
	char	*c, tmp[5] = {0};
	size_t	len;

	minibuffer_taint_hist();

	if (thiskey.cp == 0)
		return;

	len = utf8_encode(thiskey.cp, tmp);
	c = utf8_nth(ministate.buffer.current_line->parent->line,
	    ministate.buffer.cpoff);
	if (c + len > ministate.buf + sizeof(ministate.buf) - 1)
		return;

	memmove(c + len, c, strlen(c)+1);
	memcpy(c, tmp, len);
	ministate.buffer.cpoff++;

	recompute_completions(1);
}

void
sensible_self_insert(void)
{
	if (thiskey.meta ||
	    (!unicode_isgraph(thiskey.key) && thiskey.key != ' ')) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

void
eecmd_select(void)
{
	struct cmd	*cmd;
	const char	*t;

	t = minibuffer_compl_text();
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
ir_select_gemini(void)
{
	static struct iri	iri;
	char		 buf[1025];
	struct tab	*tab = current_tab;

	minibuffer_hist_save_entry();

	if (iri_parse(NULL, tab->hist_cur->h, &iri) == -1)
		goto err;
	if (iri_setquery(&iri, minibuffer_compl_text()) == -1)
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
ir_select_reply(void)
{
	char		 buf[1025] = {0};
	struct phos_uri	 uri;
	struct tab	*tab = current_tab;

	minibuffer_hist_save_entry();

	/* a bit ugly but... */
	strlcpy(buf, tab->last_input_url, sizeof(buf));
	phos_parse_absolute_uri(buf, &uri);
	phos_uri_set_query(&uri, minibuffer_compl_text());
	phos_serialize_uri(&uri, buf, sizeof(buf));

	exit_minibuffer();
	load_url_in_tab(tab, buf, NULL, LU_MODE_NOCACHE);
}

void
ir_select_gopher(void)
{
	minibuffer_hist_save_entry();
	gopher_send_search_req(current_tab, minibuffer_compl_text());
	exit_minibuffer();
}

void
lu_select(void)
{
	char url[GEMINI_URL_LEN+1];

	minibuffer_hist_save_entry();
	humanify_url(minibuffer_compl_text(), url, sizeof(url));
	exit_minibuffer();
	load_url_in_tab(current_tab, url, NULL, LU_MODE_NOCACHE);
}

void
bp_select(void)
{
	exit_minibuffer();
	if (*ministate.buf != '\0') {
		if (!bookmark_page(ministate.buf))
			message("failed to bookmark page: %s",
			    strerror(errno));
	} else
		message("Abort.");
}

void
ts_select(void)
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
ls_select(void)
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

	TAILQ_FOREACH(vl, &buffer->head, vlines) {
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
swiper_select(void)
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
toc_select(void)
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
	if (thiskey.meta || !unicode_isgraph(thiskey.cp)) {
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
read_select(void)
{
	exit_minibuffer();
	minibuffer_hist_save_entry();
	read_cb(ministate.buf, read_data);
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
	struct parser	*p;
	void		*linedata;

	b = &ministate.compl.buffer;
	p = &b->page;

	linedata = NULL;
	descr = NULL;
	while ((s = fn(&data, &linedata, &descr)) != NULL) {
		if ((l = calloc(1, sizeof(*l))) == NULL)
			abort();

		l->type = LINE_COMPL;
		l->data = linedata;
		l->alt = (char*)descr;
		if ((l->line = strdup(s)) == NULL)
			abort();

		TAILQ_INSERT_TAIL(&p->head, l, lines);

		linedata = NULL;
		descr = NULL;
	}

	if ((l = TAILQ_FIRST(&p->head)) != NULL &&
	    ministate.compl.must_select)
		l->type = LINE_COMPL_CURRENT;
}

void
enter_minibuffer(void (*self_insert_fn)(void), void (*donefn)(void),
    void (*abortfn)(void), struct histhead *hist,
    complfn *complfn, void *compldata, int must_select)
{
	ministate.compl.must_select = must_select;
	ministate.compl.fn = complfn;
	ministate.compl.data = compldata;

	in_minibuffer = complfn == NULL ? MB_READ : MB_COMPREAD;
	if (in_minibuffer == MB_COMPREAD) {
		populate_compl_buffer(complfn, compldata);
		ui_schedule_redraw();
	}

	base_map = &minibuffer_map;
	current_map = &minibuffer_map;

	base_map->unhandled_input = self_insert_fn;

	ministate.donefn = donefn;
	ministate.abortfn = abortfn;
	memset(ministate.buf, 0, sizeof(ministate.buf));
	ministate.buffer.current_line = &ministate.vline;
	ministate.buffer.current_line->parent->line = ministate.buf;
	ministate.buffer.cpoff = 0;
	strlcpy(ministate.buf, "", sizeof(ministate.prompt));

	ministate.history = hist;
	ministate.hist_cur = NULL;
	ministate.hist_off = 0;
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
yornp(const char *prompt, void (*fn)(int, struct tab*),
    struct tab *data)
{
	size_t len;

	if (in_minibuffer) {
		fn(0, data);
		return;
	}

	yornp_cb = fn;
	yornp_data = data;
	enter_minibuffer(yornp_self_insert, yornp_self_insert,
	    yornp_abort, NULL, NULL, NULL, 0);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, prompt, len);
	strlcat(ministate.prompt, " (y or n) ", len);
}

void
minibuffer_read(const char *prompt, void (*fn)(const char *, struct tab *),
    struct tab *data)
{
	size_t len;

	if (in_minibuffer)
		return;

	read_cb = fn;
	read_data = data;
	enter_minibuffer(read_self_insert, read_select, read_abort,
	    &read_history, NULL, NULL, 0);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, prompt, len);
	strlcat(ministate.prompt, ": ", len);
}

static void
handle_clear_echoarea(int fd, short ev, void *d)
{
	free(ministate.curmesg);
	ministate.curmesg = NULL;

	ui_after_message_hook();
}

void
vmessage(const char *fmt, va_list ap)
{
	if (evtimer_pending(&clechoev, NULL))
		evtimer_del(&clechoev);

	free(ministate.curmesg);
	ministate.curmesg = NULL;

	if (fmt != NULL) {
		evtimer_set(&clechoev, handle_clear_echoarea, NULL);
		evtimer_add(&clechoev, &clechoev_timer);

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
	TAILQ_INIT(&eecmd_history.head);
	TAILQ_INIT(&ir_history.head);
	TAILQ_INIT(&lu_history.head);
	TAILQ_INIT(&read_history.head);

	TAILQ_INIT(&ministate.compl.buffer.head);
	TAILQ_INIT(&ministate.compl.buffer.page.head);

	ministate.line.type = LINE_TEXT;
	ministate.vline.parent = &ministate.line;
	ministate.buffer.page.name = "*minibuffer*";
	ministate.buffer.current_line = &ministate.vline;

	evtimer_set(&clechoev, handle_clear_echoarea, NULL);
}
