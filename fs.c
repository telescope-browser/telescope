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
#include "pages.h"

static void		 die(void) __attribute__((__noreturn__));
static void		 handle_get(struct imsg*, size_t);
static void		 handle_quit(struct imsg*, size_t);
static void		 handle_bookmark_page(struct imsg*, size_t);
static void		 handle_save_cert(struct imsg*, size_t);
static void		 handle_update_cert(struct imsg*, size_t);
static void		 handle_file_open(struct imsg*, size_t);
static void		 handle_session_start(struct imsg*, size_t);
static void		 handle_session_tab(struct imsg*, size_t);
static void		 handle_session_tab_title(struct imsg*, size_t);
static void		 handle_session_end(struct imsg*, size_t);
static void		 handle_dispatch_imsg(int, short, void*);
static int		 fs_send_ui(int, uint32_t, int, const void *, uint16_t);

static struct imsgev		*iev_ui;
static FILE			*session;

static char	base_path[PATH_MAX];
static char	lockfile_path[PATH_MAX];
static char	bookmark_file[PATH_MAX];
static char	known_hosts_file[PATH_MAX], known_hosts_tmp[PATH_MAX];
static char	crashed_file[PATH_MAX];

char	session_file[PATH_MAX];

static imsg_handlerfn *handlers[] = {
	[IMSG_GET] = handle_get,
	[IMSG_QUIT] = handle_quit,
	[IMSG_BOOKMARK_PAGE] = handle_bookmark_page,
	[IMSG_SAVE_CERT] = handle_save_cert,
	[IMSG_UPDATE_CERT] = handle_update_cert,
	[IMSG_FILE_OPEN] = handle_file_open,
	[IMSG_SESSION_START] = handle_session_start,
	[IMSG_SESSION_TAB] = handle_session_tab,
	[IMSG_SESSION_TAB_TITLE] = handle_session_tab_title,
	[IMSG_SESSION_END] = handle_session_end,
};

static void __attribute__((__noreturn__))
die(void)
{
	abort(); 		/* TODO */
}

static void
handle_get(struct imsg *imsg, size_t datalen)
{
	const char	*bpath = "bookmarks.gmi";
	char		 path[PATH_MAX], buf[BUFSIZ];
	FILE		*f;
	const char	*data, *p;
	ssize_t		 r;
	size_t		 i;
	struct page {
		const char	*name;
		const char	*path;
		const uint8_t	*data;
		size_t		 len;
	} pages[] = {
		{"about",	NULL,	about_about,	about_about_len},
		{"blank",	NULL,	about_blank,	about_blank_len},
		{"bookmarks",	bpath,	bookmarks,	bookmarks_len},
		{"crash",	NULL,	about_crash,	about_crash_len},
		{"help",	NULL,	about_help,	about_help_len},
		{"license",	NULL,	about_license,	about_license_len},
		{"new",		NULL,	about_new,	about_new_len},
	}, *page = NULL;

	data = imsg->data;
	if (data[datalen-1] != '\0') /* make sure it's NUL-terminated */
		die();
	if ((data = strchr(data, ':')) == NULL)
		goto notfound;
	data++;

	for (i = 0; i < sizeof(pages)/sizeof(pages[0]); ++i)
		if (!strcmp(data, pages[i].name)) {
			page = &pages[i];
			break;
		}

	if (page == NULL)
		goto notfound;

	strlcpy(path, base_path, sizeof(path));
	strlcat(path, "/", sizeof(path));
	if (page->path != NULL)
		strlcat(path, page->path, sizeof(path));
	else {
		strlcat(path, "pages/about_", sizeof(path));
		strlcat(path, page->name, sizeof(path));
		strlcat(path, ".gmi", sizeof(path));
	}

	if ((f = fopen(path, "r")) == NULL) {
		fs_send_ui(IMSG_BUF, imsg->hdr.peerid, -1,
		    page->data, page->len);
		fs_send_ui(IMSG_EOF, imsg->hdr.peerid, -1,
		    NULL, 0);
		return;
	}

	for (;;) {
		r = fread(buf, 1, sizeof(buf), f);
		fs_send_ui(IMSG_BUF, imsg->hdr.peerid, -1, buf, r);
		if (r != sizeof(buf))
			break;
	}
	fs_send_ui(IMSG_EOF, imsg->hdr.peerid, -1, NULL, 0);
	fclose(f);
	return;

notfound:
	p = "# not found!\n";
	fs_send_ui(IMSG_BUF, imsg->hdr.peerid, -1, p, strlen(p));
	fs_send_ui(IMSG_EOF, imsg->hdr.peerid, -1, NULL, 0);
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
	char		*url;
	uint32_t	 flags;

	if (session == NULL)
		die();

	flags = imsg->hdr.peerid;
	url = imsg->data;
	if (datalen == 0 || url[datalen-1] != '\0')
		die();
	fprintf(session, "%s", url);

	if (flags & TAB_CURRENT)
		fprintf(session, " current ");
	else
		fprintf(session, " - ");
}

static void
handle_session_tab_title(struct imsg *imsg, size_t datalen)
{
	const char	*title;

	title = imsg->data;
	if (title == NULL) {
		datalen = 1;
		title = "";
	}

	if (title[datalen-1] != '\0')
		die();

	fprintf(session, "%s\n", title);
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
	int		 e;

	if (dispatch_imsg(iev, ev, handlers, sizeof(handlers)) == -1) {
		/*
		 * This should leave a ~/.telescope/crashed file to
		 * trigger about:crash on next run.  Unfortunately, if
		 * the main process dies the fs sticks around and
		 * doesn't notice that the fd was closed.  Why EV_READ
		 * is not triggered when a fd is closed on the other end?
		 */
		e = errno;
		if ((fd = open(crashed_file, O_CREAT|O_TRUNC|O_WRONLY, 0600))
		    == -1)
			err(1, "open");
		close(fd);
		errx(1, "connection closed: %s", strerror(e));
	}
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
	strlcpy(base_path, getenv("HOME"), sizeof(base_path));
	strlcat(base_path, "/.telescope", sizeof(base_path));
	mkdir(base_path, 0700);

	strlcpy(lockfile_path, base_path, sizeof(lockfile_path));
	strlcat(lockfile_path, "/lock", sizeof(lockfile_path));

	strlcpy(bookmark_file, base_path, sizeof(bookmark_file));
	strlcat(bookmark_file, "/bookmarks.gmi", sizeof(bookmark_file));

	strlcpy(known_hosts_file, base_path, sizeof(known_hosts_file));
	strlcat(known_hosts_file, "/known_hosts", sizeof(known_hosts_file));

	strlcpy(known_hosts_tmp, base_path, sizeof(known_hosts_tmp));
	strlcat(known_hosts_tmp, "/known_hosts.tmp.XXXXXXXXXX",
	    sizeof(known_hosts_file));

	strlcpy(session_file, base_path, sizeof(session_file));
	strlcat(session_file, "/session", sizeof(session_file));

	strlcpy(crashed_file, base_path, sizeof(crashed_file));
	strlcat(crashed_file, "/crashed", sizeof(crashed_file));

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



int
last_time_crashed(void)
{
	int fd;

	if ((fd = open(crashed_file, O_RDONLY)) == -1)
		return 0;

	close(fd);
	unlink(crashed_file);
	return 1;
}

int
lock_session(void)
{
	struct flock	lock;
	int		fd;

	if ((fd = open(lockfile_path, O_WRONLY|O_CREAT, 0600)) == -1)
		return -1;

	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;

	if (fcntl(fd, F_SETLK, &lock) == -1) {
		close(fd);
		return -1;
	}

	return fd;
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

