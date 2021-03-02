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

static struct imsgbuf	*ibuf;
static uint32_t		 tab_counter;

static void	handle_imsg_err(struct imsg*, size_t);
static void	handle_imsg_check_cert(struct imsg*, size_t);
static void	handle_imsg_got_code(struct imsg*, size_t);
static void	handle_imsg_got_meta(struct imsg*, size_t);
static void	handle_imsg_buf(struct imsg*, size_t);
static void	handle_imsg_eof(struct imsg*, size_t);

static void	load_page_from_str(struct tab*, const char*);
static void	load_url(struct tab*, const char*);

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
	/* write(2, imsg->data, datalen); */
	/* fprintf(stderr, "\nEOF\n"); */
	/* event_loopbreak(); */
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

	/* TODO: parse the MIME type if it's 20 */
	gemtext_initparser(&tab->page);

	if (tab->code == 20) {
		imsg_compose(ibuf, IMSG_PROCEED, tab->id, 0, -1, NULL, 0);
		imsg_flush(ibuf);
		return;
	}

	if (tab->code == 30)
		die();

	/* 4x, 5x or 6x */

	if (!tab->page.parse(&tab->page, err_pages[tab->code],
	    strlen(err_pages[tab->code])))
		die();
	if (!tab->page.free(&tab->page))
		die();

	ui_on_tab_refresh(tab);
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
	if (!tab->page.parse(&tab->page, about_new, strlen(about_new)))
		die();
	if (!tab->page.free(&tab->page))
		die();
	ui_on_tab_refresh(tab);
}

static void
load_url(struct tab *tab, const char *url)
{
	if (!strcmp(url, "about:new")) {
		load_page_from_str(tab, about_new);
		return;
	}

	imsg_compose(ibuf, IMSG_GET, tab->id, 0, -1, url, strlen(url)+1);
	imsg_flush(ibuf);
}

void
new_tab(void)
{
	struct tab	*tab;
	const char	*url = "about:new";
	/* const char	*url = "gemini://localhost.it/"; */

	if ((tab = calloc(1, sizeof(*tab))) == NULL)
		die();

	TAILQ_INSERT_HEAD(&tabshead, tab, tabs);

	tab->id = tab_counter++;
	TAILQ_INIT(&tab->page.head);

	ui_on_new_tab(tab);

	load_url(tab, url);
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

	new_tab();

	event_dispatch();

	imsg_compose(ibuf, IMSG_QUIT, 0, 0, -1, NULL, 0);
	imsg_flush(ibuf);

	ui_end();

	return 0;
}
