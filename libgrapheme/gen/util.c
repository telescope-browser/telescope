/* See LICENSE file for copyright and license details. */
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

struct range {
	uint_least32_t lower;
	uint_least32_t upper;
};

struct properties_payload {
	struct properties *prop;
	const struct property_spec *spec;
	uint_least8_t speclen;
	int (*set_value)(struct properties_payload *, uint_least32_t, int_least64_t);
	uint_least8_t (*handle_conflict)(uint_least32_t, uint_least8_t, uint_least8_t);
};

struct break_test_payload
{
	struct break_test **test;
	size_t *testlen;
};

static void *
reallocate_array(void *p, size_t len, size_t size)
{
	if (len > 0 && size > SIZE_MAX / len) {
		errno = ENOMEM;
		return NULL;
	}

	return realloc(p, len * size);
}

int
hextocp(const char *str, size_t len, uint_least32_t *cp)
{
	size_t i;
	int off;
	char relative;

	/* the maximum valid codepoint is 0x10FFFF */
	if (len > 6) {
		fprintf(stderr, "hextocp: '%.*s' is too long.\n",
		        (int)len, str);
		return 1;
	}

	for (i = 0, *cp = 0; i < len; i++) {
		if (str[i] >= '0' && str[i] <= '9') {
			relative = '0';
			off = 0;
		} else if (str[i] >= 'a' && str[i] <= 'f') {
			relative = 'a';
			off = 10;
		} else if (str[i] >= 'A' && str[i] <= 'F') {
			relative = 'A';
			off = 10;
		} else {
			fprintf(stderr, "hextocp: '%.*s' is not hexadecimal.\n",
			        (int)len, str);
			return 1;
		}

		*cp += ((uint_least32_t)1 << (4 * (len - i - 1))) *
		       (uint_least32_t)(str[i] - relative + off);
	}

	if (*cp > UINT32_C(0x10FFFF)) {
		fprintf(stderr, "hextocp: '%.*s' is too large.\n",
		        (int)len, str);
		return 1;
	}

	return 0;
}

static int
range_parse(const char *str, struct range *range)
{
	char *p;

	if ((p = strstr(str, "..")) == NULL) {
		/* input has the form "XXXXXX" */
		if (hextocp(str, strlen(str), &range->lower)) {
			return 1;
		}
		range->upper = range->lower;
	} else {
		/* input has the form "XXXXXX..XXXXXX" */
		if (hextocp(str, (size_t)(p - str), &range->lower) ||
		    hextocp(p + 2, strlen(p + 2), &range->upper)) {
			return 1;
		}
	}

	return 0;
}

void
parse_file_with_callback(const char *fname, int (*callback)(const char *,
                         char **, size_t, char *, void *), void *payload)
{
	FILE *fp;
	char *line = NULL, **field = NULL, *comment;
	size_t linebufsize = 0, i, fieldbufsize = 0, j, nfields;
	ssize_t len;

	/* open file */
	if (!(fp = fopen(fname, "r"))) {
		fprintf(stderr, "parse_file_with_callback: fopen '%s': %s.\n",
		        fname, strerror(errno));
		exit(1);
	}

	while ((len = getline(&line, &linebufsize, fp)) >= 0) {
		/* remove trailing newline */
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
			len--;
		}

		/* skip empty lines and comment lines */
		if (len == 0 || line[0] == '#') {
			continue;
		}

		/* tokenize line into fields */
		for (i = 0, nfields = 0, comment = NULL; i < (size_t)len; i++) {
			/* skip leading whitespace */
			while (line[i] == ' ') {
				i++;
			}

			/* check if we crashed into the comment */
			if (line[i] != '#') {
				/* extend field buffer, if necessary */
				if (++nfields > fieldbufsize) {
					if ((field = realloc(field, nfields *
					      sizeof(*field))) == NULL) {
						fprintf(stderr, "parse_file_with_"
						        "callback: realloc: %s.\n",
						        strerror(errno));
						exit(1);
					}
					fieldbufsize = nfields;
				}

				/* set current position as field start */
				field[nfields - 1] = &line[i];

				/* continue until we reach ';' or '#' or end */
				while (line[i] != ';' && line[i] != '#' &&
				       line[i] != '\0') {
					i++;
				}
			}

			if (line[i] == '#') {
				/* set comment-variable for later */
				comment = &line[i + 1];
			}

			/* go back whitespace and terminate field there */
			if (i > 0) {
				for (j = i - 1; line[j] == ' '; j--)
					;
				line[j + 1] = '\0';
			} else {
				line[i] = '\0';
			}

			/* if comment is set, we are done */
			if (comment != NULL) {
				break;
			}
		}

		/* skip leading whitespace in comment */
		while (comment != NULL && comment[0] == ' ') {
			comment++;
		}

		/* call callback function */
		if (callback(fname, field, nfields, comment, payload)) {
			fprintf(stderr, "parse_file_with_callback: "
			        "Malformed input.\n");
			exit(1);
		}
	}

	free(line);
	free(field);
}

