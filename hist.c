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

#include "telescope.h"

void
hist_clear(struct histhead *head)
{
	struct hist *h, *th;

	TAILQ_FOREACH_SAFE(h, &head->head, entries, th) {
		TAILQ_REMOVE(&head->head, h, entries);
		free(h);
	}
	head->len = 0;
}

void
hist_clear_forward(struct histhead *head, struct hist *h)
{
	if (h == NULL)
		return;
	hist_clear_forward(head, TAILQ_NEXT(h, entries));
	TAILQ_REMOVE(&head->head, h, entries);
	free(h);
}

void
hist_push(struct histhead *head, struct hist *h)
{
	head->len++;
	TAILQ_INSERT_TAIL(&head->head, h, entries);
}

void
hist_add_before(struct histhead *head, struct hist *curr, struct hist *h)
{
	head->len++;
	TAILQ_INSERT_BEFORE(curr, h, entries);
}

struct hist *
hist_pop(struct histhead *head)
{
	struct hist	*h, *p;

	if ((h = TAILQ_LAST(&head->head, mhisthead)) == NULL)
		return NULL;
	if ((p = TAILQ_PREV(h, mhisthead, entries)) == NULL)
		return NULL;

	hist_clear_forward(head, h);
	return p;
}
