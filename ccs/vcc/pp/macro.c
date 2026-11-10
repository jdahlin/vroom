/* vcc preprocessor: the macro table, #define, content-canonical macros */
#include "ppimpl.h"

/* ---- content-canonical macros (definitions in the main file and <built-in>) ---- */

#define CANON_BITS 18
static _Atomic(const pmacro *) canon_tab[1u << CANON_BITS];
const vtok no_body[1];

static uint32_t macro_hash(const pmacro *m)
{
	uint64_t h = m->name * 0x9E3779B97F4A7C15ull ^ ((uint64_t)m->kind << 8 | m->flags | (uint64_t)m->nparam << 16);

	for (uint32_t j = 0; j < m->nbody; j++) {
		const vtok *v = &m->body[j];
		uint32_t x = v->kind | (v->flags & (TF_SPACE | TF_DIRTY)) << 8 | (m->pidx ? m->pidx[j] : 0) << 16;

		h = (h ^ x) * 0x100000001B3ull;
		h = (h ^ vintern_hash(m->src + v->off, v->len)) * 0x100000001B3ull;
	}
	return (uint32_t)(h ^ h >> 32);
}

static bool macro_same(const pmacro *a, const pmacro *b)
{
	if (a->name != b->name || a->kind != b->kind || a->flags != b->flags || a->nparam != b->nparam ||
	    a->nbody != b->nbody || !a->pidx != !b->pidx)
		return false;
	for (uint32_t j = 0; j < a->nbody; j++) {
		const vtok *x = &a->body[j], *y = &b->body[j];

		if (x->kind != y->kind || x->len != y->len || ((x->flags ^ y->flags) & (TF_SPACE | TF_DIRTY)) ||
		    (a->pidx && a->pidx[j] != b->pidx[j]) || memcmp(a->src + x->off, b->src + y->off, x->len))
			return false;
	}
	return true;
}

/* the shared macro equal to m (m's body may be in per-TU memory) */
static const pmacro *canon_macro(const pmacro *m)
{
	uint32_t h = macro_hash(m);
	pmacro *c = NULL;

	for (uint32_t i = h >> (32 - CANON_BITS), k = 0; k < 64; i = (i + 1) & ((1u << CANON_BITS) - 1), k++) {
		const pmacro *e = atomic_load_explicit(&canon_tab[i], memory_order_acquire);

		if (!e) {
			if (!c) {
				/* a copy in shared memory: body tokens rebased on a copy of their text */
				uint32_t lo = m->nbody ? m->body[0].off : 0;
				uint32_t hi = m->nbody ? m->body[m->nbody - 1].off + m->body[m->nbody - 1].len : 0;
				char *t = salloc(hi - lo + 64);
				vtok *v = m->nbody ? salloc(m->nbody * sizeof(vtok)) : (vtok *)no_body;

				memcpy(t, m->src + lo, hi - lo);
				memset(t + (hi - lo), 0, 64);
				for (uint32_t j = 0; j < m->nbody; j++) {
					v[j] = m->body[j];
					v[j].off -= lo;
				}
				c = salloc(sizeof(*c));
				*c = *m;
				c->body = v;
				c->src = t;
				c->hash = h;
				if (m->pidx) {
					uint8_t *x = salloc(m->nbody + 1);

					memcpy(x, m->pidx, m->nbody);
					c->pidx = x;
				}
			}
			if (atomic_compare_exchange_strong_explicit(&canon_tab[i], &e, c, memory_order_acq_rel,
								    memory_order_acquire))
				return c;
		}
		if (e->hash == h && macro_same(e, m))
			return e;
	}
	return NULL;	/* table crowded here: keep the per-TU one */
}

/* -Wvariadic-macros (-pedantic): "#define M(args...)"; out of line, define() is hot */
static __attribute__((noinline, cold)) void named_variadic(pp *p, ctx *c, const vtok *vt, uint32_t b, uint32_t e)
{
	for (uint32_t i = b + 2; i + 1 < e && vt[i].kind != T_RPAREN; i++)
		if (vt[i].kind == T_IDENT && vt[i + 1].kind == T_ELLIPSIS) {
			static const char *const g[] = { "pedantic", NULL };
			pp_dinfo d = { "warning", c->loc + vt[i + 1].off, NULL, 0, NULL, 0, NULL, 0 };

			if (pp_warnx(p, "variadic-macros", false, g, &d, "named variadic macros are a GNU extension"))
				HC_BAD(p);	/* (a replay would not say it) */
			return;
		}
}

