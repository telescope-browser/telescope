/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define FILE_EAW   "data/EastAsianWidth.txt"
#define FILE_EMOJI "data/emoji-data.txt"
#define FILE_LINE  "data/LineBreak.txt"

static const struct property_spec line_break_property[] = {
	{
		.enumname = "AL",
		.file     = FILE_LINE,
		.ucdname  = "AL",
	},
	/*
	 * Both extended pictographic and cn are large classes,
	 * but we are only interested in their intersection for LB30b,
	 * so we have the following two temporary classes. At first
	 * the extpict-class is filled, then the cn-class, which leads
	 * to conflicts (that we handle by putting them in the "proper"
	 * class BOTH_CN_EXTPICT). We make use of the fact that there
	 * is no intersection between AL and Cn.
	 *
	 * Any consecutive conflicts are permitted to overwrite
	 * TMP_EXTENDED_PICTOGRAPHIC and TMP_CN, because we don't need
	 * them, and in the final postprocessing we "reset" all
	 * remaining matches (that then didn't fit any of the other
	 * classes) to the generic class AL.
	 */
	{
		.enumname = "TMP_CN",
		.file     = FILE_LINE,
		.ucdname  = "Cn",
	},
	{
		.enumname = "TMP_EXTENDED_PICTOGRAPHIC",
		.file     = FILE_EMOJI,
		.ucdname  = "Extended_Pictographic",
	},
	/* end of special block */
	{
		.enumname = "B2",
		.file     = FILE_LINE,
		.ucdname  = "B2",
	},
	{
		.enumname = "BA",
		.file     = FILE_LINE,
		.ucdname  = "BA",
	},
	{
		.enumname = "BB",
		.file     = FILE_LINE,
		.ucdname  = "BB",
	},
	{
		.enumname = "BK",
		.file     = FILE_LINE,
		.ucdname  = "BK",
	},
	{
		.enumname = "BOTH_CN_EXTPICT",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "CB",
		.file     = FILE_LINE,
		.ucdname  = "CB",
	},
	{
		.enumname = "CL",
		.file     = FILE_LINE,
		.ucdname  = "CL",
	},
	{
		.enumname = "CM",
		.file     = FILE_LINE,
		.ucdname  = "CM",
	},
	{
		.enumname = "CP_WITHOUT_EAW_HWF",
		.file     = FILE_LINE,
		.ucdname  = "CP",
	},
	{
		.enumname = "CP_WITH_EAW_HWF",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "CR",
		.file     = FILE_LINE,
		.ucdname  = "CR",
	},
	{
		.enumname = "EB",
		.file     = FILE_LINE,
		.ucdname  = "EB",
	},
	{
		.enumname = "EM",
		.file     = FILE_LINE,
		.ucdname  = "EM",
	},
	{
		.enumname = "EX",
		.file     = FILE_LINE,
		.ucdname  = "EX",
	},
	{
		.enumname = "GL",
		.file     = FILE_LINE,
		.ucdname  = "GL",
	},
	{
		.enumname = "H2",
		.file     = FILE_LINE,
		.ucdname  = "H2",
	},
	{
		.enumname = "H3",
		.file     = FILE_LINE,
		.ucdname  = "H3",
	},
	{
		.enumname = "HL",
		.file     = FILE_LINE,
		.ucdname  = "HL",
	},
	{
		.enumname = "HY",
		.file     = FILE_LINE,
		.ucdname  = "HY",
	},
	{
		.enumname = "ID",
		.file     = FILE_LINE,
		.ucdname  = "ID",
	},
	{
		.enumname = "IN",
		.file     = FILE_LINE,
		.ucdname  = "IN",
	},
	{
		.enumname = "IS",
		.file     = FILE_LINE,
		.ucdname  = "IS",
	},
	{
		.enumname = "JL",
		.file     = FILE_LINE,
		.ucdname  = "JL",
	},
	{
		.enumname = "JT",
		.file     = FILE_LINE,
		.ucdname  = "JT",
	},
	{
		.enumname = "JV",
		.file     = FILE_LINE,
		.ucdname  = "JV",
	},
	{
		.enumname = "LF",
		.file     = FILE_LINE,
		.ucdname  = "LF",
	},
	{
		.enumname = "NL",
		.file     = FILE_LINE,
		.ucdname  = "NL",
	},
	{
		.enumname = "NS",
		.file     = FILE_LINE,
		.ucdname  = "NS",
	},
	{
		.enumname = "NU",
		.file     = FILE_LINE,
		.ucdname  = "NU",
	},
	{
		.enumname = "OP_WITHOUT_EAW_HWF",
		.file     = FILE_LINE,
		.ucdname  = "OP",
	},
	{
		.enumname = "OP_WITH_EAW_HWF",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "PO",
		.file     = FILE_LINE,
		.ucdname  = "PO",
	},
	{
		.enumname = "PR",
		.file     = FILE_LINE,
		.ucdname  = "PR",
	},
	{
		.enumname = "QU",
		.file     = FILE_LINE,
		.ucdname  = "QU",
	},
	{
		.enumname = "RI",
		.file     = FILE_LINE,
		.ucdname  = "RI",
	},
	{
		.enumname = "SP",
		.file     = FILE_LINE,
		.ucdname  = "SP",
	},
	{
		.enumname = "SY",
		.file     = FILE_LINE,
		.ucdname  = "SY",
	},
	{
		.enumname = "WJ",
		.file     = FILE_LINE,
		.ucdname  = "WJ",
	},
	{
		.enumname = "ZW",
		.file     = FILE_LINE,
		.ucdname  = "ZW",
	},
	{
		.enumname = "ZWJ",
		.file     = FILE_LINE,
		.ucdname  = "ZWJ",
	},
	{
		.enumname = "TMP_AI",
		.file     = FILE_LINE,
		.ucdname  = "AI",
	},
	{
		.enumname = "TMP_CJ",
		.file     = FILE_LINE,
		.ucdname  = "CJ",
	},
	{
		.enumname = "TMP_XX",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "TMP_MN",
		.file     = FILE_LINE,
		.ucdname  = "Mn",
	},
	{
		.enumname = "TMP_MC",
		.file     = FILE_LINE,
		.ucdname  = "Mc",
	},
	{
		.enumname = "TMP_SA_WITHOUT_MN_OR_MC",
		.file     = FILE_LINE,
		.ucdname  = "SA",
	},
	{
		.enumname = "TMP_SA_WITH_MN_OR_MC",
		.file     = FILE_LINE,
		.ucdname  = "SA",
	},
	{
		.enumname = "TMP_SG",
		.file     = FILE_LINE,
		.ucdname  = "SG",
	},
	{
		.enumname = "TMP_EAW_H",
		.file     = FILE_EAW,
		.ucdname  = "H",
	},
	{
		.enumname = "TMP_EAW_W",
		.file     = FILE_EAW,
		.ucdname  = "W",
	},
	{
		.enumname = "TMP_EAW_F",
		.file     = FILE_EAW,
		.ucdname  = "F",
	},
};

