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

#include <stdlib.h>
#include <string.h>

#include "minibuffer.h"

static void		 minibuffer_hist_save_entry(void);
static void		 minibuffer_self_insert(void);
static void		 yornp_self_insert(void);
static void		 yornp_abort(void);
static void		 read_self_insert(void);
static void		 read_abort(void);
static void		 read_select(void);

static void (*yornp_cb)(int, struct tab *);
static struct tab *yornp_data;

static void (*read_cb)(const char*, unsigned int);
static unsigned int read_data;

struct histhead eecmd_history,
	ir_history,
	lu_history,
	read_history;

struct ministate ministate;

static void
minibuffer_hist_save_entry(void)
{
	struct hist	*hist;

	if (ministate.history == NULL)
		return;

	if ((hist = calloc(1, sizeof(*hist))) == NULL)
		abort();

	strlcpy(hist->h, ministate.buf, sizeof(hist->h));

	if (TAILQ_EMPTY(&ministate.history->head))
		TAILQ_INSERT_HEAD(&ministate.history->head, hist, entries);
	else
		TAILQ_INSERT_TAIL(&ministate.history->head, hist, entries);
	ministate.history->len++;
}

/*
 * taint the minibuffer cache: if we're currently showing a history
 * element, copy that to the current buf and reset the "history
 * navigation" thing.
 */
void
minibuffer_taint_hist(void)
{
	if (ministate.hist_cur == NULL)
		return;

	strlcpy(ministate.buf, ministate.hist_cur->h, sizeof(ministate.buf));
	ministate.hist_cur = NULL;
	ministate.buffer.current_line->line = ministate.buf;
}

static void
minibuffer_self_insert(void)
{
	char	*c, tmp[5] = {0};
	size_t	len;

	minibuffer_taint_hist();

	if (thiskey.cp == 0)
		return;

	len = utf8_encode(thiskey.cp, tmp);
	c = utf8_nth(ministate.buffer.current_line->line, ministate.buffer.cpoff);
	if (c + len > ministate.buf + sizeof(ministate.buf) - 1)
		return;

	memmove(c + len, c, strlen(c)+1);
	memcpy(c, tmp, len);
	ministate.buffer.cpoff++;
}

void
eecmd_self_insert(void)
{
	if (thiskey.meta || unicode_isspace(thiskey.cp) ||
	    !unicode_isgraph(thiskey.cp)) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

void
eecmd_select(void)
{
	struct cmd *cmd;

	for (cmd = cmds; cmd->cmd != NULL; ++cmd) {
		if (!strcmp(cmd->cmd, ministate.buf)) {
			exit_minibuffer();
			minibuffer_hist_save_entry();
			cmd->fn(current_buffer());
			return;
		}
	}

	message("No match");
}

void
ir_self_insert(void)
{
	minibuffer_self_insert();
}

void
ir_select(void)
{
	char		 buf[1025] = {0};
	struct phos_uri	 uri;
	struct tab	*tab;

	tab = current_tab();

	exit_minibuffer();
	minibuffer_hist_save_entry();

	/* a bit ugly but... */
	memcpy(&uri, &tab->uri, sizeof(tab->uri));
	phos_uri_set_query(&uri, ministate.buf);
	phos_serialize_uri(&uri, buf, sizeof(buf));
	load_url_in_tab(tab, buf);
}

void
lu_self_insert(void)
{
	if (thiskey.meta || unicode_isspace(thiskey.key) ||
	    !unicode_isgraph(thiskey.key)) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

void
lu_select(void)
{
	exit_minibuffer();
	minibuffer_hist_save_entry();
	load_url_in_tab(current_tab(), ministate.buf);
}

void
bp_select(void)
{
	exit_minibuffer();
	if (*ministate.buf != '\0')
		add_to_bookmarks(ministate.buf);
	else
		message("Abort.");
}

static void
yornp_self_insert(void)
{
	if (thiskey.key != 'y' && thiskey.key != 'n') {
		message("Please answer y or n");
		return;
	}

	exit_minibuffer();
	yornp_cb(thiskey.key == 'y', yornp_data);
}

static void
yornp_abort(void)
{
	exit_minibuffer();
	yornp_cb(0, yornp_data);
}

static void
read_self_insert(void)
{
	if (thiskey.meta || !unicode_isgraph(thiskey.cp)) {
		global_key_unbound();
		return;
	}

	minibuffer_self_insert();
}

static void
read_abort(void)
{
	exit_minibuffer();
	read_cb(NULL, read_data);
}

static void
read_select(void)
{
        exit_minibuffer();
	minibuffer_hist_save_entry();
	read_cb(ministate.buf, read_data);
}

void
enter_minibuffer(void (*self_insert_fn)(void), void (*donefn)(void),
    void (*abortfn)(void), struct histhead *hist)
{
	in_minibuffer = 1;
	base_map = &minibuffer_map;
	current_map = &minibuffer_map;

	base_map->unhandled_input = self_insert_fn;

	ministate.donefn = donefn;
	ministate.abortfn = abortfn;
	memset(ministate.buf, 0, sizeof(ministate.buf));
	ministate.buffer.current_line = &ministate.vline;
	ministate.buffer.current_line->line = ministate.buf;
	ministate.buffer.cpoff = 0;
	strlcpy(ministate.buf, "", sizeof(ministate.prompt));

	ministate.history = hist;
	ministate.hist_cur = NULL;
	ministate.hist_off = 0;
}

void
exit_minibuffer(void)
{
	in_minibuffer = 0;
	base_map = &global_map;
	current_map = &global_map;
}

void
yornp(const char *prompt, void (*fn)(int, struct tab*),
    struct tab *data)
{
	size_t len;

	if (in_minibuffer) {
		fn(0, data);
		return;
	}

	yornp_cb = fn;
	yornp_data = data;
	enter_minibuffer(yornp_self_insert, yornp_self_insert,
	    yornp_abort, NULL);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, prompt, len);
	strlcat(ministate.prompt, " (y or n) ", len);
}

/*
 * Not yet "completing", but soon maybe...
 */
void
completing_read(const char *prompt, void (*fn)(const char *, unsigned int),
    unsigned int data)
{
	size_t len;

	if (in_minibuffer)
		return;

	read_cb = fn;
	read_data = data;
	enter_minibuffer(read_self_insert, read_select, read_abort,
	    &read_history);

	len = sizeof(ministate.prompt);
	strlcpy(ministate.prompt, prompt, len);
	strlcat(ministate.prompt, ": ", len);
}
