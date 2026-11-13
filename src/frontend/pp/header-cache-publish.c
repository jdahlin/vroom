/* vcc preprocessor: header cache: building a region's entry when it ends and publishing it */
#include "pp-internal.h"

static void *pp_header_cache_copy(pp *p, const void *s, size_t n)
{
	void *d = n ? pp_header_cache_allocate(p, n) : NULL;

	if (n)
		memcpy(d, s, n);
	return d;
}

static const char *pp_header_cache_string_share(pp *p, const char *s, size_t n)
{
	const char *sb = pp_shared_base();

	if (s >= sb && s < sb + (1ull << 34) && !s[n])
		return s;	/* already shared (a replayed entry's): immortal */
	return pp_header_cache_blob_get(p, s, n);	/* NUL-terminated, shared */
}

/* pp_header_cache_blob_get of a token's spelling, through a small cache of recent ones */
static const char *pp_header_cache_spelling_blob(pp *p, const char *s, uint32_t len, uint8_t kind)
{
	uint64_t w;
	uint32_t h;
	const char *d;

	if (kind >= LEX_TOKEN_LEFT_SQUARE && kind < LEX_TOKEN_KIND_COUNT && len <= 4) {
		/* punctuators: one spelling per kind (digraphs aside) */
		uint32_t x, y, mk = len == 4 ? ~0u : (1u << (8 * len)) - 1;

		d = p->punctuator_blobs[kind];
		memcpy(&x, s, 4);	/* (readable past the end) */
		if (d) {
			memcpy(&y, d, 4);
			if (((const pp_header_cache_chunk *)(const void *)(d - offsetof(pp_header_cache_chunk, d)))->n == len && !((x ^ y) & mk))
				return d;
		}
		return p->punctuator_blobs[kind] = pp_header_cache_blob_get(p, s, len);
	}
	memcpy(&w, s, 8);	/* (spellings are readable 16 bytes past their end) */
	if (len < 8)
		w &= ~(~0ull << (8 * len));
	h = (uint32_t)(((w ^ len) * 0x9E3779B97F4A7C15ull) >> 52);
	d = p->blob_cache[h];
	if (d && ((const pp_header_cache_chunk *)(const void *)(d - offsetof(pp_header_cache_chunk, d)))->n == len && !memcmp(d, s, len))
		return d;
	d = pp_header_cache_blob_get(p, s, len);
	p->blob_cache[h] = d;
	return d;
}

