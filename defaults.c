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
