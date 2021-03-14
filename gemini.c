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
 * TODO:
 *  - move the various
 *	imsg_compose(...);
 *	imsg_flush(...);
 *    to something more asynchronous
 */

#include <telescope.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tls.h>
#include <unistd.h>

static struct tls_config	*tlsconf;
static struct imsgbuf		*ibuf;

struct req;

static void		 die(void) __attribute__((__noreturn__));
static char		*xasprintf(const char*, ...);
static int 		 conn_towards(struct url*, char**);

static void		 close_with_err(struct req*, const char *err);
static struct req	*req_by_id(uint32_t);
static struct req	*req_by_id_try(uint32_t);

static void		 do_handshake(int, short, void*);
static void		 write_request(int, short, void*);
static void		 read_reply(int, short, void*);
static void		 parse_reply(struct req*);
static void		 copy_body(int, short, void*);

static void		 check_special_page(struct req*, const char*);

static void		 handle_get(struct imsg*, size_t);
static void		 handle_cert_status(struct imsg*, size_t);
static void		 handle_proceed(struct imsg*, size_t);
static void		 handle_stop(struct imsg*, size_t);
static void		 handle_quit(struct imsg*, size_t);

/* TODO: making this customizable */
struct timeval timeout_for_handshake = { 5, 0 };

static imsg_handlerfn *handlers[] = {
	[IMSG_GET] = handle_get,
	[IMSG_CERT_STATUS] = handle_cert_status,
	[IMSG_PROCEED] = handle_proceed,
	[IMSG_STOP] = handle_stop,
	[IMSG_QUIT] = handle_quit,
};

typedef void (*statefn)(int, short, void*);

TAILQ_HEAD(, req) reqhead;
/* a pending request */
struct req {
	struct event		 ev;
	struct url		 url;
	uint32_t		 id;
	int			 fd;
	struct tls		*ctx;
	char			 buf[1024];
	size_t			 off;
	TAILQ_ENTRY(req)	 reqs;
};

static inline void
yield_r(struct req *req, statefn fn, struct timeval *tv)
{
	event_once(req->fd, EV_READ, fn, req, tv);
}

static inline void
yield_w(struct req *req, statefn fn, struct timeval *tv)
{
	event_once(req->fd, EV_WRITE, fn, req, tv);
}

static inline void
advance_buf(struct req *req, size_t len)
{
	assert(len <= req->off);

	req->off -= len;
	memmove(req->buf, req->buf + len, req->off);
}

static void __attribute__((__noreturn__))
die(void)
{
	abort(); 		/* TODO */
}

static char *
xasprintf(const char *fmt, ...)
{
	va_list ap;
	char *s;

	va_start(ap, fmt);
	if (vasprintf(&s, fmt, ap) == -1)
		s = NULL;
	va_end(ap);

	return s;
}

static int
conn_towards(struct url *url, char **err)
{
	struct addrinfo	 hints, *servinfo, *p;
	int		 status, sock;
	const char	*proto = "1965";

	*err = NULL;

	if (*url->port != '\0')
		proto = url->port;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((status = getaddrinfo(url->host, proto, &hints, &servinfo))) {
		*err = xasprintf("failed to resolve %s: %s",
		    url->host, gai_strerror(status));
		return -1;
	}

	sock = -1;
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
			continue;
		if (connect(sock, p->ai_addr, p->ai_addrlen) != -1)
			break;
		close(sock);
	}

	if (sock == -1)
		*err = xasprintf("couldn't connect to %s", url->host);
	else
		mark_nonblock(sock);

	freeaddrinfo(servinfo);
	return sock;
}

static struct req *
req_by_id(uint32_t id)
{
	struct req *r;

	if ((r = req_by_id_try(id)) == NULL)
		die();
	return r;
}

static struct req *
req_by_id_try(uint32_t id)
{
	struct req *r;

	TAILQ_FOREACH(r, &reqhead, reqs) {
		if (r->id == id)
			return r;
	}

	return NULL;
}

