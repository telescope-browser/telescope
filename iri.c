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

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "iri.h"

/* TODO: URI -> IRI.  accept IRI but emit always URI */

static inline int
cpstr(const char *start, const char *till, char *buf, size_t len)
{
	size_t		slen = till - start;

	if (slen + 1 >= len)
		return (-1);
	memcpy(buf, start, slen);
	buf[slen] = '\0';
	return (0);
}

static inline int
unreserved(int c)
{
	return (isalnum((unsigned char)c) ||
	    c == '-' ||
	    c == '.' ||
	    c == '_' ||
	    c == '~');
}

static inline int
pctenc(const char *s)
{
	const char	*t = s;

	return (t[0] == '%' &&
	    isxdigit((unsigned char)t[1]) &&
	    isxdigit((unsigned char)t[2]));
}

static inline int
sub_delims(int c)
{
	return (c == '!' || c == '$' || c == '&' || c == '\'' ||
	    c == '(' || c == ')' || c == '*' || c == '+' || c == ',' ||
	    c == ';' || c == '=');
}

static inline const char *
advance_pchar(const char *s)
{
	if (unreserved(*s) || sub_delims(*s) || *s == ':' || *s == '@')
		return (s + 1);
	if (pctenc(s))
		return (s + 3);
	return (NULL);
}

static inline const char *
advance_segment(const char *s)
{
	const char	*t = s;

	while ((t = advance_pchar(s)) != NULL)
		s = t;
	return (s);
}

static inline const char *
advance_segment_nz(const char *s)
{
	const char	*t;

	if ((t = advance_pchar(s)) == NULL)
		return (NULL);
	return (advance_segment(t));
}

static inline const char *
advance_segment_nz_nc(const char *s)
{
	const char	*t = s;

	for (;;) {
		if (unreserved(*t) || sub_delims(*t) || *t == '@')
			t++;
		else if (pctenc(t))
			t += 3;
		else
			break;
	}

	return (t != s ? t : NULL);
}

static const char *
parse_scheme(const char *s, struct iri *iri)
{
	const char	*t = s;

	if (!isalpha((unsigned char)*t))
		return (NULL);

	while (isalnum((unsigned char)*t) ||
	    *t == '+' ||
	    *t == '-' ||
	    *t == '.')
		t++;

	if (cpstr(s, t, iri->iri_scheme, sizeof(iri->iri_scheme)) == -1)
		return (NULL);

	iri->iri_flags |= IH_SCHEME;
	return (t);
}

/* userinfo is always optional */
static const char *
parse_uinfo(const char *s, struct iri *iri)
{
	const char	*t = s;

	for (;;) {
		if (unreserved(*t) || sub_delims(*t) || *t == ':')
			t++;
		else if (pctenc(t))
			t += 3;
		else
			break;
	}

	if (*t != '@')
		return (s);

	if (cpstr(s, t, iri->iri_uinfo, sizeof(iri->iri_uinfo)) == -1)
		return (NULL);
	iri->iri_flags |= IH_UINFO;
	return (t + 1);
}

static const char *
parse_host(const char *s, struct iri *iri)
{
	const char	*t = s;

	/*
	 * cheating a bit by relaxing and merging the rule for
	 * IPv6address and IPvFuture and by merging IPv4address and
	 * reg-name.
	 */

	if (*t == '[') {
		while (*t && *t != ']')
			++t;
		if (*t == '\0')
			return (NULL);
		t++;
		if (cpstr(s, t, iri->iri_host, sizeof(iri->iri_host)) == -1)
			return (NULL);
		iri->iri_flags |= IH_HOST;
		return (t);
	}

	for (;;) {
		if (unreserved(*t) || sub_delims(*t))
			t++;
		else if (pctenc(t))
			t += 3;
		else
			break;
	}

	if (cpstr(s, t, iri->iri_host, sizeof(iri->iri_host)) == -1)
		return (NULL);
	iri->iri_flags |= IH_HOST;
	return (t);
}

static const char *
parse_port(const char *s, struct iri *iri)
{
	const char	*t = s;
	const char	*errstr;

	while (isdigit((unsigned char)*t))
		t++;
	if (cpstr(s, t, iri->iri_portstr, sizeof(iri->iri_portstr)) == -1)
		return (NULL);
	iri->iri_port = strtonum(iri->iri_portstr, 1, UINT16_MAX, &errstr);
	if (errstr)
		return (NULL);
	iri->iri_flags |= IH_PORT;
	return (t);
}

