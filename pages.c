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

#include "telescope.h"

#define ASCII_ART							\
	"```An Ascii art of the word \"Telescope\"\n"			\
	" _______         __\n"						\
	"|_     _|.-----.|  |.-----.-----.----.-----.-----.-----.\n"	\
	"  |   |  |  -__||  ||  -__|__ --|  __|  _  |  _  |  -__|\n"	\
	"  |___|  |_____||__||_____|_____|____|_____|   __|_____|\n"	\
	"                                           |__|\n"		\
	"```\n"

#define CIRCUMLUNAR_SPACE "gemini://gemini.circumlunar.space"

const char *about_about =
	"# About pages\n"
	"\n"
	"Telescope has a number of \"special\" built-in pages:\n"
	"\n"
	"=> about:about		about:about     : this page\n"
	"=> about:blank		about:blank     : a blank page\n"
	"=> about:bookmarks	about:bookmarks : the bookmarks page\n"
	"=> about:help		about:help      : help page\n"
	"=> about:new		about:new       : the default new tab page\n"
	"\n"
	;

const char *about_blank = "\n";

const char *about_help =
	"# Help\n"
	"\n"
	"> Tip: use the arrow keys, page up/down, hjkl or C-{b,f,n,p} to move"
	" the cursor and read the page.\n"
	"\n"
	"> Tip: to enter an URL and start browsing, type `>' or `C-x C-f'.\n"
	"\n"
	"## What is Gemini?\n"
	"\n"
	"Gemini is a new internet protocol which:\n"
	"\n"
	"* is heavier than gopher\n"
	"* is lighter than the web\n"
	"* will not replace either\n"
	"* strives for maximum power to weight ratio\n"
	"* takes user privacy very seriously\n"
	"\n"
	"=> gemini://gemini.circumlunar.space Project Gemini Homepage\n"
	"\n"
	"> Tip: use `B' and `F' (or `H' and `L') to navigate"
	" forward and backward in the history.\n"
	"\n"
	"> Tip: move the cursor over a link and press M-RET (alt-enter) to"
	" open it in another tab.  Then use `C-x t n' and `C-x t p'"
	" (or `g t' and `g T') to switch between tabs.\n"
	"\n"
	"\n"
	"## What is Telescope?\n"
	"\n"
	"Telescope is a Gemini browser written for fun, as hobbystic project"
	" in my free time.  As such, it lacks a ton of features you'll find in"
	" more mature Gemini browsers, but it also has some unique ones.\n"
	"\n"
	"The UI is strongly inspired from Emacs and W3M, so you'll find some"
	" familiar concepts, such as the minibuffer or the tabline, but care"
	" has been taken to make it as familiar as possible for vi and CUA"
	" users too.\n"
	"\n"
	"=> gemini://telescope.omarpolo.com Telescope Project Homepage\n"
	"\n"
	"Telescope is documented very carefully, so please check out the"
	" manpage to know more:\n"
	"\n"
	"> man telescope\n"
	"\n"
	"=> gemini://gemini.omarpolo.com/cgi/man/telescope"
	" Telescope man page in the gemini space.\n"
	"\n"
	"\n"
	"### Appearance\n"
	"\n"
	"Telescope is fully customizable.  The default configuration file is\n"
	"\n"
	"> ~/.telescope/config\n"
	"\n"
	"If your terminal emulator doesn't have problems with UTF-8 glyphs,"
	" try to load Telescope with the following configuration:\n"
	"\n"
	"``` Example of configuration file with some pretty defaults\n"
	"# pretty prefixes\n"
	"style line.item {\n"
	"	prefix \" • \" \"   \"\n"
	"}\n"
	"\n"
	"style line.link {\n"
	"	prefix \"→ \" \"  \"\n"
	"}\n"
	"\n"
	"style line.quote {\n"
	"	prefix \" ┃ \"\n"
	"}\n"
	"```\n"
	;

const char *about_new =
	ASCII_ART
	"\n"
	"=> " CIRCUMLUNAR_SPACE "/docs		Gemini documentation\n"
	"=> " CIRCUMLUNAR_SPACE "/software	Gemini software\n"
	"\n"
	"=> gemini://geminispace.info/		Gemini Search Engine (geminispace.info)\n"
	"\n"
	"Version: " VERSION "\n"
	"Bug reports to: " PACKAGE_BUGREPORT "\n"
	"\n"
	"=> " PACKAGE_URL " Telescope project page\n"
	"=> about:help	Help\n"
	"=> about:about	Internal Pages\n"
	;

/* XXX: keep in sync with telescope.c:/^normalize_code/ */
const char *err_pages[70] = {
	[CANNOT_FETCH]		= "# Couldn't load the page\n",
	[TOO_MUCH_REDIRECTS]	= "# Too much redirects\n",
	[MALFORMED_RESPONSE]	= "# Got a malformed response\n",
	[UNKNOWN_TYPE_OR_CSET]	= "# Unsupported type or charset\n",

	[10] = "# Input required\n",
	[11] = "# Input required\n",
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