static void
close_conn(int fd, short ev, void *d)
{
	struct req	*req = d;

	if (req->ctx != NULL) {
		switch (tls_close(req->ctx)) {
		case TLS_WANT_POLLIN:
			yield_r(req, close_conn, NULL);
			return;
		case TLS_WANT_POLLOUT:
			yield_w(req, close_conn, NULL);
			return;
		}

		tls_free(req->ctx);
	}

	TAILQ_REMOVE(&reqhead, req, reqs);
	if (req->fd != -1)
		close(req->fd);
	free(req);
}

static void
close_with_err(struct req *req, const char *err)
{
	imsg_compose(ibuf, IMSG_ERR, req->id, 0, -1, err, strlen(err)+1);
	imsg_flush(ibuf);
	close_conn(0, 0, req);
}

static void
do_handshake(int fd, short ev, void *d)
{
	struct req	*req = d;
	const char	*hash;

	if (ev == EV_TIMEOUT) {
		close_with_err(req, "Timeout loading page");
		return;
	}

	switch (tls_handshake(req->ctx)) {
	case TLS_WANT_POLLIN:
		yield_r(req, do_handshake, NULL);
		return;
	case TLS_WANT_POLLOUT:
		yield_w(req, do_handshake, NULL);
		return;
	}

	hash = tls_peer_cert_hash(req->ctx);
	imsg_compose(ibuf, IMSG_CHECK_CERT, req->id, 0, -1, hash, strlen(hash)+1);
	imsg_flush(ibuf);
}

static void
write_request(int fd, short ev, void *d)
{
	struct req	*req = d;
	ssize_t		 r;
	size_t		 len;
	char		 buf[1024], *err;

	strlcpy(buf, "gemini://", sizeof(buf));
	strlcat(buf, req->url.host, sizeof(buf));
	strlcat(buf, "/", sizeof(buf));
	strlcat(buf, req->url.path, sizeof(buf));

	if (req->url.query[0] != '\0') {
		strlcat(buf, "?", sizeof(buf));
		strlcat(buf, req->url.query, sizeof(buf));
	}

	len = strlcat(buf, "\r\n", sizeof(buf));

	assert(len <= sizeof(buf));

	switch (r = tls_write(req->ctx, buf, len)) {
	case -1:
		err = xasprintf("tls_write: %s", tls_error(req->ctx));
		close_with_err(req, err);
		free(err);
		break;
	case TLS_WANT_POLLIN:
		yield_r(req, write_request, NULL);
		break;
	case TLS_WANT_POLLOUT:
		yield_w(req, write_request, NULL);
		break;
	default:
		/* assume r == len */
		(void)r;
		yield_r(req, read_reply, NULL);
		break;
	}
}

static void
read_reply(int fd, short ev, void *d)
{
	struct req	*req = d;
	size_t		 len;
	ssize_t		 r;
	char		*buf, *e;

	buf = req->buf + req->off;
	len = sizeof(req->buf) - req->off;

	switch (r = tls_read(req->ctx, buf, len)) {
	case -1:
		e = xasprintf("tls_read: %s", tls_error(req->ctx));
		close_with_err(req, e);
		free(e);
		break;
	case TLS_WANT_POLLIN:
		yield_r(req, read_reply, NULL);
		break;
	case TLS_WANT_POLLOUT:
		yield_w(req, read_reply, NULL);
		break;
	default:
		req->off += r;

		/* TODO: really watch for \r\n not \n alone */
		if ((e = telescope_strnchr(req->buf, '\n', req->off)) != NULL)
			parse_reply(req);
		else if (req->off == sizeof(req->buf))
			close_with_err(req, "invalid response");
		else
			yield_r(req, read_reply, NULL);
		break;
	}
}

