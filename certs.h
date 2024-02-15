/*
 * Copyright (c) 2021, 2023, 2024 Omar Polo <op@omarpolo.com>
 * Copyright (c) 2019 Renaud Allard <renaud@allard.it>
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2008 Reyk Floeter <reyk@openbsd.org>
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

struct iri;

/* client certificate */
struct ccert {
	char	*host;
	char	*port;
	char	*path;
	char	*cert;

#define CERT_OK		0x00
#define CERT_TEMP	0x01
#define CERT_TEMP_DEL	0x02
	int	 flags;
};

struct cstore {
	struct ccert	*certs;
	size_t		 len;
	size_t		 cap;
};

extern struct cstore	 cert_store;
extern char		**identities;

int		 certs_init(const char *);
const char	*ccert(const char *);
const char	*cert_for(struct iri *, int *);
int		 cert_save_for(const char *, struct iri *, int);
int		 cert_delete_for(const char *, struct iri *, int);
int		 cert_open(const char *);
int		 cert_new(const char *, const char *, int);
