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

#include <netdb.h>
#include <stddef.h>

#define GEMINI_URL_LEN 1024

/* +1 for NUL */
struct url {
	char	scheme[32];
	char	host[NI_MAXHOST+1];
	char	port[NI_MAXSERV+1];
	char	path[GEMINI_URL_LEN+1];
	char	query[GEMINI_URL_LEN+1];
	char	fragment[GEMINI_URL_LEN+1];
};

int		 url_parse(const char*, struct url*, const char**);
int		 url_resolve_from(struct url*, const char*, const char**);
int		 url_set_query(struct url*, const char*);
void		 url_unparse(struct url*, char*, size_t);
