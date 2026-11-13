/*
 * vccd's diagnostics across a build (PLAN-diag.md D7).
 *
 * Each compile job vccd runs has a diag_sink.  At the end of the TU pp hands it
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
#include "dstore.h"
#include "common/scoped-cleanup.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct drec_s {
	unsigned char level;
	const char *flag, *path;	/* interned (istr): never freed */
};

/* JSON kept: at most this many records per TU and bytes in all (a -pedantic kernel
   build has 1.2 million records); past it a TU keeps counts and an "omitted" line */
#define TU_JSON_MAX 500
#define JSON_BUDGET (64u << 20)

typedef struct tent {
	struct tent *next;
	char *tu, *json;
	size_t njson;
	struct drec_s *recs;
	unsigned nrec;
} tent;

typedef struct istr { struct istr *next; char s[]; } istr;

#define NBUCKET 4096

static pthread_mutex_t dmu = PTHREAD_MUTEX_INITIALIZER;
static tent *buckets[NBUCKET];
static uint64_t *dset;		/* dedup keys of this build (0: empty slot) */
static size_t dcap, dn;
static unsigned long ndup_build;
static size_t json_bytes;	/* in all tents */
static istr *ibuckets[1024];

static void *xrealloc(void *p, size_t n)
{
	p = realloc(p, n);
	if (!p) {
		fputs("vccd: out of memory\n", stderr);
		abort();
	}
	return p;
}

static char *xstrdup(const char *s) { return s ? strcpy(xrealloc(NULL, strlen(s) + 1), s) : NULL; }

static uint32_t shash(const char *s)
{
	uint32_t h = 2166136261u;

	while (*s)
		h = (h ^ (unsigned char)*s++) * 16777619u;
	return h;
}

/* under dmu: s (a file's path without "/./": an include "./h.h"), kept for good */
static const char *intern(const char *s, bool path)
{
	char tmp[4096];
	uint32_t h;
	istr *e;

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
	h = shash(s) & 1023;
	for (e = ibuckets[h]; e; e = e->next)
		if (!strcmp(e->s, s))
			return e->s;
	e = xrealloc(NULL, sizeof(*e) + strlen(s) + 1);
	strcpy(e->s, s);
	e->next = ibuckets[h], ibuckets[h] = e;
	return e->s;
}

static bool seen(diag_sink *s, uint64_t key)
{
	bool dup = false;

	(void)s;
	key |= 1;	/* never 0 */
	VLOCK(dmu);
	if (2 * (dn + 1) > dcap) {
		size_t nc = dcap ? 2 * dcap : 1024;
		uint64_t *n = calloc(nc, sizeof(*n));

		if (!n)
			abort();
		for (size_t i = 0; i < dcap; i++)
			if (dset[i])
				for (size_t k = dset[i] & (nc - 1);; k = (k + 1) & (nc - 1))
					if (!n[k]) {
						n[k] = dset[i];
						break;
					}
		free(dset);
		dset = n, dcap = nc;
	}
	for (size_t k = key & (dcap - 1);; k = (k + 1) & (dcap - 1)) {
		if (dset[k] == key) {
			dup = true;
			ndup_build++;
			break;
		}
		if (!dset[k]) {
			dset[k] = key;
			dn++;
			break;
		}
	}
	return dup;
}

static void put(dstore_job *j, const char *s, size_t n)
{
	if (j->njson + n > j->cjson) {
		j->cjson = (j->njson + n) * 2 + 256;
		j->json = xrealloc(j->json, j->cjson);
	}
	memcpy(j->json + j->njson, s, n);
	j->njson += n;
}

static void put_jstr(dstore_job *j, const char *s)
{
	put(j, "\"", 1);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		char e[8];

		if (c == '"' || c == '\\')
			e[0] = '\\', e[1] = (char)c, put(j, e, 2);
		else if (c < 0x20)
			put(j, e, (size_t)snprintf(e, sizeof e, "\\u%04x", c));
		else
			put(j, s, 1);
	}
	put(j, "\"", 1);
}

