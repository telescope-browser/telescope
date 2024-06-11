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

#include "compat.h"

#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "defaults.h"
#include "ev.h"
#include "fs.h"
#include "hist.h"
#include "imsgev.h"
#include "minibuffer.h"
#include "session.h"
#include "tofu.h"
#include "ui.h"
#include "xwrapper.h"

struct history	history;

static unsigned int	 autosavetimer;

void
switch_to_tab(struct tab *tab)
{
	current_tab = tab;
	tab->flags &= ~TAB_URGENT;

	if (operating && tab->flags & TAB_LAZY)
		load_url_in_tab(tab, hist_cur(tab->hist), NULL,
		    LU_MODE_NOHIST);
}

unsigned int
tab_new_id(void)
{
	static uint32_t tab_counter;

	return tab_counter++;
}

struct tab *
new_tab(const char *url, const char *base, struct tab *after)
{
	struct tab	*tab;

	ui_schedule_redraw();
	autosave_hook();

	tab = xcalloc(1, sizeof(*tab));

	if ((tab->hist = hist_new(HIST_LINEAR)) == NULL) {
		free(tab);
		ev_break();
		return NULL;
	}

	TAILQ_INIT(&tab->buffer.head);
	TAILQ_INIT(&tab->buffer.vhead);

	tab->id = tab_new_id();

	if (after != NULL)
		TAILQ_INSERT_AFTER(&tabshead, after, tab, tabs);
	else
		TAILQ_INSERT_TAIL(&tabshead, tab, tabs);

	if (!operating)
		tab->flags |= TAB_LAZY;
	load_url_in_tab(tab, url, base, 0);
	switch_to_tab(tab);
	return tab;
}

/*
 * Move a tab from the tablist to the killed tab list and erase its
 * contents.  Append should always be 0 to prepend tabs so unkill_tab
 * can work correctly; appending is only useful during startup when
 * receiving the list of killed tabs to keep the correct order.
 * NB: doesn't update the current_tab.
 */
void
kill_tab(struct tab *tab, int append)
{
	int count;

	stop_tab(tab);
	erase_buffer(&tab->buffer);
	TAILQ_REMOVE(&tabshead, tab, tabs);
	ui_schedule_redraw();
	autosave_hook();

	ev_timer_cancel(tab->loading_timer);

	if (append)
		TAILQ_INSERT_TAIL(&ktabshead, tab, tabs);
	else
		TAILQ_INSERT_HEAD(&ktabshead, tab, tabs);

	/* gc closed tabs */
	count = 0;
	TAILQ_FOREACH(tab, &ktabshead, tabs)
		count++;
	while (count > max_killed_tabs) {
		count--;
		free_tab(TAILQ_LAST(&ktabshead, tabshead));
	}
}

/*
 * Resurrects the lastest killed tab and returns it.  The tab is already
 * added to the tab list with the TAB_LAZY flag set.  NB: this doesn't
 * update current_tab.
 */
struct tab *
unkill_tab(void)
{
	struct tab *t;

	if (TAILQ_EMPTY(&ktabshead))
		return NULL;

	ui_schedule_redraw();
	autosave_hook();

	t = TAILQ_FIRST(&ktabshead);
	TAILQ_REMOVE(&ktabshead, t, tabs);
	TAILQ_INSERT_TAIL(&tabshead, t, tabs);
	t->flags |= TAB_LAZY;
	return t;
}

/*
 * Free every resource linked to the tab, including the tab itself, and
 * removes it from the *killed* tablist.
 */
void
free_tab(struct tab *tab)
{
	TAILQ_REMOVE(&ktabshead, tab, tabs);
	hist_free(tab->hist);
	free(tab);
}

void
stop_tab(struct tab *tab)
{
	ui_send_net(IMSG_STOP, tab->id, -1, NULL, 0);
}

static inline void
savetab(FILE *fp, struct tab *tab, int killed)
{
	size_t		 i, size, cur;
	size_t		 top_line, current_line;

	get_scroll_position(tab, &top_line, &current_line);

	fprintf(fp, "%s ", hist_cur(tab->hist));
	if (tab == current_tab)
		fprintf(fp, "current,");
	if (killed)
		fprintf(fp, "killed,");

	fprintf(fp, "top=%zu,cur=%zu %s\n", top_line, current_line,
	    tab->buffer.title);

	cur = hist_off(tab->hist);
	size = hist_size(tab->hist);
	for (i = 0; i < size; ++i) {
		if (i == cur)
			continue;
		fprintf(fp, "%s %s\n", i > cur ? ">" : "<",
		    hist_nth(tab->hist, i));
	}
}

