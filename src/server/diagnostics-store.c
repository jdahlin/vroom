/*
 * vccd's diagnostics across a build (PLAN-diag.md D7).
 *
 * Each compile job vccd runs has a pp_dsink.  At the end of the TU pp hands it
 * every top-level record as a JSON line; the store keeps them by source file,
 * so a TU compiled again replaces exactly its own set (an editor or LSP asks
 * with `vcc --vccd-diagnostics=json`, a person with `=summary`: counts by
 * flag and by file).  With VCC_DIAG_DEDUP=1 in the client's environment a
 * warning from a header that another TU of the same build (make's process
 * group) printed is left out and counted: the kernel prints the same header
 * warning once per TU that includes it.
 *
 * One mutex: the store is touched only by jobs that have diagnostics (the
 * records) and by dedup lookups, both rare next to a compile.
 */
#include "diagnostics-store.h"
#include "common/scoped-cleanup.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct vccd_diagnostics_record {
	unsigned char level;
	const char *flag, *path;	/* interned (vccd_diagnostics_interned_string): never freed */
};

/* JSON kept: at most this many records per TU and bytes in all (a -pedantic kernel
   build has 1.2 million records); past it a TU keeps counts and an "omitted" line */
#define VCCD_DIAGNOSTICS_UNIT_JSON_MAX 500
#define VCCD_DIAGNOSTICS_JSON_BUDGET (64u << 20)

typedef struct vccd_diagnostics_unit {
	struct vccd_diagnostics_unit *next;
	char *unit_path, *json;
	size_t json_length;
	struct vccd_diagnostics_record *records;
	unsigned record_count;
} vccd_diagnostics_unit;

typedef struct vccd_diagnostics_interned_string { struct vccd_diagnostics_interned_string *next; char text[]; } vccd_diagnostics_interned_string;

#define VCCD_DIAGNOSTICS_BUCKETS 4096

static pthread_mutex_t vccd_diagnostics_lock = PTHREAD_MUTEX_INITIALIZER;
static vccd_diagnostics_unit *vccd_diagnostics_unit_buckets[VCCD_DIAGNOSTICS_BUCKETS];
static uint64_t *vccd_diagnostics_dedup_keys;		/* dedup keys of this build (0: empty slot) */
static size_t vccd_diagnostics_dedup_capacity, vccd_diagnostics_dedup_count;
static unsigned long vccd_diagnostics_duplicate_count;
static size_t vccd_diagnostics_json_bytes;	/* in all tents */
static vccd_diagnostics_interned_string *vccd_diagnostics_string_buckets[1024];

static void *vccd_diagnostics_realloc(void *p, size_t n)
{
	p = realloc(p, n);
	if (!p) {
		fputs("vccd: out of memory\n", stderr);
		abort();
	}
	return p;
}

static char *vccd_diagnostics_strdup(const char *s) { return s ? strcpy(vccd_diagnostics_realloc(NULL, strlen(s) + 1), s) : NULL; }

static uint32_t vccd_diagnostics_string_hash(const char *s)
{
	uint32_t h = 2166136261u;

	while (*s)
		h = (h ^ (unsigned char)*s++) * 16777619u;
	return h;
}

/* under dmu: s (a file's path without "/./": an include "./h.h"), kept for good */
static const char *vccd_diagnostics_string_intern(const char *s, bool path)
{
	char tmp[4096];
	uint32_t h;
	vccd_diagnostics_interned_string *e;

	if (!s)
		return NULL;
	if (path && strstr(s, "/./") && strlen(s) < sizeof tmp) {
		char *w = tmp;

		for (const char *r = s; *r;)
			if (r[0] == '/' && r[1] == '.' && r[2] == '/')
				r += 2;
			else
				*w++ = *r++;
		*w = 0;
		s = tmp;
	}
	h = vccd_diagnostics_string_hash(s) & 1023;
	for (e = vccd_diagnostics_string_buckets[h]; e; e = e->next)
		if (!strcmp(e->text, s))
			return e->text;
	e = vccd_diagnostics_realloc(NULL, sizeof(*e) + strlen(s) + 1);
	strcpy(e->text, s);
	e->next = vccd_diagnostics_string_buckets[h], vccd_diagnostics_string_buckets[h] = e;
	return e->text;
}

