/* vcc preprocessor: header cache: token entries (replay, record, publish) */
#include "ppimpl.h"

/* ---------------- header cache: token entries (pp_next) ---------------- */

static void push_replay(pp *p, const hent *e, uint32_t base, unsigned cbase, uint8_t top, uint8_t norec)
{
	ctx *c = push(p);

	c->type = C_REPLAY;
	c->ht = e->tk;
	c->pt = e->xtk;
	c->src = shared_base0();
	c->n = (uint32_t)e->ntext;
	c->loc = base;
	c->he = e;
	c->si = c->ci = 0;
	c->cbase = cbase;
	c->top = top;
	c->norec = norec;
}

/* a child entry's tokens go before the next own token: pushed */
VPRIV bool replay_seg(pp *p, ctx *c)
{
	const hent *e = c->he;
	const hseg *g;

	if (c->si >= e->nseg || e->seg[c->si].at != c->i)
		return false;
	g = &e->seg[c->si++];
	push_replay(p, g->child, c->loc + g->lbase, c->cbase + g->cdelta, 0, c->norec);
	return true;
}

/* __COUNTER__ digits in the spelling of own token k: this TU's values */
static __attribute__((noinline)) void ctr_spell(pp *p, const hent *e, uint32_t *ci, uint32_t k, unsigned cbase, ptok *t)
{
	char *b = varena_alloc(&p->perm, t->len + 24 * 4 + 32), *w = b;
	uint32_t at = 0;

	while (*ci < e->nctr && e->ctr[*ci].off == k) {
		const hctr *h = &e->ctr[(*ci)++];
		uint32_t bo = h->len >> 16, bl = h->len & 0xffff;

		memcpy(w, t->s + at, bo - at);
		w += bo - at;
		w += utoa(w, cbase + h->rel);
		at = bo + bl;
	}
	memcpy(w, t->s + at, t->len - at);
	w += t->len - at;
	memset(w, 0, 16);
	t->s = b;
	t->len = (uint32_t)(w - b);
	if (t->kind == T_IDENT)
		t->id = vintern_copy(b, t->len);
}

VPRIV void replay_tok(pp *p, ctx *c, ptok *t)
{
	const hent *e = c->he;

	htok_get(t, &c->ht[c->i], c->pt, c->src, c->loc);
	t->ctr = 0;
	p->rep_norec = c->norec;
	if (t->kind == T_IDENT)
		t->flags |= PF_NOEXP;	/* handed out as recorded: expanded already */
	if (unlikely(c->ci < e->nctr && e->ctr[c->ci].off == c->i))
		ctr_spell(p, e, &c->ci, c->i, c->cbase, t);
	c->i++;
}

/* ---------------- lazy bodies (pp.h) ---------------- */

int pp_lazy_skip(pp *p, const ptok *lb, pp_lazy *z)
{
	ctx *c = &p->cx[p->depth - 1];
	const hent *e;
	uint32_t k, lo, hi;

	if (c->type != C_REPLAY || !c->i || lb->kind != T_LBRACE || p->pend || (p->rec.on && !c->norec))
		return 0;
	e = c->he;
	k = c->i - 1;
	if (e->tk[k].kind != T_LBRACE || !e->tk[k].loc || lb->loc != c->loc + e->tk[k].loc - 1)
		return 0;
	lo = 0, hi = e->nbr;
	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;

		if (e->br[2 * m] < k)
			lo = m + 1;
		else
			hi = m;
	}
	if (lo >= e->nbr || e->br[2 * lo] != k)
		return 0;
	*z = (pp_lazy){ e, k, e->br[2 * lo + 1], c->loc, c->cbase, c->ci, k, 0, 0 };
	z->rloc = e->tk[z->close].loc ? c->loc + e->tk[z->close].loc - 1 : 0;
	z->rflags = e->tk[z->close].flags;
	c->i = z->close + 1;
	while (c->ci < e->nctr && e->ctr[c->ci].off <= z->close)
		c->ci++;
	p->ntok_out += z->close - k;
	return 1;
}

void pp_lazy_tok(pp *p, pp_lazy *z, ptok *t)
{
	const hent *e = z->e;
	uint32_t k = z->k++;

	htok_get(t, &e->tk[k], e->xtk, shared_base0(), z->base);
	if (t->kind == T_IDENT)
		t->flags |= PF_NOEXP;
	while (z->ci < e->nctr && e->ctr[z->ci].off < k)
		z->ci++;
	if (unlikely(z->ci < e->nctr && e->ctr[z->ci].off == k))
		ctr_spell(p, e, &z->ci, k, z->cbase, t);
}

