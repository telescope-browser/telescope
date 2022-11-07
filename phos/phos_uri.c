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

/*
 * TODOs:
 * - distinguish between an empty component and a undefined one
 * - ...
 */

#include <assert.h>

#include "compat.h"

#include "phos.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char	*sub_ip_literal(const char*);
static const char	*sub_host_dummy(const char*);
static const char	*sub_pchar(const char*);

static const char	*sub_segment(const char*);
static const char	*sub_segment_nz(const char*);
static const char	*sub_segment_nz_nc(const char*);
static const char	*sub_path_common(const char*);

static const char	*parse_scheme(const char*, struct phos_uri*);
static const char	*parse_host(const char*, struct phos_uri*);
static const char	*parse_port(const char*, struct phos_uri*);
static const char	*parse_authority(const char*, struct phos_uri*);
static const char	*parse_path_abempty(const char*, struct phos_uri*);
static const char	*parse_path_absolute(const char*, struct phos_uri*);
static const char	*parse_path_noscheme(const char*, struct phos_uri*);
static const char	*parse_path_rootless(const char*, struct phos_uri*);
static const char	*parse_path_empty(const char*, struct phos_uri*);
static const char	*parse_hier_part(const char*, struct phos_uri*);
static const char	*parse_query(const char*, struct phos_uri*);
static const char	*parse_fragment(const char*, struct phos_uri*);
static const char	*parse_uri(const char*, struct phos_uri*);
static const char	*parse_relative_part(const char*, struct phos_uri*);
static const char	*parse_relative_ref(const char*, struct phos_uri*);
static const char	*parse_uri_reference(const char*, struct phos_uri*);

static int		 hasprefix(const char*, const char*);
static char		*dotdot(char*, char*);
static void		 path_clean(struct phos_uri*);
static int		 merge_path(struct phos_uri*, const struct phos_uri*, const struct phos_uri*);

static int		 phos_resolve_uri_from(const struct phos_uri*, const struct phos_uri*, struct phos_uri*);


/* common defs */

#if unused
static inline int
gen_delims(int c)
{
	return c == ':'
		|| c == '/'
		|| c == '?'
		|| c == '#'
		|| c == '['
		|| c == ']'
		|| c == '@';
}
#endif

static inline int
sub_delims(int c)
{
	return c == '!'
		|| c == '$'
		|| c == '&'
		|| c == '\''
		|| c == '('
		|| c == ')'
		|| c == '*'
		|| c == '+'
		|| c == ','
		|| c == ';'
		|| c == '=';
}

#if unused
static inline int
reserved(int c)
{
	return gen_delims(c) || sub_delims(c);
}
#endif

static inline int
unreserved(int c)
{
	return isalpha(c)
		|| isdigit(c)
		|| c == '-'
		|| c == '.'
		|| c == '_'
		|| c == '~';
}


/* subs */

/*
 * IP-literal = "[" ( IPv6address / IPvFuture ) "]"
 *
 * in reality, we parse [.*]
 */
static const char *
sub_ip_literal(const char *s)
{
	if (*s != '[')
		return NULL;

	while (*s != '\0' && *s != ']')
		s++;

	if (*s == '\0')
		return NULL;
	return ++s;
}

/*
 * parse everything until : or / (or \0).
 * NB: empty hosts are technically valid!
 */
static const char *
sub_host_dummy(const char *s)
{
	while (*s != '\0' && *s != ':' && *s != '/')
		s++;
	return s;
}

/*
 * pchar = unreserved / pct-encoded / sub-delims / ":" / "@"
 */
static const char *
sub_pchar(const char *s)
{
	if (*s == '\0')
		return NULL;

	if (unreserved(*s))
		return ++s;

	if (*s == '%') {
		if (isxdigit(s[1]) && isxdigit(s[2]))
			return s + 3;
	}

	if (sub_delims(*s))
		return ++s;

	if (*s == ':' || *s == '@')
		return ++s;

	return NULL;
}

/*
 * segment = *pchar
 */
static const char *
sub_segment(const char *s)
{
	const char *t;

	while ((t = sub_pchar(s)) != NULL)
		s = t;
	return s;
}

/* segment-nz = 1*pchar */
static const char *
sub_segment_nz(const char *s)
{
	if ((s = sub_pchar(s)) == NULL)
		return NULL;
	return sub_segment(s);
}

