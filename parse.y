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

#include "keymap.h"
#include "telescope.h"

#include <phos/phos.h>

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
static int color_type;

static const char *path;

FILE *yyfp;

int parse_errors = 0;

static void yyerror(const char *, ...);
static int yylex(void);
static void setprfx(const char *, const char *);
static void setvari(char *, int);
static void setvars(char *, char *);
static int colorname(const char *);
static void setcolor(const char *, const char *, const char *);
static int attrname(const char *);
static void setattr(char *, char *, char *);
static void add_proxy(char *, char *);
static void bindkey(const char *, const char *, const char *);
static void do_parseconfig(const char *, int);

%}

%token	ATTR
%token	BIND BG
%token	CONT
%token	FG
%token	PRFX PROXY
%token	SET STYLE
%token	UNBIND
%token	VIA

%token <str> STRING
%token <num> NUMBER

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
		| proxy
		;

set		: SET STRING '=' STRING	{ setvars($2, $4); }
		| SET STRING '=' NUMBER	{ setvari($2, $4); }
		;

style		: STYLE STRING { current_style = $2; } stylespec ;
stylespec	: styleopt | '{' optnl styleopts '}' ;

styleopts	: /* empty */
		| styleopts styleopt optnl
		;

styleopt	: PRFX STRING		{ setprfx($2, $2); }
		| PRFX STRING STRING	{ setprfx($2, $3); }
		| BG { color_type = BG; } colorspec
		| FG { color_type = FG; } colorspec
		| ATTR attr
		;

colorspec	: STRING			{ setcolor($1, $1, $1); free($1); }
		| STRING STRING		{ setcolor($1, $2, $1); free($1); free($2); }
		| STRING STRING STRING	{ setcolor($1, $2, $3); free($1); free($2); free($3); }
		;

attr		: STRING			{ setattr($1, $1, $1); free($1); }
		| STRING STRING		{ setattr($1, $2, $1); free($1); free($2); }
		| STRING STRING STRING	{ setattr($1, $2, $3); free($1); free($2); free($3); }
		;

bind		: BIND STRING STRING STRING	{ bindkey($2, $3, $4); free($2); free($3); free($4); }
		;

unbind		: UNBIND STRING STRING	{ yyerror("TODO: unbind %s %s", $2, $3); }
		;

proxy		: PROXY STRING VIA STRING { add_proxy($2, $4); free($4); }
		;

optnl		: '\n' optnl	/* zero or more newlines */
		| /* empty */
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
	{ "attr", ATTR },
	{ "bg", BG },
	{ "bind", BIND },
	{ "cont", CONT },
	{ "fg", FG },
	{ "prefix", PRFX },
	{ "proxy", PROXY },
	{ "set", SET },
	{ "style", STYLE },
	{ "unbind", UNBIND },
	{ "via", VIA },
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
	case '\r':
		/* silently eat up any \r */
		goto repeat;
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
		case '\r':
			/* ignore \r here too */
			continue;
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
		return NUMBER;
	}
	if ((str = strdup(buf)) == NULL)
		err(1, "%s", __func__);
	yylval.str = str;
	return STRING;

eof:
	if (ferror(yyfp))
		yyerror("input error reading config");
	return 0;
}

static void
setprfx(const char *prfx, const char *cont)
{
	assert(current_style != NULL);

	if (!config_setprfx(current_style, prfx, cont))
		yyerror("invalid style %s", current_style);
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
		{ "default",	-1 },
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
	const char *errstr;
	int n;

	if (has_prefix(name, "colo")) {
		/* people are strange */
		if (has_prefix(name, "color"))
			name += 5;
		else if (has_prefix(name, "colour"))
			name += 6;
		else
			goto err;

		n = strtonum(name, 0, 256, &errstr);
		if (errstr != NULL)
			yyerror("color number is %s: %s", errstr, name);
		return n;
	}

	for (i = colors; i->name != NULL; ++i) {
		if (!strcmp(i->name, name))
			return i->val;
	}

err:
	yyerror("unknown color name \"%s\"", name);
	return -1;
}

