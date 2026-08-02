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

/* should be plenty */
#define DOC_MAXDEPTH	128

struct docsplice {
	size_t	off;
	size_t	len;
};

struct pos {
	int	nodeid;
	size_t	off;
};

enum nodetype {
	/* block */
	NODE_DOCUMENT,
	NODE_PARAGRAPH,
	NODE_HEADING,
	NODE_ITEM,
	NODE_QUOTE,
	NODE_PRE,

	/* inline */
	NODE_LINK,
	NODE_TEXT,
};

struct node {
	int		 type;
	uint32_t	 flags;
	int		 parent;
	int		 last_descendant;
	struct docsplice text;
	struct docsplice href;
	int		 level;	/* heading */
};

struct doc {
	char		*arena;
	size_t		 alen;
	size_t		 acap;

	struct node	*nodes;
	size_t		 nlen;
	size_t		 ncap;

	int		 stack[DOC_MAXDEPTH];
	int		 depth;
	int		 overflow;
};

const char	*doc_node_type(enum nodetype);

int		 doc_push(struct doc *, const char *, size_t);

int		 doc_append(struct doc *, enum nodetype);
int		 doc_append_text(struct doc *, size_t, size_t);

int		 doc_open(struct doc *, enum nodetype);
int		 doc_open_heading(struct doc *, int);
int		 doc_open_pre(struct doc *, size_t, size_t);
int		 doc_open_link(struct doc *, size_t, size_t);

int		 doc_close(struct doc *);
void		 doc_close_all(struct doc *);
void		 doc_free(struct doc *);
