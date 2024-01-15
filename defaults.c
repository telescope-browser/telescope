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

#include <curses.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defaults.h"
#include "telescope.h"
#include "ui.h"
#include "utils.h"

char	*download_path = NULL;
char	*new_tab_url = NULL;

int autosave = 20;
int dont_wrap_pre = 0;
int emojify_link = 1;
int enable_colors = 1;
int fill_column = 120;
int fringe_ignore_offset = 1;
int hide_pre_blocks = 0;
int hide_pre_closing_line = 0;
int hide_pre_context = 0;
int load_url_use_heuristic = 1;
int max_killed_tabs = 10;
int olivetti_mode = 1;
int set_title = 1;
int tab_bar_show = 1;

static struct line fringe_line = {
	.type = LINE_FRINGE,
};
struct vline fringe = {
	.parent = &fringe_line,
};

struct lineprefix line_prefixes[] = {
	[LINE_TEXT] =		{ "",		"" },
	[LINE_LINK] =		{ "→ ",		"  " },
	[LINE_TITLE_1] =	{ "# ",		"  " },
	[LINE_TITLE_2] =	{ "## ",	"   " },
	[LINE_TITLE_3] =	{ "### ",	"    " },
	[LINE_ITEM] =		{ " • ",	"   " },
	[LINE_QUOTE] =		{ " ┃ ",	" ┃ " },
	[LINE_PRE_START] =	{ "─── ",	"    " },
	[LINE_PRE_CONTENT] =	{ "",		"" },
	[LINE_PRE_END] =	{ "─── ",	"" },

	[LINE_PATCH] =		{"", ""},
	[LINE_PATCH_HDR] =	{"", ""},
	[LINE_PATCH_HUNK_HDR] =	{"", ""},
	[LINE_PATCH_ADD] =	{"", ""},
	[LINE_PATCH_DEL] =	{"", ""},

	[LINE_COMPL] =		{"", ""},
	[LINE_COMPL_CURRENT] =	{"", ""},

	[LINE_HELP] =		{"", ""},

	[LINE_DOWNLOAD] =	{" Fetching ", "          "},
	[LINE_DOWNLOAD_DONE] =	{" Done     ", "          "},
	[LINE_DOWNLOAD_INFO] =	{" ", " "},

	[LINE_FRINGE] =		{"~", ""},
};

struct line_face line_faces[] = {
	[LINE_TEXT] =		{
		.prfx_pair = PT_PRFX,
		.pair = PT,
		.trail_pair = PT_TRAIL,
	},
	[LINE_LINK] =		{
		.prfx_pair = PL_PRFX,
		.pair = PL,
		.trail_pair = PL_TRAIL,
		.attr = A_UNDERLINE,
	},
	[LINE_TITLE_1] =	{
		.prfx_pair = PT1_PRFX,
		.pair = PT1,
		.trail_pair = PT1_TRAIL,
		.attr = A_BOLD,
	},
	[LINE_TITLE_2] =	{
		.prfx_pair = PT2_PRFX,
		.pair = PT2,
		.trail_pair = PT2_TRAIL,
		.attr = A_BOLD,
	},
	[LINE_TITLE_3] =	{
		.prfx_pair = PT3_PRFX,
		.pair = PT3,
		.trail_pair = PT3_TRAIL,
		.attr = A_BOLD,
	},
	[LINE_ITEM] =		{
		.prfx_pair = PI_PRFX,
		.pair = PI,
		.trail_pair = PI_TRAIL,
	},
	[LINE_QUOTE] =		{
		.prfx_pair = PQ_PRFX,
		.pair = PQ,
		.trail_pair = PQ_TRAIL,
		.attr = A_DIM,
	},
	[LINE_PRE_START] =	{
		.prfx_pair = PPSTART_PRFX,
		.pair = PPSTART,
		.trail_pair = PPSTART_TRAIL,
	},
	[LINE_PRE_CONTENT] =	{
		.prfx_pair = PP_PRFX,
		.pair = PP,
		.trail_pair = PP_TRAIL,
	},
	[LINE_PRE_END] =	{
		.prfx_pair = PPEND_PRFX,
		.pair = PPEND,
		.trail_pair = PPEND_TRAIL,
	},

