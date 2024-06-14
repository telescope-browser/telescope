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

#include "compat.h"

#include <sys/time.h>
#include <sys/wait.h>

#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd.h"
#include "defaults.h"
#include "ev.h"
#include "hist.h"
#include "keymap.h"
#include "mailcap.h"
#include "minibuffer.h"
#include "session.h"
#include "telescope.h"
#include "ui.h"
#include "utf8.h"

static void		 set_scroll_position(struct tab *, size_t, size_t);

static void		 restore_curs_x(struct buffer *);

static int		 readkey(void);
static void		 dispatch_stdio(int, int, void*);
static void		 handle_resize(int, int, void*);
static void		 handle_resize_nodelay(int, int, void*);
static void		 handle_download_refresh(int, int, void *);
static void		 rearrange_windows(void);
static void		 line_prefix_and_text(struct vline *, char *, size_t, const char **, const char **, int *);
static void		 print_vline(int, int, WINDOW*, struct vline*);
static void		 redraw_tabline(void);
static void		 redraw_window(WINDOW *, int, int, int, int, struct buffer *);
static void		 redraw_download(void);
static void		 redraw_help(void);
static void		 redraw_body(struct tab*);
static void		 redraw_modeline(struct tab*);
static void		 redraw_minibuffer(void);
static void		 do_redraw_echoarea(void);
static void		 do_redraw_minibuffer(void);
static void		 do_redraw_minibuffer_compl(void);
static void		 place_cursor(int);
static void		 redraw_tab(struct tab*);
static void		 update_loading_anim(int, int, void*);
static void		 stop_loading_anim(struct tab*);
static void 		 exec_external_cmd(char **);

static int		 should_rearrange_windows;
static int		 show_tab_bar;
static int		 too_small;
static int		 x_offset;

struct thiskey	 thiskey;
struct tab	*current_tab;

static unsigned int	resize_timer;
static struct timeval	resize_tv = { 0, 250000 };

static unsigned int	download_timer;
static struct timeval	download_refresh_timer = { 0, 250000 };

static WINDOW	*tabline, *body, *modeline, *echoarea, *minibuffer;

int			 body_lines, body_cols;

static WINDOW		*help;
/* not static so we can see them from help.c */
struct buffer		 helpwin;
int			 help_lines, help_cols;

static WINDOW		*download;
/* not static so we can see them from download.c */
struct buffer		 downloadwin;
int			 download_lines;
int			 download_cols;

static int		 side_window;
static int		 in_side_window;

static struct timeval	loading_tv = { 0, 250000 };

static char	keybuf[64];

/* XXX: don't forget to init these in main() */
struct kmap global_map,
	minibuffer_map,
	*current_map,
	*base_map;

static inline void
update_x_offset(void)
{
	if (olivetti_mode && fill_column < body_cols)
		x_offset = (body_cols - fill_column)/2;
	else
		x_offset = 0;
}

static void
set_scroll_position(struct tab *tab, size_t top, size_t cur)
{
	struct line *last;
	struct vline *vl;
	size_t i = 0;
	int topfound = 0;

	last = TAILQ_FIRST(&tab->buffer.head);
	TAILQ_FOREACH(vl, &tab->buffer.vhead, vlines) {
		if (last != vl->parent) {
			last = vl->parent;
			i++;
		}

		if (!topfound && i == top) {
			topfound = 1;
			tab->buffer.top_line = vl;
		}

		if (i == cur) {
			tab->buffer.current_line = vl;
			return;
		}
	}

	if (!topfound)
		tab->buffer.top_line = TAILQ_FIRST(&tab->buffer.vhead);

	tab->buffer.current_line = tab->buffer.top_line;
}

void
get_scroll_position(struct tab *tab, size_t *top, size_t *cur)
{
	struct line *l;
	int topfound = 0;

	*top = 0;
	*cur = 0;

	if (tab->buffer.top_line == NULL ||
	    tab->buffer.current_line == NULL)
		return;

	TAILQ_FOREACH(l, &tab->buffer.head, lines) {
		if (tab->buffer.top_line->parent == l)
			topfound = 1;
		if (tab->buffer.current_line->parent == l)
			return;

		if (!topfound)
			(*top)++;
		(*cur)++;
	}
}

