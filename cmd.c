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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "compl.h"
#include "defaults.h"
#include "minibuffer.h"
#include "session.h"
#include "telescope.h"
#include "ui.h"
#include "utf8.h"

#define GUARD_RECURSIVE_MINIBUFFER()				\
	do {							\
		if (in_minibuffer) {				\
			message("enable-recursive-minibuffers " \
			    "is not yet available.");		\
			return;					\
		}						\
	} while(0)

#define GUARD_READ_ONLY()				\
	do {						\
		if (!in_minibuffer) {			\
			message("text is read-only");	\
			return;				\
		}					\
	} while(0)

/* return 1 if moved, 0 otherwise */
static inline int
forward_line(struct buffer *buffer, int n)
{
	struct vline *vl;
	int did;

	if (buffer->current_line == NULL)
		return 0;
	vl = buffer->current_line;

	did = 0;
	while (n != 0) {
		if (n > 0) {
			vl = TAILQ_NEXT(vl, vlines);
			if (vl == NULL)
				return did;
			if (vl->parent->flags & L_HIDDEN)
				continue;
			buffer->current_line = vl;
			n--;
		} else {
			vl = TAILQ_PREV(vl, vhead, vlines);
			if (vl == NULL)
				return did;
			if (vl->parent->flags & L_HIDDEN)
				continue;
			if (buffer->current_line == buffer->top_line) {
				buffer->line_off--;
				buffer->top_line = vl;
			}
			buffer->current_line = vl;
			n++;
		}

		did = 1;
	}

	return did;
}

void
cmd_previous_line(struct buffer *buffer)
{
	forward_line(buffer, -1);
}

void
cmd_next_line(struct buffer *buffer)
{
	forward_line(buffer, +1);
}

void
cmd_backward_char(struct buffer *buffer)
{
	if (buffer->cpoff != 0)
		buffer->cpoff--;
}

void
cmd_forward_char(struct buffer *buffer)
{
	size_t len = 0;

	if (buffer->current_line == NULL)
		return;

	if (buffer->current_line->line != NULL)
		len = utf8_cplen(buffer->current_line->line);
	if (++buffer->cpoff > len)
		buffer->cpoff = len;
}

void
cmd_backward_paragraph(struct buffer *buffer)
{
	do {
		if (!forward_line(buffer, -1)) {
			message("No previous paragraph");
			return;
		}
	} while (buffer->current_line->line != NULL ||
	    buffer->current_line->parent->type != LINE_TEXT);
}

void
cmd_forward_paragraph(struct buffer *buffer)
{
	do {
		if (!forward_line(buffer, +1)) {
			message("No next paragraph");
			return;
		}
	} while (buffer->current_line->line != NULL ||
	    buffer->current_line->parent->type != LINE_TEXT);
}

void
cmd_move_beginning_of_line(struct buffer *buffer)
{
	buffer->cpoff = 0;
}

void
cmd_move_end_of_line(struct buffer *buffer)
{
	struct vline	*vl;

	vl = buffer->current_line;
	if (vl == NULL || vl->line == NULL)
		return;
	buffer->cpoff = utf8_cplen(vl->line);
}

void
cmd_redraw(struct buffer *buffer)
{
	ui_schedule_redraw();
}

void
cmd_scroll_line_up(struct buffer *buffer)
{
	struct vline	*vl;

	for (;;) {
		if (buffer->top_line == NULL)
			return;

		if ((vl = TAILQ_PREV(buffer->top_line, vhead, vlines))
		    == NULL)
			return;

		buffer->top_line = vl;

		if (vl->parent->flags & L_HIDDEN)
			continue;

		break;
	}

	buffer->line_off--;

	forward_line(buffer, -1);
}

void
cmd_scroll_line_down(struct buffer *buffer)
{
	if (!forward_line(buffer, +1))
		return;

	for (;;) {
		if (buffer->top_line == NULL)
			return;

		buffer->top_line = TAILQ_NEXT(buffer->top_line, vlines);
		if (buffer->top_line->parent->flags & L_HIDDEN)
			continue;
		break;
	}

	buffer->line_off++;
}

void
cmd_scroll_up(struct buffer *buffer)
{
	struct vline	*vl;
	int		 i;

	if (buffer->top_line == NULL)
		return;

	for (i = 0; i < body_lines; ++i) {
		vl = TAILQ_PREV(buffer->top_line, vhead, vlines);
		if (vl == NULL)
			break;
		buffer->line_off--;
		buffer->top_line = vl;
		forward_line(buffer, -1);
	}
}