/* pp: one top-level record */
static void record(diag_sink *s, const diag_sink_record *r)
{
	dstore_job *j = (dstore_job *)s;
	struct drec_s d = { (unsigned char)r->level, NULL, NULL };

	{
		VLOCK(dmu);
		d.flag = intern(r->flag, false);
		d.path = intern(r->path, true);
		if (!s->nojson && (j->nrec >= TU_JSON_MAX || json_bytes + j->njson + r->jlen > JSON_BUDGET))
			s->nojson = 1;	/* (pp sends no more JSON for this TU) */
	}
	if (j->nrec == j->crec) {
		j->crec = j->crec ? 2 * j->crec : 8;
		j->recs = xrealloc(j->recs, j->crec * sizeof(*j->recs));
	}
	j->recs[j->nrec++] = d;
	if (!r->json || s->nojson || r->jlen < 2 || r->json[0] != '{') {
		j->omitted++;
		return;
	}
	put(j, "{\"tu\":", 6);
	put_jstr(j, j->tu);
	put(j, ",", 1);
	put(j, r->json + 1, r->jlen - 1);
	put(j, "\n", 1);
}

void dstore_begin(dstore_job *j, const char *cwd, const char *src, int dedup)
{
	size_t n;

	memset(j, 0, sizeof(*j));
	j->sink.dedup = dedup;
	j->sink.seen = seen;
	j->sink.record = record;
	n = (src[0] == '/' || !cwd ? 0 : strlen(cwd) + 1) + strlen(src) + 1;
	j->tu = xrealloc(NULL, n);
	if (src[0] == '/' || !cwd)
		strcpy(j->tu, src);
	else
		snprintf(j->tu, n, "%s/%s", cwd, src);
}

static void tent_free(tent *t)
{
	free(t->recs);
	free(t->json);
	free(t->tu);
	free(t);
}

void dstore_end(dstore_job *j, int ok)
{
	uint32_t b = shash(j->tu) & (NBUCKET - 1);
	tent *old = NULL, *nt = NULL;

	if (ok && j->nrec && j->omitted) {
		char n[32];

		put(j, "{\"tu\":", 6);
		put_jstr(j, j->tu);
		put(j, n, (size_t)snprintf(n, sizeof n, ",\"omitted\":%u}\n", j->omitted));
	}
	if (ok && j->nrec) {
		nt = xrealloc(NULL, sizeof(*nt));
		*nt = (tent){ NULL, j->tu, j->json, j->njson, j->recs, j->nrec };
		j->tu = NULL, j->json = NULL, j->recs = NULL, j->nrec = 0;
	}
	{
		VLOCK(dmu);
		for (tent **pp = &buckets[b]; *pp; pp = &(*pp)->next)
			if (!strcmp((*pp)->tu, nt ? nt->tu : j->tu)) {
				old = *pp;
				*pp = old->next;
				break;
			}
		if (old)
			json_bytes -= old->njson;
		if (nt)
			nt->next = buckets[b], buckets[b] = nt, json_bytes += nt->njson;
	}
	if (old)
		tent_free(old);
	free(j->recs);
	free(j->json);
	free(j->tu);
	memset(j, 0, sizeof(*j));
}

void dstore_new_build(void)
{
	VLOCK(dmu);
	if (dset)
		memset(dset, 0, dcap * sizeof(*dset));
	dn = 0;
	ndup_build = 0;
}

/* ---------------- queries ---------------- */

typedef struct { const char *k; unsigned n[2]; } kcount;	/* warnings, errors */

static void count(kcount **v, unsigned *n, unsigned *c, const char *k, int err)
{
	for (unsigned i = 0; i < *n; i++)
		if (!strcmp((*v)[i].k, k)) {
			(*v)[i].n[err]++;
			return;
		}
	if (*n == *c) {
		*c = *c ? 2 * *c : 16;
		*v = xrealloc(*v, *c * sizeof(**v));
	}
	(*v)[(*n)++] = (kcount){ k, { !err, err } };
}

