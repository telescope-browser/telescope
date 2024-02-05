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

#ifndef MINIBUFFER_H
#define MINIBUFFER_H

#include "telescope.h"

/* need to be true-ish */
#define MB_READ		1
#define MB_COMPREAD	2

/*
 * Completion provider function.  These functions are called
 * asynchronously.  The function should compute the next completion
 * using the given parameter `state' and modify it eventually.  To
 * signal the end of the completions, complfn should return NULL: the
 * value of state will then be discarded and the function never called
 * again.  The second parameter is some extra metadata per-line; it'll
 * be available as line->data on the selected line during the
 * minibuffer lifecycle.  The third parameter is an extra description
 * field for the current item.
 */
typedef const char *(complfn)(void **, void **, const char **);

struct hist;
extern struct hist *eecmd_history;
extern struct hist *ir_history;
extern struct hist *lu_history;
extern struct hist *read_history;

struct ministate {
	char		*curmesg;

	char		 prompt[64];
	void		 (*donefn)(void);
	void		 (*abortfn)(void);

	char		 buf[1025];
	struct line	 line;
	struct vline	 vline;
	struct buffer	 buffer;

	struct hist	*hist;
	int		 editing;

	struct {
		struct buffer	 buffer;
		complfn		*fn;
		void		*data;
		int		 must_select;
	} compl;
};
extern struct ministate	 ministate;

extern struct buffer	 minibufferwin;
extern int		 in_minibuffer;

void	 recompute_completions(int);
int	 minibuffer_insert_current_candidate(void);
void	 minibuffer_taint_hist(void);
void	 minibuffer_self_insert(void);
void	 sensible_self_insert(void);
void	 eecmd_select(void);
void	 ir_select_gemini(void);
void	 ir_select_reply(void);
void	 ir_select_gopher(void);
void	 lu_select(void);
void	 bp_select(void);
void	 ts_select(void);
void	 ls_select(void);
void	 swiper_select(void);
void	 toc_select(void);
void	 uc_select(void);

void	 enter_minibuffer(void(*)(void), void(*)(void), void(*)(void),
    struct hist *, complfn *, void *, int);

void	 exit_minibuffer(void);
void	 yornp(const char *, void (*)(int, struct tab *), struct tab *);

/*
 * minibuffer_read asks the user for something using the minibuffer.
 * The first argument is the string prompt.  The second and third are
 * the callback to call when done and the data; the callback function
 * can't be NULL.
 */
void	 minibuffer_read(const char *,
    void (*)(const char *, struct tab *), struct tab *);

void	 vmessage(const char *, va_list);
void	 message(const char *, ...) __attribute__((format(printf, 1, 2)));
void	 minibuffer_init(void);

#endif
