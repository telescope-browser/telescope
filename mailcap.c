/*
 * Copyright (c) 2024 Thomas Adam <thomas@xteddy.org>
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

/*
 * Handles reading mailmap files.
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "mailcap.h"
#include "xwrapper.h"

#define DEFAULT_MAILCAP_ENTRY "*/*; "DEFAULT_OPENER" %s"

#define str_unappend(ch) if (sps.off > 0 && (ch) != EOF) { sps.off--; }

struct str_parse_state {
	char		*buf;
	size_t		 len, off;
	int		 esc;
};
static struct str_parse_state sps;

static void		 str_append(char **, size_t *, char);
static int		 str_getc(void);
static char		*str_tokenise(void);
static int		 str_to_argv(char *, int *, char ***);
static char		*str_trim_whitespace(char *);
static void		 argv_free(int, char **);

struct mailcaplist 	 mailcaps = TAILQ_HEAD_INITIALIZER(mailcaps);

static FILE		*find_mailcap_file(void);
static struct mailcap	*mailcap_new(void);
static void		 mailcap_expand_cmd(struct mailcap *, char *, char *);
static int		 parse_mailcap_line(char *);
static struct mailcap 	*mailcap_by_mimetype(const char *);

/* FIXME: $MAILCAPS can override this, but we don't check for that. */
static const char *default_mailcaps[] = {
	"~/.mailcap",
	"/etc/mailcap",
	"/usr/etc/mailcap",
	"/usr/local/etc/mailcap",
	NULL,
};

enum mailcap_section {
	MAILCAP_MIME = 0,
	MAILCAP_CMD,
	MAILCAP_FLAGS,
};

static char *
str_trim_whitespace(char *s)
{
	char 	*t;

	s += strspn(s, " \t\n");
	t = s + strlen(s) - 1;

	while (t >= s && isspace((unsigned char)*t))
		*t-- = '\0';
	return s;
}

static void
str_append(char **buf, size_t *len, char add)
{
	size_t 	al = 1;

	if (al > SIZE_MAX - 1 || *len > SIZE_MAX - 1 - al)
		errx(1, "buffer is too big");

	*buf = xrealloc(*buf, (*len) + 1 + al);
	memcpy((*buf) + *len, &add, al);
	(*len) += al;
}

static int
str_getc(void)
{
	int	ch;

	if (sps.esc != 0) {
		sps.esc--;
		return ('\\');
	}
	for (;;) {
		ch = EOF;
		if (sps.off < sps.len)
			ch = sps.buf[sps.off++];

		if (ch == '\\') {
			sps.esc++;
			continue;
		}
		if (ch == '\n' && (sps.esc % 2) == 1) {
			sps.esc--;
			continue;
		}

		if (sps.esc != 0) {
			str_unappend(ch);
			sps.esc--;
			return ('\\');
		}
		return (ch);
	}
}

static char *
str_tokenise(void)
{
	int			 ch;
	char			*buf;
	size_t			 len;
	enum {
		APPEND,
		DOUBLE_QUOTES,
		SINGLE_QUOTES,
	}			 state = APPEND;

	len = 0;
	buf = calloc(1, sizeof *buf);

	for (;;) {
		ch = str_getc();
		/* EOF or \n are always the end of the token. */
		if (ch == EOF || (state == APPEND && ch == '\n'))
			break;

		/* Whitespace ends a token unless inside quotes.  But if we've
		 * also been given:
		 *
		 * foo "" bar
		 *
		 * don't lose that.
		 */
		if (((ch == ' ' || ch == '\t') && state == APPEND) &&
		    buf[0] != '\0') {
			break;
		}

		if (ch == '\\' && state != SINGLE_QUOTES) {
			ch = str_getc();
			str_append(&buf, &len, ch);
			continue;
		}
		switch (ch) {
		case '\'':
			if (state == APPEND) {
				state = SINGLE_QUOTES;
				continue;
			}
			if (state == SINGLE_QUOTES) {
				state = APPEND;
				continue;
			}
			break;
		case '"':
			if (state == APPEND) {
				state = DOUBLE_QUOTES;
				continue;
			}
			if (state == DOUBLE_QUOTES) {
				state = APPEND;
				continue;
			}
			break;
		default:
			/* Otherwise add the character to the buffer. */
			str_append(&buf, &len, ch);
			break;

		}
	}
	str_unappend(ch);
	buf[len] = '\0';

	if (*buf == '\0' || state == SINGLE_QUOTES || state == DOUBLE_QUOTES) {
		fprintf(stderr, "Unterminated string: <%s>, missing: %c\n",
			buf, state == SINGLE_QUOTES ? '\'' :
			state == DOUBLE_QUOTES ? '"' : ' ');
		free(buf);
		return (NULL);
	}

	return (buf);
}

