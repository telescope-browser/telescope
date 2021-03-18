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

#define NEW_TAB_URL	"about:new"

static struct event	stdioev, winchev;

static void		 load_default_keys(void);
static void		 empty_vlist(struct tab*);
static void		 restore_cursor(struct tab *);

static void		 cmd_previous_line(struct tab*);
static void		 cmd_next_line(struct tab*);
static void		 cmd_backward_char(struct tab*);
static void		 cmd_forward_char(struct tab*);
static void		 cmd_backward_paragraph(struct tab*);
static void		 cmd_forward_paragraph(struct tab*);
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
static void		 cmd_previous_button(struct tab*);
static void		 cmd_next_button(struct tab*);
static void		 cmd_previous_page(struct tab*);
static void		 cmd_next_page(struct tab*);
static void		 cmd_clear_minibuf(struct tab*);
static void		 cmd_execute_extended_command(struct tab*);
static void		 cmd_tab_close(struct tab*);
static void		 cmd_tab_close_other(struct tab*);
static void		 cmd_tab_new(struct tab*);
static void		 cmd_tab_next(struct tab*);
static void		 cmd_tab_previous(struct tab*);
static void		 cmd_load_url(struct tab*);
static void		 cmd_load_current_url(struct tab*);
static void		 cmd_bookmark_page(struct tab*);
static void		 cmd_goto_bookmarks(struct tab*);

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
static void		 bp_select(void);

static struct vline	*nth_line(struct tab*, size_t);
static struct tab	*current_tab(void);
static void		 dispatch_stdio(int, short, void*);
static void		 handle_clear_minibuf(int, short, void*);
static void		 handle_resize(int, short, void*);
static int		 wrap_page(struct tab*);
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

static struct { int meta, key; } thiskey;

static WINDOW	*tabline, *body, *modeline, *minibuf;
static int	 body_lines, body_cols;

static struct event	clminibufev;
static struct timeval	clminibufev_timer = { 5, 0 };
static struct timeval	loadingev_timer = { 0, 250000 };

static uint32_t		 tab_counter;

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
empty_vlist(struct tab *tab)
{
	struct vline *vl, *t;

	tab->s.current_line = NULL;
	tab->s.line_max = 0;

	TAILQ_FOREACH_SAFE(vl, &tab->s.head, vlines, t) {
		TAILQ_REMOVE(&tab->s.head, vl, vlines);
		free(vl->line);
		free(vl);
	}
}

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

static void
restore_cursor(struct tab *tab)
{
	struct vline	*vl;
	const char	*prfx;

	vl =tab->s.current_line;
	if (vl == NULL || vl->line == NULL)
		tab->s.curs_x = tab->s.line_x = 0;
	else
		tab->s.line_x = MIN(tab->s.line_x, strlen(vl->line));

	if (vl != NULL) {
		prfx = line_prefixes[vl->parent->type].prfx1;
		tab->s.curs_x = tab->s.line_x + strlen(prfx);
	}

	wmove(body, tab->s.curs_y, tab->s.curs_x);
}

static void
cmd_previous_line(struct tab *tab)
{
	struct vline	*vl;

	if (tab->s.current_line == NULL
	    || (vl = TAILQ_PREV(tab->s.current_line, vhead, vlines)) == NULL)
		return;

	if (--tab->s.curs_y < 0) {
		tab->s.curs_y = 0;
		cmd_scroll_line_up(tab);
		return;
	}

	tab->s.current_line = vl;
	restore_cursor(tab);
}

static void
cmd_next_line(struct tab *tab)
{
	struct vline	*vl;

	if (tab->s.current_line == NULL
	    || (vl = TAILQ_NEXT(tab->s.current_line, vlines)) == NULL)
		return;

	if (++tab->s.curs_y > body_lines-1) {
		tab->s.curs_y = body_lines-1;
		cmd_scroll_line_down(tab);
		return;
	}

	tab->s.current_line = vl;
	restore_cursor(tab);
}

static void
cmd_backward_char(struct tab *tab)
{
	if (tab->s.line_x != 0)
		tab->s.line_x--;
	restore_cursor(tab);
}

