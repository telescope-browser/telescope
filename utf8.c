/* Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compat.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "utf8.h"

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static const uint8_t utf8d[] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
	8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
	0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
	0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
	0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
	1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
	1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
	1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};

static inline uint32_t
decode(uint32_t* restrict state, uint32_t* restrict codep, uint8_t byte)
{
	uint32_t type = utf8d[byte];

	*codep = (*state != UTF8_ACCEPT) ?
		(byte & 0x3fu) | (*codep << 6) :
		(0xff >> type) & (byte);

	*state = utf8d[256 + *state*16 + type];
	return *state;
}


/* end of the converter, utility functions ahead */

#define ZERO_WIDTH_SPACE 0x200B

/* public version of decode */
uint32_t
utf8_decode(uint32_t* restrict state, uint32_t* restrict codep, uint8_t byte)
{
	return decode(state, codep, byte);
}

/*
 * returns 0, 1, 2 or less than 8 for tabs.  assumes that
 * sizeof(wchar_t) == 4
 */
static size_t
utf8_chwidth(uint32_t cp, int col)
{
	/* XXX: if we're running on a platform where sizeof(wchar_t)
	 * == 2 what to do?  The manpage for wcwidth and wcs isn't
	 * clear about the encoding, but if it's 16 bit wide I assume
	 * it must use UTF-16... right? */
	assert(sizeof(wchar_t) == 4);

	/*
	 * Tabs are wide until the next multiple of eight.
	 */
	if (cp == '\t')
		return (((col + 8) / 8) * 8) - col;

	return wcwidth((wchar_t)cp);
}

size_t
utf8_snwidth(const char *s, size_t off, int col)
{
	size_t i, tot;
	uint32_t cp = 0, state = 0;
	int width;

	tot = 0;
	for (i = 0; i < off; ++i)
		if (!decode(&state, &cp, s[i])) {
			width = utf8_chwidth(cp, col);
			tot += width;
			col += width;
		}

	return tot;
}

size_t
utf8_swidth(const char *s, int col )
{
	size_t tot;
	uint32_t cp = 0, state = 0;
	int width;

	tot = 0;
	for (; *s; ++s)
		if (!decode(&state, &cp, *s)) {
			width = utf8_chwidth(cp, col);
			tot += width;
			col += width;
		}

	return tot;
}

size_t
utf8_swidth_between(const char *str, const char *end, int col)
{
	size_t tot;
	uint32_t cp = 0, state = 0;
	int width;

	tot = 0;
	for (; *str && str < end; ++str)
		if (!decode(&state, &cp, *str)) {
			width = utf8_chwidth(cp, col);
			tot += width;
			col += width;
		}
	return tot;
}

/*
 * XXX: This is not correct.  There are codepoints classified as
 * "emoji", but these can be joined together to form more complex
 * emoji.  There is an official list of what these valid combinations
 * are, but it would require a costly lookup (a trie can be used to
 * reduce the times, but...).  The following approach is conceptually
 * simpler: if there is a sequence of "emoji codepoints" (or ZWS) and
 * then a space, consider everything before the space a single emoji.
 * It needs a special check for numbers (yes, 0..9 and # are
 * technically speaking emojis) but otherwise seems to work well in
 * practice.
 */
int
emojied_line(const char *s, const char **space_ret)
{
	uint32_t cp = 0, state = 0;
	int only_numbers = 1;

	for (; *s; ++s) {
		if (!decode(&state, &cp, *s)) {
			if (cp == ZERO_WIDTH_SPACE)
				continue;
			if (cp == ' ') {
				*space_ret = s;
				return !only_numbers;
			}
			if (!is_emoji(cp))
				return 0;
			if (cp < '0' || cp > '9')
				only_numbers = 0;
		}
	}

	return 0;
}
