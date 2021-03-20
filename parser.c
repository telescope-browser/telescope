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

#include <stdlib.h>
#include <string.h>

int
parser_append(struct parser *p, const char *buf, size_t len)
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

int
parser_set_buf(struct parser *p, const char *buf, size_t len)
{
	char *tmp;

	if (len == 0) {
		p->len = 0;
		free(p->buf);
		p->buf = NULL;
		return 1;
	}

	/* p->buf and buf can (and probably almost always will)
	 * overlap! */

	if ((tmp = calloc(1, len)) == NULL)
		return 0;
	memcpy(tmp, buf, len);
	free(p->buf);
	p->buf = tmp;
	p->len = len;
	return 1;
}

int
parser_foreach_line(struct parser *p, const char *buf, size_t size,
    parsechunkfn fn)
{
	const char	*b, *e;
	size_t		 l, len;

	if (p->len == 0) {
		b = buf;
		len = size;
	} else {
		if (!parser_append(p, buf, size))
			return 0;
		b = p->buf;
		len = p->len;
	}

	while (len > 0) {
		if ((e = memmem((char*)b, len, "\n", 1)) == NULL)
			break;
		l = e - b;

		if (!fn(p, b, l))
			return 0;

		len -= l;
		b += l;

		if (len > 0) {
			/* skip \n */
			len--;
			b++;
		}
	}

	return parser_set_buf(p, b, len);
}