static bool vccd_diagnostics_key_is_seen(pp_dsink *s, uint64_t key)
{
	bool dup = false;

	(void)s;
	key |= 1;	/* never 0 */
	VLOCK(vccd_diagnostics_lock);
	if (2 * (vccd_diagnostics_dedup_count + 1) > vccd_diagnostics_dedup_capacity) {
		size_t nc = vccd_diagnostics_dedup_capacity ? 2 * vccd_diagnostics_dedup_capacity : 1024;
		uint64_t *n = calloc(nc, sizeof(*n));

		if (!n)
			abort();
		for (size_t i = 0; i < vccd_diagnostics_dedup_capacity; i++)
			if (vccd_diagnostics_dedup_keys[i])
				for (size_t k = vccd_diagnostics_dedup_keys[i] & (nc - 1);; k = (k + 1) & (nc - 1))
					if (!n[k]) {
						n[k] = vccd_diagnostics_dedup_keys[i];
						break;
					}
		free(vccd_diagnostics_dedup_keys);
		vccd_diagnostics_dedup_keys = n, vccd_diagnostics_dedup_capacity = nc;
	}
	for (size_t k = key & (vccd_diagnostics_dedup_capacity - 1);; k = (k + 1) & (vccd_diagnostics_dedup_capacity - 1)) {
		if (vccd_diagnostics_dedup_keys[k] == key) {
			dup = true;
			vccd_diagnostics_duplicate_count++;
			break;
		}
		if (!vccd_diagnostics_dedup_keys[k]) {
			vccd_diagnostics_dedup_keys[k] = key;
			vccd_diagnostics_dedup_count++;
			break;
		}
	}
	return dup;
}

static void vccd_diagnostics_json_append(vccd_diagnostics_job *j, const char *s, size_t n)
{
	if (j->json_length + n > j->json_capacity) {
		j->json_capacity = (j->json_length + n) * 2 + 256;
		j->json = vccd_diagnostics_realloc(j->json, j->json_capacity);
	}
	memcpy(j->json + j->json_length, s, n);
	j->json_length += n;
}

static void vccd_diagnostics_json_string_append(vccd_diagnostics_job *j, const char *s)
{
	vccd_diagnostics_json_append(j, "\"", 1);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		char e[8];

		if (c == '"' || c == '\\')
			e[0] = '\\', e[1] = (char)c, vccd_diagnostics_json_append(j, e, 2);
		else if (c < 0x20)
			vccd_diagnostics_json_append(j, e, (size_t)snprintf(e, sizeof e, "\\u%04x", c));
		else
			vccd_diagnostics_json_append(j, s, 1);
	}
	vccd_diagnostics_json_append(j, "\"", 1);
}

/* pp: one top-level record */
static void vccd_diagnostics_record_add(pp_dsink *s, const pp_drec *r)
{
	vccd_diagnostics_job *j = (vccd_diagnostics_job *)s;
	struct vccd_diagnostics_record d = { (unsigned char)r->level, NULL, NULL };

	{
		VLOCK(vccd_diagnostics_lock);
		d.flag = vccd_diagnostics_string_intern(r->flag, false);
		d.path = vccd_diagnostics_string_intern(r->path, true);
		if (!s->nojson && (j->record_count >= VCCD_DIAGNOSTICS_UNIT_JSON_MAX || vccd_diagnostics_json_bytes + j->json_length + r->jlen > VCCD_DIAGNOSTICS_JSON_BUDGET))
			s->nojson = 1;	/* (pp sends no more JSON for this TU) */
	}
	if (j->record_count == j->record_capacity) {
		j->record_capacity = j->record_capacity ? 2 * j->record_capacity : 8;
		j->records = vccd_diagnostics_realloc(j->records, j->record_capacity * sizeof(*j->records));
	}
	j->records[j->record_count++] = d;
	if (!r->json || s->nojson || r->jlen < 2 || r->json[0] != '{') {
		j->omitted++;
		return;
	}
	vccd_diagnostics_json_append(j, "{\"tu\":", 6);
	vccd_diagnostics_json_string_append(j, j->unit_path);
	vccd_diagnostics_json_append(j, ",", 1);
	vccd_diagnostics_json_append(j, r->json + 1, r->jlen - 1);
	vccd_diagnostics_json_append(j, "\n", 1);
}

