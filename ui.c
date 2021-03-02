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

#include <telescope.h>

#include <curses.h>
#include <event.h>
#include <locale.h>
#include <signal.h>

#define TAB_CURRENT	0x1

static struct event	stdioev, winchev;

static struct tab	*current_tab(void);
static void		 dispatch_stdio(int, short, void*);
static void		 handle_resize(int, short, void*);
static void		 redraw_tab(struct tab*);

static struct tab *
current_tab(void)
{
	struct tab *t;

	TAILQ_FOREACH(t, &tabshead, tabs) {
		if (t->flags & TAB_CURRENT)
			return t;
	}

	/* unreachable */
	abort();
}

static void
dispatch_stdio(int fd, short ev, void *d)
{
	int c;

	c = getch();

	if (c == ERR)
		return;

	if (c == 'q') {
		event_loopbreak();
		return;
	}

	printw("You typed %c\n", c);
	refresh();
}

static void
handle_resize(int sig, short ev, void *d)
{
	endwin();
	refresh();
	clear();

	redraw_tab(current_tab());
}

static void
redraw_tab(struct tab *tab)
{
	struct line	*l;

	erase();

	TAILQ_FOREACH(l, &tab->page.head, lines) {
		switch (l->type) {
		case LINE_TEXT:
			printw("%s\n", l->line);
			break;
		case LINE_LINK:
			printw("=> %s\n", l->line);
			break;
		case LINE_TITLE_1:
			printw("# %s\n", l->line);
			break;
		case LINE_TITLE_2:
			printw("## %s\n", l->line);
			break;
		case LINE_TITLE_3:
			printw("### %s\n", l->line);
			break;
		case LINE_ITEM:
			printw("* %s\n", l->line);
			break;
		case LINE_QUOTE:
			printw("> %s\n", l->line);
			break;
		case LINE_PRE_START:
		case LINE_PRE_END:
			printw("```\n");
			break;
		case LINE_PRE_CONTENT:
			printw("`%s\n", l->line);
			break;
		}
	}

	refresh();
}

int
ui_init(void)
{
	setlocale(LC_ALL, "");

	initscr();
	cbreak();
	noecho();

	nonl();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);

	/* non-blocking input */
	timeout(0);

	mvprintw(0, 0, "");

	event_set(&stdioev, 0, EV_READ | EV_PERSIST, dispatch_stdio, NULL);
	event_add(&stdioev, NULL);

	signal_set(&winchev, SIGWINCH, handle_resize, NULL);
	signal_add(&winchev, NULL);

	return 1;
}

void
ui_on_new_tab(struct tab *tab)
{
	struct tab	*t;

	TAILQ_FOREACH(t, &tabshead, tabs) {
		t->flags &= ~TAB_CURRENT;
	}

	tab->flags = TAB_CURRENT;

	/* TODO: redraw the tab list */
}

void
ui_on_tab_refresh(struct tab *tab)
{
	if (!(tab->flags & TAB_CURRENT))
		return;

	redraw_tab(tab);
}

void
ui_end(void)
{
	endwin();
}

