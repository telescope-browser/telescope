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
 * Handles the data in ~/.telescope
 *
 * TODO: add some form of locking on the files
 */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "telescope.h"

static void		 die(void) __attribute__((__noreturn__));
static void		 serve_bookmarks(uint32_t);
static void		 send_page(struct imsg *, const char *);
static void		 handle_get(struct imsg*, size_t);
static void		 handle_quit(struct imsg*, size_t);
static void		 handle_bookmark_page(struct imsg*, size_t);
static void		 handle_save_cert(struct imsg*, size_t);
static void		 handle_update_cert(struct imsg*, size_t);
static void		 handle_file_open(struct imsg*, size_t);
static void		 handle_session_start(struct imsg*, size_t);
static void		 handle_session_tab(struct imsg*, size_t);
static void		 handle_session_end(struct imsg*, size_t);
static void		 handle_dispatch_imsg(int, short, void*);
static int		 fs_send_ui(int, uint32_t, int, const void *, uint16_t);

static struct imsgev		*iev_ui;
static FILE			*session;

static char	bookmark_file[PATH_MAX];
static char	known_hosts_file[PATH_MAX], known_hosts_tmp[PATH_MAX];
static char	session_file[PATH_MAX];

static imsg_handlerfn *handlers[] = {
	[IMSG_GET] = handle_get,
	[IMSG_QUIT] = handle_quit,
	[IMSG_BOOKMARK_PAGE] = handle_bookmark_page,
	[IMSG_SAVE_CERT] = handle_save_cert,
	[IMSG_UPDATE_CERT] = handle_update_cert,
	[IMSG_FILE_OPEN] = handle_file_open,
	[IMSG_SESSION_START] = handle_session_start,
	[IMSG_SESSION_TAB] = handle_session_tab,
	[IMSG_SESSION_END] = handle_session_end,
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
	char		 buf[BUFSIZ];
	size_t		 r;
	FILE		*f;

	if ((f = fopen(bookmark_file, "r")) == NULL) {
		t = "# Bookmarks\n\n"
		    "No bookmarks yet!\n"
		    "Create ~/.telescope/bookmarks.gmi or use `bookmark-page'.\n";
		fs_send_ui(IMSG_BUF, peerid, -1, t, strlen(t));
		fs_send_ui(IMSG_EOF, peerid, -1, NULL, 0);
		return;
	}

	for (;;) {
		r = fread(buf, 1, sizeof(buf), f);
		fs_send_ui(IMSG_BUF, peerid, -1, buf, r);
		if (r != sizeof(buf))
			break;
	}

	fs_send_ui(IMSG_EOF, peerid, -1, NULL, 0);

	fclose(f);
}

static void
send_page(struct imsg *imsg, const char *page)
{
	fs_send_ui(IMSG_BUF, imsg->hdr.peerid, -1, page, strlen(page));
	fs_send_ui(IMSG_EOF, imsg->hdr.peerid, -1, NULL, 0);
}

static void
handle_get(struct imsg *imsg, size_t datalen)
{
	char		*data;
	const char	*p;

	data = imsg->data;

	if (data[datalen-1] != '\0')
		die();

	if (!strcmp(data, "about:about")) {
		send_page(imsg, about_about);
	} else if (!strcmp(data, "about:blank")) {
		send_page(imsg, about_blank);
	} else if (!strcmp(data, "about:bookmarks")) {
		serve_bookmarks(imsg->hdr.peerid);
	} else if (!strcmp(data, "about:help")) {
		send_page(imsg, about_help);
	} else if (!strcmp(data, "about:new")) {
		send_page(imsg, about_new);
	} else {
		p = "# not found!\n";
		fs_send_ui(IMSG_BUF, imsg->hdr.peerid, -1, p, strlen(p));
		fs_send_ui(IMSG_EOF, imsg->hdr.peerid, -1, NULL, 0);
	}
}

