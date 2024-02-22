/*
 * Copyright (c) 2021, 2024 Omar Polo <op@omarpolo.com>
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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

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

#include "bufio.h"
#include "ev.h"
#include "imsgev.h"
#include "telescope.h"
#include "utils.h"

static struct imsgev		*iev_ui;

enum conn_state {
	CONN_CONNECTING,
	CONN_HANDSHAKE,
	CONN_HEADER,
	CONN_BODY,
	CONN_CLOSE,
	CONN_ERROR,
};

/* a pending request */
struct req {
	uint32_t		 id;
	enum conn_state		 state;
	int			 proto;
	int			 fd;
	char			*host;
	char			*port;
	char			*req;
	size_t			 len;
	void			*ccert;
	size_t			 ccert_len;
	int			 ccert_fd;

	int			 eof;
	unsigned int		 timer;
	struct bufio		 bio;

	int			 conn_error;
	const char		*cause;

	struct addrinfo		*servinfo, *p;
#if HAVE_ASR_RUN
	struct asr_query	*q;
	int			 ar_fd;
#endif

	TAILQ_ENTRY(req)	 reqs;
};

static struct req	*req_by_id(uint32_t);

static void	 die(void) __attribute__((__noreturn__));

static void	 close_with_err(struct req*, const char*);
static void	 close_with_errf(struct req*, const char*, ...)
    __attribute__((format(printf, 2, 3)));

static int	 try_to_connect(struct req *);
static int	 gemini_parse_reply(struct req *, const char *, size_t);
static void	 net_ev(int, int, void *);
static void	 handle_dispatch_imsg(int, int, void*);

static int	 net_send_ui(int, uint32_t, const void *, uint16_t);

/* TODO: making this customizable */
struct timeval timeout_for_handshake = { 5, 0 };

TAILQ_HEAD(, req) reqhead;

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
close_conn(int fd, int ev, void *d)
{
	struct req	*req = d;

	if (req->state != CONN_ERROR)
		req->state = CONN_CLOSE;

#if HAVE_ASR_RUN
	if (req->q) {
		asr_abort(req->q);
		ev_del(req->ar_fd);
	}
#endif

	if (req->timer != 0) {
		ev_timer_cancel(req->timer);
		req->timer = 0;
	}

	if (req->state == CONN_CLOSE &&
	    req->fd != -1 &&
	    bufio_close(&req->bio) == -1 &&
	    errno == EAGAIN) {
		ev_add(req->fd, bufio_ev(&req->bio), close_conn, req);
		return;
	}

	if (req->servinfo)
		freeaddrinfo(req->servinfo);

	bufio_free(&req->bio);

	if (req->ccert != NULL) {
		munmap(req->ccert, req->ccert_len);
		close(req->ccert_fd);
	}

	free(req->host);
	free(req->port);
	free(req->req);

	TAILQ_REMOVE(&reqhead, req, reqs);
	if (req->fd != -1) {
		ev_del(req->fd);
		close(req->fd);
	}
	free(req);
}