static inline bool tu_mem(const pp *p, const char *s)
{
	return (s >= p->tmp.base && s < p->tmp.end) || (s >= p->perm.base && s < p->perm.end);
}

/* a token pp_next hands out while recording */
VPRIV void rec_tok(pp *p, const ptok *t)
{
	struct hrec *r = &p->rec;
	const hlev *L = &r->lev[r->n - 1];
	ptok *d;

	if (t->kind == T_EOF)
		return;
	if (unlikely(r->ntk >= HC_MAXTK || (t->loc && t->loc < L->base0))) {
		HC_BAD(p);
		return;
	}
	d = &r->tk[r->ntk++];
	*d = *t;
	if (tu_mem(p, t->s)) {
		if (r->nsb + t->len + 16 > HC_MAXSB) {
			HC_BAD(p);
			return;
		}
		d->s = r->sb + r->nsb;
		memcpy(r->sb + r->nsb, t->s, t->len);
		memset(r->sb + r->nsb + t->len, 0, 16);
		r->nsb += t->len + 16;
	}
	if (unlikely(t->ctr)) {
		if (t->flags & TF_DIRTY)
			HC_BAD(p);
		for (uint16_t q = t->ctr; q; q = r->cp[q].next) {
			const cprov *c = &r->cp[q];

			if (r->nctr < HC_MAXCP)
				r->ctr[r->nctr++] = (hcpos){ r->ntk - 1, (uint32_t)c->off << 16 | c->len, c->val };
			else
				HC_BAD(p);
		}
	}
}

/* ---------------- entries as trees of child entries ---------------- */

typedef struct kexf {
	const uint32_t *ex;
	uint32_t n;
} kexf;

static bool kex_has(const kexf *xs, int nx, uint32_t id)
{
	for (int f = 0; f < nx; f++)
		for (uint32_t i = 0; i < xs[f].n; i++)	/* (short, sorted) */
			if (xs[f].ex[i] >= id) {
				if (xs[f].ex[i] == id)
					return true;
				break;
			}
	return false;
}

/* id was written by the innermost level so far */
static inline bool lev_wrote(pp *p, uint32_t id)
{
	const uint64_t *st = rslot(p, id);

	return (uint32_t)(*st >> 32) == p->rec.lev[p->rec.n - 1].serial && (p->rec.t[(uint32_t)*st].flags & HT_W);
}

/* e's reads (all of them, as inputs) that the innermost level wrote already: into rec.kex */
static void kid_ex(pp *p, const hent *e, kexf *xs, int nx)
{
	struct hrec *r = &p->rec;

#define KX(id) do { uint32_t id_ = (id); if (lev_wrote(p, id_) && !(nx && kex_has(xs, nx, id_))) { \
		if (r->nkex < HC_MAXKEX) r->kex[r->nkex++] = id_; else HC_BAD(p); } } while (0)
	HV_FOR(e->rg, hread, x)
		KX(x->id);
	HV_FOR(e->ru, uint32_t, x)
		KX(*x);
	HV_FOR(e->rv, hread, x)
		KX(x->id);
#undef KX
	for (uint32_t i = 0; i < e->nkid; i++) {
		int m = nx;

		if (e->kid[i].nex && nx < HC_MAXLEV)
			xs[m++] = (kexf){ e->kid[i].ex, e->kid[i].nex };
		kid_ex(p, e->kid[i].e, xs, m);
	}
}

/* every id e (with its children) writes: the innermost level's, not its own */
static void kid_writes(pp *p, const hent *e)
{
	for (uint32_t i = 0; i < e->nkid; i++)
		kid_writes(p, e->kid[i].e);
	HV_FOR(e->dl, hread, x)
		touch_write_kid(p, x->id);
}

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* e becomes a child entry of the innermost level (replayed or recorded in it) */
static void kid_attach(pp *p, const hent *e)
{
	struct hrec *r = &p->rec;
	kexf xs[HC_MAXLEV + 1];
	uint32_t x0 = r->nkex, n, w = 0;

	if (r->nkid >= HC_MAXKID) {
		HC_BAD(p);
		return;
	}
	kid_ex(p, e, xs, 0);
	n = r->nkex - x0;
	if (n > 1) {
		qsort(r->kex + x0, n, sizeof(uint32_t), cmp_u32);
		for (uint32_t i = 0; i < n; i++)
			if (!w || r->kex[x0 + i] != r->kex[x0 + w - 1])
				r->kex[x0 + w++] = r->kex[x0 + i];
		n = w;
		r->nkex = x0 + n;
	}
	r->kid[r->nkid++] = (hkidr){ e, x0, n };
	kid_writes(p, e);
}

