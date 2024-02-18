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

#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ev.h"

struct evcb {
	void		(*cb)(int, int, void *);
	void		*udata;
};

struct evtimer {
	unsigned int	 id;
	struct timeval	 tv;
	struct evcb	 cb;
};

struct evbase {
	size_t		 len;

	struct pollfd	*pfds;
	size_t		 pfdlen;

	struct evcb	*cbs;
	size_t		 cblen;

	int		 sigpipe[2];
	struct evcb	 sigcb;

	unsigned int	 tid;
	struct evtimer	*timers;
	size_t		 ntimers;
	size_t		 timerscap;
};

static struct evbase	*base;
static int		 ev_stop;

static int
ev_resize(size_t len)
{
	void	*t;
	size_t	 i;

	t = recallocarray(base->pfds, base->pfdlen, len, sizeof(*base->pfds));
	if (t == NULL)
		return -1;
	base->pfds = t;
	base->pfdlen = len;

	for (i = base->len; i < len; ++i)
		base->pfds[i].fd = -1;

	t = recallocarray(base->cbs, base->cblen, len, sizeof(*base->cbs));
	if (t == NULL)
		return -1;
	base->cbs = t;
	base->cblen = len;

	base->len = len;
	return 0;
}

int
ev_init(void)
{
	if (base != NULL) {
		errno = EINVAL;
		return -1;
	}

	if ((base = calloc(1, sizeof(*base))) == NULL)
		return -1;

	base->sigpipe[0] = -1;
	base->sigpipe[1] = -1;

	if (ev_resize(16) == -1) {
		free(base->pfds);
		free(base->cbs);
		free(base);
		base = NULL;
		return -1;
	}

	return 0;
}

static inline int
ev2poll(int ev)
{
	int	 ret = 0;

	if (ev & EV_READ)
		ret |= POLLIN;
	if (ev & EV_WRITE)
		ret |= POLLOUT;

	return (ret);
}

int
ev_add(int fd, int ev, void (*cb)(int, int, void *), void *udata)
{
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}

	if ((size_t)fd >= base->len) {
		if (ev_resize(fd + 1) == -1)
			return -1;
	}

	base->pfds[fd].fd = fd;
	base->pfds[fd].events = ev2poll(ev);
	base->pfds[fd].revents = 0;

	base->cbs[fd].cb = cb;
	base->cbs[fd].udata = udata;

	return 0;
}

static void
ev_sigcatch(int signo)
{
	unsigned char	 s;
	int		 err;

	err = errno;

	/*
	 * We should be able to write up to PIPE_BUF bytes without
	 * blocking.
	 */
	s = signo;
	(void) write(base->sigpipe[1], &s, sizeof(s));

	errno = err;
}

static void
ev_sigdispatch(int fd, int ev, void *data)
{
	unsigned char	 signo;

	if (read(fd, &signo, sizeof(signo)) != sizeof(signo))
		return;

	base->sigcb.cb(signo, EV_SIGNAL, base->sigcb.udata);
}

int
ev_signal(int sig, void (*cb)(int, int, void *), void *udata)
{
	int		 flags;

	if (base->sigpipe[0] == -1) {
		/* pipe2(2) is not available everywhere... sigh */
		if (pipe(base->sigpipe) == -1)
			return -1;

		if ((flags = fcntl(base->sigpipe[1], F_GETFL)) == -1 ||
		    fcntl(base->sigpipe[1], F_SETFL, flags | O_NONBLOCK) == -1)
			return -1;

		if (ev_add(base->sigpipe[0], EV_READ, ev_sigdispatch, NULL)
		    == -1)
			return -1;
	}

	base->sigcb.cb = cb;
	base->sigcb.udata = udata;

	signal(sig, ev_sigcatch);
	return 0;
}

static inline void
bubbleup(size_t i)
{
	struct evtimer	 tmp;
	size_t		 p;

	for (;;) {
		if (i == 0)
			return;

		p = (i - 1) / 2;
		if (timercmp(&base->timers[p].tv, &base->timers[i].tv, <))
		    	return;

		/* swap */
		memcpy(&tmp, &base->timers[p], sizeof(tmp));
		memcpy(&base->timers[p], &base->timers[i], sizeof(tmp));
		memcpy(&base->timers[i], &tmp, sizeof(tmp));
		i = p;
	}
}

