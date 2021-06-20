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

#include "cmd.h"
#include "compat.h"
#include "phos/phos.h"

#include <event.h>

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
	IMSG_UPDATE_CERT,
	IMSG_UPDATE_CERT_OK,

	IMSG_FILE_OPEN,
	IMSG_FILE_OPENED,

	IMSG_SESSION_START,
	IMSG_SESSION_TAB,
	IMSG_SESSION_END,
};

extern char	*new_tab_url;
extern int	 fill_column;
extern int	 olivetti_mode;

struct lineprefix {
	const char	*prfx1;
	const char	*prfx2;
};
extern struct lineprefix line_prefixes[];

struct line_face {
	int prefix_prop;
	int text_prop;
};
extern struct line_face line_faces[];

struct tab_face  {
	int background, tab, current_tab;
};
extern struct tab_face tab_face;

struct modeline_face {
	int background;
};
extern struct modeline_face modeline_face;

struct minibuffer_face {
	int background;
};
extern struct minibuffer_face minibuffer_face;

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

	/*
	 * enough space for ``PROTO:HASH''.  probably isn't a good
	 * idea tho.
	 */
	char	hash[128+1];
	int	verified;
};

struct histhead {
	TAILQ_HEAD(mhisthead, hist)	head;
	size_t				len;
};
struct hist {
	char			h[1025];
	TAILQ_ENTRY(hist)	entries;
};

struct buffer {
	struct parser		 page;

	size_t			 last_line_off;
	int			 force_redraw;

	int			 curs_x;
	int			 curs_y;
	size_t			 line_off;
	size_t			 line_max;
	struct vline		*current_line;
	size_t			 cpoff;
	TAILQ_HEAD(vhead, vline) head;
};


#define TAB_CURRENT	0x1
#define TAB_URGENT	0x2

#define NEW_TAB_URL	"about:new"

extern TAILQ_HEAD(tabshead, tab) tabshead;
struct tab {
	TAILQ_ENTRY(tab)	 tabs;
	uint32_t		 id;
	uint32_t		 flags;

	char			*cert;
	enum trust_state	 trust;
	struct phos_uri		 uri;
	struct histhead		 hist;
	struct hist		*hist_cur;
	size_t			 hist_off;

	int			 code;
	char			 meta[GEMINI_URL_LEN];
	int			 redirect_count;

	struct buffer		 buffer;

	short			 loading_anim;
	short			 loading_anim_step;
	struct event		 loadingev;

	int			 fd;
	size_t			 bytes;
	char			*path;
};

struct proto {
	const char	*schema;

	/*
	 * should load the given url in the tab.  Optionally, it may
	 * consider the given url as relative to the one already
	 * present in tab.  It must set tab->urlstr to a serialized
	 * human-friendly URL.
	 */
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
	void			(*fn)(struct buffer*);

	TAILQ_ENTRY(keymap)	 keymaps;
};

/* defaults.c */
int		 config_setprfx(const char *, int, const char *);
int		 config_setvari(const char *, int);
int		 config_setvars(const char *, char *);

/* fs.c */
int		 fs_init(void);
int		 fs_main(struct imsgbuf*);
int		 load_certs(struct ohash*);
int		 load_last_session(void(*)(const char*));

/* gemini.c */
int		 client_main(struct imsgbuf*);

/* gemtext.c */
void		 gemtext_initparser(struct parser*);

/* hist.c */
void		 hist_clear_forward(struct histhead*, struct hist*);
void		 hist_push(struct histhead*, struct hist*);

/* keymap.c */
int		 kbd(const char*);
const char	*unkbd(int);
int		 kmap_define_key(struct kmap*, const char*, void(*)(struct buffer*));

/* mime.c */
int		 setup_parser_for(struct tab*);

/* pages.c */
extern const char	*about_about;
extern const char	*about_blank;
extern const char	*about_help;
extern const char	*about_new;

#define CANNOT_FETCH		0
#define TOO_MUCH_REDIRECTS	1
#define MALFORMED_RESPONSE	2
#define UNKNOWN_TYPE_OR_CSET	3
extern const char	*err_pages[70];