VPRIV void define(pp *p, ctx *c, uint32_t b, uint32_t e)
{
	const vtok *vt = c->vt;
	const pmacro *m;
	const char *err;
	uint32_t eoff;
	bool site = c->f != &p->builtin && c->fe != 0;	/* a header: one macro per directive */
	_Atomic(const pmacro *) *slot = site ? &((pfile *)c->fi)->mac[c->di - 1] : NULL;

	if (b >= e || vt[b].kind != T_IDENT) {
		error(p, c->loc + vt[b - 1].off, "macro name must be an identifier");
		return;
	}
	if (vt[b].id == KW_defined) {
		error(p, c->loc + vt[b].off, "'defined' cannot be used as a macro name");
		return;
	}
	m = slot ? atomic_load_explicit(slot, memory_order_acquire) : NULL;
	if (!m || unlikely(m->flags & MF_UNKNOWN))	/* -Winvalid-pp-token (a cached site knows it has none) */
		for (uint32_t i = b + 1; i < e; i++)
			if (unlikely(vt[i].kind == T_UNKNOWN))
				pp_invalid_tok(p, c->src + vt[i].off, vt[i].len, c->loc + vt[i].off);
	if (!m) {
		pmacro *n = site ? salloc(sizeof(*n)) : varena_alloc(&p->perm, sizeof(*n));

		err = parse_define(vt, b, e, n, site ? mem_shared : mem_perm, &p->perm, &eoff);
		if (err) {
			/* parse_define's two messages, each a format of its own (its id) */
			if (err[0] == '\'')
				error(p, c->loc + vt[eoff].off, "'##' cannot appear at either end of a macro expansion");
			else
				error(p, c->loc + vt[eoff].off, "invalid macro parameter list");
			return;
		}
		n->src = c->src;
		if (!n->nbody)
			n->body = no_body;
		if ((n->flags & MF_BADHASH) && !p->o->asm_mode) {
			error(p, c->loc + vt[eoff].off, "'#' is not followed by a macro parameter");
			return;
		}
		if (site) {
			const pmacro *z = NULL;

			if (!atomic_compare_exchange_strong_explicit(slot, &z, n, memory_order_acq_rel, memory_order_acquire))
				n = (pmacro *)z;	/* the same definition, built by another TU */
			m = n;
		} else {
			m = canon_macro(n);
			if (!m)
				m = n;
		}
	} else if ((m->flags & MF_BADHASH) && !p->o->asm_mode) {
		/* cached by an assembler TU; body offsets are this file's */
		uint32_t j = 0;

		while (j < m->nbody && !(m->body[j].kind == T_HASH && !(j + 1 < m->nbody && m->pidx && m->pidx[j + 1]) &&
					 !(j + 1 < m->nbody && m->body[j + 1].kind == T_IDENT && m->body[j + 1].id == KW_VA_OPT)))
			j++;
		error(p, c->loc + (j < m->nbody ? m->body[j].off : vt[b].off), "'#' is not followed by a macro parameter");
		return;
	}
	if (unlikely((m->flags & MF_VARIADIC) && p->o->nwargs) && c->f != &p->builtin && pp_wc_maybe(p, PPW_VARIADIC))
		named_variadic(p, c, vt, b, e);
	{
		/* -Wmacro-redefined: a different definition replacing a live one */
		uintptr_t o = mget(p, m->name);
		const pmacro *om = (const pmacro *)(o & ~(uintptr_t)1);

		if (unlikely(o && !(o & 1) && om != m && !om->builtin && !macro_same(om, m))) {
			uint32_t nloc = c->loc + vt[b].off;
			pp_span r = { nloc, nloc + vt[b].len };
			pp_dinfo d = { "warning", nloc, &r, 0, NULL, 0, NULL, 0 };

			if (c->f != &p->builtin && pp_warnx(p, "macro-redefined", true, NULL, &d, "'%.*s' macro redefined",
							    (int)vt[b].len, c->src + vt[b].off))
				macro_note_(p, om, nloc, "previous definition is here");
		}
	}
	mdef(p, m->name, m);
	if (unlikely(p->o->dump_defines))
		dd_out(p, m->name, m, c->f == &p->builtin);
}

/* -dM: every defined macro as "#define NAME[(params)] body\n", sorted by
 * name as clang prints them; builtins (__FILE__, __has_*) are left out.
 * The text is malloc'ed. */
static int mname_cmp(const void *a, const void *b)
{
	const vident *x = &vintern_ents[*(const uint32_t *)a], *y = &vintern_ents[*(const uint32_t *)b];
	int c = memcmp(x->s, y->s, x->len < y->len ? x->len : y->len);

	return c ? c : (x->len > y->len) - (x->len < y->len);
}

static void dm_put(char **b, size_t *n, size_t *cap, const char *s, size_t l)
{
	if (*n + l + 1 > *cap) {
		*cap = (*n + l + 1) * 2;
		*b = realloc(*b, *cap);
	}
	memcpy(*b + *n, s, l);
	*n += l;
}

static void dm_tok(char **b, size_t *n, size_t *cap, const char *src, const vtok *t)
{
	char tmp[256];

	if ((t->flags & TF_DIRTY) && t->len <= sizeof(tmp)) {
		size_t l = vlex_spell(src, t, tmp);

		dm_put(b, n, cap, tmp, l);
	} else {
		dm_put(b, n, cap, src + t->off, t->len);
	}
}


