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

#if HAVE_ASR_RUN
# include <asr.h>
#endif

#include "telescope.h"
#include "utils.h"

static struct imsgev		*iev_ui;

/* a pending request */
struct req {
	struct phos_uri		 url;
	uint32_t		 id;
	int			 proto;
	int			 fd;
	struct tls		*ctx;
	char			 req[1024];
	size_t			 len;
	int			 done_header;
	struct bufferevent	*bev;

	struct addrinfo		*servinfo, *p;
#if HAVE_ASR_RUN
	struct addrinfo		 hints;
	struct event_asr	*asrev;
#endif

	TAILQ_ENTRY(req)	 reqs;
};

static struct req	*req_by_id(uint32_t);

static void	 die(void) __attribute__((__noreturn__));

static void	 try_to_connect(int, short, void*);

#if HAVE_ASR_RUN
static void	 query_done(struct asr_result*, void*);
static void	 async_conn_towards(struct req*);
#else
static void	 blocking_conn_towards(struct req*);
#endif

static void	 close_with_err(struct req*, const char*);
static void	 close_with_errf(struct req*, const char*, ...)
    __attribute__((format(printf, 2, 3)));

static void	 net_tls_handshake(int, short, void *);
static void	 net_tls_readcb(int, short, void *);
static void	 net_tls_writecb(int, short, void *);

static int	 gemini_parse_reply(struct req *, const char *, size_t);

static void	 net_ready(struct req *req);
static void	 net_read(struct bufferevent *, void *);
static void	 net_write(struct bufferevent *, void *);
static void	 net_error(struct bufferevent *, short, void *);

static void	 handle_get_raw(struct imsg *, size_t);
static void	 handle_cert_status(struct imsg*, size_t);
static void	 handle_proceed(struct imsg*, size_t);
static void	 handle_stop(struct imsg*, size_t);
static void	 handle_quit(struct imsg*, size_t);
static void	 handle_dispatch_imsg(int, short, void*);

static int	 net_send_ui(int, uint32_t, const void *, uint16_t);

/* TODO: making this customizable */
struct timeval timeout_for_handshake = { 5, 0 };

static imsg_handlerfn *handlers[] = {
	[IMSG_GET_RAW]		= handle_get_raw,
	[IMSG_CERT_STATUS]	= handle_cert_status,
	[IMSG_PROCEED]		= handle_proceed,
	[IMSG_STOP]		= handle_stop,
	[IMSG_QUIT]		= handle_quit,
};

typedef void (*statefn)(int, short, void*);

TAILQ_HEAD(, req) reqhead;

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

static struct req *
req_by_id(uint32_t id)
{
	struct req *r;

	TAILQ_FOREACH(r, &reqhead, reqs) {
		if (r->id == id)
			return r;
	}

	return NULL;
}

static void __attribute__((__noreturn__))
die(void)
{
	abort(); 		/* TODO */
}

static void
try_to_connect(int fd, short ev, void *d)
{
	struct req	*req = d;
	int		 error = 0;
	socklen_t	 len = sizeof(error);

again:
	if (req->p == NULL)
		goto err;

	if (req->fd != -1) {
		if (getsockopt(req->fd, SOL_SOCKET, SO_ERROR, &error,
		    &len) == -1)
			goto err;
		if (error != 0) {
			errno = error;
			goto err;
		}
		goto done;
	}

	req->fd = socket(req->p->ai_family, req->p->ai_socktype,
	    req->p->ai_protocol);
	if (req->fd == -1) {
		req->p = req->p->ai_next;
		goto again;
	} else {
		if (!mark_nonblock_cloexec(req->fd))
			goto err;
		if (connect(req->fd, req->p->ai_addr, req->p->ai_addrlen) == 0)
			goto done;
		yield_w(req, try_to_connect, NULL);
	}
	return;

err:
	freeaddrinfo(req->servinfo);
	close_with_errf(req, "failed to connect to %s",
	    req->url.host);
	return;

done:
	freeaddrinfo(req->servinfo);

	switch (req->proto) {
	case PROTO_FINGER:
	case PROTO_GOPHER:
		/* finger and gopher don't have a header nor TLS */
		req->done_header = 1;
		net_ready(req);
		break;

	case PROTO_GEMINI: {
		struct tls_config *conf;

		if ((conf = tls_config_new()) == NULL)
			die();

		tls_config_insecure_noverifycert(conf);
		tls_config_insecure_noverifyname(conf);

		/* prepare tls */
		if ((req->ctx = tls_client()) == NULL) {
			close_with_errf(req, "tls_client: %s",
			    strerror(errno));
			return;
		}

		if (tls_configure(req->ctx, conf) == -1) {
			close_with_errf(req, "tls_configure: %s",
			    tls_error(req->ctx));
			return;
		}
		tls_config_free(conf);

		if (tls_connect_socket(req->ctx, req->fd, req->url.host)
		    == -1) {
			close_with_errf(req, "tls_connect_socket: %s",
			    tls_error(req->ctx));
			return;
		}
		yield_w(req, net_tls_handshake, &timeout_for_handshake);
		break;
	}

	default:
		die();
	}
}

