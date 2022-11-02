/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../grapheme.h"
#include "../gen/character-test.h"
#include "util.h"

#include <utf8proc.h>

#define NUM_ITERATIONS 100000

struct break_benchmark_payload {
	uint_least32_t *buf;
	utf8proc_int32_t *buf_utf8proc;
	size_t buflen;
};

void
libgrapheme(const void *payload)
{
	uint_least16_t state = 0;
	const struct break_benchmark_payload *p = payload;
	size_t i;

	for (i = 0; i + 1 < p->buflen; i++) {
		(void)grapheme_is_character_break(p->buf[i], p->buf[i+1],
		                                  &state);
	}
}

void
libutf8proc(const void *payload)
{
	utf8proc_int32_t state = 0;
	const struct break_benchmark_payload *p = payload;
	size_t i;

	for (i = 0; i + 1 < p->buflen; i++) {
		(void)utf8proc_grapheme_break_stateful(p->buf_utf8proc[i],
		                                       p->buf_utf8proc[i+1],
		                                       &state);
	}
}

int
main(int argc, char *argv[])
{
	struct break_benchmark_payload p;
	double baseline = (double)NAN;
	size_t i;

	(void)argc;

	if ((p.buf = generate_cp_test_buffer(character_break_test,
	                                     LEN(character_break_test),
	                                     &(p.buflen))) == NULL) {
		return 1;
	}
	if ((p.buf_utf8proc = malloc(p.buflen * sizeof(*(p.buf_utf8proc)))) == NULL) {
		fprintf(stderr, "malloc: %s\n", strerror(errno));
		exit(1);
	}
	for (i = 0; i < p.buflen; i++) {
		/*
		 * there is no overflow, as we know that the maximum
		 * codepoint is 0x10FFFF, which is way below 2^31
		 */
		p.buf_utf8proc[i] = (utf8proc_int32_t)p.buf[i];
	}

	printf("%s\n", argv[0]);
	run_benchmark(libgrapheme, &p, "libgrapheme ", NULL, "comparison",
	              &baseline, NUM_ITERATIONS, p.buflen - 1);
	run_benchmark(libutf8proc, &p, "libutf8proc ", NULL, "comparison",
	              &baseline, NUM_ITERATIONS, p.buflen - 1);

	free(p.buf);
	free(p.buf_utf8proc);

	return 0;
}
