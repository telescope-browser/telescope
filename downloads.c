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
#include <unistd.h>

#include "telescope.h"
#include "ui.h"

struct downloads downloads = STAILQ_HEAD_INITIALIZER(downloads);

static void
no_downloads(void)
{
	struct line	*l;

	if ((l = calloc(1, sizeof(*l))) == NULL)
		abort();

	l->type = LINE_DOWNLOAD_INFO;
	l->line = strdup("No downloads");

	TAILQ_INSERT_TAIL(&downloadwin.page.head, l, lines);
}

void
recompute_downloads(void)
{
	struct download *d;
	struct line	*l;
	char		 buf[FMT_SCALED_STRSIZE];

	downloadwin.page.name = "*Downloads*";
	erase_buffer(&downloadwin);

	if (STAILQ_EMPTY(&downloads)) {
		no_downloads();
		goto end;
	}

	STAILQ_FOREACH(d, &downloads, entries) {
		if ((l = calloc(1, sizeof(*l))) == NULL)
			abort();

		fmt_scaled(d->bytes, buf);

		l->type = LINE_DOWNLOAD;
		if (d->fd == -1)
			l->type = LINE_DOWNLOAD_DONE;

		l->line = strdup(buf);
		l->alt = strdup(d->path);

		TAILQ_INSERT_TAIL(&downloadwin.page.head, l, lines);
	}

end:
	/*
	 * The exact value doesn't matter, as wrap_page only considers
	 * l->line, which is the human representation of the byte
	 * counter, and we know for sure is < FMT_SCALED_STRSIZE so it
	 * fits.
	 */
	wrap_page(&downloadwin, download_cols);
}

struct download *
enqueue_download(uint32_t id, const char *path)
{
	struct download *d;

	if ((d = calloc(1, sizeof(*d))) == NULL)
		abort();

	d->id = id;
	d->fd = -1;
	d->path = strdup(path);

	STAILQ_INSERT_HEAD(&downloads, d, entries);

	return d;
}

struct download *
download_by_id(uint32_t id)
{
	struct download *d;

	STAILQ_FOREACH(d, &downloads, entries) {
		if (d->id == id)
			return d;
	}

	return NULL;
}

void
download_finished(struct download *d)
{
	if (d == NULL)
		return;

	close(d->fd);
	d->fd = -1;

	ui_on_download_refresh();
	ui_prompt_download_cmd(d->path);
}
