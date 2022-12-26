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
resolve(const char *base, const char *ref, const char *expected)
{
	static struct iri	i;
	char			buf[512];

	if (iri_parse(base, ref, &i) == -1) {
		fprintf(stderr, "FAIL resolve(\"%s\", \"%s\") %s\n", base, ref,
		    strerror(errno));
		return (1);
	}

	if (iri_unparse(&i, buf, sizeof(buf)) == -1) {
		fprintf(stderr, "FAIL resolve(\"%s\", \"%s\") %s\n", base, ref,
		    strerror(errno));
		return (1);
	}

	if (strcmp(buf, expected) != 0) {
		fprintf(stderr, "FAIL resolve(\"%s\", \"%s\")\n", base, ref);
		fprintf(stderr, "got:\t%s\n", buf);
		fprintf(stderr, "want:\t%s\n", expected);
		return (1);
	}

	fprintf(stderr, "OK resolve(\"%s\", \"%s\") -> %s\n", base, ref,
	    expected);
	return (0);
}

int
main(void)
{
	const char	*base = "http://a/b/c/d;p?q";
	int		 ret = 0;

	ret |= resolve(base, "g:h", "g:h");
	ret |= resolve(base, "g", "http://a/b/c/g");
	ret |= resolve(base, "./g", "http://a/b/c/g");
	ret |= resolve(base, "g/", "http://a/b/c/g/");
	ret |= resolve(base, "/g", "http://a/g");

	/*
	 * the RFC says "http://g" but we always normalize an
	 * empty path to "/" if there is an authority.
	 */
	ret |= resolve(base, "//g", "http://g/");

	ret |= resolve(base, "?y", "http://a/b/c/d;p?y");
	ret |= resolve(base, "g?y", "http://a/b/c/g?y");
	ret |= resolve(base, "#s", "http://a/b/c/d;p?q#s");
	ret |= resolve(base, "g#s", "http://a/b/c/g#s");
	ret |= resolve(base, "g?y#s", "http://a/b/c/g?y#s");
	ret |= resolve(base, ";x", "http://a/b/c/;x");
	ret |= resolve(base, "g;x", "http://a/b/c/g;x");
	ret |= resolve(base, "g;x?y#s", "http://a/b/c/g;x?y#s");
	ret |= resolve(base, "", "http://a/b/c/d;p?q");
	ret |= resolve(base, ".", "http://a/b/c/");
	ret |= resolve(base, "./", "http://a/b/c/");
	ret |= resolve(base, "..", "http://a/b/");
	ret |= resolve(base, "../", "http://a/b/");
	ret |= resolve(base, "../g", "http://a/b/g");
	ret |= resolve(base, "../..", "http://a/");
	ret |= resolve(base, "../../", "http://a/");
	ret |= resolve(base, "../../g", "http://a/g");

	ret |= resolve(base, "../../../g", "http://a/g");
	ret |= resolve(base, "../../../../g", "http://a/g");
	ret |= resolve(base, "/./g", "http://a/g");
	ret |= resolve(base, "/../g", "http://a/g");
	ret |= resolve(base, "g.", "http://a/b/c/g.");
	ret |= resolve(base, ".g", "http://a/b/c/.g");
	ret |= resolve(base, "g..", "http://a/b/c/g..");
	ret |= resolve(base, "..g", "http://a/b/c/..g");

	ret |= resolve(base, "./../g", "http://a/b/g");
	ret |= resolve(base, "./g/.", "http://a/b/c/g/");
	ret |= resolve(base, "g/./h", "http://a/b/c/g/h");
	ret |= resolve(base, "g/../h", "http://a/b/c/h");
	ret |= resolve(base, "g;x=1/./y", "http://a/b/c/g;x=1/y");
	ret |= resolve(base, "g;x=1/../y", "http://a/b/c/y");

	ret |= resolve(base, "g?y/./x", "http://a/b/c/g?y/./x");
	ret |= resolve(base, "g?y/../x", "http://a/b/c/g?y/../x");
	ret |= resolve(base, "g#s/./x", "http://a/b/c/g#s/./x");
	ret |= resolve(base, "g#s/../x", "http://a/b/c/g#s/../x");

	ret |= resolve(base, "http:g", "http:g");

	return (ret);
}