/*
 * segment-nz-nc = 1*( unreserved / pct-encoded / sub-delims / "@" )
 *
 * so, 1*pchar excluding ":"
 */
static const char *
sub_segment_nz_nc(const char *s)
{
	const char *t;

	if (*s == ':')
		return NULL;

        while (*s != ':' && (t = sub_pchar(s)) != NULL)
		s = t;
	return s;
}

/* *( "/" segment ) */
static const char *
sub_path_common(const char *s)
{
	for (;;) {
		if (*s != '/')
			return s;
		s++;
		s = sub_segment(s);
	}
}


/* parse fns */

/*
 * scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
 */
static const char *
parse_scheme(const char *s, struct phos_uri *parsed)
{
	const char	*start = s;
	size_t		 len;

	if (!isalpha(*s))
		return NULL;

	while (*s != '\0') {
		if (isalpha(*s) ||
		    isdigit(*s) ||
		    *s == '+' ||
		    *s == '-' ||
		    *s == '.')
			s++;
		else
			break;
	}

	if (*s == '\0')
                return NULL;

	len = s - start;
	if (len >= sizeof(parsed->scheme))
		return NULL;

	memcpy(parsed->scheme, start, len);
	return s;
}

/*
 * host = IP-literal / IPv4address / reg-name
 *
 * rules IPv4address and reg-name are relaxed into parse_host_dummy.
 */
static const char *
parse_host(const char *s, struct phos_uri *parsed)
{
	const char	*t;
	size_t		 len;

	if ((t = sub_ip_literal(s)) != NULL ||
	    (t = sub_host_dummy(s)) != NULL) {
		len = t - s;
		if (len >= sizeof(parsed->scheme))
			return NULL;
		memcpy(parsed->host, s, len);
		return t;
	}

	return NULL;
}

/*
 * port = *digit
 */
static const char *
parse_port(const char *s, struct phos_uri *parsed)
{
	const char	*errstr, *start = s;
	size_t		 len;

	while (isdigit(*s))
		s++;

	if (s == start)
		return NULL;

	len = s - start;
	if (len >= sizeof(parsed->port))
		return NULL;

	memcpy(parsed->port, start, len);

        parsed->dec_port = strtonum(parsed->port, 0, 65535, &errstr);
	if (errstr != NULL)
		return NULL;

	return s;
}

/*
 * authority = host [ ":" port ]
 * (yep, blatantly ignore the userinfo stuff -- not relevant for Gemini)
 */
static const char *
parse_authority(const char *s, struct phos_uri *parsed)
{
	if ((s = parse_host(s, parsed)) == NULL)
		return NULL;

	if (*s == ':') {
		s++;
		return parse_port(s, parsed);
	}

	return s;
}

static inline const char *
set_path(const char *start, const char *end, struct phos_uri *parsed)
{
	size_t len;

	if (end == NULL)
		return NULL;

	len = end - start;
	if (len >= sizeof(parsed->path))
		return NULL;
	memcpy(parsed->path, start, len);
	return end;
}

/*
 * path-abempty = *( "/" segment )
 */
static const char *
parse_path_abempty(const char *s, struct phos_uri *parsed)
{
	const char *t;

	t = sub_path_common(s);
	return set_path(s, t, parsed);
}

/*
 * path-absolute = "/" [ segment-nz *( "/" segment ) ]
 */
static const char *
parse_path_absolute(const char *s, struct phos_uri *parsed)
{
	const char *t, *start = s;

	if (*s != '/')
		return NULL;

	s++;
	if ((t = sub_segment_nz(s)) == NULL)
		return set_path(start, s, parsed);

	s = sub_path_common(t);
	return set_path(start, s, parsed);
}

/*
 * path-noscheme = segment-nz-nc *( "/" segment )
 */
static const char *
parse_path_noscheme(const char *s, struct phos_uri *parsed)
{
	const char *start = s;

	if ((s = sub_segment_nz_nc(s)) == NULL)
		return NULL;
	s = sub_path_common(s);
	return set_path(start, s, parsed);
}

/*
 * path-rootless = segment-nz *( "/" segment )
 */