/* tokens [L->text0, end) minus the children's: e's own tokens */
static void pp_header_cache_tokens_collect(pp *p, const pp_header_cache_level *L, size_t end, pp_header_cache_entry *e)
{
	struct pp_recording *r = &p->rec;
	uint32_t ns = r->nseg - L->s0, nc = r->nctr - L->c0;
	size_t own = end - L->text_at_start, w = 0, at = L->text_at_start, skipped = 0;
	pp_header_cache_segment *sg = pp_header_cache_allocate(p, (ns ? ns : 1) * sizeof(pp_header_cache_segment));
	pp_header_cache_counter_run *ct = pp_header_cache_allocate(p, (nc ? nc : 1) * sizeof(pp_header_cache_counter_run));
	uint32_t ci = L->c0;
	pp_header_cache_token *tk;
	ptok *xt = NULL;
	uint32_t lex_character_next = 0, xcap = 0;
	const char *sb = pp_shared_base();

	for (uint32_t i = L->s0; i < r->nseg; i++)
		own -= r->seg[i].end - r->seg[i].start;
	tk = varena_alloc(&p->tmp, (own ? own : 1) * sizeof(pp_header_cache_token));
	for (uint32_t i = L->s0; i <= r->nseg; i++) {
		size_t to = i < r->nseg ? r->seg[i].start : end;

		for (; ci < r->nctr && r->ctr[ci].pos < to; ci++)
			ct[ci - L->c0] = (pp_header_cache_counter_run){ (uint32_t)(r->ctr[ci].pos - L->text_at_start - skipped), r->ctr[ci].len,
						 r->ctr[ci].val - L->counter0 };
		for (; at < to; at++, w++) {
			ptok t = r->tk[at];
			pp_header_cache_token *h = &tk[w];

			t.loc = t.loc ? t.loc - L->base0 + 1 : 0;
			t.ctr = 0;
			*h = (pp_header_cache_token){ 0, t.loc, t.kind, t.flags, (uint16_t)t.len };
			/* (a clean identifier is spelled as its id: --vcc-tokhash checks both) */
			if (t.kind == LEX_TOKEN_IDENTIFIER && t.id && !(t.flags & LEX_TOKEN_FLAG_SPLICED) && t.len <= 0xffff) {
				h->sp = t.id;
				continue;
			}
			if (t.kind != LEX_TOKEN_IDENTIFIER && !t.id && t.len <= 0xffff && !(t.flags & PP_HEADER_CACHE_TOKEN_ESCAPE)) {
				const char *d = pp_header_cache_spelling_blob(p, t.s, t.len, t.kind);

				if (!sb)
					sb = pp_shared_base();
				h->sp = (uint32_t)((size_t)(d - sb) >> 4) + 1;
				continue;
			}
			/* escape: the full token, its spelling stored */
			if (lex_character_next == xcap) {
				ptok *n2 = varena_alloc(&p->tmp, (xcap = xcap ? 2 * xcap : 16) * sizeof(ptok));

				if (lex_character_next)
					memcpy(n2, xt, lex_character_next * sizeof(ptok));
				xt = n2;
			}
			t.s = pp_header_cache_blob_get(p, t.s, t.len);
			xt[lex_character_next] = t;
			*h = (pp_header_cache_token){ lex_character_next++, t.loc, t.kind, PP_HEADER_CACHE_TOKEN_ESCAPE, 0 };
		}
		if (i < r->nseg) {
			sg[i - L->s0] = (pp_header_cache_segment){ (uint32_t)w, r->seg[i].counter0 - L->counter0, r->seg[i].child,
						r->seg[i].base0 - L->base0 };
			skipped += r->seg[i].end - r->seg[i].start;
			at = r->seg[i].end;
		}
	}
	{
		/* outermost brace pairs with no child spliced in (pp_lazy_body_skip) */
		uint32_t nb = 0, depth = 0, open = 0, si = 0, *br;

		for (size_t k = 0; k < w; k++)
			nb += tk[k].kind == LEX_TOKEN_LEFT_BRACE;
		br = pp_header_cache_allocate(p, (nb ? nb : 1) * 2 * sizeof(uint32_t));
		nb = 0;
		for (size_t k = 0; k < w; k++) {
			if (tk[k].kind == LEX_TOKEN_LEFT_BRACE) {
				if (!depth++)
					open = (uint32_t)k;
			} else if (tk[k].kind == LEX_TOKEN_RIGHT_BRACE && depth && !--depth) {
				while (si < ns && sg[si].at <= open)
					si++;
				if (si >= ns || sg[si].at > k)
					br[2 * nb] = open, br[2 * nb + 1] = (uint32_t)k, nb++;
			}
		}
		e->br = br;
		e->brace_pair_count = nb;
	}
	e->tk = pp_header_cache_blob_get(p, tk, own * sizeof(pp_header_cache_token));	/* (16-aligned) */
	e->xtk = lex_character_next ? pp_header_cache_copy(p, xt, lex_character_next * sizeof(ptok)) : NULL;
	e->ntext = own;
	e->total = end - L->text_at_start;
	e->seg = sg;
	e->nseg = ns;
	e->ctr = ct;
	e->nctr = nc;
	e->has_text = 1;
}

