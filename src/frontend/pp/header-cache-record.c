/* vcc preprocessor: header cache: recording what a region reads and writes while it runs */
#include "ppimpl.h"

/* a child entry wrote id: not the level's own write any more */
static inline void touch_write_kid(pp *p, uint32_t id)
{
	bool isnew;
	htouch *t = touch(p, id, &isnew);

	if (t)
		t->flags = (t->flags | HT_W) & ~(uint32_t)HT_O;
}

VPRIV uint64_t *rslot(pp *p, uint32_t id)
{
	uint64_t **pg = &p->rst[id >> MPAGE_BITS];

	if (unlikely(!*pg)) {
		*pg = varena_alloc(&p->perm, MPAGE * sizeof(uint64_t));
		memset(*pg, 0, MPAGE * sizeof(uint64_t));
	}
	return &(*pg)[id & (MPAGE - 1)];
}

/* a read of id (entry e) while recording; def: only definedness matters */
VPRIV __attribute__((noinline)) void rec_read(pp *p, uint32_t id, uintptr_t e, int def)
{
	e &= ~(uintptr_t)1;
	touch_read(p, id, def ? (e ? HDEF : 0) : e, def == 2 ? HT_G : 0);
}

VPRIV uint16_t new_cprov(pp *p, uint32_t val, uint32_t off, uint32_t len, uint16_t next)
{
	if (p->rec.ncp >= HC_MAXCP || len > 0xffff) {
		HC_BAD(p);
		return 0;
	}
	p->rec.cp[++p->rec.ncp] = (cprov){ val, off, (uint16_t)len, next };
	return (uint16_t)p->rec.ncp;
}

/* chain a shifted by d, followed by chain tail (shared) */
VPRIV uint16_t cp_chain(pp *p, uint16_t a, uint32_t d, uint16_t tail)
{
	if (!a)
		return tail;
	{
		const cprov c = p->rec.cp[a];
		uint16_t n = cp_chain(p, c.next, d, tail);

		return new_cprov(p, c.val, c.off + d, c.len, n);
	}
}

/* a read of an identifier with __COUNTER__ digits: at replay its name differs */
VPRIV __attribute__((noinline)) void rec_ctr_read(pp *p, const ptok *t, uintptr_t e)
{
	const cprov *c = &p->rec.cp[t->ctr];
	char *d;

	if (e || c->next || p->rec.ncrd >= HC_MAXCRD || t->len > 4096) {
		HC_BAD(p);	/* such a name defined as a macro, or two runs: give up */
		return;
	}
	d = varena_alloc(&p->perm, t->len + 1);
	memcpy(d, t->s, t->len);	/* pasted: the spelling is clean */
	p->rec.crd[p->rec.ncrd++] = (hcrd){ d, t->len, c->off, c->len, c->val };
}

/* #define / #undef / pop_macro */
VPRIV void mdef(pp *p, uint32_t id, const pmacro *m)
{
	if (unlikely(p->rec.on))
		touch_write(p, id);
	mset(p, id, m);
}

static inline bool tu_mem(const pp *p, const char *s)
{
	const uint8_t *u = (const uint8_t *)s;

	return (u >= p->tmp.base && u < p->tmp.base + p->tmp.cap) || (u >= p->perm.base && u < p->perm.base + p->perm.cap);
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
VPRIV void kid_attach(pp *p, const hent *e)
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
		r->kid = rec_spare.kid, r->kex = rec_spare.kex, r->lk = rec_spare.lk;
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
		r->lk = vm(HC_MAXLK * sizeof(hlook));
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
		r->nlk = 0;
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
	L->l0 = r->nlk;
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
