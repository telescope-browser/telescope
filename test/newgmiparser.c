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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hist.h"
#include "parser.h"
#include "telescope.h"
#include "utf8.h"

/* XXX: needed just to please the linker */
int hide_pre_context;
int hide_pre_closing_line;
int hide_pre_blocks;
int emojify_link;
int dont_apply_styling;

int
emojied_line(const char *s, const char **space_ret)
{
	return 0;
}

void
erase_buffer(struct buffer *buffer)
{
	return;
}

int
main(void)
{
	struct doc	*doc;
	struct node	*n;
	struct tab	 tab;
	ssize_t		 r;
	size_t		 i;
	char		 buf[BUFSIZ];

	memset(&tab, 0, sizeof(tab));
	if ((tab.hist = hist_new(HIST_LINEAR)) == NULL)
		err(1, "hist_new");
	if (hist_push(tab.hist, "dummy://address") == -1)
		err(1, "hist_push");

	TAILQ_INIT(&tab.buffer.head);
	TAILQ_INIT(&tab.buffer.vhead);

	parser_init(&tab.buffer, &newgemtext_parser);
	if (doc_open(&tab.buffer.doc, NODE_DOCUMENT) == -1)
		err(1, "doc_open_document");
	for (;;) {
		if ((r = read(0, buf, sizeof(buf))) == -1)
			err(1, "read");
		if (r == 0)
			break;
		if (!parser_parse(&tab.buffer, buf, r))
			err(1, "parser_parse");
	}

	if (!parser_free(&tab))
		err(1, "parser_free");

	doc = &tab.buffer.doc;
	for (i = 0; i < doc->nlen; i++) {
		n = &doc->nodes[i];

		printf("%2zu type=%s parent=%d last_descendant=%d", i,
		    doc_node_type(n->type), n->parent, n->last_descendant);
		printf(" text=(%zu, %zu)", n->text.off, n->text.len);
		printf(" href=(%zu, %zu)", n->href.off, n->href.len);
		printf(" level=%d\n", n->level);
	}

	return 0;
}
