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

#define CIRCUMLUNAR_SPACE "gemini://gemini.circumlunar.space"

const char *about_new =
	ASCII_ART
	"\n"
	"=> " CIRCUMLUNAR_SPACE "/docs		Gemini documentation\n"
	"=> " CIRCUMLUNAR_SPACE "/software	Gemini software\n"
	"\n"
	"=> gemini://gus.guru/			Gemini Universal Search engine\n"
	"=> gemini://houston.coder.town/	Houston search engine\n"
	"\n"
	"Version: " VERSION "\n"
	"Bug reports to: " PACKAGE_BUGREPORT "\n"
	"=> " PACKAGE_URL " Telescope Gemini site: " PACKAGE_URL "\n"
	"\n"
	"*test\n"
	">quote\n"
	"Id dolore aperiam voluptatem libero eaque omnis rerum nulla. Ullam sit voluptate accusamus molestiae enim minus. Fugit sequi quam dignissimos. Odio inventore vel sed. Voluptatem aut magni dignissimos."
	;

const char *err_pages[70] = {
	[CANNOT_FETCH]		= "# Couldn't load the page\n",
	[TOO_MUCH_REDIRECTS]	= "# Too much redirects\n",

	[40] = "# Temporary failure\n",
	[41] = "# Server unavailable\n",
	[42] = "# CGI error\n",
	[43] = "# Proxy error\n",
	[44] = "# Slow down\n",
	[50] = "# Permanent failure\n",
	[51] = "# Not found\n",
	[52] = "# Gone\n",
	[53] = "# Proxy request refused\n",
	[59] = "# Bad request\n",
	[60] = "# Client certificate required\n",
	[61] = "# Certificate not authorised\n",
	[62] = "# Certificate not valid\n"
};