static const char *
parse_path_rootless(const char *s, struct phos_uri *parsed)
{
	const char *start = s;

	if ((s = sub_segment_nz(s)) == NULL)
		return NULL;
	s = sub_path_common(s);
	return set_path(start, s, parsed);
}

/*
 * path-empty = 0<pchar>
 */
static const char *
parse_path_empty(const char *s, struct phos_uri *parsed)
{
	return s;
}

/*
 * hier-part = "//" authority path-abempty
 *           / path-absolute
 *           / path-rootless
 *           / path-empty
 */
static const char *
parse_hier_part(const char *s, struct phos_uri *parsed)
{
	const char *t;

	if (s[0] == '/' && s[1] == '/') {
		s += 2;
		if ((s = parse_authority(s, parsed)) == NULL)
			return NULL;
		return parse_path_abempty(s, parsed);
	}

	if ((t = parse_path_absolute(s, parsed)) != NULL)
		return t;

	if ((t = parse_path_rootless(s, parsed)) != NULL)
		return t;

	return parse_path_empty(s, parsed);
}

/*
 * query = *( pchar / "/" / "?" )
 */
static const char *
parse_query(const char *s, struct phos_uri *parsed)
{
	const char	*t, *start = s;
	size_t		 len;

	while (*s != '\0') {
		if (*s == '/' || *s == '?') {
			s++;
			continue;
		}

		if ((t = sub_pchar(s)) == NULL)
                        break;
		s = t;
	}

	len = s - start;
	if (len >= sizeof(parsed->query))
		return NULL;

	memcpy(parsed->query, start, len);
	return s;
}

/*
 * fragment = *( pchar / "/" / "?" )
 */
static const char *
parse_fragment(const char *s, struct phos_uri *parsed)
{
	const char	*start = s;
	size_t		 len;

	for (;;) {
		if (*s == '\0')
			break;

		if (*s == '/' || *s == '?') {
			s++;
			continue;
		}

		if ((s = sub_pchar(s)) == NULL)
			return NULL;
	}

	len = s - start;
	if (len >= sizeof(parsed->fragment))
		return NULL;

	memcpy(parsed->fragment, start, len);
	return s;
}

/*
 * URI = scheme ":" hier-part [ "?" query ] [ "#" fragment ]
 */
static const char *
parse_uri(const char *s, struct phos_uri *parsed)
{
	if ((s = parse_scheme(s, parsed)) == NULL)
		return NULL;

	if (*s != ':')
		return NULL;

	s++;
	if ((s = parse_hier_part(s, parsed)) == NULL)
		return NULL;

	if (*s == '?') {
		s++;
		if ((s = parse_query(s, parsed)) == NULL)
			return NULL;
	}

	if (*s == '#') {
		s++;
		if ((s = parse_fragment(s, parsed)) == NULL)
			return NULL;
	}

	return s;
}

/*
 * relative-part = "//" authority path-abempty
 *               / path-absolute
 *               / path-noscheme
 *               / path-empty
 */
static const char *
parse_relative_part(const char *s, struct phos_uri *parsed)
{
	const char *t;

	if (s[0] == '/' && s[1] == '/') {
		s += 2;
		if ((s = parse_authority(s, parsed)) == NULL)
			return NULL;
		return parse_path_abempty(s, parsed);
	}

	if ((t = parse_path_absolute(s, parsed)) != NULL)
		return t;

	if ((t = parse_path_noscheme(s, parsed)) != NULL)
		return t;

	return parse_path_empty(s, parsed);
}

/*
 * relative-ref = relative-part [ "?" query ] [ "#" fragment ]
 */
static const char *
parse_relative_ref(const char *s, struct phos_uri *parsed)
{
	if ((s = parse_relative_part(s, parsed)) == NULL)
		return NULL;

	if (*s == '?') {
		s++;
		if ((s = parse_query(s, parsed)) == NULL)
			return NULL;
	}

	if (*s == '#') {
		s++;
		if ((s = parse_fragment(s, parsed)) == NULL)
			return NULL;
	}

	return s;
}

/*
 * URI-reference = URI / relative-ref
 */
static const char *
parse_uri_reference(const char *s, struct phos_uri *parsed)
{
	const char *t;

	if ((t = parse_uri(s, parsed)) != NULL)
		return t;
	memset(parsed, 0, sizeof(*parsed));
	return parse_relative_ref(s, parsed);
}