static int
properties_callback(const char *file, char **field, size_t nfields,
                    char *comment, void *payload)
{
	/* prop always has the length 0x110000 */
	struct properties_payload *p = (struct properties_payload *)payload;
	struct range r;
	uint_least8_t i;
	uint_least32_t cp;

	(void)comment;

	if (nfields < 2) {
		return 1;
	}

	for (i = 0; i < p->speclen; i++) {
		/* identify fitting file and identifier */
		if (p->spec[i].file &&
		    !strcmp(p->spec[i].file, file) &&
		    (!strcmp(p->spec[i].ucdname, field[1]) ||
		     (comment != NULL && !strncmp(p->spec[i].ucdname, comment, strlen(p->spec[i].ucdname)) &&
		      comment[strlen(p->spec[i].ucdname)] == ' '))) {
			/* parse range in first field */
			if (range_parse(field[0], &r)) {
				return 1;
			}

			/* apply to all codepoints in the range */
			for (cp = r.lower; cp <= r.upper; cp++) {
				if (p->set_value(payload, cp, i)) {
					exit(1);
				}
			}
			break;
		}
	}

	return 0;
}

void
properties_compress(const struct properties *prop,
                    struct properties_compressed *comp)
{
	uint_least32_t cp, i;

	/* initialization */
	if (!(comp->offset = malloc((size_t)UINT32_C(0x110000) * sizeof(*(comp->offset))))) {
		fprintf(stderr, "malloc: %s\n", strerror(errno));
		exit(1);
	}
	comp->data = NULL;
	comp->datalen = 0;

	for (cp = 0; cp < UINT32_C(0x110000); cp++) {
		for (i = 0; i < comp->datalen; i++) {
			if (!memcmp(&(prop[cp]), &(comp->data[i]), sizeof(*prop))) {
				/* found a match! */
				comp->offset[cp] = i;
				break;
			}
		}
		if (i == comp->datalen) {
			/*
			 * found no matching properties-struct, so
			 * add current properties to data and add the
			 * offset in the offset-table
			 */
			if (!(comp->data = reallocate_array(comp->data,
			                                    ++(comp->datalen),
			                                    sizeof(*(comp->data))))) {
				fprintf(stderr, "reallocate_array: %s\n",
				        strerror(errno));
				exit(1);
			}
			memcpy(&(comp->data[comp->datalen - 1]), &(prop[cp]),
			       sizeof(*prop));
			comp->offset[cp] = comp->datalen - 1;
		}
	}
}

double
properties_get_major_minor(const struct properties_compressed *comp,
                           struct properties_major_minor *mm)
{
	size_t i, j, compression_count = 0;

	/*
	 * we currently have an array comp->offset which maps the
	 * codepoints 0..0x110000 to offsets into comp->data.
	 * To improve cache-locality instead and allow a bit of
	 * compressing, instead of directly mapping a codepoint
	 * 0xAAAABB with comp->offset, we generate two arrays major
	 * and minor such that
	 *    comp->offset(0xAAAABB) == minor[major[0xAAAA] + 0xBB]
	 * This yields a major-array of length 2^16 and a minor array
	 * of variable length depending on how many common subsequences
	 * can be filtered out.
	 */

	/* initialize */
	if (!(mm->major = malloc((size_t)0x1100 * sizeof(*(mm->major))))) {
		fprintf(stderr, "malloc: %s\n", strerror(errno));
		exit(1);
	}
	mm->minor = NULL;
	mm->minorlen = 0;