void vccd_diagnostics_job_begin(vccd_diagnostics_job *j, const char *cwd, const char *src, int dedup)
{
	size_t n;

	memset(j, 0, sizeof(*j));
	j->sink.dedup = dedup;
	j->sink.seen = vccd_diagnostics_key_is_seen;
	j->sink.record = vccd_diagnostics_record_add;
	n = (src[0] == '/' || !cwd ? 0 : strlen(cwd) + 1) + strlen(src) + 1;
	j->unit_path = vccd_diagnostics_realloc(NULL, n);
	if (src[0] == '/' || !cwd)
		strcpy(j->unit_path, src);
	else
		snprintf(j->unit_path, n, "%s/%s", cwd, src);
}

static void vccd_diagnostics_unit_free(vccd_diagnostics_unit *t)
{
	free(t->records);
	free(t->json);
	free(t->unit_path);
	free(t);
}

void vccd_diagnostics_job_end(vccd_diagnostics_job *j, int ok)
{
	uint32_t b = vccd_diagnostics_string_hash(j->unit_path) & (VCCD_DIAGNOSTICS_BUCKETS - 1);
	vccd_diagnostics_unit *old = NULL, *nt = NULL;

	if (ok && j->record_count && j->omitted) {
		char n[32];

		vccd_diagnostics_json_append(j, "{\"tu\":", 6);
		vccd_diagnostics_json_string_append(j, j->unit_path);
		vccd_diagnostics_json_append(j, n, (size_t)snprintf(n, sizeof n, ",\"omitted\":%u}\n", j->omitted));
	}
	if (ok && j->record_count) {
		nt = vccd_diagnostics_realloc(NULL, sizeof(*nt));
		*nt = (vccd_diagnostics_unit){ NULL, j->unit_path, j->json, j->json_length, j->records, j->record_count };
		j->unit_path = NULL, j->json = NULL, j->records = NULL, j->record_count = 0;
	}
	{
		VLOCK(vccd_diagnostics_lock);
		for (vccd_diagnostics_unit **pp = &vccd_diagnostics_unit_buckets[b]; *pp; pp = &(*pp)->next)
			if (!strcmp((*pp)->unit_path, nt ? nt->unit_path : j->unit_path)) {
				old = *pp;
				*pp = old->next;
				break;
			}
		if (old)
			vccd_diagnostics_json_bytes -= old->json_length;
		if (nt)
			nt->next = vccd_diagnostics_unit_buckets[b], vccd_diagnostics_unit_buckets[b] = nt, vccd_diagnostics_json_bytes += nt->json_length;
	}
	if (old)
		vccd_diagnostics_unit_free(old);
	free(j->records);
	free(j->json);
	free(j->unit_path);
	memset(j, 0, sizeof(*j));
}

void vccd_diagnostics_build_new(void)
{
	VLOCK(vccd_diagnostics_lock);
	if (vccd_diagnostics_dedup_keys)
		memset(vccd_diagnostics_dedup_keys, 0, vccd_diagnostics_dedup_capacity * sizeof(*vccd_diagnostics_dedup_keys));
	vccd_diagnostics_dedup_count = 0;
	vccd_diagnostics_duplicate_count = 0;
}

/* ---------------- queries ---------------- */

typedef struct { const char *key; unsigned counts[2]; } vccd_diagnostics_key_count;	/* warnings, errors */

static void vccd_diagnostics_key_count_add(vccd_diagnostics_key_count **v, unsigned *n, unsigned *c, const char *k, int err)
{
	for (unsigned i = 0; i < *n; i++)
		if (!strcmp((*v)[i].key, k)) {
			(*v)[i].counts[err]++;
			return;
		}
	if (*n == *c) {
		*c = *c ? 2 * *c : 16;
		*v = vccd_diagnostics_realloc(*v, *c * sizeof(**v));
	}
	(*v)[(*n)++] = (vccd_diagnostics_key_count){ k, { !err, err } };
}

static int vccd_diagnostics_key_compare(const void *a, const void *b)
{
	const vccd_diagnostics_key_count *x = a, *y = b;
	unsigned tx = x->counts[0] + x->counts[1], ty = y->counts[0] + y->counts[1];

	return tx != ty ? (tx < ty) - (tx > ty) : strcmp(x->key, y->key);
}