void
cmd_scroll_down(struct buffer *buffer)
{
	int i;

	if (buffer->top_line == NULL)
		return;

	for (i = 0; i < body_lines; ++i) {
		if (!forward_line(buffer, +1))
			break;

		buffer->top_line = TAILQ_NEXT(buffer->top_line,
		    vlines);
		buffer->line_off++;
	}
}

void
cmd_beginning_of_buffer(struct buffer *buffer)
{
	buffer->current_line = TAILQ_FIRST(&buffer->head);
	buffer->cpoff = 0;
	buffer->top_line = buffer->current_line;
	buffer->line_off = 0;
}

void
cmd_end_of_buffer(struct buffer *buffer)
{
	buffer->current_line = TAILQ_LAST(&buffer->head, vhead);

	if (buffer->current_line == NULL)
		return;

	/* deal with invisible lines */
	if (buffer->current_line->parent->flags & L_HIDDEN)
		forward_line(buffer, -1);

	cmd_move_end_of_line(buffer);
}

void
cmd_kill_telescope(struct buffer *buffer)
{
	save_session();
	event_loopbreak();
}

void
cmd_push_button(struct buffer *buffer)
{
	struct vline	*vl;
	struct line	*l;

	vl = buffer->current_line;

	if (vl == NULL)
		return;

	switch (vl->parent->type) {
	case LINE_LINK:
		load_url_in_tab(current_tab, vl->parent->alt, NULL, 0);
		break;
	case LINE_PRE_START:
		l = TAILQ_NEXT(vl->parent, lines);
		for (; l != NULL; l = TAILQ_NEXT(l, lines)) {
			if (l->type == LINE_PRE_END)
				break;
			l->flags ^= L_HIDDEN;
			if (l->flags & L_HIDDEN)
				buffer->line_max--;
			else
				buffer->line_max++;
		}
		break;
	default:
		break;
	}
}

void
cmd_push_button_new_tab(struct buffer *buffer)
{
	struct vline	*vl;

	vl = buffer->current_line;
	if (vl == NULL || vl->parent->type != LINE_LINK)
		return;

	new_tab(vl->parent->alt, current_tab->hist_cur->h, current_tab);
}

void
cmd_previous_button(struct buffer *buffer)
{
	struct excursion place;

	save_excursion(&place, buffer);

	do {
		if (!forward_line(buffer, -1)) {
			restore_excursion(&place, buffer);
			message("No previous link");
			return;
		}
	} while (buffer->current_line->parent->type != LINE_LINK);
}

void
cmd_next_button(struct buffer *buffer)
{
	struct excursion place;

	save_excursion(&place, buffer);

	do {
		if (!forward_line(buffer, +1)){
			restore_excursion(&place, buffer);
			message("No next link");
			return;
		}
	} while (buffer->current_line->parent->type != LINE_LINK);
}

static inline int
is_heading(const struct line *l)
{
	return l->type == LINE_TITLE_1 ||
		l->type == LINE_TITLE_2 ||
		l->type == LINE_TITLE_3;
}

void
cmd_previous_heading(struct buffer *buffer)
{
	struct excursion place;

	save_excursion(&place, buffer);

	do {
		if (!forward_line(buffer, -1)) {
			restore_excursion(&place, buffer);
			message("No previous heading");
			return;
		}
	} while (!is_heading(buffer->current_line->parent));
}

void
cmd_next_heading(struct buffer *buffer)
{
	struct excursion place;

	save_excursion(&place, buffer);

	do {
		if (!forward_line(buffer, +1)) {
			restore_excursion(&place, buffer);
			message("No next heading");
			return;
		}
	} while (!is_heading(buffer->current_line->parent));
}

void
cmd_previous_page(struct buffer *buffer)
{
	if (!load_previous_page(current_tab))
		message("No previous page");
	else
		start_loading_anim(current_tab);
}

void
cmd_next_page(struct buffer *buffer)
{
	if (!load_next_page(current_tab))
		message("No next page");
	else
		start_loading_anim(current_tab);
}

void
cmd_clear_minibuf(struct buffer *buffer)
{
	message(NULL);
}