/* e's delta: its children's in order, then its own */
static void apply_delta(pp *p, const hent *e)
{
	for (uint32_t i = 0; i < e->nkid; i++)
		apply_delta(p, e->kid[i].e);
	HV_FOR(e->dl, hread, x)
		mset(p, x->id, (const pmacro *)x->v);
}

static void hc_apply(pp *p, const hent *e)
{
	unsigned c0 = p->counter;
	struct hrec *r = &p->rec;
	hlev *L = r->n ? &r->lev[r->n - 1] : NULL;

	p->hc_last = e;

	if (p->tokmode == 2) {
		if (e->has_text) {
			/*
			 * Recording: the entry's tokens are a segment of the enclosing
			 * levels, which publish skips; reserve their positions instead of
			 * copying every token (rec_tok was ~13% of vccd).
			 */
			bool reserve = false;

			if (L) {
				if (r->nseg < HC_MAXSEG && r->ntk + e->total < HC_MAXTK) {
					r->seg[r->nseg++] = (hsegr){ r->ntk, r->ntk + e->total, e, c0, p->next_base };
					r->ntk += e->total;
					reserve = true;
				} else {
					HC_BAD(p);
				}
			}
			push_replay(p, e, p->next_base, c0, 1, reserve);
		}
	} else if (e->has_text) {
		size_t st;

		if (p->col)
			out_ch(p, e->first_sol ? '\n' : ' ');
		if (L)
			text_start(p, e->first_sol);
		out_reserve(p, e->total + 16 * (size_t)e->nctr);
		st = p->nout;
		emit(p, e, c0);
		p->col = e->col_end;
		if (L) {
			if (r->nseg < HC_MAXSEG)
				r->seg[r->nseg++] = (hsegr){ st, p->nout, e, c0, 0 };
			else
				HC_BAD(p);
		}
	}
	if (L) {
		/* the entry's inputs and writes are the enclosing level's too */
		for (int i = 0; i < r->n; i++) {
			if (e->level >= 0)
				r->lev[i].uses_level = 1;
			if (r->lev[i].ndirs < e->ndirs)
				r->lev[i].ndirs = e->ndirs;
		}
		kid_attach(p, e);	/* (its reads and writes stay in it) */
		for (uint32_t i = 0; i < e->nord; i++) {
			if (e->ord[i].present && once_index(p, e->ord[i].f) >= (int)L->nonce0)
				continue;	/* marked by this level itself */
			if (r->nord < HC_MAXORD)
				r->ord[r->nord++] = e->ord[i];
			else
				HC_BAD(p);
		}
		for (uint32_t i = 0; i < e->ncrd; i++) {
			const hcread *c = &e->crd[i];

			if (r->ncrd < HC_MAXCRD)
				r->crd[r->ncrd++] = (hcrd){ c->s, c->len, c->off, c->dlen, c0 + c->rel };
			else
				HC_BAD(p);
		}
	}
	for (uint32_t i = 0; i < e->nfe; i++) {
		const hfent *h = &e->fe[i];
		fent *d = &p->fe[p->nfe++];

		d->base = p->next_base + h->rbase;
		d->dstate = p->dcur;
		d->f = h->f;
		d->fi = h->fi;
		d->name = h->name;
		d->namelen = h->namelen;
		d->spath = h->spath;
		d->sys = h->sys;
	}
	p->next_base += e->span;
	apply_delta(p, e);
	for (uint32_t i = 0; i < e->noadd; i++)
		once_add(p, e->oadd[i]);
	p->counter += e->counter_delta;
	if (p->tokmode != 2 || !e->has_text)
		p->pend = e->pend_out;	/* (tokens: when the replay is done) */
	atomic_fetch_add_explicit(&hstat.hit, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.bytes_replayed, e->total, memory_order_relaxed);
}

static void hc_why(const pp *p, const hkey *k, const hent *e)
{
	char b[512];
	int n = 0;

	HV_FOR(e->rg, hread, x)
		if (!n && (mget(p, x->id) != 0) != (x->v != 0)) {
			const vident *d = &vintern_ents[x->id];

			n = snprintf(b, sizeof(b), "hc-miss %s: guard %.*s %s\n", k->spath, (int)d->len, d->s,
				     x->v ? "def/undef" : "undef/def");
		}
	for (uint32_t i = 0; i < e->nord && !n; i++)
		if ((once_index(p, e->ord[i].f) >= 0) != (bool)e->ord[i].present)
			n = snprintf(b, sizeof(b), "hc-miss %s: once %s\n", k->spath, e->ord[i].f->path);
	HV_FOR(e->ru, uint32_t, x)
		if (!n && mget(p, *x)) {
			const vident *d = &vintern_ents[*x];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s undef/def\n", k->spath, (int)d->len, d->s);
		}
	HV_FOR(e->rv, hread, x) {
		uintptr_t c = mget(p, x->id) & ~(uintptr_t)1, v = x->v;

		if (!n && (v == HDEF ? !c : c != v)) {
			const vident *d = &vintern_ents[x->id];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s %s/%s\n", k->spath, (int)d->len, d->s,
				     v == HDEF ? "def" : "val", c ? "def" : "undef");
		}
	}
	if (!n)
		n = snprintf(b, sizeof(b), "hc-miss %s: counter name or a child entry\n", k->spath);
	(void)!write(2, b, (size_t)n);
}