static void
cmd_forward_char(struct tab *tab)
{
	tab->s.line_x++;
	restore_cursor(tab);
}

static void
cmd_backward_paragraph(struct tab *tab)
{
	do {
		if (tab->s.current_line == NULL ||
		    tab->s.current_line == TAILQ_FIRST(&tab->s.head)) {
			message("No previous paragraph");
			return;
		}
		cmd_previous_line(tab);
	} while (tab->s.current_line->line != NULL ||
	    tab->s.current_line->parent->type != LINE_TEXT);
}

static void
cmd_forward_paragraph(struct tab *tab)
{
	do {
		if (tab->s.current_line == NULL ||
		    tab->s.current_line == TAILQ_LAST(&tab->s.head, vhead)) {
			message("No next paragraph");
			return;
		}
		cmd_next_line(tab);
	} while (tab->s.current_line->line != NULL ||
	    tab->s.current_line->parent->type != LINE_TEXT);
}

static void
cmd_move_beginning_of_line(struct tab *tab)
{
	tab->s.line_x = 0;
	restore_cursor(tab);
}

static void
cmd_move_end_of_line(struct tab *tab)
{
	struct vline	*vl;

	vl = tab->s.current_line;
	if (vl->line == NULL)
		return;
	tab->s.line_x = body_cols;
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
	struct vline	*vl;

	if (tab->s.line_off == 0)
		return;

	vl = nth_line(tab, --tab->s.line_off);
	wscrl(body, -1);
	wmove(body, 0, 0);
	print_vline(vl);

	tab->s.current_line = TAILQ_PREV(tab->s.current_line, vhead, vlines);
}

static void
cmd_scroll_line_down(struct tab *tab)
{
	struct vline	*vl;

	vl = tab->s.current_line;
	if ((vl = TAILQ_NEXT(vl, vlines)) == NULL)
		return;
	tab->s.current_line = vl;

	tab->s.line_off++;
	wscrl(body, 1);

	if (tab->s.line_max - tab->s.line_off < (size_t)body_lines)
		return;

	vl = nth_line(tab, tab->s.line_off + body_lines-1);
	wmove(body, body_lines-1, 0);
	print_vline(vl);
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
	tab->s.current_line = TAILQ_FIRST(&tab->s.head);
	tab->s.line_off = 0;
	tab->s.curs_y = 0;
	tab->s.line_x = 0;
	restore_cursor(tab);
	redraw_body(tab);
}

static void
cmd_end_of_buffer(struct tab *tab)
{
	ssize_t off;

	off = tab->s.line_max - body_lines;
	off = MAX(0, off);

	tab->s.line_off = off;
	tab->s.curs_y = MIN((size_t)body_lines, tab->s.line_max-1);

	tab->s.current_line = TAILQ_LAST(&tab->s.head, vhead);
	tab->s.line_x = body_cols;
	restore_cursor(tab);
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
	struct vline	*vl;
	size_t		 nth;

	nth = tab->s.line_off + tab->s.curs_y;
	if (nth >= tab->s.line_max)
		return;
	vl = nth_line(tab, nth);
	if (vl->parent->type != LINE_LINK)
		return;

	load_url_in_tab(tab, vl->parent->alt);
}

static void
cmd_push_button_new_tab(struct tab *tab)
{
	struct vline	*vl;
	size_t		 nth;

	nth = tab->s.line_off + tab->s.curs_y;
	if (nth > tab->s.line_max)
		return;
	vl = nth_line(tab, nth);
	if (vl->parent->type != LINE_LINK)
		return;

	new_tab(vl->parent->alt);
}

static void
cmd_previous_button(struct tab *tab)
{
	do {
		if (tab->s.current_line == NULL ||
		    tab->s.current_line == TAILQ_FIRST(&tab->s.head)) {
			message("No previous link");
			return;
		}
		cmd_previous_line(tab);
	} while (tab->s.current_line->parent->type != LINE_LINK);
}