static int vccd_diagnostics_unit_compare(const void *a, const void *b) { return strcmp((*(vccd_diagnostics_unit *const *)a)->unit_path, (*(vccd_diagnostics_unit *const *)b)->unit_path); }

static bool vccd_diagnostics_write(int fd, const char *s, size_t n)
{
	while (n) {
		ssize_t w = write(fd, s, n);

		if (w <= 0)
			return false;
		s += w, n -= (size_t)w;
	}
	return true;
}

static void vccd_diagnostics_table_print(int fd, const char *title, vccd_diagnostics_key_count *v, unsigned n)
{
	char line[4200];

	if (!n)
		return;
	qsort(v, n, sizeof(*v), vccd_diagnostics_key_compare);
	dprintf(fd, "%s:\n", title);
	for (unsigned i = 0; i < n; i++) {
		int l = snprintf(line, sizeof line, "  %6u warning%s %6u error%s  %s\n", v[i].counts[0], v[i].counts[0] == 1 ? " " : "s",
				 v[i].counts[1], v[i].counts[1] == 1 ? " " : "s", v[i].key);

		vccd_diagnostics_write(fd, line, (size_t)(l < (int)sizeof line ? l : (int)sizeof line - 1));
	}
}

int vccd_diagnostics_query(const char *what, int fd, int errfd)
{
	vccd_diagnostics_unit **all = NULL;
	size_t n = 0, c = 0;
	int json = what && !strcmp(what, "json");

	if (what && !json && strcmp(what, "summary")) {
		dprintf(errfd, "vcc: error: --vccd-diagnostics=%s: summary or json\n", what);
		return 1;
	}
	VLOCK_AS(dg, vccd_diagnostics_lock);
	for (unsigned b = 0; b < VCCD_DIAGNOSTICS_BUCKETS; b++)
		for (vccd_diagnostics_unit *t = vccd_diagnostics_unit_buckets[b]; t; t = t->next) {
			if (n == c)
				all = vccd_diagnostics_realloc(all, (c = c ? 2 * c : 64) * sizeof(*all));
			all[n++] = t;
		}
	if (n)
		qsort(all, n, sizeof(*all), vccd_diagnostics_unit_compare);
	if (json) {
		for (size_t i = 0; i < n; i++)
			vccd_diagnostics_write(fd, all[i]->json, all[i]->json_length);
	} else {
		vccd_diagnostics_key_count *fl = NULL, *fi = NULL;
		unsigned nfl = 0, cfl = 0, nfi = 0, cfi = 0, w = 0, e = 0;

		for (size_t i = 0; i < n; i++)
			for (unsigned k = 0; k < all[i]->record_count; k++) {
				const struct vccd_diagnostics_record *r = &all[i]->records[k];
				int err = r->level >= VDL_ERROR;

				if (r->level < VDL_WARNING)
					continue;
				err ? e++ : w++;
				vccd_diagnostics_key_count_add(&fl, &nfl, &cfl, r->flag ? r->flag : "(no flag)", err);
				vccd_diagnostics_key_count_add(&fi, &nfi, &cfi, r->path ? r->path : "(no file)", err);
			}
		dprintf(fd, "vccd: %u warning%s and %u error%s in %zu TU%s; %lu header warning%s left out in this build\n", w,
			w == 1 ? "" : "s", e, e == 1 ? "" : "s", n, n == 1 ? "" : "s", vccd_diagnostics_duplicate_count, vccd_diagnostics_duplicate_count == 1 ? "" : "s");
		for (unsigned i = 0; i < nfl; i++)
			if (strcmp(fl[i].key, "(no flag)")) {
				char *f = vccd_diagnostics_realloc(NULL, strlen(fl[i].key) + 3);

				strcpy(f, "-W"), strcat(f, fl[i].key);
				fl[i].key = f;
			} else {
				fl[i].key = vccd_diagnostics_strdup(fl[i].key);
			}
		vccd_diagnostics_table_print(fd, "by flag", fl, nfl);
		vccd_diagnostics_table_print(fd, "by file", fi, nfi);
		for (unsigned i = 0; i < nfl; i++)
			free((char *)fl[i].key);
		free(fl);
		free(fi);
	}
	VUNLOCK(dg);
	free(all);
	return 0;
}
