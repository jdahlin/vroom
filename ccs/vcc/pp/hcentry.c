/* vcc preprocessor: header cache: token entries (replay, record, publish) */
#include "ppimpl.h"

/* ---------------- header cache: token entries (pp_next) ---------------- */

static void push_replay(pp *p, const hent *e, uint32_t base, unsigned cbase, uint8_t top, uint8_t norec)
{
	ctx *c = push(p);

	c->type = C_REPLAY;
	c->pt = e->tk;
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

VPRIV void replay_tok(pp *p, ctx *c, ptok *t)
{
	const hent *e = c->he;

	*t = c->pt[c->i];
	t->loc = t->loc ? c->loc + t->loc - 1 : 0;
	t->ctr = 0;
	p->rep_norec = c->norec;
	if (t->kind == T_IDENT)
		t->flags |= PF_NOEXP;	/* handed out as recorded: expanded already */
	if (unlikely(c->ci < e->nctr && e->ctr[c->ci].off == c->i)) {
		/* __COUNTER__ digits in the spelling: this TU's values */
		char *b = alloc(&p->perm, t->len + 24 * 4 + 32), *w = b;
		uint32_t at = 0;

		while (c->ci < e->nctr && e->ctr[c->ci].off == c->i) {
			const hctr *h = &e->ctr[c->ci++];
			uint32_t bo = h->len >> 16, bl = h->len & 0xffff;

			memcpy(w, t->s + at, bo - at);
			w += bo - at;
			w += utoa(w, c->cbase + h->rel);
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
	c->i++;
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
		for (uint32_t i = 0; i < e->nrg; i++)
			touch_read(p, e->rg[i].id, e->rg[i].v, HT_G);
		for (uint32_t i = 0; i < e->nru; i++)
			touch_read(p, e->ru[i], 0, 0);
		for (uint32_t i = 0; i < e->nrv; i++)
			touch_read(p, e->rv[i].id, e->rv[i].v, 0);
		for (uint32_t i = 0; i < e->ndl; i++)
			touch_write(p, e->dl[i].id);
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
		d->f = h->f;
		d->fi = h->fi;
		d->name = h->name;
		d->namelen = h->namelen;
		d->spath = h->spath;
		d->sys = h->sys;
	}
	p->next_base += e->span;
	for (uint32_t i = 0; i < e->ndl; i++)
		mset(p, e->dl[i].id, (const pmacro *)e->dl[i].v);
	for (uint32_t i = 0; i < e->noadd && p->nonce < 4096; i++)
		p->once[p->nonce++] = e->oadd[i];
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

	for (uint32_t i = 0; i < e->nrg && !n; i++)
		if ((mget(p, e->rg[i].id) != 0) != (e->rg[i].v != 0)) {
			const vident *d = &vintern_ents[e->rg[i].id];

			n = snprintf(b, sizeof(b), "hc-miss %s: guard %.*s %s\n", k->spath, (int)d->len, d->s,
				     e->rg[i].v ? "def/undef" : "undef/def");
		}
	for (uint32_t i = 0; i < e->nord && !n; i++)
		if ((once_index(p, e->ord[i].f) >= 0) != (bool)e->ord[i].present)
			n = snprintf(b, sizeof(b), "hc-miss %s: once %s\n", k->spath, e->ord[i].f->path);
	for (uint32_t i = 0; i < e->nru && !n; i++)
		if (mget(p, e->ru[i])) {
			const vident *d = &vintern_ents[e->ru[i]];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s undef/def\n", k->spath, (int)d->len, d->s);
		}
	for (uint32_t i = 0; i < e->nrv && !n; i++) {
		uintptr_t c = mget(p, e->rv[i].id) & ~(uintptr_t)1, v = e->rv[i].v;

		if (v == HDEF ? !c : c != v) {
			const vident *d = &vintern_ents[e->rv[i].id];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s %s/%s\n", k->spath, (int)d->len, d->s,
				     v == HDEF ? "def" : "val", c ? "def" : "undef");
		}
	}
	if (!n)
		n = snprintf(b, sizeof(b), "hc-miss %s: counter name\n", k->spath);
	(void)!write(2, b, (size_t)n);
}

/* replay a matching entry for this #include; true if done */
VPRIV bool hc_try(pp *p, hkey *k)
{
	static _Atomic int dbg_ = -1;
	int dbg;
	const hent *h = atomic_load_explicit(&k->head, memory_order_acquire);

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
		rec_spare.t = NULL;
	}
	if (!r->t) {
		r->t = vm(HC_MAXT * sizeof(htouch));
		r->ord = vm(HC_MAXORD * sizeof(honce));
		r->ctr = vm(HC_MAXCP * sizeof(hcpos));
		r->crd = vm(HC_MAXCRD * sizeof(hcrd));
		r->seg = vm(HC_MAXSEG * sizeof(hsegr));
		r->cp = vm((HC_MAXCP + 1) * sizeof(cprov));
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

static void *hcopy(const void *s, size_t n)
{
	void *d = n ? salloc(n) : NULL;

	if (n)
		memcpy(d, s, n);
	return d;
}

static char *hstrdup(const char *s, size_t n)
{
	char *d = salloc(n + 1);

	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

/* tokens [L->text0, end) minus the children's: e's own tokens */
static void publish_tok(pp *p, const hlev *L, size_t end, hent *e)
{
	struct hrec *r = &p->rec;
	uint32_t ns = r->nseg - L->s0, nc = r->nctr - L->c0;
	size_t own = end - L->text0, w = 0, at = L->text0, skipped = 0;
	hseg *sg = salloc((ns ? ns : 1) * sizeof(hseg));
	hctr *ct = salloc((nc ? nc : 1) * sizeof(hctr));
	uint32_t ci = L->c0;
	ptok *tk;

	for (uint32_t i = L->s0; i < r->nseg; i++)
		own -= r->seg[i].end - r->seg[i].start;
	tk = salloc((own ? own : 1) * sizeof(ptok));
	for (uint32_t i = L->s0; i <= r->nseg; i++) {
		size_t to = i < r->nseg ? r->seg[i].start : end;

		for (; ci < r->nctr && r->ctr[ci].pos < to; ci++)
			ct[ci - L->c0] = (hctr){ (uint32_t)(r->ctr[ci].pos - L->text0 - skipped), r->ctr[ci].len,
						 r->ctr[ci].val - L->counter0 };
		for (; at < to; at++, w++) {
			ptok t = r->tk[at];

			t.loc = t.loc ? t.loc - L->base0 + 1 : 0;
			t.ctr = 0;
			if (t.s >= r->sb && t.s < r->sb + r->nsb) {
				char *d = salloc(t.len + 16);

				memcpy(d, t.s, t.len);
				memset(d + t.len, 0, 16);
				t.s = d;
			}
			tk[w] = t;
		}
		if (i < r->nseg) {
			sg[i - L->s0] = (hseg){ (uint32_t)w, r->seg[i].counter0 - L->counter0, r->seg[i].child,
						r->seg[i].base0 - L->base0 };
			skipped += r->seg[i].end - r->seg[i].start;
			at = r->seg[i].end;
		}
	}
	e->tk = tk;
	e->ntext = own;
	e->total = end - L->text0;
	e->seg = sg;
	e->nseg = ns;
	e->ctr = ct;
	e->nctr = nc;
	e->has_text = 1;
}

/* the entry for the innermost level, which ends at output offset end */
static hent *publish(pp *p, const hlev *L, size_t end)
{
	struct hrec *r = &p->rec;
	hent *e = salloc(sizeof(*e));
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
		ndl += (t->flags & HT_W) != 0;
	}
	rg = salloc((nrg ? nrg : 1) * sizeof(*rg));
	ru = salloc((nru ? nru : 1) * sizeof(*ru));
	rv = salloc((nrv ? nrv : 1) * sizeof(*rv));
	dl = salloc((ndl ? ndl : 1) * sizeof(*dl));
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
		if (t->flags & HT_W)
			dl[ndl++] = (hread){ t->id, 0, mget(p, t->id) & ~(uintptr_t)1 };
	}
	atomic_fetch_add_explicit(&hstat.nru, nru, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.nrv, nrv, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.ndl, ndl, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.nfe, p->nfe - L->fe0, memory_order_relaxed);
	e->rg = rg, e->nrg = nrg;
	e->ru = ru, e->nru = nru;
	e->rv = rv, e->nrv = nrv;
	e->dl = dl, e->ndl = ndl;
	e->ord = hcopy(r->ord + L->o0, (r->nord - L->o0) * sizeof(honce));
	e->nord = r->nord - L->o0;
	e->oadd = hcopy(p->once + L->nonce0, (p->nonce - L->nonce0) * sizeof(*p->once));
	e->noadd = p->nonce - L->nonce0;
	e->nfe = p->nfe - L->fe0;
	fe = salloc((e->nfe ? e->nfe : 1) * sizeof(hfent));
	for (uint32_t i = 0; i < e->nfe; i++) {
		const fent *f = &p->fe[L->fe0 + i];

		fe[i] = (hfent){ f->f, f->fi, hstrdup(f->name, f->namelen), hstrdup(f->spath, strlen(f->spath)),
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
		hcread *cr = salloc((r->ncrd - L->r0 ? r->ncrd - L->r0 : 1) * sizeof(hcread));

		for (uint32_t i = L->r0; i < r->ncrd; i++) {
			const hcrd *c = &r->crd[i];

			cr[i - L->r0] = (hcread){ hstrdup(c->s, c->len), c->len, c->off, c->dlen, c->val - L->counter0 };
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
		hseg *sg = salloc((ns ? ns : 1) * sizeof(hseg));
		hctr *ct = salloc((nc ? nc : 1) * sizeof(hctr));
		size_t w = 0, at = L->text0, skipped = 0;
		uint32_t ci = L->c0;

		for (uint32_t i = L->s0; i < r->nseg; i++)
			own -= r->seg[i].end - r->seg[i].start;
		t = salloc(own + 1);
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
		e->text = t;
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
	return e;
}

/* the innermost region's file context is being popped */
VPRIV void hc_end(pp *p)
{
	struct hrec *r = &p->rec;
	hlev *L = &r->lev[r->n - 1], *P;
	hkey *k = L->k;
	hent *e = NULL;
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
		uint64_t m0 = atomic_load_explicit(&shared_used, memory_order_relaxed);

		sz = (r->nt - L->t0) * 16 + (p->nfe - L->fe0) * (sizeof(hfent) + 64) + sizeof(hent);
		if (atomic_load_explicit(&k->n, memory_order_relaxed) >= HC_MAXVAR ||
		    (hc_mem_max && atomic_load_explicit(&hstat.mem, memory_order_relaxed) + sz > hc_mem_max)) {
			atomic_fetch_add_explicit(&hstat.full, 1, memory_order_relaxed);
		} else {
			e = publish(p, L, p->tokmode == 2 ? r->ntk : p->nout);
			/* (other threads allocate too: close enough for a limit) */
			atomic_fetch_add_explicit(&hstat.mem, atomic_load_explicit(&shared_used, memory_order_relaxed) - m0,
						  memory_order_relaxed);
			atomic_fetch_add_explicit(&hstat.rec, 1, memory_order_relaxed);
			atomic_fetch_add_explicit(&hstat.bytes_rec, e->ntext, memory_order_relaxed);
			atomic_fetch_add_explicit(&k->n, 1, memory_order_relaxed);
			e->next = atomic_load_explicit(&k->head, memory_order_relaxed);
			while (!atomic_compare_exchange_weak_explicit(&k->head, &e->next, e, memory_order_release,
								      memory_order_relaxed))
				;
		}
	}
	if (r->n == 1 && p->top_rec) {
		top_event(p, e, 0, 0, 1, 0);
		p->top_rec = 0;
	}
	r->n--;
	if (r->nbad > r->n)
		r->nbad = r->n;
	if (!r->n) {
		r->on = 0;
		return;
	}
	/* merge into the enclosing level */
	P = &r->lev[r->n - 1];
	{
		uint32_t w = L->t0;

		for (uint32_t i = L->t0; i < r->nt; i++) {
			htouch T = r->t[i];
			uint64_t *st = rslot(p, T.id);

			if ((uint32_t)(T.prev >> 32) == P->serial) {
				htouch *pt = &r->t[(uint32_t)T.prev];

				if (!(pt->flags & HT_W) && (T.flags & HT_R) && (pt->flags & HT_R) && pt->v == HDEF &&
				    T.v != HDEF)
					pt->v = T.v;
				pt->flags |= T.flags & HT_W;
				*st = T.prev;
			} else {
				r->t[w] = T;
				*st = (uint64_t)P->serial << 32 | w;
				w++;
			}
		}
		r->nt = w;
	}
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
	}
}