static uint_least8_t
handle_conflict(uint_least32_t cp, uint_least8_t prop1, uint_least8_t prop2)
{
	uint_least8_t result = prop2;
	char *target = NULL;

	(void)cp;

	if ((!strcmp(line_break_property[prop1].enumname, "TMP_EAW_H")  ||
	     !strcmp(line_break_property[prop1].enumname, "TMP_EAW_W")  ||
	     !strcmp(line_break_property[prop1].enumname, "TMP_EAW_F")) ||
	    (!strcmp(line_break_property[prop2].enumname, "TMP_EAW_H") ||
	     !strcmp(line_break_property[prop2].enumname, "TMP_EAW_W") ||
	     !strcmp(line_break_property[prop2].enumname, "TMP_EAW_F"))) {
		if (!strcmp(line_break_property[prop1].enumname, "CP_WITHOUT_EAW_HWF") ||
		    !strcmp(line_break_property[prop2].enumname, "CP_WITHOUT_EAW_HWF")) {
			target = "CP_WITH_EAW_HWF";
		} else if (!strcmp(line_break_property[prop1].enumname, "OP_WITHOUT_EAW_HWF") ||
		    !strcmp(line_break_property[prop2].enumname, "OP_WITHOUT_EAW_HWF")) {
			target = "OP_WITH_EAW_HWF";
		} else {
			/* ignore EAW for the rest */
			if ((!strcmp(line_break_property[prop1].enumname, "TMP_EAW_H") ||
			     !strcmp(line_break_property[prop1].enumname, "TMP_EAW_W") ||
			     !strcmp(line_break_property[prop1].enumname, "TMP_EAW_F"))) {
				result = prop2;
			} else {
				result = prop1;
			}
		}
	} else if ((!strcmp(line_break_property[prop1].enumname, "TMP_MN") ||
	            !strcmp(line_break_property[prop1].enumname, "TMP_MC")) ||
		   (!strcmp(line_break_property[prop2].enumname, "TMP_MN") ||
		    !strcmp(line_break_property[prop2].enumname, "TMP_MC"))) {
		if (!strcmp(line_break_property[prop1].enumname, "SA_WITHOUT_MN_OR_MC") ||
		    !strcmp(line_break_property[prop2].enumname, "SA_WITHOUT_MN_OR_MC")) {
			target = "SA_WITH_MN_OR_MC";
		} else {
			/* ignore Mn and Mc for the rest */
			if ((!strcmp(line_break_property[prop1].enumname, "TMP_MN") ||
			     !strcmp(line_break_property[prop1].enumname, "TMP_MC"))) {
				result = prop2;
			} else {
				result = prop1;
			}
		}
	} else if (!strcmp(line_break_property[prop1].enumname, "TMP_CN") ||
	           !strcmp(line_break_property[prop2].enumname, "TMP_CN")) {
		if (!strcmp(line_break_property[prop1].enumname, "TMP_EXTENDED_PICTOGRAPHIC") ||
		    !strcmp(line_break_property[prop2].enumname, "TMP_EXTENDED_PICTOGRAPHIC")) {
			target = "BOTH_CN_EXTPICT";
		} else {
			/* ignore Cn for all the other properties */
			if (!strcmp(line_break_property[prop1].enumname, "TMP_CN")) {
				result = prop2;
			} else {
				result = prop1;
			}
		}
	} else if (!strcmp(line_break_property[prop1].enumname, "TMP_EXTENDED_PICTOGRAPHIC") ||
	           !strcmp(line_break_property[prop2].enumname, "TMP_EXTENDED_PICTOGRAPHIC")) {
		if (!strcmp(line_break_property[prop1].enumname, "TMP_CN") ||
		    !strcmp(line_break_property[prop2].enumname, "TMP_CN")) {
			target = "BOTH_CN_EXTPICT";
		} else {
			/* ignore Extended_Pictographic for all the other properties */
			if (!strcmp(line_break_property[prop1].enumname, "TMP_EXTENDED_PICTOGRAPHIC")) {
				result = prop2;
			} else {
				result = prop1;
			}
		}
	} else {
		fprintf(stderr, "handle_conflict: Cannot handle conflict %s <- %s.\n",
		        line_break_property[prop1].enumname, line_break_property[prop2].enumname);
		exit(1);
	}

	if (target) {
		for (result = 0; result < LEN(line_break_property); result++) {
			if (!strcmp(line_break_property[result].enumname,
			            target)) {
				break;
			}
		}
		if (result == LEN(line_break_property)) {
			fprintf(stderr, "handle_conflict: Internal error.\n");
			exit(1);
		}
	}

	return result;
}

