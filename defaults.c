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
int fill_column = INT_MAX;
int olivetti_mode = 0;
int enable_colors = 1;

static struct lineface_descr {
	int	used;
	int	pp, p, tp;
	int	prfx_bg, bg, trail_bg;
	int	prfx_fg, fg, trail_fg;
} linefaces_descr[] = {
	[LINE_TEXT] =		{.pp=PT_PRFX,		.p=PT,		.tp=PT_TRAIL},
	[LINE_LINK] =		{.pp=PL_PRFX,		.p=PL,		.tp=PL_TRAIL},
	[LINE_TITLE_1] =	{.pp=PT1_PRFX,		.p=PT1,		.tp=PT1_TRAIL},
	[LINE_TITLE_2] =	{.pp=PT2_PRFX,		.p=PT1,		.tp=PT1_TRAIL},
	[LINE_TITLE_3] =	{.pp=PT3_PRFX,		.p=PT3,		.tp=PT3_TRAIL},
	[LINE_ITEM] =		{.pp=PI_PRFX,		.p=PI,		.tp=PI_TRAIL},
	[LINE_QUOTE] =		{.pp=PQ_PRFX,		.p=PQ,		.tp=PQ_TRAIL},
	[LINE_PRE_START] =	{.pp=PPSTART_PRFX,	.p=PT,		.tp=PT_TRAIL},
	[LINE_PRE_CONTENT] =	{.pp=PP_PRFX,		.p=PP,		.tp=PP_TRAIL},
	[LINE_PRE_END] =	{.pp=PPEND_PRFX,	.p=PPEND,	.tp=PPEND_TRAIL},
};

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
	[LINE_TEXT] =		{ 0,		0,		0 },
	[LINE_LINK] =		{ 0,		A_UNDERLINE,	0 },
	[LINE_TITLE_1] =	{ A_BOLD,	A_BOLD,		0 },
	[LINE_TITLE_2] =	{ A_BOLD,	A_BOLD,		0 },
	[LINE_TITLE_3] =	{ A_BOLD,	A_BOLD,		0 },
	[LINE_ITEM] =		{ 0,		0,		0 },
	[LINE_QUOTE] =		{ 0,		A_DIM,		0 },
	[LINE_PRE_START] =	{ 0,		0,		0 },
	[LINE_PRE_CONTENT] =	{ 0,		0,		0 },
	[LINE_PRE_END] =	{ 0,		0,		0 },
};

struct tab_face tab_face = {
	.background	= A_REVERSE,
	.tab		= A_REVERSE,
	.current_tab	= A_NORMAL,
};

struct body_face body_face = {
	.bg = 0,
	.fg = 0,
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
	struct lineface_descr *d;

	if (has_prefix(name, "line.")) {
		name += 5;

		if ((m = mapping_by_name(name)) == NULL)
			return 0;

		d = &linefaces_descr[m->linetype];

		d->used = 1;
		if (bg) {
			d->prfx_bg = prfx;
			d->bg = line;
			d->trail_bg = trail;
		} else {
			d->prfx_fg = prfx;
			d->fg = line;
			d->trail_fg = trail;
		}
	} else if (!strcmp(name, "line")) {
		if (bg)
			body_face.bg = prfx;
		else
			body_face.fg = prfx;
	} else {
		return 0;
	}

	return 1;
}

void
config_apply_colors(void)
{
        size_t i, len;
	struct lineface_descr *d;
	struct line_face *f;

	len = sizeof(linefaces_descr)/sizeof(linefaces_descr[0]);
	for (i = 0; i < len; ++i) {
		d = &linefaces_descr[i];
		f = &line_faces[i];

		if (d->used) {
			init_pair(d->pp, d->prfx_fg, d->prfx_bg);
			f->prefix_prop = COLOR_PAIR(d->pp);

			init_pair(d->p, d->fg, d->bg);
			f->text_prop = COLOR_PAIR(d->p);

			init_pair(d->tp, d->trail_fg, d->trail_bg);
			f->trail_prop = COLOR_PAIR(d->tp);
		}
	}

	init_pair(PBODY, body_face.fg, body_face.bg);
	body_face.body = COLOR_PAIR(PBODY);
}
