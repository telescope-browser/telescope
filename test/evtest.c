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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <poll.h>

#include "ev.h"

int	 fired_a;
int	 fired_b;
int	 fired_c;

struct timeval	 tv_a = { 0, 250000 };
struct timeval	 tv_b = { 0, 300000 };
struct timeval	 tv_c = { 0, 350000 };

unsigned long	 tout_a;
unsigned long	 tout_b;
unsigned long	 tout_c;

static void
pipe_ev(int fd, int ev, void *data)
{
	warn("shouldn't have happened!");
	abort();
}

static void
timeout_cb(int fd, int ev, void *data)
{
	int	*d = data;

	assert(fd == -1);
	*d = 1;
}

static void
timeout_cancel_b(int fd, int ev, void *data)
{
	timeout_cb(fd, ev, data);
	ev_timer_cancel(tout_b);
}

static void
timeout_quit(int fd, int ev, void *data)
{
	timeout_cb(fd, ev, data);
	ev_break();
}

int
main(void)
{
	int	 p[2];

	alarm(2); /* safety net */

	/* the ev subsystem needs at least a file descriptor */
	if (pipe(p) == -1)
		err(1, "pipe");

	if (ev_init() == -1)
		err(1, "ev_init");

	if (ev_add(p[0], POLLIN, pipe_ev, NULL) == -1)
		err(1, "ev_add");

	if ((tout_c = ev_timer(&tv_c, timeout_quit, &fired_c)) == 0 ||
	    (tout_b = ev_timer(&tv_b, timeout_cb, &fired_b)) == 0 ||
	    (tout_a = ev_timer(&tv_a, timeout_cancel_b, &fired_a)) == 0)
		err(1, "ev_timer");

	ev_loop();

	if (fired_a && !fired_b && fired_c)
		return 0;

	errx(1, "events fired not as expected: a:%d b:%d c:%d",
	    fired_a, fired_b, fired_c);
}