/* replay a matching entry for this #include; true if done */
VPRIV bool hc_try(pp *p, hkey *k)
{
	static _Atomic int dbg_ = -1;
	int dbg;
	const hent *h = atomic_load_explicit(&k->head, memory_order_acquire);

	p->hgen++;	/* (child entry verdicts are for one state) */
	for (const hent *e = h; e; e = e->next)
		if (hc_valid(p, e)) {
			hc_apply(p, e);
			return true;
		}
	if ((dbg = atomic_load_explicit(&dbg_, memory_order_relaxed)) < 0)
		atomic_store_explicit(&dbg_, dbg = getenv("VPP_HC_DEBUG") != NULL, memory_order_relaxed);
	if (dbg && h)
		hc_why(p, k, h);
	return false;
}

_Thread_local ptok *tk_spare;
_Thread_local char *sb_spare;
_Thread_local rec_spare_t rec_spare;

/* the region's file was just entered (top of the stack) */
VPRIV void hc_begin(pp *p, hkey *k)
{
	struct hrec *r = &p->rec;
	hlev *L;

	if (!r->t && rec_spare.t) {
		/* this thread's last pp's buffers: counts are reset below, contents need no clearing */
		r->t = rec_spare.t, r->ord = rec_spare.ord, r->ctr = rec_spare.ctr;
		r->crd = rec_spare.crd, r->seg = rec_spare.seg, r->cp = rec_spare.cp;
		r->kid = rec_spare.kid, r->kex = rec_spare.kex;
		rec_spare.t = NULL;
	}
	if (!r->t) {
		r->t = vm(HC_MAXT * sizeof(htouch));
		r->ord = vm(HC_MAXORD * sizeof(honce));
		r->ctr = vm(HC_MAXCP * sizeof(hcpos));
		r->crd = vm(HC_MAXCRD * sizeof(hcrd));
		r->seg = vm(HC_MAXSEG * sizeof(hsegr));
		r->cp = vm((HC_MAXCP + 1) * sizeof(cprov));
		r->kid = vm(HC_MAXKID * sizeof(hkidr));
		r->kex = vm(HC_MAXKEX * sizeof(uint32_t));
	}
	if (p->tokmode == 2 && !r->tk) {
		if (tk_spare) {
			r->tk = tk_spare, r->sb = sb_spare;
			tk_spare = NULL;
		} else {
			r->tk = vm(HC_MAXTK * sizeof(ptok));
			r->sb = vm(HC_MAXSB);
		}
	}
	if (!r->n) {
		r->nt = r->nord = r->nctr = r->ncrd = r->nseg = r->ncp = 0;
		r->nkid = r->nkex = 0;
		r->ntk = 0;
		r->nsb = 0;
		r->nbad = 0;
	}
	L = &r->lev[r->n++];
	r->on = 1;
	L->k = k;
	L->serial = ++r->serial;
	L->depth = p->depth;
	L->t0 = r->nt;
	L->o0 = r->nord;
	L->c0 = r->nctr;
	L->r0 = r->ncrd;
	L->s0 = r->nseg;
	L->k0 = r->nkid;
	L->x0 = r->nkex;
	L->fe0 = p->cx[p->depth - 1].fe;
	L->base0 = p->fe[L->fe0].base;
	L->nonce0 = p->nonce;
	L->nlm0 = p->nlm;
	L->npushed0 = p->npushed;
	L->errors0 = p->errors;
	L->warns0 = p->warns;
	L->counter0 = p->counter;
	L->text_started = p->tokmode == 2;	/* tokens: positions count from here */
	L->text0 = r->ntk;
	L->uses_level = 0;
	L->ndirs = 0;
	L->nfile = p->nfile - 1;	/* before the region's file */
	atomic_fetch_add_explicit(&hstat.miss, 1, memory_order_relaxed);
}

static void *hcopy(pp *p, const void *s, size_t n)
{
	void *d = n ? hsalloc(p, n) : NULL;

	if (n)
		memcpy(d, s, n);
	return d;
}

