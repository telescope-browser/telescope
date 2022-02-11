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

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "control.h"
#include "defaults.h"
#include "fs.h"
#include "mcache.h"
#include "minibuffer.h"
#include "parser.h"
#include "session.h"
#include "telescope.h"
#include "ui.h"
#include "utils.h"

static struct option longopts[] = {
	{"colors",      no_argument,    NULL,   'C'},
	{"colours",	no_argument,    NULL,   'C'},
	{"help",	no_argument,	NULL,	'h'},
	{"safe",	no_argument,	NULL,	'S'},
	{"version",	no_argument,	NULL,	'v'},
	{NULL,		0,		NULL,	0},
};

static const char *opts = "Cc:hnST:v";

static int	has_url;
static char	url[GEMINI_URL_LEN];

/*
 * Used to know when we're finished loading.
 */
int			 operating;

/*
 * "Safe" (or "sandobox") mode.  If enabled, Telescope shouldn't write
 * anything to the filesystem or execute external programs.
 */
int			safe_mode;

static struct imsgev	*iev_fs, *iev_net;

struct tabshead		 tabshead = TAILQ_HEAD_INITIALIZER(tabshead);
struct tabshead		 ktabshead = TAILQ_HEAD_INITIALIZER(ktabshead);
struct proxylist	 proxies = TAILQ_HEAD_INITIALIZER(proxies);

enum telescope_process {
	PROC_UI,
	PROC_FS,
	PROC_NET,
};

#define CANNOT_FETCH		0
#define TOO_MUCH_REDIRECTS	1
#define MALFORMED_RESPONSE	2
#define UNKNOWN_TYPE_OR_CSET	3
#define UNKNOWN_PROTOCOL	4

/* XXX: keep in sync with normalize_code */
const char *err_pages[70] = {
	[CANNOT_FETCH]		= "# Couldn't load the page\n",
	[TOO_MUCH_REDIRECTS]	= "# Too much redirects\n",
	[MALFORMED_RESPONSE]	= "# Got a malformed response\n",
	[UNKNOWN_TYPE_OR_CSET]	= "# Unsupported type or charset\n",
	[UNKNOWN_PROTOCOL]	= "# Unknown protocol\n",

	[10] = "# Input required\n",
	[11] = "# Input required\n",
	[40] = "# Temporary failure\n",
	[41] = "# Server unavailable\n",
	[42] = "# CGI error\n",
	[43] = "# Proxy error\n",
	[44] = "# Slow down\n",
	[50] = "# Permanent failure\n",
	[51] = "# Not found\n",
	[52] = "# Gone\n",
	[53] = "# Proxy request refused\n",
	[59] = "# Bad request\n",
	[60] = "# Client certificate required\n",
	[61] = "# Certificate not authorised\n",
	[62] = "# Certificate not valid\n"
};

static void		 die(void) __attribute__((__noreturn__));
static struct tab	*tab_by_id(uint32_t);
static void		 handle_imsg_err(struct imsg *, size_t);
static void		 handle_imsg_check_cert(struct imsg *, size_t);
static void		 handle_check_cert_user_choice(int, struct tab *);
static void		 handle_maybe_save_new_cert(int, struct tab *);
static void		 handle_imsg_got_code(struct imsg *, size_t);
static void		 handle_imsg_got_meta(struct imsg *, size_t);
static void		 handle_maybe_save_page(int, struct tab *);
static void		 handle_save_page_path(const char *, struct tab *);
static void		 handle_imsg_file_opened(struct imsg *, size_t);
static void		 handle_imsg_buf(struct imsg *, size_t);
static void		 handle_imsg_eof(struct imsg *, size_t);
static void		 handle_imsg_tofu(struct imsg *, size_t);
static void		 handle_imsg_bookmark_ok(struct imsg *, size_t);
static void		 handle_imsg_save_cert_ok(struct imsg *, size_t);
static void		 handle_imsg_update_cert_ok(struct imsg *, size_t);
static void		 handle_imsg_session(struct imsg *, size_t);
static void		 handle_dispatch_imsg(int, short, void *);
static int		 load_about_url(struct tab *, const char *);
static int		 load_file_url(struct tab *, const char *);
static int		 load_finger_url(struct tab *, const char *);
static int		 load_gemini_url(struct tab *, const char *);
static int		 load_gopher_url(struct tab *, const char *);
static int		 load_via_proxy(struct tab *, const char *,
			     struct proxy *);
static int		 make_request(struct tab *, struct get_req *, int,
			     const char *);
static int		 make_fs_request(struct tab *, int, const char *);
static int		 do_load_url(struct tab *, const char *, const char *, int);
static pid_t		 start_child(enum telescope_process, const char *, int);
static void		 send_url(const char *);