static void
close_with_err(struct req *req, const char *err)
{
	req->state = CONN_ERROR;
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

#if HAVE_ASR_RUN
static void
req_resolve(int fd, int ev, void *d)
{
	struct req		*req = d;
	struct addrinfo		 hints;
	struct asr_result	 ar;
	struct timeval		 tv;

	if (req->q == NULL) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		req->q = getaddrinfo_async(req->host, req->port, &hints, NULL);
		if (req->q == NULL) {
			close_with_errf(req, "getaddrinfo_async: %s",
			    strerror(errno));
			return;
		}
	}

	if (fd != -1)
		ev_del(fd);
	if (req->timer) {
		ev_timer_cancel(req->timer);
		req->timer = 0;
	}

	if (asr_run(req->q, &ar) == 0) {
		ev = 0;
		if (ar.ar_cond & ASR_WANT_READ)
			ev |= EV_READ;
		if (ar.ar_cond & ASR_WANT_WRITE)
			ev |= EV_WRITE;

		req->ar_fd = ar.ar_fd;
		if (ev_add(req->ar_fd, ev, req_resolve, req) == -1) {
			close_with_errf(req, "ev_add failure: %s",
			    strerror(errno));
			return;
		}

		tv.tv_sec = ar.ar_timeout / 1000;
		tv.tv_usec = (ar.ar_timeout % 1000) * 1000;
		req->timer = ev_timer(&tv, req_resolve, req);
		if (req->timer == 0)
			close_with_errf(req, "ev_timer failure: %s",
			    strerror(errno));
		return;
	}

	req->ar_fd = -1;
	req->q = NULL;

	if (ar.ar_gai_errno) {
		close_with_errf(req, "failed to resolve %s: %s",
		    req->host, gai_strerror(ar.ar_gai_errno));
		return;
	}

	req->servinfo = ar.ar_addrinfo;

	req->fd = -1;
	req->p = req->servinfo;
	net_ev(-1, EV_READ, req);
}
#else
static void
req_resolve(int fd, int ev, struct req *req)
{
	struct addrinfo		 hints;
	int			 s;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	s = getaddrinfo(req->host, req->port, &hints, &req->servinfo);
	if (s != 0) {
		close_with_errf(req, "failed to resolve %s: %s",
		    req->host, gai_strerror(s));
		return;
	}

	req->fd = -1;
	req->p = req->servinfo;
	net_ev(-1, EV_READ, req);
}
#endif

static int
try_to_connect(struct req *req)
{
	int		 error;
	socklen_t	 len = sizeof(error);

 again:
	if (req->p == NULL)
		return (-1);

	if (req->fd != -1) {
		if (getsockopt(req->fd, SOL_SOCKET, SO_ERROR, &error,
		    &len) == -1) {
			req->conn_error = errno;
			req->cause = "getsockopt";
			return (-1);
		}

		if (error == 0) /* connected */
			return (0);

		req->conn_error = error;
		req->cause = "connect";
		close(req->fd);
		req->fd = -1;
		req->p = req->p->ai_next;
		goto again;
	}

	req->fd = socket(req->p->ai_family, req->p->ai_socktype,
	    req->p->ai_protocol);
	if (req->fd == -1) {
		req->conn_error = errno;
		req->cause = "socket";
		req->p = req->p->ai_next;
		goto again;
	}

	if (!mark_nonblock_cloexec(req->fd)) {
		req->conn_error = errno;
		req->cause = "setsockopt";
		return (-1);
	}

	if (connect(req->fd, req->p->ai_addr, req->p->ai_addrlen) == 0)
		return (0);
	errno = EAGAIN;
	return (-1);
}

static int
gemini_parse_reply(struct req *req, const char *header, size_t len)
{
	struct ibuf	*ibuf;
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
	len = strlen(t) + 1;

	if ((ibuf = imsg_create(&iev_ui->ibuf, IMSG_REPLY, req->id, 0,
	    sizeof(code) + len)) == NULL)
		die();
	if (imsg_add(ibuf, &code, sizeof(code)) == -1 ||
	    imsg_add(ibuf, t, len) == -1)
		die();
	imsg_close(&iev_ui->ibuf, ibuf);
	imsg_event_add(iev_ui);

	/* pause until we've told go go ahead */
	ev_del(req->fd);

	return code;
}

static inline int
net_send_req(struct req *req)
{
	return (bufio_compose(&req->bio, req->req, req->len));
}

