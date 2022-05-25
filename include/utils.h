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

#ifndef UTILS_H
#define UTILS_H

int		 mark_nonblock_cloexec(int);

int		 has_suffix(const char *, const char *);
int		 unicode_isspace(uint32_t);
int		 unicode_isgraph(uint32_t);

void		 imsg_event_add(struct imsgev *);
int		 dispatch_imsg(struct imsgev *, short, imsg_handlerfn **, size_t);
int		 imsg_compose_event(struct imsgev *, uint16_t, uint32_t, pid_t, int, const void *, uint16_t);

void		*hash_alloc(size_t, void *);
void		*hash_calloc(size_t, size_t, void *);
void		 hash_free(void *, void *);

#endif /* UTILS_H */