static const char *
parse_authority(const char *s, struct iri *iri)
{
	const char	*t;

	if ((t = parse_uinfo(s, iri)) == NULL)
		return (NULL);

	if ((t = parse_host(t, iri)) == NULL)
		return (NULL);

	if (*t == ':')
		return (parse_port(t, iri));

	return (t);
}

static const char *
parse_path_abempty(const char *s, struct iri *iri)
{
	const char	*t = s;

	while (*t == '/')
		t = advance_segment(t + 1);

	if (cpstr(s, t, iri->iri_path, sizeof(iri->iri_path)) == -1)
		return (NULL);
	iri->iri_flags |= IH_PATH;
	return (t);
}

static const char *
parse_path_absolute(const char *s, struct iri *iri)
{
	const char	*t;

	if (*s != '/')
		return (NULL);

	if ((t = advance_segment_nz(s + 1)) == NULL)
		return (s + 1);

	while (*t == '/')
		t = advance_segment(t + 1);

	if (cpstr(s, t, iri->iri_path, sizeof(iri->iri_path)) == -1)
		return (NULL);
	iri->iri_flags |= IH_PATH;
	return (t);
}

static const char *
parse_path_rootless(const char *s, struct iri *iri)
{
	const char	*t;

	if ((t = advance_segment_nz(s)) == NULL)
		return (NULL);

	while (*t == '/')
		t = advance_segment(t + 1);

	if (cpstr(s, t, iri->iri_path, sizeof(iri->iri_path)) == -1)
		return (NULL);
	iri->iri_flags |= IH_PATH;
	return (t);
}

static const char *
parse_path_noscheme(const char *s, struct iri *iri)
{
	const char	*t;

	if ((t = advance_segment_nz_nc(s)) == NULL)
		return (NULL);

	while (*t == '/')
		t = advance_segment(t + 1);

	if (cpstr(s, t, iri->iri_path, sizeof(iri->iri_path)) == -1)
		return (NULL);
	iri->iri_flags |= IH_PATH;
	return (t);
}

static const char *
parse_path_empty(const char *s, struct iri *iri)
{
	iri->iri_path[0] = '\0';
	iri->iri_flags |= IH_PATH;
	return (s);
}

static const char *
parse_hier(const char *s, struct iri *iri)
{
	const char	*t;

	if (!strncmp(s, "//", 2)) {
		if ((t = parse_authority(s + 2, iri)) == NULL)
			return (NULL);
		return (parse_path_abempty(t, iri));
	}

	if ((t = parse_path_absolute(s, iri)) != NULL)
		return (t);

	if ((t = parse_path_rootless(s, iri)) != NULL)
		return (t);

	return (parse_path_empty(s, iri));
}

static const char *
parse_relative(const char *s, struct iri *iri)
{
	const char	*t = s;

	if (!strncmp(s, "//", 2)) {
		if ((t = parse_authority(s + 2, iri)) == NULL)
			return (NULL);
		return (parse_path_abempty(t, iri));
	}

	if ((t = parse_path_absolute(s, iri)) != NULL)
		return (t);

	if ((t = parse_path_noscheme(s, iri)) != NULL)
		return (t);

	return (parse_path_empty(s, iri));
}

static const char *
parse_query(const char *s, struct iri *iri)
{
	const char	*n, *t = s;

	for (;;) {
		if ((n = advance_pchar(t)) != NULL)
			t = n;
		else if (*t == '/' || *t == '?')
			t++;
		else
			break;
	}

	if (cpstr(s, t, iri->iri_query, sizeof(iri->iri_query)) == -1)
		return (NULL);
	iri->iri_flags |= IH_QUERY;
	return (t);
}

static int
parse_uri(const char *s, struct iri *iri)
{
	if ((s = parse_scheme(s, iri)) == NULL)
		return (-1);

	if (*s != ':')
		return (-1);

	if ((s = parse_hier(s + 1, iri)) == NULL)
		return (-1);

	if (*s == '?' && (s = parse_query(s + 1, iri)) == NULL)
		return (-1);

	/* skip fragments */
	if (*s == '#' || *s == '\0')
		return (0);

	return (-1);
}

static int
parse_relative_ref(const char *s, struct iri *iri)
{
	if ((s = parse_relative(s, iri)) == NULL)
		return (-1);

	if (*s == '?' && (s = parse_query(s + 1, iri)) == NULL)
		return (-1);

	/* skip fragments */
	if (*s == '#' || *s == '\0')
		return (0);

	return (-1);
}

