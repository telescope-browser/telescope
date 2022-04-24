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

#include "pages.h"
#include "parser.h"
#include "session.h"
#include "telescope.h"
#include "utils.h"

#include "fs.h"

#ifndef nitems
#define nitems(x)  (sizeof(x) / sizeof(x[0]))
#endif

static void		 die(void) __attribute__((__noreturn__));
static int		 select_non_dot(const struct dirent *);
static int		 select_non_dotdot(const struct dirent *);
static size_t		 join_path(char*, const char*, const char*, size_t);
static void		 getenv_default(char*, const char*, const char*, size_t);
static void		 mkdirs(const char*, mode_t);
static void		 init_paths(void);
static void		 load_last_session(void);
static void		 load_hist(void);
static int		 last_time_crashed(void);
static void		 load_certs(struct ohash *);

/*
 * Where to store user data.  These are all equal to ~/.telescope if
 * it exists.
 */
char		config_path_base[PATH_MAX];
char		data_path_base[PATH_MAX];
char		cache_path_base[PATH_MAX];

char		ctlsock_path[PATH_MAX];
char		config_path[PATH_MAX];
char		lockfile_path[PATH_MAX];
char		bookmark_file[PATH_MAX];
char		known_hosts_file[PATH_MAX], known_hosts_tmp[PATH_MAX];
char		crashed_file[PATH_MAX];
char		session_file[PATH_MAX], session_file_tmp[PATH_MAX];
char		history_file[PATH_MAX], history_file_tmp[PATH_MAX];