static int
str_to_argv(char *str, int *ret_argc, char ***ret_argv)
{
	char			*token;
	int			 ch, next;
	char			**argv = NULL;
	int			 argc = 0;

	if (str == NULL)
		return -1;

	free(sps.buf);
	memset(&sps, 0, sizeof(sps));
	sps.buf = xstrdup(str);
	sps.len = strlen(sps.buf);

	for (;;) {
		if (str[0] == '#') {
			/* Skip comment. */
			next = str_getc();
			while (((next = str_getc()) != EOF))
				; /* Nothing. */
		}

		ch = str_getc();

		if (ch == '\n' || ch == EOF)
			goto out;
		if (ch == ' ' || ch == '\t')
			continue;

		/* Tokenise the string according to quoting rules.  Note that
		 * the string is stepped-back by one character to make the
		 * tokenisation easier, and not to kick-off the state of the
		 * parsing from this point.
		 */
		str_unappend(ch);
		if ((token = str_tokenise()) == NULL) {
			argv_free(argc, argv);
			return -1;
		}

		/* Add to argv. */
		argv = xreallocarray(argv, argc + 1, sizeof *argv);
		argv[argc++] = xstrdup(token);
	}
out:
	*ret_argv = argv;
	*ret_argc = argc;

	return 0;
}

void
argv_free(int argc, char **argv)
{
	int	i;

	if (argc == 0)
		return;

	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
}

static FILE *
find_mailcap_file(void)
{
	FILE	 *fp;
	char	**entry = (char **)default_mailcaps;
	char	 *home = NULL;
	char	 *expanded = NULL;

	for (; *entry != NULL; entry++) {
		if (strncmp(*entry, "~/", 2) == 0) {
			*entry += 2;
			if ((home = getenv("HOME")) == NULL)
				errx(1, "HOME not set");
			asprintf(&expanded, "%s/%s", home, *entry);
		} else
			asprintf(&expanded, "%s", *entry);

		fp = fopen(expanded, "r");
		free(expanded);
		if (fp != NULL)
			return (fp);
	}
	return (NULL);
}

static struct mailcap *
mailcap_new(void)
{
	struct mailcap	*mc = NULL;

	mc = xcalloc(1, sizeof *mc);

	return (mc);
}

static int
parse_mailcap_line(char *input)
{
	struct mailcap		*mc;
	int	 		 ms = 0;
	char			*line = NULL;

	mc = mailcap_new();

	while ((line = strsep(&input, ";")) != NULL) {
		line = str_trim_whitespace(line);

		switch (ms) {
		case MAILCAP_MIME:
			mc->mime_type = xstrdup(line);
			ms++;
			break;
		case MAILCAP_CMD:
			mc->cmd = xstrdup(line);
			ms++;
			break;
		case MAILCAP_FLAGS:
			if (strcmp(line, "needsterminal") == 0)
				mc->flags |= MAILCAP_NEEDSTERMINAL;
			if (strcmp(line, "copiousoutput") == 0)
				mc->flags |= MAILCAP_COPIOUSOUTPUT;
			break;
		}
	}

	if (line != NULL && *line != '\0') {
		fprintf(stderr, "mailcap: trailing: %s: skipping...\n", line);
		free(mc);
		return (-1);
	}
	TAILQ_INSERT_TAIL(&mailcaps, mc, mailcaps);

	return (0);
}

void
mailcap_expand_cmd(struct mailcap *mc, char *mt, char *file)
{
	char		**argv;
	int		 argc = 0, ret;

	if (mc->cmd == NULL)
		return;

	ret = str_to_argv(mc->cmd, &argc, &argv);

	if (ret != 0 || argv == NULL)
		return;

	for (int z = 0; z < argc; z++) {
		if (strcmp(argv[z], "%s") == 0) {
			free(argv[z]);
			argv[z] = xstrdup(file);
		}

		if (strcmp(argv[z], "%t") == 0) {
			free(argv[z]);
			argv[z] = xstrdup(mt);
		}
	}
	argv[argc++] = NULL;

	argv_free(mc->cmd_argc, mc->cmd_argv);

	mc->cmd_argv = argv;
	mc->cmd_argc = argc;
}

static struct mailcap *
mailcap_by_mimetype(const char *mt)
{
	struct mailcap	*mc;

	TAILQ_FOREACH(mc, &mailcaps, mailcaps) {
		if (fnmatch(mc->mime_type, mt, 0) == 0)
			return (mc);
	}
	return (NULL);
}

void
init_mailcap(void)
{
	FILE		*f;
	char		*copy;

	if ((f = find_mailcap_file()) != NULL) {
		mailcap_parse(f);
		fclose(f);
	}

	copy = xstrdup(DEFAULT_MAILCAP_ENTRY);

	/* Our own entry won't error. */
	(void)parse_mailcap_line(copy);
	free(copy);
}

void
mailcap_parse(FILE *f)
{
	const char	 delims[3] = {'\\', '\\', '\0'};
	char		*buf, *copy;
	size_t		 line = 0;

	while ((buf = fparseln(f, NULL, &line, delims, 0)) != NULL) {
		memset(&sps, 0, sizeof sps);
		copy = buf;

		copy = str_trim_whitespace(copy);

		if (*copy == '#' || *copy == '\0') {
			free(buf);
			continue;
		}

		if (parse_mailcap_line(copy) == -1) {
			fprintf(stderr, "Error with entry: <<%s>>, line: %ld\n",
			    copy, line);
		}
		free(buf);
	}
}

struct mailcap *
mailcap_cmd_from_mimetype(char *mime_type, char *filename)
{
	struct mailcap	*mc = NULL;

	if (mime_type == NULL || filename == NULL)
		return (NULL);

	if ((mc = mailcap_by_mimetype(mime_type)) != NULL)
		mailcap_expand_cmd(mc, mime_type, filename);

	return (mc);
}
