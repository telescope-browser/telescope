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

static struct keytable {
	const char	*p;
	int		 k;
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
	{ "<f0>",	KEY_F(0) },
	{ "<f1>",	KEY_F(1) },
	{ "<f2>",	KEY_F(2) },
	{ "<f3>",	KEY_F(3) },
	{ "<f4>",	KEY_F(4) },
	{ "<f5>",	KEY_F(5) },
	{ "<f6>",	KEY_F(6) },
	{ "<f7>",	KEY_F(7) },
	{ "<f8>",	KEY_F(8) },
	{ "<f9>",	KEY_F(9) },
	{ "<f10>",	KEY_F(10) },
	{ "<f11>",	KEY_F(11) },
	{ "<f12>",	KEY_F(12) },
	{ "<f13>",	KEY_F(13) },
	{ "<f14>",	KEY_F(14) },
	{ "<f15>",	KEY_F(15) },
	{ "<f16>",	KEY_F(16) },
	{ "<f17>",	KEY_F(17) },
	{ "<f18>",	KEY_F(18) },
	{ "<f19>",	KEY_F(19) },
	{ "<f20>",	KEY_F(20) },
	{ "<f21>",	KEY_F(21) },
	{ "<f22>",	KEY_F(22) },
	{ "<f23>",	KEY_F(23) },
	{ "<f24>",	KEY_F(24) },
	{ "<f25>",	KEY_F(25) },
	{ "<f26>",	KEY_F(26) },
	{ "<f27>",	KEY_F(27) },
	{ "<f28>",	KEY_F(28) },
	{ "<f29>",	KEY_F(29) },
	{ "<f30>",	KEY_F(30) },
	{ "<f31>",	KEY_F(31) },
	{ "<f32>",	KEY_F(32) },
	{ "<f33>",	KEY_F(33) },
	{ "<f34>",	KEY_F(34) },
	{ "<f35>",	KEY_F(35) },
	{ "<f36>",	KEY_F(36) },
	{ "<f37>",	KEY_F(37) },
	{ "<f38>",	KEY_F(38) },
	{ "<f39>",	KEY_F(39) },
	{ "<f40>",	KEY_F(40) },
	{ "<f41>",	KEY_F(41) },
	{ "<f42>",	KEY_F(42) },
	{ "<f43>",	KEY_F(43) },
	{ "<f44>",	KEY_F(44) },
	{ "<f45>",	KEY_F(45) },
	{ "<f46>",	KEY_F(46) },
	{ "<f47>",	KEY_F(47) },
	{ "<f48>",	KEY_F(48) },
	{ "<f49>",	KEY_F(49) },
	{ "<f50>",	KEY_F(50) },
	{ "<f51>",	KEY_F(51) },
	{ "<f52>",	KEY_F(52) },
	{ "<f53>",	KEY_F(53) },
	{ "<f54>",	KEY_F(54) },
	{ "<f55>",	KEY_F(55) },
	{ "<f56>",	KEY_F(56) },
	{ "<f57>",	KEY_F(57) },
	{ "<f58>",	KEY_F(58) },
	{ "<f59>",	KEY_F(59) },
	{ "<f60>",	KEY_F(60) },
	{ "<f61>",	KEY_F(61) },
	{ "<f62>",	KEY_F(62) },
	{ "<f63>",	KEY_F(63) },
	/* ... */
	{ "del",	KEY_BACKSPACE },
	{ "backspace",	127 },
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
kmap_define_key(struct kmap *map, const char *key, void (*fn)(struct window*))
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

