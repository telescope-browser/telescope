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

	if (operating && tab->flags & TAB_LAZY)
		load_url_in_tab(tab, tab->hist_cur->h, NULL, 0);
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
	if (!operating)
		tab->flags |= TAB_LAZY;
	switch_to_tab(tab);

	if (after != NULL)
		TAILQ_INSERT_AFTER(&tabshead, after, tab, tabs);
	else
		TAILQ_INSERT_TAIL(&tabshead, tab, tabs);

	load_url_in_tab(tab, url, base, 0);
	return tab;
}

/*
 * Free every resource linked to the tab, including the tab itself.
 * Removes the tab from the tablist, but doesn't update the
 * current_tab though.
 */
void
free_tab(struct tab *tab)
{
	stop_tab(tab);
	ui_schedule_redraw();
	autosave_hook();

	if (evtimer_pending(&tab->loadingev, NULL))
		evtimer_del(&tab->loadingev);

	TAILQ_REMOVE(&tabshead, tab, tabs);
	free(tab);
}

void
stop_tab(struct tab *tab)
{
	ui_send_net(IMSG_STOP, tab->id, NULL, 0);
}

void
save_session(void)
{
	struct tab	*tab;
	char		*t;
	int		 flags;

	if (safe_mode)
		return;

	ui_send_fs(IMSG_SESSION_START, 0, NULL, 0);

	TAILQ_FOREACH(tab, &tabshead, tabs) {
		flags = tab->flags;
		if (tab == current_tab)
			flags |= TAB_CURRENT;

		t = tab->hist_cur->h;
		ui_send_fs(IMSG_SESSION_TAB, flags, t, strlen(t)+1);

		t = tab->buffer.page.title;
		ui_send_fs(IMSG_SESSION_TAB_TITLE, 0, t, strlen(t)+1);
	}

	ui_send_fs(IMSG_SESSION_END, 0, NULL, 0);
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
		if (*ap == '\0')
			;
		else if (!strcmp(ap, "current"))
			*flags |= TAB_CURRENT;
		else
			message("unknown tab flag: %s", ap);
	}
}

void
load_last_session(void)
{
	const char	*title;
	char		*nl, *line = NULL;
	uint32_t	 flags;
	size_t		 linesize = 0;
	ssize_t		 linelen;
	FILE		*session;
	struct tab	*tab, *curr = NULL;

	if ((session = fopen(session_file, "r")) == NULL) {
		/* first time? */
		new_tab("about:new", NULL, NULL);
		switch_to_tab(new_tab("about:help", NULL, NULL));
		return;
	}

	while ((linelen = getline(&line, &linesize, session)) != -1) {
		if ((nl = strchr(line, '\n')) != NULL)
			*nl = '\0';
		parse_session_line(line, &title, &flags);
		if ((tab = new_tab(line, NULL, NULL)) == NULL)
			err(1, "new_tab");
		strlcpy(tab->buffer.page.title, title,
		    sizeof(tab->buffer.page.title));
		if (flags & TAB_CURRENT)
			curr = tab;
	}

	if (ferror(session))
		message("error reading %s: %s",
		    session_file, strerror(errno));
	fclose(session);
	free(line);

	if (curr != NULL)
		switch_to_tab(curr);

	if (last_time_crashed())
		switch_to_tab(new_tab("about:crash", NULL, NULL));

	return;
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
