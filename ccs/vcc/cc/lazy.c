/* vcc front end: lazy bodies (see lztok in ccint.h) */
#include <stdatomic.h>

#include "ccimpl.h"

/* ---------------- lazy bodies ---------------- */

static _Atomic uint64_t lz_st_fn, lz_st_tok, lz_st_pfn, lz_st_ptok, lz_st_hc;
static int lz_off = -1;

void cc_lz_stats(char *buf, size_t n)
{
	snprintf(buf, n, "lazy bodies: %llu kept (%.1f M tokens; %llu in the header cache), %llu parsed (%.1f M tokens)",
		 (unsigned long long)lz_st_fn, lz_st_tok / 1e6, (unsigned long long)lz_st_hc,
		 (unsigned long long)lz_st_pfn, lz_st_ptok / 1e6);
}

/* n lazy bodies appended by a declaration replay (cc/replay.c): counted as kept */
VPRIV void lazy_replayed(const lzfn *L, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		atomic_fetch_add_explicit(&lz_st_fn, 1, memory_order_relaxed);
		atomic_fetch_add_explicit(&lz_st_tok, L[i].z.close - L[i].z.open + 1, memory_order_relaxed);
		atomic_fetch_add_explicit(&lz_st_hc, 1, memory_order_relaxed);
	}
}

/* a definition whose code only exists if something references it, from a header */
VPRIV bool lazy_ok(cc *c, uint32_t s, const declr *d)
{
	const sym *y = &c->syms[s];
	const attrs *a = y->attrs ? &c->attr[y->attrs] : NULL;

	int off = __atomic_load_n(&lz_off, __ATOMIC_RELAXED);

	if (unlikely(off < 0))	/* (threads race to set it: same value) */
		__atomic_store_n(&lz_off, off = getenv("VCC_EAGER") != NULL, __ATOMIC_RELAXED);
	if (off || y->kind != S_FUNC || y->sc != SC_STATIC || (y->flags & (SF_DEFINED | SF_ATTR_USED | SF_OVERLOAD)))
		return false;
	if (a && (a->used || a->constructor || a->destructor || a->alias))
		return false;
	if (c->npack || c->lz_on || c->ntk != 1)
		return false;	/* (lookahead past the '{': never at a definition) */	/* (#pragma pack: the body's local structs would see the TU's end) */
	return !pp_in_main_file(c->p, d->loc);
}

static void lz_tok(cc *c, const ptok *t)
{
	lztok *z = &c->lzt[c->nlzt++];

	z->loc = t->loc;
	z->kind = t->kind;
	z->len = t->len;
	if (t->kind == T_IDENT) {
		z->v = t->id;
		z->flags = t->flags & ~TF_DIRTY;	/* (the interned spelling has no splices) */
		z->len = vintern_ents[t->id].len;
	} else {
		z->v = c->nlzs;
		z->flags = t->flags;
		memcpy(c->lzs + c->nlzs, t->s, t->len);
		c->lzs[c->nlzs + t->len] = 0;
		c->nlzs += t->len + 1;
	}
}

static void lazy_parse(cc *c, uint32_t i);

