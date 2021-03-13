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

static struct event	stdioev, winchev;

static void		 load_default_keys(void);
static int		 push_line(struct tab*, const struct line*, const char*, size_t, int);
static void		 empty_vlist(struct tab*);
static void		 restore_cursor(struct tab *);

static void		 cmd_previous_line(struct tab*);
static void		 cmd_next_line(struct tab*);
static void		 cmd_forward_char(struct tab*);
static void		 cmd_backward_char(struct tab*);
static void		 cmd_move_beginning_of_line(struct tab*);
static void		 cmd_move_end_of_line(struct tab*);
static void		 cmd_redraw(struct tab*);
static void		 cmd_scroll_line_down(struct tab*);
static void		 cmd_scroll_line_up(struct tab*);
static void		 cmd_scroll_up(struct tab*);
static void		 cmd_scroll_down(struct tab*);
static void		 cmd_beginning_of_buffer(struct tab*);
static void		 cmd_end_of_buffer(struct tab*);
static void		 cmd_kill_telescope(struct tab*);
static void		 cmd_push_button(struct tab*);
static void		 cmd_push_button_new_tab(struct tab*);
static void		 cmd_clear_minibuf(struct tab*);
static void		 cmd_execute_extended_command(struct tab*);
static void		 cmd_tab_close(struct tab*);
static void		 cmd_tab_new(struct tab*);
static void		 cmd_tab_next(struct tab*);
static void		 cmd_tab_previous(struct tab*);
static void		 cmd_load_url(struct tab*);
static void		 cmd_load_current_url(struct tab*);

static void		 global_key_unbound(void);

static void		 cmd_mini_delete_char(struct tab*);
static void		 cmd_mini_delete_backward_char(struct tab*);
static void		 cmd_mini_forward_char(struct tab*);
static void		 cmd_mini_backward_char(struct tab*);
static void		 cmd_mini_move_end_of_line(struct tab*);
static void		 cmd_mini_move_beginning_of_line(struct tab*);
static void		 cmd_mini_kill_line(struct tab*);
static void		 cmd_mini_abort(struct tab*);
static void		 cmd_mini_complete_and_exit(struct tab*);
static void		 cmd_mini_previous_history_element(struct tab*);
static void		 cmd_mini_next_history_element(struct tab*);

static void		 minibuffer_hist_save_entry(void);
static void		 minibuffer_taint_hist(void);
static void		 minibuffer_self_insert(void);
static void		 eecmd_self_insert(void);
static void		 eecmd_select(void);
static void		 ir_self_insert(void);
static void		 ir_select(void);
static void		 lu_self_insert(void);
static void		 lu_select(void);

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
static void		 redraw_body(struct tab*);
static void		 redraw_modeline(struct tab*);
static void		 redraw_minibuffer(void);
static void		 redraw_tab(struct tab*);
static void		 message(const char*, ...) __attribute__((format(printf, 1, 2)));
static void		 start_loading_anim(struct tab*);
static void		 update_loading_anim(int, short, void*);
static void		 stop_loading_anim(struct tab*);
static void		 load_url_in_tab(struct tab*, const char*);
static void		 enter_minibuffer(void(*)(void), void(*)(void), void(*)(void), struct histhead*);
static void		 exit_minibuffer(void);
static void		 switch_to_tab(struct tab*);
static struct tab	*new_tab(void);

static struct { int meta, key; } thiskey;

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

static char	keybuf[64];

struct kmap global_map,
	minibuffer_map,
	*current_map,
	*base_map;

static struct histhead eecmd_history,
	ir_history,
	lu_history;

static int	in_minibuffer;

static struct {
	char	*curmesg;

	char	 buf[1025];
	size_t	 off, len;
	char	 prompt[32];
	void	 (*donefn)(void);
	void	 (*abortfn)(void);

	struct histhead	*history;
	struct hist	*hist_cur;
	size_t			 hist_off;
} ministate;

struct lineprefix {
	const char	*prfx1;
	const char	*prfx2;
} line_prefixes[] = {
	[LINE_TEXT] =		{ "",		"" },
	[LINE_LINK] =		{ "=> ",	"   " },
	[LINE_TITLE_1] =	{ "# ",		"  " },
	[LINE_TITLE_2] =	{ "## ",	"   " },
	[LINE_TITLE_3] =	{ "### ",	"    " },
	[LINE_ITEM] =		{ "* ",		"  " },
	[LINE_QUOTE] =		{ "> ",		"> " },
	[LINE_PRE_START] =	{ "```",	"```" },
	[LINE_PRE_CONTENT] =	{ "",		"" },
	[LINE_PRE_END] =	{ "```",	"```" },
};

