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
empty_linelist(struct window *window)
{
	struct line *l, *lt;

	TAILQ_FOREACH_SAFE(l, &window->page.head, lines, lt) {
		TAILQ_REMOVE(&window->page.head, l, lines);
		free(l->line);
		free(l->alt);
		free(l);
	}
}

void
empty_vlist(struct window *window)
{
	struct vline *vl, *t;

	window->current_line = NULL;
	window->line_max = 0;

	TAILQ_FOREACH_SAFE(vl, &window->head, vlines, t) {
		TAILQ_REMOVE(&window->head, vl, vlines);
		free(vl->line);
		free(vl);
	}
}

static int
push_line(struct window *window, const struct line *l, const char *buf, size_t len, int cont)
{
	struct vline *vl;

	window->line_max++;

	if ((vl = calloc(1, sizeof(*vl))) == NULL)
		return 0;

	if (len != 0 && (vl->line = calloc(1, len+1)) == NULL) {
		free(vl);
		return 0;
	}

	vl->parent = l;
	if (len != 0)
		memcpy(vl->line, buf, len);
	vl->flags = cont;

	if (TAILQ_EMPTY(&window->head))
		TAILQ_INSERT_HEAD(&window->head, vl, vlines);
	else
		TAILQ_INSERT_TAIL(&window->head, vl, vlines);
	return 1;
}

/*
 * Build a list of visual line by wrapping the given line, assuming
 * that when printed will have a leading prefix prfx.
 */
int
wrap_text(struct window *window, const char *prfx, struct line *l, size_t width)
{
	const char	*separators = " \t-";
	const char	*start, *end, *line, *lastsep, *lastchar;
	uint32_t	 cp = 0, state = 0;
	size_t		 cur, prfxwidth, w;
	int		 cont;

	if ((line = l->line) == NULL)
		return push_line(window, l, NULL, 0, 0);

	prfxwidth = utf8_swidth(prfx);
	cur = prfxwidth;
	start = line;
	lastsep = NULL;
	lastchar = line;
	cont = 0;
	for (; *line; line++) {
		if (utf8_decode(&state, &cp, *line))
			continue;
		w = utf8_chwidth(cp);
		if (cur + w >= width -1) {
			end = lastsep == NULL
				? utf8_next_cp((char*)lastchar)
				: utf8_next_cp((char*)lastsep);
			if (!push_line(window, l, start, end - start, cont))
				return 0;
			cont = 1;
			start = end;
			cur = prfxwidth + utf8_swidth_between(start, lastchar);
		} else {
			if (strchr(separators, *line) != NULL)
				lastsep = line;
		}

		lastchar = utf8_prev_cp(line, l->line);
		cur += w;
	}

	return push_line(window, l, start, line - start, cont);
}

int
hardwrap_text(struct window *window, struct line *l, size_t width)
{
	const char	*line, *start, *lastchar;
	int		 cont;
	uint32_t	 state = 0, cp = 0;
	size_t		 cur, w;

	if ((line = l->line) == NULL)
		return push_line(window, l, NULL, 0, 0);

	start = line;
	lastchar = line;
	cont = 0;
	cur = 0;
	for (; *line; line++) {
		if (utf8_decode(&state, &cp, *line))
			continue;
		w = utf8_chwidth(cp);
		if (cur + w >= width) {
			if (!push_line(window, l, start, lastchar - start, cont))
				return 0;
			cont = 1;
			cur = 0;
			start = lastchar;
		}

		lastchar = utf8_prev_cp(line, l->line);
		cur += w;
	}

	return push_line(window, l, start, line - start, cont);
}