static const char *hstrdup(pp *p, const char *s, size_t n)
{
	const char *sb = shared_base0();

	if (s >= sb && s < sb + (1ull << 34) && !s[n])
		return s;	/* already shared (a replayed entry's): immortal */
	return hc_blob(p, s, n);	/* NUL-terminated, shared */
}

/* hc_blob of a token's spelling, through a small cache of recent ones */
static const char *spell_blob(pp *p, const char *s, uint32_t len, uint8_t kind)
{
	uint64_t w;
	uint32_t h;
	const char *d;

	if (kind >= T_LSQUARE && kind < T_NKINDS && len <= 4) {
		/* punctuators: one spelling per kind (digraphs aside) */
		uint32_t x, y, mk = len == 4 ? ~0u : (1u << (8 * len)) - 1;

		d = p->kspell[kind];
		memcpy(&x, s, 4);	/* (readable past the end) */
		if (d) {
			memcpy(&y, d, 4);
			if (((const hck *)(const void *)(d - offsetof(hck, d)))->n == len && !((x ^ y) & mk))
				return d;
		}
		return p->kspell[kind] = hc_blob(p, s, len);
	}
	memcpy(&w, s, 8);	/* (spellings are readable 16 bytes past their end) */
	if (len < 8)
		w &= ~(~0ull << (8 * len));
	h = (uint32_t)(((w ^ len) * 0x9E3779B97F4A7C15ull) >> 52);
	d = p->bcache[h];
	if (d && ((const hck *)(const void *)(d - offsetof(hck, d)))->n == len && !memcmp(d, s, len))
		return d;
	d = hc_blob(p, s, len);
	p->bcache[h] = d;
	return d;
}

/* tokens [L->text0, end) minus the children's: e's own tokens */
static void publish_tok(pp *p, const hlev *L, size_t end, hent *e)
{
	struct hrec *r = &p->rec;
	uint32_t ns = r->nseg - L->s0, nc = r->nctr - L->c0;
	size_t own = end - L->text0, w = 0, at = L->text0, skipped = 0;
	hseg *sg = hsalloc(p, (ns ? ns : 1) * sizeof(hseg));
	hctr *ct = hsalloc(p, (nc ? nc : 1) * sizeof(hctr));
	uint32_t ci = L->c0;
	htok *tk;
	ptok *xt = NULL;
	uint32_t nx = 0, xcap = 0;
	const char *sb = shared_base0();

	for (uint32_t i = L->s0; i < r->nseg; i++)
		own -= r->seg[i].end - r->seg[i].start;
	tk = varena_alloc(&p->tmp, (own ? own : 1) * sizeof(htok));
	for (uint32_t i = L->s0; i <= r->nseg; i++) {
		size_t to = i < r->nseg ? r->seg[i].start : end;

		for (; ci < r->nctr && r->ctr[ci].pos < to; ci++)
			ct[ci - L->c0] = (hctr){ (uint32_t)(r->ctr[ci].pos - L->text0 - skipped), r->ctr[ci].len,
						 r->ctr[ci].val - L->counter0 };
		for (; at < to; at++, w++) {
			ptok t = r->tk[at];
			htok *h = &tk[w];

			t.loc = t.loc ? t.loc - L->base0 + 1 : 0;
			t.ctr = 0;
			*h = (htok){ 0, t.loc, t.kind, t.flags, (uint16_t)t.len };
			/* (a clean identifier is spelled as its id: --vcc-tokhash checks both) */
			if (t.kind == T_IDENT && t.id && !(t.flags & TF_DIRTY) && t.len <= 0xffff) {
				h->sp = t.id;
				continue;
			}
			if (t.kind != T_IDENT && !t.id && t.len <= 0xffff && !(t.flags & HF_X)) {
				const char *d = spell_blob(p, t.s, t.len, t.kind);

				if (!sb)
					sb = shared_base0();
				h->sp = (uint32_t)((size_t)(d - sb) >> 4) + 1;
				continue;
			}
			/* escape: the full token, its spelling stored */
			if (nx == xcap) {
				ptok *n2 = varena_alloc(&p->tmp, (xcap = xcap ? 2 * xcap : 16) * sizeof(ptok));

				if (nx)
					memcpy(n2, xt, nx * sizeof(ptok));
				xt = n2;
			}
			t.s = hc_blob(p, t.s, t.len);
			xt[nx] = t;
			*h = (htok){ nx++, t.loc, t.kind, HF_X, 0 };
		}
		if (i < r->nseg) {
			sg[i - L->s0] = (hseg){ (uint32_t)w, r->seg[i].counter0 - L->counter0, r->seg[i].child,
						r->seg[i].base0 - L->base0 };
			skipped += r->seg[i].end - r->seg[i].start;
			at = r->seg[i].end;
		}
	}
	{
		/* outermost brace pairs with no child spliced in (pp_lazy_skip) */
		uint32_t nb = 0, depth = 0, open = 0, si = 0, *br;

		for (size_t k = 0; k < w; k++)
			nb += tk[k].kind == T_LBRACE;
		br = hsalloc(p, (nb ? nb : 1) * 2 * sizeof(uint32_t));
		nb = 0;
		for (size_t k = 0; k < w; k++) {
			if (tk[k].kind == T_LBRACE) {
				if (!depth++)
					open = (uint32_t)k;
			} else if (tk[k].kind == T_RBRACE && depth && !--depth) {
				while (si < ns && sg[si].at <= open)
					si++;
				if (si >= ns || sg[si].at > k)
					br[2 * nb] = open, br[2 * nb + 1] = (uint32_t)k, nb++;
			}
		}
		e->br = br;
		e->nbr = nb;
	}
	e->tk = hc_blob(p, tk, own * sizeof(htok));	/* (16-aligned) */
	e->xtk = nx ? hcopy(p, xt, nx * sizeof(ptok)) : NULL;
	e->ntext = own;
	e->total = end - L->text0;
	e->seg = sg;
	e->nseg = ns;
	e->ctr = ct;
	e->nctr = nc;
	e->has_text = 1;
}