void
save_excursion(struct excursion *place, struct buffer *buffer)
{
	place->curs_x = buffer->curs_x;
	place->curs_y = buffer->curs_y;
	place->line_off = buffer->line_off;
	place->top_line = buffer->top_line;
	place->current_line = buffer->current_line;
	place->cpoff = buffer->cpoff;
}

void
restore_excursion(struct excursion *place, struct buffer *buffer)
{
	buffer->curs_x = place->curs_x;
	buffer->curs_y = place->curs_y;
	buffer->line_off = place->line_off;
	buffer->top_line = place->top_line;
	buffer->current_line = place->current_line;
	buffer->cpoff = place->cpoff;
}

static void
restore_curs_x(struct buffer *buffer)
{
	struct vline	*vl;
	struct lineprefix *lp = line_prefixes;
	const char	*prfx, *text;

	if (dont_apply_styling)
		lp = raw_prefixes;

	vl = buffer->current_line;
	if (vl == NULL || vl->len == 0 || vl->parent == NULL)
		buffer->curs_x = buffer->cpoff = 0;
	else if (vl->parent->data != NULL) {
		text = vl->parent->data;
		buffer->curs_x = utf8_snwidth(text + 1, buffer->cpoff) + 1;
	} else {
		text = vl->parent->line + vl->from;
		buffer->curs_x = utf8_snwidth(text, buffer->cpoff);
	}

	/* small hack: don't olivetti-mode the download pane */
	if (buffer != &downloadwin)
		buffer->curs_x += x_offset;

	if (vl == NULL)
		return;

	if (vl->parent->data != NULL)
		buffer->curs_x += utf8_swidth_between(vl->parent->line,
		    vl->parent->data);
	else {
		prfx = lp[vl->parent->type].prfx1;
		buffer->curs_x += utf8_swidth(prfx);
	}
}

void
global_key_unbound(void)
{
	message("%s is undefined", keybuf);
}

struct buffer *
current_buffer(void)
{
	if (in_minibuffer)
		return &ministate.buffer;
	if (in_side_window & SIDE_WINDOW_LEFT)
		return &helpwin;
	if (in_side_window & SIDE_WINDOW_BOTTOM)
		return &downloadwin;
	return &current_tab->buffer;
}

static int
readkey(void)
{
	uint32_t state = 0;

	if ((thiskey.key = wgetch(body)) == ERR)
		return 0;

	thiskey.meta = thiskey.key == '\e';
	if (thiskey.meta) {
		thiskey.key = wgetch(body);
		if (thiskey.key == ERR || thiskey.key == '\e') {
			thiskey.meta = 0;
			thiskey.key = '\e';
		}
	}

	thiskey.cp = 0;

	if ((unsigned int)thiskey.key >= UINT8_MAX)
		return 1;

	while (1) {
		if (!utf8_decode(&state, &thiskey.cp, (uint8_t)thiskey.key))
			break;
		if ((thiskey.key = wgetch(body)) == ERR) {
			message("Error decoding user input");
			return 0;
		}
	}

	return 1;
}

static void
dispatch_stdio(int fd, int ev, void *d)
{
	int		 lk;
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
	} else if ((keyname = unkbd(thiskey.key)) != NULL) {
		strlcat(keybuf, keyname, sizeof(keybuf));
	} else {
		tmp[0] = thiskey.key;
		strlcat(keybuf, tmp, sizeof(keybuf));
	}

	lk = lookup_key(&current_map, &thiskey, current_buffer());
	if (lk == LK_UNBOUND) {
		if (current_map->unhandled_input != NULL)
			current_map->unhandled_input();
		else
			global_key_unbound();
	}
	if (lk != LK_ADVANCED_MAP) {
		current_map = base_map;
		strlcpy(keybuf, "", sizeof(keybuf));
	}

	if (side_window & SIDE_WINDOW_LEFT)
		recompute_help();

	if (should_rearrange_windows)
		rearrange_windows();
	redraw_tab(current_tab);
}