static void
net_ev(int fd, int ev, void *d)
{
	static char	 buf[4096];
	struct req	*req = d;
	const char	*hash;
	ssize_t		 read;
	size_t		 len;
	char		*header, *endl;
	int		 r;

	if (ev == EV_TIMEOUT) {
		close_with_err(req, "Timeout loading page");
		return;
	}

	if (req->state == CONN_CONNECTING) {
		ev_del(req->fd);
		if (try_to_connect(req) == -1) {
			if (req->fd != -1 && errno == EAGAIN) {
				ev_add(req->fd, EV_WRITE, net_ev, req);
				return;
			}
			close_with_errf(req, "failed to connect to %s"
			    " (%s: %s)", req->host, req->cause,
			    strerror(req->conn_error));
			return;
		}

		bufio_set_fd(&req->bio, req->fd);

		switch (req->proto) {
		case PROTO_FINGER:
		case PROTO_GOPHER:
			/* finger and gopher don't have a header nor TLS */
			req->state = CONN_BODY;
			if (net_send_req(req) == -1) {
				close_with_err(req, "failed to send request");
				return;
			}
			break;
		case PROTO_GEMINI:
			req->state = CONN_HANDSHAKE;
			if (bufio_starttls(&req->bio, req->host, 1,
			    req->ccert, req->ccert_len,
			    req->ccert, req->ccert_len) == -1) {
				close_with_err(req, "failed to setup TLS");
				return;
			}
			req->timer = ev_timer(&timeout_for_handshake,
			    net_ev, req);
			if (req->timer == 0) {
				close_with_err(req, "failed to setup"
				    " handshake timer");
				return;
			}
			break;
		}
	}

	if (req->state == CONN_HANDSHAKE) {
		if (bufio_handshake(&req->bio) == -1 && errno == EAGAIN) {
			ev_add(req->fd, bufio_ev(&req->bio),
			    net_ev, req);
			return;
		}

		ev_timer_cancel(req->timer);
		req->timer = 0;

		req->state = CONN_HEADER;

		/* pause until we've told the certificate is OK */
		ev_del(req->fd);

		hash = tls_peer_cert_hash(req->bio.ctx);
		if (hash == NULL) {
			close_with_errf(req, "handshake failed: %s",
			    tls_error(req->bio.ctx));
			return;
		}

		net_send_ui(IMSG_CHECK_CERT, req->id, hash, strlen(hash)+1);
		return;
	}

	if (ev & EV_READ) {
		read = bufio_read(&req->bio);
		if (read == -1 && errno != EAGAIN) {			
			close_with_errf(req, "Read error");
			return;
		}
		if (read == 0)
			req->eof = 1;
	}

	if ((ev & EV_WRITE) && bufio_write(&req->bio) == -1 &&
	    errno != EAGAIN) {
		close_with_errf(req, "bufio_write: %s", strerror(errno));
		return;
	}

	if (req->state == CONN_HEADER) {
		header = req->bio.rbuf.buf;
		endl = memmem(header, req->bio.rbuf.len, "\r\n", 2);
		if (endl == NULL && req->bio.rbuf.len >= 1024) {
			close_with_err(req, "Invalid gemini reply (too long)");
			return;
		}
		if (endl == NULL && req->eof) {
			close_with_err(req, "Invalid gemini reply.");
			return;
		}
		if (endl == NULL) {
			ev_add(req->fd, bufio_ev(&req->bio), net_ev, req);
			return;
		}
		*endl = '\0';
		req->state = CONN_BODY;
		r = gemini_parse_reply(req, header, strlen(header));
		buf_drain(&req->bio.rbuf, endl - header + 2);
		if (r == 0) {
			close_with_err(req, "Malformed gemini reply");
			return;
		}
		if (r < 20 || r >= 30)
			close_conn(0, 0, req);
		return;
	}
	
	/*
	 * Split data into chunks before sending.  imsg can't handle
	 * message that are "too big".
	 */
	for (;;) {
		if ((len = bufio_drain(&req->bio, buf, sizeof(buf))) == 0)
			break;
		net_send_ui(IMSG_BUF, req->id, buf, len);
	}

	if (req->eof) {
		net_send_ui(IMSG_EOF, req->id, NULL, 0);
		close_conn(0, 0, req);
		return;
	}

	ev_add(req->fd, bufio_ev(&req->bio), net_ev, req);
}

static int
load_cert(struct imsg *imsg, struct req *req)
{
	struct stat	 sb;
	int		 fd;

	if ((fd = imsg_get_fd(imsg)) == -1)
		return (0);

	if (fstat(fd, &sb) == -1)
		return (-1);

#if 0
	if (sb.st_size >= (off_t)SIZE_MAX) {
		close(fd);
		return (-1);
	}
#endif

	req->ccert = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (req->ccert == MAP_FAILED) {
		req->ccert = NULL;
		close(fd);
		return (-1);
	}

	req->ccert_len = sb.st_size;
	req->ccert_fd = fd;

	return (0);
}