#if HAVE_ASR_RUN
static void
query_done(struct asr_result *res, void *d)
{
	struct req	*req = d;

	req->asrev = NULL;
	if (res->ar_gai_errno != 0) {
		close_with_errf(req, "failed to resolve %s: %s",
		    req->url.host, gai_strerror(res->ar_gai_errno));
		return;
	}

	req->fd = -1;
	req->servinfo = res->ar_addrinfo;
	req->p = res->ar_addrinfo;
	try_to_connect(0, 0, req);
}

static void
async_conn_towards(struct req *req)
{
	struct asr_query	*q;
	const char		*proto = "1965";

	if (*req->url.port != '\0')
		proto = req->url.port;

	req->hints.ai_family = AF_UNSPEC;
	req->hints.ai_socktype = SOCK_STREAM;
	q = getaddrinfo_async(req->url.host, proto, &req->hints, NULL);
	req->asrev = event_asr_run(q, query_done, req);
}
#else
static void
blocking_conn_towards(struct req *req)
{
	struct addrinfo	 hints;
	struct phos_uri	*url = &req->url;
	int		 status;
	const char	*proto = "1965";

	if (*url->port != '\0')
		proto = url->port;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((status = getaddrinfo(url->host, proto, &hints, &req->servinfo))) {
		close_with_errf(req, "failed to resolve %s: %s",
		    url->host, gai_strerror(status));
		return;
	}

	req->fd = -1;
	req->p = req->servinfo;
	try_to_connect(0, 0, req);
}
#endif

static void
close_conn(int fd, short ev, void *d)
{
	struct req	*req = d;

#if HAVE_ASR_RUN
	if (req->asrev != NULL)
		event_asr_abort(req->asrev);
#endif

	if (req->bev != NULL) {
		bufferevent_free(req->bev);
		req->bev = NULL;
	}

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
		req->ctx = NULL;
	}

	TAILQ_REMOVE(&reqhead, req, reqs);
	if (req->fd != -1)
		close(req->fd);
	free(req);
}

static void
close_with_err(struct req *req, const char *err)
{
	net_send_ui(IMSG_ERR, req->id, err, strlen(err)+1);
	close_conn(0, 0, req);
}

static void
close_with_errf(struct req *req, const char *fmt, ...)
{
	va_list	 ap;
	char	*s;

	va_start(ap, fmt);
	if (vasprintf(&s, fmt, ap) == -1)
		abort();
	va_end(ap);

	close_with_err(req, s);
	free(s);
}

static void
net_tls_handshake(int fd, short event, void *d)
{
	struct req	*req = d;
	const char	*hash;

	if (event == EV_TIMEOUT) {
		close_with_err(req, "Timeout loading page");
		return;
	}

	switch (tls_handshake(req->ctx)) {
	case TLS_WANT_POLLIN:
		yield_r(req, net_tls_handshake, NULL);
		return;
	case TLS_WANT_POLLOUT:
		yield_w(req, net_tls_handshake, NULL);
		return;
	}

	hash = tls_peer_cert_hash(req->ctx);
	if (hash == NULL) {
		close_with_errf(req, "handshake failed: %s",
		    tls_error(req->ctx));
		return;
	}
	net_send_ui(IMSG_CHECK_CERT, req->id, hash, strlen(hash)+1);
}

static void
net_tls_readcb(int fd, short event, void *d)
{
	struct bufferevent	*bufev = d;
	struct req		*req = bufev->cbarg;
	char			 buf[IBUF_READ_SIZE];
	int			 what = EVBUFFER_READ;
	int			 howmuch = IBUF_READ_SIZE;
	int			 res;
	ssize_t			 ret;
	size_t			 len;

	if (event == EV_TIMEOUT) {
		what |= EVBUFFER_TIMEOUT;
		goto err;
	}

	if (bufev->wm_read.high != 0)
		howmuch = MIN(sizeof(buf), bufev->wm_read.high);

	switch (ret = tls_read(req->ctx, buf, howmuch)) {
	case TLS_WANT_POLLIN:
	case TLS_WANT_POLLOUT:
		goto retry;
	case -1:
		what |= EVBUFFER_ERROR;
		goto err;
	}
	len = ret;

	if (len == 0) {
		what |= EVBUFFER_EOF;
		goto err;
	}

	res = evbuffer_add(bufev->input, buf, len);
	if (res == -1) {
		what |= EVBUFFER_ERROR;
		goto err;
	}

	event_add(&bufev->ev_read, NULL);

	len = EVBUFFER_LENGTH(bufev->input);
	if (bufev->wm_read.low != 0 && len < bufev->wm_read.low)
		return;

	if (bufev->readcb != NULL)
		(*bufev->readcb)(bufev, bufev->cbarg);
	return;

retry:
	event_add(&bufev->ev_read, NULL);
	return;

err:
	(*bufev->errorcb)(bufev, what, bufev->cbarg);
}