static void
handle_resize(int sig, int ev, void *d)
{
	ev_timer_cancel(resize_timer);
	resize_timer = ev_timer(&resize_tv, handle_resize_nodelay, NULL);
}

static void
handle_resize_nodelay(int s, int ev, void *d)
{
	endwin();
	refresh();
	clear();

	rearrange_windows();
}

static void
handle_download_refresh(int s, int v, void *d)
{
	if (side_window & SIDE_WINDOW_BOTTOM) {
		recompute_downloads();
		redraw_tab(current_tab);
	}
}

static inline int
should_show_tab_bar(void)
{
	if (tab_bar_show == -1)
		return 0;
	if (tab_bar_show == 0)
		return 1;

	return TAILQ_NEXT(TAILQ_FIRST(&tabshead), tabs) != NULL;
}

static void
rearrange_windows(void)
{
	int		 lines;
	int		 minibuffer_lines;

	should_rearrange_windows = 0;
	show_tab_bar = should_show_tab_bar();

	lines = LINES;

	/* 3 lines for the ui and 12 for the body and minibuffer  */
	if ((too_small = lines < 15)) {
		erase();
		printw("Window too small.");
		refresh();
		return;
	}

	/* move and resize the windows, in reverse order! */

	if (in_minibuffer == MB_COMPREAD) {
		minibuffer_lines = MIN(10, lines/2);
		mvwin(minibuffer, lines - minibuffer_lines, 0);
		wresize(minibuffer, minibuffer_lines, COLS);
		lines -= minibuffer_lines;

		wrap_page(&ministate.compl.buffer, COLS);
	}

	mvwin(echoarea, --lines, 0);
	wresize(echoarea, 1, COLS);

	mvwin(modeline, --lines, 0);
	wresize(modeline, 1, COLS);

	if (side_window & SIDE_WINDOW_BOTTOM) {
		download_lines = MIN(5, lines/2);
		download_cols = COLS;
		mvwin(download, lines - download_lines, 0);
		wresize(download, download_lines, download_cols);
		lines -= download_lines;

		wrap_page(&downloadwin, download_cols);
	}

	body_lines = show_tab_bar ? --lines : lines;
	body_cols = COLS;

	/*
	 * Here we make the assumption that show_tab_bar is either 0
	 * or 1, and reuse that as argument to mvwin.
	 */
	if (side_window & SIDE_WINDOW_LEFT) {
		help_cols = 0.3 * COLS;
		help_lines = lines;
		mvwin(help, show_tab_bar, 0);
		wresize(help, help_lines, help_cols);

		wrap_page(&helpwin, help_cols);

		body_cols = COLS - help_cols - 1;
		mvwin(body, show_tab_bar, help_cols);
	} else
		mvwin(body, show_tab_bar, 0);

	update_x_offset();
	wresize(body, body_lines, body_cols);

	if (show_tab_bar)
		wresize(tabline, 1, COLS);

	wrap_page(&current_tab->buffer, body_cols);
	redraw_tab(current_tab);
}

static void
line_prefix_and_text(struct vline *vl, char *buf, size_t len,
    const char **prfx_ret, const char **text_ret, int *text_len)
{
	struct lineprefix *lp = line_prefixes;
	int type, cont;
	size_t i, width;
	char *space, *t;

	if (dont_apply_styling)
		lp = raw_prefixes;

	if (vl->len == 0) {
		*text_ret = "";
		*text_len = 0;
	}

	cont = vl->flags & L_CONTINUATION;
	type = vl->parent->type;
	if (!cont)
		*prfx_ret = lp[type].prfx1;
	else
		*prfx_ret = lp[type].prfx2;

	space = vl->parent->data;
	*text_ret = vl->parent->line + vl->from;
	*text_len = MIN(INT_MAX, vl->len);
	if (!emojify_link || type != LINE_LINK || space == NULL) {
		return;
	}

	if (cont) {
		memset(buf, 0, len);
		width = utf8_swidth_between(vl->parent->line, space);
		for (i = 0; i < width + 1 && i < len - 1; ++i)
			buf[i] = ' ';
	} else {
		strlcpy(buf, vl->parent->line, len);
		if ((t = strchr(buf, ' ')) != NULL)
			*t = '\0';
		strlcat(buf, " ", len);
	}

	*prfx_ret = buf;
}

