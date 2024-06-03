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

/*
 * Handles config and runtime files
 */

#include "compat.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pages.h"
#include "parser.h"
#include "session.h"
#include "utils.h"

#include "fs.h"

#ifndef nitems
#define nitems(x)  (sizeof(x) / sizeof(x[0]))
#endif

static int		 select_non_dot(const struct dirent *);
static int		 select_non_dotdot(const struct dirent *);
static size_t		 join_path(char*, const char*, const char*, size_t);
static void		 getenv_default(char*, const char*, const char*, size_t);
static void		 mkdirs(const char*, mode_t);
static void		 init_paths(void);

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
char		cert_dir[PATH_MAX], cert_dir_tmp[PATH_MAX];
char		certs_file[PATH_MAX], certs_file_tmp[PATH_MAX];

char		cwd[PATH_MAX];

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
	const struct mapping {
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
	const char *dot;

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

	if (getcwd(cwd, sizeof(cwd)) == NULL)
		err(1, "getcwd failed");

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
	    sizeof(session_file_tmp));
	join_path(history_file, cache_path_base, "/history",
	    sizeof(history_file));
	join_path(history_file_tmp, cache_path_base, "/history.XXXXXXXXXX",
	    sizeof(history_file_tmp));
	join_path(cert_dir, data_path_base, "/certs/",
	    sizeof(cert_dir));
	join_path(cert_dir_tmp, data_path_base, "/certs/id.XXXXXXXXXX",
	    sizeof(cert_dir_tmp));
	join_path(certs_file, config_path_base, "/certs.conf",
	    sizeof(certs_file));
	join_path(certs_file_tmp, config_path_base, "/certs.conf.XXXXXXXXXX",
	    sizeof(certs_file_tmp));
	join_path(crashed_file, cache_path_base, "/crashed",
	    sizeof(crashed_file));

	mkdirs(cert_dir, S_IRWXU);

	return 0;
}
