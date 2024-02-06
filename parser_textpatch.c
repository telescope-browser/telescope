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
 * A streaming text/x-patch parser
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "utils.h"

static int	tpatch_parse(struct parser *, const char *, size_t);
static int	tpatch_emit_line(struct parser *, const char *, size_t);
static int	tpatch_foreach_line(struct parser *, const char *, size_t);
static int	tpatch_free(struct parser *);

void
textpatch_initparser(struct parser *p)
{
	memset(p, 0, sizeof(*p));

	p->name = "text/x-patch";
	p->parse = &tpatch_parse;
	p->free = &tpatch_free;

	p->flags = PARSER_IN_PATCH_HDR;

	TAILQ_INIT(&p->head);
}

static int
tpatch_parse(struct parser *p, const char *buf, size_t size)
{
	return parser_foreach_line(p, buf, size, tpatch_foreach_line);
}

static int
tpatch_emit_line(struct parser *p, const char *line, size_t linelen)
{
	struct line *l;

	if ((l = calloc(1, sizeof(*l))) == NULL)
		return 0;

	if (p->flags & PARSER_IN_PATCH_HDR)
		l->type = LINE_PATCH_HDR;
	else
		l->type = LINE_PATCH;

	if (linelen != 0) {
		if ((l->line = calloc(1, linelen+1)) == NULL) {
			free(l);
			return 0;
		}

		memcpy(l->line, line, linelen);

		if (!(p->flags & PARSER_IN_PATCH_HDR))
			switch (*l->line) {
			case '+':
				l->type = LINE_PATCH_ADD;
				break;
			case '-':
				l->type = LINE_PATCH_DEL;
				break;
			case '@':
				l->type = LINE_PATCH_HUNK_HDR;
				break;
			case ' ':
				/* context lines */
				break;
			default:
				/*
				 * A single patch file can have more
				 * than one "header" if touches more
				 * than one file.
				 */
				l->type = LINE_PATCH_HDR;
				p->flags |= PARSER_IN_PATCH_HDR;
				break;
			}

		if (!strncmp(l->line, "+++", 3))
			p->flags &= ~PARSER_IN_PATCH_HDR;
	}

	TAILQ_INSERT_TAIL(&p->head, l, lines);

	return 1;
}

static int
tpatch_foreach_line(struct parser *p, const char *line, size_t linelen)
{
	return tpatch_emit_line(p, line, linelen);
}

static int
tpatch_free(struct parser *p)
{
	if (p->len != 0)
		return tpatch_emit_line(p, p->buf, p->len);
	return 1;
}
