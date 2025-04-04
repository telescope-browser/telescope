/*	$OpenBSD: control.c,v 1.4 2021/08/01 09:07:03 florian Exp $	*/

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "control.h"
#include "ev.h"
#include "imsgev.h"
#include "minibuffer.h"
#include "telescope.h"
#include "utils.h"
#include "ui.h"
#include "xwrapper.h"

#define	CONTROL_BACKLOG	5

struct {
	unsigned long	timeout;
	int		fd;
} control_state = {.fd = -1};

struct ctl_conn {
	TAILQ_ENTRY(ctl_conn)	entry;
	struct imsgev		iev;
};

struct ctl_conn	*control_connbyfd(int);
struct ctl_conn	*control_connbypid(pid_t);
void		 control_close(int);

TAILQ_HEAD(ctl_conns, ctl_conn) ctl_conns = TAILQ_HEAD_INITIALIZER(ctl_conns);

int
control_init(char *path)
{
	struct sockaddr_un	 sun;
	int			 fd;
	mode_t			 old_umask;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		warn("%s: socket", __func__);
		return (-1);
	}

	if (!mark_nonblock_cloexec(fd)) {
		close(fd);
		return (-1);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, path, sizeof(sun.sun_path));

	if (unlink(path) == -1)
		if (errno != ENOENT) {
			warn("%s: unlink %s", __func__, path);
			close(fd);
			return (-1);
		}

	old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		warn("%s: bind: %s", __func__, path);
		close(fd);
		umask(old_umask);
		return (-1);
	}
	umask(old_umask);

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
		warn("%s: chmod", __func__);
		close(fd);
		(void)unlink(path);
		return (-1);
	}

	return (fd);
}

int
control_listen(int fd)
{
	control_state.fd = fd;
	if (listen(control_state.fd, CONTROL_BACKLOG) == -1) {
		warn("%s: listen", __func__);
		return (-1);
	}

	ev_add(control_state.fd, EV_READ, control_accept, NULL);
	return (0);
}

void
control_accept(int listenfd, int event, void *bula)
{
	int			 connfd;
	socklen_t		 len;
	struct sockaddr_un	 sun;
	struct ctl_conn		*c;

	ev_add(control_state.fd, EV_READ, control_accept, NULL);
	if ((event & EV_TIMEOUT))
		return;

	len = sizeof(sun);
	if ((connfd = accept(listenfd, (struct sockaddr *)&sun, &len)) == -1) {
		/*
		 * Pause accept if we are out of file descriptors, or
		 * ev will haunt us here too.
		 */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			ev_del(control_state.fd);
			control_state.timeout = ev_timer(&evtpause,
			    control_accept, NULL);
		} else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			message("%s: accept4: %s", __func__, strerror(errno));
		return;
	}

	if (!mark_nonblock_cloexec(connfd)) {
		message("%s: mark_nonblock_cloexec: %s", __func__,
		    strerror(errno));
		close(connfd);
		return;
	}

	c = xcalloc(1, sizeof(struct ctl_conn));

	if (imsgbuf_init(&c->iev.ibuf, connfd) == -1) {
		message("%s: ev_add: %s", __func__, strerror(errno));
		close(connfd);
		free(c);
		return;
	}

	c->iev.handler = control_dispatch_imsg;
	c->iev.events = EV_READ;
	if (ev_add(connfd, c->iev.events, c->iev.handler, &c->iev) == -1) {
		message("%s: ev_add: %s", __func__, strerror(errno));
		close(connfd);
		free(c);
		return;
	}

	TAILQ_INSERT_TAIL(&ctl_conns, c, entry);
}

struct ctl_conn *
control_connbyfd(int fd)
{
	struct ctl_conn	*c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (c->iev.ibuf.fd == fd)
			break;
	}

	return (c);
}

struct ctl_conn *
control_connbypid(pid_t pid)
{
	struct ctl_conn	*c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (c->iev.ibuf.pid == pid)
			break;
	}

	return (c);
}

void
control_close(int fd)
{
	struct ctl_conn	*c;

	if ((c = control_connbyfd(fd)) == NULL) {
		message("%s: fd %d: not found", __func__, fd);
		return;
	}

	ev_del(c->iev.ibuf.fd);
	close(c->iev.ibuf.fd);

	imsgbuf_clear(&c->iev.ibuf);
	TAILQ_REMOVE(&ctl_conns, c, entry);

	free(c);

	/* Some file descriptors are available again. */
	if (ev_timer_pending(control_state.timeout)) {
		ev_timer_cancel(control_state.timeout);
		control_state.timeout = 0;
		ev_add(control_state.fd, EV_READ, control_accept, NULL);
	}
}

void
control_dispatch_imsg(int fd, int event, void *bula)
{
	struct ctl_conn	*c;
	struct imsg	 imsg;
	ssize_t		 n;

	if ((c = control_connbyfd(fd)) == NULL) {
		message("%s: fd %d: not found", __func__, fd);
		return;
	}

	if (event & EV_READ) {
		if (imsgbuf_read(&c->iev.ibuf) == -1) {
			control_close(fd);
			return;
		}
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(&c->iev.ibuf) == -1) {
			control_close(fd);
			return;
		}
	}

	for (;;) {
		if ((n = imsg_get(&c->iev.ibuf, &imsg)) == -1) {
			control_close(fd);
			return;
		}
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_CTL_OPEN_URL: {
			static char uri[GEMINI_URL_LEN];

			if (IMSG_DATA_SIZE(imsg) >= sizeof(uri))
				break;
			memset(uri, 0, sizeof(uri));
			memcpy(uri, imsg.data, sizeof(uri));
			if (uri[IMSG_DATA_SIZE(imsg)-1] != '\0')
				break;

			ui_remotely_open_link(uri);
			break;
		}
		default:
			message("%s: error handling imsg %d", __func__,
			    imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}

	imsg_event_add(&c->iev);
}