void
cmd_execute_extended_command(struct buffer *buffer)
{
	size_t	 len;

	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(sensible_self_insert, eecmd_select, exit_minibuffer,
	    &eecmd_history, compl_eecmd, NULL);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, "", len);

	if (thiskey.meta)
		strlcat(ministate.prompt, "M-", len);

	strlcat(ministate.prompt, ui_keyname(thiskey.key), len);

	if (thiskey.meta)
		strlcat(ministate.prompt, " ", len);
}

void
cmd_tab_close(struct buffer *buffer)
{
	struct tab *tab, *t;

	tab = current_tab;

	if ((t = TAILQ_NEXT(tab, tabs)) != NULL ||
	    (t = TAILQ_PREV(tab, tabshead, tabs)) != NULL) {
		switch_to_tab(t);
		free_tab(tab);
	} else
		message("Can't close the only tab.");

}

void
cmd_tab_close_other(struct buffer *buffer)
{
	struct tab *t, *i;

	TAILQ_FOREACH_SAFE(t, &tabshead, tabs, i) {
		if (t == current_tab)
			continue;

		free_tab(t);
	}
}

void
cmd_tab_new(struct buffer *buffer)
{
	const char *url;

	if ((url = new_tab_url) == NULL)
		url = NEW_TAB_URL;

	new_tab(url, NULL, NULL);
}

void
cmd_tab_next(struct buffer *buffer)
{
	struct tab *t;

	if ((t = TAILQ_NEXT(current_tab, tabs)) == NULL)
		t = TAILQ_FIRST(&tabshead);
	switch_to_tab(t);
}

void
cmd_tab_previous(struct buffer *buffer)
{
	struct tab *t;

	if ((t = TAILQ_PREV(current_tab, tabshead, tabs)) == NULL)
		t = TAILQ_LAST(&tabshead, tabshead);
	switch_to_tab(t);
}

void
cmd_tab_move(struct buffer *buffer)
{
	struct tab *t;

	t = TAILQ_NEXT(current_tab, tabs);
	TAILQ_REMOVE(&tabshead, current_tab, tabs);

	if (t == NULL)
		TAILQ_INSERT_HEAD(&tabshead, current_tab, tabs);
	else
		TAILQ_INSERT_AFTER(&tabshead, t, current_tab, tabs);
}

void
cmd_tab_move_to(struct buffer *buffer)
{
	struct tab *t;

	t = TAILQ_PREV(current_tab, tabshead, tabs);
	TAILQ_REMOVE(&tabshead, current_tab, tabs);

	if (t == NULL)
		TAILQ_INSERT_TAIL(&tabshead, current_tab, tabs);
	else
		TAILQ_INSERT_BEFORE(t, current_tab, tabs);
}

void
cmd_tab_select(struct buffer *buffer)
{
	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(sensible_self_insert, ts_select, exit_minibuffer,
	    NULL, compl_ts, NULL);
	strlcpy(ministate.prompt, "Select tab: ", sizeof(ministate.prompt));
}

void
cmd_load_url(struct buffer *buffer)
{
	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(sensible_self_insert, lu_select, exit_minibuffer,
	    &lu_history, NULL, NULL);
	strlcpy(ministate.prompt, "Load URL: ", sizeof(ministate.prompt));
}

void
cmd_load_current_url(struct buffer *buffer)
{
	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(sensible_self_insert, lu_select, exit_minibuffer,
	    &lu_history, NULL, NULL);
	strlcpy(ministate.prompt, "Load URL: ", sizeof(ministate.prompt));
	strlcpy(ministate.buf, current_tab->hist_cur->h, sizeof(ministate.buf));
	ministate.buffer.cpoff = utf8_cplen(ministate.buf);
}

void
cmd_reload_page(struct buffer *buffer)
{
	load_url_in_tab(current_tab, current_tab->hist_cur->h, NULL, 1);
}

void
cmd_bookmark_page(struct buffer *buffer)
{
	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(sensible_self_insert, bp_select, exit_minibuffer, NULL,
	    NULL, NULL);
	strlcpy(ministate.prompt, "Bookmark URL: ", sizeof(ministate.prompt));
	strlcpy(ministate.buf, current_tab->hist_cur->h, sizeof(ministate.buf));
	ministate.buffer.cpoff = utf8_cplen(ministate.buf);
}