unsigned int
ev_timer(const struct timeval *tv, void (*cb)(int, int, void*), void *udata)
{
	struct evtimer	*evt;
	void		*t;
	size_t		 newcap;
	unsigned int	 nextid;

	if (tv == NULL) {
		errno = EINVAL;
		return 0;
	}

	if (base->ntimers == base->timerscap) {
		newcap = base->timerscap + 8;
		t = recallocarray(base->timers, base->timerscap, newcap,
		    sizeof(*base->timers));
		if (t == NULL)
			return 0;
		base->timers = t;
		base->timerscap = newcap;
	}

	if ((nextid = ++base->tid) == 0)
		nextid = ++base->tid;

	evt = &base->timers[base->ntimers];
	evt->id = nextid;
	memcpy(&evt->tv, tv, sizeof(*tv));
	evt->cb.cb = cb;
	evt->cb.udata = udata;

	bubbleup(base->ntimers);
	base->ntimers++;

	return (nextid);
}

int
ev_timer_pending(unsigned int id)
{
	size_t		 i;

	for (i = 0; i < base->ntimers; ++i) {
		if (base->timers[i].id == id)
			return (1);
	}

	return (0);
}

static void
bubbledown(size_t i)
{
	struct timeval	 tmp;
	size_t		 l, r, s;

	for (;;) {
		l = 2 * i + 1;
		r = 2 * i + 2;

		/* base case: there are no children */
		if (l > base->ntimers)
			return;

		/* find the smaller child */
		s = r;
		if (r > base->ntimers ||
		    timercmp(&base->timers[l].tv, &base->timers[r].tv, <))
			s = l;

		/* other base case: it's at the right place */
		if (timercmp(&base->timers[i].tv, &base->timers[s].tv, <))
			return;

		/* swap */
		memcpy(&tmp, &base->timers[s], sizeof(tmp));
		memcpy(&base->timers[s], &base->timers[i], sizeof(tmp));
		memcpy(&base->timers[i], &tmp, sizeof(tmp));

		i = s;
	}
}

static inline void
cancel_timer(size_t i)
{
	/* special case: it's the last one */
	if (i == base->ntimers - 1) {
		base->ntimers--;
		memset(&base->timers[base->ntimers], 0, sizeof(*base->timers));
		return;
	}

	memcpy(&base->timers[i], &base->timers[base->ntimers - 1],
	    sizeof(*base->timers));
	base->ntimers--;

	bubbledown(i);
}

int
ev_timer_cancel(unsigned int id)
{
	size_t		 i;

	for (i = 0; i < base->ntimers; ++i) {
		if (base->timers[i].id == id)
			break;
	}

	if (i == base->ntimers)
		return -1;

	cancel_timer(i);
	return (0);
}

int
ev_del(int fd)
{
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}

	if ((size_t)fd >= base->len) {
		errno = ERANGE;
		return -1;
	}

	base->pfds[fd].fd = -1;
	base->pfds[fd].events = 0;

	base->cbs[fd].cb = NULL;
	base->cbs[fd].udata = NULL;

	return 0;
}

static inline int
poll2ev(int ev)
{
	int r = 0;

	if (ev & (POLLIN|POLLHUP))
		r |= EV_READ;
	if (ev & (POLLOUT|POLLWRNORM|POLLWRBAND))
		r |= EV_WRITE;

	return (r);
}

int
ev_loop(void)
{
	struct timespec	 elapsed, beg, end, min, *wait;
	struct timeval	 tv, sub;
	int		 n;
	size_t		 i;

	while (!ev_stop) {
		wait = NULL;
		if (base->ntimers) {
			TIMEVAL_TO_TIMESPEC(&base->timers[0].tv, &min);
			wait = &min;
		}

		clock_gettime(CLOCK_MONOTONIC, &beg);
		if ((n = ppoll(base->pfds, base->len, wait, NULL)) == -1) {
			if (errno != EINTR)
				return -1;
		}

		if (n == 0)
			memcpy(&elapsed, &min, sizeof(min));
		else {
			clock_gettime(CLOCK_MONOTONIC, &end);
			timespecsub(&end, &beg, &elapsed);
		}

		TIMESPEC_TO_TIMEVAL(&tv, &elapsed);

		for (i = 0; i < base->ntimers && !ev_stop; /* nop */) {
			timersub(&base->timers[i].tv, &tv, &sub);
			if (sub.tv_sec <= 0) {
				base->timers[i].cb.cb(-1, EV_TIMEOUT,
				    base->timers[i].cb.udata);
				cancel_timer(i);
				continue;
			}

			memcpy(&base->timers[i].tv, &sub, sizeof(sub));
			i++;
		}

		for (i = 0; i < base->len && n > 0 && !ev_stop; ++i) {
			if (base->pfds[i].fd == -1)
				continue;
			if (base->pfds[i].revents & (POLLIN|POLLOUT|POLLHUP)) {
				n--;
				base->cbs[i].cb(base->pfds[i].fd,
				    poll2ev(base->pfds[i].revents),
				    base->cbs[i].udata);
			}
		}
	}

	return 0;
}

void
ev_break(void)
{
	ev_stop = 1;
}
