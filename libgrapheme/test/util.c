/* See LICENSE file for copyright and license details. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../grapheme.h"
#include "../gen/types.h"
#include "util.h"

int
run_break_tests(size_t (*next_break)(const uint_least32_t *, size_t),
                const struct break_test *test, size_t testlen, const char *argv0)
{
	size_t i, j, off, res, failed;

	/* character break test */
	for (i = 0, failed = 0; i < testlen; i++) {
		for (j = 0, off = 0; off < test[i].cplen; off += res) {
			res = next_break(test[i].cp + off, test[i].cplen - off);

			/* check if our resulting offset matches */
			if (j == test[i].lenlen ||
			    res != test[i].len[j++]) {
				fprintf(stderr, "%s: Failed conformance test %zu \"%s\".\n",
				        argv0, i, test[i].descr);
				fprintf(stderr, "J=%zu: EXPECTED len %zu, got %zu\n", j-1, test[i].len[j-1], res);
				failed++;
				break;
			}
		}
	}
	printf("%s: %zu/%zu conformance tests passed.\n", argv0,
	       testlen - failed, testlen);

	return (failed > 0) ? 1 : 0;
}

int
run_unit_tests(int (*unit_test_callback)(const void *, size_t, const char *,
               const char *), const void *test, size_t testlen, const char *name,
               const char *argv0)
{
	size_t i, failed;

	for (i = 0, failed = 0; i < testlen; i++) {
		failed += (unit_test_callback(test, i, name, argv0) == 0) ? 0 : 1;
	}

	printf("%s: %s: %zu/%zu unit tests passed.\n", argv0, name,
	       testlen - failed, testlen);

	return (failed > 0) ? 1 : 0;
}

int
unit_test_callback_next_break(const struct unit_test_next_break *t, size_t off,
                                   size_t (*next_break)(const uint_least32_t *, size_t),
                                   const char *name, const char *argv0)
{
	const struct unit_test_next_break *test = t + off;

	size_t ret = next_break(test->input.src, test->input.srclen);

	if (ret != test->output.ret) {
		goto err;
	}

	return 0;
err:
	fprintf(stderr, "%s: %s: Failed unit test %zu \"%s\" "
	        "(returned %zu instead of %zu).\n", argv0,
	        name, off, test->description, ret, test->output.ret);
	return 1;
}

int
unit_test_callback_next_break_utf8(const struct unit_test_next_break_utf8 *t,
                                   size_t off,
                                   size_t (*next_break_utf8)(const char *, size_t),
                                   const char *name, const char *argv0)
{
	const struct unit_test_next_break_utf8 *test = t + off;

	size_t ret = next_break_utf8(test->input.src, test->input.srclen);

	if (ret != test->output.ret) {
		goto err;
	}

	return 0;
err:
	fprintf(stderr, "%s: %s: Failed unit test %zu \"%s\" "
	        "(returned %zu instead of %zu).\n", argv0,
	        name, off, test->description, ret, test->output.ret);
	return 1;
}
