#include <sys/socket.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defaults.h"
#include "pages.h"
#include "parser.h"
#include "telescope.h"
#include "ui.h"

static struct imsgev	*iev_fs, *iev_net;

struct tabshead		 tabshead = TAILQ_HEAD_INITIALIZER(tabshead);
struct proxylist	 proxies = TAILQ_HEAD_INITIALIZER(proxies);

enum telescope_process {
	PROC_UI,
	PROC_FS,
	PROC_NET,
};

/* the first is also the fallback one */
static struct proto protos[] = {
	{ "gemini",	load_gemini_url },
	{ "about",	load_about_url },
	{ NULL, NULL },
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
static void		 do_load_url(struct tab*, const char*);
static pid_t		 start_child(enum telescope_process, const char *, int);
static int		 ui_send_net(int, uint32_t, const void *, uint16_t);
static int		 ui_send_fs(int, uint32_t, const void *, uint16_t);

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

	die();
}

static void
handle_imsg_err(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;
	char		*page;

	tab = tab_by_id(imsg->hdr.peerid);

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

	tab = tab_by_id(imsg->hdr.peerid);

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

	tab = tab_by_id(imsg->hdr.peerid);

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

	tab = tab_by_id(imsg->hdr.peerid);

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
			do_load_url(tab, tab->meta);
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

	tab = tab_by_id(imsg->hdr.peerid);

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

	tab = tab_by_id(imsg->hdr.peerid);

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

	tab = tab_by_id(imsg->hdr.peerid);

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
	dispatch_imsg(iev, ev, handlers, sizeof(handlers));
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

void
load_about_url(struct tab *tab, const char *url)
{
	tab->trust = TS_VERIFIED;

	gemtext_initparser(&tab->buffer.page);

	ui_send_fs(IMSG_GET, tab->id,
	    tab->hist_cur->h, strlen(tab->hist_cur->h)+1);
}

void
load_gemini_url(struct tab *tab, const char *url)
{
	struct get_req	 req;

	stop_tab(tab);
	tab->id = tab_new_id();

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, tab->uri.host, sizeof(req.host));
	strlcpy(req.port, tab->uri.port, sizeof(req.host));

	strlcpy(req.req, tab->hist_cur->h, sizeof(req.req));
	strlcat(req.req, "\r\n", sizeof(req.req));

	req.proto = PROTO_GEMINI;

	ui_send_net(IMSG_GET_RAW, tab->id,
	    &req, sizeof(req));
}

void
load_via_proxy(struct tab *tab, const char *url, struct proxy *p)
{
	struct get_req req;

	stop_tab(tab);
	tab->id = tab_new_id();
	tab->proxy = p;

	memset(&req, 0, sizeof(req));
	strlcpy(req.host, p->host, sizeof(req.host));
	strlcpy(req.port, p->port, sizeof(req.host));

	strlcpy(req.req, tab->hist_cur->h, sizeof(req.req));
	strlcat(req.req, "\r\n", sizeof(req.req));

	req.proto = p->proto;

	ui_send_net(IMSG_GET_RAW, tab->id,
	    &req, sizeof(req));
}

static void
do_load_url(struct tab *tab, const char *url)
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

	memcpy(&uri, &tab->uri, sizeof(tab->uri));
	if (!phos_resolve_uri_from_str(&uri, url, &tab->uri)) {
                if (asprintf(&t, "#error loading %s\n>%s\n",
		    url, "Can't parse the URI") == -1)
			die();
		strlcpy(tab->hist_cur->h, url, sizeof(tab->hist_cur->h));
		load_page_from_str(tab, t);
		free(t);
		return;
	}

	phos_serialize_uri(&tab->uri, tab->hist_cur->h,
	    sizeof(tab->hist_cur->h));

	for (p = protos; p->schema != NULL; ++p) {
		if (!strcmp(tab->uri.scheme, p->schema)) {
			p->loadfn(tab, url);
                        return;
		}
	}

	TAILQ_FOREACH(proxy, &proxies, proxies) {
		if (!strcmp(tab->uri.scheme, proxy->match_proto)) {
			load_via_proxy(tab, url, proxy);
			return;
		}
	}

	protos[0].loadfn(tab, url);
}

void
load_url(struct tab *tab, const char *url)
{
	if (tab->hist_cur != NULL)
		hist_clear_forward(&tab->hist, TAILQ_NEXT(tab->hist_cur, entries));

	if ((tab->hist_cur = calloc(1, sizeof(*tab->hist_cur))) == NULL) {
		event_loopbreak();
		return;
	}

	hist_push(&tab->hist, tab->hist_cur);
	do_load_url(tab, url);
	erase_buffer(&tab->buffer);
}

int
load_previous_page(struct tab *tab)
{
	struct hist	*h;

	if ((h = TAILQ_PREV(tab->hist_cur, mhisthead, entries)) == NULL)
		return 0;
	tab->hist_cur = h;
	do_load_url(tab, h->h);
	return 1;
}

int
load_next_page(struct tab *tab)
{
	struct hist	*h;

	if ((h = TAILQ_NEXT(tab->hist_cur, entries)) == NULL)
		return 0;
	tab->hist_cur = h;
	do_load_url(tab, h->h);
	return 1;
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
	struct tab *tab;

	ui_send_fs(IMSG_SESSION_START, 0, NULL, 0);

	TAILQ_FOREACH(tab, &tabshead, tabs) {
		ui_send_fs(IMSG_SESSION_TAB, 0,
		    tab->hist_cur->h, strlen(tab->hist_cur->h)+1);
	}

	ui_send_fs(IMSG_SESSION_END, 0, NULL, 0);
}

static void
session_new_tab_cb(const char *url)
{
	new_tab(url);
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
	fprintf(stderr, "USAGE: %s [-hn] [-c config] [url]\n", getprogname());
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

	while ((ch = getopt(argc, argv, "c:hnT:")) != -1) {
		switch (ch) {
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
			return client_main();
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
		load_last_session(session_new_tab_cb);
		if (has_url || TAILQ_EMPTY(&tabshead))
			new_tab(url);

		sandbox_ui_process();
		event_dispatch();
		ui_end();
	}

	ui_send_fs(IMSG_QUIT, 0, NULL, 0);
	ui_send_net(IMSG_QUIT, 0, NULL, 0);
	imsg_flush(&iev_fs->ibuf);

	close(sessionfd);

	return 0;
}
