/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "compat.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tls.h>
#include <unistd.h>

#include "bufio.h"
#include "ev.h"		/* for EV_* flags */

int
buf_init(struct buf *buf)
{
	const size_t	 cap = BIO_CHUNK;

	memset(buf, 0, sizeof(*buf));
	if ((buf->buf = malloc(cap)) == NULL)
		return (-1);
	buf->cap = cap;
	return (0);
}

static int
buf_grow(struct buf *buf)
{
	size_t		 newcap;
	void		*t;

	newcap = buf->cap + BIO_CHUNK;
	t = realloc(buf->buf, newcap);
	if (t == NULL)
		return (-1);
	buf->buf = t;
	buf->cap = newcap;
	return (0);
}

int
buf_has_line(struct buf *buf, const char *nl)
{
	return (memmem(buf->buf, buf->len, nl, strlen(nl)) != NULL);
}

void
buf_drain(struct buf *buf, size_t l)
{
	buf->cur = 0;

	if (l >= buf->len) {
		buf->len = 0;
		return;
	}

	memmove(buf->buf, buf->buf + l, buf->len - l);
	buf->len -= l;
}

void
buf_drain_line(struct buf *buf, const char *nl)
{
	uint8_t		*endln;
	size_t		 nlen;

	nlen = strlen(nl);
	if ((endln = memmem(buf->buf, buf->len, nl, nlen)) == NULL)
		return;
	buf_drain(buf, endln + nlen - buf->buf);
}

void
buf_free(struct buf *buf)
{
	free(buf->buf);
	memset(buf, 0, sizeof(*buf));
}

int
bufio_init(struct bufio *bio)
{
	memset(bio, 0, sizeof(*bio));
	bio->fd = -1;

	if (buf_init(&bio->wbuf) == -1)
		return (-1);
	if (buf_init(&bio->rbuf) == -1) {
		buf_free(&bio->wbuf);
		return (-1);
	}
	return (0);
}

void
bufio_free(struct bufio *bio)
{
	if (bio->ctx)
		tls_free(bio->ctx);
	bio->ctx = NULL;

	if (bio->fd != -1)
		close(bio->fd);
	bio->fd = -1;

	buf_free(&bio->rbuf);
	buf_free(&bio->wbuf);
}

int
bufio_close(struct bufio *bio)
{
	if (bio->ctx == NULL)
		return (0);

	switch (tls_close(bio->ctx)) {
	case 0:
		return 0;
	case TLS_WANT_POLLIN:
	case TLS_WANT_POLLOUT:
		errno = EAGAIN;
		bio->pflags = EV_READ | EV_WRITE;
		/* fallthrough */
	default:
		return (-1);
	}
}

int
bufio_reset(struct bufio *bio)
{
	bufio_free(bio);
	return (bufio_init(bio));
}

void
bufio_set_fd(struct bufio *bio, int fd)
{
	bio->fd = fd;
}

int
bufio_starttls(struct bufio *bio, const char *host, int insecure)
{
	struct tls_config	*conf;

	if ((conf = tls_config_new()) == NULL)
		return (-1);

	if (insecure) {
		tls_config_insecure_noverifycert(conf);
		tls_config_insecure_noverifyname(conf);
		tls_config_insecure_noverifytime(conf);
	}

	if ((bio->ctx = tls_client()) == NULL) {
		tls_config_free(conf);
		return (-1);
	}

	if (tls_configure(bio->ctx, conf) == -1) {
		tls_config_free(conf);
		return (-1);
	}

	tls_config_free(conf);

	if (tls_connect_socket(bio->ctx, bio->fd, host) == -1)
		return (-1);

	return (0);
}

int
bufio_ev(struct bufio *bio)
{
	short		 ev;

	if (bio->pflags)
		return (bio->pflags);

	ev = EV_READ;
	if (bio->wbuf.len != 0)
		ev |= EV_WRITE;

	return (ev);
}

ssize_t
bufio_read(struct bufio *bio)
{
	struct buf	*rbuf = &bio->rbuf;
	ssize_t		 r;

	assert(rbuf->cap >= rbuf->len);
	if (rbuf->cap - rbuf->len < BIO_CHUNK) {
		if (buf_grow(rbuf) == -1)
			return (-1);
	}

	if (bio->ctx) {
		r = tls_read(bio->ctx, rbuf->buf + rbuf->len,
		    rbuf->cap - rbuf->cap);
		switch (r) {
		case TLS_WANT_POLLIN:
		case TLS_WANT_POLLOUT:
			bio->pflags = EV_READ | EV_WRITE;
			errno = EAGAIN;
			return (-1);
		case -1:
			return (-1);
		default:
			bio->pflags = 0;
			rbuf->len += r;
			return (r);
		}
	}

	r = read(bio->fd, rbuf->buf + rbuf->len, rbuf->cap - rbuf->len);
	if (r == -1)
		return (-1);
	rbuf->len += r;
	return (r);
}

ssize_t
bufio_write(struct bufio *bio)
{
	struct buf	*wbuf = &bio->wbuf;
	ssize_t		 w;

	if (bio->ctx) {
		switch (w = tls_write(bio->ctx, wbuf->buf, wbuf->len)) {
		case TLS_WANT_POLLIN:
		case TLS_WANT_POLLOUT:
			bio->pflags = EV_WRITE | EV_READ;
			errno = EAGAIN;
			return (-1);
		case -1:
			return (-1);
		default:
			bio->pflags = 0;
			buf_drain(wbuf, w);
			return (w);
		}
	}

	w = write(bio->fd, wbuf->buf, wbuf->len);
	if (w == -1)
		return (-1);
	buf_drain(wbuf, w);
	return (w);
}

int
bufio_compose(struct bufio *bio, const void *d, size_t len)
{
	struct buf	*wbuf = &bio->wbuf;

	while (wbuf->cap - wbuf->len < len) {
		if (buf_grow(wbuf) == -1)
			return (-1);
	}

	memcpy(wbuf->buf + wbuf->len, d, len);
	wbuf->len += len;
	return (0);
}

int
bufio_compose_str(struct bufio *bio, const char *str)
{
	return (bufio_compose(bio, str, strlen(str)));
}

int
bufio_compose_fmt(struct bufio *bio, const char *fmt, ...)
{
	va_list		 ap;
	char		*str;
	int		 r;

	va_start(ap, fmt);
	r = vasprintf(&str, fmt, ap);
	va_end(ap);

	if (r == -1)
		return (-1);
	r = bufio_compose(bio, str, r);
	free(str);
	return (r);
}

void
bufio_rewind_cursor(struct bufio *bio)
{
	bio->rbuf.cur = 0;
}

int
bufio_get_cb(void *d)
{
	struct bufio	*bio = d;
	struct buf	*rbuf = &bio->rbuf;

	if (rbuf->cur >= rbuf->len)
		return (EOF);
	return (rbuf->buf[rbuf->cur++]);
}

int
bufio_peek_cb(void *d)
{
	struct bufio	*bio = d;
	struct buf	*rbuf = &bio->rbuf;

	if (rbuf->cur >= rbuf->len)
		return (EOF);
	return (rbuf->buf[rbuf->cur]);
}
