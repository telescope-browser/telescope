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

#include <curses.h>
#include <event.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TAB_CURRENT	0x1

#define NEW_TAB_URL	"about:new"

static struct event	stdioev, winchev;

static void		 load_default_keys(void);
static void		 empty_vlist(struct window*);
static void		 restore_cursor(struct window*);

#define CMD(fnname) static void fnname(struct window *)

CMD(cmd_previous_line);
CMD(cmd_next_line);
CMD(cmd_backward_char);
CMD(cmd_forward_char);
CMD(cmd_backward_paragraph);
CMD(cmd_forward_paragraph);
CMD(cmd_move_beginning_of_line);
CMD(cmd_move_end_of_line);
CMD(cmd_redraw);
CMD(cmd_scroll_line_down);
CMD(cmd_scroll_line_up);
CMD(cmd_scroll_up);
CMD(cmd_scroll_down);
CMD(cmd_beginning_of_buffer);
CMD(cmd_end_of_buffer);
CMD(cmd_kill_telescope);
CMD(cmd_push_button);
CMD(cmd_push_button_new_tab);
CMD(cmd_previous_button);
CMD(cmd_next_button);
CMD(cmd_previous_page);
CMD(cmd_next_page);
CMD(cmd_clear_minibuf);
CMD(cmd_execute_extended_command);
CMD(cmd_tab_close);
CMD(cmd_tab_close_other);
CMD(cmd_tab_new);
CMD(cmd_tab_next);
CMD(cmd_tab_previous);
CMD(cmd_load_url);
CMD(cmd_load_current_url);
CMD(cmd_bookmark_page);
CMD(cmd_goto_bookmarks);

CMD(cmd_mini_delete_char);
CMD(cmd_mini_delete_backward_char);
CMD(cmd_mini_kill_line);
CMD(cmd_mini_abort);
CMD(cmd_mini_complete_and_exit);
CMD(cmd_mini_previous_history_element);
CMD(cmd_mini_next_history_element);

#include "cmd.gen.h"

static void		 global_key_unbound(void);
static void		 minibuffer_hist_save_entry(void);
static void		 minibuffer_taint_hist(void);
static void		 minibuffer_self_insert(void);
static void		 eecmd_self_insert(void);
static void		 eecmd_select(void);
static void		 ir_self_insert(void);
static void		 ir_select(void);
static void		 lu_self_insert(void);
static void		 lu_select(void);
static void		 bp_select(void);
static void		 yornp_self_insert(void);
static void		 yornp_abort(void);

static struct vline	*nth_line(struct window*, size_t);
static struct tab	*current_tab(void);
static struct window	*current_window(void);
static int		 readkey(void);
static void		 dispatch_stdio(int, short, void*);
static void		 handle_clear_minibuf(int, short, void*);
static void		 handle_resize(int, short, void*);
static int		 wrap_page(struct window*);
static void		 print_vline(struct vline*);
static void		 redraw_tabline(void);
static void		 redraw_body(struct tab*);
static void		 redraw_modeline(struct tab*);
static void		 redraw_minibuffer(void);
static void		 redraw_tab(struct tab*);
static void		 vmessage(const char*, va_list);
static void		 message(const char*, ...) __attribute__((format(printf, 1, 2)));
static void		 start_loading_anim(struct tab*);
static void		 update_loading_anim(int, short, void*);
static void		 stop_loading_anim(struct tab*);
static void		 load_url_in_tab(struct tab*, const char*);
static void		 enter_minibuffer(void(*)(void), void(*)(void), void(*)(void), struct histhead*);
static void		 exit_minibuffer(void);
static void		 switch_to_tab(struct tab*);
static struct tab	*new_tab(const char*);
static void		 usage(void);

static struct { short meta; int key; uint32_t cp; } thiskey;

static WINDOW	*tabline, *body, *modeline, *minibuf;
static int	 body_lines, body_cols;

static struct event	clminibufev;
static struct timeval	clminibufev_timer = { 5, 0 };
static struct timeval	loadingev_timer = { 0, 250000 };

static uint32_t		 tab_counter;

static char	keybuf[64];

