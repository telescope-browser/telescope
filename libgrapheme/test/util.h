/* See LICENSE file for copyright and license details. */
#ifndef UTIL_H
#define UTIL_H

#include "../gen/types.h"
#include "../grapheme.h"

#undef MIN
#define MIN(x,y)  ((x) < (y) ? (x) : (y))
#undef LEN
#define LEN(x) (sizeof(x) / sizeof(*(x)))

struct unit_test_next_break {
	const char *description;
	struct {
		const uint_least32_t *src;
		size_t srclen;
	} input;
	struct {
		size_t ret;
	} output;
};

struct unit_test_next_break_utf8 {
	const char *description;
	struct {
		const char *src;
		size_t srclen;
	} input;
	struct {
		size_t ret;
	} output;
};

int run_break_tests(size_t (*next_break)(const uint_least32_t *, size_t),
                    const struct break_test *test, size_t testlen,
                    const char *);
int run_unit_tests(int (*unit_test_callback)(const void *, size_t, const char *,
                   const char *), const void *, size_t, const char *, const char *);

int unit_test_callback_next_break(const struct unit_test_next_break *, size_t,
                                  size_t (*next_break)(const uint_least32_t *, size_t),
                                  const char *, const char *);
int unit_test_callback_next_break_utf8(const struct unit_test_next_break_utf8 *,
                                       size_t,
                                       size_t (*next_break_utf8)(const char *, size_t),
                                       const char *, const char *);

#endif /* UTIL_H */