static void
net_tls_writecb(int fd, short event, void *d)
{
	struct bufferevent	*bufev = d;
	struct req		*req = bufev->cbarg;
	ssize_t			 ret;
	size_t			 len;
	short			 what = EVBUFFER_WRITE;

	if (event & EV_TIMEOUT) {
		what |= EVBUFFER_TIMEOUT;
		goto err;
	}

	if (EVBUFFER_LENGTH(bufev->output) != 0) {
		ret = tls_write(req->ctx, EVBUFFER_DATA(bufev->output),
		    EVBUFFER_LENGTH(bufev->output));
		switch (ret) {
		case TLS_WANT_POLLIN:
		case TLS_WANT_POLLOUT:
			goto retry;
		case -1:
			what |= EVBUFFER_ERROR;
			goto err;
		}
		len = ret;

		evbuffer_drain(bufev->output, len);
	}

	if (EVBUFFER_LENGTH(bufev->output) != 0)
		event_add(&bufev->ev_write, NULL);

	if (bufev->writecb != NULL &&
	    EVBUFFER_LENGTH(bufev->output) <= bufev->wm_write.low)
		(*bufev->writecb)(bufev, bufev->cbarg);
	return;

retry:
	event_add(&bufev->ev_write, NULL);
	return;

err:
	(*bufev->errorcb)(bufev, what, bufev->cbarg);
}

static int
gemini_parse_reply(struct req *req, const char *header, size_t len)
{
	int		 code;
	const char	*t;

	if (len < 4)
		return 0;

	if (!isdigit(header[0]) || !isdigit(header[1]))
		return 0;

	code = (header[0] - '0')*10 + (header[1] - '0');
	if (header[2] != ' ')
		return 0;

	t = header + 3;

	net_send_ui(IMSG_GOT_CODE, req->id, &code, sizeof(code));
	net_send_ui(IMSG_GOT_META, req->id, t, strlen(t)+1);

	bufferevent_disable(req->bev, EV_READ|EV_WRITE);

	return code;
}

/* called when we're ready to read/write */
static void
net_ready(struct req *req)
{
	req->bev = bufferevent_new(req->fd, net_read, net_write, net_error,
	    req);
	if (req->bev == NULL)
		die();

#if HAVE_EVENT2
	evbuffer_unfreeze(req->bev->input, 0);
	evbuffer_unfreeze(req->bev->output, 1);
#endif

	/* setup tls i/o layer */
	if (req->ctx != NULL) {
		event_set(&req->bev->ev_read, req->fd, EV_READ,
		    net_tls_readcb, req->bev);
		event_set(&req->bev->ev_write, req->fd, EV_WRITE,
		    net_tls_writecb, req->bev);
	}

	/* TODO: adjust watermarks */
	bufferevent_setwatermark(req->bev, EV_WRITE, 1, 0);
	bufferevent_setwatermark(req->bev, EV_READ,  1, 0);

	bufferevent_enable(req->bev, EV_READ|EV_WRITE);

	bufferevent_write(req->bev, req->req, req->len);
}

/* called after a read has been done */
static void
net_read(struct bufferevent *bev, void *d)
{
	struct req	*req = d;
	struct evbuffer	*src = EVBUFFER_INPUT(bev);
	uint8_t		*data;
	size_t		 len, chunk;
	int		 r;
	char		*header;

	if (!req->done_header) {
		header = evbuffer_readln(src, &len, EVBUFFER_EOL_CRLF_STRICT);
		if (header == NULL && EVBUFFER_LENGTH(src) >= 1024)
			goto err;
		if (header == NULL)
			return;
		r = gemini_parse_reply(req, header, len);
		free(header);
		req->done_header = 1;
		if (r == 0)
			goto err;
		else if (r < 20 || r >= 30)
			close_conn(0, 0, req);
		return;
	}

	if ((len = EVBUFFER_LENGTH(src)) == 0)
		return;
	data = EVBUFFER_DATA(src);

	/*
	 * Split data into chunks before sending.  imsg can't handle
	 * message that are "too big".
	 */
	while (len != 0) {
		chunk = MIN(len, 4096);
		net_send_ui(IMSG_BUF, req->id, data, chunk);
		data += chunk;
		len -= chunk;
	}

	evbuffer_drain(src, EVBUFFER_LENGTH(src));
	return;

err:
	(*bev->errorcb)(bev, EVBUFFER_READ, bev->cbarg);
}

