/*
 * Copyright (c) 2021, 2022, 2024 Omar Polo <op@omarpolo.com>
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

#define TOFU_URL_MAX_LEN	(1024 + 1)

struct tofu_entry {
	char	domain[TOFU_URL_MAX_LEN];

	/*
	 * enough space for ``PROTO:HASH''.  probably isn't a good
	 * idea tho.
	 */
	char	hash[128+1];
	int	verified;
};

void			 tofu_init(struct ohash *, unsigned int, ptrdiff_t);
struct tofu_entry	*tofu_lookup(struct ohash *, const char *,
			    const char *);
void			 tofu_add(struct ohash *, struct tofu_entry *);
int			 tofu_save(struct ohash *, struct tofu_entry *);
void			 tofu_update(struct ohash *, struct tofu_entry *);
int			 tofu_update_persist(struct ohash *,
			    struct tofu_entry *);
void			 tofu_temp_trust(struct ohash *, const char *,
			    const char *, const char *);