/* one macro as "#define NAME[(params)] body\n" (-dM, -dD) */
static void dm_one(char **bp, size_t *np, size_t *capp, uint32_t id, const pmacro *m)
{
	dm_put(bp, np, capp, "#define ", 8);
	dm_put(bp, np, capp, vintern_ents[id].s, vintern_ents[id].len);
	if (m->kind == MK_FUNC) {
		/* spelled from kinds and ids: a canonical copy's src is its body's text only */
		for (uint32_t j = 0; j < m->nplist; j++) {
			const vtok *q = &m->plist[j];

			if (q->kind == T_IDENT)
				dm_put(bp, np, capp, vintern_ents[q->id].s, vintern_ents[q->id].len);
			else
				dm_put(bp, np, capp, q->kind == T_LPAREN ? "(" : q->kind == T_RPAREN ? ")" :
				       q->kind == T_COMMA ? "," : "...", q->kind == T_ELLIPSIS ? 3 : 1);
		}
	}
	dm_put(bp, np, capp, " ", 1);
	for (uint32_t j = 0; j < m->nbody; j++) {
		if (j && (m->body[j].flags & TF_SPACE))
			dm_put(bp, np, capp, " ", 1);
		dm_tok(bp, np, capp, m->src, &m->body[j]);
	}
	dm_put(bp, np, capp, "\n", 1);
}

/* -dD: the directive in the output, where it is (clang's spelling) */
static void dd_text(pp *p, uint32_t id, const pmacro *m)
{
	char *b = NULL;
	size_t n = 0, cap = 0;

	if (m) {
		dm_one(&b, &n, &cap, id, m);
	} else {
		dm_put(&b, &n, &cap, "#undef ", 7);
		dm_put(&b, &n, &cap, vintern_ents[id].s, vintern_ents[id].len);
		dm_put(&b, &n, &cap, "\n", 1);
	}
	if (p->col)
		out_ch(p, '\n');
	out_put(p, b, n);
	p->col = 0;
	free(b);
}

/* the predefined macros, as clang prints them: those clang predefines in its
 * order (dd_order) with their final <built-in> value, then the rest of
 * <built-in>'s directives (the command line's -D/-U) one by one */
VPRIV void dd_flush(pp *p)
{
	uint8_t *done = calloc(p->ndd + 1, 1);

	p->dd_done = 1;
	for (int k = 0; k < p->o->ndd_order; k++) {
		const char *s = p->o->dd_order[k];
		uint32_t id = vintern_find(s, strlen(s), vintern_hash(s, strlen(s))), any = 0;

		for (uint32_t i = 0; id && i < p->ndd; i++)
			if (p->dd_ev[i].id == id)
				done[i] = 1, any = 1;
		if (any && mget(p, id))
			dd_text(p, id, (const pmacro *)(mget(p, id) & ~(uintptr_t)1));
	}
	for (uint32_t i = 0; i < p->ndd; i++)
		if (!done[i])
			dd_text(p, p->dd_ev[i].id, p->dd_ev[i].m);
	free(done);
	free(p->dd_ev);
	p->dd_ev = NULL;
}

/* -dD: a #define (m) or #undef (NULL) in the text, where it is; <built-in>'s
 * are kept for dd_flush */
VPRIV void dd_out(pp *p, uint32_t id, const pmacro *m, bool builtin)
{
	if (builtin && !p->dd_done) {
		if (p->ndd == p->capdd)
			p->dd_ev = realloc(p->dd_ev, (p->capdd = p->capdd ? 2 * p->capdd : 1024) * sizeof(*p->dd_ev));
		p->dd_ev[p->ndd].id = id;
		p->dd_ev[p->ndd++].m = m;
		return;
	}
	if (!p->dd_done)
		dd_flush(p);
	dd_text(p, id, m);
}

int pp_macros(pp *p, char **out, size_t *len)
{
	uint32_t *ids = NULL, nid = 0, capid = 0;
	char *b = NULL;
	size_t n = 0, cap = 0;

	for (uint32_t d = 0; d < NMDIR; d++) {
		const uintptr_t *pg = p->mdir[d];

		if (!pg)
			continue;
		for (uint32_t k = 0; k < MPAGE; k++) {
			const pmacro *m = (const pmacro *)(pg[k] & ~(uintptr_t)1);

			if (!m || m->kind == MK_BUILTIN)
				continue;
			if (nid == capid)
				ids = realloc(ids, (capid = capid ? capid * 2 : 1024) * sizeof(*ids));
			ids[nid++] = d << MPAGE_BITS | k;
		}
	}
	qsort(ids, nid, sizeof(*ids), mname_cmp);
	for (uint32_t i = 0; i < nid; i++) {
		const pmacro *m = (const pmacro *)(mget(p, ids[i]) & ~(uintptr_t)1);
		dm_one(&b, &n, &cap, ids[i], m);
	}
	free(ids);
	*out = b ? b : calloc(1, 1);
	*len = n;
	return 0;
}
