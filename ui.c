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
 *
 * Text wrapping
 * =============
 *
 * There's a simple text wrapping algorithm.
 *
 * 1. if it's a line in a pre-formatted block:
 *    a. hard wrap.
 *    b. repeat
 * 2. there is enough room for the next word?
 *    a. yes: render it
 *    b. no: break the current line.
 *       i.  while there isn't enough space to draw the current
 *           word, hard-wrap it
 *       ii. draw the remainder of the current word (can be the
 *           the entirely)
 * 3. render the spaces after the word
 *    a. but if there is not enough room break the line and
 *       forget them
 * 4. repeat
 *
 *
 * Text scrolling
 * ==============
 *
 * ncurses allows you to scroll a window, but when a line goes out of
 * the visible area it's forgotten.  We keep a list of formatted lines
 * (``visual lines'') that we know fits in the window, and draw them.
 * This way is easy to scroll: just call wscrl and then render the
 * first/last line!
 *
 * This means that on every resize we have to clear our list of lines
 * and re-render everything.  A clever approach would be to do this
 * ``on-demand''.
 *
 * TODO: make the text formatting on-demand.
 *
 */

#include <telescope.h>

#include <ctype.h>
#include <curses.h>
#include <event.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TAB_CURRENT	0x1

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct kmap;

static struct event	stdioev, winchev;

static int		 kbd(const char*);
static void		 kmap_define_key(struct kmap*, const char*, void(*)(struct tab*));
static void		 load_default_keys(void);
static int		 push_line(struct tab*, const struct line*, const char*, size_t);
static void		 empty_vlist(struct tab*);
static void		 restore_cursor(struct tab *);
static void		 cmd_previous_line(struct tab*);
static void		 cmd_next_line(struct tab*);
static void		 cmd_forward_char(struct tab*);
static void		 cmd_backward_char(struct tab*);
static void		 cmd_redraw(struct tab*);
static void		 cmd_scroll_down(struct tab*);
static void		 cmd_scroll_up(struct tab*);
static void		 cmd_kill_telescope(struct tab*);
static void		 cmd_push_button(struct tab*);
static struct line	*nth_line(struct tab*, size_t);
static struct tab	*current_tab(void);
static void		 dispatch_stdio(int, short, void*);
static void		 handle_clear_minibuf(int, short, void*);
static void		 handle_resize(int, short, void*);
static int		 word_bourdaries(const char*, const char*, const char**, const char**);
static void		 wrap_text(struct tab*, const char*, struct line*);
static int		 hardwrap_text(struct tab*, struct line*);
static int		 wrap_page(struct tab*);
static void		 print_line(struct line*);
static void		 redraw_tabline(void);
static void		 redraw_modeline(struct tab*);
static void		 redraw_tab(struct tab*);
static void		 message(const char*, ...) __attribute__((format(printf, 1, 2)));
static void		 start_loading_anim(struct tab*);
static void		 update_loading_anim(int, short, void*);
static void		 stop_loading_anim(struct tab*);
static void		 load_url_in_tab(struct tab*, const char*);
static void		 new_tab(void);

static WINDOW	*tabline, *body, *modeline, *minibuf;
static int	 body_lines, body_cols;

static struct event	clminibufev;
static int		clminibufev_set;
static struct timeval	clminibufev_timer = { 5, 0 };
static struct timeval	loadingev_timer = { 0, 250000 };

static uint32_t		 tab_counter;

struct ui_state {
	int			curs_x;
	int			curs_y;
	size_t			line_off;
	size_t			line_max;

	short			loading_anim;
	short			loading_anim_step;
	struct event		loadingev;

	TAILQ_HEAD(, line)	head;
};

#define CTRL(n)	((n)&0x1F)

static TAILQ_HEAD(kmap, keymap) global_map, *current_map;
struct keymap {
	int			 meta;
	int			 key;
	struct kmap		 map;
	void			(*fn)(struct tab*);

	TAILQ_ENTRY(keymap)	 keymaps;
};

static int
kbd(const char *key)
{
	struct table {
		char	*p;
		int	 k;
	} table[] = {
		{ "<up>",	KEY_UP },
		{ "<down>",	KEY_DOWN },
		{ "<left>",	KEY_LEFT },
		{ "<right>",	KEY_RIGHT },
		/* ... */
		{ "space",	' ' },
		{ "spc",	' ' },
		{ "enter",	CTRL('m') },
		{ "tab",	CTRL('i') },
		/* ... */
		{ NULL, 0 },
	}, *t;

	for (t = table; t->p != NULL; ++t) {
		if (has_prefix(key, t->p))
			return t->k;
	}

        return *key;
}

