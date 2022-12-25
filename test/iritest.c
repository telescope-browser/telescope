/*
 * Copyright (c) 2022 Omar Polo <op@omarpolo.com>
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

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "iri.h"

static int
check(const char *base, const char *ref, const char *expected)
{
	static struct iri	i;
	char			buf[512];

	if (iri_parse(base, ref, &i) == -1) {
		fprintf(stderr, "FAIL (\"%s\", \"%s\") %s\n", base, ref,
		    strerror(errno));
		return (1);
	}

	if (iri_unparse(&i, buf, sizeof(buf)) == -1) {
		fprintf(stderr, "FAIL (\"%s\", \"%s\") %s\n", base, ref,
		    strerror(errno));
		return (1);
	}

	if (strcmp(buf, expected) != 0) {
		fprintf(stderr, "FAIL (\"%s\", \"%s\")\n", base, ref);
		fprintf(stderr, "got:\t%s\n", buf);
		fprintf(stderr, "want:\t%s\n", expected);
		return (1);
	}

	fprintf(stderr, "OK (\"%s\", \"%s\") -> %s\n", base, ref, expected);
	return (0);
}

int
main(void)
{
	const char	*base = "http://a/b/c/d;p?q";
	int		 ret = 0;

	ret |= check(base, "g:h", "g:h");
	ret |= check(base, "g", "http://a/b/c/g");
	ret |= check(base, "./g", "http://a/b/c/g");
	ret |= check(base, "g/", "http://a/b/c/g/");
	ret |= check(base, "/g", "http://a/g");

	/*
	 * the RFC says "http://g" but we always normalize an
	 * empty path to "/" if there is an authority.
	 */
	ret |= check(base, "//g", "http://g/");

	ret |= check(base, "?y", "http://a/b/c/d;p?y");
	ret |= check(base, "g?y", "http://a/b/c/g?y");
//	ret |= check(base, "#s", "http://a/b/c/d;p?q#s");
//	ret |= check(base, "g#s", "http://a/b/c/g#s");
//	ret |= check(base, "g?y#s", "http://a/b/c/g?y#s");
	ret |= check(base, ";x", "http://a/b/c/;x");
	ret |= check(base, "g;x", "http://a/b/c/g;x");
//	ret |= check(base, "g;x?y#s", "http://a/b/c/g;x?y#s");
	ret |= check(base, "", "http://a/b/c/d;p?q");
	ret |= check(base, ".", "http://a/b/c/");
	ret |= check(base, "./", "http://a/b/c/");
	ret |= check(base, "..", "http://a/b/");
	ret |= check(base, "../", "http://a/b/");
	ret |= check(base, "../g", "http://a/b/g");
	ret |= check(base, "../..", "http://a/");
	ret |= check(base, "../../", "http://a/");
	ret |= check(base, "../../g", "http://a/g");

	ret |= check(base, "../../../g", "http://a/g");
	ret |= check(base, "../../../../g", "http://a/g");
	ret |= check(base, "/./g", "http://a/g");
	ret |= check(base, "/../g", "http://a/g");
	ret |= check(base, "g.", "http://a/b/c/g.");
	ret |= check(base, ".g", "http://a/b/c/.g");
	ret |= check(base, "g..", "http://a/b/c/g..");
	ret |= check(base, "..g", "http://a/b/c/..g");

	ret |= check(base, "./../g", "http://a/b/g");
	ret |= check(base, "./g/.", "http://a/b/c/g/");
	ret |= check(base, "g/./h", "http://a/b/c/g/h");
	ret |= check(base, "g/../h", "http://a/b/c/h");
	ret |= check(base, "g;x=1/./y", "http://a/b/c/g;x=1/y");
	ret |= check(base, "g;x=1/../y", "http://a/b/c/y");

	ret |= check(base, "g?y/./x", "http://a/b/c/g?y/./x");
	ret |= check(base, "g?y/../x", "http://a/b/c/g?y/../x");
//	ret |= check(base, "g#s/./x", "http://a/b/c/g#s/./x");
//	ret |= check(base, "g#s/../x", "http://a/b/c/g#s/../x");

	ret |= check(base, "http:g", "http:g");

	return (ret);
}
