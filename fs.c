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
 * Handles config and runtime files
 */

#include "compat.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"
#include "pages.h"
#include "telescope.h"
#include "session.h"

static void		 die(void) __attribute__((__noreturn__));
static void		 send_file(uint32_t, FILE *);
static void		 handle_get(struct imsg*, size_t);
static int		 select_non_dot(const struct dirent *);
static int		 select_non_dotdot(const struct dirent *);
static void		 handle_get_file(struct imsg*, size_t);
static void		 handle_misc(struct imsg *, size_t);
static void		 handle_bookmark_page(struct imsg*, size_t);
static void		 handle_save_cert(struct imsg*, size_t);
static void		 handle_update_cert(struct imsg*, size_t);
static void		 handle_file_open(struct imsg*, size_t);
static void		 handle_session_start(struct imsg*, size_t);
static void		 handle_session_tab(struct imsg*, size_t);
static void		 handle_session_tab_hist(struct imsg*, size_t);
static void		 handle_session_end(struct imsg*, size_t);
static void		 handle_dispatch_imsg(int, short, void*);
static int		 fs_send_ui(int, uint32_t, int, const void *, uint16_t);
static size_t		 join_path(char*, const char*, const char*, size_t);
static void		 getenv_default(char*, const char*, const char*, size_t);
static void		 mkdirs(const char*, mode_t);
static void		 init_paths(void);
static void		 load_last_session(void);
static int		 last_time_crashed(void);
static void		 load_certs(void);

static struct imsgev		*iev_ui;
static FILE			*session;

/*
 * Where to store user data.  These are all equal to ~/.telescope if
 * it exists.
 */
char		config_path_base[PATH_MAX];
char		data_path_base[PATH_MAX];
char		cache_path_base[PATH_MAX];

char		config_path[PATH_MAX];
char		lockfile_path[PATH_MAX];
char		bookmark_file[PATH_MAX];
char		known_hosts_file[PATH_MAX], known_hosts_tmp[PATH_MAX];
char		crashed_file[PATH_MAX];
char		session_file[PATH_MAX];

static imsg_handlerfn *handlers[] = {
	[IMSG_GET] = handle_get,
	[IMSG_GET_FILE] = handle_get_file,
	[IMSG_QUIT] = handle_misc,
	[IMSG_INIT] = handle_misc,
	[IMSG_BOOKMARK_PAGE] = handle_bookmark_page,
	[IMSG_SAVE_CERT] = handle_save_cert,
	[IMSG_UPDATE_CERT] = handle_update_cert,
	[IMSG_FILE_OPEN] = handle_file_open,
	[IMSG_SESSION_START] = handle_session_start,
	[IMSG_SESSION_TAB] = handle_session_tab,
	[IMSG_SESSION_TAB_HIST] = handle_session_tab_hist,
	[IMSG_SESSION_END] = handle_session_end,
};

static void __attribute__((__noreturn__))
die(void)
{
	abort(); 		/* TODO */
}

static void
send_file(uint32_t peerid, FILE *f)
{
	ssize_t	 r;
	char	 buf[BUFSIZ];

	for (;;) {
		r = fread(buf, 1, sizeof(buf), f);
		if (r != 0)
			fs_send_ui(IMSG_BUF, peerid, -1, buf, r);
		if (r != sizeof(buf))
			break;
	}
	fs_send_ui(IMSG_EOF, peerid, -1, NULL, 0);
	fclose(f);
}

static void
handle_get(struct imsg *imsg, size_t datalen)
{
	const char	*bpath = "bookmarks.gmi";
	char		 path[PATH_MAX];
	FILE		*f;
	const char	*data, *p;
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

	strlcpy(path, data_path_base, sizeof(path));
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

	send_file(imsg->hdr.peerid, f);
	return;

notfound:
	p = "# not found!\n";
	fs_send_ui(IMSG_BUF, imsg->hdr.peerid, -1, p, strlen(p));
	fs_send_ui(IMSG_EOF, imsg->hdr.peerid, -1, NULL, 0);
}

static inline void
send_hdr(uint32_t peerid, int code, const char *meta)
{
	fs_send_ui(IMSG_GOT_CODE, peerid, -1, &code, sizeof(code));
	fs_send_ui(IMSG_GOT_META, peerid, -1, meta, strlen(meta)+1);
}

static inline void
send_errno(uint32_t peerid, int code, const char *str, int no)
{
	char *s;

	if (asprintf(&s, "%s: %s", str, strerror(no)) == -1)
		s = NULL;

	send_hdr(peerid, code, s == NULL ? str : s);
	free(s);
}