	/* text/x-patch */
	[LINE_PATCH] = {
		.prfx_pair = PPATCH_PRFX,
		.pair = PPATCH,
		.trail_pair = PPATCH_TRAIL,
	},
	[LINE_PATCH_HDR] = {
		.prfx_pair = PPATCH_HDR_PRFX,
		.pair = PPATCH_HDR,
		.trail_pair = PPATCH_HDR_TRAIL,
	},
	[LINE_PATCH_HUNK_HDR] = {
		.prfx_pair = PPATCH_HUNK_HDR_PRFX,
		.pair = PPATCH_HUNK_HDR,
		.trail_pair = PPATCH_HUNK_HDR_TRAIL,
	},
	[LINE_PATCH_ADD] = {
		.prfx_pair = PPATCH_ADD_PRFX,
		.pair = PPATCH_ADD,
		.trail_pair = PPATCH_ADD_TRAIL,
	},
	[LINE_PATCH_DEL] = {
		.prfx_pair = PPATCH_DEL_PRFX,
		.pair = PPATCH_DEL,
		.trail_pair = PPATCH_DEL_TRAIL,
	},

	/* minibuffer */
	[LINE_COMPL] = {
		.prfx_pair = PCOMPL_PRFX,
		.pair = PCOMPL,
		.trail_pair = PCOMPL_TRAIL,
	},
	[LINE_COMPL_CURRENT] = {
		.prfx_pair = PCOMPL_CURR_PRFX,
		.pair = PCOMPL_CURR,
		.trail_pair = PCOMPL_CURR_TRAIL,
		.attr = A_REVERSE,
		.trail_attr = A_REVERSE,
	},

	/* help */
	[LINE_HELP] = {
		.prfx_pair = PHELP_PRFX,
		.pair = PHELP,
		.trail_pair = PHELP_TRAIL,
	},

	/* download */
	[LINE_DOWNLOAD] = {
		.prfx_pair = PDOWNLOAD_PRFX,
		.pair = PDOWNLOAD,
		.trail_pair = PDOWNLOAD_TRAIL
	},
	[LINE_DOWNLOAD_DONE] = {
		.prfx_pair = PDOWNLOAD_DONE_PRFX,
		.pair = PDOWNLOAD_DONE,
		.trail_pair = PDOWNLOAD_DONE_TRAIL
	},
	[LINE_DOWNLOAD_INFO] = {
		.prfx_pair = PDOWNLOAD_INFO_PRFX,
		.pair = PDOWNLOAD_INFO,
		.trail_pair = PDOWNLOAD_INFO_TRAIL
	},

	/* misc ui */
	[LINE_FRINGE] = {
		.prfx_pair = PFRINGE_PRFX,
		.pair = PFRINGE,
		.trail_pair = PFRINGE_TRAIL,
	}
};

struct tab_face tab_face = {
	.bg_attr = A_REVERSE, .bg_bg = -1, .bg_fg = -1,
	.t_attr  = A_REVERSE, .t_bg  = -1, .t_fg  = -1,
	.c_attr  = A_NORMAL,  .c_bg  = -1, .c_fg  = -1,

	/*
	 * set these so that even when enable-color=0 the bar has some
	 * sane defaults.
	 */
	.background =	A_REVERSE,
	.tab =		A_REVERSE,
	.current =	A_NORMAL,
};

struct body_face body_face = {
	.lbg = -1, .lfg = -1,
	.bg  = -1, .fg  = -1,
	.rbg = -1, .rfg = -1,
};

struct download_face download_face = {
	.bg = -1,
	.fg = -1,
	.attr = A_NORMAL,
};

struct modeline_face modeline_face = {
	.bg  = -1,
	.fg  = -1,
	.attr = A_REVERSE,
};

struct minibuffer_face minibuffer_face = {
	.bg = -1,
	.fg = -1,
	.attr = A_NORMAL,
};

