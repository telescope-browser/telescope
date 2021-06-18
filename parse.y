/*
 * Much of the design is taken from doas (parse.y rev 1.29)
 *
 * Copyright (c) 2021 Omar Polo <op@omarpolo.com>
 * Copyright (c) 2015 Ted Unangst <tedu@openbsd.org>
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
 *
 */

%{

#include "telescope.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	union {
		char	*str;
		int	 num;
	};
	int lineno;
	int colno;
} yystype;
#define YYSTYPE yystype

static char *current_style;
static const char *path;

FILE *yyfp;

int parse_errors = 0;

static void yyerror(const char *, ...);
static int yylex(void);
static void setprfx(int, const char *);

%}

%token TSET
%token TSTYLE TPRFX TCONT TBG TFG TATTR TBOLD TUNDERLINE
%token TBIND TUNBIND

%token <str> TSTRING
%token <num> TNUMBER

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar rule '\n'
		| error '\n'
		;

rule		: set
		| style		{ free(current_style); current_style = NULL; }
		| bind
		| unbind
		;

set		: TSET TSTRING '=' TSTRING	{ printf("set %s = \"%s\"\n", $2, $4); }
		| TSET TSTRING '=' TNUMBER	{ printf("set %s = %d\n", $2, $4); }
		;

style		: TSTYLE TSTRING { current_style = $2; } styleopt
		| TSTYLE TSTRING { current_style = $2; } '{' styleopts '}'
		;

styleopts	: /* empty */
		| styleopts '\n'
		| styleopts styleopt '\n'
		;

styleopt	: TPRFX TSTRING			{ setprfx(0, $2); }
		| TCONT TSTRING			{ setprfx(1, $2); }
		| TBG TSTRING			{ printf("style background setted to \"%s\"\n", $2); }
		| TFG TSTRING			{ printf("style foreground setted to \"%s\"\n", $2); }
		| TATTR TBOLD			{ printf("style attr setted to bold\n"); }
		| TATTR TUNDERLINE		{ printf("style attr setted to underline\n"); }
		;

bind		: TBIND TSTRING TSTRING TSTRING	{ printf("TODO: bind %s %s %s\n", $2, $3, $4); }
		;

unbind		: TUNBIND TSTRING TSTRING	{ printf("TODO: unbind %s %s\n", $2, $3); }
		;

%%

void
yyerror(const char *fmt, ...)
{
	va_list va;

	fprintf(stderr, "%s:%d ", path, yylval.lineno+1);
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
	fprintf(stderr, "\n");
	parse_errors++;
}

static struct keyword {
	const char *word;
	int token;
} keywords[] = {
	{ "set", TSET },
	{ "style", TSTYLE },
	{ "prefix", TPRFX },
	{ "cont", TCONT },
	{ "background", TBG },
	{ "foreground", TFG },
	{ "attr", TATTR },
	{ "bold", TBOLD },
	{ "underline", TUNDERLINE },
	{ "bind", TBIND },
	{ "unbind", TUNBIND },
};

int
yylex(void)
{
	char buf[1024], *ebuf, *p, *str;
	const char *errstr;
	int c, quotes = 0, escape = 0, qpos = -1, nonkw = 0;
	size_t i;

	p = buf;
	ebuf = buf + sizeof(buf);

repeat:
	/* skip whitespace first */
	for (c = getc(yyfp); c == ' ' || c == '\t' || c == '\f'; c = getc(yyfp))
		yylval.colno++;

	/* check for special one-character constructions */
	switch (c) {
	case '\n':
		yylval.colno = 0;
		yylval.lineno++;
		/* fallthrough */
	case '{':
	case '}':
	case '=':
		return c;
	case '#':
		/* skip comments; NUL is allowed; no continuation */
		while ((c = getc(yyfp)) != '\n')
			if (c == EOF)
				goto eof;
		yylval.colno = 0;
		yylval.lineno++;
		return c;
	case EOF:
		goto eof;
	}

	/* parsing next word */
	for (;; c = getc(yyfp), yylval.colno++) {
		switch (c) {
		case '\0':
			yyerror("unallowed character NULL in column %d",
			    yylval.colno+1);
			escape = 0;
			continue;
		case '\\':
			escape = !escape;
			if (escape)
				continue;
			break;
		case '\n':
			if (quotes)
				yyerror("unterminated quotes in column %d",
				    yylval.colno+1);
			if (escape) {
				nonkw = 1;
				escape = 0;
				yylval.colno = 0;
				yylval.lineno++;
			}
			goto eow;
		case EOF:
			if (escape)
				yyerror("unterminated escape in column %d",
				    yylval.colno);
			if (quotes)
				yyerror("unterminated quotes in column %d",
				    qpos + 1);
			goto eow;
		case '{':
		case '}':
		case '=':
		case '#':
		case ' ':
		case '\t':
			if (!escape && !quotes)
				goto eow;
			break;
		case '"':
			if (!escape) {
				quotes = !quotes;
				if (quotes) {
					nonkw = 1;
					qpos = yylval.colno;
				}
				continue;
			}
		}
		*p++ = c;
		if (p == ebuf) {
			yyerror("line too long");
			p = buf;
		}
		escape = 0;
	}

eow:
	*p = 0;
	if (c != EOF)
		ungetc(c, yyfp);
	if (p == buf) {
		/*
		 * There could be a number of reason for empty buffer,
		 * and we handle all of them here, to avoid cluttering
		 * the main loop.
		 */
		if (c == EOF)
			goto eof;
		else if (qpos == -1) /* accept, e.g., empty args: cmd foo args "" */
			goto repeat;
	}
	if (!nonkw) {
		for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
			if (strcmp(buf, keywords[i].word) == 0)
				return keywords[i].token;
		}
	}
	c = *buf;
	if (!nonkw && (c == '-' || isdigit(c))) {
		yylval.num = strtonum(buf, INT_MIN, INT_MAX, &errstr);
		if (errstr != NULL)
			yyerror("number is %s: %s", errstr, buf);
		return TNUMBER;
	}
	if ((str = strdup(buf)) == NULL)
		err(1, "%s", __func__);
	yylval.str = str;
	return TSTRING;

eof:
	if (ferror(yyfp))
		yyerror("input error reading config");
	return 0;
}

static void
setprfx(int cont, const char *name)
{
	if (current_style == NULL) {
		warnx("current_style = NULL!");
		abort();
	}

	if (!config_setprfx(current_style, cont, name))
		yyerror("invalid style %s", name);
}

void
parseconfig(const char *filename, int fonf)
{
	if ((yyfp = fopen(filename, "r")) == NULL) {
		if (fonf)
			err(1, "%s", filename);
		return;
	}

	path = filename;
	yyparse();
	fclose(yyfp);
	if (parse_errors)
		exit(1);
}