#define R16(n) (((size_t)(n) + 15) & ~(size_t)15)
/* bytes an entry holds before sharing, by kind (VPP_HC_DEBUG; VPP_HC_PUB=1: a line per entry) */
static void hc_account(const hent *e, const hkey *k)
{
	static _Atomic int pub_ = -1;
	size_t rd = R16(e->rg.n * sizeof(hread)) + R16(e->ru.n * 4) + R16(e->rv.n * sizeof(hread)) +
		    R16(e->ncrd * sizeof(hcread));
	size_t dl = R16(e->dl.n * sizeof(hread)), on = R16(e->nord * sizeof(honce)) + R16(e->noadd * 8);
	size_t fe = R16(e->nfe * sizeof(hfent)), tx, sg = R16(e->nseg * sizeof(hseg)) + R16(e->nctr * sizeof(hctr));
	int pub;

	for (uint32_t i = 0; i < e->nfe; i++)
		fe += R16(e->fe[i].namelen + 1) + R16(strlen(e->fe[i].spath) + 1);
	tx = e->tk ? R16(e->ntext * sizeof(*e->tk)) : R16(e->ntext + 1);
	atomic_fetch_add_explicit(&hstat.b_rd, rd, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.b_dl, dl, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.b_once, on, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.b_fe, fe, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.b_text, tx, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.b_seg, sg, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.b_ent, R16(sizeof(hent)), memory_order_relaxed);
	if ((pub = atomic_load_explicit(&pub_, memory_order_relaxed)) < 0)
		atomic_store_explicit(&pub_, pub = getenv("VPP_HC_PUB") != NULL, memory_order_relaxed);
	if (pub) {
		char b[600];
		int n = snprintf(b, sizeof(b), "hc-pub %s %zu %zu %zu %zu %zu %zu\n", k->spath, rd, dl, on, fe, tx, sg);

		(void)!write(2, b, (size_t)n);
	}
}

