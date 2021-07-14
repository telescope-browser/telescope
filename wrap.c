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

#include <stdlib.h>
#include <string.h>

#include "telescope.h"
#include "utf8.h"

/*
 * Text wrapping
 * =============
 *
 * There's a simple text wrapping algorithm.
 *
 * 1. if it's a line in a pre-formatted block:
 *    a. hard wrap.
 *    b. repeat
 * 2. otherwise advance the line char by char.
 * 3. when ending the space, split the line at the last occurrence of
 *    a "word separator" (i.e. " \t-") or at point if none.
 * 4. repeat
 *
 */

void
erase_buffer(struct buffer *buffer)
{
	empty_vlist(buffer);
	empty_linelist(buffer);
}

void
empty_linelist(struct buffer *buffer)
{
	struct line *l, *lt;

	TAILQ_FOREACH_SAFE(l, &buffer->page.head, lines, lt) {
		TAILQ_REMOVE(&buffer->page.head, l, lines);
		free(l->line);

		if (l->type != LINE_COMPL &&
		    l->type != LINE_COMPL_CURRENT)
			free(l->meta.alt);

		free(l);
	}
}

void
empty_vlist(struct buffer *buffer)
{
	struct vline *vl, *t;

	buffer->top_line = NULL;
	buffer->line_off = 0;
	buffer->current_line = NULL;
	buffer->line_max = 0;

	TAILQ_FOREACH_SAFE(vl, &buffer->head, vlines, t) {
		TAILQ_REMOVE(&buffer->head, vl, vlines);
		free(vl->line);
		free(vl);
	}
}

static int
push_line(struct buffer *buffer, struct line *l, const char *buf, size_t len, int flags)
{
	struct vline *vl;

	if (!(l->flags & L_HIDDEN))
		buffer->line_max++;

	if ((vl = calloc(1, sizeof(*vl))) == NULL)
		return 0;

	if (len != 0 && (vl->line = calloc(1, len+1)) == NULL) {
		free(vl);
		return 0;
	}

	vl->parent = l;
	if (len != 0)
		memcpy(vl->line, buf, len);
	vl->flags = flags;

	if (TAILQ_EMPTY(&buffer->head))
		TAILQ_INSERT_HEAD(&buffer->head, vl, vlines);
	else
		TAILQ_INSERT_TAIL(&buffer->head, vl, vlines);
	return 1;
}

/*
 * Similar to wrap_text, but emit only o vline.
 */
int
wrap_one(struct buffer *buffer, const char *prfx, struct line *l, size_t width)
{
	struct vline *vl, *t;

	/*
	 * be lazy: call wrap_text and then discard the continuations.
	 */

	if (!wrap_text(buffer, prfx, l, width))
		return 0;

	TAILQ_FOREACH_SAFE(vl, &buffer->head, vlines, t) {
		if (vl->flags & L_CONTINUATION) {
			TAILQ_REMOVE(&buffer->head, vl, vlines);
			free(vl->line);
			free(vl);
		}
	}

	return 1;
}

/*
 * Build a list of visual line by wrapping the given line, assuming
 * that when printed will have a leading prefix prfx.
 */
int
wrap_text(struct buffer *buffer, const char *prfx, struct line *l, size_t width)
{
	const char	*separators = " \t-";
	const char	*start, *end, *line, *lastsep, *lastchar;
	uint32_t	 cp = 0, state = 0;
	size_t		 cur, prfxwidth, w;
	int		 flags;

	if ((line = l->line) == NULL)
		return push_line(buffer, l, NULL, 0, 0);

	prfxwidth = utf8_swidth(prfx);
	cur = prfxwidth;
	start = line;
	lastsep = NULL;
	lastchar = line;
	flags = 0;
	for (; *line; line++) {
		if (utf8_decode(&state, &cp, *line))
			continue;
		w = utf8_chwidth(cp);
		if (cur + w >= width -1) {
			end = lastsep == NULL
				? utf8_next_cp((char*)lastchar)
				: utf8_next_cp((char*)lastsep);
			if (!push_line(buffer, l, start, end - start, flags))
				return 0;
			flags = L_CONTINUATION;
			start = end;
			cur = prfxwidth + utf8_swidth_between(start, lastchar);
		} else if (strchr(separators, *line) != NULL) {
			lastsep = line;
		}

		lastchar = utf8_prev_cp(line, l->line);
		cur += w;
	}

	return push_line(buffer, l, start, line - start, flags);
}

int
hardwrap_text(struct buffer *buffer, struct line *l, size_t width)
{
	const char	*line, *start, *lastchar;
	int		 cont;
	uint32_t	 state = 0, cp = 0;
	size_t		 cur, w;

	if ((line = l->line) == NULL)
		return push_line(buffer, l, NULL, 0, 0);

	start = line;
	lastchar = line;
	cont = 0;
	cur = 0;
	for (; *line; line++) {
		if (utf8_decode(&state, &cp, *line))
			continue;
		w = utf8_chwidth(cp);
		if (cur + w >= width) {
			if (!push_line(buffer, l, start, lastchar - start, cont))
				return 0;
			cont = 1;
			cur = 0;
			start = lastchar;
		}

		lastchar = utf8_prev_cp(line, l->line);
		cur += w;
	}

	return push_line(buffer, l, start, line - start, cont);
}
