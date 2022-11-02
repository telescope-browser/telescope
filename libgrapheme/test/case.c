/* See LICENSE file for copyright and license details. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../grapheme.h"
#include "util.h"

struct unit_test_is_case_utf8 {
	const char *description;
	struct {
		const char *src;
		size_t srclen;
	} input;
	struct {
		bool ret;
		size_t caselen;
	} output;
};

struct unit_test_to_case_utf8 {
	const char *description;
	struct {
		const char *src;
		size_t srclen;
		size_t destlen;
	} input;
	struct {
		const char *dest;
		size_t ret;
	} output;
};

static const struct unit_test_is_case_utf8 is_lowercase_utf8[] = {
	{
		.description = "empty input",
		.input =  { "", 0 },
		.output = { true, 0 },
	},
	{
		.description = "one character, violation",
		.input =  { "A", 1 },
		.output = { false, 0 },
	},
	{
		.description = "one character, confirmation",
		.input =  { "\xC3\x9F", 2 },
		.output = { true, 2 },
	},
	{
		.description = "one character, violation, NUL-terminated",
		.input =  { "A", SIZE_MAX },
		.output = { false, 0 },
	},
	{
		.description = "one character, confirmation, NUL-terminated",
		.input =  { "\xC3\x9F", SIZE_MAX },
		.output = { true, 2 },
	},
	{
		.description = "one word, violation",
		.input =  { "Hello", 5 },
		.output = { false, 0 },
	},
	{
		.description = "one word, partial confirmation",
		.input =  { "gru" "\xC3\x9F" "fOrmel", 11 },
		.output = { false, 6 },
	},
	{
		.description = "one word, full confirmation",
		.input =  { "gru" "\xC3\x9F" "formel", 11 },
		.output = { true, 11 },
	},
	{
		.description = "one word, violation, NUL-terminated",
		.input =  { "Hello", SIZE_MAX },
		.output = { false, 0 },
	},
	{
		.description = "one word, partial confirmation, NUL-terminated",
		.input =  { "gru" "\xC3\x9F" "fOrmel", SIZE_MAX },
		.output = { false, 6 },
	},
	{
		.description = "one word, full confirmation, NUL-terminated",
		.input =  { "gru" "\xC3\x9F" "formel", SIZE_MAX },
		.output = { true, 11 },
	},
};

static const struct unit_test_is_case_utf8 is_uppercase_utf8[] = {
	{
		.description = "empty input",
		.input =  { "", 0 },
		.output = { true, 0 },
	},
	{
		.description = "one character, violation",
		.input =  { "\xC3\x9F", 2 },
		.output = { false, 0 },
	},
	{
		.description = "one character, confirmation",
		.input =  { "A", 1 },
		.output = { true, 1 },
	},
	{
		.description = "one character, violation, NUL-terminated",
		.input =  { "\xC3\x9F", SIZE_MAX },
		.output = { false, 0 },
	},
	{
		.description = "one character, confirmation, NUL-terminated",
		.input =  { "A", SIZE_MAX },
		.output = { true, 1 },
	},
	{
		.description = "one word, violation",
		.input =  { "hello", 5 },
		.output = { false, 0 },
	},
	{
		.description = "one word, partial confirmation",
		.input =  { "GRU" "\xC3\x9F" "formel", 11 },
		.output = { false, 3 },
	},
	{
		.description = "one word, full confirmation",
		.input =  { "HELLO", 5 },
		.output = { true, 5 },
	},
	{
		.description = "one word, violation, NUL-terminated",
		.input =  { "hello", SIZE_MAX },
		.output = { false, 0 },
	},
	{
		.description = "one word, partial confirmation, NUL-terminated",
		.input =  { "GRU" "\xC3\x9F" "formel", SIZE_MAX },
		.output = { false, 3 },
	},
	{
		.description = "one word, full confirmation, NUL-terminated",
		.input =  { "HELLO", SIZE_MAX },
		.output = { true, 5 },
	},
};

static const struct unit_test_is_case_utf8 is_titlecase_utf8[] = {
	{
		.description = "empty input",
		.input =  { "", 0 },
		.output = { true, 0 },
	},
	{
		.description = "one character, violation",
		.input =  { "\xC3\x9F", 2 },
		.output = { false, 0 },
	},
	{
		.description = "one character, confirmation",
		.input =  { "A", 1 },
		.output = { true, 1 },
	},
	{
		.description = "one character, violation, NUL-terminated",
		.input =  { "\xC3\x9F", SIZE_MAX },
		.output = { false, 0 },
	},
	{
		.description = "one character, confirmation, NUL-terminated",
		.input =  { "A", SIZE_MAX },
		.output = { true, 1 },
	},
	{
		.description = "one word, violation",
		.input =  { "hello", 5 },
		.output = { false, 0 },
	},
	{
		.description = "one word, partial confirmation",
		.input =  { "Gru" "\xC3\x9F" "fOrmel", 11 },
		.output = { false, 6 },
	},
	{
		.description = "one word, full confirmation",
		.input =  { "Gru" "\xC3\x9F" "formel", 11 },
		.output = { true, 11 },
	},
	{
		.description = "one word, violation, NUL-terminated",
		.input =  { "hello", SIZE_MAX },
		.output = { false, 0 },
	},
	{
		.description = "one word, partial confirmation, NUL-terminated",
		.input =  { "Gru" "\xC3\x9F" "fOrmel", SIZE_MAX },
		.output = { false, 6 },
	},
	{
		.description = "one word, full confirmation, NUL-terminated",
		.input =  { "Gru" "\xC3\x9F" "formel", SIZE_MAX },
		.output = { true, 11 },
	},
	{
		.description = "multiple words, partial confirmation",
		.input =  { "Hello Gru" "\xC3\x9F" "fOrmel!", 18 },
		.output = { false, 12 },
	},
	{
		.description = "multiple words, full confirmation",
		.input =  { "Hello Gru" "\xC3\x9F" "formel!", 18 },
		.output = { true, 18 },
	},
	{
		.description = "multiple words, partial confirmation, NUL-terminated",
		.input =  { "Hello Gru" "\xC3\x9F" "fOrmel!", SIZE_MAX },
		.output = { false, 12 },
	},
	{
		.description = "multiple words, full confirmation, NUL-terminated",
		.input =  { "Hello Gru" "\xC3\x9F" "formel!", SIZE_MAX },
		.output = { true, 18 },
	},
};

static const struct unit_test_to_case_utf8 to_lowercase_utf8[] = {
	{
		.description = "empty input",
		.input =  { "", 0, 10 },
		.output = { "", 0 },
	},
	{
		.description = "empty output",
		.input =  { "hello", 5, 0 },
		.output = { "", 5 },
	},
	{
		.description = "one character, conversion",
		.input =  { "A", 1, 10 },
		.output = { "a", 1 },
	},
	{
		.description = "one character, no conversion",
		.input =  { "\xC3\x9F", 2, 10 },
		.output = { "\xC3\x9F", 2 },
	},
	{
		.description = "one character, conversion, truncation",
		.input =  { "A", 1, 0 },
		.output = { "", 1 },
	},
	{
		.description = "one character, conversion, NUL-terminated",
		.input =  { "A", SIZE_MAX, 10 },
		.output = { "a", 1 },
	},
	{
		.description = "one character, no conversion, NUL-terminated",
		.input =  { "\xC3\x9F", SIZE_MAX, 10 },
		.output = { "\xC3\x9F", 2 },
	},
	{
		.description = "one character, conversion, NUL-terminated, truncation",
		.input =  { "A", SIZE_MAX, 0 },
		.output = { "", 1 },
	},
	{
		.description = "one word, conversion",
		.input =  { "wOrD", 4, 10 },
		.output = { "word", 4 },
	},
	{
		.description = "one word, no conversion",
		.input =  { "word", 4, 10 },
		.output = { "word", 4 },
	},
	{
		.description = "one word, conversion, truncation",
		.input =  { "wOrD", 4, 3 },
		.output = { "wo", 4 },
	},
	{
		.description = "one word, conversion, NUL-terminated",
		.input =  { "wOrD", SIZE_MAX, 10 },
		.output = { "word", 4 },
	},
	{
		.description = "one word, no conversion, NUL-terminated",
		.input =  { "word", SIZE_MAX, 10 },
		.output = { "word", 4 },
	},
	{
		.description = "one word, conversion, NUL-terminated, truncation",
		.input =  { "wOrD", SIZE_MAX, 3 },
		.output = { "wo", 4 },
	},
};

static const struct unit_test_to_case_utf8 to_uppercase_utf8[] = {
	{
		.description = "empty input",
		.input =  { "", 0, 10 },
		.output = { "", 0 },
	},
	{
		.description = "empty output",
		.input =  { "hello", 5, 0 },
		.output = { "", 5 },
	},
	{
		.description = "one character, conversion",
		.input =  { "\xC3\x9F", 2, 10 },
		.output = { "SS", 2 },
	},
	{
		.description = "one character, no conversion",
		.input =  { "A", 1, 10 },
		.output = { "A", 1 },
	},
	{
		.description = "one character, conversion, truncation",
		.input =  { "\xC3\x9F", 2, 0 },
		.output = { "", 2 },
	},
	{
		.description = "one character, conversion, NUL-terminated",
		.input =  { "\xC3\x9F", SIZE_MAX, 10 },
		.output = { "SS", 2 },
	},
	{
		.description = "one character, no conversion, NUL-terminated",
		.input =  { "A", SIZE_MAX, 10 },
		.output = { "A", 1 },
	},
	{
		.description = "one character, conversion, NUL-terminated, truncation",
		.input =  { "\xC3\x9F", SIZE_MAX, 0 },
		.output = { "", 2 },
	},
	{
		.description = "one word, conversion",
		.input =  { "gRu" "\xC3\x9F" "fOrMel", 11, 15 },
		.output = { "GRUSSFORMEL", 11 },
	},
	{
		.description = "one word, no conversion",
		.input =  { "WORD", 4, 10 },
		.output = { "WORD", 4 },
	},
	{
		.description = "one word, conversion, truncation",
		.input =  { "gRu" "\xC3\x9F" "formel", 11, 5 },
		.output = { "GRUS", 11 },
	},
	{
		.description = "one word, conversion, NUL-terminated",
		.input =  { "gRu" "\xC3\x9F" "formel", SIZE_MAX, 15 },
		.output = { "GRUSSFORMEL", 11 },
	},
	{
		.description = "one word, no conversion, NUL-terminated",
		.input =  { "WORD", SIZE_MAX, 10 },
		.output = { "WORD", 4 },
	},
	{
		.description = "one word, conversion, NUL-terminated, truncation",
		.input =  { "gRu" "\xC3\x9F" "formel", SIZE_MAX, 5 },
		.output = { "GRUS", 11 },
	},
};

static const struct unit_test_to_case_utf8 to_titlecase_utf8[] = {
	{
		.description = "empty input",
		.input =  { "", 0, 10 },
		.output = { "", 0 },
	},
	{
		.description = "empty output",
		.input =  { "hello", 5, 0 },
		.output = { "", 5 },
	},
	{
		.description = "one character, conversion",
		.input =  { "a", 1, 10 },
		.output = { "A", 1 },
	},
	{
		.description = "one character, no conversion",
		.input =  { "A", 1, 10 },
		.output = { "A", 1 },
	},
	{
		.description = "one character, conversion, truncation",
		.input =  { "a", 1, 0 },
		.output = { "", 1 },
	},
	{
		.description = "one character, conversion, NUL-terminated",
		.input =  { "a", SIZE_MAX, 10 },
		.output = { "A", 1 },
	},
	{
		.description = "one character, no conversion, NUL-terminated",
		.input =  { "A", SIZE_MAX, 10 },
		.output = { "A", 1 },
	},
	{
		.description = "one character, conversion, NUL-terminated, truncation",
		.input =  { "a", SIZE_MAX, 0 },
		.output = { "", 1 },
	},
	{
		.description = "one word, conversion",
		.input =  { "heLlo", 5, 10 },
		.output = { "Hello", 5 },
	},
	{
		.description = "one word, no conversion",
		.input =  { "Hello", 5, 10 },
		.output = { "Hello", 5 },
	},
	{
		.description = "one word, conversion, truncation",
		.input =  { "heLlo", 5, 2 },
		.output = { "H", 5 },
	},
	{
		.description = "one word, conversion, NUL-terminated",
		.input =  { "heLlo", SIZE_MAX, 10 },
		.output = { "Hello", 5 },
	},
	{
		.description = "one word, no conversion, NUL-terminated",
		.input =  { "Hello", SIZE_MAX, 10 },
		.output = { "Hello", 5 },
	},
	{
		.description = "one word, conversion, NUL-terminated, truncation",
		.input =  { "heLlo", SIZE_MAX, 3 },
		.output = { "He", 5 },
	},
	{
		.description = "two words, conversion",
		.input =  { "heLlo wORLd!", 12, 20 },
		.output = { "Hello World!", 12 },
	},
	{
		.description = "two words, no conversion",
		.input =  { "Hello World!", 12, 20 },
		.output = { "Hello World!", 12 },
	},
	{
		.description = "two words, conversion, truncation",
		.input =  { "heLlo wORLd!", 12, 8 },
		.output = { "Hello W", 12 },
	},
	{
		.description = "two words, conversion, NUL-terminated",
		.input =  { "heLlo wORLd!", SIZE_MAX, 20 },
		.output = { "Hello World!", 12 },
	},
	{
		.description = "two words, no conversion, NUL-terminated",
		.input =  { "Hello World!", SIZE_MAX, 20 },
		.output = { "Hello World!", 12 },
	},
	{
		.description = "two words, conversion, NUL-terminated, truncation",
		.input =  { "heLlo wORLd!", SIZE_MAX, 4 },
		.output = { "Hel", 12 },
	},
};

static int
unit_test_callback_is_case_utf8(const void *t, size_t off, const char *name,
                                const char *argv0)
{
	const struct unit_test_is_case_utf8 *test =
		(const struct unit_test_is_case_utf8 *)t + off;
	bool ret = false;
	size_t caselen = 0x7f;

	if (t == is_lowercase_utf8) {
		ret = grapheme_is_lowercase_utf8(test->input.src, test->input.srclen,
		                                 &caselen);
	} else if (t == is_uppercase_utf8) {
		ret = grapheme_is_uppercase_utf8(test->input.src, test->input.srclen,
		                                 &caselen);
	} else if (t == is_titlecase_utf8) {
		ret = grapheme_is_titlecase_utf8(test->input.src, test->input.srclen,
		                                 &caselen);

	} else {
		goto err;
	}

	/* check results */
	if (ret != test->output.ret || caselen != test->output.caselen) {
		goto err;
	}

	return 0;
