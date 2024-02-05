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

#include "compat.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"
#include "telescope.h"
#include "utils.h"

void
tofu_init(struct ohash *h, unsigned int sz, ptrdiff_t ko)
{
	struct ohash_info info = {
		.key_offset = ko,
		.calloc = hash_calloc,
		.free = hash_free,
		.alloc = hash_alloc,
	};

	ohash_init(h, sz, &info);
}

struct tofu_entry *
tofu_lookup(struct ohash *h, const char *domain, const char *port)
{
	char		buf[GEMINI_URL_LEN];
	unsigned int	slot;

	strlcpy(buf, domain, sizeof(buf));
	if (port != NULL && *port != '\0' && strcmp(port, "1965")) {
		strlcat(buf, ":", sizeof(buf));
		strlcat(buf, port, sizeof(buf));
	}

	slot = ohash_qlookup(h, buf);
	return ohash_find(h, slot);
}

void
tofu_add(struct ohash *h, struct tofu_entry *e)
{
	unsigned int	slot;

	slot = ohash_qlookup(h, e->domain);
	ohash_insert(h, slot, e);
}

int
tofu_save(struct ohash *h, struct tofu_entry *e)
{
	FILE *fp;

	tofu_add(h, e);

	if ((fp = fopen(known_hosts_file, "a")) == NULL)
		return -1;
	fprintf(fp, "%s %s %d\n", e->domain, e->hash, e->verified);
	fclose(fp);
	return 0;
}

void
tofu_update(struct ohash *h, struct tofu_entry *e)
{
	struct tofu_entry *t;

	if ((t = tofu_lookup(h, e->domain, NULL)) == NULL)
		tofu_add(h, e);
	else {
		strlcpy(t->hash, e->hash, sizeof(t->hash));
		t->verified = e->verified;
		free(e);
	}
}

int
tofu_update_persist(struct ohash *h, struct tofu_entry *e)
{
	FILE	*tmp, *fp;
	char	 sfn[PATH_MAX], *line = NULL;
	size_t	 l, linesize = 0;
	ssize_t	 linelen;
	int	 fd, err;

	tofu_update(h, e);

	strlcpy(sfn, known_hosts_tmp, sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1 ||
	    (tmp = fdopen(fd, "w")) == NULL) {
		if (fd != -1) {
			unlink(sfn);
			close(fd);
		}
		return -1;
	}

	if ((fp = fopen(known_hosts_file, "r")) == NULL) {
		unlink(sfn);
		fclose(tmp);
		return -1;
	}

	l = strlen(e->domain);
	while ((linelen = getline(&line, &linesize, fp)) != -1) {
		if (!strncmp(line, e->domain, l))
			continue;
		if (linesize > 0 && line[linesize-1] == '\n')
			line[linesize-1] = '\0';
		fprintf(tmp, "%s\n", line);
	}
	fprintf(tmp, "%s %s %d\n", e->domain, e->hash, e->verified);

	free(line);
	err = ferror(tmp);
	fclose(tmp);
	fclose(fp);

	if (err) {
		unlink(sfn);
		return -1;
	}

	if (rename(sfn, known_hosts_file) == -1)
		return -1;
	return 0;
}

void
tofu_temp_trust(struct ohash *h, const char *host, const char *port,
    const char *hash)
{
	struct tofu_entry *e;

	if ((e = calloc(1, sizeof(*e))) == NULL)
		abort();

	strlcpy(e->domain, host, sizeof(e->domain));
	if (*port != '\0' && strcmp(port, "1965")) {
		strlcat(e->domain, ":", sizeof(e->domain));
		strlcat(e->domain, port, sizeof(e->domain));
	}
	strlcpy(e->hash, hash, sizeof(e->hash));
	e->verified = -1;

	tofu_update(h, e);
}
