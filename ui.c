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

/*
 * Ncurses UI for telescope.
 *
 * Text scrolling
 * ==============
 *
 * ncurses allows you to scroll a window, but when a line goes out of
 * the visible area it's forgotten.  We keep a list of formatted lines
 * (``visual lines'') that we know fits in the window, and draw them.
 *
 * This means that on every resize we have to clear our list of lines
 * and re-render everything.  A clever approach would be to do this
 * ``on-demand'', but it's still missing.
 *
 */

#include "telescope.h"

#include <assert.h>
#include <curses.h>
#include <event.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct event	stdioev, winchev;

static void		 restore_curs_x(struct buffer *);
static void		 global_key_unbound(void);
static void		 minibuffer_hist_save_entry(void);
static void		 minibuffer_self_insert(void);
static void		 yornp_self_insert(void);
static void		 yornp_abort(void);
static void		 read_self_insert(void);
static void		 read_abort(void);
static void		 read_select(void);

static struct vline	*nth_line(struct buffer*, size_t);
static struct buffer	*current_buffer(void);
static int		 readkey(void);
static void		 dispatch_stdio(int, short, void*);
static void		 handle_clear_minibuf(int, short, void*);
static void		 handle_resize(int, short, void*);
static void		 handle_resize_nodelay(int, short, void*);
static int		 wrap_page(struct buffer*, int);
static void		 print_vline(int, int, WINDOW*, struct vline*);
static void		 redraw_tabline(void);
static void		 redraw_window(WINDOW*, int, int, struct buffer*);
static void		 redraw_help(void);
static void		 redraw_body(struct tab*);
static void		 redraw_modeline(struct tab*);
static void		 redraw_minibuffer(void);
static void		 redraw_tab(struct tab*);
static void		 emit_help_item(char*, void*);
static void		 rec_compute_help(struct kmap*, char*, size_t);
static void		 recompute_help(void);
static void		 update_loading_anim(int, short, void*);
static void		 stop_loading_anim(struct tab*);
static void		 session_new_tab_cb(const char*);
static void		 usage(void);

static int		 x_offset;

struct thiskey thiskey;

static struct event	resizeev;
static struct timeval	resize_timer = { 0, 250000 };

static WINDOW	*tabline, *body, *modeline, *minibuf;

int			 body_lines, body_cols;

static WINDOW		*help;
static struct buffer	 helpwin;
static int		 help_lines, help_cols;

static int		 side_window;

static struct event	clminibufev;
static struct timeval	clminibufev_timer = { 5, 0 };
static struct timeval	loadingev_timer = { 0, 250000 };

static uint32_t		 tab_counter;

static char	keybuf[64];

static void (*yornp_cb)(int, unsigned int);
static unsigned int yornp_data;

static void (*read_cb)(const char*, unsigned int);
static unsigned int read_data;

struct kmap global_map,
	minibuffer_map,
	*current_map,
	*base_map;

struct histhead eecmd_history,
	ir_history,
	lu_history,
	read_history;

int in_minibuffer;

struct ministate ministate;

static inline void
update_x_offset(void)
{
	if (olivetti_mode && fill_column < body_cols)
		x_offset = (body_cols - fill_column)/2;
	else
		x_offset = 0;
}

void
save_excursion(struct excursion *place, struct buffer *buffer)
{
	place->curs_x = buffer->curs_x;
	place->curs_y = buffer->curs_y;
	place->line_off = buffer->line_off;
	place->current_line = buffer->current_line;
	place->cpoff = buffer->cpoff;
}

void
restore_excursion(struct excursion *place, struct buffer *buffer)
{
	buffer->curs_x = place->curs_x;
	buffer->curs_y = place->curs_y;
	buffer->line_off = place->line_off;
	buffer->current_line = place->current_line;
	buffer->cpoff = place->cpoff;
}

static void
restore_curs_x(struct buffer *buffer)
{
	struct vline	*vl;
	const char	*prfx;

	vl = buffer->current_line;
	if (vl == NULL || vl->line == NULL)
		buffer->curs_x = buffer->cpoff = 0;
	else
		buffer->curs_x = utf8_snwidth(vl->line, buffer->cpoff);

	buffer->curs_x += x_offset;

	if (vl != NULL) {
		prfx = line_prefixes[vl->parent->type].prfx1;
		buffer->curs_x += utf8_swidth(prfx);
	}
}