static void
handle_dispatch_imsg(int fd, int event, void *d)
{
	struct imsgev	*iev = d;
	struct imsgbuf	*ibuf = &iev->ibuf;
	struct imsg	 imsg;
	struct req	*req;
	struct get_req	 r;
	ssize_t		 n;
	int		 certok;

	if (event & EV_READ) {
		if ((n = imsg_read(ibuf)) == -1 && errno != EAGAIN)
			err(1, "imsg_read");
		if (n == 0)
			err(1, "connection closed");
	}
	if (event & EV_WRITE) {
		if ((n = msgbuf_write(&ibuf->w)) == -1 && errno != EAGAIN)
			err(1, "msgbuf_write");
		if (n == 0)
			err(1, "connection closed");
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			err(1, "imsg_get");
		if (n == 0)
			break;
		switch (imsg_get_type(&imsg)) {
		case IMSG_GET:
			if (imsg_get_data(&imsg, &r, sizeof(r)) == -1 ||
			    r.host[sizeof(r.host) - 1] != '\0' ||
			    r.port[sizeof(r.port) - 1] != '\0' ||
			    r.req[sizeof(r.req) - 1] != '\0')
				die();
			if (r.proto != PROTO_FINGER &&
			    r.proto != PROTO_GEMINI &&
			    r.proto != PROTO_GOPHER)
				die();

			if ((req = calloc(1, sizeof(*req))) == NULL)
				die();

			req->ccert_fd = -1;
			req->id = imsg_get_id(&imsg);
			TAILQ_INSERT_HEAD(&reqhead, req, reqs);

			if ((req->host = strdup(r.host)) == NULL)
				die();
			if ((req->port = strdup(r.port)) == NULL)
				die();
			if ((req->req = strdup(r.req)) == NULL)
				die();
			if (load_cert(&imsg, req) == -1)
				die();
			if (bufio_init(&req->bio) == -1)
				die();

			req->len = strlen(req->req);
			req->proto = r.proto;
			req_resolve(-1, 0, req);
			break;

		case IMSG_CERT_STATUS:
			if ((req = req_by_id(imsg_get_id(&imsg))) == NULL)
				break;

			if (imsg_get_data(&imsg, &certok, sizeof(certok)) ==
			    -1)
				die();
			if (!certok) {
				close_conn(0, 0, req);
				break;
			}

			if (net_send_req(req) == -1) {
				close_with_err(req, "failed to send request");
				break;
			}

			if (ev_add(req->fd, EV_WRITE, net_ev, req) == -1) {
				close_with_err(req,
				    "failed to register event.");
				break;
			}
			break;

		case IMSG_PROCEED:
			if ((req = req_by_id(imsg_get_id(&imsg))) == NULL)
				break;
			ev_add(req->fd, EV_READ, net_ev, req);
			net_ev(req->fd, 0, req);
			break;

		case IMSG_STOP:
			if ((req = req_by_id(imsg_get_id(&imsg))) == NULL)
				break;
			close_conn(0, 0, req);
			break;

		case IMSG_QUIT:
			ev_break();
			imsg_free(&imsg);
			return;

		default:
			errx(1, "got unknown imsg %d", imsg_get_type(&imsg));
		}

		imsg_free(&imsg);
	}

	imsg_event_add(iev);
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

	if (ev_init() == -1)
		exit(1);

	/* Setup pipe and event handler to the main process */
	if ((iev_ui = malloc(sizeof(*iev_ui))) == NULL)
		die();
	imsg_init(&iev_ui->ibuf, 3);
	iev_ui->handler = handle_dispatch_imsg;
	iev_ui->events = EV_READ;
	ev_add(iev_ui->ibuf.fd, iev_ui->events, iev_ui->handler, iev_ui);

	sandbox_net_process();

	ev_loop();

	msgbuf_clear(&iev_ui->ibuf.w);
	close(iev_ui->ibuf.fd);
	free(iev_ui);

	return 0;
}
