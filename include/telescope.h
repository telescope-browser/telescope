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

#include <limits.h>
#include <stdio.h>		/* XXX: for parsers.h */

#include "cmd.h"
#include "phos.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define GEMINI_URL_LEN 1024
#define TITLE_MAX 128+1		/* account for NUL too */

#define SIDE_WINDOW_LEFT	0x1
#define SIDE_WINDOW_BOTTOM	0x2

struct imsgev {
	struct imsgbuf	 ibuf;
	void		(*handler)(int, short, void *);
	struct event	 ev;
	short		 events;
};

#define IMSG_DATA_SIZE(imsg)	((imsg).hdr.len - IMSG_HEADER_SIZE)

enum imsg_type {
	/* ui <-> client/fs */
	IMSG_GET,		/* data is URL, peerid the tab id */
	IMSG_GET_FILE,		/* data is path, without \r\n or such */
	IMSG_GET_RAW,		/* get but with an explicit req str */
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
	IMSG_INIT,
	IMSG_TOFU,
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
	IMSG_SESSION_TAB_HIST,
	IMSG_SESSION_END,

	IMSG_HIST_ITEM,		/* struct histitem */
	IMSG_HIST_END,		/* empty */

	IMSG_CTL_OPEN_URL,
};

enum line_type {
	/* text/gemini */
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

	/* text/x-patch */
	LINE_PATCH,
	LINE_PATCH_HDR,
	LINE_PATCH_HUNK_HDR,
	LINE_PATCH_ADD,
	LINE_PATCH_DEL,

	/* minibuffer */
	LINE_COMPL,
	LINE_COMPL_CURRENT,

	/* help */
	LINE_HELP,

	/* download */
	LINE_DOWNLOAD,
	LINE_DOWNLOAD_DONE,
	LINE_DOWNLOAD_INFO,

	/* misc ui */
	LINE_FRINGE,
};

/* for lines: mark as hidden */
#define L_HIDDEN	1

/* for vlines: mark as continuation */
#define L_CONTINUATION	2

struct line {
	enum line_type		 type;
	char			*line;
	char			*alt;
	void			*data;
	int			 flags;
	TAILQ_ENTRY(line)	 lines;
};

struct vline {
	struct line		*parent;
	char			*line;
	int			 flags;
	TAILQ_ENTRY(vline)	 vlines;
};

struct parser;

typedef int	(*printfn)(void *, const char *, ...);

typedef void	(*parserinit)(struct parser *);

typedef int	(*parsechunkfn)(struct parser *, const char *, size_t);
typedef int	(*parserfreefn)(struct parser *);
typedef int	(*parserserial)(struct parser *, FILE *);

typedef void (imsg_handlerfn)(struct imsg*, size_t);

struct parser {
	const char	*name;
	char		 title[128+1];
	char		*buf;
	size_t		 len;
	size_t		 cap;

#define PARSER_IN_BODY	1
#define PARSER_IN_PRE	2
#define PARSER_IN_PATCH_HDR 4
	int		 flags;
	parserinit	 init;
	parsechunkfn	 parse;
	parserfreefn	 free;
	parserserial	 serialize;

	TAILQ_HEAD(, line)	 head;
};

/*
 * differnt types of trust for a certificate.  Following
 * gemini://thfr.info/gemini/modified-trust-verify.gmi
 */
enum trust_state {
	TS_UNKNOWN,
	TS_UNTRUSTED,
	TS_TEMP_TRUSTED,
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
	size_t			line_off;
	size_t			current_off;
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
	struct vline		*top_line;
	struct vline		*current_line;
	size_t			 cpoff;
	TAILQ_HEAD(vhead, vline) head;
};

#define TAB_CURRENT	0x1	/* only for save_session */
#define TAB_KILLED	0x2	/* only for save_session */
#define TAB_URGENT	0x4
#define TAB_LAZY	0x8	/* to lazy load tabs */

#define NEW_TAB_URL	"about:new"

TAILQ_HEAD(tabshead, tab);
extern struct tabshead tabshead;
extern struct tabshead ktabshead;
struct tab {
	TAILQ_ENTRY(tab)	 tabs;
	uint32_t		 id;
	uint32_t		 flags;

