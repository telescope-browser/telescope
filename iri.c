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

#include "compat.h"

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
		return (parse_port(t + 1, iri));

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
		t = s + 1;
	else {
		while (*t == '/')
			t = advance_segment(t + 1);
	}

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
parse_qf(const char *s, int flag, struct iri *iri, char *buf, size_t bufsize)
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

	if (cpstr(s, t, buf, bufsize) == -1)
		return (NULL);
	iri->iri_flags |= flag;
	return (t);
}

static int
parse_uri(const char *s, struct iri *iri)
{
	iri->iri_flags = 0;

	if ((s = parse_scheme(s, iri)) == NULL)
		return (-1);

	if (*s != ':')
		return (-1);

	if ((s = parse_hier(s + 1, iri)) == NULL)
		return (-1);

	if (*s == '?') {
		s = parse_qf(s + 1, IH_QUERY, iri, iri->iri_query,
		    sizeof(iri->iri_query));
		if (s == NULL)
			return (-1);
	}

	if (*s == '#') {
		s = parse_qf(s + 1, IH_FRAGMENT, iri, iri->iri_fragment,
		    sizeof(iri->iri_fragment));
		if (s == NULL)
			return (-1);
	}

	if (*s == '\0')
		return (0);

	return (-1);
}

static int
parse_relative_ref(const char *s, struct iri *iri)
{
	if ((s = parse_relative(s, iri)) == NULL)
		return (-1);

	if (*s == '?') {
		s = parse_qf(s + 1, IH_QUERY, iri, iri->iri_query,
		    sizeof(iri->iri_query));
		if (s == NULL)
			return (-1);
	}

	if (*s == '#') {
		s = parse_qf(s + 1, IH_FRAGMENT, iri, iri->iri_fragment,
		    sizeof(iri->iri_fragment));
		if (s == NULL)
			return (-1);
	}

	if (*s == '\0')
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
			memcpy(dest->iri_portstr, src->iri_portstr,
			    sizeof(dest->iri_portstr));
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
	if (flags & IH_FRAGMENT) {
		if (src->iri_flags & IH_FRAGMENT) {
			dest->iri_flags |= IH_FRAGMENT;
			memcpy(dest->iri_fragment, src->iri_fragment,
			    sizeof(dest->iri_fragment));
		}
	}
}

static inline int
remove_dot_segments(char *buf, ptrdiff_t bufsize)
{
	char		*p, *q;

	p = q = buf;
	while (*p && (q - buf < bufsize)) {
		if (p[0] == '/' && p[1] == '.' &&
		    (p[2] == '/' || p[2] == '\0')) {
			p += 2;
			if (*p != '/')
				*q++ = '/';
		} else if (p[0] == '/' && p[1] == '.' && p[2] == '.' &&
		    (p[3] == '/' || p[3] == '\0')) {
			p += 3;
			while (q > buf && *--q != '/')
				continue;
			if (*p != '/' && (q > buf && q[-1] != '/'))
				*q++ = '/';
		} else
			*q++ = *p++;
	}
	if ((*p == '\0') && (q - buf < bufsize)) {
		*q = '\0';
		return (0);
	}

	errno = ENAMETOOLONG;
	return (-1);
}

static inline int
mergepath(char *buf, size_t bufsize, int abs, const char *base, const char *r)
{
	const char	*s;

	if (base == NULL || *base == '\0')
		base = "/";
	if (r == NULL || *r == '\0')
		r = "/";

	if (bufsize == 0)
		return (-1);
	buf[0] = '\0';

	if (abs && (*base == '\0' || !strcmp(base, "/"))) {
		if (*r == '/')
			r++;
		strlcpy(buf, "/", bufsize);
		strlcat(buf, r, bufsize);
		return (0);
	}

	if ((s = strrchr(base, '/')) != NULL) {
		cpstr(base, s + 1, buf, bufsize);
		if (*r == '/')
			r++;
	}
	if (strlcat(buf, r, bufsize) >= bufsize) {
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

	cpfields(iri, &iparsed, IH_FRAGMENT);

	if (iparsed.iri_flags & IH_SCHEME) {
		cpfields(iri, &iparsed, iparsed.iri_flags);
		remove_dot_segments(iri->iri_path, sizeof(iri->iri_path));
		return (0);
	}

	cpfields(iri, &ibase, IH_SCHEME);

	if (iparsed.iri_flags & IH_HOST) {
		cpfields(iri, &iparsed, IH_AUTHORITY|IH_PATH|IH_QUERY);
		remove_dot_segments(iri->iri_path, sizeof(iri->iri_path));
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
		if (mergepath(iri->iri_path, sizeof(iri->iri_path),
		    ibase.iri_flags & IH_AUTHORITY, ibase.iri_path,
		    iparsed.iri_path) == -1)
			return (-1);
		iri->iri_flags |= IH_PATH;
	}
	if (remove_dot_segments(iri->iri_path, sizeof(iri->iri_path)) == -1)
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

	if (i->iri_flags & IH_FRAGMENT) {
		if (strlcat(buf, "#", buflen) >= buflen ||
		    strlcat(buf, i->iri_fragment, buflen) >= buflen)
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
iri_setport(struct iri *iri, const char *portstr)
{
	const char	*errstr;
	int		 port;

	port = strtonum(portstr, 1, UINT16_MAX, &errstr);
	if (errstr)
		return (-1);

	snprintf(iri->iri_portstr, sizeof(iri->iri_portstr), "%d", port);
	iri->iri_port = port;
	return (0);
}

int
iri_setquery(struct iri *iri, const char *p)
{
	ptrdiff_t	 bufsize;
	int		 r;
	char		*buf, *q, tmp[4];

	buf = q = iri->iri_query;
	bufsize = sizeof(iri->iri_query);
	while (*p && (q - buf < bufsize)) {
		if (unreserved(*p) || sub_delims(*p) || *p == ':' ||
		    *p == '@' || *p == '/' || *p == '?') {
			*q++ = *p++;
			continue;
		}

		if (q - buf >= bufsize - 3)
			goto err;
		r = snprintf(tmp, sizeof(tmp), "%%%02X", (int)*p);
		if (r < 0 || (size_t)r > sizeof(tmp))
			return (-1);
		*q++ = tmp[0];
		*q++ = tmp[1];
		*q++ = tmp[2];
		p++;
	}
	if ((*p == '\0') && (q - buf < bufsize)) {
		iri->iri_flags |= IH_QUERY;
		*q = '\0';
		return (0);
	}

 err:
	errno = ENOBUFS;
	return (-1);
}