static void
global_key_unbound(void)
{
	message("%s is undefined", keybuf);
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
}

static void
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
}

void
eecmd_self_insert(void)
{
	if (thiskey.meta || unicode_isspace(thiskey.cp) ||
	    !unicode_isgraph(thiskey.cp)) {
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
ir_self_insert(void)
{
	minibuffer_self_insert();
}

void
ir_select(void)
{
	char		 buf[1025] = {0};
	struct phos_uri	 uri;
	struct tab	*tab;

	tab = current_tab();

	exit_minibuffer();
	minibuffer_hist_save_entry();

	/* a bit ugly but... */
	memcpy(&uri, &tab->uri, sizeof(tab->uri));
	phos_uri_set_query(&uri, ministate.buf);
	phos_serialize_uri(&uri, buf, sizeof(buf));
	load_url_in_tab(tab, buf);
}

void
lu_self_insert(void)
{
	if (thiskey.meta || unicode_isspace(thiskey.key) ||
	    !unicode_isgraph(thiskey.key)) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

void
lu_select(void)
{
	exit_minibuffer();
	minibuffer_hist_save_entry();
	load_url_in_tab(current_tab(), ministate.buf);
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

static struct vline *
nth_line(struct buffer *buffer, size_t n)
{
	struct vline	*vl;
	size_t		 i;

	i = 0;
	TAILQ_FOREACH(vl, &buffer->head, vlines) {
		if (i == n)
			return vl;
		i++;
	}

	/* unreachable */
	abort();
}

struct tab *
current_tab(void)
{
	struct tab *t;

	TAILQ_FOREACH(t, &tabshead, tabs) {
		if (t->flags & TAB_CURRENT)
			return t;
	}

	/* unreachable */
	abort();
}

struct buffer *
current_buffer(void)
{
	if (in_minibuffer)
		return &ministate.buffer;
	return &current_tab()->buffer;
}

static int
readkey(void)
{
	uint32_t state = 0;

	if ((thiskey.key = wgetch(body)) == ERR)
		return 0;

	thiskey.meta = thiskey.key == 27;
	if (thiskey.meta) {
		thiskey.key = wgetch(body);
		if (thiskey.key == ERR || thiskey.key == 27) {
			thiskey.meta = 0;
			thiskey.key = 27;
		}
	}

	thiskey.cp = 0;
	if ((unsigned int)thiskey.key < UINT8_MAX) {
		while (1) {
			if (!utf8_decode(&state, &thiskey.cp, (uint8_t)thiskey.key))
				break;
			if ((thiskey.key = wgetch(body)) == ERR) {
				message("Error decoding user input");
				return 0;
			}
		}
	}

	return 1;
}

static void
dispatch_stdio(int fd, short ev, void *d)
{
	struct keymap	*k;
	const char	*keyname;
	char		 tmp[5] = {0};

	if (!readkey())
		return;

	if (keybuf[0] != '\0')
		strlcat(keybuf, " ", sizeof(keybuf));
	if (thiskey.meta)
		strlcat(keybuf, "M-", sizeof(keybuf));
	if (thiskey.cp != 0) {
		utf8_encode(thiskey.cp, tmp);
		strlcat(keybuf, tmp, sizeof(keybuf));
	} else {
		if ((keyname = unkbd(thiskey.key)) != NULL)
			strlcat(keybuf, keyname, sizeof(keybuf));
		else {
			tmp[0] = thiskey.key;
			strlcat(keybuf, tmp, sizeof(keybuf));
		}
	}

	TAILQ_FOREACH(k, &current_map->m, keymaps) {
		if (k->meta == thiskey.meta &&
		    k->key == thiskey.key) {
			if (k->fn == NULL)
				current_map = &k->map;
			else {
				current_map = base_map;
				strlcpy(keybuf, "", sizeof(keybuf));
				k->fn(current_buffer());
			}
			goto done;
		}
	}

	if (current_map->unhandled_input != NULL)
		current_map->unhandled_input();
	else
		global_key_unbound();

	strlcpy(keybuf, "", sizeof(keybuf));
	current_map = base_map;

done:
	if (side_window)
		recompute_help();

	redraw_tab(current_tab());
}

static void
handle_clear_minibuf(int fd, short ev, void *d)
{
	free(ministate.curmesg);
	ministate.curmesg = NULL;

	redraw_minibuffer();
	if (in_minibuffer) {
		wrefresh(body);
		wrefresh(minibuf);
	} else {
		wrefresh(minibuf);
		wrefresh(body);
	}
}

static void
handle_resize(int sig, short ev, void *d)
{
	if (event_pending(&resizeev, EV_TIMEOUT, NULL)) {
		event_del(&resizeev);
	}
	evtimer_set(&resizeev, handle_resize_nodelay, NULL);
	evtimer_add(&resizeev, &resize_timer);
}

static void
handle_resize_nodelay(int s, short ev, void *d)
{
	struct tab	*tab;

	endwin();
	refresh();
	clear();

	/* move and resize the windows, in reverse order! */

	mvwin(minibuf, LINES-1, 0);
	wresize(minibuf, 1, COLS);

	mvwin(modeline, LINES-2, 0);
	wresize(modeline, 1, COLS);

	body_lines = LINES-3;
	body_cols = COLS;

	if (side_window) {
		help_cols = 0.3 * COLS;
		help_lines = LINES-3;
		mvwin(help, 1, 0);
		wresize(help, help_lines, help_cols);

		wrap_page(&helpwin, help_cols);

		body_cols = COLS - help_cols - 1;
		mvwin(body, 1, help_cols);
	} else
		mvwin(body, 1, 0);

	update_x_offset();
	wresize(body, body_lines, body_cols);

	wresize(tabline, 1, COLS);

	tab = current_tab();

	wrap_page(&tab->buffer, body_cols);
	redraw_tab(tab);
}

static int
wrap_page(struct buffer *buffer, int width)
{
	struct line		*l;
	const struct line	*top_orig, *orig;
	struct vline		*vl;
	int			 pre_width;
	const char		*prfx;

	top_orig = buffer->top_line == NULL ? NULL : buffer->top_line->parent;
	orig = buffer->current_line == NULL ? NULL : buffer->current_line->parent;

	buffer->top_line = NULL;
	buffer->current_line = NULL;

	buffer->force_redraw = 1;
	buffer->curs_y = 0;
	buffer->line_off = 0;

	empty_vlist(buffer);

	TAILQ_FOREACH(l, &buffer->page.head, lines) {
		prfx = line_prefixes[l->type].prfx1;
		switch (l->type) {
		case LINE_TEXT:
		case LINE_LINK:
		case LINE_TITLE_1:
		case LINE_TITLE_2:
		case LINE_TITLE_3:
		case LINE_ITEM:
		case LINE_QUOTE:
		case LINE_PRE_START:
		case LINE_PRE_END:
			wrap_text(buffer, prfx, l, MIN(fill_column, width));
			break;
		case LINE_PRE_CONTENT:
			if (olivetti_mode)
				pre_width = MIN(fill_column, width);
			else
				pre_width = width;
			hardwrap_text(buffer, l, pre_width);
			break;
		}

		if (top_orig == l && buffer->top_line == NULL) {
			buffer->line_off = buffer->line_max-1;
			buffer->top_line = TAILQ_LAST(&buffer->head, vhead);

			while (1) {
				vl = TAILQ_PREV(buffer->top_line, vhead, vlines);
				if (vl == NULL || vl->parent != orig)
					break;
				buffer->top_line = vl;
				buffer->line_off--;
			}
		}

		if (orig == l && buffer->current_line == NULL) {
			buffer->current_line = TAILQ_LAST(&buffer->head, vhead);

			while (1) {
				vl = TAILQ_PREV(buffer->current_line, vhead, vlines);
				if (vl == NULL || vl->parent != orig)
					break;
				buffer->current_line = vl;
			}
		}
	}

        if (buffer->current_line == NULL)
		buffer->current_line = TAILQ_FIRST(&buffer->head);

	if (buffer->top_line == NULL)
		buffer->top_line = buffer->current_line;

	return 1;
}

static void
print_vline(int off, int width, WINDOW *window, struct vline *vl)
{
	const char *text;
	const char *prfx;
	struct line_face *f;
	int i, left, x, y;

	f = &line_faces[vl->parent->type];

	/* unused, set by getyx */
	(void)y;

	if (!vl->flags)
		prfx = line_prefixes[vl->parent->type].prfx1;
	else
		prfx = line_prefixes[vl->parent->type].prfx2;

	text = vl->line;
	if (text == NULL)
		text = "";

	wattr_on(window, body_face.left, NULL);
	for (i = 0; i < off; i++)
		waddch(window, ' ');
	wattr_off(window, body_face.left, NULL);

	wattr_on(window, f->prefix, NULL);
	wprintw(window, "%s", prfx);
	wattr_off(window, f->prefix, NULL);

	wattr_on(window, f->text, NULL);
	wprintw(window, "%s", text);
	wattr_off(window, f->text, NULL);

	getyx(window, y, x);

	left = width - x;

	wattr_on(window, f->trail, NULL);
	for (i = 0; i < left - off - 1; ++i)
		waddch(window, ' ');
	wattr_off(window, f->trail, NULL);

	wattr_on(window, body_face.right, NULL);
	for (i = 0; i < off; i++)
		waddch(window, ' ');
	wattr_off(window, body_face.right, NULL);

}

static void
redraw_tabline(void)
{
	struct tab	*tab;
	size_t		 toskip, ots, tabwidth, space, x;
	int		 current, y, truncated;
	const char	*title;
	char		 buf[25];

	x = 0;

	/* unused, but setted by a getyx */
	(void)y;

	tabwidth = sizeof(buf)+1;
	space = COLS-2;

	toskip = 0;
	TAILQ_FOREACH(tab, &tabshead, tabs) {
		toskip++;
		if (tab->flags & TAB_CURRENT)
			break;
	}

	if (toskip * tabwidth < space)
		toskip = 0;
	else {
		ots = toskip;
		toskip--;
                while (toskip != 0 &&
		    (ots - toskip+1) * tabwidth < space)
			toskip--;
	}

	werase(tabline);
	wattr_on(tabline, tab_face.background, NULL);
	wprintw(tabline, toskip == 0 ? " " : "<");
	wattr_off(tabline, tab_face.background, NULL);

	truncated = 0;
	TAILQ_FOREACH(tab, &tabshead, tabs) {
		if (truncated)
			break;
		if (toskip != 0) {
			toskip--;
			continue;
		}

		getyx(tabline, y, x);
		if (x + sizeof(buf)+2 >= (size_t)COLS)
			truncated = 1;

		current = tab->flags & TAB_CURRENT;

		if (*(title = tab->buffer.page.title) == '\0')
			title = tab->hist_cur->h;

		if (tab->flags & TAB_URGENT)
			strlcpy(buf, "!", sizeof(buf));
		else
			strlcpy(buf, " ", sizeof(buf));

                if (strlcat(buf, title, sizeof(buf)) >= sizeof(buf)) {
			/* truncation happens */
			strlcpy(&buf[sizeof(buf)-4], "...", 4);
		} else {
			/* pad with spaces */
			while (strlcat(buf, "    ", sizeof(buf)) < sizeof(buf))
				/* nop */ ;
		}

		if (current)
			wattr_on(tabline, tab_face.current, NULL);
		else
			wattr_on(tabline, tab_face.tab, NULL);

		wprintw(tabline, "%s", buf);
		if (TAILQ_NEXT(tab, tabs) != NULL)
			wprintw(tabline, " ");

		if (current)
			wattr_off(tabline, tab_face.current, NULL);
		else
			wattr_off(tabline, tab_face.tab, NULL);
	}

	wattr_on(tabline, tab_face.background, NULL);
	for (; x < (size_t)COLS; ++x)
		waddch(tabline, ' ');
	if (truncated)
		mvwprintw(tabline, 0, COLS-1, ">");
	wattr_off(tabline, tab_face.background, NULL);
}

static void
redraw_window(WINDOW *win, int height, int width, struct buffer *buffer)
{
        struct vline	*vl;
	int		 l, onscreen;

	restore_curs_x(buffer);

	/*
	 * TODO: ignoring buffer->force_update and always
	 * re-rendering.  In theory we can recompute the y position
	 * without a re-render, and optimize here.  It's not the only
	 * optimisation possible here, wscrl wolud also be an
	 * interesting one.
	 */

again:
	werase(win);
	buffer->curs_y = 0;

	if (TAILQ_EMPTY(&buffer->head))
		goto end;

	l = 0;
	onscreen = 0;
	for (vl = buffer->top_line; vl != NULL; vl = TAILQ_NEXT(vl, vlines)) {
		wmove(win, l, 0);
		print_vline(x_offset, width, win, vl);

		if (vl == buffer->current_line)
			onscreen = 1;

		if (!onscreen)
			buffer->curs_y++;

		l++;
		if (l == height)
			break;
	}

	if (!onscreen) {
		for (; vl != NULL; vl = TAILQ_NEXT(vl, vlines)) {
			if (vl == buffer->current_line)
				break;
			buffer->line_off++;
			buffer->top_line = TAILQ_NEXT(buffer->top_line, vlines);
		}

		goto again;
	}

	buffer->last_line_off = buffer->line_off;
	buffer->force_redraw = 0;
end:
	wmove(win, buffer->curs_y, buffer->curs_x);
}

static void
redraw_help(void)
{
	redraw_window(help, help_lines, help_cols, &helpwin);
}

static void
redraw_body(struct tab *tab)
{
	static struct tab *last_tab;

	if (last_tab != tab)
		tab->buffer.force_redraw =1;
	last_tab = tab;

	redraw_window(body, body_lines, body_cols, &tab->buffer);
}

static inline char
trust_status_char(enum trust_state ts)
{
	switch (ts) {
	case TS_UNKNOWN:	return 'u';
	case TS_UNTRUSTED:	return '!';
	case TS_TRUSTED:	return 'v';
	case TS_VERIFIED:	return 'V';
	default:		return 'X';
	}
}

static void
redraw_modeline(struct tab *tab)
{
	double		 pct;
	int		 x, y, max_x, max_y;
	const char	*mode = tab->buffer.page.name;
	const char	*spin = "-\\|/";

	werase(modeline);
	wattr_on(modeline, modeline_face.background, NULL);
	wmove(modeline, 0, 0);

	wprintw(modeline, "-%c%c %s ",
	    spin[tab->loading_anim_step],
	    trust_status_char(tab->trust),
	    mode == NULL ? "(none)" : mode);

	pct = (tab->buffer.line_off + tab->buffer.curs_y) * 100.0 / tab->buffer.line_max;

	if (tab->buffer.line_max <= (size_t)body_lines)
                wprintw(modeline, "All ");
	else if (tab->buffer.line_off == 0)
                wprintw(modeline, "Top ");
	else if (tab->buffer.line_off + body_lines >= tab->buffer.line_max)
		wprintw(modeline, "Bottom ");
	else
		wprintw(modeline, "%.0f%% ", pct);

	wprintw(modeline, "%d/%d %s ",
	    tab->buffer.line_off + tab->buffer.curs_y,
	    tab->buffer.line_max,
	    tab->hist_cur->h);

	getyx(modeline, y, x);
	getmaxyx(modeline, max_y, max_x);

	(void)y;
	(void)max_y;

	for (; x < max_x; ++x)
		waddstr(modeline, "-");

	wattr_off(modeline, modeline_face.background, NULL);
}

static void
redraw_minibuffer(void)
{
	struct tab *tab;
	size_t off_y, off_x = 0;
	char *start = NULL, *c = NULL;

	/* unused, but set by getyx */
	(void)off_y;

	wattr_on(minibuf, minibuffer_face.background, NULL);
	werase(minibuf);

	if (in_minibuffer) {
		mvwprintw(minibuf, 0, 0, "%s", ministate.prompt);
		if (ministate.hist_cur != NULL)
			wprintw(minibuf, "(%zu/%zu) ",
			    ministate.hist_off + 1,
			    ministate.history->len);

		getyx(minibuf, off_y, off_x);

		start = ministate.hist_cur != NULL
			? ministate.hist_cur->h
			: ministate.buf;
		c = utf8_nth(ministate.buffer.current_line->line,
		    ministate.buffer.cpoff);
		while (utf8_swidth_between(start, c) > (size_t)COLS/2) {
			start = utf8_next_cp(start);
		}

		waddstr(minibuf, start);
	}

	if (ministate.curmesg != NULL)
                wprintw(minibuf, in_minibuffer ? "  [%s]" : "%s",
		    ministate.curmesg);

	if (!in_minibuffer && ministate.curmesg == NULL)
		waddstr(minibuf, keybuf);

	/* If nothing else, show the URL at point */
	if (!in_minibuffer && ministate.curmesg == NULL && *keybuf == '\0') {
		tab = current_tab();
		if (tab->buffer.current_line != NULL &&
		    tab->buffer.current_line->parent->type == LINE_LINK)
			waddstr(minibuf, tab->buffer.current_line->parent->alt);
	}

	if (in_minibuffer)
		wmove(minibuf, 0, off_x + utf8_swidth_between(start, c));

	wattr_off(minibuf, minibuffer_face.background, NULL);
}

static void
redraw_tab(struct tab *tab)
{
	if (side_window) {
		redraw_help();
		wnoutrefresh(help);
	}

	redraw_tabline();
	redraw_body(tab);
	redraw_modeline(tab);
	redraw_minibuffer();

	wnoutrefresh(tabline);
	wnoutrefresh(modeline);

	if (in_minibuffer) {
		wnoutrefresh(body);
		wnoutrefresh(minibuf);
	} else {
		wnoutrefresh(minibuf);
		wnoutrefresh(body);
	}

	doupdate();
}

static void
emit_help_item(char *prfx, void *fn)
{
	struct line	*l;
	struct cmd	*cmd;

	for (cmd = cmds; cmd->cmd != NULL; ++cmd) {
		if (fn == cmd->fn)
			break;
	}
	assert(cmd != NULL);

	if ((l = calloc(1, sizeof(*l))) == NULL)
		abort();

	l->type = LINE_TEXT;
	l->alt = NULL;

	asprintf(&l->line, "%s %s", prfx, cmd->cmd);

	if (TAILQ_EMPTY(&helpwin.page.head))
		TAILQ_INSERT_HEAD(&helpwin.page.head, l, lines);
	else
		TAILQ_INSERT_TAIL(&helpwin.page.head, l, lines);
}

static void
rec_compute_help(struct kmap *keymap, char *prfx, size_t len)
{
	struct keymap	*k;
	char		 p[32];
	const char	*kn;

	TAILQ_FOREACH(k, &keymap->m, keymaps) {
		strlcpy(p, prfx, sizeof(p));
		if (*p != '\0')
			strlcat(p, " ", sizeof(p));
		if (k->meta)
			strlcat(p, "M-", sizeof(p));
		if ((kn = unkbd(k->key)) != NULL)
			strlcat(p, kn, sizeof(p));
		else
			strlcat(p, keyname(k->key), sizeof(p));

		if (k->fn == NULL)
			rec_compute_help(&k->map, p, sizeof(p));
		else
			emit_help_item(p, k->fn);
	}
}

static void
recompute_help(void)
{
	char	p[32] = { 0 };

	empty_vlist(&helpwin);
	empty_linelist(&helpwin);
	rec_compute_help(current_map, p, sizeof(p));
	wrap_page(&helpwin, help_cols);
}

void
vmessage(const char *fmt, va_list ap)
{
	if (evtimer_pending(&clminibufev, NULL))
		evtimer_del(&clminibufev);

	free(ministate.curmesg);
	ministate.curmesg = NULL;

	if (fmt != NULL) {
		evtimer_set(&clminibufev, handle_clear_minibuf, NULL);
		evtimer_add(&clminibufev, &clminibufev_timer);

		/* TODO: what to do if the allocation fails here? */
		if (vasprintf(&ministate.curmesg, fmt, ap) == -1)
			ministate.curmesg = NULL;
	}

	redraw_minibuffer();
	if (in_minibuffer) {
		wrefresh(body);
		wrefresh(minibuf);
	} else {
		wrefresh(minibuf);
		wrefresh(body);
	}
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
start_loading_anim(struct tab *tab)
{
	if (tab->loading_anim)
		return;
	tab->loading_anim = 1;
	evtimer_set(&tab->loadingev, update_loading_anim, tab);
	evtimer_add(&tab->loadingev, &loadingev_timer);
}

static void
update_loading_anim(int fd, short ev, void *d)
{
	struct tab	*tab = d;

	tab->loading_anim_step = (tab->loading_anim_step+1)%4;

	if (tab->flags & TAB_CURRENT) {
		redraw_modeline(tab);
		wrefresh(modeline);
		wrefresh(body);
		if (in_minibuffer)
			wrefresh(minibuf);
	}

	evtimer_add(&tab->loadingev, &loadingev_timer);
}

static void
stop_loading_anim(struct tab *tab)
{
	if (!tab->loading_anim)
		return;
	evtimer_del(&tab->loadingev);
	tab->loading_anim = 0;
	tab->loading_anim_step = 0;

	if (!(tab->flags & TAB_CURRENT))
		return;

	redraw_modeline(tab);

	wrefresh(modeline);
	wrefresh(body);
	if (in_minibuffer)
		wrefresh(minibuf);
}

void
load_url_in_tab(struct tab *tab, const char *url)
{
	message("Loading %s...", url);
	start_loading_anim(tab);
	load_url(tab, url);

	tab->buffer.curs_x = 0;
	tab->buffer.curs_y = 0;
	redraw_tab(tab);
}

void
enter_minibuffer(void (*self_insert_fn)(void), void (*donefn)(void),
    void (*abortfn)(void), struct histhead *hist)
{
	in_minibuffer = 1;
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
	werase(minibuf);

	in_minibuffer = 0;
	base_map = &global_map;
	current_map = &global_map;
}

void
switch_to_tab(struct tab *tab)
{
	struct tab	*t;

	TAILQ_FOREACH(t, &tabshead, tabs) {
		t->flags &= ~TAB_CURRENT;
	}

	tab->flags |= TAB_CURRENT;
	tab->flags &= ~TAB_URGENT;
}

unsigned int
tab_new_id(void)
{
	return tab_counter++;
}

struct tab *
new_tab(const char *url)
{
	struct tab	*tab;

	if ((tab = calloc(1, sizeof(*tab))) == NULL) {
		event_loopbreak();
		return NULL;
	}
	tab->fd = -1;

	TAILQ_INIT(&tab->hist.head);

	TAILQ_INIT(&tab->buffer.head);

	tab->id = tab_new_id();
	switch_to_tab(tab);

	if (TAILQ_EMPTY(&tabshead))
		TAILQ_INSERT_HEAD(&tabshead, tab, tabs);
	else
		TAILQ_INSERT_TAIL(&tabshead, tab, tabs);

	load_url_in_tab(tab, url);
	return tab;
}

static void
session_new_tab_cb(const char *url)
{
	new_tab(url);
}

static void
usage(void)
{
	fprintf(stderr, "USAGE: %s [-hn] [-c config] [url]\n", getprogname());
	fprintf(stderr, "version: " PACKAGE " " VERSION "\n");
}

int
ui_init(int argc, char * const *argv)
{
	char path[PATH_MAX];
	const char *url = NEW_TAB_URL;
	int ch, configtest = 0, fonf = 0;

	if (getenv("NO_COLOR") != NULL)
		enable_colors = 0;

	strlcpy(path, getenv("HOME"), sizeof(path));
	strlcat(path, "/.telescope/config", sizeof(path));

	while ((ch = getopt(argc, argv, "c:hn")) != -1) {
		switch (ch) {
		case 'c':
			fonf = 1;
			strlcpy(path, optarg, sizeof(path));
			break;
		case 'n':
			configtest = 1;
			break;
		case 'h':
			usage();
			return 0;
		default:
			usage();
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	/* setup keys before reading the config */
	TAILQ_INIT(&global_map.m);
	global_map.unhandled_input = global_key_unbound;

	TAILQ_INIT(&minibuffer_map.m);

	config_init();
	parseconfig(path, fonf);
	if (configtest){
		puts("config OK");
		exit(0);
	}

	if (argc != 0)
		url = argv[0];

	setlocale(LC_ALL, "");

	TAILQ_INIT(&eecmd_history.head);
	TAILQ_INIT(&ir_history.head);
	TAILQ_INIT(&lu_history.head);

	ministate.line.type = LINE_TEXT;
	ministate.vline.parent = &ministate.line;
	ministate.buffer.current_line = &ministate.vline;

	/* initialize help window */
	TAILQ_INIT(&helpwin.head);

	base_map = &global_map;
	current_map = &global_map;

	initscr();

	if (enable_colors) {
		if (has_colors()) {
			start_color();
			use_default_colors();
		} else
			enable_colors = 0;
	}

	config_apply_style();

	raw();
	noecho();
	nonl();
	intrflush(stdscr, FALSE);

	if ((tabline = newwin(1, COLS, 0, 0)) == NULL)
		return 0;
	if ((body = newwin(LINES - 3, COLS, 1, 0)) == NULL)
		return 0;
	if ((modeline = newwin(1, COLS, LINES-2, 0)) == NULL)
		return 0;
	if ((minibuf = newwin(1, COLS, LINES-1, 0)) == NULL)
		return 0;
	if ((help = newwin(1, 1, 1, 0)) == NULL)
		return 0;

	body_lines = LINES-3;
	body_cols = COLS;

	wbkgd(body, body_face.body);
	wbkgd(minibuf, minibuffer_face.background);

	update_x_offset();

	keypad(body, TRUE);
	scrollok(body, FALSE);

	/* non-blocking input */
	wtimeout(body, 0);

	mvwprintw(body, 0, 0, "");

	event_set(&stdioev, 0, EV_READ | EV_PERSIST, dispatch_stdio, NULL);
	event_add(&stdioev, NULL);

	signal_set(&winchev, SIGWINCH, handle_resize, NULL);
	signal_add(&winchev, NULL);

	load_last_session(session_new_tab_cb);
	if (strcmp(url, NEW_TAB_URL) || TAILQ_EMPTY(&tabshead))
		new_tab(url);

	return 1;
}

void
ui_on_tab_loaded(struct tab *tab)
{
	stop_loading_anim(tab);
	message("Loaded %s", tab->hist_cur->h);

	redraw_tabline();
	wrefresh(tabline);
	if (in_minibuffer)
		wrefresh(minibuf);
	else
		wrefresh(body);
}

void
ui_on_tab_refresh(struct tab *tab)
{
	wrap_page(&tab->buffer, body_cols);
	if (tab->flags & TAB_CURRENT)
		redraw_tab(tab);
	else
		tab->flags |= TAB_URGENT;
}

const char *
ui_keyname(int k)
{
	return keyname(k);
}

void
ui_toggle_side_window(void)
{
	side_window = !side_window;
	if (side_window)
		recompute_help();

	/*
	 * ugly hack, but otherwise the window doesn't get updated
	 * until I call handle_resize a second time (i.e. C-l).  I
	 * will be happy to know why something like this is needed.
	 */
	handle_resize_nodelay(0, 0, NULL);
	handle_resize_nodelay(0, 0, NULL);
}

void
ui_schedule_redraw(void)
{
	handle_resize_nodelay(0, 0, NULL);
}

void
ui_require_input(struct tab *tab, int hide)
{
	/* TODO: hard-switching to another tab is ugly */
	switch_to_tab(tab);

	enter_minibuffer(ir_self_insert, ir_select, exit_minibuffer,
	    &ir_history);
	strlcpy(ministate.prompt, "Input required: ",
	    sizeof(ministate.prompt));
	redraw_tab(tab);
}

void
ui_yornp(const char *prompt, void (*fn)(int, unsigned int),
    unsigned int data)
{
	size_t len;

	if (in_minibuffer) {
		fn(0, data);
		return;
	}

	yornp_cb = fn;
	yornp_data = data;
	enter_minibuffer(yornp_self_insert, yornp_self_insert,
	    yornp_abort, NULL);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, prompt, len);
	strlcat(ministate.prompt, " (y or n) ", len);
	redraw_tab(current_tab());
}

void
ui_read(const char *prompt, void (*fn)(const char*, unsigned int),
    unsigned int data)
{
	size_t len;

	if (in_minibuffer)
		return;

	read_cb = fn;
	read_data = data;
	enter_minibuffer(read_self_insert, read_select, read_abort,
	    &read_history);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, prompt, len);
	strlcat(ministate.prompt, ": ", len);
	redraw_tab(current_tab());
}

void
ui_end(void)
{
	endwin();
}