static int
parse(const char *s, struct iri *iri)
{
	iri->iri_flags = 0;

	if (s == NULL)
		return (0);

	if (parse_uri(s, iri) == -1) {
		iri->iri_flags = 0;
		if (parse_relative_ref(s, iri) == -1)
			return (-1);
	}

	return (0);
}

static inline void
lowerify(char *s)
{
	for (; *s; ++s)
		*s = tolower((unsigned char)*s);
}

static void
cpfields(struct iri *dest, const struct iri *src, int flags)
{
	if (flags & IH_SCHEME) {
		dest->iri_flags |= IH_SCHEME;
		if (src->iri_flags & IH_SCHEME)
			memcpy(dest->iri_scheme, src->iri_scheme,
			    sizeof(dest->iri_scheme));
		lowerify(dest->iri_scheme);
	}
	if (flags & IH_UINFO) {
		if (src->iri_flags & IH_UINFO) {
			memcpy(dest->iri_uinfo, src->iri_uinfo,
			    sizeof(dest->iri_uinfo));
			dest->iri_flags |= IH_UINFO;
		}
	}
	if (flags & IH_HOST) {
		dest->iri_flags |= IH_HOST;
		if (src->iri_flags & IH_HOST)
			memcpy(dest->iri_host, src->iri_host,
			    sizeof(dest->iri_host));
		lowerify(dest->iri_host);
	}
	if (flags & IH_PORT) {
		if (src->iri_flags & IH_PORT) {
			dest->iri_port = src->iri_port;
			dest->iri_flags |= IH_PORT;
		}
	}
	if (flags & IH_PATH) {
		dest->iri_flags |= IH_PATH;
		if (src->iri_flags & IH_PATH)
			memcpy(dest->iri_path, src->iri_path,
			    sizeof(dest->iri_path));
	}
	if (flags & IH_QUERY) {
		if (src->iri_flags & IH_QUERY) {
			dest->iri_flags |= IH_QUERY;
			memcpy(dest->iri_query, src->iri_query,
			    sizeof(dest->iri_query));
		}
	}
}

static inline int
remove_dot_segments(struct iri *iri)
{
	char		*p, *q, *buf, *s;
	ptrdiff_t	 bufsize;

	buf = p = q = iri->iri_path;
	bufsize = sizeof(iri->iri_path);

	while (*p && (q - buf < bufsize)) {
		/* A */
		if (!strncmp(p, "../", 3)) {
			p += 3;
			continue;
		}
		if (!strncmp(p, "./", 2)) {
			p += 2;
			continue;
		}
		/* B */
		if (!strncmp(p, "/./", 3)) {
			*q++ = '/';
			p += 3;
			continue;
		}
		if (!strcmp(p, "/.")) {
			*q++ = '/';
			p += 2;
			break;
		}
		/* C */
		if (p[0] == '/' && p[1] == '.' && p[2] == '.' &&
		    (p[3] == '/' || p[3] == '\0')) {
			p += 3;
			while (q != buf && *--q != '/')
				continue;
			*q++ = '/';
			continue;
		}
		/* D */
		if (!strcmp(p, ".")) {
			p++;
			break;
		}
		if (!strcmp(p, "..")) {
			p += 2;
			break;
		}
		/* E */
		s = strchr(p + 1, '/');
		while (*p && p != s && (q - buf < bufsize))
			*q++ = *p++;
	}

	if (*p == '\0' && (q - buf < bufsize)) {
		*q = '\0';
		return (0);
	}

	errno = ENAMETOOLONG;
	return (-1);
}

static inline int
mergepath(struct iri *i, struct iri *base, struct iri *r)
{
	const char	*bpath, *rpath, *s;

	bpath = (base->iri_flags & IH_PATH) ? base->iri_path : "/";
	rpath = (r->iri_flags & IH_PATH) ? r->iri_path : "/";

	i->iri_flags |= IH_PATH;
	i->iri_path[0] = '\0';

	if ((base->iri_flags & IH_AUTHORITY) &&
	    (*bpath == '\0' || !strcmp(bpath, "/"))) {
		if (*rpath == '/')
			rpath++;
		strlcpy(i->iri_path, "/", sizeof(i->iri_path));
		strlcat(i->iri_path, rpath, sizeof(i->iri_path));
		return (0);
	}

	if ((s = strrchr(bpath, '/')) != NULL) {
		cpstr(bpath, s + 1, i->iri_path, sizeof(i->iri_path));
		if (*rpath == '/')
			rpath++;
	}
	if (strlcat(i->iri_path, rpath, sizeof(i->iri_path)) >=
	    sizeof(i->iri_path)) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	return (0);
}

