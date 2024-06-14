/*
 * Copyright (c) 2021, 2022 Omar Polo <op@omarpolo.com>
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
 * A streaming gemtext parser.
 *
 * TODO:
 *  - handle NULs
 *  - UTF8
 */

#include "compat.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "defaults.h"
#include "parser.h"
#include "telescope.h"
#include "utf8.h"

static int	gemtext_parse_line(struct buffer *, const char *, size_t);
static int	gemtext_free(struct buffer *);
static int	gemtext_serialize(struct buffer *, FILE *);

static int	parse_link(struct buffer *, const char*, size_t);
static int	parse_title(struct buffer *, const char*, size_t);
static void	search_title(struct buffer *, enum line_type);

struct parser gemtext_parser = {
	.name = "text/gemini",
	.parseline = &gemtext_parse_line,
	.free = &gemtext_free,
	.serialize = &gemtext_serialize,
};

static inline int
emit_line(struct buffer *b, enum line_type type, char *line, char *alt)
{
	struct line *l;

	if ((l = calloc(1, sizeof(*l))) == NULL)
		return 0;

	l->type = type;
	l->line = line;
	l->alt = alt;

	switch (l->type) {
	case LINE_PRE_START:
	case LINE_PRE_END:
		if (hide_pre_context)
			l->flags = L_HIDDEN;
		if (l->type == LINE_PRE_END &&
		    hide_pre_closing_line)
			l->flags = L_HIDDEN;
		break;
	case LINE_PRE_CONTENT:
		if (hide_pre_blocks)
			l->flags = L_HIDDEN;
		break;
	case LINE_LINK:
		if (emojify_link &&
		    !emojied_line(line, (const char **)&l->data))
			l->data = NULL;
		break;
	default:
		break;
	}

	if (dont_apply_styling)
		l->flags &= ~L_HIDDEN;

	TAILQ_INSERT_TAIL(&b->head, l, lines);

	return 1;
}

static int
parse_link(struct buffer *b, const char *line, size_t len)
{
	char *label, *url;
	const char *start;

	if (len <= 2)
		return emit_line(b, LINE_TEXT, NULL, NULL);

	line += 2, len -= 2;
	while (len > 0 && isspace((unsigned char)line[0]))
		line++, len--;

	if (len == 0)
		return emit_line(b, LINE_TEXT, NULL, NULL);

	start = line;
	while (len > 0 && !isspace((unsigned char)line[0]))
		line++, len--;

	if ((url = strndup(start, line - start)) == NULL)
		return 0;

	while (len > 0 && isspace(line[0]))
		line++, len--;

	if (len == 0) {
		if ((label = strdup(url)) == NULL)
			return 0;
	} else {
		if ((label = strndup(line, len)) == NULL)
			return 0;
	}

	return emit_line(b, LINE_LINK, label, url);
}

static int
parse_title(struct buffer *b, const char *line, size_t len)
{
	enum line_type t = LINE_TITLE_1;
	char *l;

	line++, len--;
	while (len > 0 && *line == '#') {
		line++, len--;
		t++;
		if (t == LINE_TITLE_3)
			break;
	}

	while (len > 0 && isspace((unsigned char)*line))
		line++, len--;

	if (len == 0)
		return emit_line(b, t, NULL, NULL);

	if (t == LINE_TITLE_1 && *b->title == '\0')
		strncpy(b->title, line, MIN(sizeof(b->title)-1, len));

	if ((l = strndup(line, len)) == NULL)
		return 0;
	return emit_line(b, t, l, NULL);
}

