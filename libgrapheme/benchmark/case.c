/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../grapheme.h"
#include "../gen/word-test.h"
#include "util.h"

#define NUM_ITERATIONS 10000

struct break_benchmark_payload {
	uint_least32_t *src;
	size_t srclen;
	uint_least32_t *dest;
	size_t destlen;
};

void
libgrapheme(const void *payload)
{
	const struct break_benchmark_payload *p = payload;

	grapheme_to_uppercase(p->src, p->srclen, p->dest, p->destlen);
}

int
main(int argc, char *argv[])
{
	struct break_benchmark_payload p;
	double baseline = (double)NAN;

	(void)argc;

	if ((p.src = generate_cp_test_buffer(word_break_test,
	                                     LEN(word_break_test),
	                                     &(p.srclen))) == NULL) {
		return 1;
	}
	if ((p.dest = calloc((p.destlen = 2 * p.srclen), sizeof(*(p.dest)))) == NULL) {
		fprintf(stderr, "calloc: Out of memory\n");
	}

	printf("%s\n", argv[0]);
	run_benchmark(libgrapheme, &p, "libgrapheme ", NULL, "codepoint",
	              &baseline, NUM_ITERATIONS, p.srclen - 1);

	free(p.src);
	free(p.dest);

	return 0;
}
