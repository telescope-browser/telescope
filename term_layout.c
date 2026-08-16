/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "compat.h"

#include <stdint.h>
#include <stdlib.h>

#include <grapheme.h>

#include "doc.h"
#include "term_layout.h"
#include "utf8.h"

#define MAXDEPTH DOC_MAXDEPTH

struct lstate {
	struct doc	*doc;
	struct rowlist	*page;
	int		 width;
	int		 xoff;
	int		 col;
};

static int
grow(struct rowlist *page)
{
	void	*t;
	size_t	 newcap;

	newcap = page->cap + 16;
	t = recallocarray(page->rows, page->cap, newcap, sizeof(*page->rows));
	if (t == NULL)
		return (-1);
	page->rows = t;
	page->cap = newcap;
	return (0);
}

static int
growruns(struct runs *line)
{
	void	*t;
	size_t	 newcap;

	newcap = line->cap + 16;
	t = recallocarray(line->runs, line->cap, newcap, sizeof(*line->runs));
	if (t == NULL)
		return (-1);
	line->runs = t;
	line->cap = newcap;
	return (0);
}

static int
pushrun(struct lstate *ls, int id, size_t off, size_t len, int action)
{
	struct rowlist	*page = ls->page;
	struct runs	*line;
	struct run	*run;

	/* implicitly create a row if there is none */
	if (page->cap == 0 && grow(page) == -1)
		return (-1);

	line = &page->rows[page->cur];

	if (line->len == line->cap && growruns(line) == -1)
		return (-1);

	run = &line->runs[line->len++];
	run->node = id;
	run->splice = (struct docsplice){ off, len };
	run->action = action;

	return (0);
}

static int
flushrow(struct lstate *ls)
{
	struct rowlist	*page = ls->page;

	page->cur++;
	if (page->cur == page->cap && grow(page) == -1)
		return (-1);
	return (0);
}

static int
emit_text(struct lstate *ls, int id, int action)
{
	struct docsplice	 sp = ls->doc->nodes[id].text;
	const char		*text = ls->doc->arena + sp.off;
	size_t			 off, start = 0, ret, t;

	for (off = 0; off < sp.len; off += ret) {
		ret = grapheme_next_line_break_utf8(&text[off], sp.len - off);
		t = utf8_swidth_between(&text[off], &text[off + ret], ls->col);

		if (ls->col + (int)t < ls->width) {
			ls->col += t;
			continue;
		}

		/* have to wrap */
		if (pushrun(ls, id, sp.off, off - start, action) == -1)
			return (-1);
		if (flushrow(ls) == -1)
			return (-1);
		start = off;
		ls->col = ls->xoff + t;
	}

	if (off != start)
		return pushrun(ls, id, sp.off, off - start, action);
	return (0);
}

#if 0
#define log(...) fprintf(stderr, __VA_ARGS__)
#else
#define log(...)
#endif

struct style {
	int	 display_block;
	int	 in_pre;

	struct line_face *face;
};

static inline void
face_of(struct doc *)

int
term_layout(struct doc *doc, struct rowlist *ret, int width, int xoff)
{
	struct lstate	 ls = { doc, ret, width, xoff, 0 };
	struct node	*n;
	int		 stack[MAXDEPTH];
	struct style	 state[MAXDEPTH + 1];
	int		 depth = 0;
	size_t		 id;

	actions[0] = -1;

	for (id = 0; id < doc->nlen; ++id) {
		n = &doc->nodes[id];

		log("entering node %s\n", doc_node_type(n->type));

		while (depth > 0 && stack[depth - 1] != n->parent) {
			log("leaving node %s\n",
			    doc_node_type(doc->nodes[stack[depth - 1]].type));
			switch (doc->nodes[stack[depth - 1]].type) {
			case NODE_DOCUMENT:
			case NODE_PARAGRAPH:
			case NODE_HEADING:
			case NODE_ITEM:
			case NODE_QUOTE:
			case NODE_PRE:
				if (flushrow(&ls) == -1)
					return (-1);
			}
			depth--;
		}

		if (n->type == NODE_TEXT) {
			if (emit_text(&ls, id, actions[depth]) == -1)
				return (-1);
			continue;
		}

		if (n->flags & N_HIDDEN) {
			id = n->last_descendant; /* jump out of this */
			continue;
		}

		stack[depth] = id;
		actions[depth + 1] = n->type == NODE_LINK ? id : -1;
		depth++;
	}

	return (0);
}
