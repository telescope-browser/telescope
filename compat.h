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

#ifndef COMPAT_H
#define COMPAT_H

#include "config.h"

#include <sys/types.h>
#include <sys/uio.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if HAVE_ENDIAN_H
# include <endian.h>
#elif HAVE_SYS_ENDIAN_H
# include <sys/endian.h>
#elif HAVE_LIBKERN_OSBYTEORDER_H
# include <machine/endian.h>
# include <libkern/OSByteOrder.h>

# define htobe16(x) OSSwapHostToBigInt16(x)
# define htole16(x) OSSwapHostToLittleInt16(x)
# define be16toh(x) OSSwapBigToHostInt16(x)
# define le16toh(x) OSSwapLittleToHostInt16(x)

# define htobe32(x) OSSwapHostToBigInt32(x)
# define htole32(x) OSSwapHostToLittleInt32(x)
# define be32toh(x) OSSwapBigToHostInt32(x)
# define le32toh(x) OSSwapLittleToHostInt32(x)

# define htobe64(x) OSSwapHostToBigInt64(x)
# define htole64(x) OSSwapHostToLittleInt64(x)
# define be64toh(x) OSSwapBigToHostInt64(x)
# define le64toh(x) OSSwapLittleToHostInt64(x)
#else
# error no compatible endian.h header found
#endif

#if HAVE_EVENT2
# include <event2/event.h>
# include <event2/event_compat.h>
# include <event2/event_struct.h>
# include <event2/buffer.h>
# include <event2/buffer_compat.h>
# include <event2/bufferevent.h>
# include <event2/bufferevent_struct.h>
# include <event2/bufferevent_compat.h>
#else
# include <event.h>
#endif

#ifdef HAVE_QUEUE_H
# include <sys/queue.h>
#else
# include "compat/queue.h"
#endif

#ifdef HAVE_IMSG
# include <imsg.h>
#else
# include "compat/imsg.h"
#endif

#ifdef HAVE_LIBUTIL
# include <ohash.h>
# include <util.h>
#else
# include "compat/ohash.h"
# define FMT_SCALED_STRSIZE      7       /* minus sign, 4 digits, suffix, null byte */
int		 scan_scaled(char *, long long *);
int		 fmt_scaled(long long, char *);
#endif

#ifndef HAVE_ASPRINTF
int		 asprintf(char**, const char*, ...);
int		 vasprintf(char**, const char*, va_list);
#endif

#ifndef HAVE_ERR
void		 err(int, const char*, ...);
void		 errx(int, const char*, ...);
void		 warn(int, const char*, ...);
void		 warnx(int, const char*, ...);
#else
# include <err.h>
#endif

#ifndef HAVE_EXPLICIT_BZERO
void		 explicit_bzero(void *, size_t);
#endif

#ifndef HAVE_FREEZERO
void		 freezero(void*, size_t);
#endif

#ifndef HAVE_GETDTABLECOUNT
int		 getdtablecount(void);
#endif

#ifndef HAVE_GETDTABLESIZE
int		 getdtablesize(void);
#endif

#ifndef HAVE_GETPROGNAME
const char	*getprogname(void);
#endif

#ifndef HAVE_MEMMEM
void		*memmem(const void*, size_t, const void*, size_t);
#endif

#ifndef HAVE_RECALLOCARRAY
void		*recallocarray(void*, size_t, size_t, size_t);
#endif

#ifndef HAVE_STRCASESTR
char		*strcasestr(const char *, const char *);
#endif

#ifndef HAVE_STRLCPY
size_t		 strlcpy(char*, const char*, size_t);
#endif

#ifndef HAVE_STRLCAT
size_t		 strlcat(char*, const char*, size_t);
#endif

#ifndef HAVE_STRSEP
char		*strsep(char**, const char*);
#endif

#ifndef HAVE_STRTONUM
long long	 strtonum(const char*, long long, long long, const char**);
#endif

#ifndef HAVE_SETPROCTITLE
void		 setproctitle(const char*, ...);
#endif

#endif /* COMPAT_H */