void
cmd_list_bookmarks(struct buffer *buffer)
{
	load_url_in_tab(current_tab, "about:bookmarks", NULL, 0);
}

void
cmd_toggle_help(struct buffer *buffer)
{
	ui_toggle_side_window(SIDE_WINDOW_LEFT);
}

void
cmd_link_select(struct buffer *buffer)
{
	struct line *l;

	GUARD_RECURSIVE_MINIBUFFER();

	l = TAILQ_FIRST(&buffer->page.head);
	while (l != NULL && l->type != LINE_LINK)
		l = TAILQ_NEXT(l, lines);

	if (l == NULL) {
		message("No links found");
		return;
	}

	enter_minibuffer(sensible_self_insert, ls_select, exit_minibuffer,
	    NULL, compl_ls, l);
	strlcpy(ministate.prompt, "Select link: ", sizeof(ministate.prompt));
}

void
cmd_swiper(struct buffer *buffer)
{
	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(sensible_self_insert, swiper_select, exit_minibuffer,
	    NULL, compl_swiper, TAILQ_FIRST(&buffer->page.head));
	strlcpy(ministate.prompt, "Select line: ", sizeof(ministate.prompt));
}

void
cmd_toc(struct buffer *buffer)
{
	struct line *l;

	GUARD_RECURSIVE_MINIBUFFER();

	l = TAILQ_FIRST(&buffer->page.head);
	while (l != NULL &&
	    l->type != LINE_TITLE_1 &&
	    l->type != LINE_TITLE_2 &&
	    l->type != LINE_TITLE_3)
		l = TAILQ_NEXT(l, lines);

	if (l == NULL) {
		message("No headings found");
		return;
	}

	enter_minibuffer(sensible_self_insert, toc_select, exit_minibuffer,
	    NULL, compl_toc, l);
	strlcpy(ministate.prompt, "Select heading: ",
	    sizeof(ministate.prompt));
}

void
cmd_inc_fill_column(struct buffer *buffer)
{
	if (fill_column == INT_MAX)
		return;

	fill_column += 2;
	message("fill-column: %d", fill_column);

	ui_schedule_redraw();
}

void
cmd_dec_fill_column(struct buffer *buffer)
{
	if (fill_column == INT_MAX || fill_column < 8)
		return;

	fill_column -= 2;
	message("fill-column: %d", fill_column);

	ui_schedule_redraw();
}

void
cmd_olivetti_mode(struct buffer *buffer)
{
	olivetti_mode = !olivetti_mode;
	if (olivetti_mode)
		message("olivetti-mode enabled");
	else
		message("olivetti-mode disabled");

	ui_schedule_redraw();
}

void
cmd_mini_delete_char(struct buffer *buffer)
{
	char *c, *n;

	GUARD_READ_ONLY();

	minibuffer_taint_hist();

	c = utf8_nth(buffer->current_line->line, buffer->cpoff);
	if (*c == '\0')
		return;
	n = utf8_next_cp(c);

	memmove(c, n, strlen(n)+1);

	recompute_completions(0);
}

void
cmd_mini_delete_backward_char(struct buffer *buffer)
{
	char *c, *p, *start;

	GUARD_READ_ONLY();

	minibuffer_taint_hist();

	c = utf8_nth(buffer->current_line->line, buffer->cpoff);
	start = buffer->current_line->line;
	if (c == start)
		return;
	p = utf8_prev_cp(c-1, start);

	memmove(p, c, strlen(c)+1);
	buffer->cpoff--;

	recompute_completions(0);
}

void
cmd_mini_kill_line(struct buffer *buffer)
{
	char *c;

	GUARD_READ_ONLY();

	minibuffer_taint_hist();
	c = utf8_nth(buffer->current_line->line, buffer->cpoff);
	*c = '\0';

	recompute_completions(0);
}

void
cmd_mini_abort(struct buffer *buffer)
{
	if (!in_minibuffer)
		return;

	ministate.abortfn();
}

void
cmd_mini_complete_and_exit(struct buffer *buffer)
{
	if (!in_minibuffer)
		return;

	minibuffer_taint_hist();
	ministate.donefn();
}

void
cmd_mini_previous_history_element(struct buffer *buffer)
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
			message("No prev history item");
	} else {
		ministate.hist_off--;
	}

	if (ministate.hist_cur != NULL)
		buffer->current_line->line = ministate.hist_cur->h;
}

