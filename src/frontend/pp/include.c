/* vcc preprocessor: include search, entering files, #include */
#include "pp-internal.h"

/*
 * Include resolution, process-wide.  A lookup's answer depends only on the
 * options' signature (cwd, frameworks, ...), the mode (where the search
 * starts, angled, include_next), the includer's directory (quoted), the name
 * and the search directories it looked at: the ones up to where it found
 * the file (all if not found), whose prefix signature (psig) it keeps.
 * Files never change under the token cache (lex_file_get caches misses too).
 * Kbuild puts the per-directory -I last, so one entry serves every TU.  A
 * warm lookup is one probe instead of a path build, directory-cache probe
 * and token-cache probe per search directory.
 *
 * The key also holds the whole search path's id (fsig): meson puts each
 * target's own -I first, so the prefix an answer depends on differs per
 * target anyway, and without fsig the entries of hundreds of targets for
 * the same header shared one hash and one probe cluster (PostgreSQL: 175
 * probes per lookup, half of them same-key entries for other targets).
 */
struct pp_include_answer {
	uint64_t hash;
	uint32_t sig, fsig, key_length;
	int32_t mode;		/* start * 4 + angled * 2 + next */
	uint32_t used, used_signature;	/* directories looked at, psig[used] */
	int32_t sdir;
	uint32_t epoch;		/* the cache epoch (lex.h) it was computed in */
	const lex_file *f;
	const char *path;
	char key[];		/* includer's directory, '\0', name; zero padded to 8 */
};