static void
save_tabs(void)
{
	FILE		*fp;
	struct tab	*tab;
	int		 fd, err;
	char		 sfn[PATH_MAX];

	strlcpy(sfn, session_file_tmp, sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1 ||
	    (fp = fdopen(fd, "w")) == NULL) {
		if (fd != -1) {
			unlink(sfn);
			close(fd);
		}
		return;
	}

	TAILQ_FOREACH(tab, &tabshead, tabs)
		savetab(fp, tab, 0);
	TAILQ_FOREACH(tab, &ktabshead, tabs)
		savetab(fp, tab, 1);

	err = fflush(fp) == EOF;
	fclose(fp);

	if (err || rename(sfn, session_file) == -1)
		unlink(sfn);
}

static void
save_all_history(void)
{
	FILE	*fp;
	size_t	 i;
	int	 fd, err;
	char	 sfn[PATH_MAX];

	strlcpy(sfn, history_file_tmp, sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1 ||
	    (fp = fdopen(fd, "w")) == NULL) {
		if (fd != -1) {
			unlink(sfn);
			close(fd);
		}
		return;
	}

	for (i = 0; i < history.len; ++i) {
		history.items[i].dirty = 0;
		fprintf(fp, "%lld %s\n", (long long)history.items[i].ts,
		    history.items[i].uri);
	}

	err = fflush(fp) == EOF;
	fclose(fp);

	if (err || rename(sfn, history_file) == -1) {
		unlink(sfn);
		return;
	}

	history.dirty = 0;
	history.extra = 0;
}

static void
save_dirty_history(void)
{
	FILE	*fp;
	size_t	 i;

	if ((fp = fopen(history_file, "a")) == NULL)
		return;

	for (i = 0; i < history.len && history.dirty > 0; ++i) {
		if (!history.items[i].dirty)
			continue;
		history.dirty--;
		history.items[i].dirty = 0;
		fprintf(fp, "%lld %s\n", (long long)history.items[i].ts,
		    history.items[i].uri);
	}
	history.dirty = 0;

	fclose(fp);
}

void
save_session(void)
{
	if (safe_mode)
		return;

	save_tabs();

	if (history.extra > HISTORY_CAP/2)
		save_all_history();
	else if (history.dirty)
		save_dirty_history();
}

void
history_push(struct histitem *hi)
{
	size_t		 i, oldest = 0;
	char		*uri;

	for (i = 0; i < history.len; ++i) {
		if (history.items[i].ts < history.items[oldest].ts)
			oldest = i;

		/* remove duplicates */
		if (!strcmp(history.items[i].uri, hi->uri))
			return;
	}

	uri = xstrdup(hi->uri);

	/* don't grow too much; replace the oldest */
	if (history.len == HISTORY_CAP) {
		history.items[oldest].ts = hi->ts;
		free(history.items[oldest].uri);
		history.items[oldest].uri = uri;

		/* Growed past the max value, signal to regen the file. */
		history.extra++;
		return;
	}

	history.items[history.len].ts = hi->ts;
	history.items[history.len].uri = uri;
	history.len++;
}

static int
history_cmp(const void *a, const void *b)
{
	const struct history_item *i = a, *j = b;
	return strcmp(i->uri, j->uri);
}

void
history_sort(void)
{
	qsort(history.items, history.len, sizeof(history.items[0]),
	    history_cmp);
}

