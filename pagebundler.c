/*
 * Copyright (c) 2021, 2022 Omar Polo <op@omarpolo.com>
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
 * pagebundler converts the given file into a valid C program that can
 * be compiled.  The generated code provides a variable that holds the
 * content of the original file and a _len variable with the size.
 *
 * Usage: pagebundler file > outfile
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
setfname(const char *fname, char *buf, size_t siz)
{
	const char	*c, *d;
	size_t		 len;

	if ((c = strrchr(fname, '/')) != NULL)
		c++;
	else
		c = fname;

	if ((d = strrchr(fname, '.')) == NULL || c > d)
		d = strchr(fname, '\0');

	len = d - c;
	if (len >= siz) {
		fprintf(stderr, "file name too long: %s\n", fname);
		exit(1);
	}

	memcpy(buf, c, len);
	buf[len] = '\0';
}

static int
validc(int c)
{
	return isprint(c) && c != '\\' && c != '\'' && c != '\n';
}

int
main(int argc, char **argv)
{
	size_t	 r, i, n;
	FILE	*f;
	uint8_t	 buf[BUFSIZ];
	char	 varname[PATH_MAX];

	if (argc != 2) {
		fprintf(stderr, "usage: %s file\n", *argv);
		return 1;
	}

	setfname(argv[1], varname, sizeof(varname));

	if ((f = fopen(argv[1], "r")) == NULL) {
		fprintf(stderr, "%s: can't open %s: %s",
		    argv[0], argv[1], strerror(errno));
		return 1;
	}

	printf("const uint8_t %s[] = {", varname);

	n = 0;
	for (;;) {
		r = fread(buf, 1, sizeof(buf), f);

		for (i = 0; i < r; ++i, ++n) {
			if (n % 12 == 0)
				printf("\n\t");
			else
				printf(" ");

			if (validc(buf[i]))
				printf("'%c',", buf[i]);
			else if (buf[i] == '\n')
				printf("'\\n',");
			else
				printf("0x%x,", buf[i]);
		}
		printf("\n");

		if (r != sizeof(buf))
			break;
	}

	printf("\t0x0\n");
	printf("}; /* %s */\n", varname);

	fclose(f);
	return 0;
}
