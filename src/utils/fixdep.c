/*
 * fixdep inside vccd: the kernel's scripts/basic/fixdep (Kai Germaschewski,
 * GPL-2.0) on a dependency file, writing the .cmd file itself.  Output is
 * byte-identical to fixdep's: the dep-file parser is a straight port, and
 * the CONFIG_ scan is the same raw-text scan (comments and strings count).
 *
 * The difference is where the text comes from: every listed file goes
 * through the token cache (lex_file_get), where the compile has usually just
 * read it, and each file's CONFIG_ names are found once per process and
 * kept (files never change under vccd, as for the token cache).  fixdep
 * opens and reads all ~900 headers of a TU every time (~29 ms CPU per TU).
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/intern.h"
#include "frontend/lex/lex.h"
#include "utils/fixdep.h"
#include "common/buffer.h"

/* ---------------- output buffer ---------------- */

/* ---------------- fixdep's hash sets (per job) ---------------- */

typedef struct fixdep_item {
	struct fixdep_item *next;
	unsigned int len, hash;
	char name[];
} fixdep_item;

#define FIXDEP_HASH_SIZE 256

static unsigned int fixdep_string_hash(const char *str, unsigned int sz)
{
	unsigned int hash = 2166136261U;

	for (unsigned int i = 0; i < sz; i++)
		hash = (hash ^ (unsigned char)str[i]) * 0x01000193;
	return hash;
}

/* true if name was there; otherwise added */
static bool fixdep_table_contains(const char *name, unsigned int len, fixdep_item *tab[])
{
	unsigned int hash = fixdep_string_hash(name, len);
	fixdep_item *a;

	for (a = tab[hash % FIXDEP_HASH_SIZE]; a; a = a->next)
		if (a->hash == hash && a->len == len && !memcmp(a->name, name, len))
			return true;
	a = malloc(sizeof(*a) + len);
	if (!a)
		abort();
	memcpy(a->name, name, len);
	a->len = len;
	a->hash = hash;
	a->next = tab[hash % FIXDEP_HASH_SIZE];
	tab[hash % FIXDEP_HASH_SIZE] = a;
	return false;
}

static void fixdep_table_free(fixdep_item *tab[])
{
	for (int i = 0; i < FIXDEP_HASH_SIZE; i++)
		for (fixdep_item *a = tab[i], *n; a; a = n) {
			n = a->next;
			free(a);
		}
}

/* ---------------- CONFIG_ names per file, cached ---------------- */

static int fixdep_string_ends_with(const char *s, size_t slen, const char *sub)
{
	size_t sublen = strlen(sub);

	return sublen <= slen && !memcmp(s + slen - sublen, sub, sublen);
}

/*
 * fixdep's parse_config_file on NUL-terminated text: the names in order of
 * first occurrence, deduplicated within the file, as "NAME\n" lines.
 */
static char *fixdep_configs_scan(const char *text)
{
	fixdep_item *seen[FIXDEP_HASH_SIZE] = { 0 };
	vbuffer b = { 0 };
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
		r = fixdep_string_ends_with(p, (size_t)(q - p), "_MODULE") ? q - 7 : q;
		if (r > p && !fixdep_table_contains(p, (unsigned int)(r - p), seen)) {
			vbuffer_append(&b, p, (size_t)(r - p));
			vbuffer_append(&b, "\n", 1);
		}
		p = q;
	}
	vbuffer_append(&b, "", 1);
	fixdep_table_free(seen);
	return b.p;
}

