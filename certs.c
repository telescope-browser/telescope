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

#include "compat.h"

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

/*
 * Default number of bits when creating a new RSA key.
 */
#define KBITS 4096

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
