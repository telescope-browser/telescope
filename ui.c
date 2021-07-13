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

#include <assert.h>
#include <curses.h>
#include <event.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defaults.h"
#include "minibuffer.h"
#include "telescope.h"
#include "ui.h"
#include "utf8.h"

static struct event	stdioev, winchev;

static void		 restore_curs_x(struct buffer *);

static struct vline	*nth_line(struct buffer*, size_t);
static int		 readkey(void);
static void		 dispatch_stdio(int, short, void*);
static void		 handle_clear_echoarea(int, short, void*);
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
static void		 do_redraw_echoarea(void);
static void		 do_redraw_minibuffer(void);
static void		 place_cursor(int);
static void		 redraw_tab(struct tab*);
static void		 emit_help_item(char*, void*);
static void		 rec_compute_help(struct kmap*, char*, size_t);
static void		 recompute_help(void);
static void		 update_loading_anim(int, short, void*);
static void		 stop_loading_anim(struct tab*);

static int		 too_small;
static int		 x_offset;

struct thiskey thiskey;

static struct event	resizeev;
static struct timeval	resize_timer = { 0, 250000 };

static WINDOW	*tabline, *body, *modeline, *echoarea, *minibuffer;

int			 body_lines, body_cols;

static WINDOW		*help;
static struct buffer	 helpwin;
static int		 help_lines, help_cols;

static int		 side_window;

static struct event	clechoev;
static struct timeval	clechoev_timer = { 5, 0 };
static struct timeval	loadingev_timer = { 0, 250000 };

static uint32_t		 tab_counter;

static char	keybuf[64];

struct kmap global_map,
	minibuffer_map,
	*current_map,
	*base_map;

int in_minibuffer;

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

