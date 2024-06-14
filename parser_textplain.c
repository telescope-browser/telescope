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

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "telescope.h"

static int	textplain_parse_line(struct buffer *, const char *, size_t);

struct parser textplain_parser = {
	.name = "text/plain",
	.parseline = &textplain_parse_line,
};

static inline int
emit_line(struct buffer *b, const char *line, size_t len)
{
	struct line *l;

	if ((l = calloc(1, sizeof(*l))) == NULL)
		return 0;

	l->type = LINE_TEXT;

	if (len != 0) {
		if ((l->line = calloc(1, len+1)) == NULL) {
			free(l);
			return 0;
		}

		memcpy(l->line, line, len);
	}

	TAILQ_INSERT_TAIL(&b->head, l, lines);

	return 1;
}

static int
textplain_parse_line(struct buffer *b, const char *line, size_t linelen)
{
	return emit_line(b, line, linelen);
}
