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

#include "compat.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "certs.h"
#include "cmd.h"
#include "control.h"
#include "defaults.h"
#include "fs.h"
#include "hist.h"
#include "iri.h"
#include "mcache.h"
#include "minibuffer.h"
#include "parser.h"
#include "session.h"
#include "telescope.h"
#include "ui.h"
#include "utils.h"

static const struct option longopts[] = {
	{"help",	no_argument,	NULL,	'h'},
	{"safe",	no_argument,	NULL,	'S'},
	{"version",	no_argument,	NULL,	'v'},
	{NULL,		0,		NULL,	0},
};

static const char *opts = "c:hnST:v";

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

static struct imsgev	*iev_net;

struct tabshead		 tabshead = TAILQ_HEAD_INITIALIZER(tabshead);
struct tabshead		 ktabshead = TAILQ_HEAD_INITIALIZER(ktabshead);
struct proxylist	 proxies = TAILQ_HEAD_INITIALIZER(proxies);

enum telescope_process {
	PROC_UI,
	PROC_NET,
};

#define CANNOT_FETCH		0
#define TOO_MANY_REDIRECTS	1
#define MALFORMED_RESPONSE	2
#define UNKNOWN_TYPE_OR_CSET	3
#define UNKNOWN_PROTOCOL	4

