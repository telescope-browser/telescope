/*
 * Copyright (c) 2021, 2024 Omar Polo <op@omarpolo.com>
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

struct hist {
	TAILQ_HEAD(mhist, hist_item)	 head;
	int				 flags;
	size_t				 size;
	ssize_t				 off;
	struct hist_item		*cur;
};

struct hist_item {
	char			*str;
	size_t			 line_off;
	size_t			 current_off;
	TAILQ_ENTRY(hist_item)	 entries;
};

struct hist *
hist_new(int flags)
{
	struct hist	*hist;

	if ((hist = calloc(1, sizeof(*hist))) == NULL)
		return (NULL);

	TAILQ_INIT(&hist->head);
	hist->flags = flags;
	hist->off = -1;
	return (hist);
}

void
hist_free(struct hist *hist)
{
	if (hist == NULL)
		return;

	hist_erase(hist);
	free(hist);
}

static void
hist_erase_from(struct hist *hist, struct hist_item *h)
{
	struct hist_item	*next;

	while (h != NULL) {
		next = TAILQ_NEXT(h, entries);

		hist->size--;
		TAILQ_REMOVE(&hist->head, h, entries);
		free(h->str);
		free(h);
		h = next;
	}
}

void
hist_erase(struct hist *hist)
{
	hist_erase_from(hist, TAILQ_FIRST(&hist->head));

	hist->size = 0;
	hist->off = -1;
	hist->cur = NULL;
}

size_t
hist_size(struct hist *hist)
{
	return (hist->size);
}

size_t
hist_off(struct hist *hist)
{
	if (hist->off == -1)
		return (0);
	return (hist->off);
}

const char *
hist_cur(struct hist *hist)
{
	if (hist->cur == NULL)
		return (NULL);
	return (hist->cur->str);
}

int
hist_cur_offs(struct hist *hist, size_t *line, size_t *curr)
{
	*line = 0;
	*curr = 0;

	if (hist->cur == NULL)
		return (-1);

	*line = hist->cur->line_off;
	*curr = hist->cur->current_off;
	return (0);
}

int
hist_set_cur(struct hist *hist, const char *str)
{
	char		*d;

	if (hist->cur == NULL)
		return (-1);

	if ((d = strdup(str)) == NULL)
		return (-1);

	free(hist->cur->str);
	hist->cur->str = d;
	return (0);
}

int
hist_set_offs(struct hist *hist, size_t line, size_t curr)
{
	if (hist->cur == NULL)
		return (-1);

	hist->cur->line_off = line;
	hist->cur->current_off = line;
	return (0);
}

const char *
hist_nth(struct hist *hist, size_t n)
{
	size_t			 i;
	struct hist_item	*h;

	i = 0;
	TAILQ_FOREACH(h, &hist->head, entries) {
		if (i++ == n)
			return (h->str);
	}
	return (NULL);
}

const char *
hist_prev(struct hist *hist)
{
	struct hist_item	*h;
	int			 wrap = hist->flags & HIST_WRAP;

	if (hist->cur == NULL && !wrap)
		return (NULL);

	if (hist->cur == NULL ||
	    (h = TAILQ_PREV(hist->cur, mhist, entries)) == NULL) {
		if (!wrap || (h = TAILQ_LAST(&hist->head, mhist)) == NULL)
			return (NULL);
		hist->off = hist->size - 1;
	} else
		hist->off--;

	hist->cur = h;
	return (h->str);
}

const char *
hist_next(struct hist *hist)
{
	struct hist_item	*h;
	int			 wrap = hist->flags & HIST_WRAP;

	if (hist->cur == NULL && !wrap)
		return (NULL);

	if (hist->cur == NULL ||
	    (h = TAILQ_NEXT(hist->cur, entries)) == NULL) {
		if (!wrap || (h = TAILQ_FIRST(&hist->head)) == NULL)
			return (NULL);
		hist->off = 0;
	} else
		hist->off++;

	hist->cur = h;
	return (h->str);
}

void
hist_seek_start(struct hist *hist)
{
	hist->off = -1;
	hist->cur = NULL;
}

int
hist_push(struct hist *hist, const char *str)
{
	struct hist_item	*h;

	if ((h = calloc(1, sizeof(*h))) == NULL)
		return (-1);

	if ((h->str = strdup(str)) == NULL) {
		free(h);
		return (-1);
	}

	if (hist->cur != NULL)
		hist_erase_from(hist, TAILQ_NEXT(hist->cur, entries));
	hist->cur = h;
	hist->off++;
	hist->size++;
	TAILQ_INSERT_TAIL(&hist->head, h, entries);
	return (0);
}

int
hist_prepend(struct hist *hist, const char *str)
{
	struct hist_item	*h;

	if (hist->cur == NULL)
		return (-1);

	if ((h = calloc(1, sizeof(*h))) == NULL)
		return (-1);

	if ((h->str = strdup(str)) == NULL) {
		free(h);
		return (-1);
	}

	hist->size++;
	hist->off++;
	TAILQ_INSERT_BEFORE(hist->cur, h, entries);
	return (0);
}

int
hist_append(struct hist *hist, const char *str)
{
	struct hist_item	*h;

	/*
	 * Not sure.  The minibuffer needs to append even when there
	 * are no items.
	 */
	if (hist->cur == NULL && !(hist->flags & HIST_WRAP))
		return (-1);

	if ((h = calloc(1, sizeof(*h))) == NULL)
		return (-1);

	if ((h->str = strdup(str)) == NULL) {
		free(h);
		return (-1);
	}

	hist->size++;
	TAILQ_INSERT_TAIL(&hist->head, h, entries);
	return (0);
}
