/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define FILE_EMOJI "data/emoji-data.txt"
#define FILE_WORD  "data/WordBreakProperty.txt"

static const struct property_spec word_break_property[] = {
	{
		.enumname = "OTHER",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "ALETTER",
		.file     = FILE_WORD,
		.ucdname  = "ALetter",
	},
	{
		.enumname = "BOTH_ALETTER_EXTPICT",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "CR",
		.file     = FILE_WORD,
		.ucdname  = "CR",
	},
	{
		.enumname = "DOUBLE_QUOTE",
		.file     = FILE_WORD,
		.ucdname  = "Double_Quote",
	},
	{
		.enumname = "EXTEND",
		.file     = FILE_WORD,
		.ucdname  = "Extend",
	},
	{
		.enumname = "EXTENDED_PICTOGRAPHIC",
		.file     = FILE_EMOJI,
		.ucdname  = "Extended_Pictographic",
	},
	{
		.enumname = "EXTENDNUMLET",
		.file     = FILE_WORD,
		.ucdname  = "ExtendNumLet",
	},
	{
		.enumname = "FORMAT",
		.file     = FILE_WORD,
		.ucdname  = "Format",
	},
	{
		.enumname = "HEBREW_LETTER",
		.file     = FILE_WORD,
		.ucdname  = "Hebrew_Letter",
	},
	{
		.enumname = "KATAKANA",
		.file     = FILE_WORD,
		.ucdname  = "Katakana",
	},
	{
		.enumname = "LF",
		.file     = FILE_WORD,
		.ucdname  = "LF",
	},
	{
		.enumname = "MIDLETTER",
		.file     = FILE_WORD,
		.ucdname  = "MidLetter",
	},
	{
		.enumname = "MIDNUM",
		.file     = FILE_WORD,
		.ucdname  = "MidNum",
	},
	{
		.enumname = "MIDNUMLET",
		.file     = FILE_WORD,
		.ucdname  = "MidNumLet",
	},
	{
		.enumname = "NEWLINE",
		.file     = FILE_WORD,
		.ucdname  = "Newline",
	},
	{
		.enumname = "NUMERIC",
		.file     = FILE_WORD,
		.ucdname  = "Numeric",
	},
	{
		.enumname = "REGIONAL_INDICATOR",
		.file     = FILE_WORD,
		.ucdname  = "Regional_Indicator",
	},
	{
		.enumname = "SINGLE_QUOTE",
		.file     = FILE_WORD,
		.ucdname  = "Single_Quote",
	},
	{
		.enumname = "WSEGSPACE",
		.file     = FILE_WORD,
		.ucdname  = "WSegSpace",
	},
	{
		.enumname = "ZWJ",
		.file     = FILE_WORD,
		.ucdname  = "ZWJ",
	},
};

static uint_least8_t
handle_conflict(uint_least32_t cp, uint_least8_t prop1, uint_least8_t prop2)
{
	uint_least8_t result;

	(void)cp;

	if ((!strcmp(word_break_property[prop1].enumname, "ALETTER") &&
	     !strcmp(word_break_property[prop2].enumname, "EXTENDED_PICTOGRAPHIC")) ||
	    (!strcmp(word_break_property[prop1].enumname, "EXTENDED_PICTOGRAPHIC") &&
	     !strcmp(word_break_property[prop2].enumname, "ALETTER"))) {
		for (result = 0; result < LEN(word_break_property); result++) {
			if (!strcmp(word_break_property[result].enumname,
			            "BOTH_ALETTER_EXTPICT")) {
				break;
			}
		}
		if (result == LEN(word_break_property)) {
			fprintf(stderr, "handle_conflict: Internal error.\n");
			exit(1);
		}
	} else {
		fprintf(stderr, "handle_conflict: Cannot handle conflict.\n");
		exit(1);
	}

	return result;
}

int
main(int argc, char *argv[])
{
	(void)argc;

	properties_generate_break_property(word_break_property,
	                                   LEN(word_break_property),
	                                   handle_conflict, NULL, "word_break",
	                                   argv[0]);

	return 0;
}