/* XXX: keep in sync with normalize_code */
const char *err_pages[70] = {
	[CANNOT_FETCH]		= "# Couldn't load the page\n",
	[TOO_MANY_REDIRECTS]	= "# Too many redirects\n",
	[MALFORMED_RESPONSE]	= "# Got a malformed response\n",
	[UNKNOWN_TYPE_OR_CSET]	= "# Unsupported document format or charset\n",
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
static void		 handle_imsg_check_cert(struct imsg *);
static void		 handle_check_cert_user_choice(int, struct tab *);
static void		 handle_maybe_save_new_cert(int, struct tab *);
static void		 handle_maybe_save_page(int, struct tab *);
static void		 handle_save_page_path(const char *, struct tab *);
static void		 handle_dispatch_imsg(int, short, void *);
static void		 load_about_url(struct tab *, const char *);
static void		 load_file_url(struct tab *, const char *);
static void		 load_finger_url(struct tab *, const char *);
static void		 load_gemini_url(struct tab *, const char *);
static void		 load_gopher_url(struct tab *, const char *);
static void		 load_via_proxy(struct tab *, const char *,
			     struct proxy *);
static void		 make_request(struct tab *, struct get_req *, int,
			     const char *, int);
static void		 do_load_url(struct tab *, const char *, const char *, int);
static pid_t		 start_child(enum telescope_process, const char *, int);
static void		 send_url(const char *);

static const struct proto {
	const char	*schema;
	const char	*port;
	void		 (*loadfn)(struct tab *, const char *);
} protos[] = {
	{"about",	NULL,	load_about_url},
	{"file",	NULL,	load_file_url},
	{"finger",	"79",	load_finger_url},
	{"gemini",	"1965",	load_gemini_url},
	{"gopher",	"70",	load_gopher_url},
	{NULL, NULL, NULL},
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
handle_imsg_check_cert(struct imsg *imsg)
{
	char			*hash;
	const char		*host, *port;
	int			 tofu_res;
	struct ibuf		 ibuf;
	struct tofu_entry	*e;
	struct tab		*tab;
	size_t			 datalen;

	if (imsg_get_ibuf(imsg, &ibuf) == -1 ||
	    ibuf_borrow_str(&ibuf, &hash) == -1)
		abort();
	datalen = strlen(hash);

	if ((tab = tab_by_id(imsg_get_id(imsg))) == NULL)
		return;

	if (tab->proxy != NULL) {
		host = tab->proxy->host;
		port = tab->proxy->port;
	} else {
		host = tab->iri.iri_host;
		port = tab->iri.iri_portstr;
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
		tofu_save(&certs, e);
	} else
		tofu_res = !strcmp(hash, e->hash);

	if (tofu_res) {
		if (e->verified == -1)
			tab->trust = TS_TEMP_TRUSTED;
		else if (e->verified == 1)
			tab->trust = TS_VERIFIED;
		else
			tab->trust = TS_TRUSTED;

		ui_send_net(IMSG_CERT_STATUS, imsg->hdr.peerid, -1,
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
	ui_send_net(IMSG_CERT_STATUS, tab->id, -1, &accept,
	    sizeof(accept));

	if (accept) {
		const char *host, *port;

		host = tab->iri.iri_host;
		port = tab->iri.iri_portstr;

		/*
		 * trust the certificate for this session only.  If
		 * the page results in a redirect while we're asking
		 * the user to save, we'll end up with an invalid
		 * tabid (one request == one tab id).  It also makes
		 * sense to save it for the current session if the
		 * user accepted it.
		 */
		tofu_temp_trust(&certs, host, port, tab->cert);

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
		host = tab->iri.iri_host;
		port = tab->iri.iri_portstr;
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

	tofu_update_persist(&certs, e);

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
handle_request_response(struct tab *tab)
{
	char		 buf[128];

	if (tab->code != 30 && tab->code != 31)
		tab->redirect_count = 0;

	if (tab->code < 10) {	/* internal errors */
		load_page_from_str(tab, err_pages[tab->code]);
	} else if (tab->code < 20) {	/* 1x */
		free(tab->last_input_url);
		tab->last_input_url = strdup(hist_cur(tab->hist));
		if (tab->last_input_url == NULL)
			die();

		load_page_from_str(tab, err_pages[tab->code]);
		ui_require_input(tab, tab->code == 11, ir_select_gemini);
	} else if (tab->code == 20) {
		history_add(hist_cur(tab->hist));
		if (setup_parser_for(tab)) {
			ui_send_net(IMSG_PROCEED, tab->id, -1, NULL, 0);
		} else if (safe_mode) {
			load_page_from_str(tab,
			    err_pages[UNKNOWN_TYPE_OR_CSET]);
		} else {
			hist_prev(tab->hist);
			snprintf(buf, sizeof(buf),
			    "Can't display \"%s\", save it?", tab->meta);
			ui_yornp(buf, handle_maybe_save_page, tab);
		}
	} else if (tab->code < 40) { /* 3x */
		tab->redirect_count++;

		/* TODO: make customizable? */
		if (tab->redirect_count > 5) {
			load_page_from_str(tab,
			    err_pages[TOO_MANY_REDIRECTS]);
		} else
			do_load_url(tab, tab->meta, hist_cur(tab->hist),
			    LU_MODE_NOCACHE);
	} else { /* 4x, 5x & 6x */
		load_page_from_str(tab, err_pages[tab->code]);
		if (tab->code >= 60)
			cmd_use_certificate(&tab->buffer);
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

	if ((f = strrchr(tab->iri.iri_path, '/')) == NULL)
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
	struct download *d;
	int fd;

	if (path == NULL) {
		stop_tab(tab);
		return;
	}

	if ((fd = open(path, O_WRONLY|O_TRUNC|O_CREAT, 0644)) == -1) {
		message("Can't open file %s: %s", path, strerror(errno));
		stop_tab(tab);
		return;
	}

	ui_show_downloads_pane();
	d = enqueue_download(tab->id, path);
	d->fd = fd;
	ui_send_net(IMSG_PROCEED, d->id, -1, NULL, 0);

	/*
	 * Change this tab id, the old one is associated with the
	 * download now.
	 */
	tab->id = tab_new_id();
}

static void
handle_dispatch_imsg(int fd, short event, void *data)
{
	struct imsgev	*iev = data;
	struct imsgbuf	*imsgbuf = &iev->ibuf;
	struct imsg	 imsg;
	struct ibuf	 ibuf;
	struct tab	*tab;
	struct download	*d;
	const char	*h;
	char		*str, *page;
	ssize_t		 n;
	int		 code;

	if (event & EV_READ) {
		if ((n = imsg_read(imsgbuf)) == -1 && errno != EAGAIN)
			err(1, "imsg_read");
		if (n == 0)
			err(1, "connection closed");
	}
	if (event & EV_WRITE) {
		if ((n = msgbuf_write(&imsgbuf->w)) == -1 && errno != EAGAIN)
			err(1, "msgbuf_write");
		if (n == 0)
			err(1, "connection closed");
	}

	for (;;) {
		if ((n = imsg_get(imsgbuf, &imsg)) == -1)
			err(1, "imsg_get");
		if (n == 0)
			break;

		switch (imsg_get_type(&imsg)) {
		case IMSG_ERR:
			if ((tab = tab_by_id(imsg_get_id(&imsg))) == NULL)
				break;
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_borrow_str(&ibuf, &str) == -1)
				die();
			if (asprintf(&page, "# Error loading %s\n\n> %s\n",
			    hist_cur(tab->hist), str) == -1)
				die();
			load_page_from_str(tab, page);
			free(page);
			break;
		case IMSG_CHECK_CERT:
			handle_imsg_check_cert(&imsg);
			break;
		case IMSG_REPLY:
			if ((tab = tab_by_id(imsg_get_id(&imsg))) == NULL)
				break;
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_get(&ibuf, &code, sizeof(code)) == -1 ||
			    ibuf_borrow_str(&ibuf, &str) == -1)
				die();
			if (strlcpy(tab->meta, str, sizeof(tab->meta)) >=
			    sizeof(tab->meta))
				die();
			tab->code = normalize_code(code);
			handle_request_response(tab);
			break;
		case IMSG_BUF:
			if ((tab = tab_by_id(imsg_get_id(&imsg))) == NULL &&
			    ((d = download_by_id(imsg_get_id(&imsg)))) == NULL)
				return;

			if (tab) {
				if (!parser_parse(tab, imsg.data,
				    imsg_get_len(&imsg)))
					die();
				ui_on_tab_refresh(tab);
			} else {
				size_t datalen = imsg_get_len(&imsg);

				d->bytes += datalen;
				write(d->fd, imsg.data, datalen);
				ui_on_download_refresh();
			}
			break;
		case IMSG_EOF:
			if ((tab = tab_by_id(imsg_get_id(&imsg))) == NULL &&
			    ((d = download_by_id(imsg_get_id(&imsg)))) == NULL)
				return;

			if (tab != NULL) {
				if (!parser_free(tab))
					die();
				h = hist_cur(tab->hist);
				if (!strncmp(h, "gemini://", 9) ||
				    !strncmp(h, "gopher://", 9) ||
				    !strncmp(h, "finger://", 9))
					mcache_tab(tab);

				/*
				 * Gemini is handled as soon as a 2x
				 * reply is got.
				 */
				if (!strncmp(h, "finger://", 9) ||
				    !strncmp(h, "gopher://", 9))
					history_add(h);

				ui_on_tab_refresh(tab);
				ui_on_tab_loaded(tab);
			} else {
				close(d->fd);
				d->fd = -1;
				ui_on_download_refresh();
			}
			break;
		default:
			errx(1, "got unknown imsg %d", imsg_get_type(&imsg));
		}

		imsg_free(&imsg);
	}

	imsg_event_add(iev);
}

static void
load_about_url(struct tab *tab, const char *url)
{
	tab->trust = TS_TRUSTED;
	fs_load_url(tab, url);
	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
}

static void
load_file_url(struct tab *tab, const char *url)
{
	tab->trust = TS_TRUSTED;
	fs_load_url(tab, url);
	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
}

static void
load_finger_url(struct tab *tab, const char *url)
{
	struct get_req	 req;
	const char	*path;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->iri.iri_host, sizeof(req.host));
	strlcpy(req.port, tab->iri.iri_portstr, sizeof(req.port));

	/*
	 * Sometimes the finger url have the user as path component
	 * (e.g. finger://thelambdalab.xyz/plugd), sometimes as
	 * userinfo (e.g. finger://cobradile@finger.farm).
	 */
	if (tab->iri.iri_flags & IH_UINFO) {
		strlcpy(req.req, tab->iri.iri_uinfo, sizeof(req.req));
	} else {
		path = tab->iri.iri_path;
		while (*path == '/')
			++path;
		strlcpy(req.req, path, sizeof(req.req));
	}
	strlcat(req.req, "\r\n", sizeof(req.req));

	parser_init(tab, textplain_initparser);
	make_request(tab, &req, PROTO_FINGER, NULL, 0);
}

static void
load_gemini_url(struct tab *tab, const char *url)
{
	struct get_req	 req;
	int		 use_cert = 0;

	if ((tab->client_cert = cert_for(&tab->iri)) != NULL)
		use_cert = 1;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->iri.iri_host, sizeof(req.host));
	strlcpy(req.port, tab->iri.iri_portstr, sizeof(req.port));

	make_request(tab, &req, PROTO_GEMINI, hist_cur(tab->hist), use_cert);
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

static void
load_gopher_url(struct tab *tab, const char *url)
{
	struct get_req	 req;
	int		 type;
	const char	*path;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->iri.iri_host, sizeof(req.host));
	strlcpy(req.port, tab->iri.iri_portstr, sizeof(req.port));

	path = gopher_skip_selector(tab->iri.iri_path, &type);
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
		load_page_from_str(tab, err_pages[10]);
		return;
	default:
		load_page_from_str(tab, "Unknown gopher selector");
		return;
	}

	strlcpy(req.req, path, sizeof(req.req));
	if (tab->iri.iri_flags & IH_QUERY) {
		strlcat(req.req, "?", sizeof(req.req));
		strlcat(req.req, tab->iri.iri_query, sizeof(req.req));
	}
	strlcat(req.req, "\r\n", sizeof(req.req));

	make_request(tab, &req, PROTO_GOPHER, NULL, 0);
}

static void
load_via_proxy(struct tab *tab, const char *url, struct proxy *p)
{
	struct get_req req;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, p->host, sizeof(req.host));
	strlcpy(req.port, p->port, sizeof(req.port));

	tab->proxy = p;

	make_request(tab, &req, p->proto, hist_cur(tab->hist), 0);
}

static void
make_request(struct tab *tab, struct get_req *req, int proto, const char *r,
    int use_cert)
{
	int	 fd = -1;

	stop_tab(tab);
	tab->id = tab_new_id();
	req->proto = proto;

	if (r != NULL) {
		strlcpy(req->req, r, sizeof(req->req));
		strlcat(req->req, "\r\n", sizeof(req->req));
	}

	start_loading_anim(tab);

	if (!use_cert)
		tab->client_cert = NULL;
	if (use_cert && (fd = cert_open(tab->client_cert)) == -1) {
		tab->client_cert = NULL;
		message("failed to open certificate: %s", strerror(errno));
	}

	ui_send_net(IMSG_GET, tab->id, fd, req, sizeof(*req));
}

void
gopher_send_search_req(struct tab *tab, const char *text)
{
	struct get_req	req;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->iri.iri_host, sizeof(req.host));
	strlcpy(req.port, tab->iri.iri_portstr, sizeof(req.port));

	/* +2 to skip /7 */
	strlcpy(req.req, tab->iri.iri_path+2, sizeof(req.req));
	if (tab->iri.iri_flags & IH_QUERY) {
		strlcat(req.req, "?", sizeof(req.req));
		strlcat(req.req, tab->iri.iri_query, sizeof(req.req));
	}

	strlcat(req.req, "\t", sizeof(req.req));
	strlcat(req.req, text, sizeof(req.req));
	strlcat(req.req, "\r\n", sizeof(req.req));

	erase_buffer(&tab->buffer);
	parser_init(tab, gophermap_initparser);

	make_request(tab, &req, PROTO_GOPHER, NULL, 0);
}

