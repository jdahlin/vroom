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

			/* (in <built-in>: the predefines are exempt, -D on the command line is not, as clang) */
			if ((c->f != &p->builtin || vt[b].off >= p->o->predef_cmdl) &&
			    pp_warnx(p, "macro-redefined", true, NULL, &d, "'%.*s' macro redefined",
							    (int)vt[b].len, c->src + vt[b].off))
				macro_note_(p, om, nloc, "previous definition is here");
		}
	}
	mdef(p, m->name, m);
	if (unlikely(p->o->dump_defines))
		dd_out(p, m->name, m, c->f == &p->builtin);
}
