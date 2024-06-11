/* $OpenBSD$ */

/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Versions of malloc and friends that check their results, and never return
 * failure (they call errx if they encounter an error).
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */

#include "compat.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xwrapper.h"

void *
xmalloc(size_t size)
{
	void *ptr;

	if (size == 0)
		errx(1, "xmalloc: zero size");
	ptr = malloc(size);
	if (ptr == NULL)
		errx(1, "xmalloc: allocating %zu bytes: %s",
		    size, strerror(errno));
	return ptr;
}

void *
xcalloc(size_t nmemb, size_t size)
{
	void *ptr;

	if (size == 0 || nmemb == 0)
		errx(1, "xcalloc: zero size");
	ptr = calloc(nmemb, size);
	if (ptr == NULL)
		errx(1, "xcalloc: allocating %zu * %zu bytes: %s",
		    nmemb, size, strerror(errno));
	return ptr;
}

void *
xrealloc(void *ptr, size_t size)
{
	return xreallocarray(ptr, 1, size);
}

void *
xreallocarray(void *ptr, size_t nmemb, size_t size)
{
	void *new_ptr;

	if (nmemb == 0 || size == 0)
		errx(1, "xreallocarray: zero size");
	new_ptr = reallocarray(ptr, nmemb, size);
	if (new_ptr == NULL)
		errx(1, "xreallocarray: allocating %zu * %zu bytes: %s",
		    nmemb, size, strerror(errno));
	return new_ptr;
}

void *
xrecallocarray(void *ptr, size_t oldnmemb, size_t nmemb, size_t size)
{
	void *new_ptr;

	if (nmemb == 0 || size == 0)
		errx(1, "xrecallocarray: zero size");
	new_ptr = recallocarray(ptr, oldnmemb, nmemb, size);
	if (new_ptr == NULL)
		errx(1, "xrecallocarray: allocating %zu * %zu bytes: %s",
		    nmemb, size, strerror(errno));
	return new_ptr;
}

char *
xstrdup(const char *str)
{
	char *cp;

	if ((cp = strdup(str)) == NULL)
		errx(1, "xstrdup: %s", strerror(errno));
	return cp;
}

char *
xstrndup(const char *str, size_t maxlen)
{
	char *cp;

	if ((cp = strndup(str, maxlen)) == NULL)
		errx(1, "xstrndup: %s", strerror(errno));
	return cp;
}

int
xasprintf(char **ret, const char *fmt, ...)
{
	va_list ap;
	int i;

	va_start(ap, fmt);
	i = xvasprintf(ret, fmt, ap);
	va_end(ap);

	return i;
}

int
xvasprintf(char **ret, const char *fmt, va_list ap)
{
	int i;

	i = vasprintf(ret, fmt, ap);

	if (i == -1)
		errx(1, "xasprintf: %s", strerror(errno));

	return i;
}

int
xsnprintf(char *str, size_t len, const char *fmt, ...)
{
	va_list ap;
	int i;

	va_start(ap, fmt);
	i = xvsnprintf(str, len, fmt, ap);
	va_end(ap);

	return i;
}

int
xvsnprintf(char *str, size_t len, const char *fmt, va_list ap)
{
	int i;

	if (len > INT_MAX)
		errx(1, "xsnprintf: len > INT_MAX");

	i = vsnprintf(str, len, fmt, ap);

	if (i < 0 || i >= (int)len)
		errx(1, "xsnprintf: overflow");

	return i;
}
