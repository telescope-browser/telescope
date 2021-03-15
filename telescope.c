#include "telescope.h"

#include <sys/socket.h>

#include <err.h>
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
	{ "gemini:",	load_gemini_url },
	{ "about:",	load_about_url },
	{ NULL, NULL },
};

static struct imsgbuf	*netibuf, *fsibuf;

static void		 die(void) __attribute__((__noreturn__));
static struct tab	*tab_by_id(uint32_t);
static void		 handle_imsg_err(struct imsg*, size_t);
static void		 handle_imsg_check_cert(struct imsg*, size_t);
static void		 handle_imsg_got_code(struct imsg*, size_t);
static void		 handle_imsg_got_meta(struct imsg*, size_t);
static void		 handle_imsg_buf(struct imsg*, size_t);
static void		 handle_imsg_eof(struct imsg*, size_t);
static void		 handle_imsg_bookmark_ok(struct imsg*, size_t);
static void		 dispatch_imsg(int, short, void*);
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
};

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
	int	tofu_res = 1;

	imsg_compose(netibuf, IMSG_CERT_STATUS, imsg->hdr.peerid, 0, -1, &tofu_res, sizeof(tofu_res));
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

	if (!tab->page.parse(&tab->page, imsg->data, datalen))
		die();

	ui_on_tab_refresh(tab);
}

static void
handle_imsg_eof(struct imsg *imsg, size_t datalen)
{
	struct tab	*t;

	t = tab_by_id(imsg->hdr.peerid);
	if (!t->page.free(&t->page))
		die();

	ui_on_tab_refresh(t);
	ui_on_tab_loaded(t);
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
dispatch_imsg(int fd, short ev, void *d)
{
	struct imsgbuf	*ibuf = d;
	struct imsg	 imsg;
	size_t		 datalen;
	ssize_t		 n;

	if ((n = imsg_read(ibuf)) == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		die();
	}

	if (n == 0)
		_exit(1);

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			die();
		if (n == 0)
			return;
		datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
		handlers[imsg.hdr.type](&imsg, datalen);
		imsg_free(&imsg);
	}
}

static void
load_page_from_str(struct tab *tab, const char *page)
{
	gemtext_initparser(&tab->page);
	if (!tab->page.parse(&tab->page, page, strlen(page)))
		die();
	if (!tab->page.free(&tab->page))
		die();
	ui_on_tab_refresh(tab);
	ui_on_tab_loaded(tab);
}

void
load_about_url(struct tab *tab, const char *url)
{
	char	*m;
	size_t	 len;


	memset(&tab->url, 0, sizeof(tab->url));

	m = strchr(url, ':');
	strlcpy(tab->url.scheme, "about", sizeof(tab->url.scheme));
	strlcpy(tab->url.path, m+1, sizeof(tab->url.path));

	len = sizeof(tab->hist_cur->h)-1;
	strlcpy(tab->hist_cur->h, url, len);

	gemtext_initparser(&tab->page);

	imsg_compose(fsibuf, IMSG_GET, tab->id, 0, -1,
	    tab->hist_cur->h, len+1);
	imsg_flush(fsibuf);
}

void
load_gemini_url(struct tab *tab, const char *url)
{
	const char	*err;
	char		*p;
	size_t		 len;

	if (has_prefix(url, "gemini:")) {
		if (!url_parse(url, &tab->url, &err))
			goto err;
	} else {
		if (!url_resolve_from(&tab->url, url, &err))
			goto err;
	}

	len = sizeof(tab->hist_cur->h)-1;
	url_unparse(&tab->url, tab->hist_cur->h, len);
	imsg_compose(netibuf, IMSG_GET, tab->id, 0, -1,
	    tab->hist_cur->h, len+1);
	imsg_flush(netibuf);
	return;

err:
	if (asprintf(&p, "#error loading %s\n>%s\n",
	    url, err) == -1)
		die();
	strlcpy(tab->hist_cur->h, url, len);
	load_page_from_str(tab, p);
	free(p);
}

static void
do_load_url(struct tab *tab, const char *url)
{
	struct proto *p;

	for (p = protos; p->schema != NULL; ++p) {
		if (has_prefix(url, p->schema)) {
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

int
main(void)
{
	struct imsgbuf	network_ibuf, fs_ibuf;
	int		net_fds[2], fs_fds[2];
	pid_t		pid;

	pid = getpid();

	signal(SIGCHLD, SIG_IGN);

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, fs_fds) == -1)
		err(1, "socketpair");

	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		/* child */
		setproctitle("(%d) fs", pid);
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
		setproctitle("(%d) client", pid);
		close(net_fds[0]);
		close(fs_fds[0]);
		imsg_init(&network_ibuf, net_fds[1]);
		exit(client_main(&network_ibuf));
	default:
		close(net_fds[1]);
		imsg_init(&network_ibuf, net_fds[0]);
		netibuf = &network_ibuf;
	}

	setproctitle("(%d) ui", pid);

	TAILQ_INIT(&tabshead);

	event_init();

	event_set(&netev, netibuf->fd, EV_READ | EV_PERSIST, dispatch_imsg, netibuf);
	event_add(&netev, NULL);

	event_set(&fsev, fsibuf->fd, EV_READ | EV_PERSIST, dispatch_imsg, fsibuf);
	event_add(&fsev, NULL);

	ui_init();

	sandbox_ui_process();

	event_dispatch();

	imsg_compose(netibuf, IMSG_QUIT, 0, 0, -1, NULL, 0);
	imsg_flush(netibuf);

	imsg_compose(fsibuf, IMSG_QUIT, 0, 0, -1, NULL, 0);
	imsg_flush(fsibuf);

	ui_end();

	return 0;
}
