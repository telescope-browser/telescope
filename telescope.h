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

#include "compat.h"

#include "url.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

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
	int			 flags;
	TAILQ_ENTRY(line)	 lines;
};

struct parser;
struct page;

/* typedef void	(*initparserfn)(struct parser*); */

typedef int	(*parsechunkfn)(struct parser*, const char*, size_t);
typedef int	(*parserfreefn)(struct parser*);

typedef void (imsg_handlerfn)(struct imsg*, size_t);

struct parser {
	const char	*name;
	char		 title[32+1];
	char		*buf;
	size_t		 len;
	size_t		 cap;
	int		 flags;
	parsechunkfn	 parse;
	parserfreefn	 free;

	TAILQ_HEAD(, line)	 head;
};

struct histhead {
	TAILQ_HEAD(mhisthead, hist)	head;
	size_t				len;
};
struct hist {
	char			h[1025];
	TAILQ_ENTRY(hist)	entries;
};

struct ui_state;

extern TAILQ_HEAD(tabshead, tab) tabshead;
struct tab {
	struct parser		 page;
	TAILQ_ENTRY(tab)	 tabs;
	uint32_t		 id;
	uint32_t		 flags;

	char			 urlstr[GEMINI_URL_LEN];
	struct url		 url;

	int			 code;
	char			 meta[GEMINI_URL_LEN];
	int			 redirect_count;

	struct ui_state		*s;
};

struct proto {
	const char	*schema;

	/* should load the given url in the tab.  Optionally, it may
	 * consider the given url as relative to the one already
	 * present in tab.  It must set tab->urlstr to a serialized
	 * human-friendly URL. */
	void		 (*loadfn)(struct tab*, const char*);
};

struct kmap {
	TAILQ_HEAD(map, keymap)	m;
	void			(*unhandled_input)(void);
};

struct keymap {
	int			 meta;
	int			 key;
	struct kmap		 map;
	void			(*fn)(struct tab*);

	TAILQ_ENTRY(keymap)	 keymaps;
};

extern struct event		 imsgev;

/* gemini.c */
int		 client_main(struct imsgbuf *b);

/* gemtext.c */
void		 gemtext_initparser(struct parser*);

/* keymap.c */
int		 kbd(const char*);
const char	*unkbd(int);
int		 kmap_define_key(struct kmap*, const char*, void(*)(struct tab*));

/* mime.c */
int		 setup_parser_for(struct tab*);

/* pages.c */
extern const char	*about_new;

#define CANNOT_FETCH		0
#define TOO_MUCH_REDIRECTS	1
#define MALFORMED_RESPONSE	2
#define UNKNOWN_TYPE_OR_CSET	3
extern const char	*err_pages[70];

/* parser.c */
int		 parser_append(struct parser*, const char*, size_t);
int		 parser_set_buf(struct parser*, const char*, size_t);

/* sandbox.c */
void		 sandbox_network_process(void);

/* telescope.c */
void		 load_about_url(struct tab*, const char*);
void		 load_gemini_url(struct tab*, const char*);
void		 load_url(struct tab*, const char*);
void		 stop_tab(struct tab*);

/* textplain.c */
void		 textplain_initparser(struct parser*);

/* ui.c */
int		 ui_init(void);
void		 ui_on_tab_loaded(struct tab*);
void		 ui_on_tab_refresh(struct tab*);
void		 ui_require_input(struct tab*, int);
void		 ui_end(void);

/* util.c */
int		 mark_nonblock(int);
char		*telescope_strnchr(char*, char, size_t);
int		 has_prefix(const char*, const char*);

#endif /* TELESCOPE_H */