#define PP_ROUND_UP_16(n) (((size_t)(n) + 15) & ~(size_t)15)
/* bytes an entry holds before sharing, by kind (VPP_HC_DEBUG; VPP_HC_PUB=1: a line per entry) */
static void pp_header_cache_bytes_account(const pp_header_cache_entry *e, const pp_header_cache_key *k)
{
	static _Atomic int pub_ = -1;
	size_t rd = PP_ROUND_UP_16(e->rg.n * sizeof(pp_header_cache_read)) + PP_ROUND_UP_16(e->ru.n * 4) + PP_ROUND_UP_16(e->rv.n * sizeof(pp_header_cache_read)) +
		    PP_ROUND_UP_16(e->ncrd * sizeof(pp_header_cache_counter_name_read));
	size_t dl = PP_ROUND_UP_16(e->dl.n * sizeof(pp_header_cache_read)), on = PP_ROUND_UP_16(e->nord * sizeof(pp_header_cache_once_mark)) + PP_ROUND_UP_16(e->noadd * 8);
	size_t fe = PP_ROUND_UP_16(e->nfe * sizeof(pp_header_cache_file)), tx, sg = PP_ROUND_UP_16(e->nseg * sizeof(pp_header_cache_segment)) + PP_ROUND_UP_16(e->nctr * sizeof(pp_header_cache_counter_run));
	int pub;

	for (uint32_t i = 0; i < e->nfe; i++)
		fe += PP_ROUND_UP_16(e->fe[i].namelen + 1) + PP_ROUND_UP_16(strlen(e->fe[i].spath) + 1);
	tx = e->tk ? PP_ROUND_UP_16(e->ntext * sizeof(*e->tk)) : PP_ROUND_UP_16(e->ntext + 1);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_rd, rd, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_dl, dl, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_once, on, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_fe, fe, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_text, tx, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_seg, sg, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_ent, PP_ROUND_UP_16(sizeof(pp_header_cache_entry)), memory_order_relaxed);
	if ((pub = atomic_load_explicit(&pub_, memory_order_relaxed)) < 0)
		atomic_store_explicit(&pub_, pub = getenv("VPP_HC_PUB") != NULL, memory_order_relaxed);
	if (pub) {
		char b[600];
		int n = snprintf(b, sizeof(b), "hc-pub %s %zu %zu %zu %zu %zu %zu\n", k->spath, rd, dl, on, fe, tx, sg);

		(void)!write(2, b, (size_t)n);
	}
}