/* parse.y */
void		 parseconfig(const char *, int);

/* parser.c */
int		 parser_append(struct parser*, const char*, size_t);
int		 parser_set_buf(struct parser*, const char*, size_t);
int		 parser_foreach_line(struct parser*, const char*, size_t, parsechunkfn);

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
void		 save_session(void);

/* textplain.c */
void		 textplain_initparser(struct parser*);

/* tofu.c */
void			 tofu_init(struct ohash*, unsigned int, ptrdiff_t);
struct tofu_entry	*tofu_lookup(struct ohash*, const char*, const char*);
void			 tofu_add(struct ohash*, struct tofu_entry*);
void			 tofu_update(struct ohash*, struct tofu_entry*);

/* ui.c */
extern int	 body_lines;
extern int	 body_cols;
extern int	 in_minibuffer;

struct thiskey {
	short meta;
	int key;
	uint32_t cp;
};
extern struct thiskey thiskey;

extern struct histhead eecmd_history,
	ir_history,
	lu_history,
	read_history;

struct ministate {
	char		*curmesg;

	char		 prompt[64];
	void		 (*donefn)(void);
	void		 (*abortfn)(void);

	char		 buf[1025];
	struct line	 line;
	struct vline	 vline;
	struct buffer	 buffer;

	struct histhead	*history;
	struct hist	*hist_cur;
	size_t		 hist_off;
};
extern struct ministate ministate;

void		 restore_cursor(struct buffer *);
void		 minibuffer_taint_hist(void);
void		 eecmd_self_insert(void);
void		 eecmd_select(void);
void		 ir_self_insert(void);
void		 ir_select(void);
void		 lu_self_insert(void);
void		 lu_select(void);
void		 bp_select(void);
void		 vmessage(const char*, va_list);
void		 message(const char*, ...) __attribute__((format(printf, 1, 2)));
void		 start_loading_anim(struct tab *tab);
void		 load_url_in_tab(struct tab *, const char *);
void		 enter_minibuffer(void(*)(void), void(*)(void), void(*)(void), struct histhead *);
void		 exit_minibuffer(void);
void		 switch_to_tab(struct tab *);
struct tab	*current_tab(void);
struct tab	*new_tab(const char *);
unsigned int	 tab_new_id(void);
int		 ui_init(int, char * const*);
void		 ui_on_tab_loaded(struct tab*);
void		 ui_on_tab_refresh(struct tab*);
const char	*ui_keyname(int);
void		 ui_toggle_side_window(void);
void		 ui_schedule_redraw(void);
void		 ui_require_input(struct tab*, int);
void		 ui_read(const char*, void(*)(const char*, unsigned int), unsigned int);
void		 ui_yornp(const char*, void (*)(int, unsigned int), unsigned int);
void		 ui_end(void);

/* utf.8 */
uint32_t	 utf8_decode(uint32_t*restrict, uint32_t*restrict, uint8_t);
size_t		 utf8_encode(uint32_t, char*);
char		*utf8_nth(char*, size_t);
size_t		 utf8_cplen(char*);
size_t		 utf8_chwidth(uint32_t);
size_t		 utf8_snwidth(const char*, size_t);
size_t		 utf8_swidth(const char*);
size_t		 utf8_swidth_between(const char*, const char*);
char		*utf8_next_cp(const char*);
char		*utf8_prev_cp(const char*, const char*);

/* util.c */
int		 mark_nonblock(int);
int		 has_prefix(const char*, const char*);
int		 unicode_isspace(uint32_t);
int		 unicode_isgraph(uint32_t);
void		 dispatch_imsg(struct imsgbuf*, imsg_handlerfn**, size_t);

/* wrap.c */
void		 empty_linelist(struct buffer*);
void		 empty_vlist(struct buffer*);
int		 wrap_text(struct buffer*, const char*, struct line*, size_t);
int		 hardwrap_text(struct buffer*, struct line*, size_t);

#endif /* TELESCOPE_H */