static void (*yornp_cb)(int, unsigned int);

struct kmap global_map,
	minibuffer_map,
	*current_map,
	*base_map;

static struct histhead eecmd_history,
	ir_history,
	lu_history;

static int	in_minibuffer;

static struct {
	char		*curmesg;

	char		 prompt[64];
	void		 (*donefn)(void);
	void		 (*abortfn)(void);

	char		 buf[1025];
	struct line	 line;
	struct vline	 vline;
	struct window	 window;

	struct histhead	*history;
	struct hist	*hist_cur;
	size_t		 hist_off;
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
	[LINE_QUOTE] =		{ "> ",		"  " },
	[LINE_PRE_START] =	{ "```",	"   " },
	[LINE_PRE_CONTENT] =	{ "",		"" },
	[LINE_PRE_END] =	{ "```",	"```" },
};

static struct line_face {
	int prefix_prop;
	int text_prop;
} line_faces[] = {
	[LINE_TEXT] =		{ 0,		0 },
	[LINE_LINK] =		{ 0,		A_UNDERLINE },
	[LINE_TITLE_1] =	{ A_BOLD,	A_BOLD },
	[LINE_TITLE_2] =	{ A_BOLD,	A_BOLD },
	[LINE_TITLE_3] =	{ A_BOLD,	A_BOLD },
	[LINE_ITEM] =		{ 0,		0 },
	[LINE_QUOTE] =		{ 0,		A_DIM },
	[LINE_PRE_START] =	{ 0,		0 },
	[LINE_PRE_CONTENT] =	{ 0,		0 },
	[LINE_PRE_END] =	{ 0,		0 },
};

static struct tab_face {
	int background, tab, current_tab;
} tab_face = {
	A_REVERSE, A_REVERSE, A_NORMAL
};

static void
empty_vlist(struct window *window)
{
	struct vline *vl, *t;

	window->current_line = NULL;
	window->line_max = 0;

	TAILQ_FOREACH_SAFE(vl, &window->head, vlines, t) {
		TAILQ_REMOVE(&window->head, vl, vlines);
		free(vl->line);
		free(vl);
	}
}

static inline void
global_set_key(const char *key, void (*fn)(struct window*))
{
	if (!kmap_define_key(&global_map, key, fn))
		_exit(1);
}