static void
kmap_define_key(struct kmap *map, const char *key, void (*fn)(struct tab*))
{
	int ctrl, meta, k;
	struct keymap	*entry;

again:
	if ((ctrl = has_prefix(key, "C-")))
		key += 2;
	if ((meta = has_prefix(key, "M-")))
		key += 2;
	if (*key == '\0')
		_exit(1);
	k = kbd(key);

	if (ctrl)
		k = CTRL(k);

	/* skip key & spaces */
	while (*key != '\0' && !isspace(*key))
		++key;
	while (*key != '\0' && isspace(*key))
		++key;

	TAILQ_FOREACH(entry, map, keymaps) {
		if (entry->meta == meta && entry->key == k) {
			if (*key == '\0') {
				entry->fn = fn;
				return;
			}
			map = &entry->map;
			goto again;
		}
	}

	if ((entry = calloc(1, sizeof(*entry))) == NULL)
		abort();

	entry->meta = meta;
	entry->key = k;
	TAILQ_INIT(&entry->map);

	if (TAILQ_EMPTY(map))
		TAILQ_INSERT_HEAD(map, entry, keymaps);
	else
		TAILQ_INSERT_TAIL(map, entry, keymaps);

        if (*key != '\0') {
		map = &entry->map;
		goto again;
	}

	entry->fn = fn;
}

static inline void
global_set_key(const char *key, void (*fn)(struct tab*))
{
	kmap_define_key(&global_map, key, fn);
}

static void
load_default_keys(void)
{
	/* emacs */
	global_set_key("C-p",		cmd_previous_line);
	global_set_key("C-n",		cmd_next_line);
	global_set_key("C-f",		cmd_forward_char);
	global_set_key("C-b",		cmd_backward_char);

	/* tmp */
	global_set_key("M-v",		cmd_scroll_up);
	global_set_key("C-v",		cmd_scroll_down);

	global_set_key("C-x C-c",	cmd_kill_telescope);

	/* vi/vi-like */
	global_set_key("k",		cmd_previous_line);
	global_set_key("j",		cmd_next_line);
	global_set_key("l",		cmd_forward_char);
	global_set_key("h",		cmd_backward_char);

	global_set_key("K",		cmd_scroll_up);
	global_set_key("J",		cmd_scroll_down);

	/* tmp */
	global_set_key("q",		cmd_kill_telescope);

	/* cua */
	global_set_key("<up>",		cmd_previous_line);
	global_set_key("<down>",	cmd_next_line);
	global_set_key("<right>",	cmd_forward_char);
	global_set_key("<left>",	cmd_backward_char);

	/* "ncurses standard" */
	global_set_key("C-l",		cmd_redraw);

	/* global */
	global_set_key("C-m",		cmd_push_button);
}

static int
push_line(struct tab *tab, const struct line *l, const char *buf, size_t len)
{
	struct line *vl;

	tab->s->line_max++;

	if ((vl = calloc(1, sizeof(*vl))) == NULL)
		return 0;

	if (len != 0 && (vl->line = calloc(1, len+1)) == NULL) {
		free(vl);
		return 0;
	}

	vl->type = l->type;
	if (len != 0)
		memcpy(vl->line, buf, len);
	vl->alt = l->alt;

	if (TAILQ_EMPTY(&tab->s->head))
		TAILQ_INSERT_HEAD(&tab->s->head, vl, lines);
	else
		TAILQ_INSERT_TAIL(&tab->s->head, vl, lines);
	return 1;
}

static void
empty_vlist(struct tab *tab)
{
	struct line *l, *t;

	tab->s->line_max = 0;

	TAILQ_FOREACH_SAFE(l, &tab->s->head, lines, t) {
		TAILQ_REMOVE(&tab->s->head, l, lines);
		free(l->line);
		/* l->alt references the original line! */
		free(l);
	}
}

static void
restore_cursor(struct tab *tab)
{
	wmove(body, tab->s->curs_y, tab->s->curs_x);
}

static void
cmd_previous_line(struct tab *tab)
{
	if (--tab->s->curs_y < 0) {
		tab->s->curs_y = 0;
		cmd_scroll_up(tab);
	}

	restore_cursor(tab);
}

static void
cmd_next_line(struct tab *tab)
{
	if (++tab->s->curs_y > body_lines-1) {
		tab->s->curs_y = body_lines-1;
		cmd_scroll_down(tab);
	}

	restore_cursor(tab);
}

static void
cmd_forward_char(struct tab *tab)
{
	tab->s->curs_x = MIN(body_cols-1, tab->s->curs_x+1);
	restore_cursor(tab);
}