void
load_page_from_str(struct tab *tab, const char *page)
{
	parser_init(tab, gemtext_initparser);
	if (!tab->buffer.page.parse(&tab->buffer.page, page, strlen(page)))
		abort();
	if (!tab->buffer.page.free(&tab->buffer.page))
		abort();
	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
}

/*
 * Effectively load the given url in the given tab.
 */
static void
do_load_url(struct tab *tab, const char *url, const char *base, int mode)
{
	const struct proto	*p;
	struct proxy	*proxy;
	int		 nocache = mode & LU_MODE_NOCACHE;
	char		*t;
	char		 buf[1025];

	tab->proxy = NULL;
	tab->trust = TS_UNKNOWN;

	if (iri_parse(base, url, &tab->iri) == -1) {
		if (asprintf(&t, "# error loading %s\n>%s\n",
		    url, "Can't parse the IRI") == -1)
			die();
		hist_set_cur(tab->hist, url);
		load_page_from_str(tab, t);
		free(t);
		return;
	}

	iri_unparse(&tab->iri, buf, sizeof(buf));
	hist_set_cur(tab->hist, buf);

	if (!nocache && mcache_lookup(buf, tab)) {
		ui_on_tab_refresh(tab);
		ui_on_tab_loaded(tab);
		return;
	}

	for (p = protos; p->schema != NULL; ++p) {
		if (!strcmp(tab->iri.iri_scheme, p->schema)) {
			/* patch the port */
			if (*tab->iri.iri_portstr == '\0' &&
			    p->port != NULL)
				iri_setport(&tab->iri, p->port);

			p->loadfn(tab, buf);
			return;
		}
	}

	TAILQ_FOREACH(proxy, &proxies, proxies) {
		if (!strcmp(tab->iri.iri_scheme, proxy->match_proto)) {
			load_via_proxy(tab, url, proxy);
			return;
		}
	}

	load_page_from_str(tab, err_pages[UNKNOWN_PROTOCOL]);
}

