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

#ifndef PARSER_H
#define PARSER_H

#include "telescope.h"

typedef void (*parserfn)(struct parser *);

void	 parser_init(struct tab *, parserfn);
int	 parser_parse(struct tab *, const char *, size_t);
int	 parser_free(struct tab *);
int	 parser_serialize(struct tab *, FILE *);

int	 parser_append(struct parser*, const char*, size_t);
int	 parser_set_buf(struct parser*, const char*, size_t);
int	 parser_foreach_line(struct parser*, const char*, size_t, parsechunkfn);

/* parser_gemtext.c */
void	 gemtext_initparser(struct parser*);

/* parser_gophermap.c */
void	 gophermap_initparser(struct parser *);

/* parser_textpatch.c */
void	 textpatch_initparser(struct parser *);

/* parser_textplain.c */
void	 textplain_initparser(struct parser*);

#endif
