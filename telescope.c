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

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defaults.h"
#include "minibuffer.h"
#include "parser.h"
#include "telescope.h"
#include "ui.h"

static struct option longopts[] = {
	{"colors",	no_argument,	NULL,	'c'},
	{"help",	no_argument,	NULL,	'h'},
	{"version",	no_argument,	NULL,	'v'},
	{NULL,		0,		NULL,	0},
};

static const char *opts = "Cc:hnT:v";

static struct imsgev	*iev_fs, *iev_net;

struct tabshead		 tabshead = TAILQ_HEAD_INITIALIZER(tabshead);
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

/* XXX: keep in sync with telescope.c:/^normalize_code/ */
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
static void		 handle_imsg_err(struct imsg*, size_t);
static void		 handle_imsg_check_cert(struct imsg*, size_t);
static void		 handle_check_cert_user_choice(int, struct tab *);
static void		 handle_maybe_save_new_cert(int, struct tab *);
static void		 handle_imsg_got_code(struct imsg*, size_t);
static void		 handle_imsg_got_meta(struct imsg*, size_t);
static void		 handle_maybe_save_page(int, struct tab *);
static void		 handle_save_page_path(const char *, struct tab *);
static void		 handle_imsg_file_opened(struct imsg*, size_t);
static void		 handle_imsg_buf(struct imsg*, size_t);
static void		 handle_imsg_eof(struct imsg*, size_t);
static void		 handle_imsg_bookmark_ok(struct imsg*, size_t);
static void		 handle_imsg_save_cert_ok(struct imsg*, size_t);
static void		 handle_imsg_update_cert_ok(struct imsg *, size_t);
static void		 handle_dispatch_imsg(int, short, void*);
static void		 load_page_from_str(struct tab*, const char*);
static void		 load_about_url(struct tab*, const char*);
static void		 load_finger_url(struct tab *, const char *);
static void		 load_gemini_url(struct tab*, const char*);
static void		 load_via_proxy(struct tab *, const char *,
			     struct proxy *);
static void		 make_request(struct tab *, struct get_req *, int,
			     const char *);
static int		 do_load_url(struct tab*, const char *, const char *);
static void		 parse_session_line(char *, const char **, uint32_t *);
static void		 load_last_session(void);
static pid_t		 start_child(enum telescope_process, const char *, int);
static int		 ui_send_net(int, uint32_t, const void *, uint16_t);
static int		 ui_send_fs(int, uint32_t, const void *, uint16_t);

static struct proto protos[] = {
	{"about",	load_about_url},
	{"finger",	load_finger_url},
	{"gemini",	load_gemini_url},
	{NULL, NULL},
};

static imsg_handlerfn *handlers[] = {
	[IMSG_ERR] = handle_imsg_err,
	[IMSG_CHECK_CERT] = handle_imsg_check_cert,
	[IMSG_GOT_CODE] = handle_imsg_got_code,
	[IMSG_GOT_META] = handle_imsg_got_meta,
	[IMSG_BUF] = handle_imsg_buf,
	[IMSG_EOF] = handle_imsg_eof,
	[IMSG_BOOKMARK_OK] = handle_imsg_bookmark_ok,
	[IMSG_SAVE_CERT_OK] = handle_imsg_save_cert_ok,
	[IMSG_UPDATE_CERT_OK] = handle_imsg_update_cert_ok,
	[IMSG_FILE_OPENED] = handle_imsg_file_opened,
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
		 * tabid (one request == one tab id) and crash.  It
		 * also makes sense to save it for the current session
		 * if the user accepted it.
		 */
		tofu_temp_trust(&certs, tab->uri.host, tab->uri.port, tab->cert);

		ui_yornp("Save the new certificate?",
		    handle_maybe_save_new_cert, tab);
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

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL)
		return;

	if (sizeof(tab->meta) <= datalen)
		die();

	memcpy(tab->meta, imsg->data, datalen);

	if (tab->code < 10) {	/* internal errors */
		load_page_from_str(tab, err_pages[tab->code]);
	} else if (tab->code < 20) {	/* 1x */
		load_page_from_str(tab, err_pages[tab->code]);
		ui_require_input(tab, tab->code == 11);
	} else if (tab->code == 20) {
		if (setup_parser_for(tab)) {
			ui_send_net(IMSG_PROCEED, tab->id, NULL, 0);
		} else {
			load_page_from_str(tab, err_pages[UNKNOWN_TYPE_OR_CSET]);
			ui_yornp("Can't display page, wanna save?",
			    handle_maybe_save_page, tab);
		}
	} else if (tab->code < 40) { /* 3x */
		tab->redirect_count++;

		/* TODO: make customizable? */
		if (tab->redirect_count > 5) {
			load_page_from_str(tab,
			    err_pages[TOO_MUCH_REDIRECTS]);
		} else
			do_load_url(tab, tab->meta, NULL);
	} else { /* 4x, 5x & 6x */
		load_page_from_str(tab, err_pages[tab->code]);
	}
}