struct mapping {
	const char	*label;
	int		 linetype;
} mappings[] = {
	{"text",	LINE_TEXT},
	{"link",	LINE_LINK},
	{"title1",	LINE_TITLE_1},
	{"title2",	LINE_TITLE_2},
	{"title3",	LINE_TITLE_3},
	{"item",	LINE_ITEM},
	{"quote",	LINE_QUOTE},
	{"pre.start",	LINE_PRE_START},
	{"pre",		LINE_PRE_CONTENT},
	{"pre.end",	LINE_PRE_END},

	/* text/x-patch */
	{"patch",	LINE_PATCH},
	{"patch.hdr",	LINE_PATCH_HDR},
	{"patch.hunk",	LINE_PATCH_HUNK_HDR},
	{"patch.add",	LINE_PATCH_ADD},
	{"patch.del",	LINE_PATCH_DEL},

	/* minibuffer */
	{"compl",	LINE_COMPL},
	{"compl.current", LINE_COMPL_CURRENT},

	/* help */
	{"help",	LINE_HELP},

	/* download */
	{"download.ongoing",	LINE_DOWNLOAD},
	{"download.done",	LINE_DOWNLOAD_DONE},
	{"download.info",	LINE_DOWNLOAD_INFO},

	/* misc ui */
	{"fringe",		LINE_FRINGE},
};

static struct mapping *
mapping_by_name(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(mappings)/sizeof(mappings[0]); ++i) {
		if (!strcmp(name, mappings[i].label))
			return &mappings[i];
	}

	return NULL;
}

static inline void
global_set_key(const char *key, void (*fn)(struct buffer*))
{
	if (!kmap_define_key(&global_map, key, fn))
		_exit(1);
}