static void __attribute__((__noreturn__))
die(void)
{
	abort(); 		/* TODO */
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

static void
send_dir(struct tab *tab, const char *path)
{
	struct dirent	**names;
	int		(*selector)(const struct dirent *) = select_non_dot;
	int		  i, len;

#if notyet
	/*
	 * need something to fake a redirect
	 */

	if (!has_suffix(path, "/")) {
		if (asprintf(&s, "%s/", path) == -1)
			die();
		send_hdr(peerid, 30, s);
		free(s);
		return;
	}
#endif

	if (!strcmp(path, "/"))
		selector = select_non_dotdot;

	if ((len = scandir(path, &names, selector, alphasort)) == -1) {
		load_page_from_str(tab, "# failure reading the directory\n");
		return;
	}

	parser_init(tab, gemtext_initparser);
	parser_parsef(tab, "# Index of %s\n\n", path);

	for (i = 0; i < len; ++i) {
		const char *sufx = "";

		if (names[i]->d_type == DT_DIR)
			sufx = "/";

		parser_parsef(tab, "=> %s%s\n", names[i]->d_name, sufx);
	}

	parser_free(tab);
	free(names);
}

static int
is_dir(FILE *fp)
{
	struct stat sb;

	if (fstat(fileno(fp), &sb) == -1)
		return 0;

	return S_ISDIR(sb.st_mode);
}

static parserinit
file_type(const char *path)
{
	struct mapping {
		const char	*ext;
		parserinit	 fn;
	} ms[] = {
		{"diff",	textpatch_initparser},
		{"gemini",	gemtext_initparser},
		{"gmi",		gemtext_initparser},
		{"markdown",	textplain_initparser},
		{"md",		textplain_initparser},
		{"patch",	gemtext_initparser},
		{NULL, NULL},
	}, *m;
	char *dot;

	if ((dot = strrchr(path, '.')) == NULL)
		return textplain_initparser;

	dot++;

	for (m = ms; m->ext != NULL; ++m)
		if (!strcmp(m->ext, dot))
			return m->fn;

	return textplain_initparser;
}

void
fs_load_url(struct tab *tab, const char *url)
{
	const char	*bpath = "bookmarks.gmi", *fallback = "# Not found\n";
	parserinit	 initfn = gemtext_initparser;
	char		 path[PATH_MAX];
	FILE		*fp = NULL;
	size_t		 i;
	char		 buf[BUFSIZ];
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

	if (!strncmp(url, "about:", 6)) {
		url += 6;

		for (i = 0; page == NULL && i < nitems(pages); ++i) {
			if (!strcmp(url, pages[i].name))
				page = &pages[i];
		}

		if (page == NULL)
			goto done;

		strlcpy(path, data_path_base, sizeof(path));
		strlcat(path, "/", sizeof(path));
		if (page->path != NULL)
			strlcat(path, page->path, sizeof(path));
		else {
			strlcat(path, "page/about_", sizeof(path));
			strlcat(path, page->name, sizeof(path));
			strlcat(path, ".gmi", sizeof(path));
		}

		fallback = page->data;
	} else if (!strncmp(url, "file://", 7)) {
		url += 7;
		strlcpy(path, url, sizeof(path));
		initfn = file_type(url);
	} else
		goto done;

	if ((fp = fopen(path, "r")) == NULL)
		goto done;

	if (is_dir(fp)) {
		fclose(fp);
		send_dir(tab, path);
		goto done;
	}

	parser_init(tab, initfn);
	for (;;) {
		size_t r;

		r = fread(buf, 1, sizeof(buf), fp);
		if (!parser_parse(tab, buf, r))
			break;
		if (r != sizeof(buf))
			break;
	}
	parser_free(tab);

done:
	if (fp != NULL)
		fclose(fp);
	else
		load_page_from_str(tab, fallback);
}

int
bookmark_page(const char *url)
{
	FILE *f;

	if ((f = fopen(bookmark_file, "a")) == NULL)
		return -1;
	fprintf(f, "=> %s\n", url);
	fclose(f);
	return 0;
}

int
save_cert(const struct tofu_entry *e)
{
	FILE *f;

	if ((f = fopen(known_hosts_file, "a")) == NULL)
		return -1;
	fprintf(f, "%s %s %d\n", e->domain, e->hash, e->verified);
	fclose(f);
	return 0;
}

int
update_cert(const struct tofu_entry *e)
{
	FILE	*tmp, *f;
	char	 sfn[PATH_MAX], *line = NULL, *t;
	size_t	 l, linesize = 0;
	ssize_t	 linelen;
	int	 fd, err;

	strlcpy(sfn, known_hosts_tmp, sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1 ||
	    (tmp = fdopen(fd, "w")) == NULL) {
		if (fd != -1) {
			unlink(sfn);
			close(fd);
		}
		return -1;
	}

	if ((f = fopen(known_hosts_file, "r")) == NULL) {
		unlink(sfn);
		fclose(tmp);
		return -1;
	}

	l = strlen(e->domain);
	while ((linelen = getline(&line, &linesize, f)) != -1) {
		if ((t = strstr(line, e->domain)) != NULL &&
		    (line[l] == ' ' || line[l] == '\t'))
			continue;
		/* line has a trailing \n */
		fprintf(tmp, "%s", line);
	}
	fprintf(tmp, "%s %s %d\n", e->domain, e->hash, e->verified);

	free(line);
	err = ferror(tmp);

	fclose(tmp);
	fclose(f);

	if (err) {
		unlink(sfn);
		return -1;
	}

	if (rename(sfn, known_hosts_file))
		return -1;
	return 0;
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

	join_path(ctlsock_path, cache_path_base, "/ctl",
	    sizeof(ctlsock_path));
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
	join_path(session_file_tmp, cache_path_base, "/session.XXXXXXXXXX",
	    sizeof(session_file));
	join_path(history_file, cache_path_base, "/history",
	    sizeof(history_file));
	join_path(history_file_tmp, cache_path_base, "/history.XXXXXXXXXX",
	    sizeof(history_file));
	join_path(crashed_file, cache_path_base, "/crashed",
	    sizeof(crashed_file));

	return 1;
}

/*
 * Parse a line of the session file and restores it.  The format is:
 *
 *	URL [flags,...] [title]\n
 */
static inline struct tab *
parse_session_line(char *line, struct tab **ct)
{
	struct tab *tab;
	char *s, *t, *ap;
	const char *uri, *title = "";
	int current = 0, killed = 0;
	size_t top_line = 0, current_line = 0;

	uri = line;
	if ((s = strchr(line, ' ')) == NULL)
		return NULL;

	*s++ = '\0';

	if ((t = strchr(s, ' ')) != NULL) {
		*t++ = '\0';
		title = t;
	}

	while ((ap = strsep(&s, ",")) != NULL) {
		if (!strcmp(ap, "current"))
			current = 1;
		else if (!strcmp(ap, "killed"))
			killed = 1;
		else if (has_prefix(ap, "top="))
			top_line = strtonum(ap+4, 0, UINT32_MAX, NULL);
		else if (has_prefix(ap, "cur="))
			current_line = strtonum(ap+4, 0, UINT32_MAX, NULL);
	}

	if (top_line > current_line) {
		top_line = 0;
		current_line = 0;
	}

	if ((tab = new_tab(uri, NULL, NULL)) == NULL)
		die();
	tab->hist_cur->line_off = top_line;
	tab->hist_cur->current_off = current_line;
	strlcpy(tab->buffer.page.title, title, sizeof(tab->buffer.page.title));

	if (current)
		*ct = tab;
	else if (killed)
		kill_tab(tab, 1);

	return tab;
}

static inline void
sendhist(struct tab *tab, const char *uri, int future)
{
	struct hist *h;

	if ((h = calloc(1, sizeof(*h))) == NULL)
		die();
	strlcpy(h->h, uri, sizeof(h->h));

	if (future)
		hist_push(&tab->hist, h);
	else
		hist_add_before(&tab->hist, tab->hist_cur, h);
}

static void
load_last_session(void)
{
	struct tab	*tab = NULL, *ct = NULL;
	FILE		*session;
	size_t		 linesize = 0;
	ssize_t		 linelen;
	int		 future;
	char		*nl, *s, *line = NULL;

	if ((session = fopen(session_file, "r")) == NULL) {
		new_tab("about:new", NULL, NULL);
		switch_to_tab(new_tab("about:help", NULL, NULL));
		return;
	}

	while ((linelen = getline(&line, &linesize, session)) != -1) {
		if ((nl = strchr(line, '\n')) != NULL)
			*nl = '\0';

		if (*line == '<' || *line == '>') {
			future = *line == '>';
			s = line+1;
			if (*s != ' ' || tab == NULL)
				continue;
			sendhist(tab, ++s, future);
		} else {
			tab = parse_session_line(line, &ct);
		}
	}

	fclose(session);
	free(line);

	if (ct != NULL)
		switch_to_tab(ct);

	if (last_time_crashed())
		switch_to_tab(new_tab("about:crash", NULL, NULL));
}

static void
load_hist(void)
{
	FILE		*hist;
	size_t		 linesize = 0;
	ssize_t		 linelen;
	char		*nl, *spc, *line = NULL;
	const char	*errstr;
	struct histitem	 hi;

	if ((hist = fopen(history_file, "r")) == NULL)
		return;

	while ((linelen = getline(&line, &linesize, hist)) != -1) {
		if ((nl = strchr(line, '\n')) != NULL)
			*nl = '\0';
		if ((spc = strchr(line, ' ')) == NULL)
			continue;
		*spc = '\0';
		spc++;

		memset(&hi, 0, sizeof(hi));
		hi.ts = strtonum(line, INT64_MIN, INT64_MAX, &errstr);
		if (errstr != NULL)
			continue;
		if (strlcpy(hi.uri, spc, sizeof(hi.uri)) >= sizeof(hi.uri))
			continue;

		history_push(&hi);
	}

	fclose(hist);
	free(line);

	history_sort();
}

int
fs_load_state(struct ohash *certs)
{
	load_certs(certs);
	load_hist();
	load_last_session();
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
load_certs(struct ohash *certs)
{
	char		*tmp[3], *line = NULL;
	const char	*errstr;
	size_t		 lineno = 0, linesize = 0;
	ssize_t		 linelen;
	FILE		*f;
	struct tofu_entry *e;

	if ((f = fopen(known_hosts_file, "r")) == NULL)
		return;

	if ((e = calloc(1, sizeof(*e))) == NULL) {
		fclose(f);
		return;
	}

	while ((linelen = getline(&line, &linesize, f)) != -1) {
		lineno++;

		if (parse_khost_line(line, tmp)) {
			strlcpy(e->domain, tmp[0], sizeof(e->domain));
			strlcpy(e->hash, tmp[1], sizeof(e->hash));

			e->verified = strtonum(tmp[2], 0, 1, &errstr);
			if (errstr != NULL)
				errx(1, "%s:%zu verification for %s is %s: %s",
				    known_hosts_file, lineno,
				    e->domain, errstr, tmp[2]);

			tofu_add(certs, e);
		} else {
			warnx("%s:%zu invalid entry",
			    known_hosts_file, lineno);
		}
	}

	free(line);
	fclose(f);
	return;
}