static inline void
minibuffer_set_key(const char *key, void (*fn)(struct window*))
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
	global_set_key("M-{",		cmd_backward_paragraph);
	global_set_key("M-}",		cmd_forward_paragraph);
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
	global_set_key("C-x t 1",	cmd_tab_close_other);
	global_set_key("C-x t 2",	cmd_tab_new);
	global_set_key("C-x t o",	cmd_tab_next);
	global_set_key("C-x t O",	cmd_tab_previous);

	global_set_key("M-<",		cmd_beginning_of_buffer);
	global_set_key("M->",		cmd_end_of_buffer);

	global_set_key("C-M-b",		cmd_previous_page);
	global_set_key("C-M-f",		cmd_next_page);

	global_set_key("<f7> a",	cmd_bookmark_page);
	global_set_key("<f7> <f7>",	cmd_goto_bookmarks);

	/* vi/vi-like */
	global_set_key("k",		cmd_previous_line);
	global_set_key("j",		cmd_next_line);
	global_set_key("l",		cmd_forward_char);
	global_set_key("h",		cmd_backward_char);
	global_set_key("{",		cmd_backward_paragraph);
	global_set_key("}",		cmd_forward_paragraph);
	global_set_key("^",		cmd_move_beginning_of_line);
	global_set_key("$",		cmd_move_end_of_line);

	global_set_key("K",		cmd_scroll_line_up);
	global_set_key("J",		cmd_scroll_line_down);

	global_set_key("g D",		cmd_tab_close);
	global_set_key("g N",		cmd_tab_new);
	global_set_key("g t",		cmd_tab_next);
	global_set_key("g T",		cmd_tab_previous);

	global_set_key("g g",		cmd_beginning_of_buffer);
	global_set_key("G",		cmd_end_of_buffer);

	global_set_key("H",		cmd_previous_page);
	global_set_key("L",		cmd_next_page);

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

	global_set_key("M-<left>",	cmd_previous_page);
	global_set_key("M-<right>",	cmd_next_page);

	/* "ncurses standard" */
	global_set_key("C-l",		cmd_redraw);

	/* global */
	global_set_key("C-m",		cmd_push_button);
	global_set_key("M-enter",	cmd_push_button_new_tab);
	global_set_key("M-tab",		cmd_previous_button);
	global_set_key("tab",		cmd_next_button);

	/* === minibuffer map === */
	minibuffer_set_key("ret",		cmd_mini_complete_and_exit);
	minibuffer_set_key("C-g",		cmd_mini_abort);
	minibuffer_set_key("esc",		cmd_mini_abort);
	minibuffer_set_key("C-d",		cmd_mini_delete_char);
	minibuffer_set_key("del",		cmd_mini_delete_backward_char);

	minibuffer_set_key("C-b",		cmd_backward_char);
	minibuffer_set_key("C-f",		cmd_forward_char);
	minibuffer_set_key("<left>",		cmd_backward_char);
	minibuffer_set_key("<right>",		cmd_forward_char);
	minibuffer_set_key("C-e",		cmd_move_end_of_line);
	minibuffer_set_key("C-a",		cmd_move_beginning_of_line);
	minibuffer_set_key("<end>",		cmd_move_end_of_line);
	minibuffer_set_key("<home>",		cmd_move_beginning_of_line);
	minibuffer_set_key("C-k",		cmd_mini_kill_line);

	minibuffer_set_key("M-p",		cmd_mini_previous_history_element);
	minibuffer_set_key("M-n",		cmd_mini_next_history_element);
	minibuffer_set_key("<up>",		cmd_mini_previous_history_element);
	minibuffer_set_key("<down>",		cmd_mini_next_history_element);
}

static void
restore_cursor(struct window *window)
{
	struct vline	*vl;
	const char	*prfx;

	vl = window->current_line;
	if (vl == NULL || vl->line == NULL)
		window->curs_x = window->cpoff = 0;
	else
		window->curs_x = utf8_snwidth(vl->line, window->cpoff);

	if (vl != NULL) {
		prfx = line_prefixes[vl->parent->type].prfx1;
		window->curs_x += utf8_swidth(prfx);
	}
}

static void
cmd_previous_line(struct window *window)
{
	struct vline	*vl;

	if (window->current_line == NULL
	    || (vl = TAILQ_PREV(window->current_line, vhead, vlines)) == NULL)
		return;

	if (--window->curs_y < 0) {
		window->curs_y = 0;
		cmd_scroll_line_up(window);
		return;
	}

	window->current_line = vl;
	restore_cursor(window);
}

static void
cmd_next_line(struct window *window)
{
	struct vline	*vl;

	if (window->current_line == NULL
	    || (vl = TAILQ_NEXT(window->current_line, vlines)) == NULL)
		return;

	if (++window->curs_y > body_lines-1) {
		window->curs_y = body_lines-1;
		cmd_scroll_line_down(window);
		return;
	}

	window->current_line = vl;
	restore_cursor(window);
}

static void
cmd_backward_char(struct window *window)
{
	if (window->cpoff != 0)
		window->cpoff--;
	restore_cursor(window);
}

static void
cmd_forward_char(struct window *window)
{
	window->cpoff++;
	restore_cursor(window);
}

static void
cmd_backward_paragraph(struct window *window)
{
	do {
		if (window->current_line == NULL ||
		    window->current_line == TAILQ_FIRST(&window->head)) {
			message("No previous paragraph");
			return;
		}
		cmd_previous_line(window);
	} while (window->current_line->line != NULL ||
	    window->current_line->parent->type != LINE_TEXT);
}

static void
cmd_forward_paragraph(struct window *window)
{
	do {
		if (window->current_line == NULL ||
		    window->current_line == TAILQ_LAST(&window->head, vhead)) {
			message("No next paragraph");
			return;
		}
		cmd_next_line(window);
	} while (window->current_line->line != NULL ||
	    window->current_line->parent->type != LINE_TEXT);
}