static void
handle_quit(struct imsg *imsg, size_t datalen)
{
	event_loopbreak();
}

static void
handle_bookmark_page(struct imsg *imsg, size_t datalen)
{
	char	*data;
	int	 res;
	FILE	*f;

	data = imsg->data;
	if (data[datalen-1] != '\0')
		die();

	if ((f = fopen(bookmark_file, "a")) == NULL) {
		res = errno;
		goto end;
	}
	fprintf(f, "=> %s\n", data);
	fclose(f);

	res = 0;
end:
	fs_send_ui(IMSG_BOOKMARK_OK, 0, -1, &res, sizeof(res));
}

static void
handle_save_cert(struct imsg *imsg, size_t datalen)
{
	struct tofu_entry	 e;
	FILE			*f;
	int			 res;

	/* TODO: traverse the file to avoid duplications? */

	if (datalen != sizeof(e))
		die();
	memcpy(&e, imsg->data, datalen);

	if ((f = fopen(known_hosts_file, "a")) == NULL) {
		res = errno;
		goto end;
	}
	fprintf(f, "%s %s %d\n", e.domain, e.hash, e.verified);
	fclose(f);

	res = 0;
end:
	fs_send_ui(IMSG_SAVE_CERT_OK, imsg->hdr.peerid, -1,
	    &res, sizeof(res));
}

static void
handle_update_cert(struct imsg *imsg, size_t datalen)
{
	FILE	*tmp, *f;
	struct	 tofu_entry entry;
	char	 sfn[PATH_MAX], *line = NULL, *t;
	size_t	 l, linesize = 0;
	ssize_t	 linelen;
	int	 fd, e, res = 0;

	if (datalen != sizeof(entry))
		die();
	memcpy(&entry, imsg->data, datalen);

	strlcpy(sfn, known_hosts_tmp, sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1 ||
	    (tmp = fdopen(fd, "w")) == NULL) {
		if (fd != -1) {
			unlink(sfn);
			close(fd);
		}
		res = 0;
		goto end;
	}

	if ((f = fopen(known_hosts_file, "r")) == NULL) {
		unlink(sfn);
		fclose(tmp);
                res = 0;
		goto end;
	}

	l = strlen(entry.domain);
	while ((linelen = getline(&line, &linesize, f)) != -1) {
		if ((t = strstr(line, entry.domain)) != NULL &&
		    (line[l] == ' ' || line[l] == '\t'))
			continue;
		/* line has a trailing \n */
		fprintf(tmp, "%s", line);
	}
	fprintf(tmp, "%s %s %d\n", entry.domain, entry.hash, entry.verified);

	free(line);
	e = ferror(tmp);

	fclose(tmp);
	fclose(f);

	if (e) {
		unlink(sfn);
		res = 0;
		goto end;
	}

	res = rename(sfn, known_hosts_file) != -1;

end:
	fs_send_ui(IMSG_UPDATE_CERT_OK, imsg->hdr.peerid, -1,
	    &res, sizeof(res));
}

static void
handle_file_open(struct imsg *imsg, size_t datalen)
{
	char	*path, *e;
	int	 fd;

	path = imsg->data;
	if (path[datalen-1] != '\0')
		die();

	if ((fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0644)) == -1) {
		e = strerror(errno);
		fs_send_ui(IMSG_FILE_OPENED, imsg->hdr.peerid, -1,
		    e, strlen(e)+1);
	} else
		fs_send_ui(IMSG_FILE_OPENED, imsg->hdr.peerid, fd,
		    NULL, 0);
}

static void
handle_session_start(struct imsg *imsg, size_t datalen)
{
	if (datalen != 0)
		die();

	if ((session = fopen(session_file, "w")) == NULL)
		die();
}

static void
handle_session_tab(struct imsg *imsg, size_t datalen)
{
	if (session == NULL)
		die();

	if (datalen == 0)
		die();

	/* skip the NUL-terminator */
        fwrite(imsg->data, 1, datalen-1, session);

	fprintf(session, "\n");
}