/* tk[0] is the body's '{': keep the tokens through its '}' */
VPRIV void lazy_capture(cc *c, uint32_t s, const declr *d)
{
	uint32_t i = c->nlzf++, depth = 0;
	lzfn *L = &c->lzf[i];
	ptok t;
	bool eof = false;

	*L = (lzfn){ s, c->nlzt, 0, c->nsym, d->name, d->loc, d->psyms, d->npsyms, d->plb, d->ple, 0, 0, { 0 } };
	if (pp_lazy_skip(c->p, &c->tk[0], &L->z)) {
		/* replayed from the header cache: the entry has the tokens, nothing passes through */
		L->hc = 1;
		c->syms[s].flags |= SF_DEFINED | SF_LAZY;
		atomic_fetch_add_explicit(&lz_st_fn, 1, memory_order_relaxed);
		atomic_fetch_add_explicit(&lz_st_tok, L->z.close - L->z.open + 1, memory_order_relaxed);
		atomic_fetch_add_explicit(&lz_st_hc, 1, memory_order_relaxed);
		c->prevloc = L->z.rloc;
		c->prevflags = L->z.rflags;
		fetch(c, &c->tk[0]);
		c->ntk = 1;
		return;
	}
	lz_tok(c, &c->tk[0]);	/* '{' */
	depth = 1;
	for (;;) {
		pp_next(c->p, &t);	/* raw: #pragma comes along as P_DIRECTIVE, applied when parsed */
		if (t.kind == T_EOF) {
			eof = true;
			break;
		}
		lz_tok(c, &t);
		depth += t.kind == T_LBRACE;
		depth -= t.kind == T_RBRACE;
		if (!depth)
			break;
	}
	L->ntk = c->nlzt - L->tk;
	c->syms[s].flags |= SF_DEFINED | SF_LAZY;
	atomic_fetch_add_explicit(&lz_st_fn, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&lz_st_tok, L->ntk, memory_order_relaxed);
	if (eof) {
		/* an unterminated body: parse it now, for clang's diagnostics */
		lazy_parse(c, i);
		c->tk[0] = t;
		c->ntk = 1;
		return;
	}
	c->prevloc = c->lzt[c->nlzt - 1].loc;
	c->prevflags = c->lzt[c->nlzt - 1].flags;
	fetch(c, &c->tk[0]);
	c->ntk = 1;
}

static void lazy_parse(cc *c, uint32_t i)
{
	lzfn *L = &c->lzf[i];
	ptok tk[4];
	int ntk = c->ntk, npack = c->npack;
	uint32_t prevloc = c->prevloc, s = L->sym;
	uint8_t prevflags = c->prevflags;
	declr d;

	L->done = 1;
	memcpy(tk, c->tk, sizeof(tk));
	memset(&d, 0, sizeof(d));
	d.name = L->name, d.loc = L->loc, d.psyms = L->psyms, d.npsyms = L->npsyms;
	d.plb = L->plb, d.ple = L->ple, d.has_func = true;
	c->lz_on = true;
	c->lz_hc = L->hc;
	c->lzz = L->z;
	c->lzc = L->tk, c->lze = L->hc ? L->z.base : L->tk + L->ntk;
	c->lz_wm = L->wm;
	c->npack = 0;
	fetch(c, &c->tk[0]);
	c->ntk = 1;
	c->syms[s].flags &= ~(SF_DEFINED | SF_LAZY);
	function_def(c, s, &d);
	if (!is(c, T_EOF))
		cc_error(c, tok(c)->loc, "expected end of function body");
	atomic_fetch_add_explicit(&lz_st_pfn, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&lz_st_ptok, L->hc ? L->z.close - L->z.open + 1 : L->ntk, memory_order_relaxed);
	c->lz_on = false;
	c->lz_hc = false;
	c->lz_wm = 0;
	c->npack = npack;
	memcpy(c->tk, tk, sizeof(tk));
	c->ntk = ntk;
	c->prevloc = prevloc;
	c->prevflags = prevflags;
}

/* after the TU: the lazy bodies something names, until nothing new is named */
VPRIV void lazy_closure(cc *c)
{
	bool more = true;

	if (!c->nlzf)
		return;
	for (uint32_t i = 0; i < c->ntoplevel; i++) {
		const sym *y = &c->syms[c->toplevel[i]];

		if (y->attrs && c->attr[y->attrs].alias) {
			uint32_t t = cc_global_named(c, c->strs[c->attr[y->attrs].alias - 1].s);

			if (t)
				c->syms[t].flags |= SF_REF;
		}
	}
	while (more) {
		more = false;
		for (uint32_t i = 0; i < c->nlzf; i++) {
			const lzfn *L = &c->lzf[i];
			const sym *y = &c->syms[L->sym];
			const attrs *a = y->attrs ? &c->attr[y->attrs] : NULL;

			if (L->done || !((y->flags & (SF_REF | SF_USED | SF_ADDR_TAKEN | SF_ATTR_USED)) ||
					 (a && (a->used || a->constructor || a->destructor))))
				continue;
			lazy_parse(c, i);
			more = true;
		}
	}
}

