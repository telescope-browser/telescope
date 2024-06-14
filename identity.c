/*
 * Copyright (c) 2024 Omar Polo <op@omarpolo.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tls.h>
#include <unistd.h>

#include "certs.h"
#include "fs.h"
#include "parser.h"
#include "telescope.h"

#ifndef nitems
#define nitems(x)	(sizeof(x) / sizeof((x)[0]))
#endif

struct cmd;

static int	 cmd_generate(const struct cmd *, int, char **);
static int	 cmd_remove(const struct cmd *, int, char **);
static int	 cmd_import(const struct cmd *, int, char **);
static int	 cmd_export(const struct cmd *, int, char **);
static int	 cmd_list(const struct cmd *, int, char **);
static int	 cmd_mappings(const struct cmd *, int, char **);
static int	 cmd_use(const struct cmd *, int, char **);
static int	 cmd_forget(const struct cmd *, int, char **);

struct cmd {
	const char	*name;
	int		(*fn)(const struct cmd *, int argc, char **argv);
	const char	*usage;
};

static const struct cmd cmds[] = {
	{ "generate",	cmd_generate,	"[-t type] name" },
	{ "remove",	cmd_remove,	"name" },
	{ "import",	cmd_import,	"-C cert [-K key] name" },
	{ "export",	cmd_export,	"-C cert name" },
	{ "list",	cmd_list,	"" },
	{ "mappings",	cmd_mappings,	"" },
	{ "use",	cmd_use,	"name host[:port][/path]" },
	{ "forget",	cmd_forget,	"name host[:port][/path]" },
};

/*
 * Provide some symbols so that we can pull in some subsystems without
 * their the dependencies.
 */

const uint8_t	*about_about;
size_t		 about_about_len;
const uint8_t	*about_blank;
size_t		 about_blank_len;
const uint8_t	*about_crash;
size_t		 about_crash_len;
const uint8_t	*about_help;
size_t		 about_help_len;
const uint8_t	*about_license;
size_t		 about_license_len;
const uint8_t	*about_new;
size_t		 about_new_len;
const uint8_t	*bookmarks;
size_t		 bookmarks_len;

const struct parser gemtext_parser, textplain_parser, textpatch_parser;

void	 load_page_from_str(struct tab *tab, const char *page) { return; }
void	 erase_buffer(struct buffer *buffer) { return; }

static void __dead
usage(void)
{
	size_t i;

	fprintf(stderr, "usage: %s command [args...]\n", getprogname());
	fprintf(stderr, "Available subcommands are:");
	for (i = 0; i < nitems(cmds); ++i)
		fprintf(stderr, " %s", cmds[i].name);
	fputs(".\n", stderr);
	exit(1);
}

static void __dead
cmd_usage(const struct cmd *cmd)
{
	fprintf(stderr, "usage: %s %s%s%s\n", getprogname(), cmd->name,
	    *cmd->usage ? " " : "", cmd->usage);
	exit(1);
}

int
main(int argc, char **argv)
{
	const struct cmd	*cmd;
	size_t			 i;

	/*
	 * Can't use portably getopt() since there's no cross-platform
	 * way of resetting it.
	 */

	if (argc == 0)
		usage();
	argc--, argv++;

	if (argc == 0)
		usage();

	if (!strcmp(*argv, "--"))
		argc--, argv++;
	else if (**argv == '-')
		usage();

	if (argc == 0)
		usage();

	for (i = 0; i < nitems(cmds); ++i) {
		cmd = &cmds[i];

		if (strcmp(cmd->name, argv[0]) != 0)
			continue;

		fs_init();
		if (certs_init(certs_file) == -1)
			errx(1, "failed to initialize the cert store.");
		return (cmd->fn(cmd, argc, argv));
	}

	warnx("unknown command: %s", argv[0]);
	usage();
}