static inline const char *
file_type(const char *path)
{
	struct mapping {
		const char	*ext;
		const char	*mime;
	} ms[] = {
		{"diff",	"text/x-patch"},
		{"gemini",	"text/gemini"},
		{"gmi",		"text/gemini"},
		{"markdown",	"text/plain"},
		{"md",		"text/plain"},
		{"patch",	"text/x-patch"},
		{"txt",		"text/plain"},
		{NULL, NULL},
	}, *m;
	char *dot;

	if ((dot = strrchr(path, '.')) == NULL)
		return NULL;

	dot++;

	for (m = ms; m->ext != NULL; ++m)
		if (!strcmp(m->ext, dot))
			return m->mime;

	return NULL;
}

static int
select_non_dot(const struct dirent *d)
{
	return strcmp(d->d_name, ".");
}

static int
select_non_dotdot(const struct dirent *d)
{
	return strcmp(d->d_name, ".") && strcmp(d->d_name, "..");
}

static inline void
send_dir(uint32_t peerid, const char *path)
{
	struct dirent	**names;
	struct evbuffer	 *ev;
	char		 *s;
	int		(*selector)(const struct dirent *) = select_non_dot;
	int		  i, len, no;

	if (!has_suffix(path, "/")) {
		if (asprintf(&s, "%s/", path) == -1)
			die();
		send_hdr(peerid, 30, s);
		free(s);
		return;
	}

	if (!strcmp(path, "/"))
		selector = select_non_dotdot;

	if ((ev = evbuffer_new()) == NULL ||
	    (len = scandir(path, &names, selector, alphasort)) == -1) {
		no = errno;
		evbuffer_free(ev);
		send_errno(peerid, 40, "failure reading the directory", no);
		return;
	}

	evbuffer_add_printf(ev, "# Index of %s\n\n", path);
	for (i = 0; i < len; ++i) {
		evbuffer_add_printf(ev, "=> %s", names[i]->d_name);
		if (names[i]->d_type == DT_DIR)
			evbuffer_add(ev, "/", 1);
		evbuffer_add(ev, "\n", 1);
	}

	send_hdr(peerid, 20, "text/gemini");
	fs_send_ui(IMSG_BUF, peerid, -1,
	    EVBUFFER_DATA(ev), EVBUFFER_LENGTH(ev));
	fs_send_ui(IMSG_EOF, peerid, -1, NULL, 0);

	evbuffer_free(ev);
	free(names);
}

static void
handle_get_file(struct imsg *imsg, size_t datalen)
{
	struct stat	 sb;
	FILE		*f;
	char		*data;
	const char	*meta = NULL;

	data = imsg->data;
	data[datalen-1] = '\0';

	if ((f = fopen(data, "r")) == NULL) {
		send_errno(imsg->hdr.peerid, 51, "can't open", errno);
		return;
	}

	if (fstat(fileno(f), &sb) == -1) {
		send_errno(imsg->hdr.peerid, 40, "fstat", errno);
		return;
	}

	if (S_ISDIR(sb.st_mode)) {
		fclose(f);
		send_dir(imsg->hdr.peerid, data);
		return;
	}

	if ((meta = file_type(data)) == NULL) {
		fclose(f);
		send_hdr(imsg->hdr.peerid, 51,
		    "don't know how to visualize this file");
		return;
	}

	send_hdr(imsg->hdr.peerid, 20, meta);
	send_file(imsg->hdr.peerid, f);
}

static void
handle_misc(struct imsg *imsg, size_t datalen)
{
	switch (imsg->hdr.type) {
	case IMSG_INIT:
		load_certs();
		load_last_session();
		break;

	case IMSG_QUIT:
		if (!safe_mode)
			unlink(crashed_file);
		event_loopbreak();
		break;

	default:
		die();
	}
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
	struct session_tab	tab;

	if (session == NULL)
		die();

	if (datalen != sizeof(tab))
		die();

	memcpy(&tab, imsg->data, sizeof(tab));
	if (tab.uri[sizeof(tab.uri)-1] != '\0' ||
	    tab.title[sizeof(tab.title)-1] != '\0')
		die();

	fprintf(session, "%s", tab.uri);

	if (tab.flags & TAB_CURRENT)
		fprintf(session, " current ");
	else
		fprintf(session, " - ");

	fprintf(session, "%s\n", tab.title);
}

