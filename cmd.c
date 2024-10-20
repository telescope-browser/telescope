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

#include "compat.h"

#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <grapheme.h>

#include "certs.h"
#include "cmd.h"
#include "compl.h"
#include "defaults.h"
#include "ev.h"
#include "exec.h"
#include "hist.h"
#include "keymap.h"
#include "mcache.h"
#include "minibuffer.h"
#include "session.h"
#include "telescope.h"
#include "ui.h"
#include "utils.h"

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
	struct vline	*vl;
	char		*text;
	size_t		 left, off, point = 0;

	if ((vl = buffer->current_line) == NULL)
		return;

	text = vl->parent->line + vl->from;
	left = vl->len;

	for (;;) {
		off = grapheme_next_character_break_utf8(text, left);
		if (point + off >= buffer->point_offset)
			break;
		point += off;
		text += off;
		left -= off;
	}

	buffer->point_offset = point;
}

void
cmd_forward_char(struct buffer *buffer)
{
	struct vline	*vl;
	char		*text;
	size_t		 left, off;

	if ((vl = buffer->current_line) == NULL)
		return;

	text = vl->parent->line + vl->from;
	left = vl->len;

	text += buffer->point_offset;
	left -= buffer->point_offset;

	off = grapheme_next_character_break_utf8(text, left);
	buffer->point_offset += off;
}

void
cmd_backward_paragraph(struct buffer *buffer)
{
	do {
		if (!forward_line(buffer, -1)) {
			message("No previous paragraph");
			return;
		}
	} while (buffer->current_line->len != 0 ||
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
	} while (buffer->current_line->len != 0 ||
	    buffer->current_line->parent->type != LINE_TEXT);
}

void
cmd_move_beginning_of_line(struct buffer *buffer)
{
	buffer->point_offset = 0;
}

void
cmd_move_end_of_line(struct buffer *buffer)
{
	struct vline	*vl;

	vl = buffer->current_line;
	if (vl == NULL)
		return;
	buffer->point_offset = vl->len;
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
	buffer->current_line = TAILQ_FIRST(&buffer->vhead);
	buffer->point_offset = 0;
	buffer->top_line = buffer->current_line;
	buffer->line_off = 0;
}

void
cmd_end_of_buffer(struct buffer *buffer)
{
	buffer->current_line = TAILQ_LAST(&buffer->vhead, vhead);

	if (buffer->current_line == NULL)
		return;

	/* deal with invisible lines */
	if (buffer->current_line->parent->flags & L_HIDDEN)
		forward_line(buffer, -1);

	cmd_move_end_of_line(buffer);
}

static void
kill_telescope_cb(int r, void *data)
{
	if (r) {
		save_session();
		ev_break();
	}
}

void
cmd_kill_telescope(struct buffer *buffer)
{
	yornp("really quit?", kill_telescope_cb, NULL);
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
		load_url_in_tab(current_tab, vl->parent->alt, NULL,
		    LU_MODE_NOCACHE);
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

	new_tab(vl->parent->alt, hist_cur(current_tab->hist), current_tab);
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
}

void
cmd_next_page(struct buffer *buffer)
{
	if (!load_next_page(current_tab))
		message("No next page");
}

void
cmd_clear_minibuf(struct buffer *buffer)
{
	message(NULL);
}

void
cmd_execute_extended_command(struct buffer *buffer)
{
	struct minibuffer	 m = {
		.self_insert = sensible_self_insert,
		.done = eecmd_select,
		.history = eecmd_history,
		.complfn = compl_eecmd,
		.must_select = 1,
	};

	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(&m, "%s%s%s", thiskey.meta ? "M-" : "",
	    ui_keyname(thiskey.key), thiskey.meta ? " " : "");
}

void
cmd_tab_close(struct buffer *buffer)
{
	struct tab *tab, *t;

	tab = current_tab;

	if ((t = TAILQ_NEXT(tab, tabs)) != NULL ||
	    (t = TAILQ_PREV(tab, tabshead, tabs)) != NULL) {
		switch_to_tab(t);
		kill_tab(tab, 0);
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

		kill_tab(t, 0);
	}
}