static struct proto {
	const char	*schema;
	const char	*port;
	int		 (*loadfn)(struct tab *, const char *);
} protos[] = {
	{"about",	NULL,	load_about_url},
	{"file",	NULL,	load_file_url},
	{"finger",	"79",	load_finger_url},
	{"gemini",	"1965",	load_gemini_url},
	{"gopher",	"70",	load_gopher_url},
	{NULL, NULL, NULL},
};

static imsg_handlerfn *handlers[] = {
	[IMSG_ERR] = handle_imsg_err,
	[IMSG_CHECK_CERT] = handle_imsg_check_cert,
	[IMSG_GOT_CODE] = handle_imsg_got_code,
	[IMSG_GOT_META] = handle_imsg_got_meta,
	[IMSG_BUF] = handle_imsg_buf,
	[IMSG_EOF] = handle_imsg_eof,
	[IMSG_TOFU] = handle_imsg_tofu,
	[IMSG_BOOKMARK_OK] = handle_imsg_bookmark_ok,
	[IMSG_SAVE_CERT_OK] = handle_imsg_save_cert_ok,
	[IMSG_UPDATE_CERT_OK] = handle_imsg_update_cert_ok,
	[IMSG_FILE_OPENED] = handle_imsg_file_opened,
	[IMSG_SESSION_TAB] = handle_imsg_session,
	[IMSG_SESSION_TAB_HIST] = handle_imsg_session,
	[IMSG_SESSION_END] = handle_imsg_session,
};

static struct ohash	certs;

static void __attribute__((__noreturn__))
die(void)
{
	abort(); 		/* TODO */
}

static struct tab *
tab_by_id(uint32_t id)
{
	struct tab *t;

	TAILQ_FOREACH(t, &tabshead, tabs) {
		if (t->id == id)
			return t;
	}

	return NULL;
}

static void
handle_imsg_err(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;
	char		*page;

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL)
		return;

	page = imsg->data;
	page[datalen-1] = '\0';

	if (asprintf(&page, "# Error loading %s\n\n> %s\n",
	    tab->hist_cur->h, page) == -1)
		die();
	load_page_from_str(tab, page);
	free(page);
}

static void
handle_imsg_check_cert(struct imsg *imsg, size_t datalen)
{
	const char		*hash, *host, *port;
	int			 tofu_res;
	struct tofu_entry	*e;
	struct tab		*tab;

	hash = imsg->data;
	if (hash[datalen-1] != '\0')
		abort();

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL)
		return;

	if (tab->proxy != NULL) {
		host = tab->proxy->host;
		port = tab->proxy->port;
	} else {
		host = tab->uri.host;
		port = tab->uri.port;
	}

	if ((e = tofu_lookup(&certs, host, port)) == NULL) {
		/* TODO: an update in libressl/libretls changed
		 * significantly.  Find a better approach at storing
		 * the certs! */
		if (datalen > sizeof(e->hash))
			abort();

		tofu_res = 1;	/* trust on first use */
		if ((e = calloc(1, sizeof(*e))) == NULL)
			abort();
		strlcpy(e->domain, host, sizeof(e->domain));
		if (*port != '\0' && strcmp(port, "1965")) {
			strlcat(e->domain, ":", sizeof(e->domain));
			strlcat(e->domain, port, sizeof(e->domain));
		}
		strlcpy(e->hash, hash, sizeof(e->hash));
		tofu_add(&certs, e);
		ui_send_fs(IMSG_SAVE_CERT, tab->id, e, sizeof(*e));
	} else
		tofu_res = !strcmp(hash, e->hash);

	if (tofu_res) {
		if (e->verified == -1)
			tab->trust = TS_TEMP_TRUSTED;
		else if (e->verified == 1)
			tab->trust = TS_VERIFIED;
		else
			tab->trust = TS_TRUSTED;

		ui_send_net(IMSG_CERT_STATUS, imsg->hdr.peerid,
		    &tofu_res, sizeof(tofu_res));
	} else {
		tab->trust = TS_UNTRUSTED;
		load_page_from_str(tab, "# Certificate mismatch\n");
		if ((tab->cert = strdup(hash)) == NULL)
			die();
		ui_yornp("Certificate mismatch.  Proceed?",
		    handle_check_cert_user_choice, tab);
	}
}

static void
handle_check_cert_user_choice(int accept, struct tab *tab)
{
	ui_send_net(IMSG_CERT_STATUS, tab->id, &accept,
	    sizeof(accept));

	if (accept) {
		/*
		 * trust the certificate for this session only.  If
		 * the page results in a redirect while we're asking
		 * the user to save, we'll end up with an invalid
		 * tabid (one request == one tab id).  It also makes
		 * sense to save it for the current session if the
		 * user accepted it.
		 */
		tofu_temp_trust(&certs, tab->uri.host, tab->uri.port,
		    tab->cert);

		if (!safe_mode)
			ui_yornp("Save the new certificate?",
			    handle_maybe_save_new_cert, tab);
		else
			message("Certificate temporarly trusted");
	} else {
		free(tab->cert);
		tab->cert = NULL;
	}
}