/* the entry for the innermost level, which ends at output offset end */
static hent *publish(pp *p, const hlev *L, size_t end)
{
	struct hrec *r = &p->rec;
	hent *e = hsalloc(p, sizeof(*e));
	uint32_t nrg = 0, nru = 0, nrv = 0, ndl = 0, *ru;
	hread *rg, *rv, *dl;
	hfent *fe;

	memset(e, 0, sizeof(*e));
	for (uint32_t i = L->t0; i < r->nt; i++) {
		const htouch *t = &r->t[i];

		if (t->flags & HT_R) {
			if ((t->flags & HT_G) && (t->v == HDEF || !t->v))
				nrg++;
			else if (t->v)
				nrv++;
			else
				nru++;
		}
		ndl += (t->flags & HT_O) != 0;
	}
	/* scratch in tmp: the stored form is the shared chunks */
	rg = varena_alloc(&p->tmp, (nrg ? nrg : 1) * sizeof(*rg));
	ru = varena_alloc(&p->tmp, (nru ? nru : 1) * sizeof(*ru));
	rv = varena_alloc(&p->tmp, (nrv ? nrv : 1) * sizeof(*rv));
	dl = varena_alloc(&p->tmp, (ndl ? ndl : 1) * sizeof(*dl));
	nrg = nru = nrv = ndl = 0;
	for (uint32_t i = L->t0; i < r->nt; i++) {
		const htouch *t = &r->t[i];

		if (t->flags & HT_R) {
			if ((t->flags & HT_G) && (t->v == HDEF || !t->v))
				rg[nrg++] = (hread){ t->id, 0, t->v };
			else if (t->v)
				rv[nrv++] = (hread){ t->id, 0, t->v };
			else
				ru[nru++] = t->id;
		}
		if (t->flags & HT_O)	/* (a child's last write is in the child) */
			dl[ndl++] = (hread){ t->id, 0, mget(p, t->id) & ~(uintptr_t)1 };
	}
	atomic_fetch_add_explicit(&hstat.nru, nru, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.nrv, nrv, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.ndl, ndl, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.nfe, p->nfe - L->fe0, memory_order_relaxed);
	e->rg = hc_vec(p, rg, nrg, sizeof(*rg));
	e->ru = hc_vec(p, ru, nru, sizeof(*ru));
	e->rv = hc_vec(p, rv, nrv, sizeof(*rv));
	e->dl = hc_vec(p, dl, ndl, sizeof(*dl));
	{
		uint32_t nk = r->nkid - L->k0;
		hkid *kd = hsalloc(p, (nk ? nk : 1) * sizeof(hkid));

		for (uint32_t i = 0; i < nk; i++) {
			const hkidr *k = &r->kid[L->k0 + i];

			kd[i] = (hkid){ k->e, k->nex ? hcopy(p, r->kex + k->ex0, k->nex * sizeof(uint32_t)) : NULL, k->nex };
		}
		e->kid = kd;
		e->nkid = nk;
	}
	e->ord = hcopy(p, r->ord + L->o0, (r->nord - L->o0) * sizeof(honce));
	e->nord = r->nord - L->o0;
	e->oadd = hcopy(p, p->once + L->nonce0, (p->nonce - L->nonce0) * sizeof(*p->once));
	e->noadd = p->nonce - L->nonce0;
	e->nfe = p->nfe - L->fe0;
	fe = hsalloc(p, (e->nfe ? e->nfe : 1) * sizeof(hfent));
	for (uint32_t i = 0; i < e->nfe; i++) {
		const fent *f = &p->fe[L->fe0 + i];

		fe[i] = (hfent){ f->f, f->fi, hstrdup(p, f->name, f->namelen), hstrdup(p, f->spath, strlen(f->spath)),
				 f->namelen, f->base - L->base0, f->sys };
	}
	e->fe = fe;
	e->span = p->next_base - L->base0;
	e->counter_delta = p->counter - L->counter0;
	e->pend_out = p->pend;
	e->level = L->uses_level ? (int32_t)L->nfile : -1;
	e->ndirs = L->ndirs;
	e->psig = p->psig[L->ndirs];
	{
		hcread *cr = hsalloc(p, (r->ncrd - L->r0 ? r->ncrd - L->r0 : 1) * sizeof(hcread));

		for (uint32_t i = L->r0; i < r->ncrd; i++) {
			const hcrd *c = &r->crd[i];

			cr[i - L->r0] = (hcread){ hstrdup(p, c->s, c->len), c->len, c->off, c->dlen, c->val - L->counter0 };
		}
		e->crd = cr;
		e->ncrd = r->ncrd - L->r0;
	}
	if (p->tokmode == 2) {
		if (end > L->text0)
			publish_tok(p, L, end, e);
	} else if (L->text_started) {
		/* own text: the output minus the children's texts */
		uint32_t ns = r->nseg - L->s0, nc = r->nctr - L->c0;
		size_t own = end - L->text0;
		char *t;
		hseg *sg = hsalloc(p, (ns ? ns : 1) * sizeof(hseg));
		hctr *ct = hsalloc(p, (nc ? nc : 1) * sizeof(hctr));
		size_t w = 0, at = L->text0, skipped = 0;
		uint32_t ci = L->c0;

		for (uint32_t i = L->s0; i < r->nseg; i++)
			own -= r->seg[i].end - r->seg[i].start;
		t = varena_alloc(&p->tmp, own + 1);
		for (uint32_t i = L->s0; i <= r->nseg; i++) {
			size_t to = i < r->nseg ? r->seg[i].start : end;

			for (; ci < r->nctr && r->ctr[ci].pos < to; ci++)
				ct[ci - L->c0] = (hctr){ (uint32_t)(r->ctr[ci].pos - L->text0 - skipped), r->ctr[ci].len,
							 r->ctr[ci].val - L->counter0 };
			memcpy(t + w, p->out + at, to - at);
			w += to - at;
			if (i < r->nseg) {
				sg[i - L->s0] = (hseg){ (uint32_t)w, r->seg[i].counter0 - L->counter0, r->seg[i].child, 0 };
				skipped += r->seg[i].end - r->seg[i].start;
				at = r->seg[i].end;
			}
		}
		e->text = hc_blob(p, t, own);	/* variants often print the same */
		e->ntext = own;
		e->total = end - L->text0;
		e->seg = sg;
		e->nseg = ns;
		e->ctr = ct;
		e->nctr = nc;
		e->has_text = 1;
		e->first_sol = L->first_sol;
		e->col_end = p->col != 0;
	}
	hc_account(e, L->k);
	return e;
}