int
iri_parse(const char *base, const char *str, struct iri *iri)
{
	static struct iri	ibase, iparsed;

	memset(iri, 0, sizeof(*iri));

	if (base == NULL) {
		ibase.iri_flags = 0;
		if (parse_uri(str, &iparsed) == -1) {
			errno = EINVAL;
			return (-1);
		}
	} else {
		if (parse_uri(base, &ibase) == -1 ||
		    parse(str, &iparsed) == -1) {
			errno = EINVAL;
			return (-1);
		}
	}

	if (iparsed.iri_flags & IH_SCHEME) {
		cpfields(iri, &iparsed, iparsed.iri_flags);
		remove_dot_segments(iri);
		return (0);
	}

	/* if fragments are supported, copy iparsed fragment to iri */

	cpfields(iri, &ibase, IH_SCHEME);

	if (iparsed.iri_flags & IH_HOST) {
		cpfields(iri, &iparsed, IH_AUTHORITY|IH_PATH|IH_QUERY);
		remove_dot_segments(iri);
		return (0);
	}

	cpfields(iri, &ibase, IH_AUTHORITY);

	if ((iparsed.iri_flags & IH_PATH) && *iparsed.iri_path == '\0') {
		cpfields(iri, &ibase, IH_PATH);
		if (iparsed.iri_flags & IH_QUERY)
			cpfields(iri, &iparsed, IH_QUERY);
		else
			cpfields(iri, &ibase, IH_QUERY);
		return (0);
	}

	cpfields(iri, &iparsed, IH_QUERY);
	if ((iparsed.iri_flags & IH_PATH) && *iparsed.iri_path == '/')
		cpfields(iri, &iparsed, IH_PATH);
	else {
		if (!(ibase.iri_flags & IH_PATH))
			ibase.iri_path[0] = '\0';
		if (!(iparsed.iri_flags & IH_PATH))
			iparsed.iri_path[0] = '\0';
		if (mergepath(iri, &ibase, &iparsed) == -1)
			return (-1);
	}
	if (remove_dot_segments(iri) == -1)
		return (-1);
	return (0);
}

int
iri_unparse(const struct iri *i, char *buf, size_t buflen)
{
	if (buflen == 0)
		goto err;

	/* TODO: should %enc octets if needed */

	buf[0] = '\0';

	if (i->iri_flags & IH_SCHEME) {
		if (strlcat(buf, i->iri_scheme, buflen) >= buflen ||
		    strlcat(buf, ":", buflen) >= buflen)
			goto err;
	}

	if (i->iri_flags & IH_AUTHORITY) {
		if (strlcat(buf, "//", buflen) >= buflen)
			goto err;
	}

	if (i->iri_flags & IH_UINFO) {
		if (strlcat(buf, i->iri_uinfo, buflen) >= buflen ||
		    strlcat(buf, "@", buflen) >= buflen)
			goto err;
	}
	if (i->iri_flags & IH_HOST) {
		if (strlcat(buf, i->iri_host, buflen) >= buflen)
			goto err;
	}
	if (i->iri_flags & IH_PORT) {
		if (strlcat(buf, ":", buflen) >= buflen ||
		    strlcat(buf, i->iri_portstr, buflen) >= buflen)
			goto err;
	}

	if (i->iri_flags & IH_PATH) {
		if (i->iri_flags & IH_AUTHORITY &&
		    i->iri_path[0] != '/' &&
		    strlcat(buf, "/", buflen) >= buflen)
			goto err;
		if (strlcat(buf, i->iri_path, buflen) >= buflen)
			goto err;
	}

	if (i->iri_flags & IH_QUERY) {
		if (strlcat(buf, "?", buflen) >= buflen ||
		    strlcat(buf, i->iri_query, buflen) >= buflen)
			goto err;
	}

	return (0);

 err:
	errno = ENOBUFS;
	return (-1);
}

int
iri_human(const struct iri *iri, char *buf, size_t buflen)
{
	memset(buf, 0, buflen);
	return (-1);
}

int
iri_setquery(struct iri *iri, const char *text)
{
	return (-1);
}
