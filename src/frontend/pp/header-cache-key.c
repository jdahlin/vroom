/* vcc preprocessor: header cache: option signatures, the key table and the #pragma once set */
#include "ppimpl.h"

#define HKEY_BITS 16
static _Atomic(hkey *) hkey_tab[1u << HKEY_BITS];
size_t hc_mem_max;

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
static pthread_mutex_t sig_mu = PTHREAD_MUTEX_INITIALIZER;
static char *sig_str[1 << 16];
static size_t sig_len[1 << 16];
static uint32_t nsig;

static uint32_t sig_intern(char *b, size_t n)
{
	uint32_t r = HNONE;

	pthread_mutex_lock(&sig_mu);
	for (uint32_t i = 0; i < nsig; i++)
		if (sig_len[i] == n && !memcmp(sig_str[i], b, n)) {
			r = i;
			break;
		}
	if (r == HNONE && nsig < (1 << 16)) {
		char *c = malloc(n ? n : 1);

		memcpy(c, b, n);
		sig_str[nsig] = c;
		sig_len[nsig] = n;
		r = nsig++;
	}
	pthread_mutex_unlock(&sig_mu);
	return r;
}

/* base signature into *sig, prefix ids into psig[0..ndirs]; false if too long */
VPRIV bool sig_of(const pp_opts *o, uint32_t *sig, uint32_t *psig)
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
	*sig = sig_intern(b, n);
	n = 0;
	psig[0] = sig_intern(b, 0);
	for (int i = 0; i < o->ndirs && ok; i++) {
		/* each directory with whether it is a quote-only and a system directory */
		b[n++] = (char)('0' + (i < o->nquote) + 2 * (o->nsys1 && i >= o->nsys1 - 1));
		SIGS(o->dirs[i]);
		psig[i + 1] = sig_intern(b, n);
	}
#undef SIGS
	free(b);
	return ok && *sig != HNONE;
}

VPRIV hkey *hkey_get(uint32_t sig, const vlex_file *f, int32_t sdir, uint32_t nfile, const char *spath, uint8_t sys,
		     bool make)
{
	size_t sl = strlen(spath);
	uint64_t h = ((uint64_t)(uintptr_t)f * 0x9E3779B97F4A7C15ull) ^
		     ((uint64_t)sig << 40 | (uint64_t)(uint32_t)sdir << 16 | (uint64_t)sys << 15 | nfile);
	hkey *mine = NULL;

	h = (h ^ vintern_hash(spath, sl)) * 0xff51afd7ed558ccdull;
	h ^= h >> 29;
	for (uint32_t i = (uint32_t)(h >> (64 - HKEY_BITS)), k = 0; k < (1u << HKEY_BITS);
	     i = (i + 1) & ((1u << HKEY_BITS) - 1), k++) {
		hkey *e = atomic_load_explicit(&hkey_tab[i], memory_order_acquire);

		if (!e) {
			if (!make)
				return NULL;
			if (!mine) {
				char *sp = salloc(sl + 1);

				memcpy(sp, spath, sl + 1);
				mine = salloc(sizeof(*mine));
				mine->hash = h;
				mine->f = f;
				mine->spath = sp;
				mine->sig = sig;
				mine->nfile = nfile;
				mine->sdir = sdir;
				mine->sys = sys;
			}
			if (atomic_compare_exchange_strong_explicit(&hkey_tab[i], &e, mine, memory_order_acq_rel,
								    memory_order_acquire))
				return mine;
		}
		if (e->hash == h && e->f == f && e->sig == sig && e->sdir == sdir && e->nfile == nfile && e->sys == sys &&
		    !strcmp(e->spath, spath))
			return e;
	}
	return NULL;
}

VPRIV int once_index(const pp *p, const vlex_file *f)
{
	/* the same file by any path (symlink, hard link, a/../a): clang's FileEntry */
	for (uint32_t i = 0; i < p->nonce; i++)
		if (p->once[i] == f || (f->ino && p->once[i]->ino == f->ino && p->once[i]->dev == f->dev))
			return (int)i;
	return -1;
}

VPRIV void once_add(pp *p, const vlex_file *f)
{
	if (p->nonce == p->oncecap) {
		const vlex_file **n = varena_alloc(&p->perm, 2 * p->oncecap * sizeof(*n));

		memcpy(n, p->once, p->nonce * sizeof(*n));
		p->once = n;
		p->oncecap *= 2;
	}
	p->once[p->nonce++] = f;
}