/* the innermost region's file context is being popped */
VPRIV void hc_end(pp *p)
{
	struct hrec *r = &p->rec;
	hlev *L = &r->lev[r->n - 1], *P;
	hkey *k = L->k;
	hent *e = NULL;
	bool linked = false;
	size_t sz;

	if (r->n - 1 < r->nbad || p->errors != L->errors0 || p->warns != L->warns0 || p->nlm != L->nlm0 ||
	    p->npushed != L->npushed0 || p->nonce < L->nonce0) {
		if (r->n - 1 >= r->nbad)
			r->nbad = r->n;	/* the enclosing regions saw it too */
		atomic_fetch_add_explicit(&hstat.uncacheable, 1, memory_order_relaxed);
		if (getenv("VPP_HC_DEBUG")) {
			char b[512];
			int n = snprintf(b, sizeof(b), "hc-bad %s: line %d errors %d warns %d nlm %u\n", k->spath,
					 r->badline, p->errors - L->errors0, p->warns - L->warns0, p->nlm - L->nlm0);

			(void)!write(2, b, (size_t)n);
		}
	} else {
		sz = (r->nt - L->t0) * 16 + (p->nfe - L->fe0) * (sizeof(hfent) + 64) + sizeof(hent);
		if (hc_mem_max && atomic_load_explicit(&hstat.mem, memory_order_relaxed) + sz > hc_mem_max) {
			/* full: the enclosing entries would need this one as a child */
			atomic_fetch_add_explicit(&hstat.full, 1, memory_order_relaxed);
			r->nbad = r->n;
		} else {
			p->hc_bytes = 0;
			e = publish(p, L, p->tokmode == 2 ? r->ntk : p->nout);
			atomic_fetch_add_explicit(&hstat.mem, p->hc_bytes, memory_order_relaxed);
			atomic_fetch_add_explicit(&hstat.bytes_rec, e->ntext, memory_order_relaxed);
			if (atomic_load_explicit(&k->n, memory_order_relaxed) >= HC_MAXVAR) {
				/* not listed (too many variants), but a child of the enclosing entries */
				atomic_fetch_add_explicit(&hstat.full, 1, memory_order_relaxed);
			} else {
				linked = true;
				atomic_fetch_add_explicit(&hstat.rec, 1, memory_order_relaxed);
				atomic_fetch_add_explicit(&k->n, 1, memory_order_relaxed);
				e->next = atomic_load_explicit(&k->head, memory_order_relaxed);
				while (!atomic_compare_exchange_weak_explicit(&k->head, &e->next, e, memory_order_release,
									      memory_order_relaxed))
					;
			}
		}
	}
	if (r->n == 1 && p->top_rec) {
		top_event(p, linked ? e : NULL, 0, 0, 1, 0);
		p->top_rec = 0;
	}
	r->n--;
	if (r->nbad > r->n)
		r->nbad = r->n;
	if (!r->n) {
		r->on = 0;
		return;
	}
	/* the level's touches end (its reads and writes are in e, which the enclosing level gets as a child) */
	P = &r->lev[r->n - 1];
	for (uint32_t i = r->nt; i-- > L->t0;)
		*rslot(p, r->t[i].id) = r->t[i].prev;
	r->nt = L->t0;
	r->nkid = L->k0;
	r->nkex = L->x0;
	{
		uint32_t w = L->o0;

		for (uint32_t i = L->o0; i < r->nord; i++)
			if (!r->ord[i].present || once_index(p, r->ord[i].f) < (int)P->nonce0)
				r->ord[w++] = r->ord[i];
		r->nord = w;
	}
	if (e) {
		/* the level's own counter digits and children are inside its entry */
		r->nctr = L->c0;
		r->nseg = L->s0;
		if (e->has_text)
			r->seg[r->nseg++] = (hsegr){ L->text0, p->tokmode == 2 ? r->ntk : p->nout, e, L->counter0,
						     L->base0 };
		kid_attach(p, e);
	}
}
