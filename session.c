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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defaults.h"
#include "fs.h"
#include "minibuffer.h"
#include "parser.h"
#include "session.h"
#include "ui.h"

static struct event	 autosaveev;

void
switch_to_tab(struct tab *tab)
{
	current_tab = tab;
	tab->flags &= ~TAB_URGENT;

	if (operating && tab->flags & TAB_LAZY) {
		tab->flags ^= TAB_LAZY;
		load_url_in_tab(tab, tab->hist_cur->h, NULL, 1);
	}
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
sendtab(struct tab *tab, int killed)
{
	struct session_tab	 st;
	struct session_tab_hist	 sth;
	struct hist		*h;
	int			 future;

	memset(&st, 0, sizeof(st));

	if (tab == current_tab)
		st.flags |= TAB_CURRENT;
	if (killed)
		st.flags |= TAB_KILLED;

	strlcpy(st.uri, tab->hist_cur->h, sizeof(st.uri));
	strlcpy(st.title, tab->buffer.page.title, sizeof(st.title));
	ui_send_fs(IMSG_SESSION_TAB, 0, &st, sizeof(st));

	future = 0;
	TAILQ_FOREACH(h, &tab->hist.head, entries) {
		if (h == tab->hist_cur) {
			future = 1;
			continue;
		}

		memset(&sth, 0, sizeof(sth));
		strlcpy(sth.uri, h->h, sizeof(sth.uri));
		sth.future = future;
		ui_send_fs(IMSG_SESSION_TAB_HIST, 0, &sth, sizeof(sth));
	}

}

void
save_session(void)
{
	struct tab		*tab;

	if (safe_mode)
		return;

	ui_send_fs(IMSG_SESSION_START, 0, NULL, 0);

	TAILQ_FOREACH(tab, &tabshead, tabs)
		sendtab(tab, 0);
	TAILQ_FOREACH(tab, &ktabshead, tabs)
		sendtab(tab, 1);

	ui_send_fs(IMSG_SESSION_END, 0, NULL, 0);
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
