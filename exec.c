/*
 * Copyright (c) 2024 Omar Polo <op@omarpolo.com>
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

#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "exec.h"
#include "minibuffer.h"
#include "ui.h"

#define TMPFILE "/tmp/telescope.XXXXXXXXXX"

int
exec_cmd(char **argv, enum exec_mode mode)
{
	char	**t;
	int 	 s, fd, ret;
	pid_t 	 pid;

	if (argv == NULL)
		return (-1);

	if (mode == EXEC_FOREGROUND) {
		ui_suspend();

		fprintf(stderr, "%s: running", getprogname());
		for (t = argv; *t; ++t)
			fprintf(stderr, " %s", *t);
		fprintf(stderr, "\n");
		fflush(NULL);
	}

	switch (pid = fork()) {
	case -1:
		message("failed to fork: %s", strerror(errno));
		return (-1);
	case 0:
		if (mode == EXEC_BACKGROUND) {
			if ((fd = open("/dev/null", O_RDWR)) == -1) {
				warn("can't open /dev/null");
				_exit(1);
			}
			(void)dup2(fd, 0);
			(void)dup2(fd, 1);
			(void)dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
		execvp(argv[0], argv);
		if (mode == EXEC_FOREGROUND) {
			warn("can't exec \"%s\"", argv[0]);
			fprintf(stderr, "Press enter to continue");
			fflush(stderr);
			read(0, &s, 1);
		}
		_exit(1);
	}

	if (mode == EXEC_BACKGROUND)
		return (0);

	do {
		ret = waitpid(pid, &s, 0);
	} while (ret == -1 && errno == EINTR);

	ui_resume();

	if (WIFSIGNALED(s) || WEXITSTATUS(s) != 0) {
		message("%s failed", *argv);
		return (-1);
	}

	return (0);
}

FILE *
exec_editor(void *data, size_t len)
{
	FILE		*fp;
	char		*editor;
	char		 sfn[sizeof(TMPFILE)];
	char		*argv[3];
	int		 fd;

	strlcpy(sfn, TMPFILE, sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1) {
		message("failed to create a temp file: %s", strerror(errno));
		return (NULL);
	}
	(void) write(fd, data, len);
	close(fd);

	if ((editor = getenv("VISUAL")) == NULL &&
	    (editor = getenv("EDITOR")) == NULL)
		editor = (char *)DEFAULT_EDITOR;

	argv[0] = editor;
	argv[1] = sfn;
	argv[2] = NULL;

	if (exec_cmd(argv, EXEC_FOREGROUND) == -1) {
		(void) unlink(sfn);
		return (NULL);
	}

	if ((fp = fopen(sfn, "r")) == NULL) {
		message("can't open temp file!");
		(void) unlink(sfn);
		return (NULL);
	}
	(void) unlink(sfn);

	return (fp);
}