void
global_key_unbound(void)
{
	message("%s is undefined", keybuf);
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

	/* TODO: schedule a redraw? */
	if (too_small)
		return;

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
handle_clear_echoarea(int fd, short ev, void *d)
{
	free(ministate.curmesg);
	ministate.curmesg = NULL;

	redraw_minibuffer();
	place_cursor(0);
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
	int		 lines;

	endwin();
	refresh();
	clear();

	lines = LINES;

	if ((too_small = lines < 15)) {
		erase();
		printw("Window too small.");
		refresh();
		return;
	}

	/* move and resize the windows, in reverse order! */

	mvwin(echoarea, --lines, 0);
	wresize(echoarea, 1, COLS);

	mvwin(modeline, --lines, 0);
	wresize(modeline, 1, COLS);

	body_lines = --lines;
	body_cols = COLS;

	if (side_window) {
		help_cols = 0.3 * COLS;
		help_lines = lines;
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
	for (i = 0; i < left - off; ++i)
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

/*
 * Compute the first visible line around vl.  Try to search forward
 * until the end of the buffer; if a visible line is not found, search
 * backward.  Return NULL if no viable line was found.
 */
static inline struct vline *
adjust_line(struct vline *vl, struct buffer *buffer)
{
	struct vline *t;

	if (!(vl->parent->flags & L_HIDDEN))
		return vl;

	/* search forward */
	for (t = vl;
	     t != NULL && t->parent->flags & L_HIDDEN;
	     t = TAILQ_NEXT(t, vlines))
		;		/* nop */

	if (t != NULL)
		return t;

	/* search backward */
	for (t = vl;
	     t != NULL && t->parent->flags & L_HIDDEN;
	     t = TAILQ_PREV(t, vhead, vlines))
		;		/* nop */

	return t;
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

	buffer->top_line = adjust_line(buffer->top_line, buffer);
	if (buffer->top_line == NULL)
		goto end;

	buffer->current_line = adjust_line(buffer->current_line, buffer);

	l = 0;
	onscreen = 0;
	for (vl = buffer->top_line; vl != NULL; vl = TAILQ_NEXT(vl, vlines)) {
		if (vl->parent->flags & L_HIDDEN)
			continue;

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
			if (vl->parent->flags & L_HIDDEN)
				continue;
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
	case TS_TEMP_TRUSTED:	return '!';
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
	wattr_on(echoarea, minibuffer_face.background, NULL);
	werase(echoarea);

	if (in_minibuffer)
		do_redraw_minibuffer();
	else
		do_redraw_echoarea();

	wattr_off(echoarea, minibuffer_face.background, NULL);
}

static void
do_redraw_echoarea(void)
{
	struct tab *tab;

	if (ministate.curmesg != NULL)
                wprintw(echoarea, "%s", ministate.curmesg);
	else if (*keybuf != '\0')
		waddstr(echoarea, keybuf);
	else {
		/* If nothing else, show the URL at point */
		tab = current_tab();
		if (tab->buffer.current_line != NULL &&
		    tab->buffer.current_line->parent->type == LINE_LINK)
			waddstr(echoarea, tab->buffer.current_line->parent->alt);
	}
}

static void
do_redraw_minibuffer(void)
{
	size_t		 off_y, off_x = 0;
	const char	*start, *c;

	/* unused, set by getyx */
	(void)off_y;

	mvwprintw(echoarea, 0, 0, "%s", ministate.prompt);
	if (ministate.hist_cur != NULL)
		wprintw(echoarea, "(%zu/%zu) ",
		    ministate.hist_off + 1,
		    ministate.history->len);

	getyx(echoarea, off_y, off_x);

	start = ministate.hist_cur != NULL
		? ministate.hist_cur->h
		: ministate.buf;
	c = utf8_nth(ministate.buffer.current_line->line,
	    ministate.buffer.cpoff);
	while (utf8_swidth_between(start, c) > (size_t)COLS/2) {
		start = utf8_next_cp(start);
	}

	waddstr(echoarea, start);

	if (ministate.curmesg != NULL)
		wprintw(echoarea, " [%s]", ministate.curmesg);

	wmove(echoarea, 0, off_x + utf8_swidth_between(start, c));
}

/*
 * Place the cursor in the right ncurses window.  If soft is 1, use
 * wnoutrefresh (which shouldn't cause any I/O); otherwise use
 * wrefresh.
 */
static void
place_cursor(int soft)
{
	int (*touch)(WINDOW *);

	if (soft)
		touch = wnoutrefresh;
	else
		touch = wrefresh;

	if (in_minibuffer) {
		touch(body);
		touch(echoarea);
	} else {
		touch(echoarea);
		touch(body);
	}
}

static void
redraw_tab(struct tab *tab)
{
	if (too_small)
		return;

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

	place_cursor(1);

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

	redraw_minibuffer();
        place_cursor(0);
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
			wrefresh(echoarea);
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
		wrefresh(echoarea);
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

int
ui_init()
{
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
	if ((echoarea = newwin(1, COLS, LINES-1, 0)) == NULL)
		return 0;
	if ((minibuffer = newwin(1, COLS, LINES-1, 0)) == NULL)
		return 0;
	if ((help = newwin(1, 1, 1, 0)) == NULL)
		return 0;

	body_lines = LINES-3;
	body_cols = COLS;

	wbkgd(body, body_face.body);
	wbkgd(echoarea, minibuffer_face.background);

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

	return 1;
}

void
ui_on_tab_loaded(struct tab *tab)
{
	stop_loading_anim(tab);
	message("Loaded %s", tab->hist_cur->h);

	redraw_tabline();
	wrefresh(tabline);
	place_cursor(0);
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
	    &ir_history, NULL, NULL);
	strlcpy(ministate.prompt, "Input required: ",
	    sizeof(ministate.prompt));
	redraw_tab(tab);
}

void
ui_yornp(const char *prompt, void (*fn)(int, struct tab *),
    struct tab *data)
{
	yornp(prompt, fn, data);
	redraw_tab(current_tab());
}

void
ui_read(const char *prompt, void (*fn)(const char*, struct tab *),
    struct tab *data)
{
	completing_read(prompt, fn, data, NULL, NULL);
	redraw_tab(current_tab());
}

void
ui_end(void)
{
	endwin();
}
