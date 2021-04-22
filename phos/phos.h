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

/* for the time being, drop everything but the URL stuff */

#ifndef PHOS_H
#define PHOS_H

#ifdef __cplusplus
#extern "C" {
#endif

#include <stdint.h>

#define PHOS_URL_MAX_LEN 1024

struct phos_uri {
	char		scheme[32];
	char		host[1024];
	char		port[6];
	uint16_t	dec_port;
	char		path[1024];
	char		query[1024];
	char		fragment[32];
};

/* phos_uri.c */
int	 phos_parse_uri_reference(const char*, struct phos_uri*);
int	 phos_parse_absolute_uri(const char*, struct phos_uri*);
int	 phos_resolve_uri_from_str(const struct phos_uri*, const char *, struct phos_uri*);
void	 phos_uri_drop_empty_segments(struct phos_uri*);
int	 phos_serialize_uri(const struct phos_uri*, char*, size_t);

#ifdef __cplusplus
}
#endif

#endif	/* PHOS_H */
