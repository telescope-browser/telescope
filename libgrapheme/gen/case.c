/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define FILE_DCP "data/DerivedCoreProperties.txt"

static const struct property_spec case_property[] = {
	{
		.enumname = "OTHER",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{
		.enumname = "BOTH_CASED_CASE_IGNORABLE",
		.file     = NULL,
		.ucdname  = NULL,
	},
	{	
		.enumname = "CASED",
		.file     = FILE_DCP,
		.ucdname  = "Cased",
	},
	{
		.enumname = "CASE_IGNORABLE",
		.file     = FILE_DCP,
		.ucdname  = "Case_Ignorable",
	},
	{
		.enumname = "UNCASED",
		.file     = FILE_DCP,
		.ucdname  = "Uncased",
	},
};

static uint_least8_t
handle_conflict(uint_least32_t cp, uint_least8_t prop1, uint_least8_t prop2)
{
	uint_least8_t result;

	(void)cp;

	if ((!strcmp(case_property[prop1].enumname, "CASED") &&
	     !strcmp(case_property[prop2].enumname, "CASE_IGNORABLE")) ||
	    (!strcmp(case_property[prop1].enumname, "CASE_IGNORABLE") &&
	     !strcmp(case_property[prop2].enumname, "CASED"))) {
		for (result = 0; result < LEN(case_property); result++) {
			if (!strcmp(case_property[result].enumname,
			            "BOTH_CASED_CASE_IGNORABLE")) {
				break;
			}
		}
		if (result == LEN(case_property)) {
			fprintf(stderr, "handle_conflict: Internal error.\n");
			exit(1);
		}
	} else {
		fprintf(stderr, "handle_conflict: Cannot handle conflict.\n");
		exit(1);
	}

	return result;
}

static struct properties *prop_upper = NULL, *prop_lower, *prop_title;
static struct special_case {
	struct {
		uint_least32_t *cp;
		size_t cplen;
	} upper, lower, title;
} *sc = NULL;
static size_t sclen = 0;

static int
unicodedata_callback(const char *file, char **field, size_t nfields,
                     char *comment, void *payload)
{
	uint_least32_t cp, upper, lower, title;

	(void)file;
	(void)comment;
	(void)payload;

	hextocp(field[0], strlen(field[0]), &cp);

	upper = lower = title = cp;

	if ((strlen(field[12]) > 0 && hextocp(field[12], strlen(field[12]), &upper))                 ||
	    (strlen(field[13]) > 0 && hextocp(field[13], strlen(field[13]), &lower))                 ||
	    (nfields >= 15 && strlen(field[14]) > 0 && hextocp(field[14], strlen(field[14]), &title))) {
		return 1;
	}

	prop_upper[cp].property = (int_least32_t)upper - (int_least32_t)cp;
	prop_lower[cp].property = (int_least32_t)lower - (int_least32_t)cp;
	prop_title[cp].property = (int_least32_t)title - (int_least32_t)cp;

	return 0;
}

static int
parse_cp_list(const char *str, uint_least32_t **cp, size_t *cplen)
{
	size_t count, i;
	const char *tmp1 = NULL, *tmp2 = NULL;

	/* count the number of spaces in the string and infer list length */
	for (count = 1, tmp1 = str; (tmp2 = strchr(tmp1, ' ')) != NULL; count++, tmp1 = tmp2 + 1)
		;

	/* allocate resources */
	if (!(*cp = calloc((*cplen = count), sizeof(**cp)))) {
		fprintf(stderr, "calloc: %s\n", strerror(errno));
		exit(1);
	}

	/* go through the string again, parsing the numbers */
	for (i = 0, tmp1 = tmp2 = str; tmp2 != NULL; i++) {
		tmp2 = strchr(tmp1, ' ');
		if (hextocp(tmp1, tmp2 ? (size_t)(tmp2 - tmp1) : strlen(tmp1), &((*cp)[i]))) {
			return 1;
		}
		if (tmp2 != NULL) {
			tmp1 = tmp2 + 1;
		}
	}

	return 0;
}

static int
specialcasing_callback(const char *file, char **field, size_t nfields,
                       char *comment, void *payload)
{
	uint_least32_t cp;

	(void)file;
	(void)comment;
	(void)payload;

	if (nfields > 4 && strlen(field[4]) > 0) {
		/*
		 * we have more than 4 fields, i.e. the rule has a
		 * condition (language-sensitive, etc.) and is discarded
		 */
		return 0;
	}

	/* parse affected codepoint */
	hextocp(field[0], strlen(field[0]), &cp);

	/* extend special case array */
	if (!(sc = realloc(sc, (++sclen) * sizeof(*sc)))) {
		fprintf(stderr, "realloc: %s\n", strerror(errno));
		exit(1);	
	}

	/* parse field data */
	parse_cp_list(field[3], &(sc[sclen - 1].upper.cp),
	              &(sc[sclen - 1].upper.cplen));
	parse_cp_list(field[1], &(sc[sclen - 1].lower.cp),
	              &(sc[sclen - 1].lower.cplen));
	parse_cp_list(field[2], &(sc[sclen - 1].title.cp),
	              &(sc[sclen - 1].title.cplen));

	/*
	 * overwrite value in "single mapping" property table by the
	 * special value 0x110000 + (offset in special case array),
	 * even if the special case has length 1
	 */
	prop_upper[cp].property = (int_least64_t)(UINT32_C(0x110000) + (sclen - 1));
	prop_lower[cp].property = (int_least64_t)(UINT32_C(0x110000) + (sclen - 1));
	prop_title[cp].property = (int_least64_t)(UINT32_C(0x110000) + (sclen - 1));

	return 0;
}

static int_least64_t
get_value(const struct properties *prop, size_t offset)
{
	return prop[offset].property;
}

int
main(int argc, char *argv[])
{
	struct properties_compressed comp_upper, comp_lower, comp_title;
	struct properties_major_minor mm_upper, mm_lower, mm_title;
	size_t i, j;

	(void)argc;

	/* generate case property table from the specification */
	properties_generate_break_property(case_property,
	                                   LEN(case_property),
	                                   handle_conflict, NULL, "case",
	                                   argv[0]);

	/*
	 * allocate property buffers for all 0x110000 codepoints
	 *
	 * the buffers contain the offset from the "base" character
	 * to the respective case mapping. By callocing we set all fields
	 * to zero, which is also the Unicode "default" in the sense that
	 * there is no case mapping by default (unless we fill it in)
	 */
	if (!(prop_upper = calloc(UINT32_C(0x110000), sizeof(*prop_upper))) ||
	    !(prop_lower = calloc(UINT32_C(0x110000), sizeof(*prop_lower))) ||
	    !(prop_title = calloc(UINT32_C(0x110000), sizeof(*prop_title)))) {
		fprintf(stderr, "calloc: %s\n", strerror(errno));
		exit(1);
	}
	parse_file_with_callback("data/UnicodeData.txt", unicodedata_callback,
	                         NULL);
	parse_file_with_callback("data/SpecialCasing.txt", specialcasing_callback,
	                         NULL);

	/* compress properties */
	properties_compress(prop_upper, &comp_upper);
	properties_compress(prop_lower, &comp_lower);
	properties_compress(prop_title, &comp_title);

	fprintf(stderr, "%s: LUT compression-ratios: upper=%.2f%%, lower=%.2f%%, title=%.2f%%\n",
	        argv[0], properties_get_major_minor(&comp_upper, &mm_upper),
	        properties_get_major_minor(&comp_lower, &mm_lower),
	        properties_get_major_minor(&comp_title, &mm_title));

	/* print tables */
	printf("/* Automatically generated by %s */\n#include <stdint.h>\n#include <stddef.h>\n\n", argv[0]);

	printf("struct special_case {\n\tuint_least32_t *cp;\n\tsize_t cplen;\n};\n\n");

	properties_print_lookup_table("upper_major", mm_upper.major, 0x1100);
	printf("\n");
	properties_print_derived_lookup_table("upper_minor", "int_least32_t", mm_upper.minor,
	                                      mm_upper.minorlen, get_value, comp_upper.data);
	printf("\n");
	properties_print_lookup_table("lower_major", mm_lower.major, 0x1100);
	printf("\n");
	properties_print_derived_lookup_table("lower_minor", "int_least32_t", mm_lower.minor,
	                                      mm_lower.minorlen, get_value, comp_lower.data);
	printf("\n");
	properties_print_lookup_table("title_major", mm_title.major, 0x1100);
	printf("\n");
	properties_print_derived_lookup_table("title_minor", "int_least32_t", mm_title.minor,
	                                      mm_title.minorlen, get_value, comp_title.data);
	printf("\n");

	printf("static const struct special_case upper_special[] = {\n");
	for (i = 0; i < sclen; i++) {
		printf("\t{\n");

		printf("\t\t.cp     = (uint_least32_t[]){");
		for (j = 0; j < sc[i].upper.cplen; j++) {
			printf(" UINT32_C(0x%06X)", sc[i].upper.cp[j]);
			if (j + 1 < sc[i].upper.cplen) {
				putchar(',');
			}
		}
		printf(" },\n");
		printf("\t\t.cplen  = %zu,\n", sc[i].upper.cplen);
		printf("\t},\n");
	}
	printf("};\n\n");

	printf("static const struct special_case lower_special[] = {\n");
	for (i = 0; i < sclen; i++) {
		printf("\t{\n");

		printf("\t\t.cp     = (uint_least32_t[]){");
		for (j = 0; j < sc[i].lower.cplen; j++) {
			printf(" UINT32_C(0x%06X)", sc[i].lower.cp[j]);
			if (j + 1 < sc[i].lower.cplen) {
				putchar(',');
			}
		}
		printf(" },\n");
		printf("\t\t.cplen  = %zu,\n", sc[i].lower.cplen);
		printf("\t},\n");
	}
	printf("};\n\n");

	printf("static const struct special_case title_special[] = {\n");
	for (i = 0; i < sclen; i++) {
		printf("\t{\n");

		printf("\t\t.cp     = (uint_least32_t[]){");
		for (j = 0; j < sc[i].title.cplen; j++) {
			printf(" UINT32_C(0x%06X)", sc[i].title.cp[j]);
			if (j + 1 < sc[i].title.cplen) {
				putchar(',');
			}
		}
		printf(" },\n");
		printf("\t\t.cplen  = %zu,\n", sc[i].title.cplen);
		printf("\t},\n");
	}
	printf("};\n\n");

	free(comp_lower.data);
	free(comp_lower.offset);
	free(comp_title.data);
	free(comp_title.offset);
	free(comp_upper.data);
	free(comp_upper.offset);
	free(mm_lower.major);
	free(mm_lower.minor);
	free(mm_title.major);
	free(mm_title.minor);
	free(mm_upper.major);
	free(mm_upper.minor);

	return 0;
}