static void
handle_session_tab_hist(struct imsg *imsg, size_t datalen)
{
	struct session_tab_hist th;

	if (session == NULL)
		die();

	if (datalen != sizeof(th))
		die();

	memcpy(&th, imsg->data, sizeof(th));
	if (th.uri[sizeof(th.uri)-1] != '\0')
		die();

	fprintf(session, "%s %s\n", th.future ? ">" : "<", th.uri);
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
		 * This should leave a ~/.cache/telescope/crashed file to
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

static size_t
join_path(char *buf, const char *lhs, const char *rhs, size_t buflen)
{
	strlcpy(buf, lhs, buflen);
	return strlcat(buf, rhs, buflen);
}

static void
getenv_default(char *buf, const char *name, const char *def, size_t buflen)
{
	size_t ret;
	char *home, *env;

	if ((home = getenv("HOME")) == NULL)
		errx(1, "HOME is not defined");

	if ((env = getenv(name)) != NULL)
		ret = strlcpy(buf, env, buflen);
	else
		ret = join_path(buf, home, def, buflen);

	if (ret >= buflen)
		errx(1, "buffer too small for %s", name);
}

static void
mkdirs(const char *path, mode_t mode)
{
	char copy[PATH_MAX+1], orig[PATH_MAX+1], *parent;

	strlcpy(copy, path, sizeof(copy));
	strlcpy(orig, path, sizeof(orig));
	parent = dirname(copy);
	if (!strcmp(parent, "/"))
		return;
	mkdirs(parent, mode);

	if (mkdir(orig, mode) != 0) {
		if (errno == EEXIST)
			return;
		err(1, "can't mkdir %s", orig);
	}
}

static void
init_paths(void)
{
	char		 xdg_config_base[PATH_MAX];
	char		 xdg_data_base[PATH_MAX];
	char		 xdg_cache_base[PATH_MAX];
	char		 old_path[PATH_MAX];
	char		*home;
	struct stat	 info;

	/* old path */
	if ((home = getenv("HOME")) == NULL)
		errx(1, "HOME is not defined");
	join_path(old_path, home, "/.telescope", sizeof(old_path));

	/* if ~/.telescope exists, use that instead of xdg dirs */
	if (stat(old_path, &info) == 0 && S_ISDIR(info.st_mode)) {
		join_path(config_path_base, home, "/.telescope",
		    sizeof(config_path_base));
		join_path(data_path_base, home, "/.telescope",
		    sizeof(data_path_base));
		join_path(cache_path_base, home, "/.telescope",
		    sizeof(cache_path_base));
		return;
	}

	/* xdg paths */
	getenv_default(xdg_config_base, "XDG_CONFIG_HOME", "/.config",
	    sizeof(xdg_config_base));
	getenv_default(xdg_data_base, "XDG_DATA_HOME", "/.local/share",
	    sizeof(xdg_data_base));
	getenv_default(xdg_cache_base, "XDG_CACHE_HOME", "/.cache",
	    sizeof(xdg_cache_base));

	join_path(config_path_base, xdg_config_base, "/telescope",
	    sizeof(config_path_base));
	join_path(data_path_base, xdg_data_base, "/telescope",
	    sizeof(data_path_base));
	join_path(cache_path_base, xdg_cache_base, "/telescope",
	    sizeof(cache_path_base));

	mkdirs(xdg_config_base, S_IRWXU);
	mkdirs(xdg_data_base, S_IRWXU);
	mkdirs(xdg_cache_base, S_IRWXU);

	mkdirs(config_path_base, S_IRWXU);
	mkdirs(data_path_base, S_IRWXU);
	mkdirs(cache_path_base, S_IRWXU);
}

int
fs_init(void)
{
	init_paths();

	join_path(config_path, config_path_base, "/config",
	    sizeof(config_path));
	join_path(lockfile_path, cache_path_base, "/lock",
	    sizeof(lockfile_path));
	join_path(bookmark_file, data_path_base, "/bookmarks.gmi",
	    sizeof(bookmark_file));
	join_path(known_hosts_file, data_path_base, "/known_hosts",
	    sizeof(known_hosts_file));
	join_path(known_hosts_tmp, cache_path_base,
	    "/known_hosts.tmp.XXXXXXXXXX", sizeof(known_hosts_tmp));
	join_path(session_file, cache_path_base, "/session",
	    sizeof(session_file));
	join_path(crashed_file, cache_path_base, "/crashed",
	    sizeof(crashed_file));

	return 1;
}

/*
 * Parse a line of the session file.  The format is:
 *
 *	URL [flags,...] [title]\n
 */
static void
parse_session_line(char *line, const char **title, uint32_t *flags)
{
	char *s, *t, *ap;

	*title = "";
	*flags = 0;
	if ((s = strchr(line, ' ')) == NULL)
		return;

	*s++ = '\0';

	if ((t = strchr(s, ' ')) != NULL) {
		*t++ = '\0';
		*title = t;
	}

	while ((ap = strsep(&s, ",")) != NULL) {
		if (!strcmp(ap, "current"))
			*flags |= TAB_CURRENT;
	}
}

static inline void
sendtab(uint32_t flags, const char *uri, const char *title)
{
	struct session_tab tab;

	memset(&tab, 0, sizeof(tab));
	tab.flags = flags;

	if (strlcpy(tab.uri, uri, sizeof(tab.uri)) >= sizeof(tab.uri))
		return;

	/* don't worry about cached title truncation */
	if (title != NULL)
		strlcpy(tab.title, title, sizeof(tab.title));

	fs_send_ui(IMSG_SESSION_TAB, 0, -1, &tab, sizeof(tab));
}

static inline void
sendhist(const char *uri, int future)
{
	struct session_tab_hist sth;

	memset(&sth, 0, sizeof(sth));
	sth.future = future;

	if (strlcpy(sth.uri, uri, sizeof(sth.uri)) >= sizeof(sth.uri))
		return;

	fs_send_ui(IMSG_SESSION_TAB_HIST, 0, -1, &sth, sizeof(sth));
}

static void
load_last_session(void)
{
	FILE		*session;
	uint32_t	 flags;
	size_t		 linesize = 0;
	ssize_t		 linelen;
	int		 first_time = 0;
	int		 future;
	const char	*title;
	char		*nl, *s, *line = NULL;

	if ((session = fopen(session_file, "r")) == NULL) {
		/* first time? */
		first_time = 1;
		goto end;
	}

	while ((linelen = getline(&line, &linesize, session)) != -1) {
		if ((nl = strchr(line, '\n')) != NULL)
			*nl = '\0';

		if (*line == '<' || *line == '>') {
			future = *line == '>';
			s = line+1;
			if (*s != ' ')
				continue;
			sendhist(++s, future);
		} else {
			parse_session_line(line, &title, &flags);
			sendtab(flags, line, title);
		}
	}

	fclose(session);
	free(line);

	if (last_time_crashed())
		sendtab(TAB_CURRENT, "about:crash", NULL);

end:
	fs_send_ui(IMSG_SESSION_END, 0, -1, &first_time, sizeof(first_time));
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

/*
 * Check if the last time telescope crashed.  The check is done by
 * looking at `crashed_file': if it exists then last time we crashed.
 * Then, while here, touch the file too.  During IMSG_QUIT we'll
 * remove it.
 */
static int
last_time_crashed(void)
{
	int fd, crashed = 1;

	if (safe_mode)
		return 0;

	if (unlink(crashed_file) == -1 && errno == ENOENT)
		crashed = 0;

	if ((fd = open(crashed_file, O_CREAT|O_WRONLY, 0600)) == -1)
		return crashed;
	close(fd);

	return crashed;
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

static inline int
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

static void
load_certs(void)
{
	char		*tmp[3], *line = NULL;
	const char	*errstr;
	size_t		 lineno = 0, linesize = 0;
	ssize_t		 linelen;
	FILE		*f;
	struct tofu_entry e;

	if ((f = fopen(known_hosts_file, "r")) == NULL)
		return;

	while ((linelen = getline(&line, &linesize, f)) != -1) {
		lineno++;

		memset(&e, 0, sizeof(e));
		if (parse_khost_line(line, tmp)) {
			strlcpy(e.domain, tmp[0], sizeof(e.domain));
			strlcpy(e.hash, tmp[1], sizeof(e.hash));

			e.verified = strtonum(tmp[2], 0, 1, &errstr);
			if (errstr != NULL)
				errx(1, "%s:%zu verification for %s is %s: %s",
				    known_hosts_file, lineno,
				    e.domain, errstr, tmp[2]);

			fs_send_ui(IMSG_TOFU, 0, -1, &e, sizeof(e));
		} else {
			warnx("%s:%zu invalid entry",
			    known_hosts_file, lineno);
		}
	}

	free(line);
	fclose(f);
	return;
}

