#include "telescope.h"

#include <sys/socket.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct event		 netev, fsev;
struct tabshead		 tabshead;

/* the first is also the fallback one */
static struct proto protos[] = {
	{ "gemini",	load_gemini_url },
	{ "about",	load_about_url },
	{ NULL, NULL },
};

static struct imsgbuf	*netibuf, *fsibuf;

static void		 die(void) __attribute__((__noreturn__));
static struct tab	*tab_by_id(uint32_t);
static void		 handle_imsg_err(struct imsg*, size_t);
static void		 handle_imsg_check_cert(struct imsg*, size_t);
static void		 handle_check_cert_user_choice(int, unsigned int);
static void		 handle_imsg_got_code(struct imsg*, size_t);
static void		 handle_imsg_got_meta(struct imsg*, size_t);
static void		 handle_imsg_buf(struct imsg*, size_t);
static void		 handle_imsg_eof(struct imsg*, size_t);
static void		 handle_imsg_bookmark_ok(struct imsg*, size_t);
static void		 handle_imsg_save_cert_ok(struct imsg*, size_t);
static void		 handle_dispatch_imsg(int, short, void*);
static void		 load_page_from_str(struct tab*, const char*);
static void		 do_load_url(struct tab*, const char*);

static imsg_handlerfn *handlers[] = {
	[IMSG_ERR] = handle_imsg_err,
	[IMSG_CHECK_CERT] = handle_imsg_check_cert,
	[IMSG_GOT_CODE] = handle_imsg_got_code,
	[IMSG_GOT_META] = handle_imsg_got_meta,
	[IMSG_BUF] = handle_imsg_buf,
	[IMSG_EOF] = handle_imsg_eof,
	[IMSG_BOOKMARK_OK] = handle_imsg_bookmark_ok,
	[IMSG_SAVE_CERT_OK] = handle_imsg_save_cert_ok,
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
	const char		*hash;
	int			 tofu_res;
	struct tofu_entry	*e;
	struct tab		*tab;

	hash = imsg->data;
	if (hash[datalen-1] != '\0')
		abort();

	tab = tab_by_id(imsg->hdr.peerid);

	if ((e = telescope_lookup_tofu(&certs, tab->uri.host)) == NULL) {
		/* TODO: an update in libressl/libretls changed
		 * significantly.  Find a better approach at storing
		 * the certs! */
		if (datalen > sizeof(e->hash))
			abort();

		tofu_res = 1;	/* trust on first use */
		if ((e = calloc(1, sizeof(*e))) == NULL)
			abort();
		strlcpy(e->domain, tab->uri.host, sizeof(e->domain));
		strlcpy(e->hash, hash, sizeof(e->hash));
		telescope_ohash_insert(&certs, e);
		imsg_compose(fsibuf, IMSG_SAVE_CERT, tab->id, 0, -1,
		    e, sizeof(*e));
		imsg_flush(fsibuf);
	} else
		tofu_res = !strcmp(hash, e->hash);

	if (tofu_res) {
		tab->trust = e->verified ? TS_VERIFIED : TS_TRUSTED;
		imsg_compose(netibuf, IMSG_CERT_STATUS, imsg->hdr.peerid, 0, -1,
		    &tofu_res, sizeof(tofu_res));
		imsg_flush(netibuf);
	} else {
		tab->trust = TS_UNTRUSTED;
		load_page_from_str(tab, "# Certificate mismatch\n");
		ui_yornp("Certificate mismatch.  Proceed?",
		    handle_check_cert_user_choice, tab->id);
	}
}