static uint_least8_t
post_process(uint_least8_t prop)
{
	const char *target = NULL;
	uint_least8_t result;

	/* LB1 */
	if (!strcmp(line_break_property[prop].enumname, "TMP_AI") ||
	    !strcmp(line_break_property[prop].enumname, "TMP_SG") ||
	    !strcmp(line_break_property[prop].enumname, "TMP_XX")) {
		/* map AI, SG and XX to AL */
		target = "AL";
	} else if (!strcmp(line_break_property[prop].enumname, "TMP_SA_WITH_MN_OR_MC")) {
		/* map SA (with General_Category Mn or Mc) to CM */
		target = "CM";
	} else if (!strcmp(line_break_property[prop].enumname, "TMP_SA_WITHOUT_MN_OR_MC")) {
		/* map SA (without General_Category Mn or Mc) to AL */
		target = "AL";
	} else if (!strcmp(line_break_property[prop].enumname, "TMP_CJ")) {
		/* map CJ to NS */
		target = "NS";
	} else if (!strcmp(line_break_property[prop].enumname, "TMP_CN") ||
	           !strcmp(line_break_property[prop].enumname, "TMP_EXTENDED_PICTOGRAPHIC") ||
	           !strcmp(line_break_property[prop].enumname, "TMP_MN") ||
	           !strcmp(line_break_property[prop].enumname, "TMP_MC") ||
	           !strcmp(line_break_property[prop].enumname, "TMP_EAW_H") ||
	           !strcmp(line_break_property[prop].enumname, "TMP_EAW_W") ||
	           !strcmp(line_break_property[prop].enumname, "TMP_EAW_F")) {
		/* map all the temporary classes "residue" to AL */
		target = "AL";
	}

	if (target) {
		for (result = 0; result < LEN(line_break_property); result++) {
			if (!strcmp(line_break_property[result].enumname,
			            target)) {
				break;
			}
		}
		if (result == LEN(line_break_property)) {
			fprintf(stderr, "handle_conflict: Internal error.\n");
			exit(1);
		}

		return result;
	} else {
		return prop;
	}
}

int
main(int argc, char *argv[])
{
	(void)argc;

	properties_generate_break_property(line_break_property,
	                                   LEN(line_break_property),
	                                   handle_conflict, post_process,
	                                   "line_break", argv[0]);

	return 0;
}
