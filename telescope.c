#include "telescope.h"

#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct event		 imsgev;
struct tabshead		 tabshead;

struct proto protos[] = {
	{ "gemini:",	load_gemini_url },
	{ "about:",	load_about_url },
	{ NULL, NULL },
};

static struct imsgbuf	*ibuf;

static void	handle_imsg_err(struct imsg*, size_t);
static void	handle_imsg_check_cert(struct imsg*, size_t);
static void	handle_imsg_got_code(struct imsg*, size_t);
static void	handle_imsg_got_meta(struct imsg*, size_t);
static void	handle_imsg_buf(struct imsg*, size_t);
static void	handle_imsg_eof(struct imsg*, size_t);

static void	load_page_from_str(struct tab*, const char*);

static imsg_handlerfn *handlers[] = {
	[IMSG_ERR] = handle_imsg_err,
	[IMSG_CHECK_CERT] = handle_imsg_check_cert,
	[IMSG_GOT_CODE] = handle_imsg_got_code,
	[IMSG_GOT_META] = handle_imsg_got_meta,
	[IMSG_BUF] = handle_imsg_buf,
	[IMSG_EOF] = handle_imsg_eof,
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
	    tab->urlstr, page) == -1)
		die();
	load_page_from_str(tab, page);
	free(page);
}

static void
handle_imsg_check_cert(struct imsg *imsg, size_t datalen)
{
	int	tofu_res = 1;

	imsg_compose(ibuf, IMSG_CERT_STATUS, imsg->hdr.peerid, 0, -1, &tofu_res, sizeof(tofu_res));
	imsg_flush(ibuf);
}

static void
handle_imsg_got_code(struct imsg *imsg, size_t datalen)
{
	const char	*errpage;
	struct tab	*tab;

	tab = tab_by_id(imsg->hdr.peerid);

	if (sizeof(tab->code) != datalen)
		die();
	memcpy(&tab->code, imsg->data, sizeof(tab->code));

        if (tab->code < 20) {
		if (tab->code != 10 && tab->code != 11)
			tab->code = 10;
	} else if (tab->code < 30)
		tab->code = 20;
	else if (tab->code < 40)
		tab->code = 30;
	else if (tab->code < 50)
		tab->code = 40;
	else if (tab->code < 60)
		tab->code = 50;
	else
		tab->code = 60;

	if (tab->code != 30)
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

	if (tab->code != 30)
		tab->redirect_count = 0;

	if (tab->code == 20) {
		/* TODO: parse the MIME type */
		gemtext_initparser(&tab->page);
		imsg_compose(ibuf, IMSG_PROCEED, tab->id, 0, -1, NULL, 0);
		imsg_flush(ibuf);
		return;
	}

	if (tab->code == 30) {
		tab->redirect_count++;

		/* TODO: make customizable? */
		if (tab->redirect_count > 5) {
			load_page_from_str(tab,
			    err_pages[TOO_MUCH_REDIRECTS]);
			return;
		}

		load_url(tab, tab->meta);
		return;
	}

	/* 4x, 5x or 6x */
	load_page_from_str(tab, err_pages[tab->code]);
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
dispatch_imsg(int fd, short ev, void *d)
{
	struct imsg	imsg;
	size_t		datalen;
	ssize_t		n;

	if ((n = imsg_read(ibuf)) == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		die();
	}

	if (n == 0) {
		fprintf(stderr, "other side is dead\n");
		exit(0);
	}

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
	char *m;

	memset(tab->urlstr, 0, sizeof(tab->urlstr));
	memset(&tab->url, 0, sizeof(tab->url));

	m = strchr(url, ':');
	strlcpy(tab->url.scheme, "about", sizeof(tab->url.scheme));
	strlcpy(tab->url.path, m+1, sizeof(tab->url.path));

	strlcpy(tab->urlstr, url, sizeof(tab->urlstr));

	if (!strcmp(url, "about:new"))
		load_page_from_str(tab, about_new);
	else
		load_page_from_str(tab, "# not found\n");
}

void
load_gemini_url(struct tab *tab, const char *url)
{
	const char	*err;
	char		*p;

	if (has_prefix(url, "gemini:")) {
		if (!url_parse(url, &tab->url, &err))
			goto err;
	} else {
		if (!url_resolve_from(&tab->url, url, &err))
			goto err;
	}

	url_unparse(&tab->url, tab->urlstr, sizeof(tab->urlstr));
	imsg_compose(ibuf, IMSG_GET, tab->id, 0, -1,
	    tab->urlstr, strlen(tab->urlstr)+1);
	imsg_flush(ibuf);
	return;

err:
	if (asprintf(&p, "#error loading %s\n>%s\n",
	    url, err) == -1)
		die();
	strlcpy(tab->urlstr, url, sizeof(tab->urlstr));
	load_page_from_str(tab, p);
	free(p);
}

void
load_url(struct tab *tab, const char *url)
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

int
main(void)
{
	struct imsgbuf	main_ibuf, network_ibuf;
	int		imsg_fds[2];

	signal(SIGCHLD, SIG_IGN);

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, imsg_fds) == -1)
		err(1, "socketpair");

	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		/* child */
		setproctitle("client");
		close(imsg_fds[0]);
		imsg_init(&network_ibuf, imsg_fds[1]);
		exit(client_main(&network_ibuf));
	}

	close(imsg_fds[1]);
	imsg_init(&main_ibuf, imsg_fds[0]);
	ibuf = &main_ibuf;

	TAILQ_INIT(&tabshead);

	event_init();

	event_set(&imsgev, ibuf->fd, EV_READ | EV_PERSIST, dispatch_imsg, ibuf);
	event_add(&imsgev, NULL);

	ui_init();

	event_dispatch();

	imsg_compose(ibuf, IMSG_QUIT, 0, 0, -1, NULL, 0);
	imsg_flush(ibuf);

	ui_end();

	return 0;
}
