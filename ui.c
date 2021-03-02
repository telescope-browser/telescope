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
#include <stdlib.h>
#include <string.h>

#define TAB_CURRENT	0x1

#define CTRL(x)		((x)&0x1F)

static struct event	stdioev, winchev;

static struct tab	*current_tab(void);
static void		 dispatch_stdio(int, short, void*);
static void		 handle_resize(int, short, void*);
static int		 word_bourdaries(const char*, const char*, const char**, const char**);
static void		 wrap_text(const char*, const char*, const char*, const char*);
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

	if (c == CTRL('L')) {
		clear();
		redraw_tab(current_tab());
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

/*
 * Helper function for wrap_text.  Find the end of the current word
 * and the end of the separator after the word.
 */
static int
word_boundaries(const char *s, const char *sep, const char **endword, const char **endspc)
{
	*endword = s;
	*endword = s;

	if (*s == '\0')
		return 0;

	/* find the end of the current world */
	for (; *s != '\0'; ++s) {
		if (strchr(sep, *s) != NULL)
			break;
	}

	*endword = s;

	/* find the end of the separator */
	for (; *s != '\0'; ++s) {
		if (strchr(sep, *s) == NULL)
			break;
	}

	*endspc = s;

	return 1;
}

static inline void
emitline(const char *prfx, size_t zero, size_t *off)
{
	printw("\n%s", prfx);
	*off = zero;
}

static inline void
emitstr(const char **s, size_t len, size_t *off)
{
	size_t i;

	/* printw("%*s", ...) doesn't seem to respect the precision, so... */
	for (i = 0; i < len; ++i)
		addch((*s)[i]);
	*off += len;
	*s += len;
}

/*
 * Wrap the text, prefixing the first line with prfx1 and the
 * following with prfx2, and breaking on characters in the breakon set.
 * The idea is pretty simple: if there is enough space, write the next
 * word; if we are at the start of a line and there's not enough
 * space, hard-split it.
 *
 * TODO: it considers each byte one cell on the screen!
 * TODO: assume strlen(prfx1) == strlen(prfx2)
 */
static void
wrap_text(const char *prfx1, const char *prfx2, const char *line, const char *breakon)
{
	size_t		 zero, off, len, split;
	const char	*endword, *endspc;

	printw("%s", prfx1);
	zero = strlen(prfx1);
	off = zero;

	while (word_boundaries(line, breakon, &endword, &endspc)) {
		len = endword - line;
		if (off + len < COLS) {
			emitstr(&line, len, &off);
		} else {
			emitline(prfx2, zero, &off);
			while (len >= COLS) {
				/* hard wrap */
				printw("%*s", COLS-1, line);
				emitline(prfx2, zero, &off);
				len -= COLS-1;
				line += COLS-1;
			}

			if (len != 0)
				emitstr(&line, len, &off);
		}

		/* print the spaces iff not at bol */
		len = endspc - endword;
		/* line = endspc; */
		if (off != zero) {
			if (off + len > COLS)
				emitline(prfx2, zero, &off);
			else
				emitstr(&line, len, &off);
		}

		line = endspc;
	}

	printw("\n");
}

static void
redraw_tab(struct tab *tab)
{
	struct line	*l;
	const char	*sep = " \t";

	erase();

	TAILQ_FOREACH(l, &tab->page.head, lines) {
		switch (l->type) {
		case LINE_TEXT:
			wrap_text("", "", l->line, sep);
			break;
		case LINE_LINK:
			wrap_text("=> ", "   ", l->line, sep);
			break;
		case LINE_TITLE_1:
			wrap_text("# ", "  ", l->line, sep);
			break;
		case LINE_TITLE_2:
			wrap_text("## ", "   ", l->line, sep);
			break;
		case LINE_TITLE_3:
			wrap_text("### ", "    ", l->line, sep);
			break;
		case LINE_ITEM:
			wrap_text("* ", "  ", l->line, sep);
			break;
		case LINE_QUOTE:
			wrap_text("> ", "> ", l->line, sep);
			break;
		case LINE_PRE_START:
		case LINE_PRE_END:
			printw("```\n");
			break;
		case LINE_PRE_CONTENT:
			printw("%s\n", l->line);
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

