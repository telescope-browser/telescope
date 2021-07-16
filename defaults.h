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

#ifndef DEFAULTS_H
#define DEFAULTS_H

extern char	*new_tab_url;
extern int	 fill_column;
extern int	 olivetti_mode;
extern int	 enable_colors;
extern int	 hide_pre_context;
extern int	 hide_pre_blocks;
extern int	 hide_pre_closing_line;
extern int	 dont_wrap_pre;
extern int	 emojify_link;

struct lineprefix {
	const char	*prfx1;
	const char	*prfx2;
};
extern struct lineprefix line_prefixes[];

struct line_face {
	int prfx_pair, pair, trail_pair;
	int prfx_bg, bg, trail_bg;
	int prfx_fg, fg, trail_fg;
	int prfx_attr, attr, trail_attr;

	int prefix, text, trail;
};
extern struct line_face line_faces[];

struct tab_face  {
	int bg_attr, bg_bg, bg_fg;
	int t_attr, t_bg, t_fg;
	int c_attr, c_bg, c_fg;

	int background, tab, current;
};
extern struct tab_face tab_face;

struct body_face {
	int lbg, lfg;
	int bg, fg;
	int rbg, rfg;

	int left, body, right;
};
extern struct body_face body_face;

struct modeline_face {
	int bg, fg, attr;
	int background;
};
extern struct modeline_face modeline_face;

struct minibuffer_face {
	int bg, fg, attr;
	int background;
};
extern struct minibuffer_face minibuffer_face;

#endif
