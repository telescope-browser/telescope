/*
 * Copyright (c) 2021, 2024 Omar Polo <op@omarpolo.com>
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

extern char	*default_protocol;
extern char	*default_search_engine;
extern char	*download_path;
extern char	*new_tab_url;

extern int	 autosave;
extern int	 dont_wrap_pre;
extern int	 emojify_link;
extern int	 enable_colors;
extern int	 fill_column;
extern int	 fringe_ignore_offset;
extern int	 hide_pre_blocks;
extern int	 hide_pre_closing_line;
extern int	 hide_pre_context;
extern int	 load_url_use_heuristic;
extern int	 max_killed_tabs;
extern int	 olivetti_mode;
extern int	 set_title;
extern int	 tab_bar_show;

extern struct vline fringe;

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

struct download_face {
	int bg, fg, attr;
	int background;
};
extern struct download_face download_face;

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

void		 config_init(void);
int		 config_setprfx(const char *, const char *, const char *);
int		 config_setvari(const char *, int);
int		 config_setvars(const char *, char *);
int		 config_setvarb(const char *, int);
int		 config_setcolor(int, const char *, int, int, int);
int		 config_setattr(const char *, int, int, int);
void		 config_apply_style(void);

#endif
