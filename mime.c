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

#include <ctype.h>
#include <fnmatch.h>
#include <string.h>

#include "parser.h"
#include "telescope.h"

static int	check_for_utf8(char*);

static struct parser_table {
	const char	*mediatype;
	void		(*parserinit)(struct parser*);
} ptable[] = {
	{ "text/gemini",	gemtext_initparser },
	{ "text/*",		textplain_initparser },
	{ NULL, NULL}
};

static int
check_for_utf8(char *b)
{
	for (;;) {
		while (*b != '\0' && isspace(*b))
			b++;
		if (*b == '\0')
			break;
		if (!has_prefix(b, "charset=")) {
			while (*b != '\0' && *b != ';')
				b++;
			if (*b == '\0')
				break;
			b++;
			continue;
		}

		/* is charset= */
		b += strlen("charset=");
		/* TODO: improve the matching */
		return has_prefix(b, "ASCII") || has_prefix(b, "ascii") ||
			has_prefix(b, "UTF-8") || has_prefix(b, "utf-8");
	}

	return 1;
}

int
setup_parser_for(struct tab *tab)
{
	char			*b, buf[GEMINI_URL_LEN] = {0};
	struct parser_table	*t;

        memcpy(buf, tab->meta, sizeof(tab->meta));

	for (b = buf; *b != ';' && *b != '\0'; ++b)
		;

	if (*b == ';') {
		*b = '\0';
		++b;
	}

	if (!check_for_utf8(b))
		return 0;

	for (t = ptable; t->mediatype != NULL; ++t) {
		if (!fnmatch(t->mediatype, buf, 0)) {
			t->parserinit(&tab->buffer.page);
			return 1;
		}
	}

	return 0;
}
