/* vcc preprocessor: header cache: serving a valid entry, as tokens to pp_token_next or as text to pp_text_render */
#include "pp-internal.h"

static void pp_header_cache_serve_push(pp *p, const pp_header_cache_entry *e, uint32_t base, unsigned cbase, uint8_t top, uint8_t norec)
{
	pp_context *c = pp_context_push(p);

	c->type = PP_CONTEXT_SERVED;
	c->ht = e->tk;
	c->pt = e->xtk;
	c->src = pp_shared_base();
	c->n = (uint32_t)e->ntext;
	c->loc = base;
	c->he = e;
	c->si = c->ci = 0;
	c->cbase = cbase;
	c->top = top;
	c->norec = norec;
}

/* a child entry's tokens go before the next own token: pushed */
VPRIV bool pp_header_cache_serve_child(pp *p, pp_context *c)
{
	const pp_header_cache_entry *e = c->he;
	const pp_header_cache_segment *g;

	if (c->si >= e->nseg || e->seg[c->si].at != c->i)
		return false;
	g = &e->seg[c->si++];
	pp_header_cache_serve_push(p, g->child, c->loc + g->lbase, c->cbase + g->counter_delta, 0, c->norec);
	return true;
}

/* __COUNTER__ digits in the spelling of own token k: this TU's values */
static __attribute__((noinline)) void pp_header_cache_counter_spell(pp *p, const pp_header_cache_entry *e, uint32_t *ci, uint32_t k, unsigned cbase, ptok *t)
{
	char *b = varena_alloc(&p->perm, t->len + 24 * 4 + 32), *w = b;
	uint32_t at = 0;

	while (*ci < e->nctr && e->ctr[*ci].off == k) {
		const pp_header_cache_counter_run *h = &e->ctr[(*ci)++];
		uint32_t bo = h->len >> 16, bl = h->len & 0xffff;

		memcpy(w, t->s + at, bo - at);
		w += bo - at;
		w += pp_unsigned_format(w, cbase + h->rel);
		at = bo + bl;
	}
	memcpy(w, t->s + at, t->len - at);
	w += t->len - at;
	memset(w, 0, 16);
	t->s = b;
	t->len = (uint32_t)(w - b);
	if (t->kind == LEX_TOKEN_IDENTIFIER)
		t->id = vintern_copy(b, t->len);
}

VPRIV void pp_header_cache_serve_token(pp *p, pp_context *c, ptok *t)
{
	const pp_header_cache_entry *e = c->he;

	pp_header_cache_token_load(t, &c->ht[c->i], c->pt, c->src, c->loc);
	t->ctr = 0;
	p->token_unrecorded = c->norec;
	if (t->kind == LEX_TOKEN_IDENTIFIER)
		t->flags |= PP_TOKEN_FLAG_NO_EXPAND;	/* handed out as recorded: expanded already */
	if (unlikely(c->ci < e->nctr && e->ctr[c->ci].off == c->i))
		pp_header_cache_counter_spell(p, e, &c->ci, c->i, c->cbase, t);
	c->i++;
}

/* ---------------- lazy bodies (pp.h) ---------------- */

int pp_lazy_body_skip(pp *p, const ptok *lb, pp_lazy *z)
{
	pp_context *c = &p->cx[p->depth - 1];
	const pp_header_cache_entry *e;
	uint32_t k, lo, hi;

	if (c->type != PP_CONTEXT_SERVED || !c->i || lb->kind != LEX_TOKEN_LEFT_BRACE || p->pend || (p->rec.on && !c->norec))
		return 0;
	e = c->he;
	k = c->i - 1;
	if (e->tk[k].kind != LEX_TOKEN_LEFT_BRACE || !e->tk[k].loc || lb->loc != c->loc + e->tk[k].loc - 1)
		return 0;
	lo = 0, hi = e->brace_pair_count;
	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;

		if (e->br[2 * m] < k)
			lo = m + 1;
		else
			hi = m;
	}
	if (lo >= e->brace_pair_count || e->br[2 * lo] != k)
		return 0;
	*z = (pp_lazy){ e, k, e->br[2 * lo + 1], c->loc, c->cbase, c->ci, k, 0, 0 };
	z->rloc = e->tk[z->close].loc ? c->loc + e->tk[z->close].loc - 1 : 0;
	z->rflags = e->tk[z->close].flags;
	c->i = z->close + 1;
	while (c->ci < e->nctr && e->ctr[c->ci].off <= z->close)
		c->ci++;
	p->tokens_delivered += z->close - k;
	return 1;
}