static inline void
minibuffer_set_key(const char *key, void (*fn)(struct buffer*))
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

	global_set_key("M-<",		cmd_beginning_of_buffer);
	global_set_key("M->",		cmd_end_of_buffer);

	global_set_key("C-x C-c",	cmd_kill_telescope);
	global_set_key("C-x C-w",	cmd_write_buffer);

	global_set_key("C-g",		cmd_clear_minibuf);

	global_set_key("M-x",		cmd_execute_extended_command);

	global_set_key("C-c {",		cmd_dec_fill_column);
	global_set_key("C-c }",		cmd_inc_fill_column);

	global_set_key("C-c p",		cmd_previous_heading);
	global_set_key("C-c n",		cmd_next_heading);

	global_set_key(">",		cmd_load_url);
	global_set_key("<",		cmd_load_current_url);
	global_set_key("C-x C-f",	cmd_load_url);
	global_set_key("C-x M-f",	cmd_load_current_url);

	global_set_key("C-x o",		cmd_other_window);

	global_set_key("C-x t 0",	cmd_tab_close);
	global_set_key("C-x t 1",	cmd_tab_close_other);
	global_set_key("C-x t 2",	cmd_tab_new);
	global_set_key("C-x t o",	cmd_tab_next);
	global_set_key("C-x t O",	cmd_tab_previous);
	global_set_key("C-x t m",	cmd_tab_move);
	global_set_key("C-x t M",	cmd_tab_move_to);

	global_set_key("B",		cmd_previous_page);
	global_set_key("C-M-b",		cmd_previous_page);
	global_set_key("F",		cmd_next_page);
	global_set_key("C-M-f",		cmd_next_page);

	global_set_key("<f7> a",	cmd_bookmark_page);
	global_set_key("<f7> <f7>",	cmd_list_bookmarks);

	global_set_key("C-z",		cmd_suspend_telescope);

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

	global_set_key("g g",		cmd_beginning_of_buffer);
	global_set_key("G",		cmd_end_of_buffer);

	global_set_key("g D",		cmd_tab_close);
	global_set_key("g N",		cmd_tab_new);
	global_set_key("g t",		cmd_tab_next);
	global_set_key("g T",		cmd_tab_previous);
	global_set_key("g M-t",		cmd_tab_move);
	global_set_key("g M-T",		cmd_tab_move_to);

	global_set_key("H",		cmd_previous_page);
	global_set_key("L",		cmd_next_page);

	global_set_key("u",		cmd_tab_undo_close);

	/* tmp */
	global_set_key("q",		cmd_kill_telescope);

	global_set_key("esc",		cmd_clear_minibuf);

	global_set_key(":",		cmd_execute_extended_command);

	/* cua */
	global_set_key("<up>",		cmd_previous_line);
	global_set_key("<down>",	cmd_next_line);
	global_set_key("<right>",	cmd_forward_char);
	global_set_key("<left>",	cmd_backward_char);
	global_set_key("<home>",	cmd_move_beginning_of_line);
	global_set_key("<end>",		cmd_move_end_of_line);
	global_set_key("<prior>",	cmd_scroll_up);
	global_set_key("<next>",	cmd_scroll_down);

	global_set_key("C-w",		cmd_tab_close);
	global_set_key("C-t",		cmd_tab_new);
	global_set_key("M-<prior>",	cmd_tab_previous);
	global_set_key("M-<next>",	cmd_tab_next);

	global_set_key("del",		cmd_previous_page);
	global_set_key("M-<left>",	cmd_previous_page);
	global_set_key("M-<right>",	cmd_next_page);

	global_set_key("<f5>",		cmd_reload_page);
	global_set_key("r",		cmd_reload_page);

	/* "ncurses standard" */
	global_set_key("C-l",		cmd_redraw);

	/* global */
	global_set_key("<f1>",		cmd_toggle_help);
	global_set_key("<f2>",		cmd_toggle_downloads);
	global_set_key("C-m",		cmd_push_button);
	global_set_key("M-enter",	cmd_push_button_new_tab);
	global_set_key("M-tab",		cmd_previous_button);
	global_set_key("backtab",	cmd_previous_button);
	global_set_key("tab",		cmd_next_button);
	global_set_key("M-t",		cmd_tab_select);
	global_set_key("[",		cmd_tab_previous);
	global_set_key("]",		cmd_tab_next);
	global_set_key("M-[",		cmd_tab_move_to);
	global_set_key("M-]",		cmd_tab_move);
	global_set_key("M-l",		cmd_link_select);
	global_set_key("M-/",		cmd_swiper);
	global_set_key("t",		cmd_toc);
	global_set_key("M-r",		cmd_reply_last_input);

	/* === minibuffer map === */
	minibuffer_set_key("ret",	cmd_mini_complete_and_exit);
	minibuffer_set_key("C-g",	cmd_mini_abort);
	minibuffer_set_key("esc",	cmd_mini_abort);
	minibuffer_set_key("C-d",	cmd_mini_delete_char);
	minibuffer_set_key("del",	cmd_mini_delete_backward_char);
	minibuffer_set_key("backspace",	cmd_mini_delete_backward_char);
	minibuffer_set_key("C-h",	cmd_mini_delete_backward_char);

	minibuffer_set_key("C-b",	cmd_backward_char);
	minibuffer_set_key("C-f",	cmd_forward_char);
	minibuffer_set_key("<left>",	cmd_backward_char);
	minibuffer_set_key("<right>",	cmd_forward_char);
	minibuffer_set_key("C-e",	cmd_move_end_of_line);
	minibuffer_set_key("C-a",	cmd_move_beginning_of_line);
	minibuffer_set_key("<end>",	cmd_move_end_of_line);
	minibuffer_set_key("<home>",	cmd_move_beginning_of_line);
	minibuffer_set_key("C-k",	cmd_mini_kill_line);
	minibuffer_set_key("C-u",	cmd_mini_kill_whole_line);

	minibuffer_set_key("M-p",	cmd_mini_previous_history_element);
	minibuffer_set_key("M-n",	cmd_mini_next_history_element);

	minibuffer_set_key("C-p",	cmd_previous_completion);
	minibuffer_set_key("C-n",	cmd_next_completion);
	minibuffer_set_key("<up>",	cmd_previous_completion);
	minibuffer_set_key("<down>",	cmd_next_completion);

	minibuffer_set_key("C-v",	cmd_mini_scroll_down);
	minibuffer_set_key("M-v",	cmd_mini_scroll_up);
	minibuffer_set_key("<next>",	cmd_mini_scroll_down);
	minibuffer_set_key("<prior>",	cmd_mini_scroll_up);

	minibuffer_set_key("M-<",	cmd_mini_goto_beginning);
	minibuffer_set_key("M->",	cmd_mini_goto_end);

	minibuffer_set_key("tab",	cmd_insert_current_candidate);
}

void
config_init(void)
{
	struct line_face *f;
	size_t i, len;

	len = sizeof(line_faces)/sizeof(line_faces[0]);
	for (i = 0; i < len; ++i) {
		f = &line_faces[i];

		f->prfx_bg = f->bg = f->trail_bg = -1;
		f->prfx_fg = f->fg = f->trail_fg = -1;
	}

	line_faces[LINE_LINK].fg = COLOR_BLUE;
	line_faces[LINE_PATCH_ADD].fg = COLOR_GREEN;
	line_faces[LINE_PATCH_DEL].fg = COLOR_RED;

	load_default_keys();
}

