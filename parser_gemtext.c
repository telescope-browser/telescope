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
#include "utf8.h"

static int	gemtext_parse(struct parser *, const char *, size_t);
static int	gemtext_foreach_line(struct parser *, const char *, size_t);
static int	gemtext_free(struct parser *);
static int	gemtext_serialize(struct parser *, FILE *);

static int	parse_text(struct parser*, enum line_type, const char*, size_t);
static int	parse_link(struct parser*, enum line_type, const char*, size_t);
static int	parse_title(struct parser*, enum line_type, const char*, size_t);
static int	parse_item(struct parser*, enum line_type, const char*, size_t);
static int	parse_quote(struct parser*, enum line_type, const char*, size_t);
static int	parse_pre_start(struct parser*, enum line_type, const char*, size_t);
static int	parse_pre_cnt(struct parser*, enum line_type, const char*, size_t);
static int	parse_pre_end(struct parser*, enum line_type, const char*, size_t);
static void	search_title(struct parser*, enum line_type);

typedef int (parselinefn)(struct parser*, enum line_type, const char*, size_t);

static parselinefn *parsers[] = {
	[LINE_TEXT]		= parse_text,
	[LINE_LINK]		= parse_link,
	[LINE_TITLE_1]		= parse_title,
	[LINE_TITLE_2]		= parse_title,
	[LINE_TITLE_3]		= parse_title,
	[LINE_ITEM]		= parse_item,
	[LINE_QUOTE]		= parse_quote,
	[LINE_PRE_START]	= parse_pre_start,
	[LINE_PRE_CONTENT]	= parse_pre_cnt,
	[LINE_PRE_END]		= parse_pre_end,
};

void
gemtext_initparser(struct parser *p)
{
	memset(p, 0, sizeof(*p));

	p->name = "text/gemini";
	p->parse = &gemtext_parse;
	p->free  = &gemtext_free;
	p->serialize = &gemtext_serialize;

	TAILQ_INIT(&p->head);
}

static inline int
emit_line(struct parser *p, enum line_type type, char *line, char *alt)
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

	TAILQ_INSERT_TAIL(&p->head, l, lines);

	return 1;
}

static int
parse_text(struct parser *p, enum line_type t, const char *buf, size_t len)
{
	char *l;

	if ((l = calloc(1, len+1)) == NULL)
		return 0;
	memcpy(l, buf, len);
	return emit_line(p, t, l, NULL);
}

static int
parse_link(struct parser *p, enum line_type t, const char *buf, size_t len)
{
	char *l, *u;
	const char *url_start;

	if (len <= 2)
		return emit_line(p, LINE_TEXT, NULL, NULL);
	buf += 2;
	len -= 2;

	while (len > 0 && isspace(buf[0])) {
		buf++;
		len--;
	}

	if (len == 0)
		return emit_line(p, LINE_TEXT, NULL, NULL);

	url_start = buf;
	while (len > 0 && !isspace(buf[0])) {
		buf++;
		len--;
	}

	if ((u = calloc(1, buf - url_start + 1)) == NULL)
		return 0;
	memcpy(u, url_start, buf - url_start);

	if (len == 0)
		goto nolabel;

	while (len > 0 && isspace(buf[0])) {
		buf++;
		len--;
	}

	if (len == 0)
		goto nolabel;

	if ((l = calloc(1, len + 1)) == NULL)
		return 0;

	memcpy(l, buf, len);
	return emit_line(p, t, l, u);

nolabel:
	if ((l = strdup(u)) == NULL)
		return 0;
	return emit_line(p, t, l, u);
}

static int
parse_title(struct parser *p, enum line_type t, const char *buf, size_t len)
{
	char *l;

	switch (t) {
	case LINE_TITLE_1:
		if (len <= 1)
			return emit_line(p, t, NULL, NULL);
		buf++;
		len--;
		break;
	case LINE_TITLE_2:
		if (len <= 2)
			return emit_line(p, t, NULL, NULL);
		buf += 2;
		len -= 2;
		break;
	case LINE_TITLE_3:
		if (len <= 3)
			return emit_line(p, t, NULL, NULL);
		buf += 3;
		len -= 3;
		break;
	default:
		/* unreachable */
		abort();
	}

	while (len > 0 && isspace(buf[0])) {
		buf++;
		len--;
	}

	if (len == 0)
		return emit_line(p, t, NULL, NULL);

	if (t == LINE_TITLE_1 && *p->title == '\0')
		strncpy(p->title, buf, MIN(sizeof(p->title)-1, len));

	if ((l = calloc(1, len+1)) == NULL)
		return 0;
	memcpy(l, buf, len);
	return emit_line(p, t, l, NULL);
}

