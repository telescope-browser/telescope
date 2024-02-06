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

#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdint.h>

struct buffer;

struct kmap {
	TAILQ_HEAD(map, keymap)	m;
	void			(*unhandled_input)(void);
};
extern struct kmap global_map, minibuffer_map;

typedef void(interactivefn)(struct buffer *);

struct keymap {
	int			 meta;
	int			 key;
	struct kmap		 map;
	interactivefn		 *fn;

	TAILQ_ENTRY(keymap)	 keymaps;
};

struct thiskey {
	short meta;
	int key;
	uint32_t cp;
};

enum {
	LK_ADVANCED_MAP,
	LK_MATCHED,
	LK_UNBOUND,
};

int		 kbd(const char *);
const char	*unkbd(int);
int		 kmap_define_key(struct kmap *, const char *, interactivefn *);
int		 lookup_key(struct kmap **, struct thiskey *, struct buffer *);

#endif
