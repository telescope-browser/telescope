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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"

#define FAIL(fmt, ...) \
	errx(1, "%s:%d %s:" fmt, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define MUST(op)				    \
	do {					    \
		if ((op) == -1)			    \
			err(1, #op "failed");	    \
	} while (0)

static void
test_push(void)
{
	struct doc	d;
	size_t		curcap;
	int		off;

	memset(&d, 0, sizeof(d));
	MUST(off = doc_push(&d, "hello", 5));
	if (off > (int)d.alen)
		FAIL("offset greater than doc len: got %d, len is %zu",
		    off, d.alen);

	curcap = d.acap;
	while (d.acap == curcap) {
		MUST(doc_push(&d, "world", 5));
	}

	if (memcmp(d.arena + off, "hello", 5))
		FAIL("arena @ %d,5 expected to be \"hello\"", off);

	doc_free(&d);
}

static void
test_builddoc(void)
{
	struct doc	d;
	int		s, doc_id, head_id, ht_id, p_id, text1_id, l_id, lt_id, dot_id;

	memset(&d, 0, sizeof(d));

	MUST(s = doc_push(&d, "Title Some text with a example.com link in the middle.", 54));
	/*                    ^0     ^6               ^23        ^34               ^53 */
	assert(s == 0);
	assert(d.alen == 54);

	MUST(doc_id  = doc_open(&d, NODE_DOCUMENT));
	assert(d.nodes[doc_id].parent == -1);

	MUST(head_id = doc_open(&d, NODE_HEADING));
	assert(d.nodes[head_id].parent == doc_id);
	assert(doc_id < head_id);
	d.nodes[head_id].level = 1;

	MUST(ht_id = doc_append(&d, NODE_TEXT));
	assert(d.nodes[ht_id].parent == head_id);
	assert(d.nodes[ht_id].last_descendant == ht_id);
	assert(head_id < ht_id);
	d.nodes[ht_id].text = (struct docsplice){0, 5};

	assert(d.depth);
	assert(d.stack[d.depth-1] == head_id);
	MUST(doc_close(&d));
	assert(d.nodes[head_id].last_descendant == ht_id);

	MUST(p_id = doc_open(&d, NODE_PARAGRAPH));
	assert(d.nodes[p_id].parent == doc_id);
	assert(ht_id < p_id);

	MUST(text1_id = doc_append(&d, NODE_TEXT));
	assert(d.nodes[text1_id].parent == p_id);
	assert(d.nodes[text1_id].last_descendant == text1_id);
	assert(p_id < text1_id);
	d.nodes[text1_id].text = (struct docsplice){6, 17};

	MUST(l_id = doc_open(&d, NODE_LINK));
	assert(d.nodes[l_id].parent == p_id);
	assert(text1_id < l_id);
	d.nodes[l_id].href = (struct docsplice){23, 11};

	MUST(lt_id = doc_append(&d, NODE_TEXT));
	assert(d.nodes[lt_id].parent == l_id);
	assert(d.nodes[lt_id].last_descendant == lt_id);
	assert(l_id < lt_id);
	d.nodes[lt_id].text = (struct docsplice){35, 18};

	assert(d.depth);
	assert(d.stack[d.depth-1] == l_id);
	MUST(doc_close(&d));
	assert(d.nodes[l_id].last_descendant == lt_id);

	MUST(dot_id = doc_append(&d, NODE_TEXT));
	assert(d.nodes[dot_id].parent == p_id);
	assert(d.nodes[dot_id].last_descendant == dot_id);
	assert(lt_id < dot_id);
	d.nodes[dot_id].text = (struct docsplice){53, 1};

	assert(d.depth);
	assert(d.stack[d.depth-1] == p_id);
	MUST(doc_close(&d));
	assert(d.nodes[p_id].last_descendant == dot_id);

	assert(d.depth);
	assert(d.stack[d.depth-1] == doc_id);
	MUST(doc_close(&d));
	assert(d.nodes[doc_id].last_descendant == dot_id);

	assert(d.depth == 0);
	assert(d.overflow == 0);

	assert(!memcmp(&d.arena[d.nodes[dot_id].text.off], ".", 1));

	doc_free(&d);
}

static void
test_overflow(void)
{
	struct doc	d;
	int		i;

	memset(&d, 0, sizeof(d));
	while (d.overflow == 0)
		MUST(doc_open(&d, NODE_TEXT));

	assert(d.nlen == DOC_MAXDEPTH + 1);
	assert(d.depth == DOC_MAXDEPTH);

	for (i = 0; i < 10; ++i) {
		assert(d.overflow == i + 1);
		MUST(doc_open(&d, NODE_TEXT));
		assert(d.depth == DOC_MAXDEPTH);
	}

	for (i = 0; i < 5; ++i) {
		doc_close(&d);
		assert(d.overflow == 10 - i);
		assert(d.depth == DOC_MAXDEPTH);
	}

	assert(d.overflow == 6);

	doc_close_all(&d);
	assert(d.overflow == 0);
	assert(d.depth == 0);

	doc_free(&d);
}

int
main(void)
{
	test_push();
	test_builddoc();
	test_overflow();
}
