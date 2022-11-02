/* See LICENSE file for copyright and license details. */
#ifndef GRAPHEME_H
#define GRAPHEME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GRAPHEME_INVALID_CODEPOINT UINT32_C(0xFFFD)

size_t grapheme_decode_utf8(const char *, size_t, uint_least32_t *);
size_t grapheme_encode_utf8(uint_least32_t, char *, size_t);

bool grapheme_is_character_break(uint_least32_t, uint_least32_t, uint_least16_t *);

bool grapheme_is_lowercase(const uint_least32_t *, size_t, size_t *);
bool grapheme_is_titlecase(const uint_least32_t *, size_t, size_t *);
bool grapheme_is_uppercase(const uint_least32_t *, size_t, size_t *);

bool grapheme_is_lowercase_utf8(const char *, size_t, size_t *);
bool grapheme_is_titlecase_utf8(const char *, size_t, size_t *);
bool grapheme_is_uppercase_utf8(const char *, size_t, size_t *);

size_t grapheme_next_character_break(const uint_least32_t *, size_t);
size_t grapheme_next_line_break(const uint_least32_t *, size_t);
size_t grapheme_next_sentence_break(const uint_least32_t *, size_t);
size_t grapheme_next_word_break(const uint_least32_t *, size_t);

size_t grapheme_next_character_break_utf8(const char *, size_t);
size_t grapheme_next_line_break_utf8(const char *, size_t);
size_t grapheme_next_sentence_break_utf8(const char *, size_t);
size_t grapheme_next_word_break_utf8(const char *, size_t);

size_t grapheme_to_lowercase(const uint_least32_t *, size_t, uint_least32_t *, size_t);
size_t grapheme_to_titlecase(const uint_least32_t *, size_t, uint_least32_t *, size_t);
size_t grapheme_to_uppercase(const uint_least32_t *, size_t, uint_least32_t *, size_t);

size_t grapheme_to_lowercase_utf8(const char *, size_t, char *, size_t);
size_t grapheme_to_titlecase_utf8(const char *, size_t, char *, size_t);
size_t grapheme_to_uppercase_utf8(const char *, size_t, char *, size_t);

#endif /* GRAPHEME_H */