	for (i = 0; i < (size_t)0x1100; i++) {
		/*
		 * we now look at the cp-range (i << 8)..(i << 8 + 0xFF)
		 * and check if its corresponding offset-data already
		 * exists in minor (because then we just point there
		 * and need less storage)
		 */
		for (j = 0; j + 0xFF < mm->minorlen; j++) {
			if (!memcmp(&(comp->offset[i << 8]),
			            &(mm->minor[j]),
			            sizeof(*(comp->offset)) * 0x100)) {
				break;
			}
		}
		if (j + 0xFF < mm->minorlen) {
			/* found an index */
			compression_count++;
			mm->major[i] = j;
		} else {
			/*
			 * add "new" sequence to minor and point to it
			 * in major
			 */
			mm->minorlen += 0x100;
			if (!(mm->minor = reallocate_array(mm->minor,
			                                   mm->minorlen,
			                                   sizeof(*(mm->minor))))) {
				fprintf(stderr, "reallocate_array: %s\n",
				        strerror(errno));
				exit(1);
			}
			memcpy(&(mm->minor[mm->minorlen - 0x100]),
			       &(comp->offset[i << 8]),
			       sizeof(*(mm->minor)) * 0x100);
			mm->major[i] = mm->minorlen - 0x100;
		}
	}

	/* return compression ratio */
	return (double)compression_count / 0x1100 * 100;
}

void
properties_print_lookup_table(char *name, size_t *data, size_t datalen)
{
	char *type;
	size_t i, maxval;

	for (i = 0, maxval = 0; i < datalen; i++) {
		if (data[i] > maxval) {
			maxval = data[i];
		}
	}

	type = (maxval <= UINT_LEAST8_MAX)  ? "uint_least8_t"  :
	       (maxval <= UINT_LEAST16_MAX) ? "uint_least16_t" :
	       (maxval <= UINT_LEAST32_MAX) ? "uint_least32_t" :
	                                      "uint_least64_t";

	printf("static const %s %s[] = {\n\t", type, name);
	for (i = 0; i < datalen; i++) {
		printf("%zu", data[i]);
		if (i + 1 == datalen) {
			printf("\n");
		} else if ((i + 1) % 8 != 0) {
			printf(", ");
		} else {
			printf(",\n\t");
		}

	}
	printf("};\n");
}

void
properties_print_derived_lookup_table(char *name, char *type, size_t *offset, size_t offsetlen,
                                      int_least64_t (*get_value)(const struct properties *,
                                      size_t), const void *payload)
{
	size_t i;

	printf("static const %s %s[] = {\n\t", type, name);
	for (i = 0; i < offsetlen; i++) {
		printf("%"PRIiLEAST64, get_value(payload, offset[i]));
		if (i + 1 == offsetlen) {
			printf("\n");
		} else if ((i + 1) % 8 != 0) {
			printf(", ");
		} else {
			printf(",\n\t");
		}

	}
	printf("};\n");
}

static void
properties_print_enum(const struct property_spec *spec, size_t speclen,
                      const char *enumname, const char *enumprefix)
{
	size_t i;

	printf("enum %s {\n", enumname);
	for (i = 0; i < speclen; i++) {
		printf("\t%s_%s,\n", enumprefix, spec[i].enumname);
	}
	printf("\tNUM_%sS,\n};\n\n", enumprefix);
}

static int
set_value_bp(struct properties_payload *payload, uint_least32_t cp,
             int_least64_t value)
{
	if (payload->prop[cp].property != 0) {
		if (payload->handle_conflict == NULL) {
			fprintf(stderr, "set_value_bp: "
	                        "Unhandled character break property "
			        "overwrite for 0x%06X (%s <- %s).\n",
		                cp, payload->spec[payload->prop[cp].
			        property].enumname,
			        payload->spec[value].enumname);
			return 1;
		} else {
			value = payload->handle_conflict(cp,
			        (uint_least8_t)payload->prop[cp].property,
			        (uint_least8_t)value);
		}
	}
	payload->prop[cp].property = value;

	return 0;
}