/* called after a write has been done */
static void
net_write(struct bufferevent *bev, void *d)
{
	struct evbuffer	*dst = EVBUFFER_OUTPUT(bev);

	if (EVBUFFER_LENGTH(dst) == 0)
		(*bev->errorcb)(bev, EVBUFFER_WRITE, bev->cbarg);
}

static void
net_error(struct bufferevent *bev, short error, void *d)
{
	struct req	*req = d;
	struct evbuffer	*src;

	if (error & EVBUFFER_TIMEOUT) {
		close_with_err(req, "Timeout loading page");
		return;
	}

	if (error & EVBUFFER_ERROR) {
		close_with_err(req, "buffer event error");
		return;
	}

	if (error & EVBUFFER_EOF) {
		/* EOF and no header */
		if (!req->done_header) {
			close_with_err(req, "protocol error");
			return;
		}

		src = EVBUFFER_INPUT(req->bev);
		if (EVBUFFER_LENGTH(src) != 0)
			net_send_ui(IMSG_BUF, req->id, EVBUFFER_DATA(src),
			    EVBUFFER_LENGTH(src));
		net_send_ui(IMSG_EOF, req->id, NULL, 0);
		close_conn(0, 0, req);
		return;
	}

	if (error & EVBUFFER_WRITE) {
		/* finished sending request */
		bufferevent_disable(bev, EV_WRITE);
		return;
	}

	if (error & EVBUFFER_READ) {
		close_with_err(req, "protocol error");
		return;
	}

	close_with_errf(req, "unknown event error %x", error);
}

static void
handle_get_raw(struct imsg *imsg, size_t datalen)
{
	struct req	*req;
	struct get_req	*r;

	r = imsg->data;

	if (datalen != sizeof(*r))
		die();

	if ((req = calloc(1, sizeof(*req))) == NULL)
		die();

	req->id = imsg->hdr.peerid;
	TAILQ_INSERT_HEAD(&reqhead, req, reqs);

	strlcpy(req->url.host, r->host, sizeof(req->url.host));
	strlcpy(req->url.port, r->port, sizeof(req->url.port));

	strlcpy(req->req, r->req, sizeof(req->req));
	req->len = strlen(r->req);

	req->proto = r->proto;

#if HAVE_ASR_RUN
	async_conn_towards(req);
#else
	blocking_conn_towards(req);
#endif
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
		net_ready(req);
	else
		close_conn(0, 0, req);
}

static void
handle_proceed(struct imsg *imsg, size_t datalen)
{
	struct req *req;

	if ((req = req_by_id(imsg->hdr.peerid)) == NULL)
		return;

	bufferevent_enable(req->bev, EV_READ);
}

static void
handle_stop(struct imsg *imsg, size_t datalen)
{
	struct req	*req;

	if ((req = req_by_id(imsg->hdr.peerid)) == NULL)
		return;
	close_conn(0, 0, req);
}

static void
handle_quit(struct imsg *imsg, size_t datalen)
{
	event_loopbreak();
}

static void
handle_dispatch_imsg(int fd, short ev, void *d)
{
	struct imsgev	*iev = d;

	if (dispatch_imsg(iev, ev, handlers, sizeof(handlers)) == -1)
		err(1, "connection closed");
}

static int
net_send_ui(int type, uint32_t peerid, const void *data,
    uint16_t datalen)
{
	return imsg_compose_event(iev_ui, type, peerid, 0, -1,
	    data, datalen);
}

int
net_main(void)
{
	setproctitle("net");

	TAILQ_INIT(&reqhead);

	event_init();

	/* Setup pipe and event handler to the main process */
	if ((iev_ui = malloc(sizeof(*iev_ui))) == NULL)
		die();
	imsg_init(&iev_ui->ibuf, 3);
	iev_ui->handler = handle_dispatch_imsg;
	iev_ui->events = EV_READ;
	event_set(&iev_ui->ev, iev_ui->ibuf.fd, iev_ui->events,
	    iev_ui->handler, iev_ui);
	event_add(&iev_ui->ev, NULL);

	sandbox_net_process();

	event_dispatch();

	msgbuf_clear(&iev_ui->ibuf.w);
	close(iev_ui->ibuf.fd);
	free(iev_ui);

	return 0;
}