static void
handle_maybe_save_page(int dosave, struct tab *tab)
{
	if (dosave)
		ui_read("Save to path", handle_save_page_path, tab);
	else
		stop_tab(tab);
}

static void
handle_save_page_path(const char *path, struct tab *tab)
{
	if (path == NULL) {
		stop_tab(tab);
		return;
	}

	tab->path = strdup(path);

	ui_send_fs(IMSG_FILE_OPEN, tab->id, path, strlen(path)+1);
}

static void
handle_imsg_file_opened(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;
	char		*page;
	const char	*e;
	int		 l;

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL) {
		if (imsg->fd != -1)
			close(imsg->fd);
		return;
	}

	if (imsg->fd == -1) {
		stop_tab(tab);

		e = imsg->data;
		if (e[datalen-1] != '\0')
			die();
		l = asprintf(&page, "# Can't open file\n\n> %s: %s\n",
		    tab->path, e);
		if (l == -1)
			die();
		load_page_from_str(tab, page);
		free(page);
	} else {
		tab->fd = imsg->fd;
		ui_send_net(IMSG_PROCEED, tab->id, NULL, 0);
	}
}

static void
handle_imsg_buf(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;
	int		 l;
	char		*page, buf[FMT_SCALED_STRSIZE] = {0};

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL)
		return;

	tab->bytes += datalen;
	if (tab->fd == -1) {
		if (!tab->buffer.page.parse(&tab->buffer.page,
		    imsg->data, datalen))
			die();
	} else {
		write(tab->fd, imsg->data, datalen);
		fmt_scaled(tab->bytes, buf);
		l = asprintf(&page, "Saving to \"%s\"... (%s)\n",
		    tab->path,
		    buf);
		if (l == -1)
			die();
		load_page_from_str(tab, page);
		free(page);
	}

	ui_on_tab_refresh(tab);
}

static void
handle_imsg_eof(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;
	int		 l;
	char		*page, buf[FMT_SCALED_STRSIZE] = {0};

	if ((tab = tab_by_id(imsg->hdr.peerid)) == NULL)
		return;

	if (tab->fd == -1) {
		if (!tab->buffer.page.free(&tab->buffer.page))
			die();
	} else {
		fmt_scaled(tab->bytes, buf);
		l = asprintf(&page, "Saved to \"%s\" (%s)\n",
		    tab->path,
		    buf);
		if (l == -1)
			die();
		load_page_from_str(tab, page);
		free(page);

		close(tab->fd);
		tab->fd = -1;
		free(tab->path);
		tab->path = NULL;
	}

	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
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

static void
load_page_from_str(struct tab *tab, const char *page)
{
	erase_buffer(&tab->buffer);
	gemtext_initparser(&tab->buffer.page);
	if (!tab->buffer.page.parse(&tab->buffer.page, page, strlen(page)))
		die();
	if (!tab->buffer.page.free(&tab->buffer.page))
		die();
	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
}

static void
load_about_url(struct tab *tab, const char *url)
{
	tab->trust = TS_VERIFIED;

	gemtext_initparser(&tab->buffer.page);

	ui_send_fs(IMSG_GET, tab->id,
	    tab->hist_cur->h, strlen(tab->hist_cur->h)+1);
}

static void
load_finger_url(struct tab *tab, const char *url)
{
	struct get_req	 req;
	size_t		 len;
	char		*at;

	memset(&req, 0, sizeof(req));
	if (*tab->uri.port != '\0')
		strlcpy(req.port, tab->uri.port, sizeof(req.port));
	else
		strlcpy(req.port, "79", sizeof(req.port));

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

	textplain_initparser(&tab->buffer.page);
	make_request(tab, &req, PROTO_FINGER, NULL);
}

static void
load_gemini_url(struct tab *tab, const char *url)
{
	struct get_req	 req;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->uri.host, sizeof(req.host));
	strlcpy(req.port, tab->uri.port, sizeof(req.host));

	make_request(tab, &req, PROTO_GEMINI, tab->hist_cur->h);
}

static void
load_via_proxy(struct tab *tab, const char *url, struct proxy *p)
{
	struct get_req req;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, p->host, sizeof(req.host));
	strlcpy(req.port, p->port, sizeof(req.host));

	tab->proxy = p;

	make_request(tab, &req, p->proto, tab->hist_cur->h);
}