static void
cmd_next_button(struct tab *tab)
{
	do {
		if (tab->s.current_line == NULL ||
		    tab->s.current_line == TAILQ_LAST(&tab->s.head, vhead)) {
			message("No next link");
			return;
		}
		cmd_next_line(tab);
	} while (tab->s.current_line->parent->type != LINE_LINK);
}

static void
cmd_previous_page(struct tab *tab)
{
	if (!load_previous_page(tab))
		message("No previous page");
	else
		start_loading_anim(tab);
}

static void
cmd_next_page(struct tab *tab)
{
	if (!load_next_page(tab))
		message("No next page");
	else
		start_loading_anim(tab);
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

	if (evtimer_pending(&tab->s.loadingev, NULL))
		evtimer_del(&tab->s.loadingev);

	stop_tab(tab);

	if ((t = TAILQ_PREV(tab, tabshead, tabs)) == NULL)
		t = TAILQ_NEXT(tab, tabs);
	TAILQ_REMOVE(&tabshead, tab, tabs);
	free(tab);

	switch_to_tab(t);
}

static void
cmd_tab_close_other(struct tab *tab)
{
	struct tab *t, *i;

	TAILQ_FOREACH_SAFE(t, &tabshead, tabs, i) {
		if (t->flags & TAB_CURRENT)
			continue;

		stop_tab(t);
		TAILQ_REMOVE(&tabshead, t, tabs);
		free(t);
	}
}

static void
cmd_tab_new(struct tab *tab)
{
	new_tab(NEW_TAB_URL);
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
	strlcpy(ministate.buf, tab->hist_cur->h, sizeof(ministate.buf));
	ministate.off = strlen(tab->hist_cur->h);
	ministate.len = ministate.off;
}

static void
cmd_bookmark_page(struct tab *tab)
{
	enter_minibuffer(lu_self_insert, bp_select, exit_minibuffer, NULL);
	strlcpy(ministate.prompt, "Bookmark URL: ", sizeof(ministate.prompt));
	strlcpy(ministate.buf, tab->hist_cur->h, sizeof(ministate.buf));
	ministate.off = strlen(tab->hist_cur->h);
	ministate.len = ministate.off;
}

