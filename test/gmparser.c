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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"
#include "telescope.h"

void
erase_buffer(struct buffer *buffer)
{
	return;
}

int
main(void)
{
	struct tab	 tab;
	struct hist	 hist;
	struct evbuffer	*evb;
	ssize_t		 r;
	char		 buf[BUFSIZ];

	memset(&tab, 0, sizeof(tab));
	memset(&hist, 0, sizeof(hist));
	tab.hist_cur = &hist;

	parser_init(&tab, gophermap_initparser);
	for (;;) {
		if ((r = read(0, buf, sizeof(buf))) == -1)
			err(1, "read");
		if (r == 0)
			break;
		if (!parser_parse(&tab, buf, r))
			err(1, "parser_parse");
	}

	if (!parser_free(&tab))
		err(1, "parser_free");

	if ((evb = evbuffer_new()) == NULL)
		err(1, "evbuffer_new");

	if (parser_serialize(&tab, evb) == -1)
		err(1, "parser_serialize");

	evbuffer_write(evb, 1);

	evbuffer_free(evb);

	return 0;
}
