/* See LICENSE file for copyright and license details. */
#include <stdbool.h>
#include <stdint.h>

#include "../gen/character-test.h"
#include "../grapheme.h"
#include "util.h"

static const struct unit_test_next_break next_character_break[] = {
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
		.description = "one character",
		.input = {
			.src    = (uint_least32_t *)(uint_least32_t[]){ 0x1F1E9, 0x1F1EA, 0x2A },
			.srclen = 3,
		},
		.output = { 2 },
	},
	{
		.description = "one character, null-terminated",
		.input = {
			.src    = (uint_least32_t *)(uint_least32_t[]){ 0x1F1E9, 0x1F1EA, 0x0 },
			.srclen = SIZE_MAX,
		},
		.output = { 2 },
	},
};

static const struct unit_test_next_break_utf8 next_character_break_utf8[] = {
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
		.description = "one character",
		.input = { "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA*", 9 },
		.output = { 8 },
	},
	{
		.description = "one character, fragment",
		.input = { "\xF0\x9F\x87\xA9\xF0", 5 },
		.output = { 4 },
	},
	{
		.description = "one character, NUL-terminated",
		.input = { "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA", SIZE_MAX },
		.output = { 8 },
	},
	{
		.description = "one character, fragment, NUL-terminated",
		.input = { "\xF0\x9F\x87\xA9\xF0\x9F", SIZE_MAX },
		.output = { 4 },
	},
};

static int
unit_test_callback_next_character_break(const void *t, size_t off,
                                             const char *name,
                                             const char *argv0)
{
	return unit_test_callback_next_break(t, off,
	                                     grapheme_next_character_break,
	                                     name, argv0);
}

static int
unit_test_callback_next_character_break_utf8(const void *t, size_t off,
                                             const char *name,
                                             const char *argv0)
{
	return unit_test_callback_next_break_utf8(t, off,
	                                          grapheme_next_character_break_utf8,
	                                          name, argv0);
}

int
main(int argc, char *argv[])
{
	(void)argc;

	return run_break_tests(grapheme_next_character_break,
	                       character_break_test, LEN(character_break_test), argv[0]) +
	       run_unit_tests(unit_test_callback_next_character_break,
	                      next_character_break, LEN(next_character_break),
	                      "grapheme_next_character_break", argv[0]) +
	       run_unit_tests(unit_test_callback_next_character_break_utf8,
	                      next_character_break_utf8, LEN(next_character_break_utf8),
	                      "grapheme_next_character_break_utf8", argv[0]);
}
