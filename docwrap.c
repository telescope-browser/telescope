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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* #include "doc.h" */
#include "parser.h"
#include "telescope.h"
#include "term_layout.h"

/* to please the linker */

int hide_pre_context;
int hide_pre_closing_line;
int hide_pre_blocks;
int emojify_link;
int dont_apply_styling;

void
erase_buffer(struct buffer *buffer)
{
	return;
}

static int __dead
usage(void)
{
	fprintf(stderr, "usage: %s [file]\n", getprogname());
	exit(1);
}

int
main(int argc, char **argv)
{
	struct buffer	 b;
	struct doc	*doc = &b.doc;
	struct rowlist	 page;
	struct runs	*runs;
	struct run	*run;
	FILE		*fp = stdin;
	const char	*errstr;
	char		*line = NULL;
	size_t		 linesize = 0;
	ssize_t		 linelen;
	size_t		 i, j;
	int		 ch, width = 80;

	while ((ch = getopt(argc, argv, "w:")) != -1) {
		switch (ch) {
		case 'w':
			width = strtonum(optarg, 1, 1000, &errstr);
			if (errstr != NULL)
				err(1, "width is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	if (argc == 1 && (fp = fopen(argv[0], "r")) == NULL)
		err(1, "can't open %s", argv[0]);

	memset(&b, 0, sizeof(b));
	while ((linelen = getline(&line, &linesize, fp)) != -1) {
		if (newgemtext_parser.newparseline(&b, doc, line, linelen - 1) == -1)
			err(1, "newparseline failed");
	}
	if (newgemtext_parser.newfree(&b, doc) == -1)
		err(1, "newfree failed");

	memset(&page, 0, sizeof(page));
	if (term_layout(doc, &page, width, 2) == -1)
		err(1, "term_layout failed");

	for (i = 0; i <= page.cur; ++i) {
		runs = &page.rows[i];

		for (j = 0; j < runs->len; ++j) {
			run = &runs->runs[j];
			fwrite(doc->arena + run->splice.off, 1, run->splice.len, stdout);
		}

		putchar('\n');
	}

	return (0);
}
