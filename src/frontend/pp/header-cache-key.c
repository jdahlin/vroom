/* vcc preprocessor: header cache: option signatures, the key table and the #pragma once set */
#include "pp-internal.h"

#define PP_HEADER_CACHE_KEY_TABLE_BITS 16
static _Atomic(pp_header_cache_key *) pp_header_cache_key_table[1u << PP_HEADER_CACHE_KEY_TABLE_BITS];
size_t pp_header_cache_memory_limit;

/*
 * Signatures.  The base signature is every option that decides what a
 * header does except the macros and the search path; the search path is an
 * input of a region only as far as its lookups went: an entry records how
 * many leading search directories its #include / __has_include lookups
 * depended on (up to the one a file was found in; all of them for a
 * lookup that failed), and matches TUs whose search path starts with the
 * same directories.  (kbuild adds -I for each source directory at the end:
 * that does not split the cache.)
 */
static pthread_mutex_t pp_signature_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *pp_signature_strings[1 << 16];
static size_t pp_signature_lengths[1 << 16];
static uint32_t pp_signature_count;

static uint32_t pp_header_cache_signature_intern(char *b, size_t n)
{
	uint32_t r = PP_HEADER_CACHE_NONE;

	pthread_mutex_lock(&pp_signature_mutex);
	for (uint32_t i = 0; i < pp_signature_count; i++)
		if (pp_signature_lengths[i] == n && !memcmp(pp_signature_strings[i], b, n)) {
			r = i;
			break;
		}
	if (r == PP_HEADER_CACHE_NONE && pp_signature_count < (1 << 16)) {
		char *c = malloc(n ? n : 1);

		memcpy(c, b, n);
		pp_signature_strings[pp_signature_count] = c;
		pp_signature_lengths[pp_signature_count] = n;
		r = pp_signature_count++;
	}
	pthread_mutex_unlock(&pp_signature_mutex);
	return r;
}

/* base signature into *sig, prefix ids into psig[0..ndirs]; false if too long */
VPRIV bool pp_header_cache_signatures_get(const pp_opts *o, uint32_t *sig, uint32_t *psig)
{
	char *b = malloc(1 << 17);
	size_t n = 0;
	bool ok = true;

#define SIGS(x) do { const char *s_ = (x) ? (x) : ""; size_t l_ = strlen(s_); \
	if (n + l_ + 8 < (1 << 17)) { memcpy(b + n, s_, l_); n += l_; b[n++] = 0; } else ok = false; } while (0)
	n += (size_t)snprintf(b, 64, "%d %d %d %d %d %d %d %d %d", o->nfwdirs, o->unsigned_char, o->asm_mode, o->target,
			      o->nwargs, o->error_limit, o->trigraphs, o->no_digraphs, o->deps * 8 + o->c89 * 4 + o->c23 * 2 + o->fatal_continue) + 1;
	for (int i = 0; i < o->nwargs; i++)	/* -W/-Werror decide which replayed diagnostics are errors */
		SIGS(o->wargs[i]);
	for (int i = 0; i < o->nfwdirs; i++)
		SIGS(o->fwdirs[i]);
	SIGS(o->cwd);
	SIGS(o->prefix_from);
	SIGS(o->prefix_to);
	*sig = pp_header_cache_signature_intern(b, n);
	n = 0;
	psig[0] = pp_header_cache_signature_intern(b, 0);
	for (int i = 0; i < o->ndirs && ok; i++) {
		/* each directory with whether it is a quote-only and a system directory */
		b[n++] = (char)('0' + (i < o->nquote) + 2 * (o->nsys1 && i >= o->nsys1 - 1));
		SIGS(o->dirs[i]);
		psig[i + 1] = pp_header_cache_signature_intern(b, n);
	}
#undef SIGS
	free(b);
	return ok && *sig != PP_HEADER_CACHE_NONE;
}

VPRIV pp_header_cache_key *pp_header_cache_key_get(uint32_t sig, const lex_file *f, int32_t sdir, uint32_t nfile, const char *spath, uint8_t sys,
		     bool make)
{
	size_t sl = strlen(spath);
	uint64_t h = ((uint64_t)(uintptr_t)f * 0x9E3779B97F4A7C15ull) ^
		     ((uint64_t)sig << 40 | (uint64_t)(uint32_t)sdir << 16 | (uint64_t)sys << 15 | nfile);
	pp_header_cache_key *mine = NULL;

	h = (h ^ vintern_hash(spath, sl)) * 0xff51afd7ed558ccdull;
	h ^= h >> 29;
	for (uint32_t i = (uint32_t)(h >> (64 - PP_HEADER_CACHE_KEY_TABLE_BITS)), k = 0; k < (1u << PP_HEADER_CACHE_KEY_TABLE_BITS);
	     i = (i + 1) & ((1u << PP_HEADER_CACHE_KEY_TABLE_BITS) - 1), k++) {
		pp_header_cache_key *e = atomic_load_explicit(&pp_header_cache_key_table[i], memory_order_acquire);

		if (!e) {
			if (!make)
				return NULL;
			if (!mine) {
				char *sp = pp_shared_allocate(sl + 1);

				memcpy(sp, spath, sl + 1);
				mine = pp_shared_allocate(sizeof(*mine));
				mine->hash = h;
				mine->f = f;
				mine->spath = sp;
				mine->sig = sig;
				mine->nfile = nfile;
				mine->sdir = sdir;
				mine->sys = sys;
			}
			if (atomic_compare_exchange_strong_explicit(&pp_header_cache_key_table[i], &e, mine, memory_order_acq_rel,
								    memory_order_acquire))
				return mine;
		}
		if (e->hash == h && e->f == f && e->sig == sig && e->sdir == sdir && e->nfile == nfile && e->sys == sys &&
		    !strcmp(e->spath, spath))
			return e;
	}
	return NULL;
}

VPRIV int pp_once_file_find(const pp *p, const lex_file *f)
{
	/* the same file by any path (symlink, hard link, a/../a): clang's FileEntry */
	for (uint32_t i = 0; i < p->nonce; i++)
		if (p->once[i] == f || (f->ino && p->once[i]->ino == f->ino && p->once[i]->dev == f->dev))
			return (int)i;
	return -1;
}

VPRIV void pp_once_file_add(pp *p, const lex_file *f)
{
	if (p->nonce == p->once_file_capacity) {
		const lex_file **n = varena_alloc(&p->perm, 2 * p->once_file_capacity * sizeof(*n));

		memcpy(n, p->once, p->nonce * sizeof(*n));
		p->once = n;
		p->once_file_capacity *= 2;
	}
	p->once[p->nonce++] = f;
}