static void
cmd_move_beginning_of_line(struct window *window)
{
	window->cpoff = 0;
	restore_cursor(window);
}

static void
cmd_move_end_of_line(struct window *window)
{
	struct vline	*vl;

	vl = window->current_line;
	if (vl->line == NULL)
		return;
	window->cpoff = utf8_cplen(vl->line);
	restore_cursor(window);
}

static void
cmd_redraw(struct window *window)
{
	handle_resize(0, 0, NULL);
}

static void
cmd_scroll_line_up(struct window *window)
{
	struct vline	*vl;

	if (window->line_off == 0)
		return;

	vl = nth_line(window, --window->line_off);
	wscrl(body, -1);
	wmove(body, 0, 0);
	print_vline(vl);

	window->current_line = TAILQ_PREV(window->current_line, vhead, vlines);
	restore_cursor(window);
}

static void
cmd_scroll_line_down(struct window *window)
{
	struct vline	*vl;

	vl = window->current_line;
	if ((vl = TAILQ_NEXT(vl, vlines)) == NULL)
		return;
	window->current_line = vl;

	window->line_off++;
	wscrl(body, 1);

	if (window->line_max - window->line_off < (size_t)body_lines)
		return;

	vl = nth_line(window, window->line_off + body_lines-1);
	wmove(body, body_lines-1, 0);
	print_vline(vl);

	restore_cursor(window);
}

static void
cmd_scroll_up(struct window *window)
{
	size_t off;

	off = body_lines+1;

	for (; off > 0; --off)
		cmd_scroll_line_up(window);
}

static void
cmd_scroll_down(struct window *window)
{
	size_t off;

	off = body_lines+1;

	for (; off > 0; --off)
		cmd_scroll_line_down(window);
}

static void
cmd_beginning_of_buffer(struct window *window)
{
	window->current_line = TAILQ_FIRST(&window->head);
	window->line_off = 0;
	window->curs_y = 0;
	window->cpoff = 0;
	restore_cursor(window);
}

static void
cmd_end_of_buffer(struct window *window)
{
	ssize_t off;

	off = window->line_max - body_lines;
	off = MAX(0, off);

	window->line_off = off;
	window->curs_y = MIN((size_t)body_lines, window->line_max-1);

	window->current_line = TAILQ_LAST(&window->head, vhead);
	window->cpoff = body_cols;
	restore_cursor(window);
}

static void
cmd_kill_telescope(struct window *window)
{
	event_loopbreak();
}

static void
cmd_push_button(struct window *window)
{
	struct vline	*vl;
	size_t		 nth;

	nth = window->line_off + window->curs_y;
	if (nth >= window->line_max)
		return;
	vl = nth_line(window, nth);
	if (vl->parent->type != LINE_LINK)
		return;

	load_url_in_tab(current_tab(), vl->parent->alt);
}

static void
cmd_push_button_new_tab(struct window *window)
{
	struct vline	*vl;
	size_t		 nth;

	nth = window->line_off + window->curs_y;
	if (nth > window->line_max)
		return;
	vl = nth_line(window, nth);
	if (vl->parent->type != LINE_LINK)
		return;

	new_tab(vl->parent->alt);
}

static void
cmd_previous_button(struct window *window)
{
	do {
		if (window->current_line == NULL ||
		    window->current_line == TAILQ_FIRST(&window->head)) {
			message("No previous link");
			return;
		}
		cmd_previous_line(window);
	} while (window->current_line->parent->type != LINE_LINK);
}

static void
cmd_next_button(struct window *window)
{
	do {
		if (window->current_line == NULL ||
		    window->current_line == TAILQ_LAST(&window->head, vhead)) {
			message("No next link");
			return;
		}
		cmd_next_line(window);
	} while (window->current_line->parent->type != LINE_LINK);
}

static void
cmd_previous_page(struct window *window)
{
	struct tab *tab = current_tab();

	if (!load_previous_page(tab))
		message("No previous page");
	else
		start_loading_anim(tab);
}

