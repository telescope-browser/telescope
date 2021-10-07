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

#include "fs.h"
#include "telescope.h"

#ifdef __OpenBSD__

# include <limits.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>

void
sandbox_net_process(void)
{
	if (pledge("stdio inet dns", NULL) == -1)
		err(1, "pledge");
}

void
sandbox_ui_process(void)
{
	if (pledge("stdio tty recvfd", NULL) == -1)
		err(1, "pledge");
}

void
sandbox_fs_process(void)
{
	char path[PATH_MAX];

	if (unveil("/tmp", "rwc") == -1)
		err(1, "unveil");

	strlcpy(path, getenv("HOME"), sizeof(path));
	strlcat(path, "/Downloads", sizeof(path));
	if (unveil(path, "rwc") == -1)
		err(1, "unveil");

	if (unveil(config_path_base, "rwc") == -1)
		err(1, "unveil");

	if (unveil(data_path_base, "rwc") == -1)
		err(1, "unveil");

	if (unveil(cache_path_base, "rwc") == -1)
		err(1, "unveil");

	if (pledge("stdio rpath wpath cpath sendfd", NULL) == -1)
		err(1, "pledge");
}

#else

#warning "No sandbox for this OS"

void
sandbox_net_process(void)
{
	return;
}

void
sandbox_ui_process(void)
{
	return;
}

void
sandbox_fs_process(void)
{
	return;
}

#endif
