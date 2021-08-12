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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minibuffer.h"
#include "ui.h"
#include "utf8.h"

static void		*minibuffer_metadata(void);
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

/*
 * Recompute the visible completions.  If add is 1, don't consider the
 * ones already hidden.
 */
void
recompute_completions(int add)
{
	struct line	*l;
	struct vline	*vl;
	struct buffer	*b;

	if (in_minibuffer != MB_COMPREAD)
		return;

	b = &ministate.compl.buffer;
	TAILQ_FOREACH(l, &b->page.head, lines) {
		l->type = LINE_COMPL;
		if (add && l->flags & L_HIDDEN)
			continue;
		if (strcasestr(l->line, ministate.buf) != NULL ||
		    (l->alt != NULL && strcasestr(l->alt, ministate.buf) != NULL)) {
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
	if (vl != NULL)
		vl->parent->type = LINE_COMPL_CURRENT;
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

static void
minibuffer_hist_save_entry(void)
{
	struct hist	*hist;

	if (ministate.history == NULL)
		return;

	if ((hist = calloc(1, sizeof(*hist))) == NULL)
		abort();

	strlcpy(hist->h, ministate.buf, sizeof(hist->h));

	if (TAILQ_EMPTY(&ministate.history->head))
		TAILQ_INSERT_HEAD(&ministate.history->head, hist, entries);
	else
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
	ministate.buffer.current_line->line = ministate.buf;
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
	c = utf8_nth(ministate.buffer.current_line->line, ministate.buffer.cpoff);
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
	struct cmd *cmd;

	for (cmd = cmds; cmd->cmd != NULL; ++cmd) {
		if (!strcmp(cmd->cmd, ministate.buf)) {
			exit_minibuffer();
			minibuffer_hist_save_entry();
			cmd->fn(current_buffer());
			return;
		}
	}

	message("No match");
}

void
ir_select_gemini(void)
{
	char		 buf[1025] = {0};
	struct phos_uri	 uri;
	struct tab	*tab = current_tab;

	exit_minibuffer();
	minibuffer_hist_save_entry();

	/* a bit ugly but... */
	memcpy(&uri, &tab->uri, sizeof(tab->uri));
	phos_uri_set_query(&uri, ministate.buf);
	phos_serialize_uri(&uri, buf, sizeof(buf));
	load_url_in_tab(tab, buf, NULL, 0);
}

void
ir_select_gopher(void)
{
	exit_minibuffer();
	minibuffer_hist_save_entry();

	gopher_send_search_req(current_tab, ministate.buf);
}

void
lu_select(void)
{
	exit_minibuffer();
	minibuffer_hist_save_entry();
	load_url_in_tab(current_tab, ministate.buf, NULL, 0);
}

void
bp_select(void)
{
	exit_minibuffer();
	if (*ministate.buf != '\0')
		add_to_bookmarks(ministate.buf);
	else
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
	load_url_in_tab(current_tab, l->alt, NULL, 0);
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

		if (TAILQ_EMPTY(&p->head))
			TAILQ_INSERT_HEAD(&p->head, l, lines);
		else
			TAILQ_INSERT_TAIL(&p->head, l, lines);

		linedata = NULL;
		descr = NULL;
	}

	if ((l = TAILQ_FIRST(&p->head)) != NULL)
		l->type = LINE_COMPL_CURRENT;
}

void
enter_minibuffer(void (*self_insert_fn)(void), void (*donefn)(void),
    void (*abortfn)(void), struct histhead *hist,
    complfn *complfn, void *compldata)
{
	in_minibuffer = complfn == NULL ? MB_READ : MB_COMPREAD;
	if (in_minibuffer == MB_COMPREAD) {
		ui_schedule_redraw();

		ministate.compl.fn = complfn;
		ministate.compl.data = compldata;
		populate_compl_buffer(complfn, compldata);
	}

	base_map = &minibuffer_map;
	current_map = &minibuffer_map;

	base_map->unhandled_input = self_insert_fn;

	ministate.donefn = donefn;
	ministate.abortfn = abortfn;
	memset(ministate.buf, 0, sizeof(ministate.buf));
	ministate.buffer.current_line = &ministate.vline;
	ministate.buffer.current_line->line = ministate.buf;
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
	    yornp_abort, NULL, NULL, NULL);

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
	    &read_history, NULL, NULL);

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
