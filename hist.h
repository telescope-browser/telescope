/*
 * Copyright (c) 2021, 2024 Omar Polo <op@omarpolo.com>
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

struct hist;

#define HIST_LINEAR	0x0
#define HIST_WRAP	0x1

struct hist	*hist_new(int);
void		 hist_free(struct hist *);
void		 hist_erase(struct hist *);

size_t		 hist_size(struct hist *);
size_t		 hist_off(struct hist *);

const char	*hist_cur(struct hist *);
int		 hist_cur_offs(struct hist *, size_t *, size_t *);

int		 hist_set_cur(struct hist *, const char *);
int		 hist_set_offs(struct hist *, size_t, size_t);

const char	*hist_nth(struct hist *, size_t);
const char	*hist_prev(struct hist *);
const char	*hist_next(struct hist *);

void		 hist_seek_start(struct hist *);

int		 hist_push(struct hist *, const char *);
int		 hist_prepend(struct hist *, const char *);
int		 hist_append(struct hist *, const char *);