	char			*cert;
	enum trust_state	 trust;
	struct proxy		*proxy;
	struct phos_uri		 uri;
	struct histhead		 hist;
	struct hist		*hist_cur;
	size_t			 hist_off;
	char			*last_input_url;

	int			 code;
	char			 meta[GEMINI_URL_LEN];
	int			 redirect_count;

	struct buffer		 buffer;

	short			 loading_anim;
	short			 loading_anim_step;
	struct event		 loadingev;
};

extern TAILQ_HEAD(proxylist, proxy) proxies;
struct proxy {
	char	*match_proto;

	char	*host;
	char	*port;
	int	 proto;

	TAILQ_ENTRY(proxy) proxies;
};

enum {
	PROTO_FINGER,
	PROTO_GEMINI,
	PROTO_GOPHER,
	/* ... */
};

struct get_req {
	int		proto;
	char		host[254];
	char		port[16];
	char		req[1027];
};

struct cmd {
	const char	*cmd;
	void		(*fn)(struct buffer *);
	const char	*descr;
};
extern struct cmd cmds[];

/* defaults.c */
void		 config_init(void);
int		 config_setprfx(const char *, const char *, const char *);
int		 config_setvari(const char *, int);
int		 config_setvars(const char *, char *);
int		 config_setcolor(int, const char *, int, int, int);
int		 config_setattr(const char *, int, int, int);
void		 config_apply_style(void);

/* downloads.c */
extern STAILQ_HEAD(downloads, download) downloads;
struct download {
	uint32_t		 id;
	int			 fd;
	size_t			 bytes;
	char			*path;
	STAILQ_ENTRY(download)	 entries;
};

void		 recompute_downloads(void);
struct download	*enqueue_download(uint32_t, const char *);
void		 dequeue_first_download(void);
struct download	*download_by_id(uint32_t);

/* help.c */
void		 recompute_help(void);

/* hist.c */
void		 hist_clear(struct histhead *);
void		 hist_clear_forward(struct histhead*, struct hist*);
void		 hist_push(struct histhead*, struct hist*);
void		 hist_add_before(struct histhead *, struct hist *, struct hist *);
struct hist	*hist_pop(struct histhead *);

/* mime.c */
int		 setup_parser_for(struct tab*);

/* net.c */
int		 net_main(void);

/* parse.y */
void		 parseconfig(const char *, int);

/* sandbox.c */
void		 sandbox_net_process(void);
void		 sandbox_ui_process(void);

/* telescope.c */
extern int operating;
extern int safe_mode;

#define LU_MODE_NONE	0x0
#define LU_MODE_NOHIST	0x1
#define LU_MODE_NOCACHE	0x2

void		 gopher_send_search_req(struct tab *, const char *);
int		 load_page_from_str(struct tab *, const char *);
void		 load_url(struct tab *, const char *, const char *, int);
void		 load_url_in_tab(struct tab *, const char *, const char *, int);
int		 load_previous_page(struct tab*);
int		 load_next_page(struct tab*);
void		 write_buffer(const char *, struct tab *);
void		 humanify_url(const char *, char *, size_t);
int		 bookmark_page(const char *);
int		 ui_send_net(int, uint32_t, const void *, uint16_t);

/* tofu.c */
void			 tofu_init(struct ohash*, unsigned int, ptrdiff_t);
struct tofu_entry	*tofu_lookup(struct ohash*, const char*, const char*);
void			 tofu_add(struct ohash*, struct tofu_entry*);
int			 tofu_save(struct ohash *, struct tofu_entry *);
void			 tofu_update(struct ohash*, struct tofu_entry*);
int			 tofu_update_persist(struct ohash *, struct tofu_entry *);
void			 tofu_temp_trust(struct ohash *, const char *, const char *, const char *);

/* wrap.c */
void		 erase_buffer(struct buffer *);
void		 empty_linelist(struct buffer*);
void		 empty_vlist(struct buffer*);
int		 wrap_one(struct buffer *, const char *, struct line *, size_t);
int		 wrap_text(struct buffer*, const char*, struct line*, size_t);
int		 hardwrap_text(struct buffer*, struct line*, size_t);
int		 wrap_page(struct buffer *, int width);

#endif /* TELESCOPE_H */
