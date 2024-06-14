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

#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iri.h"
#include "parser.h"
#include "telescope.h"
#include "utils.h"

#ifndef LINE_MAX
#define LINE_MAX 2048
#endif

struct gm_selector {
	char		 type;
	const char	*ds;
	const char	*selector;
	const char	*addr;
	const char	*port;
};

static void	gm_parse_selector(char *, struct gm_selector *);

static int	gm_parse_line(struct buffer *, const char *, size_t);
static int	gm_serialize(struct buffer *, FILE *);

const struct parser gophermap_parser = {
	.name = "gophermap",
	.parseline = &gm_parse_line,
	.serialize = &gm_serialize,
};

static void
gm_parse_selector(char *line, struct gm_selector *s)
{
	s->type = *line++;
	s->ds = line;
	s->selector = "";
	s->addr = "";
	s->port = "";

	if ((line = strchr(line, '\t')) == NULL)
		return;
	*line++ = '\0';
	s->selector = line;

	if ((line = strchr(line, '\t')) == NULL)
		return;
	*line++ = '\0';
	s->addr = line;

	if ((line = strchr(line, '\t')) == NULL)
		return;
	*line++ = '\0';
	s->port = line;
}

static int
selector2uri(struct gm_selector *s, char *buf, size_t len)
{
	int	 r;

	r = snprintf(buf, len, "gopher://%s:%s/%c%s",
	    s->addr, s->port, s->type, *s->selector != '/' ? "/" : "");
	if (r < 0 || (size_t)r >= len)
		return (-1);

	buf += r;
	len -= r;
	return (iri_urlescape(s->selector, buf, len));
}

static inline int
emit_line(struct buffer *b, enum line_type type, struct gm_selector *s)
{
	struct line *l;
	char buf[LINE_MAX];

	if ((l = calloc(1, sizeof(*l))) == NULL)
		goto err;

	if ((l->line = strdup(s->ds)) == NULL)
		goto err;

	switch (l->type = type) {
	case LINE_LINK:
		if (s->type == 'h' && !strncmp(s->selector, "URL:", 4)) {
			strlcpy(buf, s->selector+4, sizeof(buf));
		} else if (selector2uri(s, buf, sizeof(buf)) == -1)
			goto err;

		if ((l->alt = strdup(buf)) == NULL)
			goto err;
		break;

	default:
		break;
	}

	TAILQ_INSERT_TAIL(&b->head, l, lines);

	return 1;

err:
	if (l != NULL) {
		free(l->line);
		free(l->alt);
		free(l);
	}
	return 0;
}

static int
gm_parse_line(struct buffer *b, const char *line, size_t linelen)
{
	char buf[LINE_MAX] = {0};
	struct gm_selector s = {0};

	memcpy(buf, line, MIN(sizeof(buf)-1, linelen));
	gm_parse_selector(buf, &s);

	switch (s.type) {
	case '0':	/* text file */
	case '1':	/* gopher submenu */
	case '2':	/* CCSO nameserver */
	case '4':	/* binhex-encoded file */
	case '5':	/* DOS file */
	case '6':	/* uuencoded file */
	case '7':	/* full-text search */
	case '8':	/* telnet */
	case '9':	/* binary file */
	case '+':	/* mirror or alternate server */
	case 'g':	/* gif */
	case 'I':	/* image */
	case 'T':	/* telnet 3270 */
	case ':':	/* gopher+: bitmap image */
	case ';':	/* gopher+: movie file */
	case 'd':	/* non-canonical: doc */
	case 'h':	/* non-canonical: html file */
	case 's':	/* non-canonical: sound file */
		if (!emit_line(b, LINE_LINK, &s))
			return 0;
		break;

	case 'i':	/* non-canonical: message */
		if (!emit_line(b, LINE_TEXT, &s))
			return 0;
		break;

	case '3':	/* error code */
		if (!emit_line(b, LINE_QUOTE, &s))
			return 0;
		break;
	}

	return 1;
}

static inline const char *
gopher_skip_selector(const char *path, int *ret_type)
{
	*ret_type = 0;

	if (!strcmp(path, "/") || *path == '\0') {
		*ret_type = '1';
		return path;
	}

	if (*path != '/')
		return path;
	path++;

	switch (*ret_type = *path) {
	case '0':
	case '1':
	case '7':
		break;

	default:
		*ret_type = 0;
		path -= 1;
		return path;
	}

	return ++path;
}

static int
serialize_link(struct line *line, const char *text, FILE *fp)
{
	size_t		 portlen = 0;
	int		 type;
	const char	*uri, *endhost, *port, *path, *colon;

	if ((uri = line->alt) == NULL)
		return -1;

	if (strncmp(uri, "gopher://", 9) != 0)
		return fprintf(fp, "h%s\tURL:%s\terror.host\t1\n",
		    text, line->alt);

	uri += 9; /* skip gopher:// */

	path = strchr(uri, '/');
	colon = strchr(uri, ':');

	if (path != NULL && colon > path)
		colon = NULL;

	if ((endhost = colon) == NULL &&
	    (endhost = path) == NULL)
		endhost = strchr(uri, '\0');

	if (colon != NULL) {
		for (port = colon+1; *port && *port != '/'; ++port)
			++portlen;
		port = colon+1;
	} else {
		port = "70";
		portlen = 2;
	}

	if (path == NULL) {
		type = '1';
		path = "";
	} else
		path = gopher_skip_selector(path, &type);

	return fprintf(fp, "%c%s\t%s\t%.*s\t%.*s\n", type, text,
	    path, (int)(endhost - uri), uri, (int)portlen, port);
}

static int
gm_serialize(struct buffer *b, FILE *fp)
{
	struct line	*line;
	const char	*text;
	int		 r;

	TAILQ_FOREACH(line, &b->head, lines) {
		if ((text = line->line) == NULL)
			text = "";

		switch (line->type) {
		case LINE_LINK:
			r = serialize_link(line, text, fp);
			break;

		case LINE_TEXT:
			r = fprintf(fp, "i%s\t\terror.host\t1\n", text);
			break;

		case LINE_QUOTE:
			r = fprintf(fp, "3%s\t\terror.host\t1\n", text);
			break;

		default:
			/* unreachable */
			abort();
		}

		if (r == -1)
			return 0;
	}

	return 1;
}
