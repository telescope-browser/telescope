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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "telescope.h"
#include "ui.h"

static void	emit_help_item(char *, interactivefn *);
static void	rec_compute_help(struct kmap *, char *, size_t);

static void
emit_help_item(char *prfx, interactivefn *fn)
{
	struct line	*l;
	struct cmd	*cmd;

	for (cmd = cmds; cmd->cmd != NULL; ++cmd) {
		if (fn == cmd->fn)
			break;
	}
	assert(cmd != NULL);

	if ((l = calloc(1, sizeof(*l))) == NULL)
		abort();

	l->type = LINE_HELP;
	l->line = strdup(prfx);
	l->alt = (char*)cmd->cmd;

	if (TAILQ_EMPTY(&helpwin.page.head))
		TAILQ_INSERT_HEAD(&helpwin.page.head, l, lines);
	else
		TAILQ_INSERT_TAIL(&helpwin.page.head, l, lines);
}

static void
rec_compute_help(struct kmap *keymap, char *prfx, size_t len)
{
	struct keymap	*k;
	char		 p[32];
	const char	*kn;

	TAILQ_FOREACH(k, &keymap->m, keymaps) {
		strlcpy(p, prfx, sizeof(p));
		if (*p != '\0')
			strlcat(p, " ", sizeof(p));
		if (k->meta)
			strlcat(p, "M-", sizeof(p));
		if ((kn = unkbd(k->key)) != NULL)
			strlcat(p, kn, sizeof(p));
		else
			strlcat(p, ui_keyname(k->key), sizeof(p));

		if (k->fn == NULL)
			rec_compute_help(&k->map, p, sizeof(p));
		else
			emit_help_item(p, k->fn);
	}
}

void
recompute_help(void)
{
	static struct kmap *last_active_map = NULL;
	char	p[32] = { 0 };

	if (last_active_map != current_map) {
		last_active_map = current_map;

		helpwin.page.name = "*Help*";
		erase_buffer(&helpwin);
		rec_compute_help(current_map, p, sizeof(p));
		wrap_page(&helpwin, help_cols);
	}
}
