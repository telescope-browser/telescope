/* See LICENSE file for copyright and license details. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../grapheme.h"
#include "util.h"

static const struct {
	char          *arr;     /* UTF-8 byte sequence */
	size_t         len;     /* length of UTF-8 byte sequence */
	size_t         exp_len; /* expected length returned */
	uint_least32_t exp_cp;  /* expected codepoint returned */
} dec_test[] = {
	{
		/* empty sequence
		 * [ ] ->
		 * INVALID
		 */
		.arr     = NULL,
		.len     = 0,
		.exp_len = 0,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid lead byte
		 * [ 11111101 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xFD },
		.len     = 1,
		.exp_len = 1,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* valid 1-byte sequence
		 * [ 00000001 ] ->
		 * 0000001
		 */
		.arr     = (char *)(unsigned char[]){ 0x01 },
		.len     = 1,
		.exp_len = 1,
		.exp_cp  = 0x1,
	},
	{
		/* valid 2-byte sequence
		 * [ 11000011 10111111 ] ->
		 * 00011111111
		 */
		.arr     = (char *)(unsigned char[]){ 0xC3, 0xBF },
		.len     = 2,
		.exp_len = 2,
		.exp_cp  = 0xFF,
	},
	{
		/* invalid 2-byte sequence (second byte missing)
		 * [ 11000011 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xC3 },
		.len     = 1,
		.exp_len = 2,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 2-byte sequence (second byte malformed)
		 * [ 11000011 11111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xC3, 0xFF },
		.len     = 2,
		.exp_len = 1,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 2-byte sequence (overlong encoded)
		 * [ 11000001 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xC1, 0xBF },
		.len     = 2,
		.exp_len = 2,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* valid 3-byte sequence
		 * [ 11100000 10111111 10111111 ] ->
		 * 0000111111111111
		 */
		.arr     = (char *)(unsigned char[]){ 0xE0, 0xBF, 0xBF },
		.len     = 3,
		.exp_len = 3,
		.exp_cp  = 0xFFF,
	},
	{
		/* invalid 3-byte sequence (second byte missing)
		 * [ 11100000 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xE0 },
		.len     = 1,
		.exp_len = 3,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 3-byte sequence (second byte malformed)
		 * [ 11100000 01111111 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xE0, 0x7F, 0xBF },
		.len     = 3,
		.exp_len = 1,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 3-byte sequence (short string, second byte malformed)
		 * [ 11100000 01111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xE0, 0x7F },
		.len     = 2,
		.exp_len = 1,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 3-byte sequence (third byte missing)
		 * [ 11100000 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xE0, 0xBF },
		.len     = 2,
		.exp_len = 3,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 3-byte sequence (third byte malformed)
		 * [ 11100000 10111111 01111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xE0, 0xBF, 0x7F },
		.len     = 3,
		.exp_len = 2,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 3-byte sequence (overlong encoded)
		 * [ 11100000 10011111 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xE0, 0x9F, 0xBF },
		.len     = 3,
		.exp_len = 3,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 3-byte sequence (UTF-16 surrogate half)
		 * [ 11101101 10100000 10000000 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xED, 0xA0, 0x80 },
		.len     = 3,
		.exp_len = 3,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* valid 4-byte sequence
		 * [ 11110011 10111111 10111111 10111111 ] ->
		 * 011111111111111111111
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0xBF, 0xBF, 0xBF },
		.len     = 4,
		.exp_len = 4,
		.exp_cp  = UINT32_C(0xFFFFF),
	},
	{
		/* invalid 4-byte sequence (second byte missing)
		 * [ 11110011 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3 },
		.len     = 1,
		.exp_len = 4,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (second byte malformed)
		 * [ 11110011 01111111 10111111 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0x7F, 0xBF, 0xBF },
		.len     = 4,
		.exp_len = 1,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (short string 1, second byte malformed)
		 * [ 11110011 011111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0x7F },
		.len     = 2,
		.exp_len = 1,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (short string 2, second byte malformed)
		 * [ 11110011 011111111 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0x7F, 0xBF },
		.len     = 3,
		.exp_len = 1,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},

	{
		/* invalid 4-byte sequence (third byte missing)
		 * [ 11110011 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0xBF },
		.len     = 2,
		.exp_len = 4,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (third byte malformed)
		 * [ 11110011 10111111 01111111 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0xBF, 0x7F, 0xBF },
		.len     = 4,
		.exp_len = 2,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (short string, third byte malformed)
		 * [ 11110011 10111111 01111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0xBF, 0x7F },
		.len     = 3,
		.exp_len = 2,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (fourth byte missing)
		 * [ 11110011 10111111 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0xBF, 0xBF },
		.len     = 3,
		.exp_len = 4,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (fourth byte malformed)
		 * [ 11110011 10111111 10111111 01111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF3, 0xBF, 0xBF, 0x7F },
		.len     = 4,
		.exp_len = 3,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (overlong encoded)
		 * [ 11110000 10000000 10000001 10111111 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF0, 0x80, 0x81, 0xBF },
		.len     = 4,
		.exp_len = 4,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
	{
		/* invalid 4-byte sequence (UTF-16-unrepresentable)
		 * [ 11110100 10010000 10000000 10000000 ] ->
		 * INVALID
		 */
		.arr     = (char *)(unsigned char[]){ 0xF4, 0x90, 0x80, 0x80 },
		.len     = 4,
		.exp_len = 4,
		.exp_cp  = GRAPHEME_INVALID_CODEPOINT,
	},
};

int
main(int argc, char *argv[])
{
	size_t i, failed;

	(void)argc;

	/* UTF-8 decoder test */
	for (i = 0, failed = 0; i < LEN(dec_test); i++) {
		size_t len;
		uint_least32_t cp;

		len = grapheme_decode_utf8(dec_test[i].arr,
		                           dec_test[i].len, &cp);

		if (len != dec_test[i].exp_len ||
		    cp != dec_test[i].exp_cp) {
			fprintf(stderr, "%s: Failed test %zu: "
			        "Expected (%zx,%u), but got (%zx,%u).\n",
			        argv[0], i, dec_test[i].exp_len,
			        dec_test[i].exp_cp, len, cp);
			failed++;
		}
	}
	printf("%s: %zu/%zu unit tests passed.\n", argv[0],
	       LEN(dec_test) - failed, LEN(dec_test));

	return (failed > 0) ? 1 : 0;
}
