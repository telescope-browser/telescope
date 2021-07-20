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

#ifndef PAGES_H
#define PAGES_H

#include <stddef.h>
#include <stdint.h>

extern const uint8_t	 about_about[];
extern size_t		 about_about_len;

extern const uint8_t	 about_blank[];
extern size_t		 about_blank_len;

extern const uint8_t	 about_crash[];
extern size_t		 about_crash_len;

extern const uint8_t	 about_help[];
extern size_t		 about_help_len;

extern const uint8_t	 about_license[];
extern size_t		 about_license_len;

extern const uint8_t	 about_new[];
extern size_t		 about_new_len;

#endif
