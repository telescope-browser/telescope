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
 * 2. there is enough room for the next word?
 *    a. yes: render it
 *    b. no: break the current line.
 *       i.  while there isn't enough space to draw the current
 *           word, hard-wrap it
 *       ii. draw the remainder of the current word (can be the
 *           the entirely)
 * 3. render the spaces after the word
 *    a. but if there is not enough room break the line and
 *       forget them
 * 4. repeat
 *
 */

static int
push_line(struct tab *tab, const struct line *l, const char *buf, size_t len, int cont)
{
	struct vline *vl;

	tab->s.line_max++;

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

	if (TAILQ_EMPTY(&tab->s.head))
		TAILQ_INSERT_HEAD(&tab->s.head, vl, vlines);
	else
		TAILQ_INSERT_TAIL(&tab->s.head, vl, vlines);
	return 1;
}

/*
 * Helper function for wrap_text.  Find the end of the current word
 * and the end of the separator after the word.
 */
static int
word_boundaries(const char *s, const char *sep, const char **endword, const char **endspc)
{
	*endword = s;
	*endword = s;

	if (*s == '\0')
		return 0;

	/* find the end of the current world */
	for (; *s != '\0'; ++s) {
		if (strchr(sep, *s) != NULL)
			break;
	}

	*endword = s;

	/* find the end of the separator */
	for (; *s != '\0'; ++s) {
		if (strchr(sep, *s) == NULL)
			break;
	}

	*endspc = s;

	return 1;
}

static inline int
emitline(struct tab *tab, size_t zero, size_t *off, const struct line *l,
    const char **line, int *cont)
{
	if (!push_line(tab, l, *line, *off - zero, *cont))
		return 0;
	if (!*cont)
		*cont = 1;
	*line += *off - zero;
	*off = zero;
	return 1;
}

/*
 * Build a list of visual line by wrapping the given line, assuming
 * that when printed will have a leading prefix prfx.
 *
 * TODO: it considers each byte one cell on the screen!
 */
void
wrap_text(struct tab *tab, const char *prfx, struct line *l, size_t width)
{
	size_t		 zero, off, len, split;
	int		 cont = 0;
	const char	*endword, *endspc, *line, *linestart;

	zero = strlen(prfx);
	off = zero;
	line = l->line;
	linestart = l->line;

	if (line == NULL) {
		push_line(tab, l, NULL, 0, 0);
		return;
	}

	while (word_boundaries(line, " \t-", &endword, &endspc)) {
		len = endword - line;
		if (off + len >= width) {
			emitline(tab, zero, &off, l, &linestart, &cont);
			while (len >= width) {
				/* hard wrap */
				emitline(tab, zero, &off, l, &linestart, &cont);
				len -= width-1;
				line += width-1;
			}

			if (len != 0)
				off += len;
		} else
			off += len;

		/* print the spaces iff not at bol */
		len = endspc - endword;
		/* line = endspc; */
		if (off != zero) {
			if (off + len >= width) {
				emitline(tab, zero, &off, l, &linestart, &cont);
				linestart = endspc;
			} else
				off += len;
		}

		line = endspc;
	}

	emitline(tab, zero, &off, l, &linestart, &cont);
}

int
hardwrap_text(struct tab *tab, struct line *l, size_t width)
{
	size_t		 off, len;
	int		 cont;
	const char	*linestart;

	if (l->line == NULL)
		return emitline(tab, 0, &off, l, &linestart, &cont);

        len = strlen(l->line);
	off = 0;
	linestart = l->line;

	while (len >= width) {
		len -= width-1;
		off = width-1;
		if (!emitline(tab, 0, &off, l, &linestart, &cont))
			return 0;
	}

	if (len != 0)
		return emitline(tab, 0, &len, l, &linestart, &cont);

	return 1;
}