static void
cmd_backward_char(struct tab *tab)
{
	tab->s->curs_x = MAX(0, tab->s->curs_x-1);
	restore_cursor(tab);
}

static void
cmd_redraw(struct tab *tab)
{
	handle_resize(0, 0, NULL);
}

static void
cmd_scroll_up(struct tab *tab)
{
	struct line	*l;

	if (tab->s->line_off == 0)
		return;

	l = nth_line(tab, --tab->s->line_off);
	wscrl(body, -1);
	wmove(body, 0, 0);
	print_line(l);
}

static void
cmd_scroll_down(struct tab *tab)
{
	struct line	*l;
	size_t		 n;

	if (tab->s->line_max == 0 || tab->s->line_off == tab->s->line_max-1)
		return;

	tab->s->line_off++;
	wscrl(body, 1);

	if (tab->s->line_max - tab->s->line_off < body_lines)
		return;

	l = nth_line(tab, tab->s->line_off + body_lines-1);
	wmove(body, body_lines-1, 0);
	print_line(l);
}

static void
cmd_kill_telescope(struct tab *tab)
{
	event_loopbreak();
}

static void
cmd_push_button(struct tab *tab)
{
	struct line	*l;
	size_t		 nth;

	nth = tab->s->line_off + tab->s->curs_y;
	if (nth > tab->s->line_max)
		return;
	l = nth_line(tab, nth);
	if (l->type != LINE_LINK)
		return;

	load_url_in_tab(tab, l->alt);
}

static struct line *
nth_line(struct tab *tab, size_t n)
{
	struct line	*l;
	size_t		 i;

	i = 0;
	TAILQ_FOREACH(l, &tab->s->head, lines) {
		if (i == n)
			return l;
		i++;
	}

	/* unreachable */
	abort();
}

static struct tab *
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

static void
dispatch_stdio(int fd, short ev, void *d)
{
	struct keymap	*k;
	int		 meta, key;

	key = wgetch(body);
	if (key == ERR)
		return;
	if (key == 27) {
		/* TODO: make escape-time customizable */

		meta = 1;
		key = wgetch(body);
		if (key == ERR)
			key = 27;
	} else
		meta = 0;

	TAILQ_FOREACH(k, current_map, keymaps) {
		if (k->meta == meta && k->key == key) {
			if (k->fn == NULL)
				current_map = &k->map;
			else {
				current_map = &global_map;
				k->fn(current_tab());
			}
			goto done;
		}
	}

	current_map = &global_map;

	message("%s%c is undefined",
	    meta ? "M-" : "", key);

done:
	restore_cursor(current_tab());
	wrefresh(tabline);
	wrefresh(modeline);
	wrefresh(minibuf);

	wrefresh(body);
}

static void
handle_clear_minibuf(int fd, short ev, void *d)
{
	clminibufev_set = 0;
	werase(minibuf);
	wrefresh(minibuf);
	wrefresh(body);
}

static void
handle_resize(int sig, short ev, void *d)
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

	wresize(body, LINES-3, COLS);
	body_lines = LINES-3;
	body_cols = COLS;

	wresize(tabline, 1, COLS);

	tab = current_tab();

	wrap_page(tab);
	redraw_tab(tab);
}

/*
 * Helper function for wrap_text.  Find the end of the current word
 * and the end of the separator after the word.
 */
static int
word_boundaries(const char *s, const char *sep, const char **endword, const char **endspc)
{
	*endword = s;
	*endword = s;

	if (*s == '\0')
		return 0;

	/* find the end of the current world */
	for (; *s != '\0'; ++s) {
		if (strchr(sep, *s) != NULL)
			break;
	}

	*endword = s;

	/* find the end of the separator */
	for (; *s != '\0'; ++s) {
		if (strchr(sep, *s) == NULL)
			break;
	}

	*endspc = s;

	return 1;
}

static inline int
emitline(struct tab *tab, size_t zero, size_t *off, const struct line *l,
    const char **line)
{
	if (!push_line(tab, l, *line, *off - zero))
		return 0;
	*line += *off - zero;
	*off = zero;
	return 1;
}

static inline void
emitstr(const char **s, size_t len, size_t *off)
{
	size_t i;

	/* printw("%*s", ...) doesn't seem to respect the precision, so... */
	for (i = 0; i < len; ++i)
		addch((*s)[i]);
	*off += len;
	*s += len;
}

/*
 * Build a list of visual line by wrapping the given line, assuming
 * that when printed will have a leading prefix prfx.
 *
 * TODO: it considers each byte one cell on the screen!
 */
