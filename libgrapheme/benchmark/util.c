/* See LICENSE file for copyright and license details. */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "../gen/types.h"
#include "../grapheme.h"
#include "util.h"

uint_least32_t *
generate_cp_test_buffer(const struct break_test *test, size_t testlen,
                        size_t *buflen)
{
	size_t i, j, off;
	uint_least32_t *buf;

	/* allocate and generate buffer */
	for (i = 0, *buflen = 0; i < testlen; i++) {
		*buflen += test[i].cplen;
	}
	if (!(buf = calloc(*buflen, sizeof(*buf)))) {
		fprintf(stderr, "generate_test_buffer: calloc: Out of memory.\n");
		exit(1);
	}
	for (i = 0, off = 0; i < testlen; i++) {
		for (j = 0; j < test[i].cplen; j++) {
			buf[off + j] = test[i].cp[j];
		}
		off += test[i].cplen;
	}

	return buf;
}

char *
generate_utf8_test_buffer(const struct break_test *test, size_t testlen,
                          size_t *buflen)
{
	size_t i, j, off, ret;
	char *buf;

	/* allocate and generate buffer */
	for (i = 0, *buflen = 0; i < testlen; i++) {
		for (j = 0; j < test[i].cplen; j++) {
			*buflen += grapheme_encode_utf8(test[i].cp[j], NULL, 0);
		}
	}
	(*buflen)++; /* terminating NUL-byte */
	if (!(buf = malloc(*buflen))) {
		fprintf(stderr, "generate_test_buffer: malloc: Out of memory.\n");
		exit(1);
	}
	for (i = 0, off = 0; i < testlen; i++) {
		for (j = 0; j < test[i].cplen; j++, off += ret) {
			if ((ret = grapheme_encode_utf8(test[i].cp[j],
			                                buf + off,
			                                *buflen - off)) >
			    (*buflen - off)) {
				/* shouldn't happen */
				fprintf(stderr, "generate_utf8_test_buffer: "
				        "Buffer too small.\n");
				exit(1);
			}
		}
	}
	buf[*buflen - 1] = '\0';

	return buf;
}

static double
time_diff(struct timespec *a, struct timespec *b)
{
	return (double)(b->tv_sec - a->tv_sec) +
	       (double)(b->tv_nsec - a->tv_nsec) * 1E-9;
}

void
run_benchmark(void (*func)(const void *), const void *payload,
              const char *name, const char *comment, const char *unit,
              double *baseline, size_t num_iterations,
              size_t units_per_iteration)
{
	struct timespec start, end;
	size_t i;
	double diff;

	printf("\t%s ", name);
	fflush(stdout);

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < num_iterations; i++) {
		func(payload);

		if (i % (num_iterations / 10) == 0) {
			printf(".");
			fflush(stdout);
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	diff = time_diff(&start, &end) / (double)num_iterations /
	       (double)units_per_iteration;

	if (isnan(*baseline)) {
		*baseline = diff;
		printf(" avg. %.3es/%s (baseline)\n", diff, unit);
	} else {
		printf(" avg. %.3es/%s (%.2f%% %s%s%s)\n", diff, unit,
		       fabs(1.0 - diff / *baseline) * 100,
		       (diff < *baseline) ? "faster" : "slower",
		       comment ? ", " : "",
		       comment ? comment : "");
	}
}
