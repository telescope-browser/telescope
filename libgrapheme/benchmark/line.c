/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../grapheme.h"
#include "../gen/line-test.h"
#include "util.h"

#define NUM_ITERATIONS 10000

struct break_benchmark_payload {
	uint_least32_t *buf;
	size_t buflen;
};

void
libgrapheme(const void *payload)
{
	const struct break_benchmark_payload *p = payload;
	size_t off;

	for (off = 0; off < p->buflen; ) {
		off += grapheme_next_line_break(p->buf + off, p->buflen - off);
	}
}

int
main(int argc, char *argv[])
{
	struct break_benchmark_payload p;
	double baseline = (double)NAN;

	(void)argc;

	if ((p.buf = generate_cp_test_buffer(line_break_test,
	                                     LEN(line_break_test),
	                                     &(p.buflen))) == NULL) {
		return 1;
	}

	printf("%s\n", argv[0]);
	run_benchmark(libgrapheme, &p, "libgrapheme ", NULL, "codepoint",
	              &baseline, NUM_ITERATIONS, p.buflen - 1);

	free(p.buf);

	return 0;
}