int
config_setprfx(const char *name, const char *prfx, const char *cont)
{
	struct lineprefix *p;
	struct mapping *m;

	if (strncmp(name, "line.", 5) != 0)
		return 0;
	name += 5;

	if ((m = mapping_by_name(name)) == NULL)
		return 0;

	p = &line_prefixes[m->linetype];
	p->prfx1 = prfx;
	p->prfx2 = cont;

	return 1;
}

int
config_setvari(const char *var, int val)
{
	if (!strcmp(var, "fill-column")) {
		if ((fill_column = val) <= 0)
			fill_column = INT_MAX;
	} else if (!strcmp(var, "olivetti-mode")) {
		olivetti_mode = !!val;
	} else if (!strcmp(var, "enable-colors")) {
		enable_colors = !!val;
	} else if (!strcmp(var, "hide-pre-context")) {
		hide_pre_context = !!val;
	} else if (!strcmp(var, "hide-pre-blocks")) {
		hide_pre_blocks = !!val;
	} else if (!strcmp(var, "hide-pre-closing-line")) {
		hide_pre_closing_line = !!val;
	} else if (!strcmp(var, "dont-wrap-pre")) {
		dont_wrap_pre = !!val;
	} else if (!strcmp(var, "emojify-link")) {
		emojify_link = !!val;
	} else if (!strcmp(var, "update-title") || !strcmp(var, "set-title")) {
		set_title = !!val;
	} else if (!strcmp(var, "autosave")) {
		autosave = val;
	} else if (!strcmp(var, "tab-bar-show")) {
		if (val < 0)
			tab_bar_show = -1;
		else if (val == 0)
			tab_bar_show = 0;
		else
			tab_bar_show = 1;
	} else if (!strcmp(var, "max-killed-tabs")) {
		if (val >= 0)
			max_killed_tabs = MIN(val, 128);
	} else if (!strcmp(var, "fringe-ignore-offset")) {
		fringe_ignore_offset = !!val;
	} else if (!strcmp(var, "load-url-use-heuristic")) {
		load_url_use_heuristic = !!val;
	} else {
		return 0;
	}

	return 1;
}

int
config_setvars(const char *var, char *val)
{
	if (!strcmp(var, "download-path")) {
		const char *prfx = "", *v = val, *sufx = "";

		if (!strncmp(val, "~/", 2) &&
		    v++ &&
		    (prfx = getenv("HOME")) == NULL)
			return 0;

		if (!has_suffix(val, "/"))
			sufx = "/";

		free(download_path);
		if (asprintf(&download_path, "%s%s%s", prfx, v, sufx) == -1) {
			download_path = NULL;
			return 0;
		}

		free(val);
		return 1;
	} else if (!strcmp(var, "new-tab-url")) {
		free(new_tab_url);
		new_tab_url = val;
	} else
		return 0;
	return 1;
}

int
config_setcolor(int bg, const char *name, int prfx, int line, int trail)
{
	struct mapping *m;
	struct line_face *f;

	if (!strcmp(name, "tabline")) {
		if (bg)
			tab_face.bg_bg = prfx;
		else
			tab_face.bg_fg = prfx;
	} else if (!strncmp(name, "tabline.", 8)) {
		name += 8;

		if (!strcmp(name, "tab")) {
			if (bg)
				tab_face.t_bg = prfx;
			else
				tab_face.t_fg = prfx;
		} else if (!strcmp(name, "current")) {
			if (bg)
				tab_face.c_bg = prfx;
			else
				tab_face.c_fg = prfx;
		} else
			return 0;
	} else if (!strncmp(name, "line.", 5)) {
		name += 5;

		if ((m = mapping_by_name(name)) == NULL)
			return 0;

		f = &line_faces[m->linetype];

		if (bg) {
			f->prfx_bg = prfx;
			f->bg = line;
			f->trail_bg = trail;
		} else {
			f->prfx_fg = prfx;
			f->fg = line;
			f->trail_fg = trail;
		}
	} else if (!strcmp(name, "line")) {
		if (bg) {
			body_face.lbg = prfx;
			body_face.bg  = line;
			body_face.rbg = trail;
		} else {
			body_face.lfg = prfx;
			body_face.fg = line;
			body_face.rfg = trail;
		}
	} else if (!strcmp(name, "download")) {
		if (bg)
			download_face.bg = prfx;
		else
			download_face.fg = prfx;
	} else if (!strcmp(name, "minibuffer")) {
		if (bg)
			minibuffer_face.bg = prfx;
		else
			minibuffer_face.fg = prfx;
	} else if (!strcmp(name, "modeline")) {
		if (bg)
			modeline_face.bg = prfx;
		else
			modeline_face.fg = prfx;
	} else {
		return 0;
	}

	return 1;
}

