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

#include <event.h>

#include "url.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define GEMINI_URL_LEN 1024

enum imsg_type {
	/* ui <-> client/fs */
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

	/* ui <-> fs */
	IMSG_BOOKMARK_PAGE,
	IMSG_BOOKMARK_OK,
	IMSG_SAVE_CERT,
	IMSG_SAVE_CERT_OK,
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

struct vline {
	const struct line	*parent;
	char			*line;
	int			 flags;
	TAILQ_ENTRY(vline)	 vlines;
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

struct ui_state {
	int			 curs_x;
	int			 curs_y;
	size_t			 line_off;
	size_t			 line_max;
	struct vline		*current_line;
	size_t			 line_x;

	short			 loading_anim;
	short			 loading_anim_step;
	struct event		 loadingev;

	TAILQ_HEAD(vhead, vline) head;
};

/*
 * differnt types of trust for a certificate.  Following
 * gemini://thfr.info/gemini/modified-trust-verify.gmi
 */
enum trust_state {
	TS_UNKNOWN,
	TS_UNTRUSTED,
	TS_TRUSTED,
	TS_VERIFIED,
};

struct tofu_entry {
	char	domain[GEMINI_URL_LEN];
	/* enough space for ``PROTO:HASH''.  probably isn't a good
	 * idea thou. */
	char	hash[128+1];
	int	verified;
};

extern TAILQ_HEAD(tabshead, tab) tabshead;
struct tab {
	struct parser		 page;
	TAILQ_ENTRY(tab)	 tabs;
	uint32_t		 id;
	uint32_t		 flags;

	enum trust_state	 trust;
	struct url		 url;
	struct histhead		 hist;
	struct hist		*hist_cur;
	size_t			 hist_off;

	int			 code;
	char			 meta[GEMINI_URL_LEN];
	int			 redirect_count;

	struct ui_state		 s;
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

/* fs.c */
int		 fs_init(void);
int		 fs_main(struct imsgbuf*);
int		 load_certs(struct ohash*);

/* gemini.c */
int		 client_main(struct imsgbuf*);

/* gemtext.c */
void		 gemtext_initparser(struct parser*);

/* hash.c */
void			 telescope_ohash_init(struct ohash*, unsigned int, ptrdiff_t);
struct tofu_entry	*telescope_lookup_tofu(struct ohash*, const char*);
void			 telescope_ohash_insert(struct ohash*, struct tofu_entry*);

/* hist.c */
void		 hist_clear_forward(struct histhead*, struct hist*);
void		 hist_push(struct histhead*, struct hist*);

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
void		 sandbox_ui_process(void);
void		 sandbox_fs_process(void);

/* telescope.c */
void		 load_about_url(struct tab*, const char*);
void		 load_gemini_url(struct tab*, const char*);
void		 load_url(struct tab*, const char*);
int		 load_previous_page(struct tab*);
int		 load_next_page(struct tab*);
void		 stop_tab(struct tab*);
void		 add_to_bookmarks(const char*);

/* textplain.c */
void		 textplain_initparser(struct parser*);

/* ui.c */
int		 ui_init(int, char * const*);
void		 ui_on_tab_loaded(struct tab*);
void		 ui_on_tab_refresh(struct tab*);
void		 ui_require_input(struct tab*, int);
void		 ui_notify(const char*, ...) __attribute__((format(printf, 1, 2)));
void		 ui_end(void);

/* util.c */
int		 mark_nonblock(int);
int		 has_prefix(const char*, const char*);
void		 dispatch_imsg(struct imsgbuf*, imsg_handlerfn**, size_t);

/* wrap.c */
void		 wrap_text(struct tab*, const char*, struct line*, size_t);
int		 hardwrap_text(struct tab*, struct line*, size_t);

#endif /* TELESCOPE_H */