/*
 * Load url in tab and handle history.  If a tab is marked as lazy, only
 * prepare the url but don't load it.
 */
void
load_url(struct tab *tab, const char *url, const char *base, int mode)
{
	size_t line_off, curr_off;
	int lazy = tab->flags & TAB_LAZY;
	int dohist = !(mode & LU_MODE_NOHIST);

	if (operating && lazy) {
		tab->flags ^= TAB_LAZY;
		lazy = 0;
	} else if (hist_size(tab->hist) != 0) {
		get_scroll_position(tab, &line_off, &curr_off);
		hist_set_offs(tab->hist, line_off, curr_off);
	}

	if (dohist) {
		if (hist_push(tab->hist, url) == -1) {
			event_loopbreak();
			return;
		}

		strlcpy(tab->buffer.page.title, url,
		    sizeof(tab->buffer.page.title));
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

	if (base == NULL)
		base = hist_cur(tab->hist);

	load_url(tab, url, base, mode);
}

int
load_previous_page(struct tab *tab)
{
	const char	*h;

	if ((h = hist_prev(tab->hist)) == NULL)
		return 0;
	do_load_url(tab, h, NULL, LU_MODE_NONE);
	return 1;
}

int
load_next_page(struct tab *tab)
{
	const char	*h;

	if ((h = hist_next(tab->hist)) == NULL)
		return 0;
	do_load_url(tab, h, NULL, LU_MODE_NONE);
	return 1;
}

void
write_buffer(const char *path, struct tab *tab)
{
	FILE *fp;

	if (path == NULL)
		return;

	if ((fp = fopen(path, "w")) == NULL)
		return;
	if (!parser_serialize(tab, fp))
		message("Failed to save the page.");
	fclose(fp);
}

/*
 * Given a user-entered URL, apply some heuristics to use it if
 * load-url-use-heuristic allows it.
 *
 * - if it's a proper url use it
 * - if it starts with a `./' or a `/' assume its a file:// url
 * - assume it's a default-protocol:// url
 *
 * `ret' (of which len is the size) will be filled with the resulting
 * url.
 */
void
humanify_url(const char *raw, const char *base, char *ret, size_t len)
{
	static struct iri	iri;

	if (load_url_use_heuristic)
		base = NULL;

	if (iri_parse(base, raw, &iri) == 0) {
		iri_unparse(&iri, ret, len);
		return;
	}

	if (!strncmp(raw, "./", 2)) {
		strlcpy(ret, "file://", len);
		strlcat(ret, cwd, len);
		strlcat(ret, "/", len);
		strlcat(ret, raw+2, len);
		return;
	}

	if (*raw == '/') {
		strlcpy(ret, "file://", len);
		strlcat(ret, raw, len);
		return;
	}

	strlcpy(ret, default_protocol, len);
	strlcat(ret, "://", len);
	strlcat(ret, raw, len);
}

int
bookmark_page(const char *url)
{
	FILE *fp;

	if ((fp = fopen(bookmark_file, "a")) == NULL)
		return -1;
	fprintf(fp, "=> %s\n", url);
	fclose(fp);
	return 0;
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
ui_send_net(int type, uint32_t peerid, int fd, const void *data,
    uint16_t datalen)
{
	return imsg_compose_event(iev_net, type, peerid, 0, fd, data,
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
	struct imsgev	 net_ibuf;
	pid_t		 pid;
	int		 control_fd;
	int		 pipe2net[2];
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

	while ((ch = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
		switch (ch) {
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
		else if (proc == PROC_NET)
			return net_main();
		else
			usage(1);
	}

	/* setup keys before reading the config */
	TAILQ_INIT(&global_map.m);
	global_map.unhandled_input = global_key_unbound;
	TAILQ_INIT(&minibuffer_map.m);

	fs_init();
	certs_init(certs_file);
	config_init();
	parseconfig(config_path, fail);
	if (configtest) {
		puts("config OK");
		exit(0);
	}

	if (default_protocol == NULL &&
	    (default_protocol = strdup("gemini")) == NULL)
		err(1, "strdup");

	if (download_path == NULL &&
	    (download_path = strdup("/tmp/")) == NULL)
		errx(1, "strdup");

	if (argc != 0) {
		char *base;

		if (asprintf(&base, "file://%s/", cwd) == -1)
			err(1, "asprintf");

		has_url = 1;
		humanify_url(argv[0], base, url, sizeof(url));

		free(base);
	}

	if (!safe_mode && (sessionfd = lock_session()) == -1) {
		if (has_url) {
			send_url(url);
			exit(0);
		}

		errx(1, "can't lock session, is another instance of "
		    "telescope already running?");
	}

	/* Start children. */
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

	/* Setup event handlers for pipes to net */
	iev_net->events = EV_READ;
	event_set(&iev_net->ev, iev_net->ibuf.fd, iev_net->events,
	    iev_net->handler, iev_net);
	event_add(&iev_net->ev, NULL);

	if (ui_init()) {
		sandbox_ui_process();
		load_session(&certs);
		if (has_url)
			new_tab(url, NULL, NULL);
		operating = 1;
		switch_to_tab(current_tab);
		ui_main_loop();
		ui_end();
	}

	ui_send_net(IMSG_QUIT, 0, -1, NULL, 0);
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

	if (!safe_mode)
		unlink(crashed_file);

	if (!safe_mode && close(sessionfd) == -1)
		err(1, "close(sessionfd = %d)", sessionfd);

	return 0;
}
