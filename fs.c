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
static void		 dispatch_imsg(int, short, void*);

static struct event		 imsgev;
static struct imsgbuf		*ibuf;

static imsg_handlerfn *handlers[] = {
	[IMSG_GET] = handle_get,
	[IMSG_QUIT] = handle_quit,
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
	char		 path[PATH_MAX], buf[BUFSIZ];
	size_t		 r;
	FILE		*f;

	strlcpy(path, getenv("HOME"), sizeof(path));
	strlcat(path, "/.telescope/bookmarks.gmi", sizeof(path));

	if ((f = fopen(path, "r")) == NULL) {
		t = "# error\n\nCan't open ~/.telescope/bookmarks.gmi";
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
dispatch_imsg(int fd, short ev, void *d)
{
	struct imsgbuf	*ibuf = d;
	struct imsg	 imsg;
	ssize_t		 n;
	size_t		 datalen;

	if ((n = imsg_read(ibuf)) == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		_exit(1);
	}

	if (n == 0)
		_exit(1);

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			_exit(1);
		if (n == 0)
			return;
		datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
		handlers[imsg.hdr.type](&imsg, datalen);
		imsg_free(&imsg);
	}
}

int
fs_main(struct imsgbuf *b)
{
	ibuf = b;

	event_init();

	event_set(&imsgev, ibuf->fd, EV_READ | EV_PERSIST, dispatch_imsg, ibuf);
	event_add(&imsgev, NULL);

	sandbox_fs_process();

	event_dispatch();
	return 0;
}