static void
handle_maybe_save_new_cert(int accept, struct tab *tab)
{
	struct tofu_entry *e;
	const char *host, *port;

	if (tab->proxy != NULL) {
		host = tab->proxy->host;
		port = tab->proxy->port;
	} else {
		host = tab->uri.host;
		port = tab->uri.port;
	}

	if (!accept)
		goto end;

	if ((e = calloc(1, sizeof(*e))) == NULL)
		die();

	strlcpy(e->domain, host, sizeof(e->domain));
	if (*port != '\0' && strcmp(port, "1965")) {
		strlcat(e->domain, ":", sizeof(e->domain));
		strlcat(e->domain, port, sizeof(e->domain));
	}
	strlcpy(e->hash, tab->cert, sizeof(e->hash));
	ui_send_fs(IMSG_UPDATE_CERT, 0, e, sizeof(*e));

	tofu_update(&certs, e);

	tab->trust = TS_TRUSTED;

end:
	free(tab->cert);
	tab->cert = NULL;
}

static inline int
normalize_code(int n)
{
	if (n < 20) {
		if (n == 10 || n == 11)
			return n;
		return 10;
	} else if (n < 30) {
		return 20;
	} else if (n < 40) {
		if (n == 30 || n == 31)
			return n;
		return 30;
	} else if (n < 50) {
		if (n <= 44)
			return n;
		return 40;
	} else if (n < 60) {
		if (n <= 53 || n == 59)
			return n;
		return 50;
	} else if (n < 70) {
		if (n <= 62)
			return n;
		return 60;
	} else
		return MALFORMED_RESPONSE;
}

static void
handle_imsg_got_code(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL)
		return;

	if (sizeof(tab->code) != datalen)
		die();

	memcpy(&tab->code, imsg->data, sizeof(tab->code));
	tab->code = normalize_code(tab->code);
	if (tab->code != 30 && tab->code != 31)
		tab->redirect_count = 0;
}

static void
handle_imsg_got_meta(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;
	char		 buf[128];

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL)
		return;

	if (sizeof(tab->meta) <= datalen)
		die();

	memcpy(tab->meta, imsg->data, datalen);

	if (tab->code < 10) {	/* internal errors */
		load_page_from_str(tab, err_pages[tab->code]);
	} else if (tab->code < 20) {	/* 1x */
		free(tab->last_input_url);
		tab->last_input_url = strdup(tab->hist_cur->h);
		if (tab->last_input_url == NULL)
			die();

		load_page_from_str(tab, err_pages[tab->code]);
		ui_require_input(tab, tab->code == 11, ir_select_gemini);
	} else if (tab->code == 20) {
		if (setup_parser_for(tab)) {
			ui_send_net(IMSG_PROCEED, tab->id, NULL, 0);
		} else if (safe_mode) {
			load_page_from_str(tab,
			    err_pages[UNKNOWN_TYPE_OR_CSET]);
		} else {
			struct hist *h;

			if ((h = hist_pop(&tab->hist)) != NULL)
				tab->hist_cur = h;
			snprintf(buf, sizeof(buf),
			    "Can't display \"%s\", save it?", tab->meta);
			ui_yornp(buf, handle_maybe_save_page, tab);
		}
	} else if (tab->code < 40) { /* 3x */
		tab->redirect_count++;

		/* TODO: make customizable? */
		if (tab->redirect_count > 5) {
			load_page_from_str(tab,
			    err_pages[TOO_MUCH_REDIRECTS]);
		} else
			do_load_url(tab, tab->meta, NULL, LU_MODE_NOCACHE);
	} else { /* 4x, 5x & 6x */
		load_page_from_str(tab, err_pages[tab->code]);
	}
}

static void
handle_maybe_save_page(int dosave, struct tab *tab)
{
	const char	*f;
	char		 input[PATH_MAX];

	/* XXX: this print a message that is confusing  */
	ui_on_tab_loaded(tab);

	if (!dosave) {
		stop_tab(tab);
		return;
	}

	if ((f = strrchr(tab->uri.path, '/')) == NULL)
		f = "";
	else
		f++;

	strlcpy(input, download_path, sizeof(input));
	strlcat(input, f, sizeof(input));

	ui_read("Save to path", handle_save_page_path, tab, input);
}

static void
handle_save_page_path(const char *path, struct tab *tab)
{
	if (path == NULL) {
		stop_tab(tab);
		return;
	}

	ui_show_downloads_pane();

	enqueue_download(tab->id, path);
	ui_send_fs(IMSG_FILE_OPEN, tab->id, path, strlen(path)+1);

	/*
	 * Change this tab id, the old one is associated with the
	 * download now.
	 */
	tab->id = tab_new_id();
}

