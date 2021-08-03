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

#ifndef UI_H
#define UI_H

#include "telescope.h"

extern int	 body_lines;
extern int	 body_cols;

extern struct kmap global_map, minibuffer_map, *current_map, *base_map;

struct excursion {
	int		 curs_x, curs_y;
	size_t		 line_off;
	struct vline	*current_line;
	struct vline	*top_line;
	size_t		 cpoff;
};

enum pairs {
	PTL_BG = 1,
	PTL_TAB,
	PTL_CURR,

	PBODY,
	PBLEFT,
	PBRIGHT,

	PT,
	PT_PRFX,
	PT_TRAIL,
	PL,
	PL_PRFX,
	PL_TRAIL,
	PT1,
	PT1_PRFX,
	PT1_TRAIL,
	PT2,
	PT2_PRFX,
	PT2_TRAIL,
	PT3,
	PT3_PRFX,
	PT3_TRAIL,
	PI,
	PI_PRFX,
	PI_TRAIL,
	PQ,
	PQ_PRFX,
	PQ_TRAIL,
	PPSTART,
	PPSTART_PRFX,
	PPSTART_TRAIL,
	PP,
	PP_PRFX,
	PP_TRAIL,
	PPEND,
	PPEND_PRFX,
	PPEND_TRAIL,

	PPATCH,
	PPATCH_PRFX,
	PPATCH_TRAIL,
	PPATCH_HDR,
	PPATCH_HDR_PRFX,
	PPATCH_HDR_TRAIL,
	PPATCH_HUNK_HDR,
	PPATCH_HUNK_HDR_PRFX,
	PPATCH_HUNK_HDR_TRAIL,
	PPATCH_ADD,
	PPATCH_ADD_PRFX,
	PPATCH_ADD_TRAIL,
	PPATCH_DEL,
	PPATCH_DEL_PRFX,
	PPATCH_DEL_TRAIL,

	PCOMPL_PRFX,
	PCOMPL,
	PCOMPL_TRAIL,

	PCOMPL_CURR_PRFX,
	PCOMPL_CURR,
	PCOMPL_CURR_TRAIL,

	PHELP_PRFX,
	PHELP,
	PHELP_TRAIL,

	PMODELINE,

	PMINIBUF,
};

struct thiskey {
	short meta;
	int key;
	uint32_t cp;
};
extern struct thiskey thiskey;

extern struct tab	*current_tab;

extern struct buffer	 helpwin;
extern int		 help_lines, help_cols;

void		 save_excursion(struct excursion *, struct buffer *);
void		 restore_excursion(struct excursion *, struct buffer *);
void		 global_key_unbound(void);
struct vline	*adjust_line(struct vline *, struct buffer *);
void		 start_loading_anim(struct tab *);
void		 load_url_in_tab(struct tab *, const char *, const char *);
void		 switch_to_tab(struct tab *);
struct buffer	*current_buffer(void);
struct tab	*new_tab(const char *, const char *base);
unsigned int	 tab_new_id(void);

int		 ui_print_colors(void);
int		 ui_init(void);
void		 ui_main_loop(void);
void		 ui_on_tab_loaded(struct tab *);
void		 ui_on_tab_refresh(struct tab *);
const char	*ui_keyname(int);
void		 ui_toggle_side_window(void);
void		 ui_schedule_redraw(void);
void		 ui_after_message_hook(void);
void		 ui_require_input(struct tab *, int, int);
void		 ui_yornp(const char *, void (*)(int, struct tab *), struct tab *);
void		 ui_read(const char *, void (*)(const char *, struct tab *), struct tab *);
void		 ui_other_window(void);
void		 ui_suspend(void);
void		 ui_end(void);

#endif
