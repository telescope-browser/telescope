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
#include "mcache.h"
#include "parser.h"
#include "utils.h"

static const char *gemtext_prefixes[] = {
	[LINE_TEXT] = "",
	[LINE_TITLE_1] = "# ",
	[LINE_TITLE_2] = "## ",
	[LINE_TITLE_3] = "### ",
	[LINE_ITEM] = "* ",
	[LINE_QUOTE] = "> ",
	[LINE_PRE_START] = "``` ",
	[LINE_PRE_CONTENT] = "",
	[LINE_PRE_END] = "```",
};

static struct timeval tv = { 5 * 60, 0 };
static struct event timerev;

static struct ohash	h;
static size_t		npages;
static size_t		tot;

struct mcache_entry {
	time_t		 ts;
	const char	*parser_name;
	int		 trust;
	struct evbuffer	*evb;
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
	tot -= EVBUFFER_LENGTH(e->evb);

	evbuffer_free(e->evb);
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
	struct line		*line;
	unsigned int		 slot;
	size_t			 l, len;
	const char		*url;

	url = tab->hist_cur->h;
	l = strlen(url);
	len = sizeof(*e) + l + 1;

	if ((e = calloc(1, len)) == NULL)
		return -1;
	e->ts = time(NULL);
	e->parser_name = tab->buffer.page.name;
	e->trust = tab->trust;
	memcpy(e->url, url, l);

	if ((e->evb = evbuffer_new()) == NULL)
		goto err;

	TAILQ_FOREACH(line, &tab->buffer.page.head, lines) {
		const char	*text, *alt;
		int		 r;

		if ((text = line->line) == NULL)
			text = "";

		if ((alt = line->alt) == NULL)
			alt = "";

		switch (line->type) {
		case LINE_TEXT:
		case LINE_TITLE_1:
		case LINE_TITLE_2:
		case LINE_TITLE_3:
		case LINE_ITEM:
		case LINE_QUOTE:
		case LINE_PRE_START:
		case LINE_PRE_CONTENT:
		case LINE_PRE_END:
			r = evbuffer_add_printf(e->evb, "%s%s\n",
			    gemtext_prefixes[line->type], text);
			break;

		case LINE_LINK:
			r = evbuffer_add_printf(e->evb, "=> %s %s\n",
			    alt, text);
			break;

		case LINE_PATCH:
		case LINE_PATCH_HDR:
		case LINE_PATCH_HUNK_HDR:
		case LINE_PATCH_ADD:
		case LINE_PATCH_DEL:
			/* TODO */
			r = -1;
			break;

		case LINE_COMPL:
		case LINE_COMPL_CURRENT:
		case LINE_HELP:
		case LINE_DOWNLOAD:
		case LINE_DOWNLOAD_DONE:
		case LINE_DOWNLOAD_INFO:
		case LINE_FRINGE:
			/* not reached */
			abort();
		}

		if (r == -1)
			goto err;
	}

	/* free any previously cached copies of this page */
	mcache_free_entry(url);

	slot = ohash_qlookup(&h, url);
	ohash_insert(&h, slot, e);

	npages++;
	tot += EVBUFFER_LENGTH(e->evb);

	if (!evtimer_pending(&timerev, NULL))
		evtimer_add(&timerev, &tv);

	return 0;

err:
	if (e->evb != NULL)
		evbuffer_free(e->evb);
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

	parser_init(tab, gemtext_initparser);
	if (!parser_parse(tab, EVBUFFER_DATA(e->evb), EVBUFFER_LENGTH(e->evb)))
		goto err;
	if (!parser_free(tab))
		goto err;

	tab->buffer.page.name = e->parser_name;
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