void
cmd_mini_next_history_element(struct buffer *buffer)
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
			message("No next history item");
	} else {
		ministate.hist_off++;
	}

	if (ministate.hist_cur != NULL)
		buffer->current_line->line = ministate.hist_cur->h;
}

void
cmd_previous_completion(struct buffer *buffer)
{
	if (in_minibuffer != MB_COMPREAD)
		return;

	buffer = &ministate.compl.buffer;

	if (buffer->current_line != NULL)
		buffer->current_line->parent->type = LINE_COMPL;

	forward_line(buffer, -1);

	if (buffer->current_line != NULL)
		buffer->current_line->parent->type = LINE_COMPL_CURRENT;
}

void
cmd_next_completion(struct buffer *buffer)
{
	if (in_minibuffer != MB_COMPREAD)
		return;

	buffer = &ministate.compl.buffer;

	if (buffer->current_line != NULL)
		buffer->current_line->parent->type = LINE_COMPL;

	forward_line(buffer, +1);

	if (buffer->current_line != NULL)
		buffer->current_line->parent->type = LINE_COMPL_CURRENT;
}

void
cmd_insert_current_candidate(struct buffer *buffer)
{
	struct vline *vl;

	if (in_minibuffer != MB_COMPREAD)
		return;

	buffer = &ministate.compl.buffer;
	if ((vl = buffer->current_line) == NULL)
		return;

	minibuffer_taint_hist();
	strlcpy(ministate.buf, vl->parent->line, sizeof(ministate.buf));
	ministate.buffer.cpoff = utf8_cplen(ministate.buf);
}

void
cmd_suspend_telescope(struct buffer *buffer)
{
	message("Zzz...");
	ui_suspend();
}

void
cmd_toggle_pre_wrap(struct buffer *buffer)
{
	dont_wrap_pre = !dont_wrap_pre;

	if (dont_wrap_pre)
		message("Don't wrap preformatted blocks");
	else
		message("Wrap preformatted blocks");

	ui_schedule_redraw();
}

void
cmd_mini_goto_beginning(struct buffer *buffer)
{
	struct vline *vl;

	if (!in_minibuffer)
		return;

	buffer = &ministate.compl.buffer;

	if ((vl = buffer->current_line) != NULL)
		vl->parent->type = LINE_COMPL;

	vl = TAILQ_FIRST(&buffer->head);
	while (vl != NULL && vl->parent->flags & L_HIDDEN)
		vl = TAILQ_NEXT(vl, vlines);

	if (vl == NULL)
		return;

	vl->parent->type = LINE_COMPL_CURRENT;
	buffer->top_line = vl;
	buffer->current_line = vl;
}

void
cmd_mini_goto_end(struct buffer *buffer)
{
	struct vline *vl;

	if (!in_minibuffer)
		return;

	buffer = &ministate.compl.buffer;

	if ((vl = buffer->current_line) != NULL)
		vl->parent->type = LINE_COMPL;

	vl = TAILQ_LAST(&buffer->head, vhead);
	while (vl != NULL && vl->parent->flags & L_HIDDEN)
		vl = TAILQ_PREV(vl, vhead, vlines);

	if (vl == NULL)
		return;

	vl->parent->type = LINE_COMPL_CURRENT;
	buffer->current_line = vl;
}

void
cmd_other_window(struct buffer *buffer)
{
	ui_other_window();
}

void
cmd_mini_scroll_up(struct buffer *buffer)
{
	if (!in_minibuffer)
		return;

	buffer = &ministate.compl.buffer;
	if (buffer->current_line == NULL)
		return;

	buffer->current_line->parent->type = LINE_COMPL;
	cmd_scroll_up(buffer);
	buffer->current_line->parent->type = LINE_COMPL_CURRENT;
}

void
cmd_mini_scroll_down(struct buffer *buffer)
{
	if (!in_minibuffer)
		return;

	buffer = &ministate.compl.buffer;
	if (buffer->current_line == NULL)
		return;

	buffer->current_line->parent->type = LINE_COMPL;
	cmd_scroll_down(buffer);
	buffer->current_line->parent->type = LINE_COMPL_CURRENT;
}

void
cmd_toggle_downloads(struct buffer *buffer)
{
	ui_toggle_side_window(SIDE_WINDOW_BOTTOM);
}
