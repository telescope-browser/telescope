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

struct utf8_benchmark_payload {
	char *buf;
	utf8proc_uint8_t *buf_utf8proc;
	size_t buflen;
};

void
libgrapheme(const void *payload)
{
	const struct utf8_benchmark_payload *p = payload;
	uint_least32_t cp;
	size_t ret, off;

	for (off = 0; off < p->buflen; off += ret) {
		if ((ret = grapheme_decode_utf8(p->buf + off,
		                                p->buflen - off, &cp)) >
		    (p->buflen - off)) {
			break;
		}
		(void)cp;
	}
}

void
libutf8proc(const void *payload)
{
	const struct utf8_benchmark_payload *p = payload;
	utf8proc_int32_t cp;
	utf8proc_ssize_t ret;
	size_t off;

	for (off = 0; off < p->buflen; off += (size_t)ret) {
		if ((ret = utf8proc_iterate(p->buf_utf8proc + off,
		                            (utf8proc_ssize_t)(p->buflen - off),
				            &cp)) < 0) {
			break;
		}
		(void)cp;
	}
}

int
main(int argc, char *argv[])
{
	struct utf8_benchmark_payload p;
	size_t i;
	double baseline = (double)NAN;

	(void)argc;

	p.buf = generate_utf8_test_buffer(character_break_test,
	                                  LEN(character_break_test),
	                                  &(p.buflen));

	/* convert cp-buffer to stupid custom libutf8proc-uint8-type */
	if ((p.buf_utf8proc = malloc(p.buflen)) == NULL) {
		fprintf(stderr, "malloc: %s\n", strerror(errno));
		exit(1);
	}
	for (i = 0; i < p.buflen; i++) {
		/* 
		 * even if char is larger than 8 bit, it will only have
		 * any of the first 8 bits set (by construction).
		 */
		p.buf_utf8proc[i] = (utf8proc_uint8_t)p.buf[i];
	}

	printf("%s\n", argv[0]);
	run_benchmark(libgrapheme, &p, "libgrapheme ", NULL,
	              "byte", &baseline, NUM_ITERATIONS, p.buflen);
	run_benchmark(libutf8proc, &p, "libutf8proc ",
	              "but unsafe (does not detect overlong encodings)",
	              "byte", &baseline, NUM_ITERATIONS, p.buflen);

	free(p.buf);
	free(p.buf_utf8proc);

	return 0;
}
