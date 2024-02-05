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

#include <stdlib.h>

#include "certs.h"
#include "compl.h"
#include "hist.h"
#include "telescope.h"
#include "session.h"

/*
 * Provide completions for load-url (lu).
 */
const char *
compl_lu(void **data, void **ret, const char **descr)
{
	struct history_item **state = (struct history_item **)data;

	/* first time: init the state */
	if (*state == NULL)
		*state = history.items;

	if (*state == history.items + history.len)
		return NULL;

	return (*state)++->uri;
}

/*
 * Provide completions for execute-extended-command (eecmd).
 */
const char *
compl_eecmd(void **data, void **ret, const char **descr)
{
	struct cmd	**state = (struct cmd **)data;

	/* first time: init the state */
	if (*state == NULL)
		*state = cmds;

	if ((*state)->cmd == NULL)
		return NULL;

	*descr = (*state)->descr;
	return (*state)++->cmd;
}

/*
 * Provide completions for tab-select.
 */
const char *
compl_ts(void **data, void **ret, const char **descr)
{
	struct tab	**tab = (struct tab **)data;

	/* first time: init the state */
	if (*tab == NULL)
		*tab = TAILQ_FIRST(&tabshead);
	else if ((*tab = TAILQ_NEXT(*tab, tabs)) == NULL)
		return NULL;

	*ret = *tab;

	if (*(*tab)->buffer.page.title == '\0')
		return hist_cur((*tab)->hist);
	*descr = hist_cur((*tab)->hist);
	return (*tab)->buffer.page.title;
}

/*
 * Provide completions for link-select.
 */
const char *
compl_ls(void **data, void **ret, const char **descr)
{
	struct line	**line = (struct line **)data;
	struct line	*l;
	const char	*link;

	l = *line;
	while (l != NULL && l->type != LINE_LINK)
		l = TAILQ_NEXT(l, lines);

	/* end of buffer */
	if (l == NULL)
		return NULL;

	if ((link = l->line) == NULL) {
		link = l->alt;
		*descr = NULL;
	} else
		*descr = l->alt;

	*ret = l;
	*line = TAILQ_NEXT(l, lines);
	return link;
}

/*
 * Provide completions for swiper.
 */
const char *
compl_swiper(void **data, void **ret, const char **descr)
{
	struct line	**line = (struct line **)data;
	const char	*text;

	while (*line != NULL && (*line)->line == NULL)
		*line = TAILQ_NEXT(*line, lines);

	if (*line == NULL)
		return NULL;

	text = (*line)->line;
	*ret = *line;
	*line = TAILQ_NEXT(*line, lines);
	return text;
}

/*
 * Provide completions for toc
 */
const char *
compl_toc(void **data, void **ret, const char **descr)
{
	struct line	**line = (struct line **)data;
	struct line	*l;
	const char	*text;

	l = *line;
	while (l != NULL && (l->line == NULL ||
	    (l->type != LINE_TITLE_1 &&
	    l->type != LINE_TITLE_2 &&
	    l->type != LINE_TITLE_3)))
		l = TAILQ_NEXT(l, lines);

	/* end of buffer */
	if (l == NULL)
		return NULL;

	text = l->line;
	*ret = l;
	*line = TAILQ_NEXT(l, lines);
	return text;
}

/*
 * Provide completions for use-certificate
 */
const char *
compl_uc(void **data, void **ret, const char **descr)
{
	const char	***state = (const char ***)data;

	/* first time: init the state */
	if (*state == NULL)
		*state = (const char **)identities;

	if (**state == NULL)
		return NULL;

	/* XXX filling descr too would be nice */
	return *((*state)++);
}
