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

#include <stdlib.h>
#include <string.h>

#include "telescope.h"
#include "parser.h"

static int	textplain_parse(struct parser*, const char*, size_t);
static int	textplain_foreach_line(struct parser*, const char*, size_t);
static int	textplain_free(struct parser*);

static inline int
emit_line(struct parser *p, const char *line, size_t len)
{
	struct line *l;

	if ((l = calloc(1, sizeof(*l))) == NULL)
		return 0;

	l->type = LINE_PRE_CONTENT;

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
}

static int
textplain_parse(struct parser *p, const char *buf, size_t size)
{
	return parser_foreach_line(p, buf, size, textplain_foreach_line);
}

static int
textplain_foreach_line(struct parser *p, const char *line, size_t linelen)
{
	return emit_line(p, line, linelen);
}

static int
textplain_free(struct parser *p)
{
	if (p->len != 0)
		return emit_line(p, p->buf, p->len);
	return 1;
}