void
cmd_tab_undo_close(struct buffer *buffer)
{
	struct tab *t;

	if ((t = unkill_tab()) == NULL) {
		message("No recently-closed tabs");
		return;
	}

	switch_to_tab(t);
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
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = ts_select,
		.complfn = compl_ts,
		.must_select = 1,
	};

	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(&m, "Select tab: ");
}

void
cmd_load_url(struct buffer *buffer)
{
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = lu_select,
		.history = lu_history,
		.complfn = compl_lu,
	};

	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(&m, "Load URL: ");
}

void
cmd_load_current_url(struct buffer *buffer)
{
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = lu_select,
		.history = lu_history,
		.complfn = compl_lu,
		.input = hist_cur(current_tab->hist),
	};

	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(&m, "Load URL: ");
}

void
cmd_reload_page(struct buffer *buffer)
{
	load_url_in_tab(current_tab, hist_cur(current_tab->hist), NULL,
	    LU_MODE_NOHIST|LU_MODE_NOCACHE);
}

void
cmd_bookmark_page(struct buffer *buffer)
{
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = bp_select,
		.input = hist_cur(current_tab->hist),
	};

	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(&m, "Bookmark URL: ");
}

void
cmd_list_bookmarks(struct buffer *buffer)
{
	load_url_in_tab(current_tab, "about:bookmarks", NULL, LU_MODE_NONE);
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
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = ls_select,
		.complfn = compl_ls,
		.compldata = NULL,
		.must_select = 1,
	};

	GUARD_RECURSIVE_MINIBUFFER();

	l = TAILQ_FIRST(&buffer->head);
	while (l != NULL && l->type != LINE_LINK)
		l = TAILQ_NEXT(l, lines);

	if (l == NULL) {
		message("No links found");
		return;
	}

	m.compldata = l;
	enter_minibuffer(&m, "Select link: ");
}

void
cmd_swiper(struct buffer *buffer)
{
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = swiper_select,
		.complfn = compl_swiper,
		.compldata = TAILQ_FIRST(&buffer->head),
		.must_select = 1,
	};

	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(&m, "Select line: ");
}

void
cmd_toc(struct buffer *buffer)
{
	struct line *l;
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = toc_select,
		.complfn = compl_toc,
		.compldata = NULL,
		.must_select = 1,
	};

	GUARD_RECURSIVE_MINIBUFFER();

	l = TAILQ_FIRST(&buffer->head);
	while (l != NULL &&
	    l->type != LINE_TITLE_1 &&
	    l->type != LINE_TITLE_2 &&
	    l->type != LINE_TITLE_3)
		l = TAILQ_NEXT(l, lines);

	if (l == NULL) {
		message("No headings found");
		return;
	}

	m.compldata = l;
	enter_minibuffer(&m, "Select heading: ");
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
	struct vline	*vl;
	char		*text;
	size_t		 old_point, gap, rest;

	GUARD_READ_ONLY();

	vl = buffer->current_line;
	old_point = buffer->point_offset;
	cmd_forward_char(buffer);
	gap = buffer->point_offset - old_point;
	if (gap == 0)
		return;

	minibuffer_taint_hist();

	text = vl->parent->line + vl->from + old_point;
	rest = vl->len - buffer->point_offset;
	memmove(text, text + gap, rest);
	buffer->point_offset = old_point;

	recompute_completions(0);
}

void
cmd_mini_delete_backward_char(struct buffer *buffer)
{
	struct vline	*vl;
	char		*text;
	size_t		 old_point, gap, rest;

	GUARD_READ_ONLY();

	vl = buffer->current_line;
	old_point = buffer->point_offset;
	cmd_backward_char(buffer);
	gap = old_point - buffer->point_offset;
	if (gap == 0)
		return;

	minibuffer_taint_hist();

	text = vl->parent->line + vl->from + buffer->point_offset;
	rest = vl->len - old_point;
	memmove(text, text + gap, rest);

	recompute_completions(0);
}

void
cmd_mini_kill_line(struct buffer *buffer)
{
	char *line, *c;

	GUARD_READ_ONLY();

	minibuffer_taint_hist();

	line = buffer->current_line->parent->line + buffer->current_line->from;
	c = line + buffer->point_offset;
	*c = '\0';

	recompute_completions(0);
}

