/* See LICENSE file for copyright and license details. */
#include <stdbool.h>
#include <stdint.h>

#include "../gen/word-test.h"
#include "../grapheme.h"
#include "util.h"

static const struct unit_test_next_break next_word_break[] = {
	{
		.description = "NULL input",
		.input = {
			.src    = NULL,
			.srclen = 0,
		},
		.output = { 0 },
	},
	{
		.description = "empty input",
		.input = {
			.src    = (uint_least32_t *)(uint_least32_t[]){ 0x0 },
			.srclen = 0,
		},
		.output = { 0 },
	},
	{
		.description = "empty input, null-terminated",
		.input = {
			.src    = (uint_least32_t *)(uint_least32_t[]){ 0x0 },
			.srclen = SIZE_MAX,
		},
		.output = { 0 },
	},
	{
		.description = "one word",
		.input = {
			.src    = (uint_least32_t *)(uint_least32_t[]){ 0x1F1E9, 0x1F1EA, 0x20, 0x2A },
			.srclen = 4,
		},
		.output = { 2 },
	},
	{
		.description = "one word, null-terminated",
		.input = {
			.src    = (uint_least32_t *)(uint_least32_t[]){ 0x1F1E9, 0x1F1EA, 0x20, 0x2A, 0x0 },
			.srclen = SIZE_MAX,
		},
		.output = { 2 },
	},
};

static const struct unit_test_next_break_utf8 next_word_break_utf8[] = {
	{
		.description = "NULL input",
		.input = {
			.src    = NULL,
			.srclen = 0,
		},
		.output = { 0 },
	},
	{
		.description = "empty input",
		.input = { "", 0 },
		.output = { 0 },
	},
	{
		.description = "empty input, NUL-terminated",
		.input = { "", SIZE_MAX },
		.output = { 0 },
	},
	{
		.description = "one word",
		.input = { "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA is", 11 },
		.output = { 8 },
	},
	{
		.description = "one word, fragment",
		.input = { "\xF0\x9F\x87\xA9\xF0", 5 },
		.output = { 4 },
	},
	{
		.description = "one word, NUL-terminated",
		.input = { "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA is", SIZE_MAX },
		.output = { 8 },
	},
	{
		.description = "one word, fragment, NUL-terminated",
		.input = { "\xF0\x9F\x87\xA9\xF0\x9F", SIZE_MAX },
		.output = { 4 },
	},
};

static int
unit_test_callback_next_word_break(const void *t, size_t off,
                                             const char *name,
                                             const char *argv0)
{
	return unit_test_callback_next_break(t, off,
	                                     grapheme_next_word_break,
	                                     name, argv0);
}

static int
unit_test_callback_next_word_break_utf8(const void *t, size_t off,
                                             const char *name,
                                             const char *argv0)
{
	return unit_test_callback_next_break_utf8(t, off,
	                                          grapheme_next_word_break_utf8,
	                                          name, argv0);
}

int
main(int argc, char *argv[])
{
	(void)argc;

	return run_break_tests(grapheme_next_word_break, word_break_test,
	                       LEN(word_break_test), argv[0]) +
	       run_unit_tests(unit_test_callback_next_word_break,
	                      next_word_break, LEN(next_word_break),
	                      "grapheme_next_word_break", argv[0]) +
	       run_unit_tests(unit_test_callback_next_word_break_utf8,
	                      next_word_break_utf8, LEN(next_word_break_utf8),
	                      "grapheme_next_word_break_utf8", argv[0]);
}
