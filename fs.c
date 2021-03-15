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
 * Handles the data in ~/.telescope
 */

#include "telescope.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void		 die(void) __attribute__((__noreturn__));
static void		 serve_bookmarks(uint32_t);
static void		 handle_get(struct imsg*, size_t);
static void		 handle_quit(struct imsg*, size_t);
static void		 handle_bookmark_page(struct imsg*, size_t);
static void		 handle_dispatch_imsg(int, short, void*);

static struct event		 imsgev;
static struct imsgbuf		*ibuf;

static char	bookmark_file[PATH_MAX];

static imsg_handlerfn *handlers[] = {
	[IMSG_GET] = handle_get,
	[IMSG_QUIT] = handle_quit,
	[IMSG_BOOKMARK_PAGE] = handle_bookmark_page,
};

static void __attribute__((__noreturn__))
die(void)
{
	abort(); 		/* TODO */
}

static void
serve_bookmarks(uint32_t peerid)
{
	const char	*t;
	char		 buf[BUFSIZ];
	size_t		 r;
	FILE		*f;

	if ((f = fopen(bookmark_file, "r")) == NULL) {
		t = "# error\n\nCan't open bookmarks\n";
		imsg_compose(ibuf, IMSG_BUF, peerid, 0, -1, t, strlen(t));
		imsg_compose(ibuf, IMSG_EOF, peerid, 0, -1, NULL, 0);
		imsg_flush(ibuf);
		return;
	}

	for (;;) {
		r = fread(buf, 1, sizeof(buf), f);
		imsg_compose(ibuf, IMSG_BUF, peerid, 0, -1, buf, r);
		imsg_flush(ibuf);
		if (r != sizeof(buf))
			break;
	}

	imsg_compose(ibuf, IMSG_EOF, peerid, 0, -1, NULL, 0);
	imsg_flush(ibuf);

	fclose(f);
}

static void
handle_get(struct imsg *imsg, size_t datalen)
{
	char		*data;
	const char	*p;

	data = imsg->data;

	if (data[datalen-1] != '\0')
		die();

	if (!strcmp(data, "about:new")) {
		imsg_compose(ibuf, IMSG_BUF, imsg->hdr.peerid, 0, -1,
		    about_new, strlen(about_new));
		imsg_compose(ibuf, IMSG_EOF, imsg->hdr.peerid, 0, -1, NULL, 0);
		imsg_flush(ibuf);
	} else if (!strcmp(data, "about:bookmarks")) {
		serve_bookmarks(imsg->hdr.peerid);
	} else {
		p = "# not found!\n";
		imsg_compose(ibuf, IMSG_BUF, imsg->hdr.peerid, 0, -1, p, strlen(p));
		imsg_compose(ibuf, IMSG_EOF, imsg->hdr.peerid, 0, -1, NULL, 0);
		imsg_flush(ibuf);
	}
}

static void
handle_quit(struct imsg *imsg, size_t datalen)
{
	event_loopbreak();
}

static void
handle_bookmark_page(struct imsg *imsg, size_t datalen)
{
	char	*data;
	int	 res;
	FILE	*f;

	data = imsg->data;
	if (data[datalen-1] != '\0')
		die();

	if ((f = fopen(bookmark_file, "a")) == NULL) {
		res = errno;
		goto end;
	}
	fprintf(f, "=> %s\n", data);
	fclose(f);

	res = 0;
end:
	imsg_compose(ibuf, IMSG_BOOKMARK_OK, 0, 0, -1, &res, sizeof(res));
	imsg_flush(ibuf);
}

static void
handle_dispatch_imsg(int fd, short ev, void *d)
{
	struct imsgbuf	*ibuf = d;
	dispatch_imsg(ibuf, handlers, sizeof(handlers));
}

int
fs_main(struct imsgbuf *b)
{
	ibuf = b;

	strlcpy(bookmark_file, getenv("HOME"), sizeof(bookmark_file));
	strlcat(bookmark_file, "/.telescope/bookmarks.gmi", sizeof(bookmark_file));

	event_init();

	event_set(&imsgev, ibuf->fd, EV_READ | EV_PERSIST, handle_dispatch_imsg, ibuf);
	event_add(&imsgev, NULL);

	sandbox_fs_process();

	event_dispatch();
	return 0;
}
