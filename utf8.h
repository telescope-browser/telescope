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

#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>
#include <stdint.h>

/* utf8.c */
uint32_t	 utf8_decode(uint32_t*restrict, uint32_t*restrict, uint8_t);
size_t		 utf8_encode(uint32_t, char*);
size_t		 utf8_chwidth(uint32_t);
size_t		 utf8_snwidth(const char*, size_t);
size_t		 utf8_swidth(const char*);
size_t		 utf8_swidth_between(const char*, const char*);
int		 emojied_line(const char *, const char **);

/* emoji-matcher.c */
int		 is_emoji(uint32_t);

#endif
