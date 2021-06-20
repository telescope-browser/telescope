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

#include <string.h>
#include <stdlib.h>

#include "telescope.h"
#include "cmd.gen.h"

void
cmd_previous_line(struct buffer *buffer)
{
	struct vline	*vl;

	if (buffer->current_line == NULL
	    || (vl = TAILQ_PREV(buffer->current_line, vhead, vlines)) == NULL)
		return;

	if (--buffer->curs_y < 0) {
		buffer->curs_y = 0;
		cmd_scroll_line_up(buffer);
		return;
	}

	buffer->current_line = vl;
	restore_cursor(buffer);
}

void
cmd_next_line(struct buffer *buffer)
{
	struct vline	*vl;

	if (buffer->current_line == NULL
	    || (vl = TAILQ_NEXT(buffer->current_line, vlines)) == NULL)
		return;

	if (++buffer->curs_y > body_lines-1) {
		buffer->curs_y = body_lines-1;
		cmd_scroll_line_down(buffer);
		return;
	}

	buffer->current_line = vl;
	restore_cursor(buffer);
}

void
cmd_backward_char(struct buffer *buffer)
{
	if (buffer->cpoff != 0)
		buffer->cpoff--;
	restore_cursor(buffer);
}

void
cmd_forward_char(struct buffer *buffer)
{
	size_t len = 0;

	if (buffer->current_line->line != NULL)
		len = utf8_cplen(buffer->current_line->line);
	if (++buffer->cpoff > len)
		buffer->cpoff = len;
	restore_cursor(buffer);
}

void
cmd_backward_paragraph(struct buffer *buffer)
{
	do {
		if (buffer->current_line == NULL ||
		    buffer->current_line == TAILQ_FIRST(&buffer->head)) {
			message("No previous paragraph");
			return;
		}
		cmd_previous_line(buffer);
	} while (buffer->current_line->line != NULL ||
	    buffer->current_line->parent->type != LINE_TEXT);
}

void
cmd_forward_paragraph(struct buffer *buffer)
{
	do {
		if (buffer->current_line == NULL ||
		    buffer->current_line == TAILQ_LAST(&buffer->head, vhead)) {
			message("No next paragraph");
			return;
		}
		cmd_next_line(buffer);
	} while (buffer->current_line->line != NULL ||
	    buffer->current_line->parent->type != LINE_TEXT);
}

void
cmd_move_beginning_of_line(struct buffer *buffer)
{
	buffer->cpoff = 0;
	restore_cursor(buffer);
}

void
cmd_move_end_of_line(struct buffer *buffer)
{
	struct vline	*vl;

	vl = buffer->current_line;
	if (vl->line == NULL)
		return;
	buffer->cpoff = utf8_cplen(vl->line);
	restore_cursor(buffer);
}

void
cmd_redraw(struct buffer *buffer)
{
        ui_schedule_redraw();
}

void
cmd_scroll_line_up(struct buffer *buffer)
{
	if (buffer->current_line == NULL)
		return;

	if (buffer->line_off == 0)
		return;

	buffer->line_off--;
	buffer->current_line = TAILQ_PREV(buffer->current_line, vhead, vlines);
	restore_cursor(buffer);
}

void
cmd_scroll_line_down(struct buffer *buffer)
{
	struct vline	*vl;

	if (buffer->current_line == NULL)
		return;

	if ((vl = TAILQ_NEXT(buffer->current_line, vlines)) == NULL)
		return;

	buffer->current_line = vl;
	buffer->line_off++;
	restore_cursor(buffer);
}

void
cmd_scroll_up(struct buffer *buffer)
{
	size_t off;

	off = body_lines-1;

	for (; off > 0; --off)
		cmd_scroll_line_up(buffer);
}

void
cmd_scroll_down(struct buffer *buffer)
{
	size_t off;

	off = body_lines-1;

	for (; off > 0; --off)
		cmd_scroll_line_down(buffer);
}

void
cmd_beginning_of_buffer(struct buffer *buffer)
{
	buffer->current_line = TAILQ_FIRST(&buffer->head);
	buffer->line_off = 0;
	buffer->curs_y = 0;
	buffer->cpoff = 0;
	restore_cursor(buffer);
}

void
cmd_end_of_buffer(struct buffer *buffer)
{
	ssize_t off;

	off = buffer->line_max - body_lines;
	off = MAX(0, off);

	buffer->line_off = off;
	buffer->curs_y = MIN((size_t)body_lines, buffer->line_max-1);

	buffer->current_line = TAILQ_LAST(&buffer->head, vhead);
	buffer->cpoff = body_cols;
	restore_cursor(buffer);
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

	vl = buffer->current_line;
	if (vl->parent->type != LINE_LINK)
		return;

	load_url_in_tab(current_tab(), vl->parent->alt);
}

void
cmd_push_button_new_tab(struct buffer *buffer)
{
	struct vline	*vl;

	vl = buffer->current_line;
	if (vl->parent->type != LINE_LINK)
		return;

	new_tab(vl->parent->alt);
}

void
cmd_previous_button(struct buffer *buffer)
{
	do {
		if (buffer->current_line == NULL ||
		    buffer->current_line == TAILQ_FIRST(&buffer->head)) {
			message("No previous link");
			return;
		}
		cmd_previous_line(buffer);
	} while (buffer->current_line->parent->type != LINE_LINK);
}

void
cmd_next_button(struct buffer *buffer)
{
	do {
		if (buffer->current_line == NULL ||
		    buffer->current_line == TAILQ_LAST(&buffer->head, vhead)) {
			message("No next link");
			return;
		}
		cmd_next_line(buffer);
	} while (buffer->current_line->parent->type != LINE_LINK);
}