static void
make_request(struct tab *tab, struct get_req *req, int proto, const char *r)
{
	stop_tab(tab);
	tab->id = tab_new_id();
	req->proto = proto;

	if (r != NULL) {
		strlcpy(req->req, r, sizeof(req->req));
		strlcat(req->req, "\r\n", sizeof(req->req));
	}

	ui_send_net(IMSG_GET_RAW, tab->id, req, sizeof(*req));
}

/*
 * Effectively load the given url in the given tab.  Return 1 when
 * loading the page asynchronously, and thus when an erase_buffer can
 * be done right after this function return, or 0 when loading the
 * page synchronously.  In this last case, erase_buffer *MUST* be
 * called by the handling function (such as load_page_from_str).
 */
static int
do_load_url(struct tab *tab, const char *url, const char *base)
{
	struct phos_uri	 uri;
	struct proto	*p;
	struct proxy	*proxy;
	char		*t;

	tab->proxy = NULL;

	if (tab->fd != -1) {
		close(tab->fd);
		tab->fd = -1;
		free(tab->path);
		tab->path = NULL;
	}

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

	for (p = protos; p->schema != NULL; ++p) {
		if (!strcmp(tab->uri.scheme, p->schema)) {
			p->loadfn(tab, url);
                        return 1;
		}
	}

	TAILQ_FOREACH(proxy, &proxies, proxies) {
		if (!strcmp(tab->uri.scheme, proxy->match_proto)) {
			load_via_proxy(tab, url, proxy);
			return 1;
		}
	}

	load_page_from_str(tab, err_pages[UNKNOWN_PROTOCOL]);
	return 0;
}

/*
 * Load url in tab.  If tab is marked as lazy, only prepare the url
 * but don't load it.  If tab is lazy and a url was already prepared,
 * do load it!
 */