static void
cmd_next_page(struct window *window)
{
	struct tab *tab = current_tab();

	if (!load_next_page(tab))
		message("No next page");
	else
		start_loading_anim(tab);
}

static void
cmd_clear_minibuf(struct window *window)
{
	handle_clear_minibuf(0, 0, NULL);
}

static void
cmd_execute_extended_command(struct window *window)
{
	size_t	 len;

	if (in_minibuffer) {
		message("We don't have enable-recursive-minibuffers");
		return;
	}

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
cmd_tab_close(struct window *window)
{
	struct tab *tab, *t;

	tab = current_tab();
	if (TAILQ_PREV(tab, tabshead, tabs) == NULL &&
	    TAILQ_NEXT(tab, tabs) == NULL) {
		message("Can't close the only tab.");
		return;
	}

	if (evtimer_pending(&tab->loadingev, NULL))
		evtimer_del(&tab->loadingev);

	stop_tab(tab);

	if ((t = TAILQ_PREV(tab, tabshead, tabs)) == NULL)
		t = TAILQ_NEXT(tab, tabs);
	TAILQ_REMOVE(&tabshead, tab, tabs);
	free(tab);

	switch_to_tab(t);
}

static void
cmd_tab_close_other(struct window *window)
{
	struct tab *tab, *t, *i;

	tab = current_tab();
	TAILQ_FOREACH_SAFE(t, &tabshead, tabs, i) {
		if (t->flags & TAB_CURRENT)
			continue;

		stop_tab(t);
		TAILQ_REMOVE(&tabshead, t, tabs);
		free(t);
	}
}

static void
cmd_tab_new(struct window *window)
{
	new_tab(NEW_TAB_URL);
}

static void
cmd_tab_next(struct window *window)
{
	struct tab *tab, *t;

	tab = current_tab();
	tab->flags &= ~TAB_CURRENT;

	if ((t = TAILQ_NEXT(tab, tabs)) == NULL)
		t = TAILQ_FIRST(&tabshead);
	t->flags |= TAB_CURRENT;
}

static void
cmd_tab_previous(struct window *window)
{
	struct tab *tab, *t;

	tab = current_tab();
	tab->flags &= ~TAB_CURRENT;

	if ((t = TAILQ_PREV(tab, tabshead, tabs)) == NULL)
		t = TAILQ_LAST(&tabshead, tabshead);
	t->flags |= TAB_CURRENT;
}

static void
cmd_load_url(struct window *window)
{
	if (in_minibuffer) {
		message("We don't have enable-recursive-minibuffers");
		return;
	}

	enter_minibuffer(lu_self_insert, lu_select, exit_minibuffer,
	    &lu_history);
	strlcpy(ministate.prompt, "Load URL: ", sizeof(ministate.prompt));
}

static void
cmd_load_current_url(struct window *window)
{
	struct tab *tab = current_tab();

	if (in_minibuffer) {
		message("We don't have enable-recursive-minibuffers");
		return;
	}

	enter_minibuffer(lu_self_insert, lu_select, exit_minibuffer,
	    &lu_history);
	strlcpy(ministate.prompt, "Load URL: ", sizeof(ministate.prompt));
	strlcpy(ministate.buf, tab->hist_cur->h, sizeof(ministate.buf));
	ministate.window.cpoff = utf8_cplen(ministate.buf);
}

static void
cmd_bookmark_page(struct window *window)
{
	struct tab *tab = current_tab();

	enter_minibuffer(lu_self_insert, bp_select, exit_minibuffer, NULL);
	strlcpy(ministate.prompt, "Bookmark URL: ", sizeof(ministate.prompt));
	strlcpy(ministate.buf, tab->hist_cur->h, sizeof(ministate.buf));
	ministate.window.cpoff = utf8_cplen(ministate.buf);
}

static void
cmd_goto_bookmarks(struct window *window)
{
	load_url_in_tab(current_tab(), "about:bookmarks");
}

static void
cmd_mini_delete_char(struct window *window)
{
	char *c, *n;

	if (!in_minibuffer) {
		message("text is read-only");
		return;
	}

	minibuffer_taint_hist();

	c = utf8_nth(window->current_line->line, window->cpoff);
	if (*c == '\0')
		return;
	n = utf8_next_cp(c);

	memmove(c, n, strlen(n)+1);
}

static void
cmd_mini_delete_backward_char(struct window *window)
{
	char *c, *p, *start;

	if (!in_minibuffer) {
		message("text is read-only");
		return;
	}

	minibuffer_taint_hist();

	c = utf8_nth(window->current_line->line, window->cpoff);
	start = window->current_line->line;
	if (c == start)
		return;
	p = utf8_prev_cp(c-1, start);

	memmove(p, c, strlen(c)+1);
	window->cpoff--;
}

static void
cmd_mini_kill_line(struct window *window)
{
	char *c;

	if (!in_minibuffer) {
		message("text is read-only");
		return;
	}

	minibuffer_taint_hist();
	c = utf8_nth(window->current_line->line, window->cpoff);
	*c = '\0';
}

static void
cmd_mini_abort(struct window *window)
{
	if (!in_minibuffer)
		return;

	ministate.abortfn();
}

static void
cmd_mini_complete_and_exit(struct window *window)
{
	if (!in_minibuffer)
		return;

	minibuffer_taint_hist();
	ministate.donefn();
}

static void
cmd_mini_previous_history_element(struct window *window)
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

	if (ministate.hist_cur != NULL)
		window->current_line->line = ministate.hist_cur->h;
}