static inline void
print_vline_descr(int width, WINDOW *window, struct vline *vl)
{
	int x, y, goal;

	switch (vl->parent->type) {
	case LINE_COMPL:
	case LINE_COMPL_CURRENT:
		goal = width/2;
		break;
	case LINE_DOWNLOAD:
	case LINE_DOWNLOAD_DONE:
		goal = width/4;
		break;
	case LINE_HELP:
		goal = 8;
		break;
	default:
		return;
	}

	if (vl->parent->alt == NULL)
		return;

	(void)y;
	getyx(window, y, x);

	if (goal <= x)
		wprintw(window, " ");
	for (; goal > x; ++x)
		wprintw(window, " ");

	wprintw(window, "%s", vl->parent->alt);
}

/*
 * Core part of the rendering.  It prints a vline starting from the
 * current cursor position.  Printing a vline consists of skipping
 * `off' columns (for olivetti-mode), print the correct prefix (which
 * may be the emoji in case of emojified links-lines), printing the
 * text itself, filling until width - off and filling off columns
 * again.
 */
static void
print_vline(int off, int width, WINDOW *window, struct vline *vl)
{
	/*
	 * Believe me or not, I've seen emoji ten code points long!
	 * That means, to stay large, 4*10 bytes + NUL.
	 */
	char emojibuf[41] = {0};
	const char *text, *prfx;
	struct line_face *f;
	int i, left, x, y, textlen;

	f = &line_faces[vl->parent->type];

	/* unused, set by getyx */
	(void)y;

	if (vl->parent->type == LINE_FRINGE && fringe_ignore_offset)
		off = 0;

	line_prefix_and_text(vl, emojibuf, sizeof(emojibuf), &prfx,
	    &text, &textlen);

	wattr_on(window, body_face.left, NULL);
	for (i = 0; i < off; i++)
		waddch(window, ' ');
	wattr_off(window, body_face.left, NULL);

	wattr_on(window, f->prefix, NULL);
	wprintw(window, "%s", prfx);
	wattr_off(window, f->prefix, NULL);

	wattr_on(window, f->text, NULL);
	if (text)
		wprintw(window, "%.*s", textlen, text);
	print_vline_descr(width, window, vl);
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
	int		 current, y, truncated, pair;
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
		if (tab == current_tab)
			break;
	}

	if (toskip * tabwidth <= space)
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

		current = tab == current_tab;

		if (*(title = tab->buffer.title) == '\0')
			title = hist_cur(tab->hist);

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

		pair = current ? tab_face.current : tab_face.tab;
		wattr_on(tabline, pair, NULL);
		wprintw(tabline, "%s", buf);
		wattr_off(tabline, pair, NULL);

		wattr_on(tabline, tab_face.background, NULL);
		if (TAILQ_NEXT(tab, tabs) != NULL)
			wprintw(tabline, "┃");
		wattr_off(tabline, tab_face.background, NULL);
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
struct vline *
adjust_line(struct vline *vl, struct buffer *buffer)
{
	struct vline *t;

	if (vl == NULL)
		return NULL;

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
redraw_window(WINDOW *win, int off, int height, int width,
    int show_fringe, struct buffer *buffer)
{
	struct vline	*vl;
	int		 onscreen = 0, l = 0;

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

	if (buffer->top_line == NULL)
		buffer->top_line = TAILQ_FIRST(&buffer->vhead);

	buffer->top_line = adjust_line(buffer->top_line, buffer);
	if (buffer->top_line == NULL)
		goto end;

	buffer->current_line = adjust_line(buffer->current_line, buffer);

	for (vl = buffer->top_line; vl != NULL; vl = TAILQ_NEXT(vl, vlines)) {
		if (vl->parent->flags & L_HIDDEN)
			continue;

		wmove(win, l, 0);
		print_vline(off, width, win, vl);

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

		if (vl != NULL)
			goto again;
	}

	buffer->last_line_off = buffer->line_off;
	buffer->force_redraw = 0;
end:
	for (; show_fringe && l < height; l++)
		print_vline(off, width, win, &fringe);

	wmove(win, buffer->curs_y, buffer->curs_x);
}

static void
redraw_download(void)
{
	redraw_window(download, 0, download_lines, COLS, 0, &downloadwin);
}

static void
redraw_help(void)
{
	redraw_window(help, 0, help_lines, help_cols, 1, &helpwin);
}

static void
redraw_body(struct tab *tab)
{
	static struct tab *last_tab;

	if (last_tab != tab)
		tab->buffer.force_redraw =1;
	last_tab = tab;

	redraw_window(body, x_offset, body_lines, body_cols, 1, &tab->buffer);
}

static inline char
trust_status_char(enum trust_state ts)
{
	switch (ts) {
	case TS_UNKNOWN:	return '-';
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
	struct buffer	*buffer;
	double		 pct;
	int		 x, y, max_x, max_y;
	const char	*mode;
	const char	*spin = "-\\|/";

	buffer = current_buffer();
	mode = buffer->mode;

	werase(modeline);
	wattr_on(modeline, modeline_face.background, NULL);
	wmove(modeline, 0, 0);

	wprintw(modeline, "-%c%c%c%c %s ",
	    spin[tab->loading_anim_step],
	    trust_status_char(tab->trust),
	    tab->client_cert ? 'C' : '-',
	    tab->faulty_gemserver ? 'W' : '-',
	    mode == NULL ? "(none)" : mode);

	pct = (buffer->line_off + buffer->curs_y) * 100.0
		/ buffer->line_max;

	if (buffer->line_max <= (size_t)body_lines)
		wprintw(modeline, "All ");
	else if (buffer->line_off == 0)
		wprintw(modeline, "Top ");
	else if (buffer->line_off + body_lines >= buffer->line_max)
		wprintw(modeline, "Bottom ");
	else
		wprintw(modeline, "%.0f%% ", pct);

	wprintw(modeline, "%zu/%zu %s ",
	    buffer->line_off + buffer->curs_y,
	    buffer->line_max,
	    hist_cur(tab->hist));

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

	if (in_minibuffer == MB_COMPREAD)
		do_redraw_minibuffer_compl();

	wattr_off(echoarea, minibuffer_face.background, NULL);
}

static void
do_redraw_echoarea(void)
{
	struct vline *vl;

	if (ministate.curmesg != NULL)
		wprintw(echoarea, "%s", ministate.curmesg);
	else if (*keybuf != '\0')
		waddstr(echoarea, keybuf);
	else {
		/* If nothing else, show the URL at point */
		vl = current_tab->buffer.current_line;
		if (vl != NULL && vl->parent->type == LINE_LINK)
			waddstr(echoarea, vl->parent->alt);
	}
}

static void
do_redraw_minibuffer(void)
{
	struct buffer	*cmplbuf, *buffer;
	size_t		 off_y, off_x = 0;
	const char	*start, *c;
	char		*line;

	cmplbuf = &ministate.compl.buffer;
	buffer = &ministate.buffer;
	(void)off_y;		/* unused, set by getyx */

	wmove(echoarea, 0, 0);

	if (in_minibuffer == MB_COMPREAD)
		wprintw(echoarea, "(%2zu) ",
		    cmplbuf->line_max);

	wprintw(echoarea, "%s", ministate.prompt);
	if (!ministate.editing)
		wprintw(echoarea, "(%zu/%zu) ",
		    hist_off(ministate.hist) + 1,
		    hist_size(ministate.hist));

	getyx(echoarea, off_y, off_x);

	start = ministate.buf;
	if (!ministate.editing)
		start = hist_cur(ministate.hist);
	line = buffer->current_line->parent->line + buffer->current_line->from;
	c = utf8_nth(line, buffer->cpoff);
	while (utf8_swidth_between(start, c) > (size_t)COLS/2) {
		start = utf8_next_cp(start);
	}

	waddstr(echoarea, start);

	if (ministate.curmesg != NULL)
		wprintw(echoarea, " [%s]", ministate.curmesg);

	wmove(echoarea, 0, off_x + utf8_swidth_between(start, c));
}

static void
do_redraw_minibuffer_compl(void)
{
	redraw_window(minibuffer, 0, 10, COLS, 1,
	    &ministate.compl.buffer);
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
		if (side_window & SIDE_WINDOW_LEFT)
			touch(help);
		if (side_window & SIDE_WINDOW_BOTTOM)
			touch(download);
		touch(body);
		touch(echoarea);
	} else if (in_side_window & SIDE_WINDOW_LEFT) {
		touch(body);
		touch(echoarea);
                if (in_side_window & SIDE_WINDOW_BOTTOM)
			touch(download);
		touch(help);
	} else if (in_side_window & SIDE_WINDOW_BOTTOM) {
		touch(body);
		touch(echoarea);
		if (in_side_window & SIDE_WINDOW_LEFT)
			touch(help);
		touch(download);
	} else {
		if (side_window & SIDE_WINDOW_LEFT)
			touch(help);
		if (side_window & SIDE_WINDOW_BOTTOM)
			touch(download);
		touch(echoarea);
		touch(body);
	}
}

static void
redraw_tab(struct tab *tab)
{
	if (too_small)
		return;

	if (side_window & SIDE_WINDOW_LEFT) {
		redraw_help();
		wnoutrefresh(help);
	}

	if (side_window & SIDE_WINDOW_BOTTOM) {
		redraw_download();
		wnoutrefresh(download);
	}

	if (show_tab_bar)
		redraw_tabline();

	redraw_body(tab);
	redraw_modeline(tab);
	redraw_minibuffer();

	wnoutrefresh(tabline);
	wnoutrefresh(modeline);

	if (in_minibuffer == MB_COMPREAD)
		wnoutrefresh(minibuffer);

	place_cursor(1);

	doupdate();

	if (set_title)
		dprintf(1, "\033]2;%s - Telescope\a",
		    current_tab->buffer.title);
}

void
start_loading_anim(struct tab *tab)
{
	if (tab->loading_anim)
		return;
	tab->loading_anim = 1;

	ev_timer_cancel(tab->loading_timer);
	tab->loading_timer = ev_timer(&loading_tv, update_loading_anim, tab);
}

static void
update_loading_anim(int fd, int ev, void *d)
{
	struct tab	*tab = d;

	tab->loading_anim_step = (tab->loading_anim_step+1)%4;

	if (tab == current_tab) {
		redraw_modeline(tab);
		wrefresh(modeline);
		wrefresh(body);
		if (in_minibuffer)
			wrefresh(echoarea);
	}

	tab->loading_timer = ev_timer(&loading_tv, update_loading_anim, tab);
}

static void
stop_loading_anim(struct tab *tab)
{
	if (!tab->loading_anim)
		return;

	ev_timer_cancel(tab->loading_timer);
	tab->loading_anim = 0;
	tab->loading_anim_step = 0;

	if (tab != current_tab)
		return;

	redraw_modeline(tab);

	wrefresh(modeline);
	wrefresh(body);
	if (in_minibuffer)
		wrefresh(echoarea);
}

int
ui_init(void)
{
	setlocale(LC_ALL, "");

	if (TAILQ_EMPTY(&global_map.m)) {
		fprintf(stderr, "no keys defined!\n");
		return 0;
	}

	minibuffer_init();

	/* initialize download window */
	TAILQ_INIT(&downloadwin.head);
	TAILQ_INIT(&downloadwin.vhead);

	/* initialize help window */
	TAILQ_INIT(&helpwin.head);
	TAILQ_INIT(&helpwin.vhead);

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

	if ((tabline = newwin(1, 1, 0, 0)) == NULL)
		return 0;
	if ((body = newwin(1, 1, 0, 0)) == NULL)
		return 0;
	if ((modeline = newwin(1, 1, 0, 0)) == NULL)
		return 0;
	if ((echoarea = newwin(1, 1, 0, 0)) == NULL)
		return 0;
	if ((minibuffer = newwin(1, 1, 0, 0)) == NULL)
		return 0;
	if ((download = newwin(1, 1, 0, 0)) == NULL)
		return 0;
	if ((help = newwin(1, 1, 0, 0)) == NULL)
		return 0;

	wbkgd(body, body_face.body);
	wbkgd(download, download_face.background);
	wbkgd(echoarea, minibuffer_face.background);

	update_x_offset();

	keypad(body, TRUE);
	scrollok(body, FALSE);

	/* non-blocking input */
	wtimeout(body, 0);
	wtimeout(help, 0);

	wmove(body, 0, 0);

	return 1;
}

void
ui_main_loop(void)
{
	if (ev_signal(SIGWINCH, handle_resize, NULL) == -1 ||
	    ev_add(0, EV_READ, dispatch_stdio, NULL) == -1)
		err(1, "ev_signal or ev_add failed");

	switch_to_tab(current_tab);
	rearrange_windows();

	ev_loop();
}

void
ui_on_tab_loaded(struct tab *tab)
{
	size_t line_off, curr_off;

	stop_loading_anim(tab);
	message("Loaded %s", hist_cur(tab->hist));

	hist_cur_offs(tab->hist, &line_off, &curr_off);
	if (curr_off != 0 &&
	    tab->buffer.current_line == TAILQ_FIRST(&tab->buffer.vhead)) {
		set_scroll_position(tab, line_off, curr_off);
		redraw_tab(tab);
		return;
	}

	if (show_tab_bar)
		redraw_tabline();

	wrefresh(tabline);
	place_cursor(0);
}

void
ui_on_tab_refresh(struct tab *tab)
{
	wrap_page(&tab->buffer, body_cols);
	if (tab == current_tab)
		redraw_tab(tab);
	else
		tab->flags |= TAB_URGENT;
}

void
ui_on_download_refresh(void)
{
	if (ev_timer_pending(download_timer))
		return;

	download_timer = ev_timer(&download_refresh_timer,
	    handle_download_refresh, NULL);
}

void
ui_prompt_download_cmd(char *path, char *mime_type)
{
	struct mailcap 	*mc = NULL;

	if ((mc = mailcap_cmd_from_mimetype(mime_type, path)) == NULL)
		return;

	message("Loaded %s with %s", mime_type, mc->cmd_argv[0]);
	exec_external_cmd(mc->cmd_argv);
}

void
ui_remotely_open_link(const char *uri)
{
	new_tab(uri, NULL, NULL);
	ui_on_tab_refresh(current_tab);

	/* ring the bell */
	printf("\a");
	fflush(stdout);
}

const char *
ui_keyname(int k)
{
	return keyname(k);
}

void
ui_toggle_side_window(int kind)
{
	if (in_side_window & kind)
		ui_other_window();

	side_window ^= kind;
	if (side_window & SIDE_WINDOW_LEFT)
		recompute_help();
	if (side_window & SIDE_WINDOW_BOTTOM)
		recompute_downloads();

	/*
	 * ugly hack, but otherwise the window doesn't get updated
	 * until I call rearrange_windows a second time (e.g. via
	 * C-l).  I will be happy to know why something like this is
	 * needed.
	 */
	rearrange_windows();
	rearrange_windows();
}

void
ui_show_downloads_pane(void)
{
	if (!(side_window & SIDE_WINDOW_BOTTOM))
		ui_toggle_side_window(SIDE_WINDOW_BOTTOM);
}

void
ui_schedule_redraw(void)
{
	should_rearrange_windows = 1;
}

void
ui_require_input(struct tab *tab, int hide, void (*fn)(const char *))
{
	/* TODO: hard-switching to another tab is ugly */
	switch_to_tab(tab);

	enter_minibuffer(sensible_self_insert, fn, exit_minibuffer,
	    ir_history, NULL, NULL, 0);
	strlcpy(ministate.prompt, "Input required: ",
	    sizeof(ministate.prompt));
	redraw_tab(tab);
}

void
ui_after_message_hook(void)
{
	redraw_minibuffer();
	place_cursor(0);
}

void
ui_yornp(const char *prompt, void (*fn)(int, struct tab *),
    struct tab *data)
{
	yornp(prompt, fn, data);
	redraw_tab(current_tab);
}

void
ui_read(const char *prompt, void (*fn)(const char*, struct tab *),
    struct tab *data, const char *input)
{
	minibuffer_read(prompt, fn, data);

	if (input != NULL) {
		strlcpy(ministate.buf, input, sizeof(ministate.buf));
		cmd_move_end_of_line(&ministate.buffer);
	}

	redraw_tab(current_tab);
}

void
ui_other_window(void)
{
	if (in_side_window & SIDE_WINDOW_LEFT &&
	    side_window & SIDE_WINDOW_BOTTOM)
		in_side_window = SIDE_WINDOW_BOTTOM;
	else if (in_side_window)
		in_side_window = 0;
	else if (!in_side_window && side_window & SIDE_WINDOW_LEFT)
		in_side_window = SIDE_WINDOW_LEFT;
	else if (!in_side_window && side_window)
		in_side_window = SIDE_WINDOW_BOTTOM;
	else
		message("No other window to select");
}

void
ui_suspend(void)
{
	endwin();

	kill(getpid(), SIGSTOP);

	refresh();
	clear();
	rearrange_windows();
}

void
ui_end(void)
{
	endwin();
}

static void
exec_external_cmd(char **argv)
{
	int 	 s;
	pid_t 	 p;

	if (argv == NULL)
		return;

	endwin();

	switch (p = fork()) {
	case -1:
		message("failed to fork: %s", strerror(errno));
		return;
	case 0:
		execvp(argv[0], argv);
		warn("execve failed");
		_exit(1);
	}

again:
	if (waitpid(p, &s, 0) == -1) {
		if (errno == EINTR)
			goto again;
	}

	refresh();
	clear();
	ui_schedule_redraw();
}

#define TMPFILE "/tmp/telescope.XXXXXXXXXX"

void
ui_edit_externally(void)
{
	FILE		*fp;
	char		 buf[1024 + 1];
	size_t		 r, len = 0;
	const char	*editor;
	char		 sfn[sizeof(TMPFILE)];
	pid_t		 pid;
	int		 fd, ret, s;

	if (!in_minibuffer) {
		message("Not in minibuffer!");
		return;
	}

	if (ministate.compl.must_select || ministate.donefn == NULL) {
		message("Can't use an external editor to complete");
		return;
	}

	strlcpy(sfn, TMPFILE, sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1) {
		message("failed to create a temp file: %s", strerror(errno));
		return;
	}
	(void) write(fd, ministate.buf, strlen(ministate.buf));
	close(fd);

	if ((editor = getenv("VISUAL")) == NULL &&
	    (editor = getenv("EDITOR")) == NULL)
		editor = DEFAULT_EDITOR;

	endwin();
	fprintf(stderr, "%s: running %s %s\n", getprogname(), editor, sfn);
	fflush(NULL);

	switch (pid = fork()) {
	case -1:
		message("failed to fork: %s", strerror(errno));
		(void) unlink(sfn);
		return;
	case 0:
		execlp(editor, editor, sfn, NULL);
		warn("exec \"%s\" failed", editor);
		fprintf(stderr, "Press enter to continue");
		fflush(stderr);
		read(0, &s, 1);
		_exit(1);
	}

	do {
		ret = waitpid(pid, &s, 0);
	} while (ret == -1 && errno == EINTR);

	refresh();
	clear();
	ui_schedule_redraw();

	if (WIFSIGNALED(s) || WEXITSTATUS(s) != 0) {
		message("%s failed", editor);
		(void) unlink(sfn);
		return;
	}

	if ((fp = fopen(sfn, "r")) == NULL) {
		message("can't open temp file!");
		(void) unlink(sfn);
		return;
	}
	(void) unlink(sfn);

	while (len < sizeof(buf) - 1) {
		r = fread(buf + len, 1, sizeof(buf) - 1 - len, fp);
		len += r;
		if (r == 0)
			break;
	}
	buf[len] = '\0';
	while (len > 0 && buf[len-1] == '\n')
		buf[--len] = '\0';

	ministate.donefn(buf);
	exit_minibuffer();
}