static void
handle_session_end(struct imsg *imsg, size_t datalen)
{
	if (session == NULL)
		die();
	fclose(session);
	session = NULL;
}

static void
handle_dispatch_imsg(int fd, short ev, void *d)
{
	struct imsgev	*iev = d;
	dispatch_imsg(iev, ev, handlers, sizeof(handlers));
}

static int
fs_send_ui(int type, uint32_t peerid, int fd, const void *data,
    uint16_t datalen)
{
	return imsg_compose_event(iev_ui, type, peerid, 0, fd,
	    data, datalen);
}

int
fs_init(void)
{
	char	dir[PATH_MAX];

	strlcpy(dir, getenv("HOME"), sizeof(dir));
	strlcat(dir, "/.telescope", sizeof(dir));
	mkdir(dir, 0700);

	strlcpy(bookmark_file, getenv("HOME"), sizeof(bookmark_file));
	strlcat(bookmark_file, "/.telescope/bookmarks.gmi", sizeof(bookmark_file));

	strlcpy(known_hosts_file, getenv("HOME"), sizeof(known_hosts_file));
	strlcat(known_hosts_file, "/.telescope/known_hosts", sizeof(known_hosts_file));

	strlcpy(known_hosts_tmp, getenv("HOME"), sizeof(known_hosts_tmp));
	strlcat(known_hosts_tmp, "/.telescope/known_hosts.tmp.XXXXXXXXXX",
	    sizeof(known_hosts_file));

	strlcpy(session_file, getenv("HOME"), sizeof(session_file));
	strlcat(session_file, "/.telescope/session", sizeof(session_file));

	return 1;
}

int
fs_main(void)
{
	setproctitle("fs");

	fs_init();

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

	sandbox_fs_process();

	event_dispatch();
	return 0;
}



static int
parse_khost_line(char *line, char *tmp[3])
{
	char **ap;

	for (ap = tmp; ap < &tmp[3] &&
	    (*ap = strsep(&line, " \t\n")) != NULL;) {
		if (**ap != '\0')
			ap++;
	}

	return ap == &tmp[3] && *line == '\0';
}

int
load_certs(struct ohash *h)
{
	char		*tmp[3], *line = NULL;
	const char	*errstr;
	size_t		 lineno = 0, linesize = 0;
	ssize_t		 linelen;
	FILE		*f;
	struct tofu_entry *e;

	if ((f = fopen(known_hosts_file, "r")) == NULL)
		return 0;

	while ((linelen = getline(&line, &linesize, f)) != -1) {
		if ((e = calloc(1, sizeof(*e))) == NULL)
			abort();

		lineno++;

		if (parse_khost_line(line, tmp)) {
			strlcpy(e->domain, tmp[0], sizeof(e->domain));
			strlcpy(e->hash, tmp[1], sizeof(e->hash));

			e->verified = strtonum(tmp[2], 0, 1, &errstr);
			if (errstr != NULL)
				errx(1, "%s:%zu verification for %s is %s: %s",
				    known_hosts_file, lineno,
				    e->domain, errstr, tmp[2]);
			tofu_add(h, e);
		} else {
			warnx("%s:%zu invalid entry",
			    known_hosts_file, lineno);
			free(e);
		}
	}

	free(line);
	return ferror(f);
}

int
load_last_session(void (*cb)(const char*))
{
	char	*nl, *line = NULL;
	int	 e;
	size_t	 linesize = 0;
	ssize_t	 linelen;
	FILE	*session;

	if ((session = fopen(session_file, "r")) == NULL) {
		/* first time? */
		cb("about:help");
		return 0;
	}

	while ((linelen = getline(&line, &linesize, session)) != -1) {
                if ((nl = strchr(line, '\n')) != NULL)
			*nl = '\0';
		cb(line);
	}

	free(line);
	e = ferror(session);
	fclose(session);

	return !e;
}