static void
handle_imsg_file_opened(struct imsg *imsg, size_t datalen)
{
	struct download	*d;
	const char	*e;

	/*
	 * There are no reason we shouldn't be able to find the
	 * required download.
	 */
	if ((d = download_by_id(imsg->hdr.peerid)) == NULL)
		die();

	if (imsg->fd == -1) {
		e = imsg->data;
		if (e[datalen-1] != '\0')
			die();
		message("Can't open file %s: %s", d->path, e);
	} else {
		d->fd = imsg->fd;
		ui_send_net(IMSG_PROCEED, d->id, NULL, 0);
	}
}

static void
handle_imsg_session(struct imsg *imsg, size_t datalen)
{
	static struct tab	*curr;
	static struct tab	*tab;
	struct session_tab	 st;
	struct session_tab_hist	 sth;
	struct hist		*h;
	int			 first_time;

	/*
	 * The fs process tried to send tabs after it has announced
	 * that he's done.  Something fishy is going on, better die.
	 */
	if (operating)
		die();

	switch (imsg->hdr.type) {
	case IMSG_SESSION_TAB:
		if (datalen != sizeof(st))
			die();

		memcpy(&st, imsg->data, sizeof(st));
		if ((tab = new_tab(st.uri, NULL, NULL)) == NULL)
			die();
		tab->hist_cur->line_off = st.top_line;
		tab->hist_cur->current_off = st.current_line;
		strlcpy(tab->buffer.page.title, st.title,
		    sizeof(tab->buffer.page.title));
		if (st.flags & TAB_CURRENT)
			curr = tab;
		if (st.flags & TAB_KILLED)
			kill_tab(tab, 1);
		break;

	case IMSG_SESSION_TAB_HIST:
		if (tab == NULL || datalen != sizeof(sth))
			die();

		memcpy(&sth, imsg->data, sizeof(sth));
		if (sth.uri[sizeof(sth.uri)-1] != '\0')
			die();

		if ((h = calloc(1, sizeof(*h))) == NULL)
			die();
		strlcpy(h->h, sth.uri, sizeof(h->h));

		if (sth.future)
			hist_push(&tab->hist, h);
		else
			hist_add_before(&tab->hist, tab->hist_cur, h);
		break;

	case IMSG_SESSION_END:
		if (datalen != sizeof(first_time))
			die();
		memcpy(&first_time, imsg->data, sizeof(first_time));
		if (first_time) {
			new_tab("about:new", NULL, NULL);
			curr = new_tab("about:help", NULL, NULL);
		}

		operating = 1;
		if (curr != NULL)
			switch_to_tab(curr);
		if (has_url || TAILQ_EMPTY(&tabshead))
			new_tab(url, NULL, NULL);
		ui_main_loop();
		break;

	default:
		die();
	}
}

static void
handle_imsg_buf(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab = NULL;
	struct download	*d = NULL;

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL &&
	    (d = download_by_id(imsg->hdr.peerid)) == NULL)
		return;

	if (tab != NULL) {
		if (!parser_parse(tab, imsg->data, datalen))
			die();
		ui_on_tab_refresh(tab);
	} else {
		d->bytes += datalen;
		write(d->fd, imsg->data, datalen);
		ui_on_download_refresh();
	}
}

static void
handle_imsg_eof(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab = NULL;
	struct download	*d = NULL;

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL &&
	    (d = download_by_id(imsg->hdr.peerid)) == NULL)
		return;

	if (tab != NULL) {
		if (!parser_free(tab))
			die();
		if (has_prefix(tab->hist_cur->h, "gemini://") ||
		    has_prefix(tab->hist_cur->h, "gopher://"))
			mcache_tab(tab);
		ui_on_tab_refresh(tab);
		ui_on_tab_loaded(tab);
	} else {
		close(d->fd);
		d->fd = -1;
		ui_on_download_refresh();
	}
}

static void
handle_imsg_tofu(struct imsg *imsg, size_t datalen)
{
	struct tofu_entry *e;

	if (operating)
		die();

	if ((e = calloc(1, sizeof(*e))) == NULL)
		die();

	if (datalen != sizeof(*e))
		die();
	memcpy(e, imsg->data, sizeof(*e));
	if (e->domain[sizeof(e->domain)-1] != '\0' ||
	    e->hash[sizeof(e->hash)-1] != '\0')
		die();
	tofu_add(&certs, e);
}

