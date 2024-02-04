/*
 * Copyright (c) 2021, 2023, 2024 Omar Polo <op@omarpolo.com>
 * Copyright (c) 2019 Renaud Allard <renaud@allard.it>
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2008 Reyk Floeter <reyk@openbsd.org>
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

/*
 * The routines to generate a certificate were derived from acme-client.
 */

#include "compat.h"

#include <sys/stat.h>

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include "certs.h"
#include "fs.h"
#include "iri.h"

/* client certificate */
struct ccert {
	char	*line;	/* fields below points inside here */
	char	*host;
	char	*port;
	char	*path;
	char	*cert;
};

static struct cert_store {
	struct ccert	*certs;
	size_t		 len;
	size_t		 cap;
} cert_store;

char		**identities;
static size_t	  id_len, id_cap;

/*
 * Default number of bits when creating a new RSA key.
 */
#define KBITS 4096

static int
identities_cmp(const void *a, const void *b)
{
	return (strcmp(a, b));
}

static inline int
push_identity(char *name)
{
	void	*t;
	size_t	 newcap, i;

	for (i = 0; i < id_len; ++i) {
		if (!strcmp(identities[i], name))
			return (0);
	}

	/* id_cap is initilized to 8 in certs_init() */
	if (id_len >= id_cap - 1) {
		newcap = id_cap + 8;
		t = recallocarray(identities, id_cap, newcap,
		    sizeof(*identities));
		if (t == NULL)
			return (-1);
		identities = t;
		id_cap = newcap;
	}

	identities[id_len++] = name;
	qsort(identities, id_len, sizeof(*identities), identities_cmp);

	return (0);
}

static int
certs_cmp(const void *a, const void *b)
{
	const struct ccert	*ca = a, *cb = b;
	int			 r;

	if ((r = strcmp(ca->host, cb->host)) != 0)
		return (r);
	if ((r = strcmp(ca->port, cb->port)) != 0)
		return (r);
	if ((r = strcmp(ca->path, cb->path)) != 0)
		return (r);
	return (strcmp(ca->cert, cb->cert));
}

static int
certs_store_add(const char *l)
{
	size_t			 newcap;
	void			*t;
	char			*line, *host, *port, *path, *cert;

	if ((line = strdup(l)) == NULL)
		return (-1);

	host = line;
	while (isspace((unsigned char)*host))
		++host;

	if (*host == '#') {
		free(line);
		return (0);
	}

	port = host + strcspn(host, " \t");
	if (*port == '\0')
		goto err;
	*port++ = '\0';
	while (isspace((unsigned char)*port))
		++port;

	path = port + strcspn(port, " \t");
	if (*path == '\0')
		goto err;
	*path++ = '\0';
	while (isspace((unsigned char)*path))
		++path;

	cert = path + strcspn(path, " \t");
	if (*cert == '\0')
		goto err;
	*cert++ = '\0';
	while (isspace((unsigned char)*cert))
		++cert;

	if (*cert == '\0')
		goto err;

	if (cert_store.len == cert_store.cap) {
		newcap = cert_store.cap + 8;
		t = reallocarray(cert_store.certs, newcap,
		    sizeof(*cert_store.certs));
		if (t == NULL)
			goto err;
		cert_store.certs = t;
		cert_store.cap = newcap;
	}

	cert_store.certs[cert_store.len].line = line;
	cert_store.certs[cert_store.len].host = host;
	cert_store.certs[cert_store.len].port = port;
	cert_store.certs[cert_store.len].path = path;
	cert_store.certs[cert_store.len].cert = cert;
	cert_store.len++;

	return (push_identity(cert));

 err:
	free(line);
	return (-1);
}

int
certs_init(const char *certfile)
{
	FILE	*fp;
	char	*line = NULL;
	size_t	 linesize = 0;
	ssize_t	 linelen;

	id_cap = 8;
	if ((identities = calloc(id_cap, sizeof(*identities))) == NULL)
		return (-1);

	if ((fp = fopen(certfile, "r")) == NULL) {
		if (errno == ENOENT)
			return (0);
		return (-1);
	}

	while ((linelen = getline(&line, &linesize, fp)) != -1) {
		if (line[linelen - 1] == '\n')
			line[--linelen] = '\0';

		if (certs_store_add(line) == -1) {
			fclose(fp);
			free(line);
			return (-1);
		}
	}

	if (ferror(fp)) {
		fclose(fp);
		free(line);
		return (-1);
	}

	/*
	 * Data should already be in order, so mergesort should be
	 * faster.  If it fails (memory scarcity), fall back to qsort()
	 * which is in place.
	 */
	if (mergesort(cert_store.certs, cert_store.len,
	    sizeof(*cert_store.certs), certs_cmp) == -1)
		qsort(cert_store.certs, cert_store.len,
		    sizeof(*cert_store.certs), certs_cmp);

	fclose(fp);
	free(line);
	return (0);
}

const char *
ccert(const char *name)
{
	size_t		 i;

	for (i = 0; i < id_len; ++i) {
		if (!strcmp(name, identities[i]))
			return (identities[i]);
	}

	return (NULL);
}

