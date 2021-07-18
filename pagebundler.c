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

/*
 * pagebundler converts the given file into a valid C program that can
 * be compiled.  The generated code provides a variable that holds the
 * original file.
 *
 * Usage: pagebundler -f file -v varname > outfile
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

const char *file;
const char *varname;

int
main(int argc, char **argv)
{
	size_t len, r, i;
	int ch;
	FILE *f;
	uint8_t buf[64];

	while ((ch = getopt(argc, argv, "f:v:")) != -1) {
		switch (ch) {
		case 'f':
			file = optarg;
			break;
		case 'v':
			varname = optarg;
			break;
		default:
			fprintf(stderr, "%s: wrong usage\n",
			    argv[0]);
			return 1;
		}
	}

	if (file == NULL || varname == NULL) {
		fprintf(stderr, "%s: wrong usage\n", argv[0]);
		return 1;
	}

	if ((f = fopen(file, "r")) == NULL) {
		fprintf(stderr, "%s: can't open %s: %s",
		    argv[0], file, strerror(errno));
	}

        printf("const uint8_t %s[] = {\n", varname);

	len = 0;
        for (;;) {
		r = fread(buf, 1, sizeof(buf), f);
		len += r;

		printf("\t");
		for (i = 0; i < r; ++i) {
			printf("0x%x, ", buf[i]);
		}
		printf("\n");

		if (r != sizeof(buf))
			break;
	}

	len++;
	printf("\t0x00\n");
	printf("}; /* %s */\n", varname);

	printf("size_t %s_len = %zu;\n", varname, len);

	fclose(f);
	return 0;
}