static int kcmp(const void *a, const void *b)
{
	const kcount *x = a, *y = b;
	unsigned tx = x->n[0] + x->n[1], ty = y->n[0] + y->n[1];

	return tx != ty ? (tx < ty) - (tx > ty) : strcmp(x->k, y->k);
}

static int tcmp(const void *a, const void *b) { return strcmp((*(tent *const *)a)->tu, (*(tent *const *)b)->tu); }

static bool wr(int fd, const char *s, size_t n)
{
	while (n) {
		ssize_t w = write(fd, s, n);

		if (w <= 0)
			return false;
		s += w, n -= (size_t)w;
	}
	return true;
}

static void table(int fd, const char *title, kcount *v, unsigned n)
{
	char line[4200];

	if (!n)
		return;
	qsort(v, n, sizeof(*v), kcmp);
	dprintf(fd, "%s:\n", title);
	for (unsigned i = 0; i < n; i++) {
		int l = snprintf(line, sizeof line, "  %6u warning%s %6u error%s  %s\n", v[i].n[0], v[i].n[0] == 1 ? " " : "s",
				 v[i].n[1], v[i].n[1] == 1 ? " " : "s", v[i].k);

		wr(fd, line, (size_t)(l < (int)sizeof line ? l : (int)sizeof line - 1));
	}
}

int dstore_query(const char *what, int fd, int errfd)
{
	tent **all = NULL;
	size_t n = 0, c = 0;
	int json = what && !strcmp(what, "json");

	if (what && !json && strcmp(what, "summary")) {
		dprintf(errfd, "vcc: error: --vccd-diagnostics=%s: summary or json\n", what);
		return 1;
	}
	VLOCK_AS(dg, dmu);
	for (unsigned b = 0; b < NBUCKET; b++)
		for (tent *t = buckets[b]; t; t = t->next) {
			if (n == c)
				all = xrealloc(all, (c = c ? 2 * c : 64) * sizeof(*all));
			all[n++] = t;
		}
	if (n)
		qsort(all, n, sizeof(*all), tcmp);
	if (json) {
		for (size_t i = 0; i < n; i++)
			wr(fd, all[i]->json, all[i]->njson);
	} else {
		kcount *fl = NULL, *fi = NULL;
		unsigned nfl = 0, cfl = 0, nfi = 0, cfi = 0, w = 0, e = 0;

		for (size_t i = 0; i < n; i++)
			for (unsigned k = 0; k < all[i]->nrec; k++) {
				const struct drec_s *r = &all[i]->recs[k];
				int err = r->level >= DIAG_LEVEL_ERROR;

				if (r->level < DIAG_LEVEL_WARNING)
					continue;
				err ? e++ : w++;
				count(&fl, &nfl, &cfl, r->flag ? r->flag : "(no flag)", err);
				count(&fi, &nfi, &cfi, r->path ? r->path : "(no file)", err);
			}
		dprintf(fd, "vccd: %u warning%s and %u error%s in %zu TU%s; %lu header warning%s left out in this build\n", w,
			w == 1 ? "" : "s", e, e == 1 ? "" : "s", n, n == 1 ? "" : "s", ndup_build, ndup_build == 1 ? "" : "s");
		for (unsigned i = 0; i < nfl; i++)
			if (strcmp(fl[i].k, "(no flag)")) {
				char *f = xrealloc(NULL, strlen(fl[i].k) + 3);

				strcpy(f, "-W"), strcat(f, fl[i].k);
				fl[i].k = f;
			} else {
				fl[i].k = xstrdup(fl[i].k);
			}
		table(fd, "by flag", fl, nfl);
		table(fd, "by file", fi, nfi);
		for (unsigned i = 0; i < nfl; i++)
			free((char *)fl[i].k);
		free(fl);
		free(fi);
	}
	VUNLOCK(dg);
	free(all);
	return 0;
}
