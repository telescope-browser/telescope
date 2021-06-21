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
	int	prfx_used, used;
	int	prfx_pair, pair;
	int	prfx_bg, bg;
	int	prfx_fg, fg;
} linefaces_descr[] = {
	[LINE_TEXT] =		{ 0, 0, PAIR_TEXT_PRFX,		PAIR_TEXT,	0, 0, 0, 0 },
	[LINE_LINK] =		{ 0, 0, PAIR_LINK_PRFX,		PAIR_LINK,	0, 0, 0, 0 },
	[LINE_TITLE_1] =	{ 0, 0, PAIR_TITLE_1_PRFX,	PAIR_TITLE_1,	0, 0, 0, 0 },
	[LINE_TITLE_2] =	{ 0, 0, PAIR_TITLE_2_PRFX,	PAIR_TITLE_1,	0, 0, 0, 0 },
	[LINE_TITLE_3] =	{ 0, 0, PAIR_TITLE_3_PRFX,	PAIR_TITLE_3,	0, 0, 0, 0 },
	[LINE_ITEM] =		{ 0, 0, PAIR_ITEM_PRFX,		PAIR_ITEM,	0, 0, 0, 0 },
	[LINE_QUOTE] =		{ 0, 0, PAIR_QUOTE_PRFX,	PAIR_QUOTE,	0, 0, 0, 0 },
	[LINE_PRE_START] =	{ 0, 0, PAIR_PRE_START_PRFX,	PAIR_TEXT,	0, 0, 0, 0 },
	[LINE_PRE_CONTENT] =	{ 0, 0, PAIR_PRE_PRFX,		PAIR_PRE,	0, 0, 0, 0 },
	[LINE_PRE_END] =	{ 0, 0, PAIR_PRE_END_PRFX,	PAIR_PRE_END,	0, 0, 0, 0 },
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

struct tab_face tab_face = {
	.background	= A_REVERSE,
	.tab		= A_REVERSE,
	.current_tab	= A_NORMAL,
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
	int		 facetype;
	int		 facetype_prfx;
} mappings[] = {
	{"text",	LINE_TEXT,		PAIR_TEXT,	PAIR_TEXT_PRFX},
	{"link",	LINE_LINK,		PAIR_LINK,	PAIR_LINK_PRFX},
	{"title1",	LINE_TITLE_1,		PAIR_TITLE_1,	PAIR_TITLE_1_PRFX},
	{"title2",	LINE_TITLE_2,		PAIR_TITLE_2,	PAIR_TITLE_2_PRFX},
	{"title3",	LINE_TITLE_3,		PAIR_TITLE_3,	PAIR_TITLE_3_PRFX},
	{"item",	LINE_ITEM,		PAIR_ITEM,	PAIR_ITEM_PRFX},
	{"quote",	LINE_QUOTE,		PAIR_QUOTE,	PAIR_QUOTE_PRFX},
	{"pre.start",	LINE_PRE_START,		PAIR_PRE_START,	PAIR_PRE_START_PRFX},
	{"pre",		LINE_PRE_CONTENT,	PAIR_PRE,	PAIR_PRE_PRFX},
	{"pre.end",	LINE_PRE_END,		PAIR_PRE_END,	PAIR_PRE_END_PRFX},
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
config_setprfx(const char *name, int cont, const char *str)
{
	struct lineprefix *p;
	struct mapping *m;

	if (!has_prefix(name, "line."))
		return 0;
	name += 5;

	if ((m = mapping_by_name(name)) == NULL)
		return 0;

	p = &line_prefixes[m->linetype];

	if (cont)
		p->prfx2 = str;
	else
		p->prfx1 = str;

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
config_setcolor(const char *name, int prfx, int bg, int color)
{
        struct mapping *m;
	struct lineface_descr *d;

	if (!has_prefix(name, "line."))
		return 0;
	name += 5;

	if ((m = mapping_by_name(name)) == NULL)
		return 0;

	d = &linefaces_descr[m->linetype];

	if (prfx) {
		d->prfx_used = 1;
		if (bg)
			d->prfx_bg = color;
		else
			d->prfx_fg = color;
	} else {
		d->used = 1;
		if (bg)
			d->bg = color;
		else
			d->fg = color;
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

		if (d->prfx_used) {
			init_pair(d->prfx_pair, d->prfx_fg, d->prfx_bg);
			f->prefix_prop = COLOR_PAIR(d->prfx_pair);
		}

		if (d->used) {
			init_pair(d->pair, d->fg, d->bg);
			f->text_prop = COLOR_PAIR(d->pair);
		}
	}
}
