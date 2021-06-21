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

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
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
static int current_bg, current_fg;

static const char *path;

FILE *yyfp;

int parse_errors = 0;

static void yyerror(const char *, ...);
static int yylex(void);
static void setprfx(int, const char *);
static void setvari(char *, int);
static void setvars(char *, char *);
static int colorname(const char *);
static void setcolor(int, int, const char *);

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
		| style {
			free(current_style);
			current_style = NULL;
		}
		| bind
		| unbind
		;

set		: TSET TSTRING '=' TSTRING	{ setvars($2, $4); }
		| TSET TSTRING '=' TNUMBER	{ setvari($2, $4); }
		;

style		: TSTYLE TSTRING { current_style = $2; } stylespec ;
stylespec	: styleopt | '{' styleopts '}' ;

styleopts	: /* empty */
		| styleopts '\n'
		| styleopts styleopt '\n'
		;

styleopt	: TPRFX TSTRING	{ setprfx(0, $2); }
		| TCONT TSTRING	{ setprfx(1, $2); }
		| TBG TSTRING {
			setcolor(0, 1, $2);
			setcolor(1, 1, $2);
			free($2);
		}
		| TFG TSTRING {
			setcolor(0, 0, $2);
			setcolor(1, 0, $2);
			free($2);
		}
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
	{ "bg", TBG },
	{ "fg", TFG },
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
	assert(current_style != NULL);

	if (!config_setprfx(current_style, cont, name))
		yyerror("invalid style %s", name);
}

static void
setvari(char *var, int val)
{
	if (!config_setvari(var, val))
		yyerror("invalid variable or value: %s = %d",
		    var, val);

	free(var);
}

static void
setvars(char *var, char *val)
{
        if (!config_setvars(var, val))
		yyerror("invalid variable or value: %s = \"%s\"",
		    var, val);

	free(var);
}

static int
colorname(const char *name)
{
	struct {
		const char	*name;
		short		 val;
	} *i, colors[] = {
		{ "default",	0 },
		{ "black",	COLOR_BLACK },
		{ "red",	COLOR_RED },
		{ "green",	COLOR_GREEN },
		{ "yellow",	COLOR_YELLOW },
		{ "blue",	COLOR_BLUE },
		{ "magenta",	COLOR_MAGENTA },
		{ "cyan",	COLOR_CYAN },
		{ "white",	COLOR_WHITE },
		{ NULL, 0 },
	};

	for (i = colors; i->name != NULL; ++i) {
		if (!strcmp(i->name, name))
			return i->val;
	}

	yyerror("unknown color name \"%s\"", name);
	return -1;
}

void
setcolor(int prfx, int bg, const char *color)
{
	int c;

	assert(current_style != NULL);

	if ((c = colorname(color)) == -1)
		return;

	if (!config_setcolor(current_style, prfx, bg, c))
		yyerror("can't set color \"%s\" for %s", color,
		    current_style);
}

void
parseconfig(const char *filename, int fonf)
{
	/*
	char c;
	printf("proceed?\n");
	read(0, &c, 1);
	*/

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