static int_least64_t
get_value_bp(const struct properties *prop, size_t offset)
{
	return (uint_least8_t)prop[offset].property;
}

void
properties_generate_break_property(const struct property_spec *spec,
                                   uint_least8_t speclen,
                                   uint_least8_t (*handle_conflict)(
                                   uint_least32_t, uint_least8_t,
                                   uint_least8_t), uint_least8_t
                                   (*post_process)(uint_least8_t),
                                   const char *prefix, const char *argv0)
{
	struct properties_compressed comp;
	struct properties_major_minor mm;
	struct properties_payload payload;
	struct properties *prop;
	size_t i, j, prefixlen = strlen(prefix);
	char buf1[64], prefix_uc[64], buf2[64], buf3[64], buf4[64];

	/* allocate property buffer for all 0x110000 codepoints */
	if (!(prop = calloc(UINT32_C(0x110000), sizeof(*prop)))) {
		fprintf(stderr, "calloc: %s\n", strerror(errno));
		exit(1);
	}

	/* generate data */
	payload.prop = prop;
	payload.spec = spec;
	payload.speclen = speclen;
	payload.set_value = set_value_bp;
	payload.handle_conflict = handle_conflict;

	/* parse each file exactly once and ignore NULL-fields */
	for (i = 0; i < speclen; i++) {
		for (j = 0; j < i; j++) {
			if (spec[i].file && spec[j].file &&
			    !strcmp(spec[i].file, spec[j].file)) {
				/* file has already been parsed */
				break;
			}
		}
		if (i == j && spec[i].file) {
			/* file has not been processed yet */
			parse_file_with_callback(spec[i].file,
			                         properties_callback,
			                         &payload);
		}
	}

	/* post-processing */
	if (post_process != NULL) {
		for (i = 0; i < UINT32_C(0x110000); i++) {
			payload.prop[i].property =
				post_process((uint_least8_t)payload.prop[i].property);
		}
	}

	/* compress data */
	printf("/* Automatically generated by %s */\n#include <stdint.h>\n\n", argv0);
	properties_compress(prop, &comp);

	fprintf(stderr, "%s: %s-LUT compression-ratio: %.2f%%\n", argv0,
	        prefix, properties_get_major_minor(&comp, &mm));

	/* prepare names */
	if ((size_t)snprintf(buf1, LEN(buf1), "%s_property", prefix) >= LEN(buf1)) {
		fprintf(stderr, "snprintf: String truncated.\n");
		exit(1);
	}
	if (LEN(prefix_uc) + 1 < prefixlen) {
		fprintf(stderr, "snprintf: Buffer too small.\n");
		exit(1);
	}
	for (i = 0; i < prefixlen; i++) {
		prefix_uc[i] = (char)toupper(prefix[i]);
	}
	prefix_uc[prefixlen] = '\0';
	if ((size_t)snprintf(buf2, LEN(buf2), "%s_PROP", prefix_uc) >= LEN(buf2) ||
	    (size_t)snprintf(buf3, LEN(buf3), "%s_major", prefix) >= LEN(buf3)   ||
	    (size_t)snprintf(buf4, LEN(buf4), "%s_minor", prefix) >= LEN(buf4)) {
		fprintf(stderr, "snprintf: String truncated.\n");
		exit(1);
	}

	/* print data */
	properties_print_enum(spec, speclen, buf1, buf2);
	properties_print_lookup_table(buf3, mm.major, 0x1100);
	printf("\n");
	properties_print_derived_lookup_table(buf4, "uint_least8_t", mm.minor, mm.minorlen,
	                                      get_value_bp, comp.data);

	/* free data */
	free(prop);
	free(comp.data);
	free(comp.offset);
	free(mm.major);
	free(mm.minor);
}

