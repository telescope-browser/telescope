/* See LICENSE file for copyright and license details. */
#ifndef UTIL_H
#define UTIL_H

#include "../gen/types.h"

#define LEN(x) (sizeof(x) / sizeof(*(x)))

#ifdef __has_attribute
	#if __has_attribute(optnone)
		void libgrapheme(const void *) __attribute__((optnone));
		void libutf8proc(const void *) __attribute__((optnone));
	#endif
#endif

uint_least32_t *generate_cp_test_buffer(const struct break_test *, size_t,
                                        size_t *);
char *generate_utf8_test_buffer(const struct break_test *, size_t, size_t *);

void run_benchmark(void (*func)(const void *), const void *, const char *,
                   const char *, const char *, double *, size_t, size_t);

#endif /* UTIL_H */
