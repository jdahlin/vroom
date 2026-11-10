/* vcc preprocessor: include search, entering files, #include */
#include "ppimpl.h"

static char *pstrdup(pp *p, const char *s, size_t n)
{
	char *d = varena_alloc(&p->perm, n + 1);

	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

_Atomic unsigned long pp_stat_try;
_Atomic unsigned long pp_stat_skip;	/* VPP_TIME */

/*
 * The file at a candidate path, if any: misses in missing directories cost
 * no syscall.  A directory is looked at once per process (dircache.c), so
 * it must not appear during a build: kbuild and meson both create every
 * build-tree directory an include search can look in before compiling
 * (PostgreSQL: all of them exist right after `meson setup`).
 */
static const vlex_file *try_path_(const char *cwd, const char *path, size_t len)
{
	static _Atomic int off_ = -1;
	int off = atomic_load_explicit(&off_, memory_order_relaxed);

	if (off < 0)
		atomic_store_explicit(&off_, off = getenv("VPP_NODIRCACHE") != NULL, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_stat_try, 1, memory_order_relaxed);
	const vlex_file *f;

	if (!off && (!cwd || path[0] == '/') && !vdir_exists(path, len)) {
		atomic_fetch_add_explicit(&pp_stat_skip, 1, memory_order_relaxed);
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
		if (!off && !vdir_exists(abs, cl + 1 + len)) {
			atomic_fetch_add_explicit(&pp_stat_skip, 1, memory_order_relaxed);
			return NULL;
		}
		return vlex_get(abs, cl + 1 + len);
	}
	f = vlex_get(path, len);
	return f;
}

/* trigraph mode reads a private, translated copy (vlex_trigraphs) */
VPRIV const vlex_file *try_path(const pp_opts *o, const char *path, size_t len)
{
	const vlex_file *f = try_path_(o->cwd, path, len);

	if (f && o->trigraphs)
		f = vlex_trigraphs(f);
	if (f && o->c89)
		f = vlex_c89(f);
	return f && o->c23 ? vlex_digitsep(f) : f;
}

static const vlex_file *find_include_(pp *p, const ctx *from, const char *name, size_t nlen,
				      bool angled, bool next, int32_t *sdir, const char **path, uint32_t *used)
{
	char buf[4096];
	const vlex_file *f;
	int32_t start;

	*sdir = -1;
	*used = 0;
	if (nlen + 2 >= sizeof(buf))
		return NULL;
	if (name[0] == '/') {
		memcpy(buf, name, nlen);
		buf[nlen] = 0;
		*path = pstrdup(p, buf, nlen);
		return try_path(p->o, buf, nlen);
	}
	if (next && from && from->sdir < 0)
		next = false;	/* #include_next outside the search path: a normal search */
	if (!angled && !next) {
		const char *fp = from && from->f != &p->builtin ? from->spath : "";
		const char *sl = strrchr(fp, '/');
		size_t dl = sl ? (size_t)(sl - fp) + 1 : 0;

		if (!sl && fp[0])
			fp = "./", dl = 2;	/* clang: the includer's directory, "." spelled out */
		if (dl + nlen + 1 < sizeof(buf)) {
			memcpy(buf, fp, dl);
			memcpy(buf + dl, name, nlen);
			buf[dl + nlen] = 0;
			if ((f = try_path(p->o, buf, dl + nlen))) {
				*path = pstrdup(p, buf, dl + nlen);
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
		if ((f = try_path(p->o, buf, l))) {
			*sdir = k;
			*path = pstrdup(p, buf, l);
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

			if (l > 0 && (size_t)l < sizeof(buf) && (f = try_path(p->o, buf, (size_t)l))) {
				*path = pstrdup(p, buf, (size_t)l);
				*sdir = -2;
				return f;
			}
		}
	}
	return NULL;
}

#define INC_BITS 17
static _Atomic(incent *) inc_tab[1u << INC_BITS];
static _Atomic uint32_t inc_n;

/* a new build: forget every include answer (pp.h) */
void pp_include_flush(void)
{
	for (size_t i = 0; i < (1u << INC_BITS); i++)
		atomic_store_explicit(&inc_tab[i], NULL, memory_order_release);
	atomic_store(&inc_n, 0);
}

static inline bool inc_same(const incent *e, uint64_t h, uint32_t sig, uint32_t fsig, int32_t mode, const char *k,
			    size_t kl)
{
	return e->hash == h && e->sig == sig && e->fsig == fsig && e->mode == mode && e->klen == kl &&
	       !memcmp(e->key, k, kl);
}

/* e's answer holds for p: the directories it looked at are p's */
static inline bool inc_valid(const pp *p, const incent *e)
{
	return e->used <= (uint32_t)p->o->ndirs && p->psig[e->used] == e->usig;
}

/* the search path prefix the lookup depended on is an input of the open regions */
static inline void inc_used(pp *p, uint32_t used)
{
	if (unlikely(p->rec.on))
		for (int i = 0; i < p->rec.n; i++)
			if (p->rec.lev[i].ndirs < used)
				p->rec.lev[i].ndirs = used;
}

/* the lookup's mode and, for a quoted one, the includer's directory (*fp, *dl) */
static inline int32_t inc_mode(const pp *p, const ctx *from, bool angled, bool next, const char **fp, size_t *dl)
{
	bool nx = next && from->sdir >= 0;
	int32_t start = nx ? from->sdir + 1 : angled ? p->o->nquote : 0;

	*fp = "", *dl = 0;
	if (!angled && !nx && from->f != &p->builtin)
		*fp = from->spath, *dl = from->sdl;
	return start * 4 + angled * 2 + nx;
}

/* *ie: the cache entry of the answer, if it has one */
VPRIV const vlex_file *find_include(pp *p, const ctx *from, const char *name, size_t nlen,
				     bool angled, bool next, int32_t *sdir, const char **path, const incent **ie)
{
	uint32_t used, i, sig = p->sig, fsig;
	const vlex_file *f;
	char kb[4096 + 16];
	const char *fp;
	size_t dl, kl, kp;
	int32_t mode;
	incent *e;
	uint64_t h, w;

	if (ie)
		*ie = NULL;
	if (name[0] == '/' || sig == HNONE || !from || nlen >= 2048)
		goto slow;
	mode = inc_mode(p, from, angled, next, &fp, &dl);
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
	for (i = (uint32_t)(h >> (64 - INC_BITS));; i = (i + 1) & ((1u << INC_BITS) - 1)) {
		e = atomic_load_explicit(&inc_tab[i], memory_order_acquire);
		if (!e)
			break;
		if (inc_same(e, h, sig, fsig, mode, kb, kl) && inc_valid(p, e))
			goto hit;
	}
	f = find_include_(p, from, name, nlen, angled, next, sdir, path, &used);
	if (p->psig[used] == HNONE || atomic_fetch_add(&inc_n, 1) >= (3u << INC_BITS) / 4)
		goto done;
	{
		size_t pl = f ? strlen(*path) : 0;
		incent *m = salloc(sizeof(*m) + kp + pl + 1);

		m->hash = h, m->sig = sig, m->fsig = fsig, m->klen = (uint32_t)kl, m->mode = mode;
		m->used = used, m->usig = p->psig[used], m->sdir = *sdir, m->f = f;
		memcpy(m->key, kb, kp);
		m->path = NULL;
		if (f) {
			memcpy(m->key + kp, *path, pl + 1);
			m->path = m->key + kp;
		}
		for (;; i = (i + 1) & ((1u << INC_BITS) - 1)) {
			e = NULL;
			if (atomic_compare_exchange_strong_explicit(&inc_tab[i], &e, m, memory_order_acq_rel,
								    memory_order_acquire)) {
				e = m;
				break;
			}
			if (inc_same(e, h, sig, fsig, mode, kb, kl) && inc_valid(p, e))
				break;	/* another thread's: the same answer */
		}
	}
hit:
	f = e->f, *sdir = e->sdir, *path = e->path, used = e->used;
	if (ie)
		*ie = e;
	goto done;
slow:
	f = find_include_(p, from, name, nlen, angled, next, sdir, path, &used);
done:
	inc_used(p, used);
	return f;
}

static void add_fent(pp *p, ctx *c, const vlex_file *f, const char *name)
{
	size_t nl = strlen(name);
	const char *from = p->o->prefix_from;
	fent *e = &p->fe[p->nfe];

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
	e->base = p->next_base;
	e->dstate = p->dcur;
	e->f = f;
	e->fi = c->fi;
	e->name = name;
	e->namelen = (uint32_t)nl;
	c->fe = p->nfe++;
	c->loc = e->base;
	p->next_base += f->size + 1;
}

/*
 * Read-ahead for a file entered for the first time: the first candidate of
 * each unconditional #include "x" / <x> (the includer's directory, or the
 * first search directory that exists) goes to the token cache's I/O
 * threads, so that opening and reading it is off this thread by the time
 * the lookup gets there.  The lookup tries that path first anyway: no extra
 * I/O.  VPP_NOPREFETCH=1 turns it off.
 */
static int pf_on = -1;

static void prefetch_includes(pp *p, const ctx *c)
{
	const pfile *fi = c->fi;
	const vtok *vt = c->vt;
	const char *cwd = p->o->cwd;
	size_t cl = cwd ? strlen(cwd) : 0;
	char buf[4096 + 16];

	if (cl + 1 >= 2048)
		return;
	if (cwd) {
		memcpy(buf, cwd, cl);
		buf[cl++] = '/';
	}
	for (uint32_t k = 0, depth = 0; k < fi->ndir; k++) {
		const pdir *d = &fi->dir[k];
		uint32_t b = d->tok + 2, j;
		const char *name, *fp;
		size_t nl, dl;
		bool angled;
		int32_t mode, n = 0;

		if (d->kind == D_IF || d->kind == D_IFDEF || d->kind == D_IFNDEF)
			depth++;
		else if (d->kind == D_ENDIF && depth)
			depth--;
		/* unconditional ones only (inside the include guard): others may not be wanted */
		if ((d->kind != D_INCLUDE && d->kind != D_INCLUDE_NEXT && d->kind != D_IMPORT) || b >= d->end ||
		    depth != (fi->guard ? 1u : 0u))
			continue;
		if (vt[b].kind == T_STRING && !(vt[b].flags & TF_DIRTY) && vt[b].len > 2) {
			name = c->src + vt[b].off + 1;
			nl = vt[b].len - 2;
			angled = false;
		} else if (vt[b].kind == T_LESS) {
			for (j = b + 1; j < d->end && vt[j].kind != T_GREATER; j++)
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
		mode = inc_mode(p, c, angled, d->kind == D_INCLUDE_NEXT, &fp, &dl);
		if (!angled && !(mode & 1)) {
			size_t o = cwd && fp[0] != '/' ? cl : 0;

			if (o + dl + nl < 4096) {
				memcpy(buf + o, fp, dl);
				memcpy(buf + o + dl, name, nl);
				buf[o + dl + nl] = 0;
				if (vdir_exists(buf, o + dl + nl)) {
					vlex_prefetch(buf, o + dl + nl);
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
			if (vdir_exists(buf, o + l + nl)) {
				vlex_prefetch(buf, o + l + nl);
				n++;
			}
		}
	}
}

VPRIV void enter_file(pp *p, const vlex_file *f, const char *path, int32_t sdir)
{
	bool fresh;
	const pfile *fi = pfile_get(f, &fresh);
	const ctx *from = cur_file(p);
	hkey *k = NULL;
	uint8_t sys;
	int oi;
	ctx *c;

	if (fi->guard && mguard(p, fi->guard))
		return;
	oi = once_index(p, f);
	if (unlikely(p->rec.on) && p->rec.nord < HC_MAXORD &&
	    (oi < 0 || oi < (int)p->rec.lev[p->rec.n - 1].nonce0))
		p->rec.ord[p->rec.nord++] = (honce){ f, oi >= 0 };
	if (oi >= 0)
		return;
	sys = sdir == -2 || (sdir >= 0 && p->o->nsys && sdir >= p->o->nsys) ||
	      (sdir == -1 && from && p->fe[from->fe].sys);
	if (f != &p->builtin && from && hc_top(p)) {
		int d0 = p->depth;
		uint32_t b0 = p->next_base;
		unsigned c0 = p->counter;

		k = hkey_get(p->sig * 2 + (p->tokmode == 2), f, sdir, 0, path, sys, true);
		/* top-level: included from the main file or <built-in> (-include) */
		bool top = p->tokmode == 2 && (p->nfile == 1 || (p->nfile == 2 && from->f == &p->builtin));

		if (k && hc_try(p, k)) {
			if (top) {
				top_event(p, p->hc_last, b0, c0, 0, 1);
				if (p->depth > d0)
					p->cx[d0].top = 2;	/* its end comes when the replay does */
				else
					top_event(p, p->hc_last, 0, 0, 1, 1);
			}
			return;
		}
		if (k && top) {
			top_event(p, NULL, b0, c0, 0, 0);
			p->top_rec = 1;
		}
	}
	c = push(p);
	c->type = C_FILE;
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

		c->sdl = sl ? (uint32_t)(sl - path) + 1 : 0;
	}
	p->nfile++;
	add_fent(p, c, f, path);
	p->fe[c->fe].sys = sys;
	p->fe[c->fe].from = from && from->f != &p->builtin && from->i ? from->loc + from->vt[from->i - 1].off : 0;
	if (unlikely(fi->bad)) {
		/* clang's message at the directive; the file is not processed */
		const vtok *t = &f->tok[fi->badtok];

		if (fi->bad == 2)
			error(p, p->fe[c->fe].base + t->off, "unterminated conditional directive");
		else
			error(p, p->fe[c->fe].base + t->off, "#%.*s without #if", (int)t->len, f->src + t->off);
		if (p->fatal_jb)
			longjmp(*p->fatal_jb, 1);
	}
	if (fresh) {
		if (pf_on < 0)
			pf_on = !getenv("VPP_NOPREFETCH");
		if (pf_on)
			prefetch_includes(p, c);
	}
	if (k)
		hc_begin(p, k);
}

VPRIV void include(pp *p, ctx *c, uint32_t b, uint32_t e, int kind, uint32_t loc)
{
	char buf[4096];
	size_t n;
	bool angled;
	int32_t sd;
	const char *path;
	const vlex_file *f;
	uint32_t floc;
	int d = p->depth;
	ptok t;
	const incent *ie;
	/* "x" or <x> as written: the name is the site's, so is the answer while the options and directory are */
	_Atomic(const incent *) *site = b < e && (c->vt[b].kind == T_STRING || c->vt[b].kind == T_LESS) &&
						p->sig != HNONE
					    ? &((pfile *)c->fi)->inc[c->di - 1] : NULL;

	if (site && (ie = atomic_load_explicit(site, memory_order_acquire))) {
		const char *fp;
		size_t dl;
		int32_t mode = inc_mode(p, c, c->vt[b].kind == T_LESS, kind == D_INCLUDE_NEXT, &fp, &dl);

		if (ie->sig == p->sig && ie->mode == mode && inc_valid(p, ie) && dl < ie->klen && !ie->key[dl] &&
		    !memcmp(ie->key, fp, dl)) {
			inc_used(p, ie->used);
			enter_file(p, ie->f, ie->path, ie->sdir);
			return;
		}
	}
	push_line(p, c, b, e);
	raw(p, &t);
	floc = t.loc;
	if (t.kind == T_STRING || t.kind == T_LESS || t.kind == T_IDENT) {
		bool ok;

		p->in_dir++;
		ok = header_name(p, buf, sizeof(buf), &n, &angled, &t);
		p->in_dir--;
		if (!ok) {
			pop_to(p, d);
			error(p, loc, "expected \"FILENAME\" or <FILENAME>");
			return;
		}
	} else {
		pop_to(p, d);
		error(p, loc, "expected \"FILENAME\" or <FILENAME>");
		return;
	}
	pop_to(p, d);
	f = find_include(p, c, buf, n, angled, kind == D_INCLUDE_NEXT, &sd, &path, &ie);
	if (!f) {
		pp_span r = { floc, floc + (uint32_t)n + 2 };
		pp_dinfo di = { "fatal error", floc, &r, 1, NULL, 0, NULL, 0 };

		pp_diagx(p, &di, "'%.*s' file not found", (int)n, buf);
		if (p->fatal_jb && !p->o->fatal_continue)
			longjmp(*p->fatal_jb, 1);
		return;
	}
	if (site && ie)
		atomic_store_explicit(site, ie, memory_order_release);
	enter_file(p, f, path, sd);
}
