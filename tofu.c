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

static void	*hash_alloc(size_t, void*);
static void	*hash_calloc(size_t, size_t, void*);
static void	 hash_free(void*, void*);

static void *
hash_alloc(size_t len, void *d)
{
	if ((d = malloc(len)) == NULL)
		abort();
	return d;
}

static void *
hash_calloc(size_t nmemb, size_t size, void *d)
{
	if ((d = calloc(nmemb, size)) == NULL)
		abort();
	return d;
}

static void
hash_free(void *ptr, void *d)
{
	free(ptr);
}

void
tofu_init(struct ohash *h, unsigned int sz, ptrdiff_t ko)
{
	struct ohash_info info = {
		.key_offset = ko,
		.calloc = hash_calloc,
		.free = hash_free,
		.alloc = hash_alloc,
	};

	ohash_init(h, sz, &info);
}

struct tofu_entry *
tofu_lookup(struct ohash *h, const char *domain, const char *port)
{
	char		buf[GEMINI_URL_LEN];
	unsigned int	slot;

	strlcpy(buf, domain, sizeof(buf));
	if (port != NULL && *port != '\0' && strcmp(port, "1965")) {
		strlcat(buf, ":", sizeof(buf));
		strlcat(buf, port, sizeof(buf));
	}

	slot = ohash_qlookup(h, buf);
	return ohash_find(h, slot);
}

void
tofu_add(struct ohash *h, struct tofu_entry *e)
{
	unsigned int	slot;

	slot = ohash_qlookup(h, e->domain);
	ohash_insert(h, slot, e);
}

void
tofu_update(struct ohash *h, struct tofu_entry *e)
{
	struct tofu_entry *t;

	if ((t = tofu_lookup(h, e->domain, NULL)) == NULL)
		tofu_add(h, e);
	else {
		strlcpy(t->hash, e->hash, sizeof(t->hash));
		t->verified = e->verified;
		free(e);
	}
}