void
cmd_mini_kill_whole_line(struct buffer *buffer)
{
	GUARD_READ_ONLY();

	minibuffer_taint_hist();
	*buffer->current_line->parent->line = '\0';
	buffer->point_offset = 0;

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
	struct vline *vl;

	if (!in_minibuffer)
		return;

	if (ministate.compl.must_select && ministate.hist == NULL) {
		vl = ministate.compl.buffer.current_line;
		if (vl == NULL || vl->parent->flags & L_HIDDEN ||
		    vl->parent->type == LINE_COMPL) {
			message("no match");
			return;
		}
	}

	minibuffer_confirm();
}

void
cmd_mini_previous_history_element(struct buffer *buffer)
{
	char *text;

	if (ministate.hist == NULL) {
		message("No history");
		return;
	}

	if (hist_prev(ministate.hist) == NULL) {
		message("No prev history item");
		return;
	}

	ministate.editing = 0;

	/* XXX the minibuffer line is never modified so this is fine */
	text = (char *)hist_cur(ministate.hist);
	buffer->current_line->parent->line = text;
	recompute_completions(0);
}

void
cmd_mini_next_history_element(struct buffer *buffer)
{
	char *text;

	if (ministate.hist == NULL) {
		message("No history");
		return;
	}

	if (hist_next(ministate.hist) == NULL) {
		message("No next history item");
		return;
	}

	ministate.editing = 0;

	/* XXX the minibuffer line is never modified so this is fine */
	text = (char *)hist_cur(ministate.hist);
	buffer->current_line->parent->line = text;
	recompute_completions(0);
}

void
cmd_previous_completion(struct buffer *buffer)
{
	if (in_minibuffer != MB_COMPREAD)
		return;

	buffer = &ministate.compl.buffer;
	if (buffer->current_line == NULL)
		return;

	buffer->current_line->parent->type = LINE_COMPL;
	if (!forward_line(buffer, -1))
		buffer->current_line->parent->type = LINE_COMPL;
	else
		buffer->current_line->parent->type = LINE_COMPL_CURRENT;
}

void
cmd_next_completion(struct buffer *buffer)
{
	if (in_minibuffer != MB_COMPREAD)
		return;

	buffer = &ministate.compl.buffer;
	if (buffer->current_line == NULL)
		return;

	if (buffer->current_line->parent->type == LINE_COMPL_CURRENT) {
		buffer->current_line->parent->type = LINE_COMPL;
		forward_line(buffer, +1);
	}

	if (buffer->current_line != NULL)
		buffer->current_line->parent->type = LINE_COMPL_CURRENT;
}

void
cmd_insert_current_candidate(struct buffer *buffer)
{
	if (in_minibuffer != MB_COMPREAD)
		return;

	minibuffer_insert_current_candidate();
}