static int
break_test_callback(const char *fname, char **field, size_t nfields,
                      char *comment, void *payload)
{
	struct break_test *t,
		**test = ((struct break_test_payload *)payload)->test;
	size_t i, *testlen = ((struct break_test_payload *)payload)->testlen;
	char *token;

	(void)fname;

	if (nfields < 1) {
		return 1;
	}

	/* append new testcase and initialize with zeroes */
	if ((*test = realloc(*test, ++(*testlen) * sizeof(**test))) == NULL) {
		fprintf(stderr, "break_test_callback: realloc: %s.\n",
		        strerror(errno));
		return 1;
	}
	t = &(*test)[*testlen - 1];
	memset(t, 0, sizeof(*t));

	/* parse testcase "<÷|×> <cp> <÷|×> ... <cp> <÷|×>" */
	for (token = strtok(field[0], " "), i = 0; token != NULL; i++,
	     token = strtok(NULL, " ")) {
		if (i % 2 == 0) {
			/* delimiter or start of sequence */
			if (i == 0 || !strncmp(token, "\xC3\xB7", 2)) { /* UTF-8 */
				/*
				 * '÷' indicates a breakpoint,
				 * the current length is done; allocate
				 * a new length field and set it to 0
				 */
				if ((t->len = realloc(t->len,
				     ++t->lenlen * sizeof(*t->len))) == NULL) {
					fprintf(stderr, "break_test_"
					        "callback: realloc: %s.\n",
					        strerror(errno));
					return 1;
				}
				t->len[t->lenlen - 1] = 0;
			} else if (!strncmp(token, "\xC3\x97", 2)) { /* UTF-8 */
				/*
				 * '×' indicates a non-breakpoint, do nothing
				 */
			} else {
				fprintf(stderr, "break_test_callback: "
				        "Malformed delimiter '%s'.\n", token);
				return 1;
			}
		} else {
			/* add codepoint to cp-array */
			if ((t->cp = realloc(t->cp, ++t->cplen *
			                     sizeof(*t->cp))) == NULL) {
				fprintf(stderr, "break_test_callback: "
				        "realloc: %s.\n", strerror(errno));
				return 1;
			}
			if (hextocp(token, strlen(token), &t->cp[t->cplen - 1])) {
				return 1;
			}
			if (t->lenlen > 0) {
				t->len[t->lenlen - 1]++;
			}
		}
	}
	if (t->len[t->lenlen - 1] == 0) {
		/*
		 * we allocated one more length than we needed because
		 * the breakpoint was at the end
		 */
		t->lenlen--;
	}

	/* store comment */
	if (((*test)[*testlen - 1].descr = strdup(comment)) == NULL) {
		fprintf(stderr, "break_test_callback: strdup: %s.\n",
		        strerror(errno));
		return 1;
	}

	return 0;
}

void
break_test_list_parse(char *fname, struct break_test **test,
                        size_t *testlen)
{
	struct break_test_payload pl = {
		.test = test,
		.testlen = testlen,
	};
	*test = NULL;
	*testlen = 0;

	parse_file_with_callback(fname, break_test_callback, &pl);
}

void
break_test_list_print(const struct break_test *test, size_t testlen,
                        const char *identifier, const char *progname)
{
	size_t i, j;

	printf("/* Automatically generated by %s */\n"
	       "#include <stdint.h>\n#include <stddef.h>\n\n"
	       "#include \"../gen/types.h\"\n\n", progname);

	printf("static const struct break_test %s[] = {\n", identifier);
	for (i = 0; i < testlen; i++) {
		printf("\t{\n");

		printf("\t\t.cp     = (uint_least32_t[]){");
		for (j = 0; j < test[i].cplen; j++) {
			printf(" UINT32_C(0x%06X)", test[i].cp[j]);
			if (j + 1 < test[i].cplen) {
				putchar(',');
			}
		}
		printf(" },\n");
		printf("\t\t.cplen  = %zu,\n", test[i].cplen);

		printf("\t\t.len    = (size_t[]){");
		for (j = 0; j < test[i].lenlen; j++) {
			printf(" %zu", test[i].len[j]);
			if (j + 1 < test[i].lenlen) {
				putchar(',');
			}
		}
		printf(" },\n");
		printf("\t\t.lenlen = %zu,\n", test[i].lenlen);

		printf("\t\t.descr  = \"%s\",\n", test[i].descr);

		printf("\t},\n");
	}
	printf("};\n");
}

void
break_test_list_free(struct break_test *test, size_t testlen)
{
	size_t i;

	for (i = 0; i < testlen; i++) {
		free(test[i].cp);
		free(test[i].len);
		free(test[i].descr);
	}

	free(test);
}
