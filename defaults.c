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

#include "telescope.h"

#include <curses.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

char *new_tab_url = NULL;
int fill_column = 80;
int olivetti_mode = 1;
int enable_colors = 1;

struct lineprefix line_prefixes[] = {
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

struct modeline_face modeline_face = {
	.background = A_REVERSE,
};

struct minibuffer_face minibuffer_face = {
	.background = A_NORMAL,
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
}

int
config_setprfx(const char *name, const char *prfx, const char *cont)
{
	struct lineprefix *p;
	struct mapping *m;

	if (!has_prefix(name, "line."))
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
	} else {
		return 0;
	}

	return 1;
}

int
config_setvars(const char *var, char *val)
{
	if (!strcmp(var, "new-tab-url")) {
		if (new_tab_url != NULL)
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
	} else if (has_prefix(name, "tabline.")) {
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
	} else if (has_prefix(name, "line.")) {
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
	} else if (has_prefix(name, "tabline.")) {
		name += 8;

		if (!strcmp(name, "tab"))
			tab_face.t_attr = prfx;
		else if (!strcmp(name, "current"))
			tab_face.c_attr = prfx;
		else
			return 0;
	} else if (has_prefix(name, "line.")) {
		name += 5;

		if ((m = mapping_by_name(name)) == NULL)
			return 0;

		f = &line_faces[m->linetype];

		f->prfx_attr = prfx;
		f->attr = line;
		f->trail_attr = trail;
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
}
