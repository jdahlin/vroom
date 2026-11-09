/*
 * fixdep inside vccd: the kernel's scripts/basic/fixdep (Kai Germaschewski,
 * GPL-2.0) on a dependency file, writing the .cmd file itself.  Output is
 * byte-identical to fixdep's: the dep-file parser is a straight port, and
 * the CONFIG_ scan is the same raw-text scan (comments and strings count).
 *
 * The difference is where the text comes from: every listed file goes
 * through the token cache (vlex_get), where the compile has usually just
 * read it, and each file's CONFIG_ names are found once per process and
 * kept (files never change under vccd, as for the token cache).  fixdep
 * opens and reads all ~900 headers of a TU every time (~29 ms CPU per TU).
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../lib/intern.h"
#include "../lex/lex.h"
#include "fixdep.h"
#include "../lib/buf.h"

/* ---------------- output buffer ---------------- */

/* ---------------- fixdep's hash sets (per job) ---------------- */

typedef struct item {
	struct item *next;
	unsigned int len, hash;
	char name[];
} item;

#define HASHSZ 256

static unsigned int strhash(const char *str, unsigned int sz)
{
	unsigned int hash = 2166136261U;

	for (unsigned int i = 0; i < sz; i++)
		hash = (hash ^ (unsigned char)str[i]) * 0x01000193;
	return hash;
}

/* true if name was there; otherwise added */
static bool in_hashtable(const char *name, unsigned int len, item *tab[])
{
	unsigned int hash = strhash(name, len);
	item *a;

	for (a = tab[hash % HASHSZ]; a; a = a->next)
		if (a->hash == hash && a->len == len && !memcmp(a->name, name, len))
			return true;
	a = malloc(sizeof(*a) + len);
	if (!a)
		abort();
	memcpy(a->name, name, len);
	a->len = len;
	a->hash = hash;
	a->next = tab[hash % HASHSZ];
	tab[hash % HASHSZ] = a;
	return false;
}

static void free_tab(item *tab[])
{
	for (int i = 0; i < HASHSZ; i++)
		for (item *a = tab[i], *n; a; a = n) {
			n = a->next;
			free(a);
		}
}

/* ---------------- CONFIG_ names per file, cached ---------------- */

static int str_ends_with(const char *s, size_t slen, const char *sub)
{
	size_t sublen = strlen(sub);

	return sublen <= slen && !memcmp(s + slen - sublen, sub, sublen);
}

/*
 * fixdep's parse_config_file on NUL-terminated text: the names in order of
 * first occurrence, deduplicated within the file, as "NAME\n" lines.
 */
static char *scan_configs(const char *text)
{
	item *seen[HASHSZ] = { 0 };
	vbuf b = { 0 };
	const char *p = text, *q, *r;

	while ((p = strstr(p, "CONFIG_"))) {
		if (p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) {
			p += 7;
			continue;
		}
		p += 7;
		q = p;
		while (isalnum((unsigned char)*q) || *q == '_')
			q++;
		r = str_ends_with(p, (size_t)(q - p), "_MODULE") ? q - 7 : q;
		if (r > p && !in_hashtable(p, (unsigned int)(r - p), seen)) {
			vbuf_put(&b, p, (size_t)(r - p));
			vbuf_put(&b, "\n", 1);
		}
		p = q;
	}
	vbuf_put(&b, "", 1);
	free_tab(seen);
	return b.p;
}

/* vlex_file -> its names; a mutex-protected open-addressing table (lookups are cheap next to a compile) */
static pthread_mutex_t cmu = PTHREAD_MUTEX_INITIALIZER;
static struct cent {
	const vlex_file *f;
	char *names;
} *ctab;
static size_t ccap, cn;