void
setcolor(const char *prfx, const char *line, const char *trail)
{
	int p, l, t;

	assert(current_style != NULL);

	p = colorname(prfx);
	l = colorname(line);
	t = colorname(trail);

	if (!config_setcolor(color_type == BG, current_style, p, l, t))
		yyerror("invalid style %s", current_style);
}

static int
attrname(const char *n)
{
	struct {
		const char	*name;
		unsigned int	 val;
	} *i, attrs[] = {
		{ "normal",	A_NORMAL },
		{ "standout",	A_STANDOUT },
		{ "underline",	A_UNDERLINE },
		{ "reverse",	A_REVERSE },
		{ "blink",	A_BLINK },
		{ "dim",	A_DIM },
		{ "bold",	A_BOLD },
		{ NULL, 0 },
	};
	int ret, found;
	char *ap, *dup, *orig;

	if ((dup = strdup(n)) == NULL)
		err(1, "strdup");

	orig = dup;

	ret = 0;
	while ((ap = strsep(&dup, ",")) != NULL) {
		if (*ap == '\0')
			continue;

		found = 0;
		for (i = attrs; i ->name != NULL; ++i) {
			if (strcmp(i->name, ap))
				continue;
			ret |= i->val;
			found = 1;
			break;
		}

		if (!found)
			yyerror("unknown attribute \"%s\" at col %d",
			    ap, yylval.colno+1);
	}

	free(orig);
	return ret;
}

static void
setattr(char *prfx, char *line, char *trail)
{
	int p, l, t;

	assert(current_style != NULL);

	p = attrname(prfx);
	l = attrname(line);
	t = attrname(trail);

	if (!config_setattr(current_style, p, l, t))
		yyerror("invalid style %s", current_style);
}

static void
add_proxy(char *proto, char *proxy)
{
	struct proxy *p;
	struct phos_uri uri;

	if (!phos_parse_absolute_uri(proxy, &uri)) {
		yyerror("can't parse URL: %s", proxy);
		return;
	}

	if (*uri.path != '\0' || *uri.query != '\0' || *uri.fragment != '\0') {
		yyerror("proxy url can't have path, query or fragments");
		return;
	}

	if (strcmp(uri.scheme, "gemini")) {
		yyerror("disallowed proxy protocol %s", uri.scheme);
		return;
	}

	if ((p = calloc(1, sizeof(*p))) == NULL)
		err(1, "calloc");

	p->match_proto = proto;
	p->proto = PROTO_GEMINI;

	if ((p->host = strdup(uri.host)) == NULL)
		err(1, "strdup");

	if ((p->port = strdup(uri.port)) == NULL)
		err(1, "strdup");

	TAILQ_INSERT_HEAD(&proxies, p, proxies);
}

static interactivefn *
cmdname(const char *name)
{
	struct cmd *cmd;

	for (cmd = cmds; cmd->cmd != NULL; ++cmd) {
		if (!strcmp(cmd->cmd, name))
			return cmd->fn;
	}

	return NULL;
}

static void
bindkey(const char *map, const char *key, const char *cmd)
{
	struct kmap *kmap;
	interactivefn *fn;

	if (!strcmp(map, "global-map"))
		kmap = &global_map;
	else if (!strcmp(map, "minibuffer-map"))
		kmap = &minibuffer_map;
	else {
		yyerror("unknown map: %s", map);
		return;
	}

	if ((fn = cmdname(cmd)) == NULL) {
		yyerror("unknown cmd: %s", fn);
		return;
	}

	if (!kmap_define_key(kmap, key, fn))
		yyerror("failed to bind %s %s %s", map, key, cmd);
}

static void
do_parseconfig(const char *filename, int fonf)
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

void
parseconfig(const char *filename, int fonf)
{
	char altconf[PATH_MAX], *term;

	/* load the given config file */
	do_parseconfig(filename, fonf);

	/* then try to load file-TERM */

	if ((term = getenv("TERM")) == NULL)
		return;

	strlcpy(altconf, filename, sizeof(altconf));
	strlcat(altconf, "-", sizeof(altconf));
	strlcat(altconf, term, sizeof(altconf));

	do_parseconfig(altconf, 0);
}
