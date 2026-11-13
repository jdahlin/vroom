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
	uint32_t signature, search_path_signature, key_length;
	int32_t mode;		/* start * 4 + angled * 2 + next */
	uint32_t used, used_signature;	/* directories looked at, psig[used] */
	int32_t search_directory;
	uint32_t epoch;		/* the cache epoch (lex.h) it was computed in */
	const lex_file *f;
	const char *path;
	char key[];		/* includer's directory, '\0', name; zero padded to 8 */
};

VPRIV char *pp_string_copy(pp *p, const char *s, size_t n)
{
	char *d = varena_alloc(&p->permanent_region, n + 1);

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
		return pp_path_try(p->options, buf, nlen);
	}
	if (next && from && from->search_directory < 0)
		next = false;	/* #include_next outside the search path: a normal search */
	if (!angled && !next) {
		const char *fp = from && from->lex_file != &p->builtin_file ? from->spelled_path : "./";
		const char *sl = strrchr(fp, '/');
		size_t dl = sl ? (size_t)(sl - fp) + 1 : 0;

		if (!sl && fp[0])
			fp = "./", dl = 2;	/* clang: the includer's directory, "." spelled out (-include's too) */
		if (dl + nlen + 1 < sizeof(buf)) {
			memcpy(buf, fp, dl);
			memcpy(buf + dl, name, nlen);
			buf[dl + nlen] = 0;
			if ((f = pp_path_try(p->options, buf, dl + nlen))) {
				*path = pp_string_copy(p, buf, dl + nlen);
				return f;
			}
		}
	}
	start = next ? from->search_directory + 1 : angled ? p->options->nquote : 0;
	for (int32_t k = start; k < p->options->ndirs; k++) {
		const char *d = p->options->dirs[k];
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
		if ((f = pp_path_try(p->options, buf, l))) {
			*sdir = k;
			*path = pp_string_copy(p, buf, l);
			*used = (uint32_t)k + 1;
			return f;
		}
	}
	*used = (uint32_t)p->options->ndirs;	/* not in any of them */
	/* frameworks: <Fw/h.h> -> DIR/Fw.framework/Headers/h.h */
	if (angled && !next) {
		const char *sl = memchr(name, '/', nlen);

		for (int k = 0; sl && k < p->options->nfwdirs; k++) {
			int l = snprintf(buf, sizeof(buf), "%s/%.*s.framework/Headers/%.*s", p->options->fwdirs[k],
					 (int)(sl - name), name, (int)(nlen - (size_t)(sl - name) - 1), sl + 1);

			if (l > 0 && (size_t)l < sizeof(buf) && (f = pp_path_try(p->options, buf, (size_t)l))) {
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
	return e->hash == h && e->signature == sig && e->search_path_signature == fsig && e->mode == mode && e->key_length == kl &&
	       !memcmp(e->key, k, kl);
}

/* e's answer holds for p: the directories it looked at are p's */
static inline bool pp_include_answer_is_valid(const pp *p, const pp_include_answer *e)
{
	return e->used <= (uint32_t)p->options->ndirs && p->search_path_signatures[e->used] == e->used_signature && e->epoch == vcache_epoch();
}

/* the search path prefix the lookup depended on is an input of the open regions */
static inline void pp_include_search_prefix_record(pp *p, uint32_t used)
{
	if (unlikely(p->recording.on))
		for (int i = 0; i < p->recording.level_count; i++)
			if (p->recording.levels[i].ndirs < used)
				p->recording.levels[i].ndirs = used;
}

/* the lookup's mode and, for a quoted one, the includer's directory (*fp, *dl) */
static inline int32_t pp_include_lookup_mode(const pp *p, const pp_context *from, bool angled, bool next, const char **fp, size_t *dl)
{
	bool lex_character_next = next && from->search_directory >= 0;
	int32_t start = lex_character_next ? from->search_directory + 1 : angled ? p->options->nquote : 0;

	*fp = "", *dl = 0;
	if (!angled && !lex_character_next) {
		*fp = from->lex_file != &p->builtin_file ? from->spelled_path : "", *dl = from->lex_file != &p->builtin_file ? from->spelled_directory_length : 0;
		if (!*dl)
			*fp = "./", *dl = 2;	/* as pp_include_find_inner spells it (-include's too) */
	}
	return start * 4 + angled * 2 + lex_character_next;
}

/* is a header found in search directory sdir in a system directory (pp_include_system_kind without the includer's case) */
static inline uint8_t pp_include_directory_is_system(const pp *p, int32_t sdir)
{
	return sdir == -2 || (sdir >= 0 && p->options->nsys1 && sdir >= p->options->nsys1 - 1);
}

/* the innermost recording level made a lookup that depended on the first used search directories */
static void pp_include_lookup_record(pp *p, const pp_context *from, const char *name, size_t nlen, bool angled, bool next,
		       const lex_file *f, int32_t sdir, const char *path)
{
	struct pp_recording *r = &p->recording;
	pp_header_cache_lookup *l;

	if (r->lookup_count >= PP_HEADER_CACHE_LOOKUP_MAX || nlen >= 4096) {
		PP_HEADER_CACHE_REGION_REJECT(p);
		return;
	}
	l = &r->lookups[r->lookup_count++];
	*l = (pp_header_cache_lookup){ pp_string_copy(p, name, nlen), from ? from->spelled_path : NULL, f ? path : NULL, f, from ? from->search_directory : -1,
		      f ? sdir : -1, (uint32_t)nlen, from ? from->spelled_directory_length : 0, angled, next, from && from->lex_file == &p->builtin_file,
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
	if (unlikely(p->recording.on) && used)
		pp_include_lookup_record(p, from, name, nlen, angled, next, f, *sdir, *path);
	return f;
}

/* each of e's own lookups that depended on the search directories gives the same answer here */
VPRIV bool pp_header_cache_lookups_match(pp *p, const pp_header_cache_entry *e)
{
	for (uint32_t i = 0; i < e->lookup_count; i++) {
		const pp_header_cache_lookup *l = &e->lookups[i];
		pp_context fc;
		int32_t sd = -1;
		const char *path = NULL;
		uint32_t used;
		const lex_file *f;

		if (l->from) {
			memset(&fc, 0, sizeof(fc));
			fc.lex_file = l->from_builtin ? &p->builtin_file : NULL;
			fc.spelled_path = l->from, fc.spelled_directory_length = l->from_directory_length, fc.search_directory = l->from_search_directory;
		}
		f = pp_include_find_at(p, l->from ? &fc : NULL, l->name, l->name_length, l->angled, l->next, &sd, &path, NULL, &used);
		if (f != l->f)
			return false;
		if (f && (sd != l->search_directory || pp_include_directory_is_system(p, sd) != l->is_system || strcmp(path, l->path)))
			return false;
	}
	return true;
}

static const lex_file *pp_include_find_at(pp *p, const pp_context *from, const char *name, size_t nlen, bool angled,
					bool next, int32_t *sdir, const char **path, const pp_include_answer **ie, uint32_t *usedp)
{
	uint32_t used, i, sig = p->options_signature, fsig;
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
	fsig = p->search_path_signatures[p->options->ndirs];
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
	if (p->search_path_signatures[used] == PP_HEADER_CACHE_NONE || (!stale && atomic_fetch_add(&pp_include_answer_count, 1) >= (3u << PP_INCLUDE_ANSWER_TABLE_BITS) / 4))
		goto done;
	{
		size_t pl = f ? strlen(*path) : 0;
		pp_include_answer *m = pp_shared_allocate(sizeof(*m) + kp + pl + 1);

		m->hash = h, m->signature = sig, m->search_path_signature = fsig, m->key_length = (uint32_t)kl, m->mode = mode;
		m->used = used, m->used_signature = p->search_path_signatures[used], m->search_directory = *sdir, m->f = f;
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
	f = e->f, *sdir = e->search_directory, *path = e->path, used = e->used;
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
	const char *from = p->options->prefix_from;
	pp_file_entry *e = &p->files[p->file_count];

	e->spelled_path = name;
	if (from && !strncmp(name, from, strlen(from))) {
		const char *to = p->options->prefix_to ? p->options->prefix_to : "";
		size_t fl = strlen(from), tl = strlen(to);
		char *d = varena_alloc(&p->permanent_region, tl + nl - fl + 1);

		memcpy(d, to, tl);
		memcpy(d + tl, name + fl, nl - fl + 1);
		name = d;
		nl = tl + nl - fl;
	}
	e->location_base = p->next_location_base;
	e->diagnostic_state = p->diagnostic_current;
	e->lex_file = f;
	e->skeleton = c->skeleton;
	e->name = name;
	e->name_length = (uint32_t)nl;
	c->file_index = p->file_count++;
	c->location = e->location_base;
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

	for (uint32_t i = 0; i < p->file_count && !seen; i++)
		seen = p->files[i].lex_file == f;
	for (int i = 0; i < p->context_depth && seen; i++)
		if (p->contexts[i].type == PP_CONTEXT_FILE && p->contexts[i].lex_file == f)
			return false;
	return seen;
}

/* -H: ". path" with a dot per level, as clang (files -include names are not shown) */
static void pp_include_show(pp *p, const char *path)
{
	uint32_t d = 0;

	for (int i = 0; i < p->context_depth; i++) {
		if (p->contexts[i].type == PP_CONTEXT_FILE && p->contexts[i].lex_file == &p->builtin_file && i + 1 < p->context_depth - 1)
			return;	/* inside an -include'd file */
		d += p->contexts[i].type == PP_CONTEXT_FILE && p->contexts[i].lex_file != &p->builtin_file;
	}
	dprintf(p->options->errfd ? p->options->errfd : 2, "%.*s %s\n", (int)(d > 1 ? d - 1 : 1),
		"................................................................", path);
}

/* is a header found in search directory sdir a system header (-1: the includer's
 * directory): 1, 2 for an extern "C" one */
VPRIV uint8_t pp_include_system_kind(pp *p, int32_t sdir)
{
	const pp_context *from = pp_file_context_current(p);

	uint8_t s = from ? p->files[from->file_index].is_system : 0, d = 0;	/* clang: the includer's, if higher */

	if (sdir >= 0 && p->options->nsys1 && sdir >= p->options->nsys1 - 1)
		d = 1 + (sdir >= p->options->xc0 && sdir < p->options->xc1);	/* 2: extern "C" */
	else if (sdir == -2)
		d = 1;
	else if (sdir != -1)
		return 0;
	return d > s ? d : s;
}

VPRIV void pp_dependency_add(pp *p, const char *path, uint8_t sys)
{
	pp_file_entry *e = &p->files[p->file_count++];

	memset(e, 0, sizeof(*e));
	e->spelled_path = path;
	e->name = "";
	e->is_system = sys;
	e->location_base = p->next_location_base++;
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
	const pp_skeleton *fi = c->skeleton;
	const lex_token *vt = c->lexed_tokens;
	const char *cwd = p->options->cwd;
	size_t cl = cwd ? strlen(cwd) : 0;
	char buf[4096 + 16];

	if (cl + 1 >= 2048)
		return;
	if (cwd) {
		memcpy(buf, cwd, cl);
		buf[cl++] = '/';
	}
	for (uint32_t k = 0, depth = 0; k < fi->directive_count; k++) {
		const pp_directive_span *d = &fi->directives[k];
		uint32_t b = d->hash_token + 2, j;
		const char *name, *fp;
		size_t nl, dl;
		bool angled;
		int32_t mode, n = 0;

		if (d->kind == PP_DIRECTIVE_IF || d->kind == PP_DIRECTIVE_IFDEF || d->kind == PP_DIRECTIVE_IFNDEF)
			depth++;
		else if (d->kind == PP_DIRECTIVE_ENDIF && depth)
			depth--;
		/* unconditional ones only (inside the include guard): others may not be wanted */
		if ((d->kind != PP_DIRECTIVE_INCLUDE && d->kind != PP_DIRECTIVE_INCLUDE_NEXT && d->kind != PP_DIRECTIVE_IMPORT) || b >= d->end_token ||
		    depth != (fi->guard_macro ? 1u : 0u))
			continue;
		if (vt[b].kind == LEX_TOKEN_STRING && !(vt[b].flags & LEX_TOKEN_FLAG_SPLICED) && vt[b].len > 2) {
			name = c->source + vt[b].off + 1;
			nl = vt[b].len - 2;
			angled = false;
		} else if (vt[b].kind == LEX_TOKEN_LESS) {
			for (j = b + 1; j < d->end_token && vt[j].kind != LEX_TOKEN_GREATER; j++)
				;
			if (j == d->end_token)
				continue;
			name = c->source + vt[b].off + 1;
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
		for (int32_t i = mode >> 2; i < p->options->ndirs && n < 1; i++) {
			const char *dd = p->options->dirs[i];
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
	if (fi->guard_macro && pp_macro_guard_is_defined(p, fi->guard_macro) && !(unlikely(p->options->show_includes || p->output_text_mode) && !pp_include_guard_is_known(p, f))) {
		if (p->options->deps)
			pp_dependency_add(p, path, sys);
		return;
	}
	oi = pp_once_file_find(p, f);
	if (unlikely(p->recording.on) && p->recording.once_mark_count < PP_HEADER_CACHE_ONCE_MARK_MAX &&
	    (oi < 0 || oi < (int)p->recording.levels[p->recording.level_count - 1].once_files_at_start))
		p->recording.once_marks[p->recording.once_mark_count++] = (pp_header_cache_once_mark){ f, oi >= 0 };
	if (oi >= 0) {
		if (p->options->deps)
			pp_dependency_add(p, path, sys);
		return;
	}
	if (unlikely(p->output_text_mode) && f != &p->builtin_file)
		pp_output_file_change(p, from, 1);
	if (f != &p->builtin_file && from && pp_header_cache_region_is_allowed(p)) {
		int d0 = p->context_depth;
		uint32_t b0 = p->next_location_base;
		unsigned c0 = p->counter_value;

		k = pp_header_cache_key_get(p->options_signature * 2 + (p->output_mode == 2), f, sdir, 0, path, sys, true);
		/* top-level: included from the main file or <built-in> (-include) */
		bool top = p->output_mode == 2 && (p->file_context_count == 1 || (p->file_context_count == 2 && from->lex_file == &p->builtin_file));

		if (k && pp_header_cache_entry_serve(p, k)) {
			if (unlikely(pp_coverage_enabled) && p->output_mode == 2)
				pp_coverage_hit_begin(p, d0);
			if (top) {
				pp_top_event_record(p, p->header_cache_last_entry, b0, c0, 0, 1);
				if (p->context_depth > d0)
					p->contexts[d0].top_level = 2;	/* its end comes when the replay does */
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
	c->lexed_tokens = f->tok;
	c->count = f->ntok;
	c->source = f->src;
	c->lex_file = f;
	c->skeleton = fi;
	c->next_directive = 0;
	c->search_directory = sdir;
	c->spelled_path = path;
	{
		const char *sl = strrchr(path, '/');

		c->spelled_directory_length = sl ? (uint32_t)(sl - path) + 1 : 0;
	}
	p->file_context_count++;
	if (unlikely(p->options->show_includes) && from && f != &p->builtin_file && from->lex_file != &p->builtin_file)
		pp_include_show(p, path);
	pp_file_entry_add(p, c, f, path);
	p->files[c->file_index].comment_position = f->src;
	p->files[c->file_index].is_system = sys;
	if (unlikely(p->markers_on) && f != &p->builtin_file)
		pp_line_marker_file_enter(p, &p->files[c->file_index]);
	p->files[c->file_index].included_from = from && from->lex_file != &p->builtin_file && from->index ? from->location + from->lexed_tokens[from->index - 1].off : 0;
	if (unlikely(fi->unbalanced)) {
		/* clang's message at the directive; the file is not processed */
		const lex_token *t = &f->tok[fi->unbalanced_token];

		if (fi->unbalanced == 2)
			PP_ERROR(p, p->files[c->file_index].location_base + t->off, "unterminated conditional directive");
		else
			PP_ERROR(p, p->files[c->file_index].location_base + t->off, "#%.*s without #if", (int)t->len, f->src + t->off);
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
	int d = p->context_depth;
	ptok t;
	const pp_include_answer *ie;
	/* "x" or <x> as written: the name is the site's, so is the answer while the options and directory are */
	_Atomic(const pp_include_answer *) *site = b < e && (c->lexed_tokens[b].kind == LEX_TOKEN_STRING || c->lexed_tokens[b].kind == LEX_TOKEN_LESS) &&
						p->options_signature != PP_HEADER_CACHE_NONE
					    ? &((pp_skeleton *)c->skeleton)->include_answers[c->next_directive - 1] : NULL;

	if (b < e && (c->lexed_tokens[b].kind == LEX_TOKEN_STRING || c->lexed_tokens[b].kind == LEX_TOKEN_LESS)) {	/* -Wextra-tokens */
		uint32_t i = b + 1;

		if (c->lexed_tokens[b].kind == LEX_TOKEN_LESS)
			while (i < e && c->lexed_tokens[i - 1].kind != LEX_TOKEN_GREATER)
				i++;
		if (unlikely(i < e && c->lexed_tokens[i - 1].kind != LEX_TOKEN_LESS))
			pp_directive_extra_token_warn(p, c, b - 1, i);
	}
	if (site && (ie = atomic_load_explicit(site, memory_order_acquire))) {
		const char *fp;
		size_t dl;
		int32_t mode = pp_include_lookup_mode(p, c, c->lexed_tokens[b].kind == LEX_TOKEN_LESS, kind == PP_DIRECTIVE_INCLUDE_NEXT, &fp, &dl);

		if (ie->signature == p->options_signature && ie->mode == mode && pp_include_answer_is_valid(p, ie) && dl < ie->key_length && !ie->key[dl] &&
		    !memcmp(ie->key, fp, dl)) {
			pp_include_search_prefix_record(p, ie->used);
			if (unlikely(p->recording.on) && ie->used)
				pp_include_lookup_record(p, c, ie->key + dl + 1, ie->key_length - dl - 1, c->lexed_tokens[b].kind == LEX_TOKEN_LESS,
					   kind == PP_DIRECTIVE_INCLUDE_NEXT, ie->f, ie->search_directory, ie->path);
			pp_file_enter(p, ie->f, ie->path, ie->search_directory);
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
		if (p->fatal_jump && !p->options->fatal_continue)
			longjmp(*p->fatal_jump, 1);
		return;
	}
	if (site && ie)
		atomic_store_explicit(site, ie, memory_order_release);
	pp_file_enter(p, f, path, sd);
}
