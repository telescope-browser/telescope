/* See LICENSE file for copyright and license details. */
#include <stddef.h>

#include "util.h"

#define FILE_EMOJI    "data/emoji-data.txt"
#define FILE_GRAPHEME "data/GraphemeBreakProperty.txt"

static const struct property_spec char_break_property[] = {
	{
		.enumname = "OTHER",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "CONTROL",
		.file     = FILE_GRAPHEME,
		.ucdname  = "Control",
	},
	{
		.enumname = "CR",
		.file     = FILE_GRAPHEME,
		.ucdname  = "CR",
	},
	{
		.enumname = "EXTEND",
		.file     = FILE_GRAPHEME,
		.ucdname  = "Extend",
	},
	{
		.enumname = "EXTENDED_PICTOGRAPHIC",
		.file     = FILE_EMOJI,
		.ucdname  = "Extended_Pictographic",
	},
	{
		.enumname = "HANGUL_L",
		.file     = FILE_GRAPHEME,
		.ucdname  = "L",
	},
	{
		.enumname = "HANGUL_V",
		.file     = FILE_GRAPHEME,
		.ucdname  = "V",
	},
	{
		.enumname = "HANGUL_T",
		.file     = FILE_GRAPHEME,
		.ucdname  = "T",
	},
	{
		.enumname = "HANGUL_LV",
		.file     = FILE_GRAPHEME,
		.ucdname  = "LV",
	},
	{
		.enumname = "HANGUL_LVT",
		.file     = FILE_GRAPHEME,
		.ucdname  = "LVT",
	},
	{
		.enumname = "LF",
		.file     = FILE_GRAPHEME,
		.ucdname  = "LF",
	},
	{
		.enumname = "PREPEND",
		.file     = FILE_GRAPHEME,
		.ucdname  = "Prepend",
	},
	{
		.enumname = "REGIONAL_INDICATOR",
		.file     = FILE_GRAPHEME,
		.ucdname  = "Regional_Indicator",
	},
	{
		.enumname = "SPACINGMARK",
		.file     = FILE_GRAPHEME,
		.ucdname  = "SpacingMark",
	},
	{
		.enumname = "ZWJ",
		.file     = FILE_GRAPHEME,
		.ucdname  = "ZWJ",
	},
};

int
main(int argc, char *argv[])
{
	(void)argc;

	properties_generate_break_property(char_break_property,
	                                   LEN(char_break_property),
	                                   NULL, NULL, "char_break", argv[0]);

	return 0;
}