VPRIV char *pp_string_copy(pp *p, const char *s, size_t n)
{
	char *d = varena_alloc(&p->perm, n + 1);

	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

_Atomic unsigned long pp_include_lookup_count;
_Atomic unsigned long pp_include_guard_skip_count;	/* VPP_TIME */

/*
 * The file at a candidate path, if any: misses in missing directories cost
 * no syscall.  A directory is looked at once per process (dircache.c), so
 * it must not appear during a build: kbuild and meson both create every
 * build-tree directory an include search can look in before compiling
 * (PostgreSQL: all of them exist right after `meson setup`).
 */
static const lex_file *pp_path_try_inner(const char *cwd, const char *path, size_t len)
{
	static _Atomic int off_ = -1;
	int off = atomic_load_explicit(&off_, memory_order_relaxed);

	if (off < 0)
		atomic_store_explicit(&off_, off = getenv("VPP_NODIRCACHE") != NULL, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_include_lookup_count, 1, memory_order_relaxed);
	const lex_file *f;

	if (!off && (!cwd || path[0] == '/') && !pp_directory_exists(path, len)) {
		atomic_fetch_add_explicit(&pp_include_guard_skip_count, 1, memory_order_relaxed);
		return NULL;
	}
	if (cwd && path[0] != '/') {
		char abs[8192];
		size_t cl = strlen(cwd);

		if (cl + len + 2 > sizeof(abs))
			return NULL;
		memcpy(abs, cwd, cl);
		abs[cl] = '/';
		memcpy(abs + cl + 1, path, len);
		abs[cl + 1 + len] = 0;
		/* relative -I (meson): the same skip, on the joined path */
		if (!off && !pp_directory_exists(abs, cl + 1 + len)) {
			atomic_fetch_add_explicit(&pp_include_guard_skip_count, 1, memory_order_relaxed);
			return NULL;
		}
		return lex_file_get(abs, cl + 1 + len);
	}
	f = lex_file_get(path, len);
	return f;
}

/* trigraph mode reads a private, translated copy (lex_file_trigraphs_replace) */
VPRIV const lex_file *pp_path_try(const pp_opts *o, const char *path, size_t len)
{
	const lex_file *f = pp_path_try_inner(o->cwd, path, len);

	if (f && o->trigraphs)
		f = lex_file_trigraphs_replace(f);
	if (f && o->c89)
		f = lex_file_c89_convert(f);
	if (f && o->no_digraphs)
		f = lex_file_without_digraphs(f);
	return f && o->c23 ? lex_file_digit_separators_remove(f) : f;
}

static const lex_file *pp_include_find_inner(pp *p, const pp_context *from, const char *name, size_t nlen,
				      bool angled, bool next, int32_t *sdir, const char **path, uint32_t *used)
{
	char buf[4096];
	const lex_file *f;
	int32_t start;

	*sdir = -1;
	*used = 0;
	if (nlen + 2 >= sizeof(buf))
		return NULL;
	if (name[0] == '/') {
		memcpy(buf, name, nlen);
		buf[nlen] = 0;
		*path = pp_string_copy(p, buf, nlen);
		return pp_path_try(p->o, buf, nlen);
	}
	if (next && from && from->sdir < 0)
		next = false;	/* #include_next outside the search path: a normal search */
	if (!angled && !next) {
		const char *fp = from && from->f != &p->builtin ? from->spath : "./";
		const char *sl = strrchr(fp, '/');
		size_t dl = sl ? (size_t)(sl - fp) + 1 : 0;

		if (!sl && fp[0])
			fp = "./", dl = 2;	/* clang: the includer's directory, "." spelled out (-include's too) */
		if (dl + nlen + 1 < sizeof(buf)) {
			memcpy(buf, fp, dl);
			memcpy(buf + dl, name, nlen);
			buf[dl + nlen] = 0;
			if ((f = pp_path_try(p->o, buf, dl + nlen))) {
				*path = pp_string_copy(p, buf, dl + nlen);
				return f;
			}
		}
	}
	start = next ? from->sdir + 1 : angled ? p->o->nquote : 0;
	for (int32_t k = start; k < p->o->ndirs; k++) {
		const char *d = p->o->dirs[k];
		size_t dl = strlen(d), l;

		if (dl + nlen + 2 >= sizeof(buf))
			continue;
		memcpy(buf, d, dl);
		l = dl;
		if (l && buf[l - 1] != '/')
			buf[l++] = '/';
		memcpy(buf + l, name, nlen);
		l += nlen;
		buf[l] = 0;
		if ((f = pp_path_try(p->o, buf, l))) {
			*sdir = k;
			*path = pp_string_copy(p, buf, l);
			*used = (uint32_t)k + 1;
			return f;
		}
	}
	*used = (uint32_t)p->o->ndirs;	/* not in any of them */
	/* frameworks: <Fw/h.h> -> DIR/Fw.framework/Headers/h.h */
	if (angled && !next) {
		const char *sl = memchr(name, '/', nlen);

		for (int k = 0; sl && k < p->o->nfwdirs; k++) {
			int l = snprintf(buf, sizeof(buf), "%s/%.*s.framework/Headers/%.*s", p->o->fwdirs[k],
					 (int)(sl - name), name, (int)(nlen - (size_t)(sl - name) - 1), sl + 1);

			if (l > 0 && (size_t)l < sizeof(buf) && (f = pp_path_try(p->o, buf, (size_t)l))) {
				*path = pp_string_copy(p, buf, (size_t)l);
				*sdir = -2;
				return f;
			}
		}
	}
	return NULL;
}

#define PP_INCLUDE_ANSWER_TABLE_BITS 17
static _Atomic(pp_include_answer *) pp_include_answer_table[1u << PP_INCLUDE_ANSWER_TABLE_BITS];
static _Atomic uint32_t pp_include_answer_count;

/* a new build: forget every include answer (pp.h) */
void pp_include_cache_flush(void)
{
	for (size_t i = 0; i < (1u << PP_INCLUDE_ANSWER_TABLE_BITS); i++)
		atomic_store_explicit(&pp_include_answer_table[i], NULL, memory_order_release);
	atomic_store(&pp_include_answer_count, 0);
}

static inline bool pp_include_answer_matches(const pp_include_answer *e, uint64_t h, uint32_t sig, uint32_t fsig, int32_t mode, const char *k,
			    size_t kl)
{
	return e->hash == h && e->sig == sig && e->fsig == fsig && e->mode == mode && e->key_length == kl &&
	       !memcmp(e->key, k, kl);
}

/* e's answer holds for p: the directories it looked at are p's */
static inline bool pp_include_answer_is_valid(const pp *p, const pp_include_answer *e)
{
	return e->used <= (uint32_t)p->o->ndirs && p->psig[e->used] == e->used_signature && e->epoch == vcache_epoch();
}

/* the search path prefix the lookup depended on is an input of the open regions */
static inline void pp_include_search_prefix_record(pp *p, uint32_t used)
{
	if (unlikely(p->rec.on))
		for (int i = 0; i < p->rec.n; i++)
			if (p->rec.levels[i].ndirs < used)
				p->rec.levels[i].ndirs = used;
}

/* the lookup's mode and, for a quoted one, the includer's directory (*fp, *dl) */
static inline int32_t pp_include_lookup_mode(const pp *p, const pp_context *from, bool angled, bool next, const char **fp, size_t *dl)
{
	bool lex_character_next = next && from->sdir >= 0;
	int32_t start = lex_character_next ? from->sdir + 1 : angled ? p->o->nquote : 0;

	*fp = "", *dl = 0;
	if (!angled && !lex_character_next) {
		*fp = from->f != &p->builtin ? from->spath : "", *dl = from->f != &p->builtin ? from->spelled_directory_length : 0;
		if (!*dl)
			*fp = "./", *dl = 2;	/* as pp_include_find_inner spells it (-include's too) */
	}
	return start * 4 + angled * 2 + lex_character_next;
}

/* is a header found in search directory sdir in a system directory (pp_include_system_kind without the includer's case) */
static inline uint8_t pp_include_directory_is_system(const pp *p, int32_t sdir)
{
	return sdir == -2 || (sdir >= 0 && p->o->nsys1 && sdir >= p->o->nsys1 - 1);
}

/* the innermost recording level made a lookup that depended on the first used search directories */
static void pp_include_lookup_record(pp *p, const pp_context *from, const char *name, size_t nlen, bool angled, bool next,
		       const lex_file *f, int32_t sdir, const char *path)
{
	struct pp_recording *r = &p->rec;
	pp_header_cache_lookup *l;

	if (r->nlk >= PP_HEADER_CACHE_LOOKUP_MAX || nlen >= 4096) {
		PP_HEADER_CACHE_REGION_REJECT(p);
		return;
	}
	l = &r->lk[r->nlk++];
	*l = (pp_header_cache_lookup){ pp_string_copy(p, name, nlen), from ? from->spath : NULL, f ? path : NULL, f, from ? from->sdir : -1,
		      f ? sdir : -1, (uint32_t)nlen, from ? from->spelled_directory_length : 0, angled, next, from && from->f == &p->builtin,
		      f ? pp_include_directory_is_system(p, sdir) : 0 };
}

static const lex_file *pp_include_find_at(pp *p, const pp_context *from, const char *name, size_t nlen, bool angled,
					bool next, int32_t *sdir, const char **path, const pp_include_answer **ie, uint32_t *usedp);

/* *ie: the cache entry of the answer, if it has one */
VPRIV const lex_file *pp_include_find(pp *p, const pp_context *from, const char *name, size_t nlen,
				     bool angled, bool next, int32_t *sdir, const char **path, const pp_include_answer **ie)
{
	uint32_t used;
	const lex_file *f = pp_include_find_at(p, from, name, nlen, angled, next, sdir, path, ie, &used);

	pp_include_search_prefix_record(p, used);
	if (unlikely(p->rec.on) && used)
		pp_include_lookup_record(p, from, name, nlen, angled, next, f, *sdir, *path);
	return f;
}

/* each of e's own lookups that depended on the search directories gives the same answer here */
VPRIV bool pp_header_cache_lookups_match(pp *p, const pp_header_cache_entry *e)
{
	for (uint32_t i = 0; i < e->nlk; i++) {
		const pp_header_cache_lookup *l = &e->lk[i];
		pp_context fc;
		int32_t sd = -1;
		const char *path = NULL;
		uint32_t used;
		const lex_file *f;

		if (l->from) {
			memset(&fc, 0, sizeof(fc));
			fc.f = l->from_bi ? &p->builtin : NULL;
			fc.spath = l->from, fc.spelled_directory_length = l->from_directory_length, fc.sdir = l->from_sdir;
		}
		f = pp_include_find_at(p, l->from ? &fc : NULL, l->name, l->nlen, l->angled, l->next, &sd, &path, NULL, &used);
		if (f != l->f)
			return false;
		if (f && (sd != l->sdir || pp_include_directory_is_system(p, sd) != l->sys || strcmp(path, l->path)))
			return false;
	}
	return true;
}

static const lex_file *pp_include_find_at(pp *p, const pp_context *from, const char *name, size_t nlen, bool angled,
					bool next, int32_t *sdir, const char **path, const pp_include_answer **ie, uint32_t *usedp)
{
	uint32_t used, i, sig = p->sig, fsig;
	const lex_file *f;
	char kb[4096 + 16];
	const char *fp;
	size_t dl, kl, kp;
	int32_t mode;
	pp_include_answer *e, *old = NULL;
	_Atomic(pp_include_answer *) *stale = NULL;
	uint32_t ep;
	uint64_t h, w;

	if (ie)
		*ie = NULL;
	if (name[0] == '/' || sig == PP_HEADER_CACHE_NONE || !from || nlen >= 2048)
		goto slow;
	mode = pp_include_lookup_mode(p, from, angled, next, &fp, &dl);
	if (dl >= 2048)
		goto slow;
	memcpy(kb, fp, dl);
	kb[dl] = 0;
	memcpy(kb + dl + 1, name, nlen);
	kl = dl + 1 + nlen;
	kp = (kl + 8) & ~(size_t)7;
	memset(kb + kl, 0, 8);
	fsig = p->psig[p->o->ndirs];
	h = ((uint64_t)sig << 32 ^ ((uint64_t)(uint32_t)mode << 17) ^ kl) + (uint64_t)fsig * 0xD6E8FEB86659FD93ull;
	for (size_t j = 0; j < kp; j += 8) {
		memcpy(&w, kb + j, 8);
		h = (h ^ w) * 0x9E3779B97F4A7C15ull;
		h ^= h >> 29;
	}
	ep = vcache_epoch();
	for (i = (uint32_t)(h >> (64 - PP_INCLUDE_ANSWER_TABLE_BITS));; i = (i + 1) & ((1u << PP_INCLUDE_ANSWER_TABLE_BITS) - 1)) {
		e = atomic_load_explicit(&pp_include_answer_table[i], memory_order_acquire);
		if (!e)
			break;
		if (pp_include_answer_matches(e, h, sig, fsig, mode, kb, kl)) {
			if (pp_include_answer_is_valid(p, e))
				goto hit;
			if (e->epoch != ep && !stale)	/* an answer of an older epoch: replaced below */
				stale = &pp_include_answer_table[i], old = e;
		}
	}
	f = pp_include_find_inner(p, from, name, nlen, angled, next, sdir, path, &used);
	if (p->psig[used] == PP_HEADER_CACHE_NONE || (!stale && atomic_fetch_add(&pp_include_answer_count, 1) >= (3u << PP_INCLUDE_ANSWER_TABLE_BITS) / 4))
		goto done;
	{
		size_t pl = f ? strlen(*path) : 0;
		pp_include_answer *m = pp_shared_allocate(sizeof(*m) + kp + pl + 1);

		m->hash = h, m->sig = sig, m->fsig = fsig, m->key_length = (uint32_t)kl, m->mode = mode;
		m->used = used, m->used_signature = p->psig[used], m->sdir = *sdir, m->f = f;
		m->epoch = ep;
		memcpy(m->key, kb, kp);
		m->path = NULL;
		if (f) {
			memcpy(m->key + kp, *path, pl + 1);
			m->path = m->key + kp;
		}
		if (stale && atomic_compare_exchange_strong_explicit(stale, &old, m, memory_order_acq_rel,
								     memory_order_acquire)) {
			e = m;
			goto hit;
		}
		for (;; i = (i + 1) & ((1u << PP_INCLUDE_ANSWER_TABLE_BITS) - 1)) {
			e = NULL;
			if (atomic_compare_exchange_strong_explicit(&pp_include_answer_table[i], &e, m, memory_order_acq_rel,
								    memory_order_acquire)) {
				e = m;
				break;
			}
			if (pp_include_answer_matches(e, h, sig, fsig, mode, kb, kl) && pp_include_answer_is_valid(p, e))
				break;	/* another thread's: the same answer */
		}
	}
hit:
	f = e->f, *sdir = e->sdir, *path = e->path, used = e->used;
	if (ie)
		*ie = e;
	goto done;
slow:
	f = pp_include_find_inner(p, from, name, nlen, angled, next, sdir, path, &used);
done:
	*usedp = used;
	return f;
}

static void pp_file_entry_add(pp *p, pp_context *c, const lex_file *f, const char *name)
{
	size_t nl = strlen(name);
	const char *from = p->o->prefix_from;
	pp_file_entry *e = &p->fe[p->nfe];

	e->spath = name;
	if (from && !strncmp(name, from, strlen(from))) {
		const char *to = p->o->prefix_to ? p->o->prefix_to : "";
		size_t fl = strlen(from), tl = strlen(to);
		char *d = varena_alloc(&p->perm, tl + nl - fl + 1);

		memcpy(d, to, tl);
		memcpy(d + tl, name + fl, nl - fl + 1);
		name = d;
		nl = tl + nl - fl;
	}
	e->base = p->next_location_base;
	e->diagnostic_state = p->dcur;
	e->f = f;
	e->fi = c->fi;
	e->name = name;
	e->namelen = (uint32_t)nl;
	c->fe = p->nfe++;
	c->loc = e->base;
	p->next_location_base += f->size + 1;
}

/* a file skipped by its include guard or #pragma once: still a dependency,
 * under this spelling (clang's FileSkipped).  An entry with no file and a
 * one-byte span holds no location (pp_location_file_entry passes over it). */
/* would clang skip f by its guard: it learns a guard at the end of the file,
 * so f must have been read to its end in this TU (not open now).  Otherwise
 * clang enters f, and -H shows it. */
static bool pp_include_guard_is_known(const pp *p, const lex_file *f)
{
	bool seen = false;

	/* clang: an entry whose #ifndef found its macro undefined (one that found it defined, its
	   body skipped, does not make the guard known: Preprocessor::HandleIfdefDirective) */
	for (uint32_t i = 0; i < p->nfe && !seen; i++)
		seen = p->fe[i].f == f && p->fe[i].guard_open;
	for (int i = 0; i < p->depth && seen; i++)
		if (p->cx[i].type == PP_CONTEXT_FILE && p->cx[i].f == f)
			return false;
	return seen;
}

/* -H: ". path" with a dot per level, as clang (files -include names are not shown) */
static void pp_include_show(pp *p, const char *path)
{
	uint32_t d = 0;

	for (int i = 0; i < p->depth; i++) {
		if (p->cx[i].type == PP_CONTEXT_FILE && p->cx[i].f == &p->builtin && i + 1 < p->depth - 1)
			return;	/* inside an -include'd file */
		d += p->cx[i].type == PP_CONTEXT_FILE && p->cx[i].f != &p->builtin;
	}
	dprintf(p->o->errfd ? p->o->errfd : 2, "%.*s %s\n", (int)(d > 1 ? d - 1 : 1),
		"................................................................", path);
}

/* is a header found in search directory sdir a system header (-1: the includer's
 * directory): 1, 2 for an extern "C" one */
VPRIV uint8_t pp_include_system_kind(pp *p, int32_t sdir)
{
	const pp_context *from = pp_file_context_current(p);

	uint8_t s = from ? p->fe[from->fe].sys : 0, d = 0;	/* clang: the includer's, if higher */

	if (sdir >= 0 && p->o->nsys1 && sdir >= p->o->nsys1 - 1)
		d = 1 + (sdir >= p->o->xc0 && sdir < p->o->xc1);	/* 2: extern "C" */
	else if (sdir == -2)
		d = 1;
	else if (sdir != -1)
		return 0;
	return d > s ? d : s;
}

VPRIV void pp_dependency_add(pp *p, const char *path, uint8_t sys)
{
	pp_file_entry *e = &p->fe[p->nfe++];

	memset(e, 0, sizeof(*e));
	e->spath = path;
	e->name = "";
	e->sys = sys;
	e->base = p->next_location_base++;
}

/*
 * Read-ahead for a file entered for the first time: the first candidate of
 * each unconditional #include "x" / <x> (the includer's directory, or the
 * first search directory that exists) goes to the token cache's I/O
 * threads, so that opening and reading it is off this thread by the time
 * the lookup gets there.  The lookup tries that path first anyway: no extra
 * I/O.  VPP_NOPREFETCH=1 turns it off.
 */
static int pp_prefetch_enabled = -1;

static void pp_include_prefetch(pp *p, const pp_context *c)
{
	const pp_skeleton *fi = c->fi;
	const lex_token *vt = c->vt;
	const char *cwd = p->o->cwd;
	size_t cl = cwd ? strlen(cwd) : 0;
	char buf[4096 + 16];

	if (cl + 1 >= 2048)
		return;
	if (cwd) {
		memcpy(buf, cwd, cl);
		buf[cl++] = '/';
	}
	for (uint32_t k = 0, depth = 0; k < fi->directive_count; k++) {
		const pp_directive_span *d = &fi->dir[k];
		uint32_t b = d->tok + 2, j;
		const char *name, *fp;
		size_t nl, dl;
		bool angled;
		int32_t mode, n = 0;

		if (d->kind == PP_DIRECTIVE_IF || d->kind == PP_DIRECTIVE_IFDEF || d->kind == PP_DIRECTIVE_IFNDEF)
			depth++;
		else if (d->kind == PP_DIRECTIVE_ENDIF && depth)
			depth--;
		/* unconditional ones only (inside the include guard): others may not be wanted */
		if ((d->kind != PP_DIRECTIVE_INCLUDE && d->kind != PP_DIRECTIVE_INCLUDE_NEXT && d->kind != PP_DIRECTIVE_IMPORT) || b >= d->end ||
		    depth != (fi->guard ? 1u : 0u))
			continue;
		if (vt[b].kind == LEX_TOKEN_STRING && !(vt[b].flags & LEX_TOKEN_FLAG_SPLICED) && vt[b].len > 2) {
			name = c->src + vt[b].off + 1;
			nl = vt[b].len - 2;
			angled = false;
		} else if (vt[b].kind == LEX_TOKEN_LESS) {
			for (j = b + 1; j < d->end && vt[j].kind != LEX_TOKEN_GREATER; j++)
				;
			if (j == d->end)
				continue;
			name = c->src + vt[b].off + 1;
			nl = vt[j].off - vt[b].off - 1;
			angled = true;
		} else {
			continue;
		}
		if (!nl || nl >= 1024 || name[0] == '/' || memchr(name, '\n', nl))
			continue;
		mode = pp_include_lookup_mode(p, c, angled, d->kind == PP_DIRECTIVE_INCLUDE_NEXT, &fp, &dl);
		if (!angled && !(mode & 1)) {
			size_t o = cwd && fp[0] != '/' ? cl : 0;

			if (o + dl + nl < 4096) {
				memcpy(buf + o, fp, dl);
				memcpy(buf + o + dl, name, nl);
				buf[o + dl + nl] = 0;
				if (pp_directory_exists(buf, o + dl + nl)) {
					lex_file_prefetch(buf, o + dl + nl);
					n++;
				}
			}
		}
		for (int32_t i = mode >> 2; i < p->o->ndirs && n < 1; i++) {
			const char *dd = p->o->dirs[i];
			size_t l = strlen(dd), o = cwd && dd[0] != '/' ? cl : 0;

			if (o + l + nl + 2 >= 4096)
				continue;
			memcpy(buf + o, dd, l);
			if (l && dd[l - 1] != '/')
				buf[o + l++] = '/';
			memcpy(buf + o + l, name, nl);
			buf[o + l + nl] = 0;
			if (pp_directory_exists(buf, o + l + nl)) {
				lex_file_prefetch(buf, o + l + nl);
				n++;
			}
		}
	}
}

VPRIV void pp_file_enter(pp *p, const lex_file *f, const char *path, int32_t sdir)
{
	bool fresh;
	const pp_skeleton *fi = pp_skeleton_get(f, &fresh);
	const pp_context *from = pp_file_context_current(p);
	pp_header_cache_key *k = NULL;
	uint8_t sys;
	int oi;
	pp_context *c;

	sys = pp_include_system_kind(p, sdir);
	if (fi->guard && pp_macro_guard_is_defined(p, fi->guard) && !(unlikely(p->o->show_includes || p->output_text_mode) && !pp_include_guard_is_known(p, f))) {
		if (p->o->deps)
			pp_dependency_add(p, path, sys);
		return;
	}
	oi = pp_once_file_find(p, f);
	if (unlikely(p->rec.on) && p->rec.nord < PP_HEADER_CACHE_ONCE_MARK_MAX &&
	    (oi < 0 || oi < (int)p->rec.levels[p->rec.n - 1].nonce0))
		p->rec.ord[p->rec.nord++] = (pp_header_cache_once_mark){ f, oi >= 0 };
	if (oi >= 0) {
		if (p->o->deps)
			pp_dependency_add(p, path, sys);
		return;
	}
	if (unlikely(p->output_text_mode) && f != &p->builtin)
		pp_output_file_change(p, from, 1);
	if (f != &p->builtin && from && pp_header_cache_region_is_allowed(p)) {
		int d0 = p->depth;
		uint32_t b0 = p->next_location_base;
		unsigned c0 = p->counter;

		k = pp_header_cache_key_get(p->sig * 2 + (p->output_mode == 2), f, sdir, 0, path, sys, true);
		/* top-level: included from the main file or <built-in> (-include) */
		bool top = p->output_mode == 2 && (p->nfile == 1 || (p->nfile == 2 && from->f == &p->builtin));

		if (k && pp_header_cache_entry_serve(p, k)) {
			if (unlikely(pp_coverage_enabled) && p->output_mode == 2)
				pp_coverage_hit_begin(p, d0);
			if (top) {
				pp_top_event_record(p, p->header_cache_last_entry, b0, c0, 0, 1);
				if (p->depth > d0)
					p->cx[d0].top = 2;	/* its end comes when the replay does */
				else
					pp_top_event_record(p, p->header_cache_last_entry, 0, 0, 1, 1);
			}
			return;
		}
		if (k && top) {
			pp_top_event_record(p, NULL, b0, c0, 0, 0);
			p->top_region_recording = 1;
		}
	}
	c = pp_context_push(p);
	c->type = PP_CONTEXT_FILE;
	c->vt = f->tok;
	c->n = f->ntok;
	c->src = f->src;
	c->f = f;
	c->fi = fi;
	c->di = 0;
	c->sdir = sdir;
	c->spath = path;
	{
		const char *sl = strrchr(path, '/');

		c->spelled_directory_length = sl ? (uint32_t)(sl - path) + 1 : 0;
	}
	p->nfile++;
	if (unlikely(p->o->show_includes) && from && f != &p->builtin && from->f != &p->builtin)
		pp_include_show(p, path);
	pp_file_entry_add(p, c, f, path);
	p->fe[c->fe].comment_position = f->src;
	p->fe[c->fe].sys = sys;
	p->fe[c->fe].guard_open = fi->guard && !pp_macro_guard_is_defined(p, fi->guard);
	if (unlikely(p->lmark) && f != &p->builtin)
		pp_line_marker_file_enter(p, &p->fe[c->fe]);
	p->fe[c->fe].from = from && from->f != &p->builtin && from->i ? from->loc + from->vt[from->i - 1].off : 0;
	if (unlikely(fi->bad)) {
		/* clang's message at the directive; the file is not processed */
		const lex_token *t = &f->tok[fi->unbalanced_token];

		if (fi->bad == 2)
			PP_ERROR(p, p->fe[c->fe].base + t->off, "unterminated conditional directive");
		else
			PP_ERROR(p, p->fe[c->fe].base + t->off, "#%.*s without #if", (int)t->len, f->src + t->off);
		if (p->fatal_jump)
			longjmp(*p->fatal_jump, 1);
	}
	if (fresh) {
		int on = __atomic_load_n(&pp_prefetch_enabled, __ATOMIC_RELAXED);

		if (on < 0)	/* (threads race to set it: the same value) */
			__atomic_store_n(&pp_prefetch_enabled, on = !getenv("VPP_NOPREFETCH"), __ATOMIC_RELAXED);
		if (on)
			pp_include_prefetch(p, c);
	}
	if (k)
		pp_header_cache_region_begin(p, k);
}

VPRIV void pp_include_run(pp *p, pp_context *c, uint32_t b, uint32_t e, int kind, uint32_t loc)
{
	char buf[4096];
	size_t n;
	bool angled;
	int32_t sd;
	const char *path;
	const lex_file *f;
	uint32_t floc;
	int d = p->depth;
	ptok t;
	const pp_include_answer *ie;
	/* "x" or <x> as written: the name is the site's, so is the answer while the options and directory are */
	_Atomic(const pp_include_answer *) *site = b < e && (c->vt[b].kind == LEX_TOKEN_STRING || c->vt[b].kind == LEX_TOKEN_LESS) &&
						p->sig != PP_HEADER_CACHE_NONE
					    ? &((pp_skeleton *)c->fi)->inc[c->di - 1] : NULL;

	if (b < e && (c->vt[b].kind == LEX_TOKEN_STRING || c->vt[b].kind == LEX_TOKEN_LESS)) {	/* -Wextra-tokens */
		uint32_t i = b + 1;

		if (c->vt[b].kind == LEX_TOKEN_LESS)
			while (i < e && c->vt[i - 1].kind != LEX_TOKEN_GREATER)
				i++;
		if (unlikely(i < e && c->vt[i - 1].kind != LEX_TOKEN_LESS))
			pp_directive_extra_token_warn(p, c, b - 1, i);
	}
	if (site && (ie = atomic_load_explicit(site, memory_order_acquire))) {
		const char *fp;
		size_t dl;
		int32_t mode = pp_include_lookup_mode(p, c, c->vt[b].kind == LEX_TOKEN_LESS, kind == PP_DIRECTIVE_INCLUDE_NEXT, &fp, &dl);

		if (ie->sig == p->sig && ie->mode == mode && pp_include_answer_is_valid(p, ie) && dl < ie->key_length && !ie->key[dl] &&
		    !memcmp(ie->key, fp, dl)) {
			pp_include_search_prefix_record(p, ie->used);
			if (unlikely(p->rec.on) && ie->used)
				pp_include_lookup_record(p, c, ie->key + dl + 1, ie->key_length - dl - 1, c->vt[b].kind == LEX_TOKEN_LESS,
					   kind == PP_DIRECTIVE_INCLUDE_NEXT, ie->f, ie->sdir, ie->path);
			pp_file_enter(p, ie->f, ie->path, ie->sdir);
			return;
		}
	}
	pp_context_directive_line_push(p, c, b, e);
	pp_token_raw_next(p, &t);
	floc = t.loc;
	if (t.kind == LEX_TOKEN_STRING || t.kind == LEX_TOKEN_LESS || t.kind == LEX_TOKEN_IDENTIFIER) {
		bool ok;

		p->in_directive++;
		ok = pp_header_name_read(p, buf, sizeof(buf), &n, &angled, &t);
		p->in_directive--;
		if (!ok) {
			pp_context_pop_to(p, d);
			PP_ERROR(p, loc, "expected \"FILENAME\" or <FILENAME>");
			return;
		}
	} else {
		pp_context_pop_to(p, d);
		PP_ERROR(p, loc, "expected \"FILENAME\" or <FILENAME>");
		return;
	}
	pp_context_pop_to(p, d);
	f = pp_include_find(p, c, buf, n, angled, kind == PP_DIRECTIVE_INCLUDE_NEXT, &sd, &path, &ie);
	if (!f) {
		diag_span r = { floc, floc + (uint32_t)n + 2 };
		diag_info di = { "fatal error", floc, &r, 1, NULL, 0, NULL, 0 };

		diag_report_extended(p, &di, "'%.*s' file not found", (int)n, buf);
		if (p->fatal_jump && !p->o->fatal_continue)
			longjmp(*p->fatal_jump, 1);
		return;
	}
	if (site && ie)
		atomic_store_explicit(site, ie, memory_order_release);
	pp_file_enter(p, f, path, sd);
}
