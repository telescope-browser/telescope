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
 * A streaming text/plain "parser."
 */

#include "telescope.h"

#include <stdlib.h>
#include <string.h>

static int	textplain_parse(struct parser*, const char*, size_t);
static int	textplain_free(struct parser*);

static inline int
emit_line(struct parser *p, enum line_type type, const char *line, size_t len)
{
	struct line *l;

	if ((l = calloc(1, sizeof(*l))) == NULL)
		return 0;

	l->type = type;

	if (len != 0) {
		if ((l->line = calloc(1, len+1)) == NULL) {
			free(l);
			return 0;
		}

		memcpy(l->line, line, len);
	}

	if (TAILQ_EMPTY(&p->head))
		TAILQ_INSERT_HEAD(&p->head, l, lines);
	else
		TAILQ_INSERT_TAIL(&p->head, l, lines);

	return 1;
}

void
textplain_initparser(struct parser *p)
{
	memset(p, 0, sizeof(*p));

	p->name = "text/plain";
	p->parse = &textplain_parse;
	p->free = &textplain_free;

	emit_line(p, LINE_PRE_START, NULL, 0);
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
textplain_parse(struct parser *p, const char *buf, size_t size)
{
	const char	*b, *e;
	size_t		 len, l;
	int		 r;

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

		r = emit_line(p, LINE_PRE_CONTENT, b, l);
		if (!r)
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
textplain_free(struct parser *p)
{
	/* flush the buffer */
	if (p->len != 0) {
		if (!emit_line(p, LINE_PRE_CONTENT, p->buf, p->len))
			return 0;
	}

	return emit_line(p, LINE_PRE_END, NULL, 0);
}
