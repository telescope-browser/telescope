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

#ifndef SESSION_H
#define SESSION_H

struct ohash;

struct session_tab {
	uint32_t	flags;
	char		uri[GEMINI_URL_LEN];
	char		title[TITLE_MAX];
	size_t		top_line;
	size_t		current_line;
};

struct session_tab_hist {
	char		uri[GEMINI_URL_LEN];
	int		future;
};

struct histitem {
	time_t	ts;
	char	uri[GEMINI_URL_LEN];
};

struct history_item {
	time_t	 ts;
	char	*uri;
	int	 dirty;
};

#define HISTORY_CAP 1000
struct history {
	struct history_item	items[HISTORY_CAP];
	size_t			len;
	size_t			dirty;
	size_t			extra;
};
extern struct history history;

void		 switch_to_tab(struct tab *);
unsigned int	 tab_new_id(void);
struct tab	*new_tab(const char *, const char *base, struct tab *);
void		 kill_tab(struct tab *, int);
struct tab	*unkill_tab(void);
void		 free_tab(struct tab *);
void		 stop_tab(struct tab*);

void		 save_session(void);

void		 history_push(struct histitem *);
void		 history_sort(void);
void		 history_add(const char *);

void		 autosave_init(void);
void		 autosave_timer(int, short, void *);
void		 autosave_hook(void);

int		 load_session(struct ohash *);
int		 lock_session(void);

#endif