int
config_setattr(const char *name, int prfx, int line, int trail)
{
	struct mapping *m;
	struct line_face *f;

	if (!strcmp(name, "tabline")) {
		tab_face.bg_attr = prfx;
	} else if (!strncmp(name, "tabline.", 8)) {
		name += 8;

		if (!strcmp(name, "tab"))
			tab_face.t_attr = prfx;
		else if (!strcmp(name, "current"))
			tab_face.c_attr = prfx;
		else
			return 0;
	} else if (!strncmp(name, "line.", 5)) {
		name += 5;

		if ((m = mapping_by_name(name)) == NULL)
			return 0;

		f = &line_faces[m->linetype];

		f->prfx_attr = prfx;
		f->attr = line;
		f->trail_attr = trail;
	} else if (!strcmp(name, "download")) {
		download_face.attr = prfx;
	} else if (!strcmp(name, "minibuffer")) {
		minibuffer_face.attr = prfx;
	} else if (!strcmp(name, "modeline")) {
		modeline_face.attr = prfx;
	} else {
		return 0;
	}

	return 1;
}

static inline void
tl_init_pair(int colors, int pair, int f, int b)
{
	if (f >= colors || !enable_colors)
		f = -1;
	if (b >= colors || !enable_colors)
		b = -1;
	init_pair(pair, f, b);
}

void
config_apply_style(void)
{
	size_t i, colors, len;
	struct line_face *f;

	colors = COLORS;

	len = sizeof(line_faces)/sizeof(line_faces[0]);
	for (i = 0; i < len; ++i) {
		f = &line_faces[i];

		tl_init_pair(colors, f->prfx_pair, f->prfx_fg, f->prfx_bg);
		f->prefix = COLOR_PAIR(f->prfx_pair) | f->prfx_attr;

		tl_init_pair(colors, f->pair, f->fg, f->bg);
		f->text = COLOR_PAIR(f->pair) | f->attr;

		tl_init_pair(colors, f->trail_pair, f->trail_fg, f->trail_bg);
		f->trail = COLOR_PAIR(f->trail_pair) | f->trail_attr;
	}

	/* tab line */
	tl_init_pair(colors, PTL_BG, tab_face.bg_fg, tab_face.bg_bg);
	tab_face.background = COLOR_PAIR(PTL_BG) | tab_face.bg_attr;

	tl_init_pair(colors, PTL_TAB, tab_face.t_fg, tab_face.t_bg);
	tab_face.tab = COLOR_PAIR(PTL_TAB) | tab_face.t_attr;

	tl_init_pair(colors, PTL_CURR, tab_face.c_fg, tab_face.c_bg);
	tab_face.current = COLOR_PAIR(PTL_CURR) | tab_face.c_attr;

	/* body */
	tl_init_pair(colors, PBODY, body_face.fg, body_face.bg);
	body_face.body = COLOR_PAIR(PBODY);

	tl_init_pair(colors, PBLEFT, body_face.lfg, body_face.lbg);
	body_face.left = COLOR_PAIR(PBLEFT);

	tl_init_pair(colors, PBRIGHT, body_face.rfg, body_face.rbg);
	body_face.right = COLOR_PAIR(PBRIGHT);

	/* download */
	tl_init_pair(colors, PDOWNLOAD_WIN, download_face.fg, download_face.bg);
	download_face.background = COLOR_PAIR(PDOWNLOAD_WIN) | download_face.attr;

	/* modeline */
	tl_init_pair(colors, PMODELINE, modeline_face.fg, modeline_face.bg);
	modeline_face.background = COLOR_PAIR(PMODELINE) | modeline_face.attr;

	/* minibuffer */
	tl_init_pair(colors, PMINIBUF, minibuffer_face.fg, minibuffer_face.bg);
	minibuffer_face.background = COLOR_PAIR(PMINIBUF) | minibuffer_face.attr;

}