static void
handle_check_cert_user_choice(int accept, unsigned int tabid)
{
	imsg_compose(netibuf, IMSG_CERT_STATUS, tabid, 0, -1,
	    &accept, sizeof(accept));
	imsg_flush(netibuf);
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
			imsg_compose(netibuf, IMSG_PROCEED, tab->id, 0, -1, NULL, 0);
			imsg_flush(netibuf);
		} else {
			load_page_from_str(tab, err_pages[UNKNOWN_TYPE_OR_CSET]);
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
handle_imsg_buf(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;

	tab = tab_by_id(imsg->hdr.peerid);

	if (!tab->window.page.parse(&tab->window.page, imsg->data, datalen))
		die();

	ui_on_tab_refresh(tab);
}

static void
handle_imsg_eof(struct imsg *imsg, size_t datalen)
{
	struct tab	*tab;

	tab = tab_by_id(imsg->hdr.peerid);
	if (!tab->window.page.free(&tab->window.page))
		die();

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
		ui_notify("Added to bookmarks!");
	else
		ui_notify("Failed to add to bookmarks: %s",
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
		ui_notify("Failed to save the cert for: %s",
		    strerror(res));
}

static void
handle_dispatch_imsg(int fd, short ev, void *d)
{
	struct imsgbuf	*ibuf = d;
	dispatch_imsg(ibuf, handlers, sizeof(handlers));
}

static void
load_page_from_str(struct tab *tab, const char *page)
{
	gemtext_initparser(&tab->window.page);
	if (!tab->window.page.parse(&tab->window.page, page, strlen(page)))
		die();
	if (!tab->window.page.free(&tab->window.page))
		die();
	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
}

void
load_about_url(struct tab *tab, const char *url)
{
	tab->trust = TS_VERIFIED;

	gemtext_initparser(&tab->window.page);

	imsg_compose(fsibuf, IMSG_GET, tab->id, 0, -1,
	    tab->hist_cur->h, strlen(tab->hist_cur->h)+1);
	imsg_flush(fsibuf);
}

void
load_gemini_url(struct tab *tab, const char *url)
{
	size_t		 len;

	stop_tab(tab);
	tab->id = tab_new_id();

	len = sizeof(tab->hist_cur->h);
	imsg_compose(netibuf, IMSG_GET, tab->id, 0, -1,
	    tab->hist_cur->h, len);
	imsg_flush(netibuf);
	return;
}

static void
do_load_url(struct tab *tab, const char *url)
{
	struct phos_uri	 uri;
	struct proto	*p;
	char		*t;

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
	empty_vlist(&tab->window);
	empty_linelist(&tab->window);
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
	imsg_compose(netibuf, IMSG_STOP, tab->id, 0, -1, NULL, 0);
	imsg_flush(netibuf);
}

void
add_to_bookmarks(const char *str)
{
	imsg_compose(fsibuf, IMSG_BOOKMARK_PAGE, 0, 0, -1, str, strlen(str)+1);
	imsg_flush(fsibuf);
}

void
save_session(void)
{
	struct tab *tab;

	imsg_compose(fsibuf, IMSG_SESSION_START, 0, 0, -1, NULL, 0);
	imsg_flush(fsibuf);

	TAILQ_FOREACH(tab, &tabshead, tabs) {
		imsg_compose(fsibuf, IMSG_SESSION_TAB, 0, 0, -1,
		    tab->hist_cur->h, strlen(tab->hist_cur->h)+1);
		imsg_flush(fsibuf);
	}

	imsg_compose(fsibuf, IMSG_SESSION_END, 0, 0, -1, NULL, 0);
	imsg_flush(fsibuf);
}

int
main(int argc, char * const *argv)
{
	struct imsgbuf	network_ibuf, fs_ibuf;
	int		net_fds[2], fs_fds[2];

	signal(SIGCHLD, SIG_IGN);

	/* initialize part of the fs layer.  Before starting the UI
	 * and dropping the priviledges we need to read some stuff. */
	fs_init();

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, fs_fds) == -1)
		err(1, "socketpair");

	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		/* child */
		setproctitle("fs");
		close(fs_fds[0]);
		imsg_init(&fs_ibuf, fs_fds[1]);
		exit(fs_main(&fs_ibuf));
	default:
		close(fs_fds[1]);
		imsg_init(&fs_ibuf, fs_fds[0]);
		fsibuf = &fs_ibuf;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, net_fds) == -1)
		err(1, "socketpair");

	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		/* child */
		setproctitle("client");
		close(net_fds[0]);
		close(fs_fds[0]);
		imsg_init(&network_ibuf, net_fds[1]);
		exit(client_main(&network_ibuf));
	default:
		close(net_fds[1]);
		imsg_init(&network_ibuf, net_fds[0]);
		netibuf = &network_ibuf;
	}

	setproctitle("ui");

	telescope_ohash_init(&certs, 5, offsetof(struct tofu_entry, domain));
	load_certs(&certs);

	TAILQ_INIT(&tabshead);

	event_init();

	event_set(&netev, netibuf->fd, EV_READ | EV_PERSIST, handle_dispatch_imsg, netibuf);
	event_add(&netev, NULL);

	event_set(&fsev, fsibuf->fd, EV_READ | EV_PERSIST, handle_dispatch_imsg, fsibuf);
	event_add(&fsev, NULL);

	if (ui_init(argc, argv)) {
		sandbox_ui_process();
		event_dispatch();
		ui_end();
	}

	imsg_compose(netibuf, IMSG_QUIT, 0, 0, -1, NULL, 0);
	imsg_flush(netibuf);

	imsg_compose(fsibuf, IMSG_QUIT, 0, 0, -1, NULL, 0);
	imsg_flush(fsibuf);

	return 0;
}
