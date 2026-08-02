/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "doc.h"

int
doc_push(struct doc *d, const char *text, size_t len)
{
	void	*t;
	size_t	 newc;

	if (d->acap - d->alen < len) {
		do {
			if ((newc = d->acap * 2) == 0)
				newc = 4096;
		} while (newc - d->alen < len);

		if ((t = realloc(d->arena, newc)) == NULL)
			return (-1);

		d->arena = t;
		d->acap = newc;
	}

	memcpy(d->arena + d->alen, text, len);
	d->alen += len;
	return (d->alen - len);
}

static int
push_node(struct doc *d, enum nodetype type)
{
	struct node	*n;
	void		*t;
	size_t		 newc;
	int		 id;

	if (d->nlen == INT_MAX - 1)	/* ETOOMANY */
		return (-1);

	if (d->nlen == d->ncap) {
		if ((newc = d->ncap * 2) == 0)
			newc = 16;

		if ((t = reallocarray(d->nodes, newc, sizeof(*d->nodes))) == NULL)
			return (-1);

		d->nodes = t;
		d->ncap = newc;
	}

	id = d->nlen++;
	n = &d->nodes[id];
	memset(n, 0, sizeof(*n));
	n->type = type;
	n->parent = d->depth == 0 ? -1 : d->stack[d->depth - 1];
	n->last_descendant = id;

	return (id);
}

int
doc_append(struct doc *d, enum nodetype type)
{
	return (push_node(d, type));
}

int
doc_open(struct doc *d, enum nodetype type)
{
	int	 id;

	if ((id = push_node(d, type)) == -1)
		return (-1);

	if (d->depth == DOC_MAXDEPTH)
		d->overflow++;
	else
		d->stack[d->depth++] = id;

	return (id);
}

int
doc_close(struct doc *d)
{
	int	 id;

	if (d->overflow) {
		d->overflow--;
		return (0);
	}

	if (d->depth == 0)
		return (-1);

	id = d->stack[--d->depth];
	d->nodes[id].last_descendant = d->nlen - 1;
	return (0);
}

void
doc_close_all(struct doc *d)
{
	while (d->depth || d->overflow)
		doc_close(d);
}

void
doc_free(struct doc *d)
{
	free(d->arena);
	free(d->nodes);

	memset(d, 0, sizeof(*d));
}