static int
cmd_generate(const struct cmd *cmd, int argc, char **argv)
{
	const char		*name;
	char			 path[PATH_MAX];
	int			 ch, r;
	int			 ec = 1;

	while ((ch = getopt(argc, argv, "t:")) != -1) {
		switch (ch) {
		case 't':
			if (!strcasecmp(optarg, "ec")) {
				ec = 1;
				break;
			}
			if (!strcasecmp(optarg, "rsa")) {
				ec = 0;
				break;
			}
			errx(1, "Unknown key type requested: %s", optarg);

		default:
			cmd_usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		cmd_usage(cmd);

	name = *argv;

	r = snprintf(path, sizeof(path), "%s%s", cert_dir, name);
	if (r < 0 || (size_t)r >= sizeof(path))
		errx(1, "path too long");

	if (cert_new(name, path, ec) == -1)
		errx(1, "failure generating the key");

	return 0;
}

static int
cmd_remove(const struct cmd *cmd, int argc, char **argv)
{
	const char		*name;
	char			 path[PATH_MAX];
	int			 ch, r;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			cmd_usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		cmd_usage(cmd);

	name = *argv;

	r = snprintf(path, sizeof(path), "%s%s", cert_dir, name);
	if (r < 0 || (size_t)r >= sizeof(path))
		errx(1, "path too long");

	if (unlink(path) == -1)
		err(1, "unlink %s", path);
	return 0;
}

static int
cmd_import(const struct cmd *cmd, int argc, char **argv)
{
	struct tls_config	*conf;
	const char		*key = NULL, *cert = NULL;
	char			 path[PATH_MAX], sfn[PATH_MAX];
	FILE			*fp;
	uint8_t			*keym, *certm;
	size_t			 keyl, certl;
	int			 ch, r, fd;
	int			 force = 0;

	while ((ch = getopt(argc, argv, "C:K:f")) != -1) {
		switch (ch) {
		case 'C':
			cert = optarg;
			break;
		case 'K':
			key = optarg;
			break;
		case 'f':
			force = 1;
			break;
		default:
			cmd_usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		cmd_usage(cmd);

	if (key == NULL)
		key = cert;
	if (cert == NULL)
		cmd_usage(cmd);

	if ((keym = tls_load_file(key, &keyl, NULL)) == NULL)
		err(1, "can't open %s", key);
	if ((certm = tls_load_file(cert, &certl, NULL)) == NULL)
		err(1, "can't open %s", cert);

	if ((conf = tls_config_new()) == NULL)
		err(1, "tls_config_new");

	if (tls_config_set_keypair_mem(conf, certm, certl, keym, keyl) == -1)
		errx(1, "failed to load the keypair: %s",
		    tls_config_error(conf));

	tls_config_free(conf);

	r = snprintf(path, sizeof(path), "%s/%s", cert_dir, *argv);
	if (r < 0 || (size_t)r >= sizeof(path))
		err(1, "identity name too long");

	strlcpy(sfn, cert_dir_tmp, sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1 ||
	    (fp = fdopen(fd, "w")) == NULL) {
		if (fd != -1) {
			warn("fdopen");
			unlink(sfn);
			close(fd);
		} else
			warn("mkstamp");
		return 1;
	}

	if (fwrite(certm, 1, certl, fp) != certl) {
		warn("fwrite");
		unlink(sfn);
		fclose(fp);
		return 1;
	}
	if (strcmp(key, cert) != 0 &&
	    fwrite(keym, 1, keyl, fp) != keyl) {
		warn("fwrite");
		unlink(sfn);
		fclose(fp);
		return 1;
	}

	if (fflush(fp) == EOF) {
		warn("fflush");
		unlink(sfn);
		fclose(fp);
		return 1;
	}
	fclose(fp);

	if (!force && access(path, F_OK) == 0) {
		warnx("identity %s already exists", *argv);
		unlink(sfn);
		return 1;
	}

	if (rename(sfn, path) == -1) {
		warn("can't rename");
		unlink(sfn);
		return 1;
	}	    

	return (0);
}

static int
cmd_export(const struct cmd *cmd, int argc, char **argv)
{
	FILE		*fp, *outfp;
	const char	*cert = NULL;
	const char	*identity = NULL;
	char		 path[PATH_MAX];
	char		 buf[BUFSIZ];
	size_t		 l;
	int		 ch, r;

	while ((ch = getopt(argc, argv, "C:")) != -1) {
		switch (ch) {
		case 'C':
			cert = optarg;
			break;
		default:
			cmd_usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		cmd_usage(cmd);
	identity = argv[0];

	if (cert == NULL)
		cmd_usage(cmd);

	r = snprintf(path, sizeof(path), "%s/%s", cert_dir, identity);
	if (r < 0 || (size_t)r >= sizeof(path))
		err(1, "path too long");
	if ((fp = fopen(path, "r")) == NULL)
		err(1, "can't open %s", path);

	if ((outfp = fopen(cert, "w")) == NULL)
		err(1, "can't open %s", cert);

	for (;;) {
		l = fread(buf, 1, sizeof(buf), fp);
		if (l == 0)
			break;
		if (fwrite(buf, 1, l, outfp) != l)
			err(1, "fwrite");
	}
	if (ferror(fp))
		err(1, "fread");

	return 0;
}

static int
cmd_list(const struct cmd *cmd, int argc, char **argv)
{
	char		**id;
	int		 ch;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			cmd_usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		cmd_usage(cmd);

	for (id = identities; *id; ++id)
		puts(*id);

	return (0);
}

static int
cmd_mappings(const struct cmd *cmd, int argc, char **argv)
{
	struct ccert	*c;
	const char	*id = NULL;
	int		 ch, defport;
	size_t		 i;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			cmd_usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 1) {
		if ((id = ccert(*argv)) == NULL)
			errx(1, "unknown identity %s", *argv);
		argc--, argv++;
	}
	if (argc != 0)
		cmd_usage(cmd);

	for (i = 0; i < cert_store.len; ++i) {
		c = &cert_store.certs[i];

		if (id && strcmp(id, c->cert) != 0)
			continue;

		defport = !strcmp(c->port, "1965");

		printf("%s\t%s%s%s%s\n", c->cert, c->host,
		    defport ? "" : ":", defport ? "" : c->port,
		    c->path);
	}

	return (0);
}

static struct iri *
parseiri(char *spec)
{
	static struct iri	 iri;
	const char		*errstr;
	char			*host, *port = NULL, *path = NULL;

	memset(&iri, 0, sizeof(iri));

	host = spec;

	port = host + strcspn(host, ":/");
	if (*port == ':') {
		*port++ = '\0';
		if ((path = strchr(port, '/')) != NULL)
			*path++ = '\0';
	} else if (*port == '/') {
		*port++ = '\0';
		path = port;
		port = NULL;
	} else
		port = NULL;

	strlcpy(iri.iri_host, host, sizeof(iri.iri_host));
	strlcpy(iri.iri_portstr, port ? port : "1965", sizeof(iri.iri_portstr));
	strlcpy(iri.iri_path, path ? path : "/", sizeof(iri.iri_path));

	iri.iri_port = strtonum(iri.iri_portstr, 0, UINT16_MAX, &errstr);
	if (errstr)
		err(1, "port number is %s: %s", errstr, iri.iri_portstr);

	return &iri;
}

static int
cmd_use(const struct cmd *cmd, int argc, char **argv)
{
	char		*cert, *spec;
	int		 ch;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			cmd_usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		cmd_usage(cmd);

	cert = argv[0];
	spec = argv[1];

	if (ccert(cert) == NULL)
		err(1, "unknown identity %s", cert);

	if (cert_save_for(cert, parseiri(spec), 1) == -1)
		errx(1, "failed to save the certificate");

	return 0;
}

static int
cmd_forget(const struct cmd *cmd, int argc, char **argv)
{
	char		*cert, *spec;
	int		 ch;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			cmd_usage(cmd);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		cmd_usage(cmd);

	cert = argv[0];
	spec = argv[1];

	if (ccert(cert) == NULL)
		err(1, "unknown identity %s", cert);

	if (cert_delete_for(cert, parseiri(spec), 1) == -1)
		errx(1, "failed to save the certificate");

	return 0;
}

