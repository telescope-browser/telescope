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

#include "hist.h"
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
	FILE		*fp;
	struct tab	 tab;
	ssize_t		 r;
	size_t		 blen;
	char		 buf[BUFSIZ], *b;

	memset(&tab, 0, sizeof(tab));
	if ((tab.hist = hist_new(HIST_LINEAR)) == NULL)
		err(1, "hist_new");
	if (hist_push(tab.hist, "dummy://address") == -1)
		err(1, "hist_push");

	TAILQ_INIT(&tab.buffer.head);
	TAILQ_INIT(&tab.buffer.vhead);

	parser_init(&tab.buffer, &gophermap_parser);
	for (;;) {
		if ((r = read(0, buf, sizeof(buf))) == -1)
			err(1, "read");
		if (r == 0)
			break;
		if (!parser_parse(&tab.buffer, buf, r))
			err(1, "parser_parse");
	}

	if (!parser_free(&tab))
		err(1, "parser_free");

	if ((fp = open_memstream(&b, &blen)) == NULL)
		err(1, "open_memstream");

	if (parser_serialize(&tab.buffer, fp) == -1)
		err(1, "parser_serialize");

	fclose(fp);
	write(1, b, blen);

	return 0;
}
