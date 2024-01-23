/*
 * Copyright (c) 2022 Omar Polo <op@omarpolo.com>
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
#include <stdint.h>
#include <string.h>

#include "telescope.h"
#include "hist.h"
#include "mcache.h"
#include "parser.h"
#include "utils.h"

static struct timeval tv = { 5 * 60, 0 };
static struct event timerev;

static struct ohash	h;
static size_t		npages;
static size_t		tot;

struct mcache_entry {
	time_t		 ts;
	parserfn	 parser;
	int		 trust;
	char		*buf;
	size_t		 buflen;
	char		 url[];
};

static void
mcache_free_entry(const char *url)
{
	struct mcache_entry	*e;
	unsigned int		 slot;

	slot = ohash_qlookup(&h, url);
	if ((e = ohash_remove(&h, slot)) == NULL)
		return;

	npages--;
	tot -= e->buflen;

	free(e->buf);
	free(e);
}

static void
clean_old_entries(int fd, short ev, void *data)
{
	struct mcache_entry	*e;
	unsigned int		 i;
	time_t			 treshold;

	/* delete pages older than an hour */
	treshold = time(NULL) - 60 * 60;

	for (e = ohash_first(&h, &i); e != NULL; e = ohash_next(&h, &i))
		if (e->ts < treshold)
			mcache_free_entry(e->url);

	evtimer_add(&timerev, &tv);
}

void
mcache_init(void)
{
	struct ohash_info info = {
		.key_offset = offsetof(struct mcache_entry, url),
		.calloc = hash_calloc,
		.free = hash_free,
		.alloc = hash_alloc,
	};

	ohash_init(&h, 5, &info);

	evtimer_set(&timerev, clean_old_entries, NULL);
}

int
mcache_tab(struct tab *tab)
{
	struct mcache_entry	*e;
	unsigned int		 slot;
	size_t			 l, len;
	const char		*url;
	FILE			*fp;

	url = hist_cur(tab->hist);
	l = strlen(url);
	len = sizeof(*e) + l + 1;

	if ((e = calloc(1, len)) == NULL)
		return -1;
	e->ts = time(NULL);
	e->parser = tab->buffer.page.init;
	e->trust = tab->trust;
	memcpy(e->url, url, l);

	if ((fp = open_memstream(&e->buf, &e->buflen)) == NULL)
		goto err;

	if (!parser_serialize(tab, fp))
		goto err;

	fclose(fp);

	/* free any previously cached copies of this page */
	mcache_free_entry(url);

	slot = ohash_qlookup(&h, url);
	ohash_insert(&h, slot, e);

	npages++;
	tot += e->buflen;

	if (!evtimer_pending(&timerev, NULL))
		evtimer_add(&timerev, &tv);

	return 0;

err:
	if (fp != NULL)
		fclose(fp);
	if (e->buf != NULL)
		free(e->buf);
	free(e);
	return -1;
}

int
mcache_lookup(const char *url, struct tab *tab)
{
	struct mcache_entry	*e;
	unsigned int		 slot;

	slot = ohash_qlookup(&h, url);
	if ((e = ohash_find(&h, slot)) == NULL)
		return 0;

	parser_init(tab, e->parser);
	if (!parser_parse(tab, e->buf, e->buflen))
		goto err;
	if (!parser_free(tab))
		goto err;

	tab->trust = e->trust;
	return 1;

err:
	parser_free(tab);
	erase_buffer(&tab->buffer);
	return 0;
}

void
mcache_info(size_t *r_npages, size_t *r_tot)
{
	*r_npages = npages;
	*r_tot = tot;
}
