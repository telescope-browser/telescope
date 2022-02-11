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

#include "compat.h"

#include "fs.h"
#include "telescope.h"

#ifdef __OpenBSD__

# include <errno.h>
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
	if (pledge("stdio tty unix recvfd", NULL) == -1)
		err(1, "pledge");
}

void
sandbox_fs_process(void)
{
	char path[PATH_MAX];

	if (unveil("/tmp", "rwc") == -1)
		err(1, "unveil(/tmp)");

	strlcpy(path, getenv("HOME"), sizeof(path));
	strlcat(path, "/Downloads", sizeof(path));
	if (unveil(path, "rwc") == -1 && errno != ENOENT)
		err(1, "unveil(%s)", path);

	if (unveil(config_path_base, "rwc") == -1)
		err(1, "unveil(%s)", config_path_base);

	if (unveil(data_path_base, "rwc") == -1)
		err(1, "unveil(%s)", data_path_base);

	if (unveil(cache_path_base, "rwc") == -1)
		err(1, "unveil(%s)", cache_path_base);

	if (pledge("stdio rpath wpath cpath sendfd", NULL) == -1)
		err(1, "pledge");
}

#elif HAVE_LINUX_LANDLOCK_H

#include <linux/landlock.h>
#include <linux/prctl.h>

#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * What's the deal with landlock?  While distro with linux >= 5.13
 * have the struct declarations, libc wrappers are missing.  The
 * sample landlock code provided by the authors includes these "shims"
 * in their example for the landlock API until libc provides them.
 *
 * Linux is such a mess sometimes.  /rant
 */

#ifndef landlock_create_ruleset
static inline int
landlock_create_ruleset(const struct landlock_ruleset_attr *attr, size_t size,
    __u32 flags)
{
	return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
#endif

#ifndef landlock_add_rule
static inline int
landlock_add_rule(int ruleset_fd, enum landlock_rule_type type,
    const void *attr, __u32 flags)
{
	return syscall(__NR_landlock_add_rule, ruleset_fd, type, attr, flags);
}
#endif

#ifndef landlock_restrict_self
static inline int
landlock_restrict_self(int ruleset_fd, __u32 flags)
{
	return syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}
#endif

static int
open_landlock(void)
{
	int fd;
	struct landlock_ruleset_attr attr = {
		.handled_access_fs =	LANDLOCK_ACCESS_FS_EXECUTE |
					LANDLOCK_ACCESS_FS_READ_FILE |
					LANDLOCK_ACCESS_FS_READ_DIR |
					LANDLOCK_ACCESS_FS_WRITE_FILE |
					LANDLOCK_ACCESS_FS_REMOVE_DIR |
					LANDLOCK_ACCESS_FS_REMOVE_FILE |
					LANDLOCK_ACCESS_FS_MAKE_CHAR |
					LANDLOCK_ACCESS_FS_MAKE_DIR |
					LANDLOCK_ACCESS_FS_MAKE_REG |
					LANDLOCK_ACCESS_FS_MAKE_SOCK |
					LANDLOCK_ACCESS_FS_MAKE_FIFO |
					LANDLOCK_ACCESS_FS_MAKE_BLOCK |
					LANDLOCK_ACCESS_FS_MAKE_SYM,
	};

	fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	if (fd == -1) {
		switch (errno) {
		case ENOSYS:
		case EOPNOTSUPP:
			return -1;
		default:
			err(1, "can't create landlock ruleset");
		}
	}
	return fd;
}

static int
landlock_unveil(int landlock_fd, const char *path, int perms)
{
	struct landlock_path_beneath_attr pb;
	int err, saved_errno;

	pb.allowed_access = perms;

	if ((pb.parent_fd = open(path, O_PATH)) == -1)
		return -1;

	err = landlock_add_rule(landlock_fd, LANDLOCK_RULE_PATH_BENEATH,
	    &pb, 0);
	saved_errno = errno;
	close(pb.parent_fd);
	errno = saved_errno;
	return err ? -1 : 0;
}

static int
landlock_apply(int fd)
{
	int r, saved_errno;

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
		err(1, "%s: prctl(PR_SET_NO_NEW_PRIVS)", __func__);

	r = landlock_restrict_self(fd, 0);
	saved_errno = errno;
	close(fd);
	errno = saved_errno;
	return r ? -1 : 0;
}

static int
landlock_no_fs(void)
{
	int fd;

	/*
	 * XXX: landlock disabled at runtime, pretend everything's
	 * good.
	 */
	if ((fd = open_landlock()) == -1)
		return 0;

	return landlock_apply(fd);
}

void
sandbox_net_process(void)
{
	/*
	 * We don't know what paths are required for the TLS stack.
	 * Yes, it sucks.
	 */
	return;
}

void
sandbox_ui_process(void)
{
	if (landlock_no_fs() == -1)
		err(1, "landlock");
}

void
sandbox_fs_process(void)
{
	int fd, rwc;
	char path[PATH_MAX];

	/*
	 * XXX: at build-time we found landlock.h but we've just
	 * realized it's not available on this kernel, so do nothing.
	 */
	if ((fd = open_landlock()) == -1)
		return;

	rwc =	LANDLOCK_ACCESS_FS_READ_FILE	|
		LANDLOCK_ACCESS_FS_READ_DIR	|
		LANDLOCK_ACCESS_FS_WRITE_FILE	|
		LANDLOCK_ACCESS_FS_MAKE_DIR	|
		LANDLOCK_ACCESS_FS_MAKE_REG;

	if (landlock_unveil(fd, "/tmp", rwc) == -1)
		err(1, "landlock_unveil(/tmp)");

	strlcpy(path, getenv("HOME"), sizeof(path));
	strlcat(path, "/Downloads", sizeof(path));
	if (landlock_unveil(fd, path, rwc) == -1 && errno != ENOENT)
		err(1, "landlock_unveil(%s)", path);

	if (landlock_unveil(fd, config_path_base, rwc) == -1)
		err(1, "landlock_unveil(%s)", config_path_base);

	if (landlock_unveil(fd, data_path_base, rwc) == -1)
		err(1, "landlock_unveil(%s)", data_path_base);

	if (landlock_unveil(fd, cache_path_base, rwc) == -1)
		err(1, "landlock_unveil(%s)", cache_path_base);
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