static void
parse_reply(struct req *req)
{
	int	 code;
	char	*e;
	size_t	 len;

	if (req->off < 4)
		goto err;

	if (!isdigit(req->buf[0]) || !isdigit(req->buf[1]))
		goto err;

	code = (req->buf[0] - '0')*10 + (req->buf[1] - '0');

	if (!isspace(req->buf[2]))
		goto err;

	advance_buf(req, 3);
	if ((e = telescope_strnchr(req->buf, '\r', req->off)) == NULL)
		goto err;

	*e = '\0';
	e++;
	len = e - req->buf;
	imsg_compose(ibuf, IMSG_GOT_CODE, req->id, 0, -1, &code, sizeof(code));
	imsg_compose(ibuf, IMSG_GOT_META, req->id, 0, -1,
	    req->buf, len);
	imsg_flush(ibuf);

	if (code != 20)
		close_conn(0, 0, req);
	advance_buf(req, len+1); /* skip \n too */

	return;

err:
	close_with_err(req, "malformed request");
}

static void
copy_body(int fd, short ev, void *d)
{
	struct req	*req = d;
	ssize_t		 r;

	do {
		if (req->off != 0) {
			imsg_compose(ibuf, IMSG_BUF, req->id, 0, -1,
			    req->buf, req->off);
			imsg_flush(ibuf);
		}

		switch (r = tls_read(req->ctx, req->buf, sizeof(req->buf))) {
		case TLS_WANT_POLLIN:
			yield_r(req, copy_body, NULL);
			return;
		case TLS_WANT_POLLOUT:
			yield_w(req, copy_body, NULL);
			return;
		case 0:
			imsg_compose(ibuf, IMSG_EOF, req->id, 0, -1, NULL, 0);
			imsg_flush(ibuf);
			close_conn(0, 0, req);
			return;
		default:
			req->off = r;
		}
	} while(1);
}

static void
handle_get(struct imsg *imsg, size_t datalen)
{
	struct req	*req;
	const char	*e;
	char		*data, *err = NULL;

	data = imsg->data;

	if (data[datalen-1] != '\0')
		die();

	if ((req = calloc(1, sizeof(*req))) == NULL)
		die();

	req->id = imsg->hdr.peerid;
	TAILQ_INSERT_HEAD(&reqhead, req, reqs);

        if (!url_parse(imsg->data, &req->url, &e)) {
		fprintf(stderr, "failed to parse url: %s\n", e);
		close_with_err(req, e);
		return;
	}

	if ((req->fd = conn_towards(&req->url, &err)) == -1)
		goto err;
	if ((req->ctx = tls_client()) == NULL)
		goto err;
	if (tls_configure(req->ctx, tlsconf) == -1) {
		err = xasprintf("tls_configure: %s", tls_error(req->ctx));
		goto err;
	}
	if (tls_connect_socket(req->ctx, req->fd, req->url.host) == -1) {
		err = xasprintf("tls_connect_socket: %s", tls_error(req->ctx));
		goto err;
	}

	yield_w(req, do_handshake, &timeout_for_handshake);
	return;

err:
        close_with_err(req, err);
	free(err);
}

static void
handle_cert_status(struct imsg *imsg, size_t datalen)
{
	struct req	*req;
	int		 is_ok;

	req = req_by_id(imsg->hdr.peerid);

	if (datalen < sizeof(is_ok))
		die();
	memcpy(&is_ok, imsg->data, sizeof(is_ok));

	if (is_ok)
		yield_w(req, write_request, NULL);
	else
		close_conn(0, 0, req);
}

static void
handle_proceed(struct imsg *imsg, size_t datalen)
{
	struct req	*req;

	req = req_by_id(imsg->hdr.peerid);
	yield_r(req, copy_body, NULL);
}

static void
handle_stop(struct imsg *imsg, size_t datalen)
{
	struct req	*req;

	if ((req = req_by_id_try(imsg->hdr.peerid)) == NULL)
		return;
	close_conn(0, 0, req);
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
client_main(struct imsgbuf *b)
{
	ibuf = b;

	TAILQ_INIT(&reqhead);

	if ((tlsconf = tls_config_new()) == NULL)
		die();
	tls_config_insecure_noverifycert(tlsconf);

	event_init();

	event_set(&imsgev, ibuf->fd, EV_READ | EV_PERSIST, dispatch_imsg, ibuf);
	event_add(&imsgev, NULL);

	sandbox_network_process();

	event_dispatch();
	return 0;
}
