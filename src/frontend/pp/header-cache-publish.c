/* vcc preprocessor: header cache: building a region's entry when it ends and publishing it */
#include "ppimpl.h"

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
	e->nlk = r->nlk - L->l0;
	if (e->nlk) {
		hlook *lk = hsalloc(p, e->nlk * sizeof(hlook));

		for (uint32_t i = 0; i < e->nlk; i++) {
			const hlook *l = &r->lk[L->l0 + i];

			lk[i] = *l;
			lk[i].name = hstrdup(p, l->name, l->nlen);
			lk[i].from = l->from ? hstrdup(p, l->from, strlen(l->from)) : NULL;
			lk[i].path = l->path ? hstrdup(p, l->path, strlen(l->path)) : NULL;
		}
		e->lk = lk;
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
		e->edir_end = p->edir;
		e->pk = p->pk, e->pk_L = p->pk_L, e->pk_pp = p->pk_pp;
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
	r->nlk = L->l0;	/* (they are in e, or the enclosing levels are not cacheable either) */
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