static void
handle_imsg_bookmark_ok(struct imsg *imsg, size_t datalen)
{
	int res;

	if (datalen != sizeof(res))
		die();

	memcpy(&res, imsg->data, sizeof(res));
	if (res == 0)
		message("Added to bookmarks!");
	else
		message("Failed to add to bookmarks: %s",
		    strerror(res));
}

static void
handle_imsg_save_cert_ok(struct imsg *imsg, size_t datalen)
{
	int res;

	if (datalen != sizeof(res))
		die();
	memcpy(&res, imsg->data, datalen);
	if (res != 0)
		message("Failed to save the cert for: %s",
		    strerror(res));
}

static void
handle_imsg_update_cert_ok(struct imsg *imsg, size_t datalen)
{
	int res;

	if (datalen != sizeof(res))
		die();
	memcpy(&res, imsg->data, datalen);
	if (!res)
		message("Failed to update the certificate");
}

static void
handle_dispatch_imsg(int fd, short ev, void *d)
{
	struct imsgev	*iev = d;

	if (dispatch_imsg(iev, ev, handlers, sizeof(handlers)) == -1)
		err(1, "connection closed");
}

static int
load_about_url(struct tab *tab, const char *url)
{
	tab->trust = TS_UNKNOWN;
	parser_init(tab, gemtext_initparser);
	return make_fs_request(tab, IMSG_GET, url);
}

static int
load_file_url(struct tab *tab, const char *url)
{
	tab->trust = TS_UNKNOWN;
	return make_fs_request(tab, IMSG_GET_FILE, tab->uri.path);
}

static int
load_finger_url(struct tab *tab, const char *url)
{
	struct get_req	 req;
	size_t		 len;
	char		*at;

	memset(&req, 0, sizeof(req));
	strlcpy(req.port, tab->uri.port, sizeof(req.port));

	/*
	 * Sometimes the finger url have the user as path component
	 * (e.g. finger://thelambdalab.xyz/plugd), sometimes as
	 * userinfo (e.g. finger://cobradile@finger.farm).
	 */
	if ((at = strchr(tab->uri.host, '@')) != NULL) {
		len = at - tab->uri.host;
		memcpy(req.req, tab->uri.host, len);

		if (len >= sizeof(req.req))
			die();
		req.req[len] = '\0';

		strlcpy(req.host, at+1, sizeof(req.host));
	} else {
		strlcpy(req.host, tab->uri.host, sizeof(req.host));

		/* +1 to skip the initial `/' */
		strlcpy(req.req, tab->uri.path+1, sizeof(req.req));
	}
	strlcat(req.req, "\r\n", sizeof(req.req));

	parser_init(tab, textplain_initparser);
	return make_request(tab, &req, PROTO_FINGER, NULL);
}

static int
load_gemini_url(struct tab *tab, const char *url)
{
	struct get_req	 req;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->uri.host, sizeof(req.host));
	strlcpy(req.port, tab->uri.port, sizeof(req.port));

	return make_request(tab, &req, PROTO_GEMINI, tab->hist_cur->h);
}

static inline const char *
gopher_skip_selector(const char *path, int *ret_type)
{
	*ret_type = 0;

	if (!strcmp(path, "/") || *path == '\0') {
		*ret_type = '1';
		return path;
	}

	if (*path != '/')
		return path;
	path++;

	switch (*ret_type = *path) {
	case '0':
	case '1':
	case '7':
		break;

	default:
		*ret_type = 0;
		path -= 1;
		return path;
	}

	return ++path;
}

static int
load_gopher_url(struct tab *tab, const char *url)
{
	struct get_req	 req;
	int		 type;
	const char	*path;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->uri.host, sizeof(req.host));
	strlcpy(req.port, tab->uri.port, sizeof(req.port));

	path = gopher_skip_selector(tab->uri.path, &type);
	switch (type) {
	case '0':
		parser_init(tab, textplain_initparser);
		break;
	case '1':
		parser_init(tab, gophermap_initparser);
		break;
	case '7':
		free(tab->last_input_url);
		tab->last_input_url = strdup(url);
		if (tab->last_input_url == NULL)
			die();
		ui_require_input(tab, 0, ir_select_gopher);
		return load_page_from_str(tab, err_pages[10]);
	default:
		return load_page_from_str(tab, "Unknown gopher selector");
	}

	strlcpy(req.req, path, sizeof(req.req));
	if (*tab->uri.query != '\0') {
		strlcat(req.req, "?", sizeof(req.req));
		strlcat(req.req, tab->uri.query, sizeof(req.req));
	}
	strlcat(req.req, "\r\n", sizeof(req.req));

	return make_request(tab, &req, PROTO_GOPHER, NULL);
}

static int
load_via_proxy(struct tab *tab, const char *url, struct proxy *p)
{
	struct get_req req;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, p->host, sizeof(req.host));
	strlcpy(req.port, p->port, sizeof(req.port));

	tab->proxy = p;

	return make_request(tab, &req, p->proto, tab->hist_cur->h);
}