static int
parse_item(struct parser *p, enum line_type t, const char *buf, size_t len)
{
	char *l;

	if (len == 1)
		return emit_line(p, t, NULL, NULL);

	buf++;
	len--;

	while (len > 0 && isspace(buf[0])) {
		buf++;
		len--;
	}

	if (len == 0)
		return emit_line(p, t, NULL, NULL);

	if ((l = calloc(1, len+1)) == NULL)
		return 0;
	memcpy(l, buf, len);
	return emit_line(p, t, l, NULL);
}

static int
parse_quote(struct parser *p, enum line_type t, const char *buf, size_t len)
{
	char *l;

	if (len == 1)
		return emit_line(p, t, NULL, NULL);

	buf++;
	len--;

	while (len > 0 && isspace(buf[0])) {
		buf++;
		len--;
	}

	if (len == 0)
		return emit_line(p, t, NULL, NULL);

	if ((l = calloc(1, len+1)) == NULL)
		return 0;
	memcpy(l, buf, len);
	return emit_line(p, t, l, NULL);
}

static int
parse_pre_start(struct parser *p, enum line_type t, const char *buf, size_t len)
{
	char *l;

	if (len <= 3)
		return emit_line(p, t, NULL, NULL);

	buf += 3;
	len -= 3;

	while (len > 0 && isspace(buf[0])) {
		buf++;
		len--;
	}

	if (len == 0)
		return emit_line(p, t, NULL, NULL);

	if ((l = calloc(1, len+1)) == NULL)
		return 0;

	memcpy(l, buf, len);
	return emit_line(p, t, l, NULL);
}

static int
parse_pre_cnt(struct parser *p, enum line_type t, const char *buf, size_t len)
{
	char *l;

	if (len == 0)
		return emit_line(p, t, NULL, NULL);

	if ((l = calloc(1, len+1)) == NULL)
		return 0;
	memcpy(l, buf, len);
	return emit_line(p, t, l, NULL);
}

static int
parse_pre_end(struct parser *p, enum line_type t, const char *buf, size_t len)
{
	return emit_line(p, t, NULL, NULL);
}

static inline enum line_type
detect_line_type(const char *buf, size_t len, int in_pre)
{
	if (in_pre) {
		if (len >= 3 &&
		    buf[0] == '`' && buf[1] == '`' && buf[2] == '`')
			return LINE_PRE_END;
		else
			return LINE_PRE_CONTENT;
	}

	if (len == 0)
		return LINE_TEXT;

	switch (*buf) {
	case '*':
		if (len > 1 && buf[1] == ' ')
			return LINE_ITEM;
		break;
	case '>': return LINE_QUOTE;
	case '=':
		if (len >= 1 && buf[1] == '>')
			return LINE_LINK;
		break;
	case '#':
		if (len == 1)
			return LINE_TEXT;
		if (buf[1] != '#')
			return LINE_TITLE_1;
		if (len == 2)
			return LINE_TEXT;
		if (buf[2] != '#')
			return LINE_TITLE_2;
		if (len == 3)
			return LINE_TEXT;
		return LINE_TITLE_3;
	case '`':
		if (len < 3)
			return LINE_TEXT;
		if (buf[0] == '`' && buf[1] == '`' && buf[2] == '`')
			return LINE_PRE_START;
		break;
	}

	return LINE_TEXT;
}

static int
gemtext_parse(struct parser *p, const char *buf, size_t size)
{
	return parser_foreach_line(p, buf, size, gemtext_foreach_line);
}

static int
gemtext_foreach_line(struct parser *p, const char *line, size_t linelen)
{
	enum line_type t;

	t = detect_line_type(line, linelen, p->flags & PARSER_IN_PRE);
	if (t == LINE_PRE_START)
		p->flags ^= PARSER_IN_PRE;
	if (t == LINE_PRE_END)
		p->flags ^= PARSER_IN_PRE;
	return parsers[t](p, t, line, linelen);
}

static int
gemtext_free(struct parser *p)
{
	enum line_type	t;

	/* flush the buffer */
	if (p->len != 0) {
		t = detect_line_type(p->buf, p->len, p->flags & PARSER_IN_PRE);
		if (!parsers[t](p, t, p->buf, p->len))
			return 0;
		if ((p->flags & PARSER_IN_PRE) &&
		    !emit_line(p, LINE_PRE_END, NULL, NULL))
			return 0;
	}

	free(p->buf);

	/*
	 * use the first level 2 or 3 header as page title if none
	 * found yet.
	 */
	if (*p->title == '\0')
		search_title(p, LINE_TITLE_2);
	if (*p->title == '\0')
		search_title(p, LINE_TITLE_3);

	return 1;
}

static void
search_title(struct parser *p, enum line_type level)
{
	struct line *l;

	TAILQ_FOREACH(l, &p->head, lines) {
		if (l->type == level) {
			if (l->line == NULL)
				continue;
			strlcpy(p->title, l->line, sizeof(p->title));
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
gemtext_serialize(struct parser *p, FILE *fp)
{
	struct line	*line;
	const char	*text;
	const char	*alt;
	int		 r;

	TAILQ_FOREACH(line, &p->head, lines) {
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
