/* See LICENSE file for copyright and license details. */
#include "util.h"

#define FILE_SENTENCE "data/SentenceBreakProperty.txt"

static const struct property_spec sentence_break_property[] = {
	{
		.enumname = "OTHER",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "CR",
		.file     = FILE_SENTENCE,
		.ucdname  = "CR",
	},
	{
		.enumname = "LF",
		.file     = FILE_SENTENCE,
		.ucdname  = "LF",
	},
	{
		.enumname = "EXTEND",
		.file     = FILE_SENTENCE,
		.ucdname  = "Extend",
	},
	{
		.enumname = "SEP",
		.file     = FILE_SENTENCE,
		.ucdname  = "Sep",
	},
	{
		.enumname = "FORMAT",
		.file     = FILE_SENTENCE,
		.ucdname  = "Format",
	},
	{
		.enumname = "SP",
		.file     = FILE_SENTENCE,
		.ucdname  = "Sp",
	},
	{
		.enumname = "LOWER",
		.file     = FILE_SENTENCE,
		.ucdname  = "Lower",
	},
	{
		.enumname = "UPPER",
		.file     = FILE_SENTENCE,
		.ucdname  = "Upper",
	},
	{
		.enumname = "OLETTER",
		.file     = FILE_SENTENCE,
		.ucdname  = "OLetter",
	},
	{
		.enumname = "NUMERIC",
		.file     = FILE_SENTENCE,
		.ucdname  = "Numeric",
	},
	{
		.enumname = "ATERM",
		.file     = FILE_SENTENCE,
		.ucdname  = "ATerm",
	},
	{
		.enumname = "SCONTINUE",
		.file     = FILE_SENTENCE,
		.ucdname  = "SContinue",
	},
	{
		.enumname = "STERM",
		.file     = FILE_SENTENCE,
		.ucdname  = "STerm",
	},
	{
		.enumname = "CLOSE",
		.file     = FILE_SENTENCE,
		.ucdname  = "Close",
	},
};

int
main(int argc, char *argv[])
{
	(void)argc;

	properties_generate_break_property(sentence_break_property,
	                                   LEN(sentence_break_property),
	                                   NULL, NULL, "sentence_break", argv[0]);

	return 0;
}