static int
make_request(struct tab *tab, struct get_req *req, int proto, const char *r)
{
	stop_tab(tab);
	tab->id = tab_new_id();
	req->proto = proto;

	if (r != NULL) {
		strlcpy(req->req, r, sizeof(req->req));
		strlcat(req->req, "\r\n", sizeof(req->req));
	}

	start_loading_anim(tab);
	ui_send_net(IMSG_GET_RAW, tab->id, req, sizeof(*req));

	/*
	 * So the various load_*_url can `return make_request` and
	 * do_load_url is happy.
	 */
	return 1;
}

static int
make_fs_request(struct tab *tab, int type, const char *r)
{
	stop_tab(tab);
	tab->id = tab_new_id();

	ui_send_fs(type, tab->id, r, strlen(r)+1);

	/*
	 * So load_{about,file}_url can `return make_fs_request` and
	 * do_load_url is happy.
	 */
	return 1;
}

void
gopher_send_search_req(struct tab *tab, const char *text)
{
	struct get_req	req;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->uri.host, sizeof(req.host));
	strlcpy(req.port, tab->uri.port, sizeof(req.port));

	/* +2 to skip /7 */
	strlcpy(req.req, tab->uri.path+2, sizeof(req.req));
	if (*tab->uri.query != '\0') {
		strlcat(req.req, "?", sizeof(req.req));
		strlcat(req.req, tab->uri.query, sizeof(req.req));
	}

	strlcat(req.req, "\t", sizeof(req.req));
	strlcat(req.req, text, sizeof(req.req));
	strlcat(req.req, "\r\n", sizeof(req.req));

	erase_buffer(&tab->buffer);
	parser_init(tab, gophermap_initparser);

	make_request(tab, &req, PROTO_GOPHER, NULL);
}

int
load_page_from_str(struct tab *tab, const char *page)
{
	parser_init(tab, gemtext_initparser);
	if (!tab->buffer.page.parse(&tab->buffer.page, page, strlen(page)))
		abort();
	if (!tab->buffer.page.free(&tab->buffer.page))
		abort();
	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
	return 0;
}

/*
 * Effectively load the given url in the given tab.  Return 1 when
 * loading the page asynchronously, and thus when an erase_buffer can
 * be done right after this function return, or 0 when loading the
 * page synchronously.
 */
static int
do_load_url(struct tab *tab, const char *url, const char *base, int mode)
{
	struct phos_uri	 uri;
	struct proto	*p;
	struct proxy	*proxy;
	int		 nocache = mode & LU_MODE_NOCACHE;
	char		*t;

	tab->proxy = NULL;
	tab->trust = TS_UNKNOWN;

	if (base == NULL)
		memcpy(&uri, &tab->uri, sizeof(tab->uri));
	else
		phos_parse_absolute_uri(base, &uri);

	if (!phos_resolve_uri_from_str(&uri, url, &tab->uri)) {
		if (asprintf(&t, "#error loading %s\n>%s\n",
		    url, "Can't parse the URI") == -1)
			die();
		strlcpy(tab->hist_cur->h, url, sizeof(tab->hist_cur->h));
		load_page_from_str(tab, t);
		free(t);
		return 0;
	}

	phos_serialize_uri(&tab->uri, tab->hist_cur->h,
	    sizeof(tab->hist_cur->h));

	if (!nocache && mcache_lookup(tab->hist_cur->h, tab)) {
		ui_on_tab_refresh(tab);
		ui_on_tab_loaded(tab);
		return 0;
	}

	for (p = protos; p->schema != NULL; ++p) {
		if (!strcmp(tab->uri.scheme, p->schema)) {
			/* patch the port */
			if (*tab->uri.port == '\0' && p->port != NULL)
				strlcpy(tab->uri.port, p->port,
				    sizeof(tab->uri.port));

			return p->loadfn(tab, tab->hist_cur->h);
		}
	}

	TAILQ_FOREACH(proxy, &proxies, proxies) {
		if (!strcmp(tab->uri.scheme, proxy->match_proto))
			return load_via_proxy(tab, url, proxy);
	}

	return load_page_from_str(tab, err_pages[UNKNOWN_PROTOCOL]);
}

/*
 * Load url in tab and handle history.  If a tab is marked as lazy, only
 * prepare the url but don't load it.
 */