static void
cmd_mini_next_history_element(struct window *window)
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

	if (ministate.hist_cur != NULL)
		window->current_line->line = ministate.hist_cur->h;
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
	char	*c, tmp[5] = {0};
	size_t	len;

	minibuffer_taint_hist();

	if (thiskey.cp == 0)
		return;

	len = utf8_encode(thiskey.cp, tmp);
	c = utf8_nth(ministate.window.current_line->line, ministate.window.cpoff);
	if (c + len > ministate.buf + sizeof(ministate.buf) - 1)
		return;

	memmove(c + len, c, strlen(c)+1);
	memcpy(c, tmp, len);
	ministate.window.cpoff++;
}

static void
eecmd_self_insert(void)
{
	if (thiskey.meta || unicode_isspace(thiskey.cp) ||
	    !unicode_isgraph(thiskey.cp)) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

static void
eecmd_select(void)
{
	struct cmds *cmd;

	exit_minibuffer();
	minibuffer_hist_save_entry();

	for (cmd = cmds; cmd->cmd != NULL; ++cmd) {
		if (!strcmp(cmd->cmd, ministate.buf)) {
			cmd->fn(current_window());
			return;
		}
	}

	message("Unknown command: %s", ministate.buf);
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
	if (thiskey.meta || unicode_isspace(thiskey.key) ||
	    !unicode_isgraph(thiskey.key)) {
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

static void
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

	yornp_cb(thiskey.key == 'y', current_tab()->id);
	exit_minibuffer();
}

static void
yornp_abort(void)
{
	yornp_cb(0, current_tab()->id);
	exit_minibuffer();
}

static struct vline *
nth_line(struct window *window, size_t n)
{
	struct vline	*vl;
	size_t		 i;

	i = 0;
	TAILQ_FOREACH(vl, &window->head, vlines) {
		if (i == n)
			return vl;
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

static struct window *
current_window(void)
{
	if (in_minibuffer)
		return &ministate.window;
	return &current_tab()->window;
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
				k->fn(current_window());
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

	wrap_page(&tab->window);
	redraw_tab(tab);
}

static int
wrap_page(struct window *window)
{
	struct line		*l;
	const struct line	*orig;
	struct vline		*vl;
	const char		*prfx;

	orig = window->current_line == NULL
		? NULL
		: window->current_line->parent;
	window->current_line = NULL;

	window->curs_y = 0;
	window->line_off = 0;

	empty_vlist(window);

	TAILQ_FOREACH(l, &window->page.head, lines) {
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
			wrap_text(window, prfx, l, body_cols);
			break;
		case LINE_PRE_CONTENT:
                        hardwrap_text(window, l, body_cols);
			break;
		}

		if (orig == l && window->current_line == NULL) {
			window->line_off = window->line_max-1;
			window->current_line = TAILQ_LAST(&window->head, vhead);

			while (1) {
				vl = TAILQ_PREV(window->current_line, vhead, vlines);
				if (vl == NULL || vl->parent != orig)
					break;
				window->current_line = vl;
				window->line_off--;
			}
		}
	}

        if (window->current_line == NULL)
		window->current_line = TAILQ_FIRST(&window->head);

	return 1;
}

static void
print_vline(struct vline *vl)
{
	const char *text = vl->line;
	const char *prfx;
	int prefix_face = line_faces[vl->parent->type].prefix_prop;
	int text_face = line_faces[vl->parent->type].text_prop;

	if (!vl->flags)
		prfx = line_prefixes[vl->parent->type].prfx1;
	else
		prfx = line_prefixes[vl->parent->type].prfx2;

	if (text == NULL)
		text = "";

	wattron(body, prefix_face);
	wprintw(body, "%s", prfx);
	wattroff(body, prefix_face);

	wattron(body, text_face);
	wprintw(body, "%s", text);
	wattroff(body, text_face);
}

static void
redraw_tabline(void)
{
	struct tab	*tab;
	size_t		 toskip;
	int		 current, x, y, truncated;
	const char	*title;
	char		 buf[25];

	toskip = 0;
	x = 1;
	TAILQ_FOREACH(tab, &tabshead, tabs) {
		x += sizeof(buf) + 1;
		toskip++;
		if (tab->flags & TAB_CURRENT)
			break;
	}
	if (x < COLS-2)
		toskip = 0;
	else
		toskip--;

	werase(tabline);
	wattron(tabline, tab_face.background);
	wprintw(tabline, toskip == 0 ? " " : "<");
	wattroff(tabline, tab_face.background);

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

		if (*(title = tab->window.page.title) == '\0')
			title = tab->hist_cur->h;

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
			wattron(tabline, tab_face.current_tab);
		else
			wattron(tabline, tab_face.tab);

		wprintw(tabline, "%s", buf);
		if (TAILQ_NEXT(tab, tabs) != NULL)
			wprintw(tabline, " ");

		if (current)
			wattroff(tabline, tab_face.current_tab);
		else
			wattroff(tabline, tab_face.tab);
	}

	wattron(tabline, tab_face.background);
	for (; x < COLS; ++x)
		waddch(tabline, ' ');
	if (truncated)
		mvwprintw(tabline, 0, COLS-1, ">");
}

static inline char
trust_status_char(enum trust_state ts)
{
	switch (ts) {
	case TS_UNKNOWN:	return 'u';
	case TS_UNTRUSTED:	return '!';
	case TS_TRUSTED:	return 'v';
	case TS_VERIFIED:	return 'V';
	}
}

static void
redraw_modeline(struct tab *tab)
{
	double		 pct;
	int		 x, y, max_x, max_y;
	const char	*mode = tab->window.page.name;
	const char	*spin = "-\\|/";

	werase(modeline);
	wattron(modeline, A_REVERSE);
	wmove(modeline, 0, 0);

	wprintw(modeline, "-%c%c %s ",
	    spin[tab->loading_anim_step],
	    trust_status_char(tab->trust),
	    mode == NULL ? "(none)" : mode);

	pct = (tab->window.line_off + tab->window.curs_y) * 100.0 / tab->window.line_max;

	if (tab->window.line_max <= (size_t)body_lines)
                wprintw(modeline, "All ");
	else if (tab->window.line_off == 0)
                wprintw(modeline, "Top ");
	else if (tab->window.line_off + body_lines >= tab->window.line_max)
		wprintw(modeline, "Bottom ");
	else
		wprintw(modeline, "%.0f%% ", pct);

	wprintw(modeline, "%d/%d %s ",
	    tab->window.line_off + tab->window.curs_y,
	    tab->window.line_max,
	    tab->hist_cur->h);

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
	struct tab *tab;
	size_t off_y, off_x = 0;
	char *start, *c;

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
		c = utf8_nth(ministate.window.current_line->line,
		    ministate.window.cpoff);
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
		if (tab->window.current_line != NULL &&
		    tab->window.current_line->parent->type == LINE_LINK)
			waddstr(minibuf, tab->window.current_line->parent->alt);
	}

	if (in_minibuffer)
		wmove(minibuf, 0, off_x + utf8_swidth_between(start, c));
}

static void
redraw_tab(struct tab *tab)
{
	redraw_tabline();
	redraw_body(tab);
	redraw_modeline(tab);
	redraw_minibuffer();

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
	struct vline	*vl;
	int		 line;

	werase(body);

	tab->window.line_off = MIN(tab->window.line_max-1, tab->window.line_off);
	if (TAILQ_EMPTY(&tab->window.head))
		return;

	line = 0;
	vl = nth_line(&tab->window, tab->window.line_off);
	for (; vl != NULL; vl = TAILQ_NEXT(vl, vlines)) {
		wmove(body, line, 0);
		print_vline(vl);
		line++;
		if (line == body_lines)
			break;
	}

	wmove(body, tab->window.curs_y, tab->window.curs_x);
}

static void
vmessage(const char *fmt, va_list ap)
{
	if (evtimer_pending(&clminibufev, NULL))
		evtimer_del(&clminibufev);
	evtimer_set(&clminibufev, handle_clear_minibuf, NULL);
	evtimer_add(&clminibufev, &clminibufev_timer);

	free(ministate.curmesg);

	/* TODO: what to do if the allocation fails here? */
	if (vasprintf(&ministate.curmesg, fmt, ap) == -1)
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
message(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmessage(fmt, ap);
	va_end(ap);
}

static void
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

static void
load_url_in_tab(struct tab *tab, const char *url)
{
	empty_vlist(&tab->window);
	message("Loading %s...", url);
	start_loading_anim(tab);
	load_url(tab, url);

	tab->window.curs_x = 0;
	tab->window.curs_y = 0;
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
	ministate.window.current_line = &ministate.vline;
	ministate.window.current_line->line = ministate.buf;
	ministate.window.cpoff = 0;
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
new_tab(const char *url)
{
	struct tab	*tab;

	if ((tab = calloc(1, sizeof(*tab))) == NULL) {
		event_loopbreak();
		return NULL;
	}

	TAILQ_INIT(&tab->hist.head);

	TAILQ_INIT(&tab->window.head);

	tab->id = tab_counter++;
	switch_to_tab(tab);

	if (TAILQ_EMPTY(&tabshead))
		TAILQ_INSERT_HEAD(&tabshead, tab, tabs);
	else
		TAILQ_INSERT_TAIL(&tabshead, tab, tabs);

	load_url_in_tab(tab, url);
	return tab;
}

static void
usage(void)
{
	fprintf(stderr, "USAGE: %s [url]\n", getprogname());
}

int
ui_init(int argc, char * const *argv)
{
	const char *url = NEW_TAB_URL;
	int ch;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			usage();
			return 0;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		url = argv[0];

	setlocale(LC_ALL, "");

	TAILQ_INIT(&global_map.m);
	global_map.unhandled_input = global_key_unbound;

	TAILQ_INIT(&minibuffer_map.m);

	TAILQ_INIT(&eecmd_history.head);
	TAILQ_INIT(&ir_history.head);
	TAILQ_INIT(&lu_history.head);

	ministate.line.type = LINE_TEXT;
	ministate.vline.parent = &ministate.line;
	ministate.window.current_line = &ministate.vline;

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
	wrap_page(&tab->window);
	if (tab->flags & TAB_CURRENT) {
		restore_cursor(&tab->window);
		redraw_tab(tab);
	}
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
ui_yornp(const char *prompt, void (*fn)(int, unsigned int))
{
	size_t len;

	yornp_cb = fn;
	enter_minibuffer(yornp_self_insert, yornp_self_insert,
	    yornp_abort, NULL);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, prompt, len);
	strlcat(ministate.prompt, " (y or n) ", len);
	redraw_tab(current_tab());
}

void
ui_notify(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmessage(fmt, ap);
	va_end(ap);
}

void
ui_end(void)
{
	endwin();
}
