/* See LICENSE file for copyright and license details. */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "types.h"

#define LEN(x) (sizeof (x) / sizeof *(x))

struct property_spec {
	const char *enumname;
	const char *file;
	const char *ucdname;
};

struct properties {
	int_least64_t property;
};

struct properties_compressed {
	size_t *offset;
	struct properties *data;
	size_t datalen;
};

struct properties_major_minor {
	size_t *major;
	size_t *minor;
	size_t minorlen;
};

int hextocp(const char *, size_t, uint_least32_t *cp);

void parse_file_with_callback(const char *, int (*callback)(const char *,
                              char **, size_t, char *, void *), void *payload);

void properties_compress(const struct properties *, struct properties_compressed *comp);
double properties_get_major_minor(const struct properties_compressed *,
                                  struct properties_major_minor *);
void properties_print_lookup_table(char *, size_t *, size_t);
void properties_print_derived_lookup_table(char *, char *, size_t *, size_t,
                                      int_least64_t (*get_value)(const struct properties *,
                                      size_t), const void *);

void properties_generate_break_property(const struct property_spec *,
                                        uint_least8_t, uint_least8_t
                                        (*handle_conflict)(uint_least32_t,
                                        uint_least8_t, uint_least8_t),
					uint_least8_t (*post_process)
                                        (uint_least8_t), const char *,
                                        const char *);

void break_test_list_parse(char *, struct break_test **, size_t *);
void break_test_list_print(const struct break_test *, size_t,
                             const char *, const char *);
void break_test_list_free(struct break_test *, size_t);

#endif /* UTIL_H */
