/*
 * Copyright (c) 2021 Omar Polo <op@omarpolo.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <telescope.h>

#define ASCII_ART							\
	"```An Ascii art of the word \"Telescope\"\n"			\
	" _______         __\n"						\
	"|_     _|.-----.|  |.-----.-----.----.-----.-----.-----.\n"	\
	"  |   |  |  -__||  ||  -__|__ --|  __|  _  |  _  |  -__|\n"	\
	"  |___|  |_____||__||_____|_____|____|_____|   __|_____|\n"	\
	"                                           |__|\n"		\
	"```\n"

const char *about_new =
	ASCII_ART
	"\n"
	"Version: " VERSION "\n"
	"Bug reports to: " PACKAGE_BUGREPORT "\n"
	"=> " PACKAGE_URL " Telescope Gemini site: " PACKAGE_URL "\n"
	"\n"
	"*test\n"
	">quote\n"
	;