void
load_url(struct tab *tab, const char *url, const char *base, int mode)
{
	int lazy = tab->flags & TAB_LAZY;
	int nohist = mode & LU_MODE_NOHIST;

	if (operating && lazy) {
		tab->flags ^= TAB_LAZY;
		lazy = 0;
	} else if (tab->hist_cur != NULL)
		get_scroll_position(tab, &tab->hist_cur->line_off,
		    &tab->hist_cur->current_off);

	if (!nohist && (!lazy || tab->hist_cur == NULL)) {
		if (tab->hist_cur != NULL)
			hist_clear_forward(&tab->hist,
			    TAILQ_NEXT(tab->hist_cur, entries));

		if ((tab->hist_cur = calloc(1, sizeof(*tab->hist_cur)))
		    == NULL) {
			event_loopbreak();
			return;
		}

		strlcpy(tab->buffer.page.title, url,
		    sizeof(tab->buffer.page.title));
		hist_push(&tab->hist, tab->hist_cur);

		if (lazy)
			strlcpy(tab->hist_cur->h, url,
			    sizeof(tab->hist_cur->h));
	}

	if (!lazy)
		do_load_url(tab, url, base, mode);
}

void
load_url_in_tab(struct tab *tab, const char *url, const char *base, int mode)
{
	if (operating) {
		autosave_hook();
		message("Loading %s...", url);
	}

	load_url(tab, url, base, mode);
}

int
load_previous_page(struct tab *tab)
{
	struct hist	*h;

	if ((h = TAILQ_PREV(tab->hist_cur, mhisthead, entries)) == NULL)
		return 0;
	tab->hist_cur = h;
	do_load_url(tab, h->h, NULL, LU_MODE_NONE);
	return 1;
}

int
load_next_page(struct tab *tab)
{
	struct hist	*h;

	if ((h = TAILQ_NEXT(tab->hist_cur, entries)) == NULL)
		return 0;
	tab->hist_cur = h;
	do_load_url(tab, h->h, NULL, LU_MODE_NONE);
	return 1;
}

void
add_to_bookmarks(const char *str)
{
	ui_send_fs(IMSG_BOOKMARK_PAGE, 0,
	    str, strlen(str)+1);
}

/*
 * Given a user-entered URL, apply some heuristics to use it:
 *
 * - if it's a proper url use it
 * - if it starts with a `./' or a `/' assume its a file:// url
 * - assume it's a gemini:// url
 *
 * `ret' (of which len is the size) will be filled with the resulting
 * url.
 */
void
humanify_url(const char *raw, char *ret, size_t len)
{
	struct phos_uri	uri;
	char		buf[PATH_MAX];

	if (phos_parse_absolute_uri(raw, &uri)) {
		strlcpy(ret, raw, len);
		return;
	}

	if (has_prefix(raw, "./")) {
		strlcpy(ret, "file://", len);
		getcwd(buf, sizeof(buf));
		strlcat(ret, buf, len);
		strlcat(ret, "/", len);
		strlcat(ret, raw+2, len);
		return;
	}

	if (*raw == '/') {
		strlcpy(ret, "file://", len);
		strlcat(ret, raw, len);
		return;
	}

	strlcpy(ret, "gemini://", len);
	strlcat(ret, raw, len);
}

static pid_t
start_child(enum telescope_process p, const char *argv0, int fd)
{
	const char	*argv[5];
	int		 argc = 0;
	pid_t		 pid;

	switch (pid = fork()) {
	case -1:
		die();
	case 0:
		break;
	default:
		close(fd);
		return pid;
	}

	if (dup2(fd, 3) == -1)
		err(1, "cannot setup imsg fd");

	argv[argc++] = argv0;
	switch (p) {
	case PROC_UI:
		errx(1, "Can't start ui process");
	case PROC_FS:
		argv[argc++] = "-Tf";
		break;
	case PROC_NET:
		argv[argc++] = "-Tn";
		break;
	}

	if (safe_mode)
		argv[argc++] = "-S";

	argv[argc++] = NULL;
	execvp(argv0, (char *const *)argv);
	err(1, "execvp(%s)", argv0);
}

