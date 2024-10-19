/*
 * Copyright (c) 2021, 2024 Omar Polo <op@omarpolo.com>
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

#include <limits.h>
#include <stdio.h>		/* XXX: for parsers.h */

#include "iri.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define GEMINI_URL_LEN 1024
#define TITLE_MAX 128+1		/* account for NUL too */

#define SIDE_WINDOW_LEFT	0x1
#define SIDE_WINDOW_BOTTOM	0x2

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

struct line {
	enum line_type		 type;
	char			*line;
	char			*alt;
	void			*data;

#define L_HIDDEN	0x1
	int			 flags;
	TAILQ_ENTRY(line)	 lines;
};

struct vline {
	struct line		*parent;
	size_t			 from;
	size_t			 len;

#define L_CONTINUATION	0x2
	int			 flags;
	TAILQ_ENTRY(vline)	 vlines;
};

/*
 * different types of trust for a certificate.  Following
 * gemini://thfr.info/gemini/modified-trust-verify.gmi
 */
enum trust_state {
	TS_UNKNOWN,
	TS_UNTRUSTED,
	TS_TEMP_TRUSTED,
	TS_TRUSTED,
	TS_VERIFIED,
};

struct parser;

struct buffer {
	char			 title[128 + 1];
	const char		*mode;
	char			*buf;
	size_t			 len;
	size_t			 cap;

#define PARSER_IN_BODY	1
#define PARSER_IN_PRE	2
#define PARSER_IN_PATCH_HDR 4
	int			 parser_flags;
	const struct parser	*parser;

	size_t			 last_line_off;
	int			 force_redraw;

	int			 curs_x;
	int			 curs_y;
	size_t			 line_off;
	size_t			 line_max;
	struct vline		*top_line;
	struct vline		*current_line;
	size_t			 point_offset;

	TAILQ_HEAD(, line)	 head;
	TAILQ_HEAD(vhead, vline) vhead;
};

#define TAB_CURRENT	0x1	/* only for save_session */
#define TAB_KILLED	0x2	/* only for save_session */
#define TAB_URGENT	0x4
#define TAB_LAZY	0x8	/* to lazy load tabs */

#define NEW_TAB_URL	"about:new"

struct hist;

TAILQ_HEAD(tabshead, tab);
extern struct tabshead tabshead;
extern struct tabshead ktabshead;
struct tab {
	TAILQ_ENTRY(tab)	 tabs;
	uint32_t		 id;
	uint32_t		 flags;

	char			*cert;
	enum trust_state	 trust;
	int			 faulty_gemserver;
	const char		*client_cert;
	int			 client_cert_temp;
	struct proxy		*proxy;
	struct iri		 iri;
	struct hist		*hist;
	char			*last_input_url;

	int			 code;
	char			 meta[GEMINI_URL_LEN];
	int			 redirect_count;

	struct buffer		 buffer;

	short			 loading_anim;
	short			 loading_anim_step;
	unsigned long		 loading_timer;
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

/* downloads.c */
extern STAILQ_HEAD(downloads, download) downloads;
struct download {
	uint32_t		 id;
	int			 fd;
	size_t			 bytes;
	char 			*mime_type;
	char			*path;
	STAILQ_ENTRY(download)	 entries;
};

void		 recompute_downloads(void);
struct download	*enqueue_download(uint32_t, const char *, const char *);
struct download	*download_by_id(uint32_t);
void 	 	 download_finished(struct download *);

/* help.c */
void		 recompute_help(void);

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
void		 load_page_from_str(struct tab *, const char *);
void		 load_url(struct tab *, const char *, const char *, int);
void		 load_url_in_tab(struct tab *, const char *, const char *, int);
int		 load_previous_page(struct tab*);
int		 load_next_page(struct tab*);
void		 write_buffer(const char *, struct tab *);
void		 humanify_url(const char *, const char *, char *, size_t);
int		 bookmark_page(const char *);
int		 ui_send_net(int, uint32_t, int, const void *, uint16_t);

/* wrap.c */
void		 erase_buffer(struct buffer *);
void		 empty_linelist(struct buffer*);
void		 empty_vlist(struct buffer*);
int		 wrap_text(struct buffer*, const char*, struct line*, size_t, int);
int		 wrap_page(struct buffer *, int width);

#endif /* TELESCOPE_H */
