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

int
config_setprfx(const char *name, int cont, const char *str)
{
	size_t i;
	struct lineprefix *p;
	struct mapping {
		const char	*label;
		int		 id;
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

	if (!has_prefix(name, "line."))
		return 0;
	name += 5;

	for (i = 0; i < sizeof(mappings)/sizeof(mappings[0]); ++i) {
		if (!strcmp(name, mappings[i].label)) {
			name += strlen(mappings[i].label);
			p = &line_prefixes[mappings[i].id];

			if (cont)
				p->prfx2 = str;
			else
				p->prfx1 = str;

			return 1;
		}
	}

	return 0;
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
