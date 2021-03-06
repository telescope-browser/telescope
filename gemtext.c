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

/*
 * A streaming gemtext parser.
 *
 * TODO:
 *  - handle NULs
 *  - UTF8
 */

#include <telescope.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

static int	gemtext_parse(struct parser*, const char*, size_t);
static int	gemtext_free(struct parser*);

static int	parse_text(struct parser*, enum line_type, const char*, size_t);
static int	parse_link(struct parser*, enum line_type, const char*, size_t);
static int	parse_title(struct parser*, enum line_type, const char*, size_t);
static int	parse_item(struct parser*, enum line_type, const char*, size_t);
static int	parse_quote(struct parser*, enum line_type, const char*, size_t);
static int	parse_pre_start(struct parser*, enum line_type, const char*, size_t);
static int	parse_pre_cnt(struct parser*, enum line_type, const char*, size_t);
static int	parse_pre_end(struct parser*, enum line_type, const char*, size_t);

typedef int (parselinefn)(struct parser*, enum line_type, const char*, size_t);

static parselinefn *parsers[] = {
	parse_text,		/* LINE_TEXT */
	parse_link,		/* LINE_LINK */
	parse_title,		/* LINE_TITLE_1 */
	parse_title,		/* LINE_TITLE_2 */
	parse_title,		/* LINE_TITLE_3 */
	parse_item,		/* LINE_ITEM */
	parse_quote,		/* LINE_QUOTE */
	parse_pre_start,	/* LINE_PRE_START */
	parse_pre_cnt,		/* LINE_PRE_CONTENT */
	parse_pre_end,		/* LINE_PRE_END */
};

void
gemtext_initparser(struct parser *p)
{
	memset(p, 0, sizeof(*p));

	p->parse = &gemtext_parse;
	p->free  = &gemtext_free;
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

	if (TAILQ_EMPTY(&p->head))
		TAILQ_INSERT_HEAD(&p->head, l, lines);
	else
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
		return emit_line(p, t, NULL, NULL);
	buf += 2;
	len -= 2;

	while (len > 0 && isspace(buf[0])) {
		buf++;
		len--;
	}

	if (len == 0)
		return emit_line(p, t, NULL, NULL);

	url_start = buf;
	while (len > 0 && !isspace(buf[0])) {
		buf++;
		len--;
	}

	if ((u = calloc(1, buf - url_start + 1)) == NULL)
		return 0;
	memcpy(u, url_start, buf - url_start);

	if (len == 0)
		return emit_line(p, t, u, NULL);

	while (len > 0) {
		buf++;
		len--;
	}

	if (len == 0)
		return emit_line(p, t, u, NULL);

	if ((l = calloc(1, len + 1)) == NULL)
		return 0;

	memcpy(l, buf, len);
	return emit_line(p, t, u, l);
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
	len += 3;

	while (len > 0 && isspace(buf[0])) {
		buf++;
		len--;
	}

	if (len == 0)
		return emit_line(p, t, NULL, NULL);

	if ((l = calloc(1, len+1)) == NULL)
		return 0;

	memcpy(l, buf, len);
	return emit_line(p, t, NULL, l);
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
	size_t i;

	if (len == 0)
		return LINE_TEXT;

	if (in_pre) {
		if (len >= 3 &&
		    buf[0] == '`' && buf[1] == '`' && buf[2] == '`')
			return LINE_PRE_END;
		else
			return LINE_PRE_CONTENT;
	}

	switch (*buf) {
	case '*': return LINE_ITEM;
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

static inline int
append(struct parser *p, const char *buf, size_t len)
{
	size_t newlen;
	char *t;

	newlen = len + p->len;
	if ((t = calloc(1, newlen)) == NULL)
		return 0;
	memcpy(t, p->buf, p->len);
	memcpy(t + p->len, buf, len);
	free(p->buf);
	p->buf = t;
	p->len = newlen;
	return 1;
}

static inline int
set_buf(struct parser *p, const char *buf, size_t len)
{
	free(p->buf);
	p->buf = NULL;

	if (len == 0) {
		p->len = 0;
		return 1;
	}

	if ((p->buf = calloc(1, len)) == NULL)
		return 0;
	memcpy(p->buf, buf, len);
	p->len = len;
	return 1;
}

static int
gemtext_parse(struct parser *p, const char *buf, size_t size)
{
	const char	*b, *e;
	enum line_type	 t;
	size_t		 len, l;

	if (p->len == 0) {
		b = buf;
		len = size;
	} else {
		if (!append(p, buf, size))
			return 0;
		b = p->buf;
		len = p->len;
	}

	while (len > 0) {
		if ((e = telescope_strnchr((char*)b, '\n', len)) == NULL)
			break;
		l = e - b;
		t = detect_line_type(b, l, p->flags);
		if (t == LINE_PRE_START)
			p->flags = 1;
		if (t == LINE_PRE_END)
			p->flags = 0;
		if (!parsers[t](p, t, b, l))
			return 0;

		len -= l;
		b += l;

		if (len > 0) {
			/* skip \n */
			len--;
			b++;
		}
	}

	return set_buf(p, b, len);
}

static int
gemtext_free(struct parser *p)
{
	enum line_type	t;

	/* flush the buffer */
	if (p->len != 0) {
		t = detect_line_type(p->buf, p->len, p->flags);
		if (!parsers[t](p, t, p->buf, p->len))
			return 0;
		if (p->flags && !emit_line(p, LINE_PRE_END, NULL, NULL))
			return 0;
	}

	free(p->buf);
	return 1;
}
