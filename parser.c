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

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "telescope.h"
#include "ui.h"

/*
 * Load a text/gemini page given the string page.  Always returns 0.
 */
int
load_page_from_str(struct tab *tab, const char *page)
{
	parser_init(tab, gemtext_initparser);
	if (!tab->buffer.page.parse(&tab->buffer.page, page, strlen(page)))
		abort();
	if (!tab->buffer.page.free(&tab->buffer.page))
		abort();
	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
	return 0;
}

void
parser_init(struct tab *tab, parserfn fn)
{
	erase_buffer(&tab->buffer);
	fn(&tab->buffer.page);
	tab->buffer.page.init = fn;
}

int
parser_parse(struct tab *tab, const char *chunk, size_t len)
{
	return tab->buffer.page.parse(&tab->buffer.page, chunk, len);
}

int
parser_free(struct tab *tab)
{
	int	 r;
	char	*tilde, *slash;

	r = tab->buffer.page.free(&tab->buffer.page);

	if (*tab->buffer.page.title != '\0')
		return r;

	/*
	 * heuristic: see if there is a "tilde user" and use that as
	 * page title, using the full domain name as fallback.
	 */
	if ((tilde = strstr(tab->hist_cur->h, "/~")) != NULL) {
		strlcpy(tab->buffer.page.title, tilde+1,
		    sizeof(tab->buffer.page.title));

		if ((slash = strchr(tab->buffer.page.title, '/')) != NULL)
			*slash = '\0';
	} else
		strlcpy(tab->buffer.page.title, tab->uri.host,
		    sizeof(tab->buffer.page.title));

	return r;
}

int
parser_serialize(struct tab *tab, struct evbuffer *evb)
{
	struct line	*line;
	const char	*text;
	int		 r;

	if (tab->buffer.page.serialize != NULL)
		return tab->buffer.page.serialize(&tab->buffer.page, evb);

	/* a default implementation good enough for plain text */
	TAILQ_FOREACH(line, &tab->buffer.page.head, lines) {
		if ((text = line->line) == NULL)
			text = "";

		r = evbuffer_add_printf(evb, "%s\n", text);
		if (r == -1)
			return 0;
	}

	return 1;
}

int
parser_append(struct parser *p, const char *buf, size_t len)
{
	size_t newlen;
	char *t;

	newlen = len + p->len;
	if ((t = calloc(1, newlen)) == NULL)
		return 0;
	memcpy(t, p->buf, p->len);
	memcpy(t + p->len, buf, len);
	free(p->buf);
	p->buf = t;
	p->len = newlen;
	return 1;
}

int
parser_set_buf(struct parser *p, const char *buf, size_t len)
{
	char *tmp;

	if (len == 0) {
		p->len = 0;
		free(p->buf);
		p->buf = NULL;
		return 1;
	}

	/*
	 * p->buf and buf can (and probably almost always will)
	 * overlap!
	 */

	if ((tmp = calloc(1, len)) == NULL)
		return 0;
	memcpy(tmp, buf, len);
	free(p->buf);
	p->buf = tmp;
	p->len = len;
	return 1;
}

int
parser_foreach_line(struct parser *p, const char *buf, size_t size,
    parsechunkfn fn)
{
	char		*b, *e;
	unsigned int	 ch;
	size_t		 i, l, len;

	if (!parser_append(p, buf, size))
		return 0;
	b = p->buf;
	len = p->len;

	if (!(p->flags & PARSER_IN_BODY) && len < 3)
		return 1;

	if (!(p->flags & PARSER_IN_BODY)) {
		p->flags |= PARSER_IN_BODY;

		/*
		 * drop the BOM: only UTF-8 is supported, and there
		 * it's useless; some editors may still add one
		 * though.
		 */
		if (memmem(b, len, "\xEF\xBB\xBF", 3) == b) {
			b += 3;
			len -= 3;
		}
	}

	/* drop every "funny" ASCII character */
	for (i = 0; i < len; ) {
		ch = b[i];
		if ((ch >= ' ' || ch == '\n' || ch == '\t')
		    && ch != 127) { /* del */
			++i;
			continue;
		}
		memmove(&b[i], &b[i+1], len - i - 1);
		len--;
	}

	while (len > 0) {
		if ((e = memmem((char*)b, len, "\n", 1)) == NULL)
			break;
		l = e - b;

		if (!fn(p, b, l))
			return 0;

		len -= l;
		b += l;

		if (len > 0) {
			/* skip \n */
			len--;
			b++;
		}
	}

	return parser_set_buf(p, b, len);
}