static const char *file_configs(const vlex_file *f)
{
	size_t h, m;
	char *names;

	pthread_mutex_lock(&cmu);
	if (ctab) {
		m = ccap - 1;
		for (h = ((uintptr_t)f >> 4) * 0x9e3779b97f4a7c15ull >> 20 & m; ctab[h].f; h = (h + 1) & m)
			if (ctab[h].f == f) {
				names = ctab[h].names;
				pthread_mutex_unlock(&cmu);
				return names;
			}
	}
	pthread_mutex_unlock(&cmu);
	names = scan_configs(f->src);	/* outside the lock; a racing thread may scan too */
	pthread_mutex_lock(&cmu);
	if (2 * (cn + 1) > ccap) {
		size_t nc = ccap ? ccap * 2 : 1 << 14;
		struct cent *nt = calloc(nc, sizeof(*nt));

		if (!nt)
			abort();
		for (size_t i = 0; i < ccap; i++)
			if (ctab[i].f) {
				for (h = ((uintptr_t)ctab[i].f >> 4) * 0x9e3779b97f4a7c15ull >> 20 & (nc - 1); nt[h].f;
				     h = (h + 1) & (nc - 1))
					;
				nt[h] = ctab[i];
			}
		free(ctab);
		ctab = nt;
		ccap = nc;
	}
	m = ccap - 1;
	for (h = ((uintptr_t)f >> 4) * 0x9e3779b97f4a7c15ull >> 20 & m; ctab[h].f; h = (h + 1) & m)
		if (ctab[h].f == f)
			break;
	if (ctab[h].f) {
		free(names);
		names = ctab[h].names;
	} else {
		ctab[h].f = f;
		ctab[h].names = names;
		cn++;
	}
	pthread_mutex_unlock(&cmu);
	return names;
}

/* ---------------- fixdep's parse_dep_file ---------------- */

static int is_ignored_file(const char *s, size_t len)
{
	return str_ends_with(s, len, "include/generated/autoconf.h");
}

static int is_no_parse_file(const char *s, size_t len)
{
	return str_ends_with(s, len, ".rlib") || str_ends_with(s, len, ".rmeta") || str_ends_with(s, len, ".so");
}

typedef struct job {
	const char *cwd;
	vbuf *o;
	item **configs;
	char err[512];
} job;

static int use_file(job *j, const char *path)
{
	char abs[4096];
	const vlex_file *f;
	const char *names;

	if (path[0] != '/' && j->cwd)
		snprintf(abs, sizeof(abs), "%s/%s", j->cwd, path);
	else
		snprintf(abs, sizeof(abs), "%s", path);
	f = vlex_get(abs, strlen(abs));
	if (!f) {
		snprintf(j->err, sizeof(j->err), "fixdep: error opening file: %s: %s\n", path, strerror(ENOENT));
		return -1;
	}
	for (names = file_configs(f); *names;) {
		const char *e = strchr(names, '\n');

		if (!in_hashtable(names, (unsigned int)(e - names), j->configs)) {
			vbuf_s(j->o, "    $(wildcard include/config/");
			vbuf_put(j->o, names, (size_t)(e - names));
			vbuf_s(j->o, ") \\\n");
		}
		names = e + 1;
	}
	return 0;
}