/* the entry for the innermost level, which ends at output offset end */
static pp_header_cache_entry *pp_header_cache_entry_publish(pp *p, const pp_header_cache_level *L, size_t end)
{
	struct pp_recording *r = &p->rec;
	pp_header_cache_entry *e = pp_header_cache_allocate(p, sizeof(*e));
	uint32_t nrg = 0, nru = 0, nrv = 0, ndl = 0, *ru;
	pp_header_cache_read *rg, *rv, *dl;
	pp_header_cache_file *fe;

	memset(e, 0, sizeof(*e));
	for (uint32_t i = L->t0; i < r->nt; i++) {
		const pp_header_cache_touch_record *t = &r->t[i];

		if (t->flags & PP_HEADER_CACHE_TOUCH_READ) {
			if ((t->flags & PP_HEADER_CACHE_TOUCH_GUARD) && (t->v == PP_HEADER_CACHE_DEFINED || !t->v))
				nrg++;
			else if (t->v)
				nrv++;
			else
				nru++;
		}
		ndl += (t->flags & PP_HEADER_CACHE_TOUCH_OWN) != 0;
	}
	/* scratch in tmp: the stored form is the shared chunks */
	rg = varena_alloc(&p->tmp, (nrg ? nrg : 1) * sizeof(*rg));
	ru = varena_alloc(&p->tmp, (nru ? nru : 1) * sizeof(*ru));
	rv = varena_alloc(&p->tmp, (nrv ? nrv : 1) * sizeof(*rv));
	dl = varena_alloc(&p->tmp, (ndl ? ndl : 1) * sizeof(*dl));
	nrg = nru = nrv = ndl = 0;
	for (uint32_t i = L->t0; i < r->nt; i++) {
		const pp_header_cache_touch_record *t = &r->t[i];

		if (t->flags & PP_HEADER_CACHE_TOUCH_READ) {
			if ((t->flags & PP_HEADER_CACHE_TOUCH_GUARD) && (t->v == PP_HEADER_CACHE_DEFINED || !t->v))
				rg[nrg++] = (pp_header_cache_read){ t->id, 0, t->v };
			else if (t->v)
				rv[nrv++] = (pp_header_cache_read){ t->id, 0, t->v };
			else
				ru[nru++] = t->id;
		}
		if (t->flags & PP_HEADER_CACHE_TOUCH_OWN)	/* (a child's last write is in the child) */
			dl[ndl++] = (pp_header_cache_read){ t->id, 0, pp_macro_table_get(p, t->id) & ~(uintptr_t)1 };
	}
	atomic_fetch_add_explicit(&pp_header_cache_statistics.nru, nru, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.nrv, nrv, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.ndl, ndl, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.nfe, p->nfe - L->fe0, memory_order_relaxed);
	e->rg = pp_header_cache_vector_store(p, rg, nrg, sizeof(*rg));
	e->ru = pp_header_cache_vector_store(p, ru, nru, sizeof(*ru));
	e->rv = pp_header_cache_vector_store(p, rv, nrv, sizeof(*rv));
	e->dl = pp_header_cache_vector_store(p, dl, ndl, sizeof(*dl));
	{
		uint32_t nk = r->nkid - L->k0;
		pp_header_cache_child *kd = pp_header_cache_allocate(p, (nk ? nk : 1) * sizeof(pp_header_cache_child));

		for (uint32_t i = 0; i < nk; i++) {
			const pp_header_cache_recorded_child *k = &r->kid[L->k0 + i];

			kd[i] = (pp_header_cache_child){ k->e, k->nex ? pp_header_cache_copy(p, r->kex + k->ex0, k->nex * sizeof(uint32_t)) : NULL, k->nex };
		}
		e->kid = kd;
		e->nkid = nk;
	}
	e->nlk = r->nlk - L->l0;
	if (e->nlk) {
		pp_header_cache_lookup *lk = pp_header_cache_allocate(p, e->nlk * sizeof(pp_header_cache_lookup));

		for (uint32_t i = 0; i < e->nlk; i++) {
			const pp_header_cache_lookup *l = &r->lk[L->l0 + i];

			lk[i] = *l;
			lk[i].name = pp_header_cache_string_share(p, l->name, l->nlen);
			lk[i].from = l->from ? pp_header_cache_string_share(p, l->from, strlen(l->from)) : NULL;
			lk[i].path = l->path ? pp_header_cache_string_share(p, l->path, strlen(l->path)) : NULL;
		}
		e->lk = lk;
	}
	e->ord = pp_header_cache_copy(p, r->ord + L->o0, (r->nord - L->o0) * sizeof(pp_header_cache_once_mark));
	e->nord = r->nord - L->o0;
	e->once_additions = pp_header_cache_copy(p, p->once + L->nonce0, (p->nonce - L->nonce0) * sizeof(*p->once));
	e->noadd = p->nonce - L->nonce0;
	e->nfe = p->nfe - L->fe0;
	fe = pp_header_cache_allocate(p, (e->nfe ? e->nfe : 1) * sizeof(pp_header_cache_file));
	for (uint32_t i = 0; i < e->nfe; i++) {
		const pp_file_entry *f = &p->fe[L->fe0 + i];

		fe[i] = (pp_header_cache_file){ f->f, f->fi, pp_header_cache_string_share(p, f->name, f->namelen), pp_header_cache_string_share(p, f->spath, strlen(f->spath)),
				 f->namelen, f->base - L->base0, f->sys, f->guard_open };
	}
	e->fe = fe;
	e->span = p->next_location_base - L->base0;
	e->counter_delta = p->counter - L->counter0;
	e->pend_out = p->pend;
	e->level = L->uses_level ? (int32_t)L->nfile : -1;
	e->ndirs = L->ndirs;
	e->psig = p->psig[L->ndirs];
	{
		pp_header_cache_counter_name_read *cr = pp_header_cache_allocate(p, (r->ncrd - L->r0 ? r->ncrd - L->r0 : 1) * sizeof(pp_header_cache_counter_name_read));

		for (uint32_t i = L->r0; i < r->ncrd; i++) {
			const pp_header_cache_counter_read *c = &r->crd[i];

			cr[i - L->r0] = (pp_header_cache_counter_name_read){ pp_header_cache_string_share(p, c->s, c->len), c->len, c->off, c->dlen, c->val - L->counter0 };
		}
		e->crd = cr;
		e->ncrd = r->ncrd - L->r0;
	}
	if (p->output_mode == 2) {
		if (end > L->text_at_start)
			pp_header_cache_tokens_collect(p, L, end, e);
	} else if (L->text_started) {
		/* own text: the output minus the children's texts */
		uint32_t ns = r->nseg - L->s0, nc = r->nctr - L->c0;
		size_t own = end - L->text_at_start;
		char *t;
		pp_header_cache_segment *sg = pp_header_cache_allocate(p, (ns ? ns : 1) * sizeof(pp_header_cache_segment));
		pp_header_cache_counter_run *ct = pp_header_cache_allocate(p, (nc ? nc : 1) * sizeof(pp_header_cache_counter_run));
		size_t w = 0, at = L->text_at_start, skipped = 0;
		uint32_t ci = L->c0;

		for (uint32_t i = L->s0; i < r->nseg; i++)
			own -= r->seg[i].end - r->seg[i].start;
		t = varena_alloc(&p->tmp, own + 1);
		for (uint32_t i = L->s0; i <= r->nseg; i++) {
			size_t to = i < r->nseg ? r->seg[i].start : end;

			for (; ci < r->nctr && r->ctr[ci].pos < to; ci++)
				ct[ci - L->c0] = (pp_header_cache_counter_run){ (uint32_t)(r->ctr[ci].pos - L->text_at_start - skipped), r->ctr[ci].len,
							 r->ctr[ci].val - L->counter0 };
			memcpy(t + w, p->out + at, to - at);
			w += to - at;
			if (i < r->nseg) {
				sg[i - L->s0] = (pp_header_cache_segment){ (uint32_t)w, r->seg[i].counter0 - L->counter0, r->seg[i].child, 0 };
				skipped += r->seg[i].end - r->seg[i].start;
				at = r->seg[i].end;
			}
		}
		e->text = pp_header_cache_blob_get(p, t, own);	/* variants often print the same */
		e->ntext = own;
		e->total = end - L->text_at_start;
		e->seg = sg;
		e->nseg = ns;
		e->ctr = ct;
		e->nctr = nc;
		e->has_text = 1;
		e->first_sol = L->first_sol;
		e->col_end = p->col != 0;
		e->directive_on_line_at_end = p->edir;
		e->pk = p->pk, e->pk_L = p->pk_L, e->pk_pp = p->pk_pp;
	}
	pp_header_cache_bytes_account(e, L->k);
	return e;
}