/*
 * absolute-URI = scheme ":" hier-part [ "?" query ]
 */
static const char *
parse_absolute_uri(const char *s, struct phos_uri *parsed)
{
	if ((s = parse_scheme(s, parsed)) == NULL)
		return NULL;

	if (*s != ':')
		return NULL;

	s++;
	if ((s = parse_hier_part(s, parsed)) == NULL)
		return NULL;

	if (*s == '?') {
		s++;
		if ((s = parse_query(s, parsed)) == NULL)
			return NULL;
	}

	return s;
}


/* normalizing fns */

static int
hasprefix(const char *str, const char *prfx)
{
	for (; *str == *prfx && *prfx != '\0'; str++, prfx++)
		;

	return *prfx == '\0';
}

static char *
dotdot(char *point, char *start)
{
	char	*t;

	for (t = point-1; t > start; --t) {
		if (*t == '/')
			break;
	}
	if (t < start)
		t = start;

	memmove(t, point, strlen(point)+1);
	return t;
}

/*
 * This is the "Remove Dot Segments" straight outta RFC3986, section
 * 5.2.4
 */
static void
path_clean(struct phos_uri *uri)
{
	char	*in = uri->path;

	while (in != NULL && *in != '\0') {
		assert(in >= uri->path);

		/* A) drop leading ../ or ./ */
		if (hasprefix(in, "../"))
			memmove(in, &in[3], strlen(&in[3])+1);
		else if (hasprefix(in, "./"))
			memmove(in, &in[2], strlen(&in[2])+1);

		/* B) replace /./ or /. with / */
		else if (hasprefix(in, "/./"))
			memmove(&in[1], &in[3], strlen(&in[3])+1);
		else if (!strcmp(in, "/."))
                        in[1] = '\0';

		/* C) resolve dot-dot */
		else if (hasprefix(in, "/../")) {
			in = dotdot(in, uri->path);
			memmove(&in[1], &in[4], strlen(&in[4])+1);
		} else if (!strcmp(in, "/..")) {
			in = dotdot(in, uri->path);
			in[1] = '\0';
			break;
		}

		/* D */
		else if (!strcmp(in, "."))
			*in = '\0';
		else if (!strcmp(in, ".."))
			*in = '\0';

		/* E */
		else
			in = strchr(in+1, '/');
	}
}

/*
 * see RFC3986 5.3.3 "Merge Paths".
 */
static int
merge_path(struct phos_uri *ret, const struct phos_uri *base,
    const struct phos_uri *ref)
{
	const char *s;
	size_t len;

	len = sizeof(ret->path);

	s = strrchr(base->path, '/');
	if ((*base->host != '\0' && *base->path == '\0') || s == NULL) {
		strlcpy(ret->path, "/", len);
	} else {
		/* copy the / too */
                memcpy(ret->path, base->path, s - base->path + 1);
	}

	return strlcat(ret->path, ref->path, len) < len;
}


/* public interface */

int
phos_parse_absolute_uri(const char *s, struct phos_uri *uri)
{
	memset(uri, 0, sizeof(*uri));

	if ((s = parse_absolute_uri(s, uri)) == NULL)
		return 0;
	if (*s != '\0')
		return 0;
	path_clean(uri);
	return 1;
}

int
phos_parse_uri_reference(const char *s, struct phos_uri *uri)
{
	memset(uri, 0, sizeof(*uri));

	if ((s = parse_uri_reference(s, uri)) == NULL)
		return 0;
	if (*s != '\0')
		return 0;
	path_clean(uri);
	return 1;
}

/*
 * Implementation of the "transform references" algorithm from
 * RFC3986, see 5.2.2.
 *
 * We expect base and ref to be URIs constructed by this library
 * (because we emit only normalized URIs).
 *
 * ATM this is marked as private because:
 * - let's say the URI is "."
 * - one calls phos_parse_uri_references
 * - it exists with success, but the path becomes ""
 * - this routine does the right thing, but the outcome is not what expected.
 *
 * so users for now have to user resolve_uri_from_str, which parses
 * the URI but not normalize it, and then call into us.
 */
static int
phos_resolve_uri_from(const struct phos_uri *base, const struct phos_uri *ref,
    struct phos_uri *ret)
{
	memset(ret, 0, sizeof(*ret));