void
load_url(struct tab *tab, const char *url, const char *base)
{
	int lazy;

	lazy = tab->flags & TAB_LAZY;

	if (lazy && tab->hist_cur != NULL) {
		lazy = 0;
		tab->flags &= ~TAB_LAZY;
	}

	if (!lazy || tab->hist_cur == NULL) {
		if (tab->hist_cur != NULL)
			hist_clear_forward(&tab->hist,
			    TAILQ_NEXT(tab->hist_cur, entries));

		if ((tab->hist_cur = calloc(1, sizeof(*tab->hist_cur))) == NULL) {
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

	if (!lazy && do_load_url(tab, url, base))
		erase_buffer(&tab->buffer);
}

int
load_previous_page(struct tab *tab)
{
	struct hist	*h;

	if ((h = TAILQ_PREV(tab->hist_cur, mhisthead, entries)) == NULL)
		return 0;
	tab->hist_cur = h;
	do_load_url(tab, h->h, NULL);
	return 1;
}

int
load_next_page(struct tab *tab)
{
	struct hist	*h;

	if ((h = TAILQ_NEXT(tab->hist_cur, entries)) == NULL)
		return 0;
	tab->hist_cur = h;
	do_load_url(tab, h->h, NULL);
	return 1;
}

/*
 * Free every resource linked to the tab, including the tab itself.
 * Removes the tab from the tablist, but doesn't update the
 * current_tab though.
 */
void
free_tab(struct tab *tab)
{
	stop_tab(tab);

	if (evtimer_pending(&tab->loadingev, NULL))
		evtimer_del(&tab->loadingev);

	TAILQ_REMOVE(&tabshead, tab, tabs);
	free(tab);
}

void
stop_tab(struct tab *tab)
{
	ui_send_net(IMSG_STOP, tab->id, NULL, 0);

	if (tab->fd != -1) {
		close(tab->fd);
		tab->fd = -1;
		free(tab->path);
		tab->path = NULL;
		load_page_from_str(tab, "Stopped.\n");
	}
}

void
add_to_bookmarks(const char *str)
{
	ui_send_fs(IMSG_BOOKMARK_PAGE, 0,
	    str, strlen(str)+1);
}

void
save_session(void)
{
	struct tab	*tab;
	char		*t;
	int		 flags;

	ui_send_fs(IMSG_SESSION_START, 0, NULL, 0);

	TAILQ_FOREACH(tab, &tabshead, tabs) {
		flags = tab->flags;
		if (tab == current_tab)
			flags |= TAB_CURRENT;

		t = tab->hist_cur->h;
		ui_send_fs(IMSG_SESSION_TAB, flags, t, strlen(t)+1);

		t = tab->buffer.page.title;
		ui_send_fs(IMSG_SESSION_TAB_TITLE, 0, t, strlen(t)+1);
	}

	ui_send_fs(IMSG_SESSION_END, 0, NULL, 0);
}

/*
 * Parse a line of the session file.  The format is:
 *
 *	URL [flags,...] [title]\n
 */
static void
parse_session_line(char *line, const char **title, uint32_t *flags)
{
	char *s, *t, *ap;

	*title = "";
	*flags = 0;
	if ((s = strchr(line, ' ')) == NULL)
		return;

	*s++ = '\0';

        if ((t = strchr(s, ' ')) != NULL) {
		*t++ = '\0';
		*title = t;
	}

	while ((ap = strsep(&s, ",")) != NULL) {
		if (*ap == '\0')
			;
		else if (!strcmp(ap, "current"))
			*flags |= TAB_CURRENT;
		else
			message("unknown tab flag: %s", ap);
	}
}

static void
load_last_session(void)
{
	const char	*title;
	char		*nl, *line = NULL;
	uint32_t	 flags;
	size_t		 linesize = 0;
	ssize_t		 linelen;
	FILE		*session;
	struct tab	*tab, *curr = NULL;

	if ((session = fopen(session_file, "r")) == NULL) {
		/* first time? */
		new_tab("about:new", NULL);
		switch_to_tab(new_tab("about:help", NULL));
		return;
	}

	while ((linelen = getline(&line, &linesize, session)) != -1) {
                if ((nl = strchr(line, '\n')) != NULL)
			*nl = '\0';
		parse_session_line(line, &title, &flags);
		if ((tab = new_tab(line, NULL)) == NULL)
			err(1, "new_tab");
                strlcpy(tab->buffer.page.title, title,
		    sizeof(tab->buffer.page.title));
		if (flags & TAB_CURRENT)
			curr = tab;
	}

	if (ferror(session))
		message("error reading %s: %s",
		    session_file, strerror(errno));
	fclose(session);
	free(line);

	if (curr != NULL)
		switch_to_tab(curr);

	if (last_time_crashed())
		switch_to_tab(new_tab("about:crash", NULL));

	return;
}

static pid_t
start_child(enum telescope_process p, const char *argv0, int fd)
{
	const char	*argv[4];
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

	argv[argc++] = NULL;
	execvp(argv0, (char *const *)argv);
	err(1, "execvp(%s)", argv0);
}

static int
ui_send_net(int type, uint32_t peerid, const void *data,
    uint16_t datalen)
{
	return imsg_compose_event(iev_net, type, peerid, 0, -1, data,
	    datalen);
}

static int
ui_send_fs(int type, uint32_t peerid, const void *data, uint16_t datalen)
{
	return imsg_compose_event(iev_fs, type, peerid, 0, -1, data,
	    datalen);
}

static void __attribute__((noreturn))
usage(int r)
{
	fprintf(stderr, "USAGE: %s [-hnv] [-c config] [url]\n",
	    getprogname());
	fprintf(stderr, "version: " PACKAGE " " VERSION "\n");
	exit(r);
}

int
main(int argc, char * const *argv)
{
	struct imsgev	 net_ibuf, fs_ibuf;
	int		 pipe2net[2], pipe2fs[2];
	int		 ch, configtest = 0, fail = 0;
	int		 has_url = 0;
	int		 proc = -1;
	int		 sessionfd;
	char		 path[PATH_MAX];
	const char	*url = NEW_TAB_URL;
	const char	*argv0;

	argv0 = argv[0];

	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	if (getenv("NO_COLOR") != NULL)
		enable_colors = 0;

	strlcpy(path, getenv("HOME"), sizeof(path));
	strlcat(path, "/.telescope/config", sizeof(path));

	while ((ch = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
		switch (ch) {
		case 'C':
			exit(ui_print_colors());
		case 'c':
			fail = 1;
			strlcpy(path, optarg, sizeof(path));
			break;
		case 'n':
			configtest = 1;
			break;
		case 'h':
			usage(0);
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
		url = argv[0];
	}

	/* setup keys before reading the config */
	TAILQ_INIT(&global_map.m);
	global_map.unhandled_input = global_key_unbound;
	TAILQ_INIT(&minibuffer_map.m);

	config_init();
	parseconfig(path, fail);
	if (configtest){
		puts("config OK");
		exit(0);
	}

	fs_init();
	if ((sessionfd = lock_session()) == -1)
		errx(1, "can't lock session, is another instance of "
		    "telescope already running?");

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

	/* initialize tofu & load certificates */
	tofu_init(&certs, 5, offsetof(struct tofu_entry, domain));
	load_certs(&certs);

	event_init();

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
		load_last_session();
		if (has_url || TAILQ_EMPTY(&tabshead))
			new_tab(url, NULL);

		sandbox_ui_process();
		ui_main_loop();
		ui_end();
	}

	ui_send_fs(IMSG_QUIT, 0, NULL, 0);
	ui_send_net(IMSG_QUIT, 0, NULL, 0);
	imsg_flush(&iev_fs->ibuf);
	imsg_flush(&iev_net->ibuf);

	close(sessionfd);

	return 0;
}