void pp_lazy_body_token_get(pp *p, pp_lazy *z, ptok *t)
{
	const pp_header_cache_entry *e = z->e;
	uint32_t k = z->k++;

	pp_header_cache_token_load(t, &e->tk[k], e->xtk, pp_shared_base(), z->base);
	if (t->kind == LEX_TOKEN_IDENTIFIER)
		t->flags |= PP_TOKEN_FLAG_NO_EXPAND;
	while (z->ci < e->nctr && e->ctr[z->ci].off < k)
		z->ci++;
	if (unlikely(z->ci < e->nctr && e->ctr[z->ci].off == k))
		pp_header_cache_counter_spell(p, e, &z->ci, k, z->cbase, t);
}
/* e's delta: its children's in order, then its own */
static void pp_header_cache_delta_apply(pp *p, const pp_header_cache_entry *e)
{
	for (uint32_t i = 0; i < e->nkid; i++)
		pp_header_cache_delta_apply(p, e->kid[i].e);
	PP_HEADER_CACHE_VECTOR_FOR(e->dl, pp_header_cache_read, x)
		pp_macro_table_set(p, x->id, (const pp_macro *)x->v);
}

static void pp_header_cache_entry_apply(pp *p, const pp_header_cache_entry *e)
{
	unsigned c0 = p->counter;
	struct pp_recording *r = &p->rec;
	pp_header_cache_level *L = r->n ? &r->levels[r->n - 1] : NULL;

	p->header_cache_last_entry = e;

	if (p->output_mode == 2) {
		if (e->has_text) {
			/*
			 * Recording: the entry's tokens are a segment of the enclosing
			 * levels, which publish skips; reserve their positions instead of
			 * copying every token (pp_header_cache_token_record was ~13% of vccd).
			 */
			bool reserve = false;

			if (L) {
				if (r->nseg < PP_HEADER_CACHE_SEGMENT_MAX && r->ntk + e->total < PP_HEADER_CACHE_TOKEN_MAX) {
					r->seg[r->nseg++] = (pp_header_cache_recorded_segment){ r->ntk, r->ntk + e->total, e, c0, p->next_location_base };
					r->ntk += e->total;
					reserve = true;
				} else {
					PP_HEADER_CACHE_REGION_REJECT(p);
				}
			}
			pp_header_cache_serve_push(p, e, p->next_location_base, c0, 1, reserve);
		}
	} else if (e->has_text) {
		size_t st;

		if (L)
			pp_header_cache_text_start(p, e->first_sol);
		pp_output_reserve(p, e->total + 16 * (size_t)e->nctr);
		st = p->nout;
		pp_header_cache_text_serve(p, e, c0);
		p->col = e->col_end;
		p->edir = e->directive_on_line_at_end;
		p->pk = e->pk, p->pk_L = e->pk_L, p->pk_pp = e->pk_pp, p->previous_end = NULL;
		if (L) {
			if (r->nseg < PP_HEADER_CACHE_SEGMENT_MAX)
				r->seg[r->nseg++] = (pp_header_cache_recorded_segment){ st, p->nout, e, c0, 0 };
			else
				PP_HEADER_CACHE_REGION_REJECT(p);
		}
	}
	if (p->output_mode != 2 && p->output_text_mode)
		pp_output_file_change(p, pp_file_context_current(p), 0);
	if (L) {
		/* the entry's inputs and writes are the enclosing level's too */
		for (int i = 0; i < r->n; i++) {
			if (e->level >= 0)
				r->levels[i].uses_level = 1;
			if (r->levels[i].ndirs < e->ndirs)
				r->levels[i].ndirs = e->ndirs;
		}
		pp_header_cache_child_attach(p, e);	/* (its reads and writes stay in it) */
		for (uint32_t i = 0; i < e->nord; i++) {
			if (e->ord[i].present && pp_once_file_find(p, e->ord[i].f) >= (int)L->nonce0)
				continue;	/* marked by this level itself */
			if (r->nord < PP_HEADER_CACHE_ONCE_MARK_MAX)
				r->ord[r->nord++] = e->ord[i];
			else
				PP_HEADER_CACHE_REGION_REJECT(p);
		}
		for (uint32_t i = 0; i < e->ncrd; i++) {
			const pp_header_cache_counter_name_read *c = &e->crd[i];

			if (r->ncrd < PP_HEADER_CACHE_COUNTER_READ_MAX)
				r->crd[r->ncrd++] = (pp_header_cache_counter_read){ c->s, c->len, c->off, c->dlen, c0 + c->rel };
			else
				PP_HEADER_CACHE_REGION_REJECT(p);
		}
	}
	for (uint32_t i = 0; i < e->nfe; i++) {
		const pp_header_cache_file *h = &e->fe[i];
		pp_file_entry *d = &p->fe[p->nfe++];

		d->base = p->next_location_base + h->record_base;
		d->diagnostic_state = p->dcur;
		d->f = h->f;
		d->fi = h->fi;
		d->name = h->name;
		d->namelen = h->namelen;
		d->spath = h->spath;
		d->sys = h->sys;
		d->guard_open = h->guard_open;
	}
	p->next_location_base += e->span;
	pp_header_cache_delta_apply(p, e);
	for (uint32_t i = 0; i < e->noadd; i++)
		pp_once_file_add(p, e->once_additions[i]);
	p->counter += e->counter_delta;
	if (p->output_mode != 2 || !e->has_text)
		p->pend = e->pend_out;	/* (tokens: when the replay is done) */
	atomic_fetch_add_explicit(&pp_header_cache_statistics.hit, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.bytes_replayed, e->total, memory_order_relaxed);
}