static void
send_url(const char *url)
{
	struct sockaddr_un	 sun;
	struct imsgbuf		 ibuf;
	int			 ctl_sock;

	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, ctlsock_path, sizeof(sun.sun_path));

	if (connect(ctl_sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", ctlsock_path);

	imsg_init(&ibuf, ctl_sock);
	imsg_compose(&ibuf, IMSG_CTL_OPEN_URL, 0, 0, -1, url,
	    strlen(url) + 1);
	imsg_flush(&ibuf);
	close(ctl_sock);
}

int
ui_send_net(int type, uint32_t peerid, const void *data,
    uint16_t datalen)
{
	return imsg_compose_event(iev_net, type, peerid, 0, -1, data,
	    datalen);
}

int
ui_send_fs(int type, uint32_t peerid, const void *data, uint16_t datalen)
{
	return imsg_compose_event(iev_fs, type, peerid, 0, -1, data,
	    datalen);
}

static void __attribute__((noreturn))
usage(int r)
{
	fprintf(stderr, "USAGE: %s [-ChnSv] [-c config] [url]\n",
	    getprogname());
	fprintf(stderr, "version: " PACKAGE " " VERSION "\n");
	exit(r);
}

int
main(int argc, char * const *argv)
{
	struct imsgev	 net_ibuf, fs_ibuf;
	pid_t		 pid;
	int		 control_fd;
	int		 pipe2net[2], pipe2fs[2];
	int		 ch, configtest = 0, fail = 0;
	int		 proc = -1;
	int		 sessionfd = -1;
	int		 status;
	const char	*argv0;

	argv0 = argv[0];

	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	if (getenv("NO_COLOR") != NULL)
		enable_colors = 0;

	fs_init();

	while ((ch = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
		switch (ch) {
		case 'C':
			exit(ui_print_colors());
		case 'c':
			fail = 1;
			strlcpy(config_path, optarg, sizeof(config_path));
			break;
		case 'n':
			configtest = 1;
			break;
		case 'h':
			usage(0);
		case 'S':
			safe_mode = 1;
			break;
		case 'T':
			switch (*optarg) {
			case 'f':
				proc = PROC_FS;
				break;
			case 'n':
				proc = PROC_NET;
				break;
			default:
				errx(1, "invalid process spec %c",
				    *optarg);
			}
			break;
		case 'v':
			printf("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
			exit(0);
			break;
		default:
			usage(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (proc != -1) {
		if (argc > 0)
			usage(1);
		else if (proc == PROC_FS)
			return fs_main();
		else if (proc == PROC_NET)
			return net_main();
		else
			usage(1);
	}

	if (argc != 0) {
		has_url = 1;
		humanify_url(argv[0], url, sizeof(url));
	}

	/* setup keys before reading the config */
	TAILQ_INIT(&global_map.m);
	global_map.unhandled_input = global_key_unbound;
	TAILQ_INIT(&minibuffer_map.m);

	config_init();
	parseconfig(config_path, fail);
	if (configtest) {
		puts("config OK");
		exit(0);
	}

	if (download_path == NULL &&
	    (download_path = strdup("/tmp/")) == NULL)
		errx(1, "strdup");

	if (!safe_mode && (sessionfd = lock_session()) == -1) {
		if (has_url) {
			send_url(url);
			exit(0);
		}

		errx(1, "can't lock session, is another instance of "
		    "telescope already running?");
	}

	/* Start children. */
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, pipe2fs) == -1)
		err(1, "socketpair");
	start_child(PROC_FS, argv0, pipe2fs[1]);
	imsg_init(&fs_ibuf.ibuf, pipe2fs[0]);
	iev_fs = &fs_ibuf;
	iev_fs->handler = handle_dispatch_imsg;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, pipe2net) == -1)
		err(1, "socketpair");
	start_child(PROC_NET, argv0, pipe2net[1]);
	imsg_init(&net_ibuf.ibuf, pipe2net[0]);
	iev_net = &net_ibuf;
	iev_net->handler = handle_dispatch_imsg;

	setproctitle("ui");

	/* initialize tofu store */
	tofu_init(&certs, 5, offsetof(struct tofu_entry, domain));

	event_init();

	if (!safe_mode) {
		if ((control_fd = control_init(ctlsock_path)) == -1)
			err(1, "control_init %s", ctlsock_path);
		control_listen(control_fd);
	}

	/* initialize the in-memory cache store */
	mcache_init();

	/* Setup event handler for the autosave */
	autosave_init();

	/* Setup event handlers for pipes to fs/net */
	iev_fs->events = EV_READ;
	event_set(&iev_fs->ev, iev_fs->ibuf.fd, iev_fs->events,
	    iev_fs->handler, iev_fs);
	event_add(&iev_fs->ev, NULL);

	iev_net->events = EV_READ;
	event_set(&iev_net->ev, iev_net->ibuf.fd, iev_net->events,
	    iev_net->handler, iev_net);
	event_add(&iev_net->ev, NULL);

	if (ui_init()) {
		sandbox_ui_process();
		ui_send_fs(IMSG_INIT, 0, NULL, 0);
		event_dispatch();
		ui_end();
	}

	ui_send_fs(IMSG_QUIT, 0, NULL, 0);
	ui_send_net(IMSG_QUIT, 0, NULL, 0);
	imsg_flush(&iev_fs->ibuf);
	imsg_flush(&iev_net->ibuf);

	/* wait for children to terminate */
	do {
		pid = wait(&status);
		if (pid == -1) {
			if (errno != EINTR && errno != ECHILD)
				err(1, "wait");
		} else if (WIFSIGNALED(status))
			warnx("child terminated; signal %d", WTERMSIG(status));
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	if (!safe_mode && close(sessionfd) == -1)
		err(1, "close(sessionfd = %d)", sessionfd);

	return 0;
}
