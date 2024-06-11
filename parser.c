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

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "hist.h"
#include "parser.h"
#include "telescope.h"
#include "xwrapper.h"

static int parser_foreach_line(struct buffer *, const char *, size_t);

void
parser_init(struct buffer *buffer, const struct parser *p)
{
	erase_buffer(buffer);

	memset(buffer->title, 0, sizeof(buffer->title));
	buffer->parser = p;
	buffer->mode = p->name;
	buffer->parser_flags = p->initflags;
}

int
parser_parse(struct buffer *buffer, const char *chunk, size_t len)
{
	const struct parser *p = buffer->parser;

	if (p->parse)
		return p->parse(buffer, chunk, len);
	return parser_foreach_line(buffer, chunk, len);
}

int
parser_parsef(struct buffer *buffer, const char *fmt, ...)
{
	char *s;
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vasprintf(&s, fmt, ap);
	va_end(ap);

	if (r == -1)
		return 0;

	r = parser_parse(buffer, s, strlen(s));
	free(s);
	return r;
}

int
parser_free(struct tab *tab)
{
	struct buffer		*buffer = &tab->buffer;
	const struct parser	*p = buffer->parser;
	int			 r = 1;
	char			*tilde, *slash;

	if (p->free) {
		r = p->free(buffer);
	} else if (buffer->len != 0) {
		if (p->parse)
			r = p->parse(buffer, buffer->buf, buffer->len);
		else
			r = parser_foreach_line(buffer, buffer->buf,
			    buffer->len);
	}

	free(buffer->buf);
	buffer->buf = NULL;
	buffer->len = 0;

	if (*buffer->title != '\0')
		return r;

	/*
	 * heuristic: see if there is a "tilde user" and use that as
	 * page title, using the full domain name as fallback.
	 */
	if ((tilde = strstr(hist_cur(tab->hist), "/~")) != NULL) {
		strlcpy(buffer->title, tilde+1, sizeof(buffer->title));

		if ((slash = strchr(buffer->title, '/')) != NULL)
			*slash = '\0';
	} else
		strlcpy(buffer->title, tab->iri.iri_host,
		    sizeof(buffer->title));

	return r;
}

int
parser_serialize(struct buffer *b, FILE *fp)
{
	const struct parser	*p = b->parser;
	struct line		*line;
	const char		*text;
	int			 r;

	if (p->serialize != NULL)
		return p->serialize(b, fp);

	/* a default implementation good enough for plain text */
	TAILQ_FOREACH(line, &b->head, lines) {
		if ((text = line->line) == NULL)
			text = "";

		r = fprintf(fp, "%s\n", text);
		if (r == -1)
			return 0;
	}

	return 1;
}

int
parser_append(struct buffer *b, const char *buf, size_t len)
{
	size_t newlen;
	char *t;

	newlen = len + b->len;
	t = xcalloc(1, newlen);
	memcpy(t, b->buf, b->len);
	memcpy(t + b->len, buf, len);
	free(b->buf);
	b->buf = t;
	b->len = newlen;
	return 1;
}

int
parser_set_buf(struct buffer *b, const char *buf, size_t len)
{
	char *tmp;

	if (len == 0) {
		b->len = 0;
		free(b->buf);
		b->buf = NULL;
		return 1;
	}

	/*
	 * p->buf and buf can (and probably almost always will)
	 * overlap!
	 */

	tmp = xcalloc(1, len);
	memcpy(tmp, buf, len);
	free(b->buf);
	b->buf = tmp;
	b->len = len;
	return 1;
}

static int
parser_foreach_line(struct buffer *b, const char *buf, size_t size)
{
	const struct parser	*p = b->parser;
	char			*beg, *end;
	unsigned int		 ch;
	size_t			 i, l, len;

	if (!parser_append(b, buf, size))
		return 0;
	beg = b->buf;
	len = b->len;

	if (!(b->parser_flags & PARSER_IN_BODY) && len < 3)
		return 1;

	if (!(b->parser_flags & PARSER_IN_BODY)) {
		b->parser_flags |= PARSER_IN_BODY;

		/*
		 * drop the BOM: only UTF-8 is supported, and there
		 * it's useless; some editors may still add one
		 * though.
		 */
		if (memmem(beg, len, "\xEF\xBB\xBF", 3) == beg) {
			b += 3;
			len -= 3;
		}
	}

	/* drop every "funny" ASCII character */
	for (i = 0; i < len; ) {
		ch = beg[i];
		if ((ch >= ' ' || ch == '\n' || ch == '\t')
		    && ch != 127) { /* del */
			++i;
			continue;
		}
		memmove(&beg[i], &beg[i+1], len - i - 1);
		len--;
	}

	while (len > 0) {
		if ((end = memmem((char*)beg, len, "\n", 1)) == NULL)
			break;
		l = end - beg;

		if (!p->parseline(b, beg, l))
			return 0;

		len -= l;
		beg += l;

		if (len > 0) {
			/* skip \n */
			len--;
			beg++;
		}
	}

	return parser_set_buf(b, beg, len);
}