/* the innermost region's file context is being popped */
VPRIV void pp_header_cache_region_end(pp *p)
{
	struct pp_recording *r = &p->rec;
	pp_header_cache_level *L = &r->levels[r->n - 1], *P;
	pp_header_cache_key *k = L->k;
	pp_header_cache_entry *e = NULL;
	bool linked = false;
	size_t sz;

	if (r->n - 1 < r->nbad || p->errors != L->errors0 || p->warns != L->warns0 || p->nlm != L->nlm0 ||
	    p->pushed_macro_count != L->pushed_macros_at_start || p->nonce < L->nonce0) {
		if (r->n - 1 >= r->nbad)
			r->nbad = r->n;	/* the enclosing regions saw it too */
		atomic_fetch_add_explicit(&pp_header_cache_statistics.uncacheable, 1, memory_order_relaxed);
		if (getenv("VPP_HC_DEBUG")) {
			char b[512];
			int n = snprintf(b, sizeof(b), "hc-bad %s: line %d errors %d warns %d nlm %u\n", k->spath,
					 r->uncacheable_line, p->errors - L->errors0, p->warns - L->warns0, p->nlm - L->nlm0);

			(void)!write(2, b, (size_t)n);
		}
	} else {
		sz = (r->nt - L->t0) * 16 + (p->nfe - L->fe0) * (sizeof(pp_header_cache_file) + 64) + sizeof(pp_header_cache_entry);
		if (pp_header_cache_memory_limit && atomic_load_explicit(&pp_header_cache_statistics.mem, memory_order_relaxed) + sz > pp_header_cache_memory_limit) {
			/* full: the enclosing entries would need this one as a child */
			atomic_fetch_add_explicit(&pp_header_cache_statistics.full, 1, memory_order_relaxed);
			r->nbad = r->n;
		} else {
			p->entry_bytes = 0;
			e = pp_header_cache_entry_publish(p, L, p->output_mode == 2 ? r->ntk : p->nout);
			atomic_fetch_add_explicit(&pp_header_cache_statistics.mem, p->entry_bytes, memory_order_relaxed);
			atomic_fetch_add_explicit(&pp_header_cache_statistics.bytes_rec, e->ntext, memory_order_relaxed);
			if (atomic_load_explicit(&k->n, memory_order_relaxed) >= PP_HEADER_CACHE_VARIANT_MAX) {
				/* not listed (too many variants), but a child of the enclosing entries */
				atomic_fetch_add_explicit(&pp_header_cache_statistics.full, 1, memory_order_relaxed);
			} else {
				linked = true;
				atomic_fetch_add_explicit(&pp_header_cache_statistics.rec, 1, memory_order_relaxed);
				atomic_fetch_add_explicit(&k->n, 1, memory_order_relaxed);
				e->next = atomic_load_explicit(&k->head, memory_order_relaxed);
				while (!atomic_compare_exchange_weak_explicit(&k->head, &e->next, e, memory_order_release,
									      memory_order_relaxed))
					;
			}
		}
	}
	if (r->n == 1 && p->top_region_recording) {
		pp_top_event_record(p, linked ? e : NULL, 0, 0, 1, 0);
		p->top_region_recording = 0;
	}
	r->n--;
	if (r->nbad > r->n)
		r->nbad = r->n;
	if (!r->n) {
		r->on = 0;
		return;
	}
	/* the level's touches end (its reads and writes are in e, which the enclosing level gets as a child) */
	P = &r->levels[r->n - 1];
	for (uint32_t i = r->nt; i-- > L->t0;)
		*pp_header_cache_stamp_slot(p, r->t[i].id) = r->t[i].prev;
	r->nt = L->t0;
	r->nkid = L->k0;
	r->child_exclusion_count = L->x0;
	r->nlk = L->l0;	/* (they are in e, or the enclosing levels are not cacheable either) */
	{
		uint32_t w = L->o0;

		for (uint32_t i = L->o0; i < r->nord; i++)
			if (!r->ord[i].present || pp_once_file_find(p, r->ord[i].f) < (int)P->nonce0)
				r->ord[w++] = r->ord[i];
		r->nord = w;
	}
	if (e) {
		/* the level's own counter digits and children are inside its entry */
		r->nctr = L->c0;
		r->nseg = L->s0;
		if (e->has_text)
			r->seg[r->nseg++] = (pp_header_cache_recorded_segment){ L->text_at_start, p->output_mode == 2 ? r->ntk : p->nout, e, L->counter0,
						     L->base0 };
		pp_header_cache_child_attach(p, e);
	}
}