/*
 * Test whether the test path is under the certificate path.
 */
static inline int
path_under(const char *cpath, const char *tpath)
{
	if (*cpath == '\0')
		return (1);

	while (*cpath != '\0') {
		if (*tpath == '\0')
			return (0);

		if (*cpath++ != *tpath++)
			return (0);
	}

	if (*tpath == '\0' || *tpath == '/')
		return (1);

	return (cpath[-1] == '/');
}

const char *
cert_for(struct iri *iri)
{
	struct ccert	*c;
	size_t		 i;

	for (i = 0; i < cert_store.len; ++i) {
		c = &cert_store.certs[i];

		if (!strcmp(c->host, iri->iri_host) &&
		    !strcmp(c->port, iri->iri_portstr) &&
		    path_under(c->path, iri->iri_path))
			return (c->cert);
	}

	return (NULL);
}

int
cert_open(const char *cert)
{
	char		 path[PATH_MAX];
	struct stat	 sb;
	int		 fd;

	strlcpy(path, cert_dir, sizeof(path));
	strlcat(path, "/", sizeof(path));
	strlcat(path, cert, sizeof(path));

	if ((fd = open(path, O_RDONLY)) == -1)
		return (-1);

	if (fstat(fd, &sb) == -1 || !S_ISREG(sb.st_mode)) {
		close(fd);
		return (-1);
	}

	return (fd);
}

static EVP_PKEY *
rsa_key_create(FILE *f, const char *fname)
{
	EVP_PKEY_CTX	*ctx = NULL;
	EVP_PKEY	*pkey = NULL;
	int		 ret = -1;

	/* First, create the context and the key. */

	if ((ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL)) == NULL)
		goto done;

	if (EVP_PKEY_keygen_init(ctx) <= 0)
		goto done;

	if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, KBITS) <= 0)
		goto done;

	if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
		goto done;

	/* Serialize the key to the disc. */
	if (!PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL))
		goto done;

	ret = 0;
 done:
	if (ret == -1) {
		EVP_PKEY_free(pkey);
		pkey = NULL;
	}
	EVP_PKEY_CTX_free(ctx);
	return pkey;
}

static EVP_PKEY *
ec_key_create(FILE *f, const char *fname)
{
	EC_KEY		*eckey = NULL;
	EVP_PKEY	*pkey = NULL;
	int		 ret = -1;

	if ((eckey = EC_KEY_new_by_curve_name(NID_secp384r1)) == NULL)
		goto done;

	if (!EC_KEY_generate_key(eckey))
		goto done;

	/* Serialise the key to the disc in EC format */
	if (!PEM_write_ECPrivateKey(f, eckey, NULL, NULL, 0, NULL, NULL))
		goto done;

	/* Convert the EC key into a PKEY structure */
	if ((pkey = EVP_PKEY_new()) == NULL)
		goto done;

	if (!EVP_PKEY_set1_EC_KEY(pkey, eckey))
		goto done;

	ret = 0;
 done:
	if (ret == -1) {
		EVP_PKEY_free(pkey);
		pkey = NULL;
	}
	EC_KEY_free(eckey);
	return pkey;
}

int
cert_new(const char *common_name, const char *certpath, const char *keypath,
    int eckey)
{
	EVP_PKEY	*pkey = NULL;
	X509		*x509 = NULL;
	X509_NAME	*name = NULL;
	FILE		*fp = NULL;
	int		 ret = -1;
	const unsigned char *cn = (const unsigned char*)common_name;

	if ((fp = fopen(keypath, "w")) == NULL)
		goto done;

	if (eckey)
		pkey = ec_key_create(fp, keypath);
	else
		pkey = rsa_key_create(fp, keypath);
	if (pkey == NULL)
		goto done;

	if (fflush(fp) == EOF || fclose(fp) == EOF)
		goto done;
	fp = NULL;

	if ((x509 = X509_new()) == NULL)
		goto done;

	ASN1_INTEGER_set(X509_get_serialNumber(x509), 0);
	X509_gmtime_adj(X509_get_notBefore(x509), 0);
	X509_gmtime_adj(X509_get_notAfter(x509), 315360000L); /* 10 years */
	X509_set_version(x509, 2); // v3

	if (!X509_set_pubkey(x509, pkey))
		goto done;

	if ((name = X509_NAME_new()) == NULL)
		goto done;

	if (!X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, cn,
	    -1, -1, 0))
		goto done;

	X509_set_subject_name(x509, name);
	X509_set_issuer_name(x509, name);

	if (!X509_sign(x509, pkey, EVP_sha256()))
		goto done;

	if ((fp = fopen(certpath, "w")) == NULL)
		goto done;

	if (!PEM_write_X509(fp, x509))
		goto done;

	if (fflush(fp) == EOF)
		goto done;

	ret = 0;
 done:
	if (pkey)
		EVP_PKEY_free(pkey);
	if (x509)
		X509_free(x509);
	if (name)
		X509_NAME_free(name);
	if (fp)
		fclose(fp);

	if (ret == -1) {
		(void) unlink(certpath);
		(void) unlink(keypath);
	}
	return (ret);
}
