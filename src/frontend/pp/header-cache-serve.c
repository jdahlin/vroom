/* vcc preprocessor: header cache: serving a valid entry, as tokens to pp_next or as text to pp_text */
#include "ppimpl.h"

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

		if (L)
			text_start(p, e->first_sol);
		out_reserve(p, e->total + 16 * (size_t)e->nctr);
		st = p->nout;
		emit(p, e, c0);
		p->col = e->col_end;
		p->edir = e->edir_end;
		p->pk = e->pk, p->pk_L = e->pk_L, p->pk_pp = e->pk_pp, p->pk_end = NULL;
		if (L) {
			if (r->nseg < HC_MAXSEG)
				r->seg[r->nseg++] = (hsegr){ st, p->nout, e, c0, 0 };
			else
				HC_BAD(p);
		}
	}
	if (p->tokmode != 2 && p->out_text)
		text_file_change(p, cur_file(p), 0);
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
/* an entry's text, children spliced in, __COUNTER__ values from base */
VPRIV void emit(pp *p, const hent *e, unsigned base)
{
	uint32_t pos = 0, ci = 0;

	for (uint32_t si = 0; si <= e->nseg; si++) {
		uint32_t to = si < e->nseg ? e->seg[si].at : (uint32_t)e->ntext;

		while (ci < e->nctr && e->ctr[ci].off < to) {
			const hctr *c = &e->ctr[ci++];

			out_put(p, e->text + pos, c->off - pos);
			out_reserve(p, 24);
			p->nout += (size_t)utoa(p->out + p->nout, base + c->rel);
			pos = c->off + c->len;
		}
		out_put(p, e->text + pos, to - pos);
		pos = to;
		if (si < e->nseg)
			emit(p, e->seg[si].child, base + e->seg[si].cdelta);
	}
}