static void
cmd_goto_bookmarks(struct tab *tab)
{
	load_url_in_tab(tab, "about:bookmarks");
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

static void
bp_select(void)
{
	exit_minibuffer();
	if (*ministate.buf != '\0')
		add_to_bookmarks(ministate.buf);
	else
		message("Abort.");
}

static struct vline *
nth_line(struct tab *tab, size_t n)
{
	struct vline	*vl;
	size_t		 i;

	i = 0;
	TAILQ_FOREACH(vl, &tab->s.head, vlines) {
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

static void
dispatch_stdio(int fd, short ev, void *d)
{
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

static int
wrap_page(struct tab *tab)
{
	struct line		*l;
	const struct line	*orig;
	struct vline		*vl;
	const char		*prfx;

	orig = tab->s.current_line == NULL
		? NULL
		: tab->s.current_line->parent;
	tab->s.current_line = NULL;

	tab->s.curs_y = 0;
	tab->s.line_off = 0;

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
		case LINE_PRE_START:
		case LINE_PRE_END:
			wrap_text(tab, prfx, l, body_cols);
			break;
		case LINE_PRE_CONTENT:
                        hardwrap_text(tab, l, body_cols);
			break;
		}

		if (orig == l && tab->s.current_line == NULL) {
			tab->s.line_off = tab->s.line_max-1;
			tab->s.current_line = TAILQ_LAST(&tab->s.head, vhead);

			while (1) {
				vl = TAILQ_PREV(tab->s.current_line, vhead, vlines);
				if (vl == NULL || vl->parent != orig)
					break;
				tab->s.current_line = vl;
				tab->s.line_off--;
			}
		}
	}

        if (tab->s.current_line == NULL)
		tab->s.current_line = TAILQ_FIRST(&tab->s.head);

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

		if (*(title = tab->page.title) == '\0')
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
	const char	*mode = tab->page.name;
	const char	*spin = "-\\|/";

	werase(modeline);
	wattron(modeline, A_REVERSE);
	wmove(modeline, 0, 0);

	wprintw(modeline, "-%c%c %s ",
	    spin[tab->s.loading_anim_step],
	    trust_status_char(tab->trust),
	    mode == NULL ? "(none)" : mode);

	pct = (tab->s.line_off + tab->s.curs_y) * 100.0 / tab->s.line_max;

	if (tab->s.line_max <= (size_t)body_lines)
                wprintw(modeline, "All ");
	else if (tab->s.line_off == 0)
                wprintw(modeline, "Top ");
	else if (tab->s.line_off + body_lines >= tab->s.line_max)
		wprintw(modeline, "Bottom ");
	else
		wprintw(modeline, "%.0f%% ", pct);

	wprintw(modeline, "%d/%d %s ",
	    tab->s.line_off + tab->s.curs_y,
	    tab->s.line_max,
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
	size_t skip = 0, off_x = 0, off_y = 0;
	struct tab *tab;

	werase(minibuf);
	if (in_minibuffer) {
		mvwprintw(minibuf, 0, 0, "%s", ministate.prompt);
		if (ministate.hist_cur != NULL)
			wprintw(minibuf, "(%zu/%zu) ",
			    ministate.hist_off + 1,
			    ministate.history->len);

		getyx(minibuf, off_y, off_x);

		while (ministate.off - skip > (size_t)COLS / 2) {
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

	/* If nothing else, show the URL at point */
	if (!in_minibuffer && ministate.curmesg == NULL && *keybuf == '\0') {
		tab = current_tab();
		if (tab->s.current_line != NULL &&
		    tab->s.current_line->parent->type == LINE_LINK)
			wprintw(minibuf, "%s",
			    tab->s.current_line->parent->alt);
	}

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
	struct vline	*vl;
	int		 line;

	werase(body);

	tab->s.line_off = MIN(tab->s.line_max-1, tab->s.line_off);
	if (TAILQ_EMPTY(&tab->s.head))
		return;

	line = 0;
	vl = nth_line(tab, tab->s.line_off);
	for (; vl != NULL; vl = TAILQ_NEXT(vl, vlines)) {
		wmove(body, line, 0);
		print_vline(vl);
		line++;
		if (line == body_lines)
			break;
	}
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
	if (tab->s.loading_anim)
		return;
	tab->s.loading_anim = 1;
	evtimer_set(&tab->s.loadingev, update_loading_anim, tab);
	evtimer_add(&tab->s.loadingev, &loadingev_timer);
}

static void
update_loading_anim(int fd, short ev, void *d)
{
	struct tab	*tab = d;

	tab->s.loading_anim_step = (tab->s.loading_anim_step+1)%4;

	if (tab->flags & TAB_CURRENT) {
		redraw_modeline(tab);
		wrefresh(modeline);
		wrefresh(body);
		if (in_minibuffer)
			wrefresh(minibuf);
	}

	evtimer_add(&tab->s.loadingev, &loadingev_timer);
}

static void
stop_loading_anim(struct tab *tab)
{
	if (!tab->s.loading_anim)
		return;
	evtimer_del(&tab->s.loadingev);
	tab->s.loading_anim = 0;
	tab->s.loading_anim_step = 0;

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

	tab->s.curs_x = 0;
	tab->s.curs_y = 0;
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
new_tab(const char *url)
{
	struct tab	*tab;

	if ((tab = calloc(1, sizeof(*tab))) == NULL) {
		event_loopbreak();
		return NULL;
	}

	TAILQ_INIT(&tab->hist.head);

	TAILQ_INIT(&tab->s.head);

	tab->id = tab_counter++;
	switch_to_tab(tab);

	if (TAILQ_EMPTY(&tabshead))
		TAILQ_INSERT_HEAD(&tabshead, tab, tabs);
	else
		TAILQ_INSERT_TAIL(&tabshead, tab, tabs);

	load_url_in_tab(tab, url);
	return tab;
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

	new_tab(NEW_TAB_URL);

	return 1;
}

void
ui_on_tab_loaded(struct tab *tab)
{
	stop_loading_anim(tab);
	message("Loaded %s", tab->hist_cur->h);
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