static void pp_header_cache_miss_explain(const pp *p, const pp_header_cache_key *k, const pp_header_cache_entry *e)
{
	char b[512];
	int n = 0;

	PP_HEADER_CACHE_VECTOR_FOR(e->rg, pp_header_cache_read, x)
		if (!n && (pp_macro_table_get(p, x->id) != 0) != (x->v != 0)) {
			const vident *d = &vintern_ents[x->id];

			n = snprintf(b, sizeof(b), "hc-miss %s: guard %.*s %s\n", k->spath, (int)d->len, d->s,
				     x->v ? "def/undef" : "undef/def");
		}
	for (uint32_t i = 0; i < e->nord && !n; i++)
		if ((pp_once_file_find(p, e->ord[i].f) >= 0) != (bool)e->ord[i].present)
			n = snprintf(b, sizeof(b), "hc-miss %s: once %s\n", k->spath, e->ord[i].f->path);
	PP_HEADER_CACHE_VECTOR_FOR(e->ru, uint32_t, x)
		if (!n && pp_macro_table_get(p, *x)) {
			const vident *d = &vintern_ents[*x];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s undef/def\n", k->spath, (int)d->len, d->s);
		}
	PP_HEADER_CACHE_VECTOR_FOR(e->rv, pp_header_cache_read, x) {
		uintptr_t c = pp_macro_table_get(p, x->id) & ~(uintptr_t)1, v = x->v;

		if (!n && (v == PP_HEADER_CACHE_DEFINED ? !c : c != v)) {
			const vident *d = &vintern_ents[x->id];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s %s/%s\n", k->spath, (int)d->len, d->s,
				     v == PP_HEADER_CACHE_DEFINED ? "def" : "val", c ? "def" : "undef");
		}
	}
	if (!n)
		n = snprintf(b, sizeof(b), "hc-miss %s: counter name or a child entry\n", k->spath);
	(void)!write(2, b, (size_t)n);
}

/* replay a matching entry for this #include; true if done */
VPRIV bool pp_header_cache_entry_serve(pp *p, pp_header_cache_key *k)
{
	static _Atomic int dbg_ = -1;
	int dbg;
	const pp_header_cache_entry *h = atomic_load_explicit(&k->head, memory_order_acquire);

	p->hgen++;	/* (child entry verdicts are for one state) */
	for (const pp_header_cache_entry *e = h; e; e = e->next)
		if (pp_header_cache_entry_is_valid(p, e)) {
			pp_header_cache_entry_apply(p, e);
			return true;
		}
	if ((dbg = atomic_load_explicit(&dbg_, memory_order_relaxed)) < 0)
		atomic_store_explicit(&dbg_, dbg = getenv("VPP_HC_DEBUG") != NULL, memory_order_relaxed);
	if (dbg && h)
		pp_header_cache_miss_explain(p, k, h);
	return false;
}
/* an entry's text, children spliced in, __COUNTER__ values from base */
VPRIV void pp_header_cache_text_serve(pp *p, const pp_header_cache_entry *e, unsigned base)
{
	uint32_t pos = 0, ci = 0;

	for (uint32_t si = 0; si <= e->nseg; si++) {
		uint32_t to = si < e->nseg ? e->seg[si].at : (uint32_t)e->ntext;

		while (ci < e->nctr && e->ctr[ci].off < to) {
			const pp_header_cache_counter_run *c = &e->ctr[ci++];

			pp_output_append(p, e->text + pos, c->off - pos);
			pp_output_reserve(p, 24);
			p->nout += (size_t)pp_unsigned_format(p->out + p->nout, base + c->rel);
			pos = c->off + c->len;
		}
		pp_output_append(p, e->text + pos, to - pos);
		pos = to;
		if (si < e->nseg)
			pp_header_cache_text_serve(p, e->seg[si].child, base + e->seg[si].counter_delta);
	}
}
