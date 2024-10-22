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

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <grapheme.h>

#include "defaults.h"
#include "telescope.h"
#include "utf8.h"
#include "xwrapper.h"

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

	TAILQ_FOREACH_SAFE(l, &buffer->head, lines, lt) {
		TAILQ_REMOVE(&buffer->head, l, lines);
		free(l->line);

		if (l->type != LINE_COMPL &&
		    l->type != LINE_COMPL_CURRENT &&
		    l->type != LINE_HELP)
			free(l->alt);

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

	TAILQ_FOREACH_SAFE(vl, &buffer->vhead, vlines, t) {
		TAILQ_REMOVE(&buffer->vhead, vl, vlines);
		free(vl);
	}
}

static int
push_line(struct buffer *buffer, struct line *l, const char *buf, size_t len, int flags)
{
	struct vline *vl;
	const char *end;

	/* omit trailing spaces */
	if (len != 0) {
		for (end = buf + len - 1;
		     end > buf && isspace(*end);
		     end--, len--)
			;	/* nop */
	}

	if (!(l->flags & L_HIDDEN))
		buffer->line_max++;

	vl = xcalloc(1, sizeof(*vl));

	vl->parent = l;
	if (len != 0) {
		vl->from = buf - l->line;
		vl->len = len;
	}
	vl->flags = flags;

	TAILQ_INSERT_TAIL(&buffer->vhead, vl, vlines);
	return 1;
}

/*
 * Build a list of visual line by wrapping the given line, assuming
 * that when printed will have a leading prefix prfx.
 */
int
wrap_text(struct buffer *buffer, const char *prfx, struct line *l,
    size_t width, int base_offset, int oneline)
{
	const char	*line, *space;
	size_t		 ret, off, start, cur, prfxwidth;
	int		 flags;

	if ((line = l->line) == NULL || *line == '\0')
		return push_line(buffer, l, NULL, 0, 0);

	prfxwidth = utf8_swidth(prfx, base_offset);
	cur = base_offset + prfxwidth;
	start = 0;
	flags = 0;

	if (l->type == LINE_LINK && emojify_link &&
	    emojied_line(l->line, &space)) {
	    	prfxwidth = utf8_swidth_between(l->line, space, base_offset);
		cur = base_offset + prfxwidth;
		line = space + 1;
	}

	for (off = 0; line[off] != '\0'; off += ret) {
		size_t t;

		ret = grapheme_next_line_break_utf8(&line[off], SIZE_MAX);
		t = utf8_swidth_between(&line[off], &line[off + ret],
		    base_offset);

		/* we can't reach the last column */
		if (cur + t < width) {
			cur += t;
			continue;
		}

		if (!push_line(buffer, l, &line[start], off - start, flags))
			return 0;

		if (oneline)
			return 0;

		flags = L_CONTINUATION;
		start = off;
		cur = base_offset + prfxwidth + t;
	}

	if (off != start)
		return push_line(buffer, l, &line[start], off - start, flags);
	return 0;
}

int
wrap_page(struct buffer *buffer, int width, int x_offset)
{
	struct line		*l;
	const struct line	*top_orig, *orig;
	struct vline		*vl;
	const char		*prfx;

	top_orig = buffer->top_line == NULL ? NULL : buffer->top_line->parent;
	orig = buffer->current_line == NULL ? NULL : buffer->current_line->parent;

	buffer->top_line = NULL;
	buffer->current_line = NULL;

	buffer->force_redraw = 1;
	buffer->curs_y = 0;
	buffer->line_off = 0;

	empty_vlist(buffer);

	TAILQ_FOREACH(l, &buffer->head, lines) {
		prfx = line_prefixes[l->type].prfx1;
		switch (l->type) {
		case LINE_TEXT:
		case LINE_LINK:
		case LINE_TITLE_1:
		case LINE_TITLE_2:
		case LINE_TITLE_3:
		case LINE_ITEM:
		case LINE_QUOTE:
		case LINE_PRE_START:
		case LINE_PRE_END:
		case LINE_PRE_CONTENT:
		case LINE_PATCH:
		case LINE_PATCH_HDR:
		case LINE_PATCH_HUNK_HDR:
		case LINE_PATCH_ADD:
		case LINE_PATCH_DEL:
			wrap_text(buffer, prfx, l, MIN(fill_column, width),
			    x_offset, 0);
			break;
		case LINE_COMPL:
		case LINE_COMPL_CURRENT:
		case LINE_HELP:
		case LINE_DOWNLOAD:
		case LINE_DOWNLOAD_DONE:
		case LINE_DOWNLOAD_INFO:
			wrap_text(buffer, prfx, l, width, x_offset, 1);
			break;
		case LINE_FRINGE:
			/* never, ever wrapped */
			break;
		}

		if (top_orig == l && buffer->top_line == NULL) {
			buffer->line_off = buffer->line_max-1;
			buffer->top_line = TAILQ_LAST(&buffer->vhead, vhead);

			while (1) {
				vl = TAILQ_PREV(buffer->top_line, vhead, vlines);
				if (vl == NULL || vl->parent != orig)
					break;
				buffer->top_line = vl;
				buffer->line_off--;
			}
		}

		if (orig == l && buffer->current_line == NULL) {
			buffer->current_line = TAILQ_LAST(&buffer->vhead, vhead);

			while (1) {
				vl = TAILQ_PREV(buffer->current_line, vhead, vlines);
				if (vl == NULL || vl->parent != orig)
					break;
				buffer->current_line = vl;
			}
		}
	}

	if (buffer->current_line == NULL)
		buffer->current_line = TAILQ_FIRST(&buffer->vhead);

	if (buffer->top_line == NULL)
		buffer->top_line = buffer->current_line;

	return 1;
}