static int parse_dep_file(job *j, char *p, const char *target)
{
	item *files[HASHSZ] = { 0 };
	bool saw_any_target = false, is_target = true, is_source = false, need_parse;
	char *q, saved_c;
	int rc = 0;

	while (*p && !rc) {
		switch (*p) {
		case '#':
			p++;
			while (*p != '\0' && *p != '\n') {
				if (*p == '\\')
					p++;
				p++;
			}
			continue;
		case ' ':
		case '\t':
			p++;
			continue;
		case '\\':
			if (p[1] == '\n') {
				p += 2;
				continue;
			}
			break;
		case '\n':
			p++;
			is_target = true;
			continue;
		case ':':
			p++;
			is_target = false;
			is_source = true;
			continue;
		}
		q = p;
		while (*q != ' ' && *q != '\t' && *q != '\n' && *q != '#' && *q != ':') {
			if (*q == '\\') {
				if (q[1] == '\n')
					break;
				if (q[1] == '#' || q[1] == ':') {
					memmove(p + 1, p, (size_t)(q - p));
					p++;
				}
				q++;
			}
			if (*q == '\0')
				break;
			q++;
		}
		if (is_target) {
			p = q;
			continue;
		}
		saved_c = *q;
		*q = '\0';
		need_parse = false;
		if (is_source) {
			if (!saw_any_target) {
				saw_any_target = true;
				vbuf_s(j->o, "source_");
				vbuf_s(j->o, target);
				vbuf_s(j->o, " := ");
				vbuf_s(j->o, p);
				vbuf_s(j->o, "\n\n");
				vbuf_s(j->o, "deps_");
				vbuf_s(j->o, target);
				vbuf_s(j->o, " := \\\n");
				need_parse = true;
			}
		} else if (!is_ignored_file(p, (size_t)(q - p)) && !in_hashtable(p, (unsigned int)(q - p), files)) {
			vbuf_s(j->o, "  ");
			vbuf_s(j->o, p);
			vbuf_s(j->o, " \\\n");
			need_parse = true;
		}
		if (need_parse && !is_no_parse_file(p, (size_t)(q - p)))
			rc = use_file(j, p);
		is_source = false;
		*q = saved_c;
		p = q;
	}
	free_tab(files);
	if (rc)
		return rc;
	if (!saw_any_target) {
		snprintf(j->err, sizeof(j->err), "fixdep: parse error; no targets found\n");
		return -1;
	}
	vbuf_s(j->o, "\n");
	vbuf_s(j->o, target);
	vbuf_s(j->o, ": $(deps_");
	vbuf_s(j->o, target);
	vbuf_s(j->o, ")\n\n$(deps_");
	vbuf_s(j->o, target);
	vbuf_s(j->o, "):\n");
	return 0;
}

/* ---------------- entry ---------------- */

static char *read_at(const char *cwd, const char *path, char *abs, size_t cap)
{
	struct stat st;
	char *buf;
	int fd;

	if (path[0] != '/' && cwd)
		snprintf(abs, cap, "%s/%s", cwd, path);
	else
		snprintf(abs, cap, "%s", path);
	fd = open(abs, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) || !(buf = malloc((size_t)st.st_size + 1))) {
		close(fd);
		return NULL;
	}
	if (read(fd, buf, (size_t)st.st_size) != st.st_size) {
		free(buf);
		close(fd);
		return NULL;
	}
	buf[st.st_size] = 0;
	close(fd);
	return buf;
}

int vfixdep(const char *cwd, const char *depfile, const char *target, const char *cmdline, const char *cmdfile,
	    int errfd)
{
	char dabs[4096], oabs[4096];
	item *configs[HASHSZ] = { 0 };
	vbuf o = { 0 };
	job j = { .cwd = cwd, .o = &o, .configs = configs };

	vintern_init();	/* the token cache interns; vsh may run fixdep before any compile */
	char *d = read_at(cwd, depfile, dabs, sizeof(dabs));
	int fd, rc;

	if (!d) {
		dprintf(errfd, "fixdep: error opening file: %s: %s\n", depfile, strerror(errno));
		return 2;
	}
	vbuf_s(&o, "savedcmd_");
	vbuf_s(&o, target);
	vbuf_s(&o, " := ");
	vbuf_s(&o, cmdline);
	vbuf_s(&o, "\n\n");
	rc = parse_dep_file(&j, d, target);
	free(d);
	free_tab(configs);
	if (rc) {
		dprintf(errfd, "%s", j.err);
		free(o.p);
		return 2;
	}
	if (cmdfile[0] != '/' && cwd)
		snprintf(oabs, sizeof(oabs), "%s/%s", cwd, cmdfile);
	else
		snprintf(oabs, sizeof(oabs), "%s", cmdfile);
	fd = open(oabs, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	rc = fd < 0 || write(fd, o.p, o.n) != (ssize_t)o.n;
	if (fd >= 0 && close(fd))
		rc = 1;
	free(o.p);
	if (rc) {
		dprintf(errfd, "fixdep: not all data was written to the output\n");
		return 1;
	}
	unlink(dabs);	/* rm -f $(depfile) */
	return 0;
}
