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

struct buffer;
struct tab;

struct parser {
	const char	 *name;
	int		  initflags;

	int		(*parse)(struct buffer *, const char *, size_t);
	int		(*parseline)(struct buffer *, const char *, size_t);
	int		(*free)(struct buffer *);
	int		(*serialize)(struct buffer *, FILE *);
};

void	 parser_init(struct buffer *, const struct parser *);
int	 parser_parse(struct buffer *, const char *, size_t);
int	 parser_parsef(struct buffer *, const char *, ...);
int	 parser_free(struct tab *);
int	 parser_serialize(struct buffer *, FILE *);

int	 parser_append(struct buffer *, const char *, size_t);
int	 parser_set_buf(struct buffer *, const char *, size_t);

extern const struct parser	 gemtext_parser;
extern const struct parser	 gophermap_parser;
extern const struct parser	 textpatch_parser;
extern const struct parser	 textplain_parser;

#endif