static void
wrap_text(struct tab *tab, const char *prfx, struct line *l)
{
	size_t		 zero, off, len, split;
	const char	*endword, *endspc, *line, *linestart;

	zero = strlen(prfx);
	off = zero;
	line = l->line;
	linestart = l->line;

	while (word_boundaries(line, " \t-", &endword, &endspc)) {
		len = endword - line;
		if (off + len >= body_cols) {
			emitline(tab, zero, &off, l, &linestart);
			while (len >= body_cols) {
				/* hard wrap */
				emitline(tab, zero, &off, l, &linestart);
				len -= body_cols-1;
				line += body_cols-1;
			}

			if (len != 0)
				off += len;
		} else
			off += len;

		/* print the spaces iff not at bol */
		len = endspc - endword;
		/* line = endspc; */
		if (off != zero) {
			if (off + len >= body_cols) {
				emitline(tab, zero, &off, l, &linestart);
				linestart = endspc;
			} else
				off += len;
		}

		line = endspc;
	}

	emitline(tab, zero, &off, l, &linestart);
}

static int
hardwrap_text(struct tab *tab, struct line *l)
{
	size_t		 off, len;
	const char	*linestart;

        len = strlen(l->line);
	off = 0;
	linestart = l->line;

	while (len >= COLS) {
		len -= COLS-1;
		off = COLS-1;
		if (!emitline(tab, 0, &off, l, &linestart))
			return 0;
	}

	if (len != 0)
		return emitline(tab, 0, &len, l, &linestart);

	return 1;
}

static int
wrap_page(struct tab *tab)
{
	struct line	*l;

	empty_vlist(tab);

	TAILQ_FOREACH(l, &tab->page.head, lines) {
		switch (l->type) {
		case LINE_TEXT:
			wrap_text(tab, "", l);
			break;
		case LINE_LINK:
			wrap_text(tab, "=> ", l);
			break;
		case LINE_TITLE_1:
			wrap_text(tab, "# ", l);
			break;
		case LINE_TITLE_2:
			wrap_text(tab, "## ", l);
			break;
		case LINE_TITLE_3:
			wrap_text(tab, "### ", l);
			break;
		case LINE_ITEM:
			wrap_text(tab, "* ", l);
			break;
		case LINE_QUOTE:
			wrap_text(tab, "> ", l);
			break;
		case LINE_PRE_START:
		case LINE_PRE_END:
                        push_line(tab, l, NULL, 0);
			break;
		case LINE_PRE_CONTENT:
                        hardwrap_text(tab, l);
			break;
		}
	}
	return 1;
}

static inline void
print_line(struct line *l)
{
	const char *text = l->line;

	if (text == NULL)
		text = "";

	switch (l->type) {
	case LINE_TEXT:
		wprintw(body, "%s", text);
		break;
	case LINE_LINK:
		wattron(body, A_UNDERLINE);
		wprintw(body, "=> %s", text);
		wattroff(body, A_UNDERLINE);
		return;
	case LINE_TITLE_1:
		wattron(body, A_BOLD);
		wprintw(body, "# %s", text);
		wattroff(body, A_BOLD);
		return;
	case LINE_TITLE_2:
		wattron(body, A_BOLD);
		wprintw(body, "## %s", text);
		wattroff(body, A_BOLD);
		return;
	case LINE_TITLE_3:
		wattron(body, A_BOLD);
		wprintw(body, "### %s", text);
		wattroff(body, A_BOLD);
		return;
	case LINE_ITEM:
		wprintw(body, "* %s", text);
		return;
	case LINE_QUOTE:
		wattron(body, A_DIM);
		wprintw(body, "> %s", text);
		wattroff(body, A_DIM);
		return;
	case LINE_PRE_START:
	case LINE_PRE_END:
		wprintw(body, "```");
		return;
	case LINE_PRE_CONTENT:
		wprintw(body, "%s", text);
		return;
	}
}

static void
redraw_tabline(void)
{
	wclear(tabline);
	wbkgd(tabline, A_REVERSE);
	mvwprintw(tabline, 0, 0, "TODO: tabs here");
}

static void
redraw_modeline(struct tab *tab)
{
	int		 x, y, max_x, max_y;
	const char	*mode = "text/gemini-mode";
	const char	*spin = "-\\|/";

	wclear(modeline);
	wattron(modeline, A_REVERSE);
	wmove(modeline, 0, 0);

	wprintw(modeline, "-%c %s %s ",
	    spin[tab->s->loading_anim_step], mode, tab->urlstr);
	getyx(modeline, y, x);
	getmaxyx(modeline, max_y, max_x);

	(void)y;
	(void)max_y;

	for (; x < max_x; ++x)
		waddstr(modeline, "-");
}