void
cmd_suspend_telescope(struct buffer *buffer)
{
	message("Zzz...");
	ui_suspend();
	kill(getpid(), SIGSTOP);
	ui_resume();
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
cmd_toggle_styling(struct buffer *buffer)
{
	dont_apply_styling = !dont_apply_styling;

	config_apply_style();

	/* Force a reload here, rather than calling ui_schedule_redraw() so
	 * that any lines which are set as L_HIDDEN are redrawn appropriately.
	 */
	load_url_in_tab(current_tab, hist_cur(current_tab->hist), NULL,
	    LU_MODE_NOHIST);
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

	vl = TAILQ_FIRST(&buffer->vhead);
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

	vl = TAILQ_LAST(&buffer->vhead, vhead);
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

void
cmd_cache_info(struct buffer *buffer)
{
	size_t	npages, tot;
	char	fmt[FMT_SCALED_STRSIZE];

	mcache_info(&npages, &tot);

	if (fmt_scaled(tot, fmt) == 0)
		message("pages: %zu, total: %s", npages, fmt);
	else
		message("pages: %zu, total: %zu", npages, tot);
}

void
cmd_reply_last_input(struct buffer *buffer)
{
	GUARD_RECURSIVE_MINIBUFFER();

	if (current_tab->last_input_url == NULL) {
		message("there was no previous input request in this tab");
		return;
	}

	if (!strncmp(current_tab->last_input_url, "gopher", 6)) {
		load_url_in_tab(current_tab, current_tab->last_input_url,
		    NULL, LU_MODE_NOCACHE);
		return;
	}

	message("%s", current_tab->last_input_url);
	ui_require_input(current_tab, 0, ir_select_reply);
}

void
cmd_write_buffer(struct buffer *buffer)
{
	const char *f, *url;
	char path[PATH_MAX];

	GUARD_RECURSIVE_MINIBUFFER();

	if (safe_mode) {
		message("Can't write buffer in safe-mode.");
		return;
	}

	url = hist_cur(current_tab->hist);

	if ((f = strrchr(url, '/')) != NULL)
		f++;
	if (f == NULL || *f == '\0') {
		/* guess a decent file name based on the protocol used */
		if (!strncmp(url, "gemini://", 9))
			f = "index.gmi";
		else
			f = "index.txt";
	}

	strlcpy(path, download_path, sizeof(path));
	strlcat(path, f, sizeof(path));

	ui_read("Write file", write_buffer, current_tab, path);
}

void
cmd_home(struct buffer *buffer)
{
	char path[GEMINI_URL_LEN];
	char *tilde, *t;

	strlcpy(path, current_tab->iri.iri_path, sizeof(path));

	if ((tilde = strstr(path, "/~")) != NULL &&
	    tilde[2] != '\0' && tilde[2] != '/') {
		if ((t = strchr(tilde + 2, '/')) != NULL)
			*++t = '\0';
		load_url_in_tab(current_tab, path, NULL, LU_MODE_NOCACHE);
	} else
		cmd_root(buffer);
}

void
cmd_root(struct buffer *buffer)
{
	load_url_in_tab(current_tab, "/", NULL, LU_MODE_NOCACHE);
}

void
cmd_up(struct buffer *buffer)
{
	load_url_in_tab(current_tab, "..", NULL, LU_MODE_NOCACHE);
}

void
cmd_use_certificate(struct buffer *buffer)
{
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = uc_select,
		.complfn = compl_uc,
		.must_select = 1,
	};

	GUARD_RECURSIVE_MINIBUFFER();

	enter_minibuffer(&m, "Select certificate: ");
}

void
cmd_client_certificate_info(struct buffer *buffer)
{
	if (current_tab->client_cert)
		message("Using certificate %s", current_tab->client_cert);
	else
		message("Not using any client certificate.");
}

static void
unload_certificate_cb(int r, void *data)
{
	struct tab	*tab = data;

	message("Won't use %s for this site.", tab->client_cert);
	cert_delete_for(tab->client_cert, &tab->iri, r);
}

void
cmd_unload_certificate(struct buffer *buffer)
{
	struct tab	*tab = current_tab;

	GUARD_RECURSIVE_MINIBUFFER();

	if (tab->client_cert == NULL) {
		message("No client certificate in use!");
		return;
	}

	if (tab->client_cert_temp) {
		message("Won't use %s for this site.", tab->client_cert);
		cert_delete_for(tab->client_cert, &tab->iri, 0);
		return;
	}

	yornp("Unload only for the current session?", unload_certificate_cb,
	    current_tab);
}

void
cmd_search(struct buffer *buffer)
{
	struct minibuffer m = {
		.self_insert = sensible_self_insert,
		.done = search_select,
	};

	GUARD_RECURSIVE_MINIBUFFER();

	if (!strncmp(default_search_engine, "gopher://", 9)) {
		load_url_in_tab(current_tab, default_search_engine, NULL,
		    LU_MODE_NOCACHE);
		return;
	}

	enter_minibuffer(&m, "Search: ");
}

void
cmd_mini_edit_external(struct buffer *buffer)
{
	FILE		*fp;
	char		 buf[1024 + 1];
	size_t		 r, len = 0;

	GUARD_READ_ONLY();

	if (ministate.compl.must_select || ministate.donefn == NULL) {
		message("Can't use an external editor to complete");
		return;
	}

	if ((fp = exec_editor(ministate.buf, strlen(ministate.buf))) == NULL)
		return;

	while (len < sizeof(buf) - 1) {
		r = fread(buf + len, 1, sizeof(buf) - 1 - len, fp);
		len += r;
		if (r == 0)
			break;
	}
	buf[len] = '\0';
	while (len > 0 && buf[len-1] == '\n')
		buf[--len] = '\0';

	/*
	 * XXX: do not use minibuffer_confirm() since the text could
	 * have multiple lines and we are not prepared to render them
	 * in the history navigation.
	 */
	ministate.donefn(buf);
	exit_minibuffer();
}