err:
	fprintf(stderr, "%s: %s: Failed unit test %zu \"%s\" "
	        "(returned (%s, %zu) instead of (%s, %zu)).\n", argv0,
	        name, off, test->description, ret ? "true" : "false",
		caselen, test->output.ret ? "true" : "false",
	        test->output.caselen);
	return 1;
}

static int
unit_test_callback_to_case_utf8(const void *t, size_t off, const char *name,
                                const char *argv0)
{
	const struct unit_test_to_case_utf8 *test =
		(const struct unit_test_to_case_utf8 *)t + off;
	size_t ret = 0, i;
	char buf[512];

	/* fill the array with canary values */
	memset(buf, 0x7f, LEN(buf));

	if (t == to_lowercase_utf8) {
		ret = grapheme_to_lowercase_utf8(test->input.src, test->input.srclen,
		                                 buf, test->input.destlen);
	} else if (t == to_uppercase_utf8) {
		ret = grapheme_to_uppercase_utf8(test->input.src, test->input.srclen,
		                                 buf, test->input.destlen);
	} else if (t == to_titlecase_utf8) {
		ret = grapheme_to_titlecase_utf8(test->input.src, test->input.srclen,
		                                 buf, test->input.destlen);
	} else {
		goto err;
	}

	/* check results */
	if (ret != test->output.ret ||
	    memcmp(buf, test->output.dest, MIN(test->input.destlen, test->output.ret))) {
		goto err;
	}

	/* check that none of the canary values have been overwritten */
	for (i = test->input.destlen; i < LEN(buf); i++) {
		if (buf[i] != 0x7f) {
			goto err;
		}
	}

	return 0;
err:
	fprintf(stderr, "%s: %s: Failed unit test %zu \"%s\" "
	        "(returned (\"%.*s\", %zu) instead of (\"%.*s\", %zu)).\n", argv0,
	        name, off, test->description, (int)ret, buf, ret,
	        (int)test->output.ret, test->output.dest, test->output.ret);
	return 1;
}

