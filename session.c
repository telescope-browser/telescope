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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "defaults.h"
#include "fs.h"
#include "minibuffer.h"
#include "session.h"
#include "ui.h"

struct history	history;

static struct event	 autosaveev;

void
switch_to_tab(struct tab *tab)
{
	current_tab = tab;
	tab->flags &= ~TAB_URGENT;

	if (operating && tab->flags & TAB_LAZY)
		load_url_in_tab(tab, tab->hist_cur->h, NULL, LU_MODE_NOHIST);
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

	if ((tab = calloc(1, sizeof(*tab))) == NULL) {
		event_loopbreak();
		return NULL;
	}

	TAILQ_INIT(&tab->hist.head);
	TAILQ_INIT(&tab->buffer.head);
	TAILQ_INIT(&tab->buffer.page.head);
	evtimer_set(&tab->loadingev, NULL, NULL);

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

	if (evtimer_pending(&tab->loadingev, NULL))
		evtimer_del(&tab->loadingev);

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
	hist_clear(&tab->hist);
	free(tab);
}

void
stop_tab(struct tab *tab)
{
	ui_send_net(IMSG_STOP, tab->id, NULL, 0);
}

static inline void
savetab(FILE *fp, struct tab *tab, int killed)
{
	struct hist	*h;
	size_t		 top_line, current_line;
	int		 future;

	get_scroll_position(tab, &top_line, &current_line);

	fprintf(fp, "%s ", tab->hist_cur->h);
	if (tab == current_tab)
		fprintf(fp, "current,");
	if (killed)
		fprintf(fp, "killed,");

	fprintf(fp, "top=%zu,cur=%zu %s\n", top_line, current_line,
	    tab->buffer.page.title);

	future = 0;
	TAILQ_FOREACH(h, &tab->hist.head, entries) {
		if (h == tab->hist_cur) {
			future = 1;
			continue;
		}

		fprintf(fp, "%s %s\n", future ? ">" : "<", h->h);
	}
}

void
save_session(void)
{
	FILE			*session, *hist;
	struct tab		*tab;
	size_t			 i;

	if (safe_mode)
		return;

	if ((session = fopen(session_file, "w")) == NULL)
		return;

	TAILQ_FOREACH(tab, &tabshead, tabs)
		savetab(session, tab, 0);
	TAILQ_FOREACH(tab, &ktabshead, tabs)
		savetab(session, tab, 1);

	fclose(session);

	if ((hist = fopen(history_file, "a")) == NULL)
		return;

	if (history.dirty) {
		for (i = 0; i < history.len && history.dirty > 0; ++i) {
			if (!history.items[i].dirty)
				continue;
			history.dirty--;
			history.items[i].dirty = 0;

			fprintf(hist, "%lld %s\n",
			    (long long)history.items[i].ts,
			    history.items[i].uri);
		}
		history.dirty = 0;
	}

	fclose(hist);
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

	if ((uri = strdup(hi->uri)) == NULL)
		abort();

	/* don't grow too much; replace the oldest */
	if (history.len == HISTORY_CAP) {
		history.items[oldest].ts = hi->ts;
		free(history.items[oldest].uri);
		history.items[oldest].uri = uri;
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

	if ((u = strdup(uri)) == NULL)
		return;

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
	evtimer_set(&autosaveev, autosave_timer, NULL);
}

void
autosave_timer(int fd, short event, void *data)
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

	if (!evtimer_pending(&autosaveev, NULL)) {
		tv.tv_sec = autosave;
		tv.tv_usec = 0;

		evtimer_add(&autosaveev, &tv);
	}
}