	if (*ref->scheme != '\0') {
		strlcpy(ret->scheme, ref->scheme, sizeof(ret->scheme));
		strlcpy(ret->host, ref->host, sizeof(ret->host));
		strlcpy(ret->port, ref->port, sizeof(ret->port));
		ret->dec_port = ret->dec_port;
		strlcpy(ret->path, ref->path, sizeof(ret->path));
		strlcpy(ret->query, ref->query, sizeof(ret->query));
	} else {
		if (*ref->host != '\0') {
			strlcpy(ret->host, ref->host, sizeof(ret->host));
			strlcpy(ret->port, ref->port, sizeof(ret->port));
			ret->dec_port = ret->dec_port;
			strlcpy(ret->path, ref->path, sizeof(ret->path));
			strlcpy(ret->query, ref->query, sizeof(ret->query));
		} else {
			if (*ref->path == '\0') {
				strlcpy(ret->path, base->path, sizeof(ret->path));
				if (*ref->query != '\0')
					strlcpy(ret->query, ref->query, sizeof(ret->query));
				else
					strlcpy(ret->query, base->query, sizeof(ret->query));
			} else {
				if (*ref->path == '/')
					strlcpy(ret->path, ref->path, sizeof(ret->path));
				else {
					if (!merge_path(ret, base, ref))
						return 0;
				}
				path_clean(ret);

				strlcpy(ret->query, ref->query, sizeof(ret->query));
			}

			strlcpy(ret->host, base->host, sizeof(ret->host));
			strlcpy(ret->port, base->port, sizeof(ret->port));
			ret->dec_port = base->dec_port;
		}

		strlcpy(ret->scheme, base->scheme, sizeof(ret->scheme));
	}

	strlcpy(ret->fragment, ref->fragment, sizeof(ret->fragment));

	return 1;
}

int
phos_resolve_uri_from_str(const struct phos_uri *base, const char *refstr,
    struct phos_uri *ret)
{
	struct phos_uri ref;

	memset(&ref, 0, sizeof(ref));

	if ((refstr = parse_uri_reference(refstr, &ref)) == NULL)
		return 0;

	if (*refstr != '\0')
		return 0;

	return phos_resolve_uri_from(base, &ref, ret);
}

void
phos_uri_drop_empty_segments(struct phos_uri *uri)
{
	char *i;

	for (i = uri->path; *i; ++i) {
		if (*i == '/' && *(i+1) == '/') {
			memmove(i, i+1, strlen(i)); /* move also the \0 */
			i--;
		}
	}
}

int
phos_uri_set_query(struct phos_uri *uri, const char *query)
{
	char		*out;
	int		 t;
	size_t		 len;

	len = sizeof(uri->query);
	out = uri->query;
	memset(uri->query, 0, len);

	for (; *query != '\0' && len > 0; ++query) {
		if (*query == '/' ||
		    *query == '?' ||
		    *query == ':' ||
		    *query == '@' ||
		    unreserved(*query) ||
		    sub_delims(*query)) {
			*out++ = *query;
			len--;
		} else {
			if (len <= 4)
				break;
			len -= 3;
			*out++ = '%';
			t = *query;
			sprintf(out, "%02X", t);
			out += 2;
		}
	}

	return *query == '\0';
}

int
phos_serialize_uri(const struct phos_uri *uri, char *buf, size_t len)
{
#define CAT(s)					\
	if (strlcat(buf, s, len) >= len)	\
		return 0;

	strlcpy(buf, "", len);

	if (*uri->scheme != '\0') {
                CAT(uri->scheme);
		CAT(":");
	}

	if (*uri->host != '\0' || strcmp(uri->scheme, "file") == 0) {
		/*
		 * The file URI scheme has a quirk that even if a
		 * hostname is not present, we still have to append
		 * the two slashes.  This is why we have
		 * file:///etc/hosts and not file:/etc/hosts
		 */
		CAT("//");
		CAT(uri->host);
	}

	if (*uri->port != '\0' && strcmp(uri->port, "1965")) {
		CAT(":");
		CAT(uri->port);
	}

	CAT(uri->path);

	if (*uri->query != '\0') {
		CAT("?");
		CAT(uri->query);
	}

	if (*uri->fragment) {
		CAT("#");
		CAT(uri->fragment);
	}

	return 1;

#undef CAT
}