static int
gemtext_parse_line(struct buffer *b, const char *line, size_t len)
{
	char *l;

	if (b->parser_flags & PARSER_IN_PRE) {
		if (len >= 3 && !strncmp(line, "```", 3)) {
			b->parser_flags ^= PARSER_IN_PRE;
			return emit_line(b, LINE_PRE_END, NULL, NULL);
		}

		if (len == 0)
			return emit_line(b, LINE_PRE_CONTENT, NULL, NULL);
		if ((l = strndup(line, len)) == NULL)
			return 0;
		return emit_line(b, LINE_PRE_CONTENT, l, NULL);
	}

	if (len == 0)
		return emit_line(b, LINE_TEXT, NULL, NULL);

	switch (*line) {
	case '*':
		if (len < 1 || line[1] != ' ')
			break;

		line += 2, len -= 2;
		while (len > 0 && isspace((unsigned char)*line))
			line++, len--;
		if (len == 0)
			return emit_line(b, LINE_ITEM, NULL, NULL);
		if ((l = strndup(line, len)) == NULL)
			return 0;
		return emit_line(b, LINE_ITEM, l, NULL);

	case '>':
		line++, len--;
		while (len > 0 && isspace((unsigned char)*line))
			line++, len--;
		if (len == 0)
			return emit_line(b, LINE_QUOTE, NULL, NULL);
		if ((l = strndup(line, len)) == NULL)
			return 0;
		return emit_line(b, LINE_QUOTE, l, NULL);

	case '=':
		if (len > 1 && line[1] == '>')
			return parse_link(b, line, len);
		break;

	case '#':
		return parse_title(b, line, len);

	case '`':
		if (len < 3 || strncmp(line, "```", 3) != 0)
			break;

		b->parser_flags |= PARSER_IN_PRE;
		line += 3, len -= 3;
		while (len > 0 && isspace((unsigned char)*line))
			line++, len--;
		if (len == 0)
			return emit_line(b, LINE_PRE_START,
			    NULL, NULL);
		if ((l = strndup(line, len)) == NULL)
			return 0;
		return emit_line(b, LINE_PRE_START, l, NULL);
	}

	if ((l = strndup(line, len)) == NULL)
		return 0;
	return emit_line(b, LINE_TEXT, l, NULL);
}

static int
gemtext_free(struct buffer *b)
{
	/* flush the buffer */
	if (b->len != 0) {
		if (!gemtext_parse_line(b, b->buf, b->len))
			return 0;
		if ((b->parser_flags & PARSER_IN_PRE) &&
		    !emit_line(b, LINE_PRE_END, NULL, NULL))
			return 0;
	}

	/*
	 * use the first level 2 or 3 header as page title if none
	 * found yet.
	 */
	if (*b->title == '\0')
		search_title(b, LINE_TITLE_2);
	if (*b->title == '\0')
		search_title(b, LINE_TITLE_3);

	return 1;
}

static void
search_title(struct buffer *b, enum line_type level)
{
	struct line *l;

	TAILQ_FOREACH(l, &b->head, lines) {
		if (l->type == level) {
			if (l->line == NULL)
				continue;
			strlcpy(b->title, l->line, sizeof(b->title));
			break;
		}
	}
}

static const char *gemtext_prefixes[] = {
	[LINE_TEXT] = "",
	[LINE_TITLE_1] = "# ",
	[LINE_TITLE_2] = "## ",
	[LINE_TITLE_3] = "### ",
	[LINE_ITEM] = "* ",
	[LINE_QUOTE] = "> ",
	[LINE_PRE_START] = "``` ",
	[LINE_PRE_CONTENT] = "",
	[LINE_PRE_END] = "```",
};

static int
gemtext_serialize(struct buffer *b, FILE *fp)
{
	struct line	*line;
	const char	*text;
	const char	*alt;
	int		 r;

	TAILQ_FOREACH(line, &b->head, lines) {
		if ((text = line->line) == NULL)
			text = "";

		if ((alt = line->alt) == NULL)
			alt = "";

		switch (line->type) {
		case LINE_TEXT:
		case LINE_TITLE_1:
		case LINE_TITLE_2:
		case LINE_TITLE_3:
		case LINE_ITEM:
		case LINE_QUOTE:
		case LINE_PRE_START:
		case LINE_PRE_CONTENT:
		case LINE_PRE_END:
			r = fprintf(fp, "%s%s\n", gemtext_prefixes[line->type],
			    text);
			break;

		case LINE_LINK:
			r = fprintf(fp, "=> %s %s\n", alt, text);
			break;

		default:
			/* not reached */
			abort();
		}

		if (r == -1)
			return 0;
	}

	return 1;
}
