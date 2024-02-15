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

#ifndef FS_H
#define FS_H

struct tab;
struct tofu_entry;

extern char	config_path_base[PATH_MAX];
extern char	data_path_base[PATH_MAX];
extern char	cache_path_base[PATH_MAX];

extern char	ctlsock_path[PATH_MAX];
extern char	config_path[PATH_MAX];
extern char	lockfile_path[PATH_MAX];
extern char	bookmark_file[PATH_MAX];
extern char	known_hosts_file[PATH_MAX], known_hosts_tmp[PATH_MAX];
extern char	crashed_file[PATH_MAX];
extern char	session_file[PATH_MAX], session_file_tmp[PATH_MAX];
extern char	history_file[PATH_MAX], history_file_tmp[PATH_MAX];
extern char	cert_dir[PATH_MAX], cert_dir_tmp[PATH_MAX];
extern char	certs_file[PATH_MAX], certs_file_tmp[PATH_MAX];

extern char	cwd[PATH_MAX];

int		 fs_init(void);
void		 fs_load_url(struct tab *, const char *);

#endif