struct line_face {
	int prop;
} line_faces[] = {
	[LINE_TEXT] =		{ 0 },
	[LINE_LINK] =		{ A_UNDERLINE },
	[LINE_TITLE_1] =	{ A_BOLD },
	[LINE_TITLE_2] =	{ A_BOLD },
	[LINE_TITLE_3] =	{ A_BOLD },
	[LINE_ITEM] =		{ 0 },
	[LINE_QUOTE] =		{ A_DIM },
	[LINE_PRE_START] =	{ 0 },
	[LINE_PRE_CONTENT] =	{ 0 },
	[LINE_PRE_END] =	{ 0 },
};

static inline void
global_set_key(const char *key, void (*fn)(struct tab*))
{
	if (!kmap_define_key(&global_map, key, fn))
		_exit(1);
}

static inline void
minibuffer_set_key(const char *key, void (*fn)(struct tab*))
{
	if (!kmap_define_key(&minibuffer_map, key, fn))
		_exit(1);
}

static void
load_default_keys(void)
{
	/* === global map === */

	/* emacs */
	global_set_key("C-p",		cmd_previous_line);
	global_set_key("C-n",		cmd_next_line);
	global_set_key("C-f",		cmd_forward_char);
	global_set_key("C-b",		cmd_backward_char);
	global_set_key("C-a",		cmd_move_beginning_of_line);
	global_set_key("C-e",		cmd_move_end_of_line);

	global_set_key("M-v",		cmd_scroll_up);
	global_set_key("C-v",		cmd_scroll_down);
	global_set_key("M-space",	cmd_scroll_up);
	global_set_key("space",		cmd_scroll_down);

	global_set_key("C-x C-c",	cmd_kill_telescope);

	global_set_key("C-g",		cmd_clear_minibuf);

	global_set_key("M-x",		cmd_execute_extended_command);
	global_set_key("C-x C-f",	cmd_load_url);
	global_set_key("C-x M-f",	cmd_load_current_url);

	global_set_key("C-x t 0",	cmd_tab_close);
	global_set_key("C-x t 2",	cmd_tab_new);
	global_set_key("C-x t o",	cmd_tab_next);
	global_set_key("C-x t O",	cmd_tab_previous);

	global_set_key("M-<",		cmd_beginning_of_buffer);
	global_set_key("M->",		cmd_end_of_buffer);

	/* vi/vi-like */
	global_set_key("k",		cmd_previous_line);
	global_set_key("j",		cmd_next_line);
	global_set_key("l",		cmd_forward_char);
	global_set_key("h",		cmd_backward_char);
	global_set_key("^",		cmd_move_beginning_of_line);
	global_set_key("$",		cmd_move_end_of_line);

	global_set_key("K",		cmd_scroll_line_up);
	global_set_key("J",		cmd_scroll_line_down);

	global_set_key("g g",		cmd_beginning_of_buffer);
	global_set_key("G",		cmd_end_of_buffer);

	/* tmp */
	global_set_key("q",		cmd_kill_telescope);

	global_set_key("esc",		cmd_clear_minibuf);

	global_set_key(":",		cmd_execute_extended_command);

	/* cua */
	global_set_key("<up>",		cmd_previous_line);
	global_set_key("<down>",	cmd_next_line);
	global_set_key("<right>",	cmd_forward_char);
	global_set_key("<left>",	cmd_backward_char);
	global_set_key("<prior>",	cmd_scroll_up);
	global_set_key("<next>",	cmd_scroll_down);

	/* "ncurses standard" */
	global_set_key("C-l",		cmd_redraw);

	/* global */
	global_set_key("C-m",		cmd_push_button);
	global_set_key("M-enter",	cmd_push_button_new_tab);

	/* === minibuffer map === */
	minibuffer_set_key("ret",		cmd_mini_complete_and_exit);
	minibuffer_set_key("C-g",		cmd_mini_abort);
	minibuffer_set_key("esc",		cmd_mini_abort);
	minibuffer_set_key("C-d",		cmd_mini_delete_char);
	minibuffer_set_key("del",		cmd_mini_delete_backward_char);

	minibuffer_set_key("C-f",		cmd_mini_forward_char);
	minibuffer_set_key("C-b",		cmd_mini_backward_char);
	minibuffer_set_key("<right>",		cmd_mini_forward_char);
	minibuffer_set_key("<left>",		cmd_mini_backward_char);
	minibuffer_set_key("C-e",		cmd_mini_move_end_of_line);
	minibuffer_set_key("C-a",		cmd_mini_move_beginning_of_line);
	minibuffer_set_key("<end>",		cmd_mini_move_end_of_line);
	minibuffer_set_key("<home>",		cmd_mini_move_beginning_of_line);
	minibuffer_set_key("C-k",		cmd_mini_kill_line);

	minibuffer_set_key("M-p",		cmd_mini_previous_history_element);
	minibuffer_set_key("M-n",		cmd_mini_next_history_element);
	minibuffer_set_key("<up>",		cmd_mini_previous_history_element);
	minibuffer_set_key("<down>",		cmd_mini_next_history_element);
}