/* f's names, computed once per file (the cache entry is immortal); a racing thread may scan too */
static const char *fixdep_file_configs(const lex_file *f)
{
	lex_file *w = (lex_file *)f;
	char *names = __atomic_load_n(&w->fixdep, __ATOMIC_ACQUIRE), *exp = NULL;

	if (names)
		return names;
	names = fixdep_configs_scan(f->src);
	if (!__atomic_compare_exchange_n(&w->fixdep, &exp, names, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		free(names);
		names = exp;
	}
	return names;
}

/* ---------------- fixdep's fixdep_depfile_parse ---------------- */

static int fixdep_file_is_ignored(const char *s, size_t len)
{
	return fixdep_string_ends_with(s, len, "include/generated/autoconf.h");
}

static int fixdep_file_is_unparsed(const char *s, size_t len)
{
	return fixdep_string_ends_with(s, len, ".rlib") || fixdep_string_ends_with(s, len, ".rmeta") || fixdep_string_ends_with(s, len, ".so");
}

typedef struct fixdep_job {
	const char *cwd;
	vbuffer *output;
	fixdep_item **configs;
	char error_message[512];
} fixdep_job;

static int fixdep_file_use(fixdep_job *j, const char *path)
{
	char abs[4096];
	const lex_file *f;
	const char *names;

	if (path[0] != '/' && j->cwd)
		snprintf(abs, sizeof(abs), "%s/%s", j->cwd, path);
	else
		snprintf(abs, sizeof(abs), "%s", path);
	f = lex_file_get(abs, strlen(abs));
	if (!f) {
		snprintf(j->error_message, sizeof(j->error_message), "fixdep: error opening file: %s: %s\n", path, strerror(ENOENT));
		return -1;
	}
	for (names = fixdep_file_configs(f); *names;) {
		const char *e = strchr(names, '\n');

		if (!fixdep_table_contains(names, (unsigned int)(e - names), j->configs)) {
			vbuffer_string_append(j->output, "    $(wildcard include/config/");
			vbuffer_append(j->output, names, (size_t)(e - names));
			vbuffer_string_append(j->output, ") \\\n");
		}
		names = e + 1;
	}
	return 0;
}

static int fixdep_depfile_parse(fixdep_job *j, char *p, const char *target)
{
	fixdep_item *files[FIXDEP_HASH_SIZE] = { 0 };
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
				vbuffer_string_append(j->output, "source_");
				vbuffer_string_append(j->output, target);
				vbuffer_string_append(j->output, " := ");
				vbuffer_string_append(j->output, p);
				vbuffer_string_append(j->output, "\n\n");
				vbuffer_string_append(j->output, "deps_");
				vbuffer_string_append(j->output, target);
				vbuffer_string_append(j->output, " := \\\n");
				need_parse = true;
			}
		} else if (!fixdep_file_is_ignored(p, (size_t)(q - p)) && !fixdep_table_contains(p, (unsigned int)(q - p), files)) {
			vbuffer_string_append(j->output, "  ");
			vbuffer_string_append(j->output, p);
			vbuffer_string_append(j->output, " \\\n");
			need_parse = true;
		}
		if (need_parse && !fixdep_file_is_unparsed(p, (size_t)(q - p)))
			rc = fixdep_file_use(j, p);
		is_source = false;
		*q = saved_c;
		p = q;
	}
	fixdep_table_free(files);
	if (rc)
		return rc;
	if (!saw_any_target) {
		snprintf(j->error_message, sizeof(j->error_message), "fixdep: parse error; no targets found\n");
		return -1;
	}
	vbuffer_string_append(j->output, "\n");
	vbuffer_string_append(j->output, target);
	vbuffer_string_append(j->output, ": $(deps_");
	vbuffer_string_append(j->output, target);
	vbuffer_string_append(j->output, ")\n\n$(deps_");
	vbuffer_string_append(j->output, target);
	vbuffer_string_append(j->output, "):\n");
	return 0;
}

/* ---------------- entry ---------------- */

static char *fixdep_file_read(const char *cwd, const char *path, char *abs, size_t cap)
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

int fixdep_run(const char *cwd, const char *depfile, const char *target, const char *cmdline, const char *cmdfile,
	    int errfd)
{
	char dabs[4096], oabs[4096];
	fixdep_item *configs[FIXDEP_HASH_SIZE] = { 0 };
	vbuffer o = { 0 };
	fixdep_job j = { .cwd = cwd, .output = &o, .configs = configs };

	vintern_init();	/* the token cache interns; vsh may run fixdep before any compile */
	char *d = fixdep_file_read(cwd, depfile, dabs, sizeof(dabs));
	int fd, rc;

	if (!d) {
		dprintf(errfd, "fixdep: error opening file: %s: %s\n", depfile, strerror(errno));
		return 2;
	}
	vbuffer_string_append(&o, "savedcmd_");
	vbuffer_string_append(&o, target);
	vbuffer_string_append(&o, " := ");
	vbuffer_string_append(&o, cmdline);
	vbuffer_string_append(&o, "\n\n");
	rc = fixdep_depfile_parse(&j, d, target);
	free(d);
	fixdep_table_free(configs);
	if (rc) {
		dprintf(errfd, "%s", j.error_message);
		free(o.p);
		return 2;
	}
	if (cmdfile[0] != '/' && cwd)
		snprintf(oabs, sizeof(oabs), "%s/%s", cwd, cmdfile);
	else
		snprintf(oabs, sizeof(oabs), "%s", cmdfile);
	fd = open(oabs, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
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