static void
redraw_tab(struct tab *tab)
{
	struct line	*l;
	int		 line;

	werase(body);

	tab->s->line_off = MIN(tab->s->line_max, tab->s->line_off);
	if (TAILQ_EMPTY(&tab->s->head))
		return;

	line = 0;
	l = nth_line(tab, tab->s->line_off);
	for (; l != NULL; l = TAILQ_NEXT(l, lines)) {
		wmove(body, line, 0);
		print_line(l);
		line++;
		if (line == body_lines)
			break;
	}

	redraw_tabline();
	redraw_modeline(tab);

	restore_cursor(tab);
	wrefresh(tabline);
	wrefresh(modeline);
	wrefresh(minibuf);

	wrefresh(body);
}

static void
message(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	if (clminibufev_set)
		evtimer_del(&clminibufev);
	evtimer_set(&clminibufev, handle_clear_minibuf, NULL);
	evtimer_add(&clminibufev, &clminibufev_timer);
	clminibufev_set = 1;

	werase(minibuf);
	vw_printw(minibuf, fmt, ap);

	wrefresh(minibuf);
	wrefresh(body);

	va_end(ap);
}

static void
start_loading_anim(struct tab *tab)
{
	if (tab->s->loading_anim)
		return;
	tab->s->loading_anim = 1;
	evtimer_set(&tab->s->loadingev, update_loading_anim, tab);
	evtimer_add(&tab->s->loadingev, &loadingev_timer);
}

static void
update_loading_anim(int fd, short ev, void *d)
{
	struct tab	*tab = d;

	tab->s->loading_anim_step = (tab->s->loading_anim_step+1)%4;

	redraw_modeline(tab);
	wrefresh(modeline);
	wrefresh(body);

	evtimer_add(&tab->s->loadingev, &loadingev_timer);
}

static void
stop_loading_anim(struct tab *tab)
{
	if (!tab->s->loading_anim)
		return;
	evtimer_del(&tab->s->loadingev);
	tab->s->loading_anim = 0;
	tab->s->loading_anim_step = 0;

	redraw_modeline(tab);
	wrefresh(modeline);
	wrefresh(body);
}

static void
load_url_in_tab(struct tab *tab, const char *url)
{
	empty_vlist(tab);
	message("Loading %s...", url);
	start_loading_anim(tab);
	load_url(tab, url);

	tab->s->curs_x = 0;
	tab->s->curs_y = 0;
	redraw_tab(tab);
}

static void
new_tab(void)
{
	struct tab	*tab, *t;
	const char	*url = "about:new";

	if ((tab = calloc(1, sizeof(*tab))) == NULL)
		goto err;

	if ((tab->s = calloc(1, sizeof(*t->s))) == NULL)
		goto err;

	TAILQ_INIT(&tab->s->head);
	TAILQ_FOREACH(t, &tabshead, tabs) {
		t->flags &= ~TAB_CURRENT;
	}

	tab->id = tab_counter++;
	tab->flags = TAB_CURRENT;

	if (TAILQ_EMPTY(&tabshead))
		TAILQ_INSERT_HEAD(&tabshead, tab, tabs);
	else
		TAILQ_INSERT_TAIL(&tabshead, tab, tabs);

	load_url_in_tab(tab, url);
	return;

err:
	event_loopbreak();
}

int
ui_init(void)
{
	setlocale(LC_ALL, "");

	TAILQ_INIT(&global_map);
	current_map = &global_map;
	load_default_keys();

	initscr();
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

	body_lines = LINES-3;
	body_cols = COLS;

	keypad(body, TRUE);
	scrollok(body, TRUE);

	/* non-blocking input */
	wtimeout(body, 0);

	mvwprintw(body, 0, 0, "");

	event_set(&stdioev, 0, EV_READ | EV_PERSIST, dispatch_stdio, NULL);
	event_add(&stdioev, NULL);

	signal_set(&winchev, SIGWINCH, handle_resize, NULL);
	signal_add(&winchev, NULL);

	new_tab();

	return 1;
}

void
ui_on_tab_loaded(struct tab *tab)
{
	stop_loading_anim(tab);
	message("Loaded %s", tab->urlstr);
}

void
ui_on_tab_refresh(struct tab *tab)
{
	if (!(tab->flags & TAB_CURRENT))
		return;

	wrap_page(tab);
	redraw_tab(tab);
}

void
ui_end(void)
{
	endwin();
}
