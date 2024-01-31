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
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "telescope.h"
#include "utils.h"

int
mark_nonblock_cloexec(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL)) == -1)
		return 0;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		return 0;
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
		return 0;
	return 1;
}

int
has_suffix(const char *str, const char *sufx)
{
	size_t l, s;

	l = strlen(str);
	s = strlen(sufx);

	if (l < s)
		return 0;

	return !strcmp(str + (l - s), sufx);
}

int
unicode_isspace(uint32_t cp)
{
	if (cp < INT8_MAX)
		return isspace(cp);
	return 0;
}

int
unicode_isgraph(uint32_t cp)
{
	if (cp < INT8_MAX)
		return isgraph(cp);
	return 1;
}

void
imsg_event_add(struct imsgev *iev)
{
	iev->events = EV_READ;
	if (iev->ibuf.w.queued)
		iev->events |= EV_WRITE;

	event_del(&iev->ev);
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev);
	event_add(&iev->ev, NULL);
}

int
imsg_compose_event(struct imsgev *iev, uint16_t type, uint32_t peerid,
    pid_t pid, int fd, const void *data, uint16_t datalen)
{
	int	ret;

	if ((ret = imsg_compose(&iev->ibuf, type, peerid, pid, fd, data,
	    datalen) != -1))
		imsg_event_add(iev);

	return ret;
}

char *
imsg_borrow_str(struct imsg *imsg)
{
	struct ibuf	 ibuf;
	char		*data;
	size_t		 len;

	if (imsg_get_ibuf(imsg, &ibuf) == -1 ||
	    (data = ibuf_data(&ibuf)) == NULL ||
	    (len = ibuf_size(&ibuf)) == 0 ||
	    data[len - 1] != '\0')
		return NULL;

	return data;
}

void *
hash_alloc(size_t len, void *d)
{
	if ((d = malloc(len)) == NULL)
		abort();
	return d;
}

void *
hash_calloc(size_t nmemb, size_t size, void *d)
{
	if ((d = calloc(nmemb, size)) == NULL)
		abort();
	return d;
}

void
hash_free(void *ptr, void *d)
{
	free(ptr);
}