int
main(int argc, char *argv[])
{
	(void)argc;

	return run_unit_tests(unit_test_callback_is_case_utf8, is_lowercase_utf8,
	                      LEN(is_lowercase_utf8), "grapheme_is_lowercase_utf8", argv[0]) +
	       run_unit_tests(unit_test_callback_is_case_utf8, is_uppercase_utf8,
	                      LEN(is_uppercase_utf8), "grapheme_is_uppercase_utf8", argv[0]) +
	       run_unit_tests(unit_test_callback_is_case_utf8, is_titlecase_utf8,
	                      LEN(is_titlecase_utf8), "grapheme_is_titlecase_utf8", argv[0]) +
	       run_unit_tests(unit_test_callback_to_case_utf8, to_lowercase_utf8,
	                      LEN(to_lowercase_utf8), "grapheme_to_lowercase_utf8", argv[0]) +
	       run_unit_tests(unit_test_callback_to_case_utf8, to_uppercase_utf8,
	                      LEN(to_uppercase_utf8), "grapheme_to_uppercase_utf8", argv[0]) +
	       run_unit_tests(unit_test_callback_to_case_utf8, to_titlecase_utf8,
	                      LEN(to_titlecase_utf8), "grapheme_to_titlecase_utf8", argv[0]);
}
