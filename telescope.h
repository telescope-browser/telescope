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

#ifndef TELESCOPE_H
#define TELESCOPE_H

#include <config.h>
#include <url.h>

/* TODO: move in config.h */
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdint.h>
#include <imsg.h>

#define GEMINI_URL_LEN 1024

enum imsg_type {
	IMSG_GET,		/* data is URL, peerid the tab id */
	IMSG_ERR,
	IMSG_CHECK_CERT,
	IMSG_CERT_STATUS,
	IMSG_GOT_CODE,
	IMSG_GOT_META,
	IMSG_PROCEED,
	IMSG_STOP,
	IMSG_BUF,
	IMSG_EOF,
	IMSG_QUIT,
};

enum line_type {
	LINE_TEXT,
	LINE_LINK,
	LINE_TITLE_1,
	LINE_TITLE_2,
	LINE_TITLE_3,
	LINE_ITEM,
	LINE_QUOTE,
	LINE_PRE_START,
	LINE_PRE_CONTENT,
	LINE_PRE_END,
};

struct line {
	enum line_type		 type;
	char			*line;
	char			*alt;
	TAILQ_ENTRY(line)	 lines;
};

struct parser;
struct page;

/* typedef void	(*initparserfn)(struct parser*); */

typedef int	(*parsechunkfn)(struct parser*, const char*, size_t);
typedef int	(*parserfreefn)(struct parser*);

typedef void (imsg_handlerfn)(struct imsg*, size_t);

struct parser {
	char		*buf;
	size_t		 len;
	size_t		 cap;
	int		 flags;
	parsechunkfn	 parse;
	parserfreefn	 free;

	TAILQ_HEAD(, line)	 head;
};

struct ui_state;

extern TAILQ_HEAD(tabshead, tab) tabshead;
struct tab {
	struct parser		 page;
	TAILQ_ENTRY(tab)	 tabs;
	uint32_t		 id;
	uint32_t		 flags;

	int			 code;
	char			 meta[GEMINI_URL_LEN];
	int			 redirect_count;

	struct ui_state		*s;
};

extern struct event		 imsgev;

/* gemini.c */
int		 client_main(struct imsgbuf *b);

/* gemtext.c */
void		 gemtext_initparser(struct parser*);

/* pages.c */
extern const char	*about_new;

#define CANNOT_FETCH		0
#define TOO_MUCH_REDIRECTS	1
extern const char	*err_pages[70];

/* ui.c */
int		 ui_init(void);
int		 ui_on_new_tab(struct tab*);
void		 ui_on_tab_refresh(struct tab*);
void		 ui_end(void);

/* util.c */
int		 mark_nonblock(int);
char		*telescope_strnchr(char*, char, size_t);

#endif /* TELESCOPE_H */