void
history_add(const char *uri)
{
	size_t	 i, j, insert = 0, oldest = 0;
	char	*u;
	int	 c;

	for (i = 0; i < history.len; ++i) {
		if (history.items[i].ts < history.items[oldest].ts)
			oldest = i;

		if (insert != 0 && insert < i)
			continue;

		c = strcmp(uri, history.items[i].uri);
		if (c == 0) {
			history.items[i].ts = time(NULL);
			history.items[i].dirty = 1;
			history.dirty++;
			autosave_hook();
			return;
		}

		if (c > 0)
			insert = i;
	}

	u = xstrdup(uri);

	/* if history is full, replace the oldest one */
	if (history.len == HISTORY_CAP) {
		free(history.items[oldest].uri);
		history.items[oldest].uri = u;
		history.items[oldest].ts = time(NULL);
		history.items[oldest].dirty = 1;
		history.dirty++;
		history_sort();
		autosave_hook();
		return;
	}

	/* otherwise just insert in the right spot */

	for (j = history.len; j > insert; --j)
		memcpy(&history.items[j], &history.items[j-1],
		    sizeof(history.items[j]));

	history.items[insert].ts = time(NULL);
	history.items[insert].uri = u;
	history.items[insert].dirty = 1;
	history.dirty++;
	history.len++;
	autosave_hook();
}

void
autosave_init(void)
{
	return;
}

void
autosave_timer(int fd, int event, void *data)
{
	save_session();
}

/*
 * Function to be called in "interesting" places where we may want to
 * schedule an autosave (like on new tab or before loading an url.)
 */
void
autosave_hook(void)
{
	struct timeval tv;

	if (autosave <= 0)
		return;

	if (!ev_timer_pending(autosavetimer)) {
		tv.tv_sec = autosave;
		tv.tv_usec = 0;

		autosavetimer = ev_timer(&tv, autosave_timer, NULL);
	}
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

	e = xcalloc(1, sizeof(*e));

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
		} else
			warnx("%s:%zu invalid entry",
			    known_hosts_file, lineno);
	}

	free(line);
	fclose(f);
	return;
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

/*
 * Check if the last time telescope crashed.  The check is done by
 * looking at `crashed_file': if it exists then last time we crashed.
 * Then, while here, touch the file too, it's removed during the
 * teardown.
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

/*
 * Parse and restore a tab from the session file.  The format is:
 *
 *	URL [flags,...] [title]\n
 */
static inline struct tab *
parse_tab_line(char *line, struct tab **ct)
{
	struct tab *tab;
	char *s, *t, *ap;
	const char *uri, *title = "";
	int current = 0, killed = 0;
	size_t tline = 0, cline = 0;

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
		else if (!strncmp(ap, "top=", 4))
			tline = strtonum(ap+4, 0, UINT32_MAX, NULL);
		else if (!strncmp(ap, "cur=", 4))
			cline = strtonum(ap + 4, 0, UINT32_MAX, NULL);
	}

	if (tline > cline) {
		tline = 0;
		cline = 0;
	}

	if ((tab = new_tab(uri, NULL, NULL)) == NULL)
		err(1, "new_tab");
	hist_set_offs(tab->hist, tline, cline);
	strlcpy(tab->buffer.title, title, sizeof(tab->buffer.title));

	if (current)
		*ct = tab;
	else if (killed)
		kill_tab(tab, 1);

	return tab;
}

static void
load_tabs(void)
{
	struct tab	*tab = NULL, *ct = NULL;
	FILE		*session;
	size_t		 lineno = 0, linesize = 0;
	ssize_t		 linelen;
	char		*uri, *line = NULL;

	if ((session = fopen(session_file, "r")) == NULL) {
		new_tab("about:new", NULL, NULL);
		new_tab("about:help", NULL, NULL);
		return;
	}

	while ((linelen = getline(&line, &linesize, session)) != -1) {
		lineno++;

		if (linelen > 0 && line[linelen-1] == '\n')
			line[linelen-1] = '\0';

		if (*line == '<' || *line == '>') {
			uri = line + 1;
			if (*uri != ' ' || tab == NULL) {
				fprintf(stderr, "%s:%zu invalid line\n",
				    session_file, lineno);
				continue;
			}
			uri++;

			if (*line == '>') /* future hist */
				hist_append(tab->hist, uri);
			else
				hist_prepend(tab->hist, uri);
		} else
			tab = parse_tab_line(line, &ct);
	}

	fclose(session);
	free(line);

	if (ct == NULL || TAILQ_EMPTY(&tabshead))
		ct = new_tab("about:new", NULL, NULL);

	switch_to_tab(ct);

	if (last_time_crashed())
		new_tab("about:crash", NULL, NULL);
}

int
load_session(struct ohash *certs)
{
	load_certs(certs);
	load_hist();
	load_tabs();
	return 0;
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
