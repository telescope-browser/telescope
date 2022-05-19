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

#include "telescope.h"
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

/* encode cp in s.  s must be at least 4 bytes wide */
size_t
utf8_encode(uint32_t cp, char *s)
{
	if (cp <= 0x7F) {
		*s = (uint8_t)cp;
		return 1;
	} else if (cp <= 0x7FF) {
		s[1] = (uint8_t)(( cp        & 0x3F ) + 0x80);
		s[0] = (uint8_t)(((cp >>  6) & 0x1F) + 0xC0);
		return 2;
	} else if (cp <= 0xFFFF) {
		s[2] = (uint8_t)(( cp        & 0x3F) + 0x80);
		s[1] = (uint8_t)(((cp >>  6) & 0x3F) + 0x80);
		s[0] = (uint8_t)(((cp >> 12) & 0x0F) + 0xE0);
		return 3;
	} else if (cp <= 0x10FFFF) {
		s[3] = (uint8_t)(( cp        & 0x3F) + 0x80);
		s[2] = (uint8_t)(((cp >>  6) & 0x3F) + 0x80);
		s[1] = (uint8_t)(((cp >> 12) & 0x3F) + 0x80);
		s[0] = (uint8_t)(((cp >> 18) & 0x07) + 0xF0);
		return 4;
	} else {
		s[0] = '\0';
		return 0;
	}
}

char *
utf8_nth(char *s, size_t n)
{
	size_t i;
	uint32_t cp = 0, state = 0;

	for (i = 0; *s && i < n; ++s)
		if (!decode(&state, &cp, *s))
			++i;

	if (state != UTF8_ACCEPT)
		return NULL;
	if (i == n)
		return s;
	return NULL;
}

size_t
utf8_cplen(char *s)
{
	uint32_t cp = 0, state = 0;
	size_t len;

	len = 0;
	for (; *s; ++s)
		if (!decode(&state, &cp, *s))
			len++;
	return len;
}

/* returns only 0, 1, 2 or 8.  assumes sizeof(wchar_t) is 4 */
size_t
utf8_chwidth(uint32_t cp)
{
	/* XXX: if we're running on a platform where sizeof(wchar_t)
	 * == 2 what to do?  The manpage for wcwidth and wcs isn't
	 * clear about the encoding, but if it's 16 bit wide I assume
	 * it must use UTF-16... right? */
	assert(sizeof(wchar_t) == 4);

	/*
	 * quick and dirty fix for the tabs.  In the future we may
	 * want to expand tabs into N spaces, but for the time being
	 * this seems to be good enough (tm).
	 */
	if (cp == '\t')
		return 8;

	return wcwidth((wchar_t)cp);
}

/* NOTE: n is the number of codepoints, NOT the byte length.  In
 * other words, s MUST be NUL-terminated. */
size_t
utf8_snwidth(const char *s, size_t n)
{
	size_t i, tot;
	uint32_t cp = 0, state = 0;

	tot = 0;
	for (i = 0; *s && i < n; ++s)
		if (!decode(&state, &cp, *s)) {
			i++;
			tot += utf8_chwidth(cp);
		}

	return tot;
}

size_t
utf8_swidth(const char *s)
{
	size_t tot;
	uint32_t cp = 0, state = 0;

	tot = 0;
	for (; *s; ++s)
		if (!decode(&state, &cp, *s))
			tot += utf8_chwidth(cp);

	return tot;
}

size_t
utf8_swidth_between(const char *str, const char *end)
{
	size_t tot;
	uint32_t cp = 0, state = 0;

	tot = 0;
	for (; *str && str < end; ++str)
		if (!decode(&state, &cp, *str))
			tot += utf8_chwidth(cp);
	return tot;
}

char *
utf8_next_cp(const char *s)
{
	uint32_t cp = 0, state = 0;

	for (; *s; ++s)
		if (!decode(&state, &cp, *s))
			break;
	return (char*)s+1;
}

char *
utf8_prev_cp(const char *start, const char *base)
{
	uint8_t c;

	for (; start > base; start--) {
		c = *start;
		if ((c & 0xC0) != 0x80)
			return (char*)start;
	}

	return (char*)base;
}

/*
 * XXX: This is not correct.  There are codepoints classified as
 * "emoji", but these can be joined toghether to form more complex
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