static int
push_line(struct tab *tab, const struct line *l, const char *buf, size_t len, int cont)
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
	vl->flags = cont;

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
		cmd_scroll_line_up(tab);
	}

	restore_cursor(tab);
}

static void
cmd_next_line(struct tab *tab)
{
	if (tab->s->line_off + tab->s->curs_y >= tab->s->line_max)
		return;

	if (++tab->s->curs_y > body_lines-1) {
		tab->s->curs_y = body_lines-1;
		cmd_scroll_line_down(tab);
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
cmd_move_beginning_of_line(struct tab *tab)
{
	tab->s->curs_x = 0;
	restore_cursor(tab);
}

static void
cmd_move_end_of_line(struct tab *tab)
{
	struct line	*line;
	size_t		 off;
	const char	*prfx;

	off = tab->s->line_off + tab->s->curs_y;
	if (off >= tab->s->line_max) {
		tab->s->curs_x = 0;
		goto end;
	}

	line = nth_line(tab, off);
	if (line->line != NULL)
		tab->s->curs_x = strlen(line->line);
	else
		tab->s->curs_x = 0;

	prfx = line_prefixes[line->type].prfx1;
	tab->s->curs_x += strlen(prfx);

end:
	restore_cursor(tab);
}

static void
cmd_redraw(struct tab *tab)
{
	handle_resize(0, 0, NULL);
}

static void
cmd_scroll_line_up(struct tab *tab)
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
cmd_scroll_line_down(struct tab *tab)
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
cmd_scroll_up(struct tab *tab)
{
	size_t off;

	off = body_lines+1;

	for (; off > 0; --off)
		cmd_scroll_line_up(tab);
}

static void
cmd_scroll_down(struct tab *tab)
{
	size_t off;

	off = body_lines+1;

	for (; off > 0; --off)
		cmd_scroll_line_down(tab);
}

static void
cmd_beginning_of_buffer(struct tab *tab)
{
	tab->s->line_off = 0;
	tab->s->curs_y = 0;
	redraw_body(tab);
}

static void
cmd_end_of_buffer(struct tab *tab)
{
	ssize_t off;

	off = tab->s->line_max - body_lines;
	off = MAX(0, off);

	tab->s->line_off = off;
	tab->s->curs_y = MIN(body_lines, tab->s->line_max);

	redraw_body(tab);
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
	if (nth >= tab->s->line_max)
		return;
	l = nth_line(tab, nth);
	if (l->type != LINE_LINK)
		return;

	load_url_in_tab(tab, l->alt);
}

static void
cmd_push_button_new_tab(struct tab *tab)
{
	struct tab	*t;
	struct line	*l;
	size_t		 nth;

	nth = tab->s->line_off + tab->s->curs_y;
	if (nth > tab->s->line_max)
		return;
	l = nth_line(tab, nth);
	if (l->type != LINE_LINK)
		return;

	t = new_tab();
	memcpy(&t->url, &tab->url, sizeof(tab->url));
	memcpy(&t->urlstr, &tab->urlstr, sizeof(tab->urlstr));
	load_url_in_tab(t, l->alt);
}

static void
cmd_clear_minibuf(struct tab *tab)
{
	handle_clear_minibuf(0, 0, NULL);
}

static void
cmd_execute_extended_command(struct tab *tab)
{
	size_t	 len;

	enter_minibuffer(eecmd_self_insert, eecmd_select, exit_minibuffer,
	    &eecmd_history);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, "", len);

	if (thiskey.meta)
		strlcat(ministate.prompt, "M-", len);

	strlcat(ministate.prompt, keyname(thiskey.key), len);
	strlcat(ministate.prompt, " ", len);
}

static void
cmd_tab_close(struct tab *tab)
{
	struct tab *t;

	if (TAILQ_PREV(tab, tabshead, tabs) == NULL &&
	    TAILQ_NEXT(tab, tabs) == NULL) {
		message("Can't close the only tab.");
		return;
	}

	stop_tab(tab);

	t = TAILQ_PREV(tab, tabshead, tabs);
	t->flags |= TAB_CURRENT;

	TAILQ_REMOVE(&tabshead, tab, tabs);

	free(tab->s);
	free(tab);
}

static void
cmd_tab_new(struct tab *tab)
{
	new_tab();
}

static void
cmd_tab_next(struct tab *tab)
{
	struct tab *t;

	tab->flags &= ~TAB_CURRENT;

	if ((t = TAILQ_NEXT(tab, tabs)) == NULL)
		t = TAILQ_FIRST(&tabshead);
	t->flags |= TAB_CURRENT;
}

static void
cmd_tab_previous(struct tab *tab)
{
	struct tab *t;

	tab->flags &= ~TAB_CURRENT;

	if ((t = TAILQ_PREV(tab, tabshead, tabs)) == NULL)
		t = TAILQ_LAST(&tabshead, tabshead);
	t->flags |= TAB_CURRENT;
}

static void
cmd_load_url(struct tab *tab)
{
	enter_minibuffer(lu_self_insert, lu_select, exit_minibuffer,
	    &lu_history);
	strlcpy(ministate.prompt, "Load URL: ", sizeof(ministate.prompt));
}

static void
cmd_load_current_url(struct tab *tab)
{
	enter_minibuffer(lu_self_insert, lu_select, exit_minibuffer,
	    &lu_history);
	strlcpy(ministate.prompt, "Load URL: ", sizeof(ministate.prompt));
	strlcpy(ministate.buf, tab->urlstr, sizeof(ministate.buf));
	ministate.off = strlen(tab->urlstr);
	ministate.len = ministate.off;
}

static void
global_key_unbound(void)
{
	message("%s is undefined", keybuf);
}

static void
cmd_mini_delete_char(struct tab *tab)
{
	minibuffer_taint_hist();

	if (ministate.len == 0 || ministate.off == ministate.len)
		return;

	memmove(&ministate.buf[ministate.off],
	    &ministate.buf[ministate.off+1],
	    ministate.len - ministate.off + 1);
	ministate.len--;
}

static void
cmd_mini_delete_backward_char(struct tab *tab)
{
	minibuffer_taint_hist();

	if (ministate.len == 0 || ministate.off == 0)
		return;

	memmove(&ministate.buf[ministate.off-1],
	    &ministate.buf[ministate.off],
	    ministate.len - ministate.off + 1);
	ministate.off--;
	ministate.len--;
}

static void
cmd_mini_forward_char(struct tab *tab)
{
	if (ministate.off == ministate.len)
		return;
	ministate.off++;
}

static void
cmd_mini_backward_char(struct tab *tab)
{
	if (ministate.off == 0)
		return;
	ministate.off--;
}

static void
cmd_mini_move_end_of_line(struct tab *tab)
{
	ministate.off = ministate.len;
}

static void
cmd_mini_move_beginning_of_line(struct tab *tab)
{
	ministate.off = 0;
}

static void
cmd_mini_kill_line(struct tab *tab)
{
	minibuffer_taint_hist();

        if (ministate.off == ministate.len)
		return;
	ministate.buf[ministate.off] = '\0';
	ministate.len -= ministate.off;
}

static void
cmd_mini_abort(struct tab *tab)
{
        ministate.abortfn();
}

static void
cmd_mini_complete_and_exit(struct tab *tab)
{
	minibuffer_taint_hist();
	ministate.donefn();
}

static void
cmd_mini_previous_history_element(struct tab *tab)
{
	if (ministate.history == NULL) {
		message("No history");
		return;
	}

	if (ministate.hist_cur == NULL ||
	    (ministate.hist_cur = TAILQ_PREV(ministate.hist_cur, mhisthead, entries)) == NULL) {
		ministate.hist_cur = TAILQ_LAST(&ministate.history->head, mhisthead);
		ministate.hist_off = ministate.history->len - 1;
		if (ministate.hist_cur == NULL)
			message("No prev item");
	} else {
		ministate.hist_off--;
	}

	if (ministate.hist_cur != NULL) {
		ministate.off = 0;
		ministate.len = strlen(ministate.hist_cur->h);
	}
}

static void
cmd_mini_next_history_element(struct tab *tab)
{
	if (ministate.history == NULL) {
		message("No history");
		return;
	}

	if (ministate.hist_cur == NULL ||
	    (ministate.hist_cur = TAILQ_NEXT(ministate.hist_cur, entries)) == NULL) {
		ministate.hist_cur = TAILQ_FIRST(&ministate.history->head);
		ministate.hist_off = 0;
		if (ministate.hist_cur == NULL)
			message("No next item");
	} else {
		ministate.hist_off++;
	}

	if (ministate.hist_cur != NULL) {
		ministate.off = 0;
		ministate.len = strlen(ministate.hist_cur->h);
	}
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
static void
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
	minibuffer_taint_hist();

	if (ministate.len == sizeof(ministate.buf) -1)
		return;

	/* TODO: utf8 handling! */

	memmove(&ministate.buf[ministate.off+1],
	    &ministate.buf[ministate.off],
	    ministate.len - ministate.off + 1);
	ministate.buf[ministate.off] = thiskey.key;
	ministate.off++;
	ministate.len++;
}

static void
eecmd_self_insert(void)
{
	if (thiskey.meta || isspace(thiskey.key) ||
	    !isgraph(thiskey.key)) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

static void
eecmd_select(void)
{
	exit_minibuffer();
	minibuffer_hist_save_entry();
	message("TODO: try to execute %s", ministate.buf);
}

static void
ir_self_insert(void)
{
	minibuffer_self_insert();
}

static void
ir_select(void)
{
	char		 buf[1025] = {0};
	struct url	 url;
	struct tab	*tab;

	tab = current_tab();

	exit_minibuffer();
	minibuffer_hist_save_entry();

	/* a bit ugly but... */
	memcpy(&url, &tab->url, sizeof(tab->url));
	url_set_query(&url, ministate.buf);
	url_unparse(&url, buf, sizeof(buf));
	load_url_in_tab(tab, buf);
}

static void
lu_self_insert(void)
{
	if (thiskey.meta || isspace(thiskey.key) ||
	    !isgraph(thiskey.key)) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

static void
lu_select(void)
{
	exit_minibuffer();
	minibuffer_hist_save_entry();
	load_url_in_tab(current_tab(), ministate.buf);
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
	struct tab	*tab;
	struct keymap	*k;
	const char	*keyname;
	char		 tmp[2] = {0};

	thiskey.key = wgetch(body);
	if (thiskey.key == ERR)
		return;
	if (thiskey.key == 27) {
		/* TODO: make escape-time customizable */

		thiskey.meta = 1;
		thiskey.key = wgetch(body);
		if (thiskey.key == ERR || thiskey.key == 27) {
			thiskey.meta = 0;
			thiskey.key = 27;
		}
	} else
		thiskey.meta = 0;

	if (keybuf[0] != '\0')
		strlcat(keybuf, " ", sizeof(keybuf));
	if (thiskey.meta)
		strlcat(keybuf, "M-", sizeof(keybuf));
	if ((keyname = unkbd(thiskey.key)) != NULL)
		strlcat(keybuf, keyname, sizeof(keybuf));
	else {
		tmp[0] = thiskey.key;
		strlcat(keybuf, tmp, sizeof(keybuf));
	}

	TAILQ_FOREACH(k, &current_map->m, keymaps) {
		if (k->meta == thiskey.meta &&
		    k->key == thiskey.key) {
			if (k->fn == NULL)
				current_map = &k->map;
			else {
				current_map = base_map;
				strlcpy(keybuf, "", sizeof(keybuf));
				k->fn(current_tab());
			}
			goto done;
		}
	}

	if (current_map->unhandled_input != NULL)
		current_map->unhandled_input();
	else {
		global_key_unbound();
	}

	strlcpy(keybuf, "", sizeof(keybuf));
	current_map = base_map;

done:
	redraw_tab(current_tab());
}

static void
handle_clear_minibuf(int fd, short ev, void *d)
{
	clminibufev_set = 0;

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
    const char **line, int *cont)
{
	if (!push_line(tab, l, *line, *off - zero, *cont))
		return 0;
	if (!*cont)
		*cont = 1;
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
	int		 cont = 0;
	const char	*endword, *endspc, *line, *linestart;

	zero = strlen(prfx);
	off = zero;
	line = l->line;
	linestart = l->line;

	while (word_boundaries(line, " \t-", &endword, &endspc)) {
		len = endword - line;
		if (off + len >= body_cols) {
			emitline(tab, zero, &off, l, &linestart, &cont);
			while (len >= body_cols) {
				/* hard wrap */
				emitline(tab, zero, &off, l, &linestart, &cont);
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
				emitline(tab, zero, &off, l, &linestart, &cont);
				linestart = endspc;
			} else
				off += len;
		}

		line = endspc;
	}

	emitline(tab, zero, &off, l, &linestart, &cont);
}

static int
hardwrap_text(struct tab *tab, struct line *l)
{
	size_t		 off, len;
	int		 cont;
	const char	*linestart;

	if (l->line == NULL)
		return emitline(tab, 0, &off, l, &linestart, &cont);

        len = strlen(l->line);
	off = 0;
	linestart = l->line;

	while (len >= COLS) {
		len -= COLS-1;
		off = COLS-1;
		if (!emitline(tab, 0, &off, l, &linestart, &cont))
			return 0;
	}

	if (len != 0)
		return emitline(tab, 0, &len, l, &linestart, &cont);

	return 1;
}

static int
wrap_page(struct tab *tab)
{
	struct line	*l;
	const char	*prfx;

	empty_vlist(tab);

	TAILQ_FOREACH(l, &tab->page.head, lines) {
		prfx = line_prefixes[l->type].prfx1;
		switch (l->type) {
		case LINE_TEXT:
		case LINE_LINK:
		case LINE_TITLE_1:
		case LINE_TITLE_2:
		case LINE_TITLE_3:
		case LINE_ITEM:
		case LINE_QUOTE:
			wrap_text(tab, prfx, l);
			break;
		case LINE_PRE_START:
		case LINE_PRE_END:
                        push_line(tab, l, NULL, 0, 0);
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
	const char *prfx;
	int face = line_faces[l->type].prop;

	if (!l->flags)
		prfx = line_prefixes[l->type].prfx1;
	else
		prfx = line_prefixes[l->type].prfx2;

	if (text == NULL)
		text = "";

	if (face != 0)
		wattron(body, face);
	wprintw(body, "%s%s", prfx, text);
	if (face != 0)
		wattroff(body, face);
}

static void
redraw_tabline(void)
{
	struct tab	*tab;
	int		 current;
	const char	*title;

	werase(tabline);
	wbkgd(tabline, A_REVERSE);

	wprintw(tabline, " ");
	TAILQ_FOREACH(tab, &tabshead, tabs) {
		current = tab->flags & TAB_CURRENT;

		if (*(title = tab->page.title) == '\0')
			title = tab->urlstr;

		if (current)
			wattron(tabline, A_UNDERLINE);

		wprintw(tabline, "%s%d: %s",
		    current ? "*" : " ", tab->id, title);

		if (current)
			wattroff(tabline, A_UNDERLINE);
	}
}

static void
redraw_modeline(struct tab *tab)
{
	double		 pct;
	int		 x, y, max_x, max_y;
	const char	*mode = tab->page.name;
	const char	*spin = "-\\|/";

	werase(modeline);
	wattron(modeline, A_REVERSE);
	wmove(modeline, 0, 0);

	wprintw(modeline, "-%c %s-mode ",
	    spin[tab->s->loading_anim_step], mode);

	pct = (tab->s->line_off + tab->s->curs_y) * 100.0 / tab->s->line_max;

	if (tab->s->line_max <= body_lines)
                wprintw(modeline, "All ");
	else if (tab->s->line_off == 0)
                wprintw(modeline, "Top ");
	else if (tab->s->line_off + body_lines >= tab->s->line_max)
		wprintw(modeline, "Bottom ");
	else
		wprintw(modeline, "%.0f%% ", pct);

	wprintw(modeline, "%d/%d %s ",
	    tab->s->line_off + tab->s->curs_y,
	    tab->s->line_max,
	    tab->urlstr);

	getyx(modeline, y, x);
	getmaxyx(modeline, max_y, max_x);

	(void)y;
	(void)max_y;

	for (; x < max_x; ++x)
		waddstr(modeline, "-");
}

static void
redraw_minibuffer(void)
{
	size_t skip = 0, off_x = 0, off_y = 0;

	werase(minibuf);
	if (in_minibuffer) {
		mvwprintw(minibuf, 0, 0, "%s", ministate.prompt);
		if (ministate.hist_cur != NULL)
			wprintw(minibuf, "(%zu/%zu) ",
			    ministate.hist_off + 1,
			    ministate.history->len);

		getyx(minibuf, off_y, off_x);

		while (ministate.off - skip > COLS / 2) {
			skip += MIN(ministate.off/4, 1);
		}

		if (ministate.hist_cur != NULL)
			wprintw(minibuf, "%s", ministate.hist_cur->h + skip);
		else
			wprintw(minibuf, "%s", ministate.buf + skip);
	}

	if (ministate.curmesg != NULL) {
		if (in_minibuffer)
			wprintw(minibuf, "  [%s]", ministate.curmesg);
		else
			wprintw(minibuf, "%s", ministate.curmesg);
	}

	if (!in_minibuffer && ministate.curmesg == NULL)
		wprintw(minibuf, "%s", keybuf);

	if (in_minibuffer)
		wmove(minibuf, 0, off_x + ministate.off - skip);
}

static void
redraw_tab(struct tab *tab)
{
	redraw_tabline();
	redraw_body(tab);
	redraw_modeline(tab);
	redraw_minibuffer();

	restore_cursor(tab);
	wrefresh(tabline);
	wrefresh(modeline);

	if (in_minibuffer) {
		wrefresh(body);
		wrefresh(minibuf);
	} else {
		wrefresh(minibuf);
		wrefresh(body);
	}
}

static void
redraw_body(struct tab *tab)
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
}

static void
message(const char *fmt, ...)
{
	va_list ap;

	if (clminibufev_set)
		evtimer_del(&clminibufev);
	evtimer_set(&clminibufev, handle_clear_minibuf, NULL);
	evtimer_add(&clminibufev, &clminibufev_timer);
	clminibufev_set = 1;

	free(ministate.curmesg);

	va_start(ap, fmt);
	/* TODO: what to do if the allocation fails here? */
	if (vasprintf(&ministate.curmesg, fmt, ap) == -1)
		ministate.curmesg = NULL;
	va_end(ap);

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
	if (in_minibuffer)
		wrefresh(minibuf);

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
	if (in_minibuffer)
		wrefresh(minibuf);
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
        ministate.off = 0;
	ministate.len = 0;
	strlcpy(ministate.buf, "", sizeof(ministate.prompt));

	ministate.history = hist;
	ministate.hist_cur = NULL;
	ministate.hist_off = 0;
}

static void
exit_minibuffer(void)
{
	werase(minibuf);

	in_minibuffer = 0;
	base_map = &global_map;
	current_map = &global_map;
}

static void
switch_to_tab(struct tab *tab)
{
	struct tab	*t;

	TAILQ_FOREACH(t, &tabshead, tabs) {
		t->flags &= ~TAB_CURRENT;
	}

	tab->flags |= TAB_CURRENT;
}

static struct tab *
new_tab(void)
{
	struct tab	*tab, *t;
	const char	*url = "about:new";

	if ((tab = calloc(1, sizeof(*tab))) == NULL)
		goto err;

	if ((tab->s = calloc(1, sizeof(*t->s))) == NULL)
		goto err;

	TAILQ_INIT(&tab->s->head);

	tab->id = tab_counter++;
	switch_to_tab(tab);

	if (TAILQ_EMPTY(&tabshead))
		TAILQ_INSERT_HEAD(&tabshead, tab, tabs);
	else
		TAILQ_INSERT_TAIL(&tabshead, tab, tabs);

	load_url_in_tab(tab, url);
	return tab;

err:
	event_loopbreak();
	return NULL;
}

int
ui_init(void)
{
	setlocale(LC_ALL, "");

	TAILQ_INIT(&global_map.m);
	global_map.unhandled_input = global_key_unbound;

	TAILQ_INIT(&minibuffer_map.m);

	TAILQ_INIT(&eecmd_history.head);
	TAILQ_INIT(&ir_history.head);
	TAILQ_INIT(&lu_history.head);

	base_map = &global_map;
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
ui_end(void)
{
	endwin();
}
