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

#ifndef MINIBUFFER_H
#define MINIBUFFER_H

#include "telescope.h"

/* need to be true-ish */
#define MB_READ		1
#define MB_COMPREAD	2

typedef char *(complfn)(void *);

void	 enter_minibuffer(void(*)(void), void(*)(void), void(*)(void),
    struct histhead *,
    complfn *, void *);

void	 exit_minibuffer(void);
void	 yornp(const char *, void (*)(int, struct tab *), struct tab *);

/*
 * completing_read asks the user for something using the minibuffer.
 * The first argument is the string prompt.  The second and third are
 * the callback to call when done and the data; the callback function
 * can't be NULL.  The last two arguments are the completion function
 * and its data; if not given, no completion will be shown.  The
 * function providing the completion will be called asynchronously.
 */
void	 completing_read(const char *,
    void (*)(const char *, struct tab *), struct tab *,
    complfn *, void *);

#endif
