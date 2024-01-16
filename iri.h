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

#ifndef IRI_H
#define IRI_H

struct iri {
	char		iri_scheme[32];
	char		iri_uinfo[254];
	char		iri_host[1024];
	char		iri_portstr[6];
	uint16_t	iri_port;
	char		iri_path[1024];
	char		iri_query[1024];
	char		iri_fragment[1024];

#define IH_SCHEME	0x01
#define IH_UINFO	0x02
#define IH_HOST		0x04
#define IH_PORT		0x08
#define IH_AUTHORITY	(IH_UINFO|IH_HOST|IH_PORT)
#define IH_PATH		0x10
#define IH_QUERY	0x20
#define IH_FRAGMENT	0x40
	int		iri_flags;
};

int	iri_parse(const char *, const char *, struct iri *);
int	iri_unparse(const struct iri *, char *, size_t);
int	iri_human(const struct iri *, char *, size_t);
int	iri_setport(struct iri *, const char *);
int	iri_setquery(struct iri *, const char *);

#endif /* IRI_H */
