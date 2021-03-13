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

#include <ctype.h>
#include <curses.h>
#include <stdlib.h>

#define CTRL(n)	((n)&0x1F)

struct keytable {
	char	*p;
	int	 k;
} keytable[] = {
	{ "<up>",	KEY_UP },
	{ "<down>",	KEY_DOWN },
	{ "<left>",	KEY_LEFT },
	{ "<right>",	KEY_RIGHT },
	{ "<prior>",	KEY_PPAGE },
	{ "<next>",	KEY_NPAGE },
	{ "<home>",	KEY_HOME },
	{ "<end>",	KEY_END },
	/* ... */
	{ "del",	KEY_BACKSPACE },
	{ "esc",	27 },
	{ "space",	' ' },
	{ "spc",	' ' },
	{ "enter",	CTRL('m') },
	{ "ret",	CTRL('m' )},
	{ "tab",	CTRL('i') },
	/* ... */
	{ NULL, 0 },
};

int
kbd(const char *key)
{
	struct keytable *t;

	for (t = keytable; t->p != NULL; ++t) {
		if (has_prefix(key, t->p))
			return t->k;
	}

        return *key;
}

const char *
unkbd(int k)
{
	struct keytable *t;

	for (t = keytable; t->p != NULL; ++t) {
		if (k == t->k)
			return t->p;
	}

	return NULL;
}

int
kmap_define_key(struct kmap *map, const char *key, void (*fn)(struct tab*))
{
	int ctrl, meta, k;
	struct keymap	*entry;

again:
	if ((ctrl = has_prefix(key, "C-")))
		key += 2;
	if ((meta = has_prefix(key, "M-")))
		key += 2;
	if (*key == '\0')
                return 0;
	k = kbd(key);

	if (ctrl)
		k = CTRL(k);

	/* skip key & spaces */
	while (*key != '\0' && !isspace(*key))
		++key;
	while (*key != '\0' && isspace(*key))
		++key;

	TAILQ_FOREACH(entry, &map->m, keymaps) {
		if (entry->meta == meta && entry->key == k) {
			if (*key == '\0') {
				entry->fn = fn;
				return 1;
			}
			map = &entry->map;
			goto again;
		}
	}

	if ((entry = calloc(1, sizeof(*entry))) == NULL)
		return 0;

	entry->meta = meta;
	entry->key = k;
	TAILQ_INIT(&entry->map.m);

	if (TAILQ_EMPTY(&map->m))
		TAILQ_INSERT_HEAD(&map->m, entry, keymaps);
	else
		TAILQ_INSERT_TAIL(&map->m, entry, keymaps);

        if (*key != '\0') {
		map = &entry->map;
		goto again;
	}

	entry->fn = fn;

	return 1;
}