void
cmd_previous_page(struct buffer *buffer)
{
	struct tab *tab = current_tab();

	if (!load_previous_page(tab))
		message("No previous page");
	else
		start_loading_anim(tab);
}

void
cmd_next_page(struct buffer *buffer)
{
	struct tab *tab = current_tab();

	if (!load_next_page(tab))
		message("No next page");
	else
		start_loading_anim(tab);
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

	strlcat(ministate.prompt, ui_keyname(thiskey.key), len);

	if (thiskey.meta)
		strlcat(ministate.prompt, " ", len);
}

void
cmd_tab_close(struct buffer *buffer)
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

void
cmd_tab_close_other(struct buffer *buffer)
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

void
cmd_tab_new(struct buffer *buffer)
{
	const char *url;

	if ((url = new_tab_url) == NULL)
		url = NEW_TAB_URL;

	new_tab(url);
}

void
cmd_tab_next(struct buffer *buffer)
{
	struct tab *tab, *t;

	tab = current_tab();
	tab->flags &= ~TAB_CURRENT;

	if ((t = TAILQ_NEXT(tab, tabs)) == NULL)
		t = TAILQ_FIRST(&tabshead);
	t->flags |= TAB_CURRENT;
	t->flags &= ~TAB_URGENT;
}

void
cmd_tab_previous(struct buffer *buffer)
{
	struct tab *tab, *t;

	tab = current_tab();
	tab->flags &= ~TAB_CURRENT;

	if ((t = TAILQ_PREV(tab, tabshead, tabs)) == NULL)
		t = TAILQ_LAST(&tabshead, tabshead);
	t->flags |= TAB_CURRENT;
	t->flags &= ~TAB_URGENT;
}

void
cmd_tab_move(struct buffer *buffer)
{
	struct tab *tab, *t;

	tab = current_tab();
	t = TAILQ_NEXT(tab, tabs);
	TAILQ_REMOVE(&tabshead, tab, tabs);

	if (t == NULL)
		TAILQ_INSERT_HEAD(&tabshead, tab, tabs);
	else
		TAILQ_INSERT_AFTER(&tabshead, t, tab, tabs);
}

void
cmd_tab_move_to(struct buffer *buffer)
{
	struct tab *tab, *t;

	tab = current_tab();
	t = TAILQ_PREV(tab, tabshead, tabs);
	TAILQ_REMOVE(&tabshead, tab, tabs);

	if (t == NULL) {
		if (TAILQ_EMPTY(&tabshead))
			TAILQ_INSERT_HEAD(&tabshead, tab, tabs);
		else
			TAILQ_INSERT_TAIL(&tabshead, tab, tabs);
	} else
		TAILQ_INSERT_BEFORE(t, tab, tabs);
}

void
cmd_load_url(struct buffer *buffer)
{
	if (in_minibuffer) {
		message("We don't have enable-recursive-minibuffers");
		return;
	}

	enter_minibuffer(lu_self_insert, lu_select, exit_minibuffer,
	    &lu_history);
	strlcpy(ministate.prompt, "Load URL: ", sizeof(ministate.prompt));
	strlcpy(ministate.buf, "gemini://", sizeof(ministate.buf));
	cmd_move_end_of_line(&ministate.buffer);
}

void
cmd_load_current_url(struct buffer *buffer)
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
	ministate.buffer.cpoff = utf8_cplen(ministate.buf);
}

void
cmd_bookmark_page(struct buffer *buffer)
{
	struct tab *tab = current_tab();

	enter_minibuffer(lu_self_insert, bp_select, exit_minibuffer, NULL);
	strlcpy(ministate.prompt, "Bookmark URL: ", sizeof(ministate.prompt));
	strlcpy(ministate.buf, tab->hist_cur->h, sizeof(ministate.buf));
	ministate.buffer.cpoff = utf8_cplen(ministate.buf);
}

void
cmd_list_bookmarks(struct buffer *buffer)
{
	load_url_in_tab(current_tab(), "about:bookmarks");
}

void
cmd_toggle_help(struct buffer *buffer)
{
	ui_toggle_side_window();
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

	if (!in_minibuffer) {
		message("text is read-only");
		return;
	}

	minibuffer_taint_hist();

	c = utf8_nth(buffer->current_line->line, buffer->cpoff);
	if (*c == '\0')
		return;
	n = utf8_next_cp(c);

	memmove(c, n, strlen(n)+1);
}

void
cmd_mini_delete_backward_char(struct buffer *buffer)
{
	char *c, *p, *start;

	if (!in_minibuffer) {
		message("text is read-only");
		return;
	}

	minibuffer_taint_hist();

	c = utf8_nth(buffer->current_line->line, buffer->cpoff);
	start = buffer->current_line->line;
	if (c == start)
		return;
	p = utf8_prev_cp(c-1, start);

	memmove(p, c, strlen(c)+1);
	buffer->cpoff--;
}

void
cmd_mini_kill_line(struct buffer *buffer)
{
	char *c;

	if (!in_minibuffer) {
		message("text is read-only");
		return;
	}

	minibuffer_taint_hist();
	c = utf8_nth(buffer->current_line->line, buffer->cpoff);
	*c = '\0';
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
			message("No prev item");
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
			message("No next item");
	} else {
		ministate.hist_off++;
	}

	if (ministate.hist_cur != NULL)
		buffer->current_line->line = ministate.hist_cur->h;
}
