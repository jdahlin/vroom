/* vcc preprocessor: the macro table, #define, content-canonical macros */
#include "pp-internal.h"

/* ---- content-canonical macros (definitions in the main file and <built-in>) ---- */

#define PP_MACRO_CANONICAL_TABLE_BITS 18
static _Atomic(const pp_macro *) pp_macro_canonical_table[1u << PP_MACRO_CANONICAL_TABLE_BITS];
const lex_token pp_macro_empty_body[1];

static uint32_t pp_macro_hash(const pp_macro *m)
{
	uint64_t h = m->name * 0x9E3779B97F4A7C15ull ^ ((uint64_t)m->kind << 8 | m->flags | (uint64_t)m->nparam << 16);

	for (uint32_t j = 0; j < m->nbody; j++) {
		const lex_token *v = &m->body[j];
		uint32_t x = v->kind | (v->flags & (LEX_TOKEN_FLAG_SPACE_BEFORE | LEX_TOKEN_FLAG_SPLICED)) << 8 | (m->pidx ? m->pidx[j] : 0) << 16;

		h = (h ^ x) * 0x100000001B3ull;
		h = (h ^ vintern_hash(m->src + v->off, v->len)) * 0x100000001B3ull;
	}
	return (uint32_t)(h ^ h >> 32);
}

static bool pp_macro_is_same(const pp_macro *a, const pp_macro *b)
{
	if (a->name != b->name || a->kind != b->kind || a->flags != b->flags || a->nparam != b->nparam ||
	    a->nbody != b->nbody || !a->pidx != !b->pidx)
		return false;
	for (uint32_t j = 0; j < a->nbody; j++) {
		const lex_token *x = &a->body[j], *y = &b->body[j];

		if (x->kind != y->kind || x->len != y->len || ((x->flags ^ y->flags) & (LEX_TOKEN_FLAG_SPACE_BEFORE | LEX_TOKEN_FLAG_SPLICED)) ||
		    (a->pidx && a->pidx[j] != b->pidx[j]) || memcmp(a->src + x->off, b->src + y->off, x->len))
			return false;
	}
	return true;
}

/* the shared macro equal to m (m's body may be in per-TU memory) */
static const pp_macro *pp_macro_canonical_get(const pp_macro *m)
{
	uint32_t h = pp_macro_hash(m);
	pp_macro *c = NULL;

	for (uint32_t i = h >> (32 - PP_MACRO_CANONICAL_TABLE_BITS), k = 0; k < 64; i = (i + 1) & ((1u << PP_MACRO_CANONICAL_TABLE_BITS) - 1), k++) {
		const pp_macro *e = atomic_load_explicit(&pp_macro_canonical_table[i], memory_order_acquire);

		if (!e) {
			if (!c) {
				/* a copy in shared memory: body tokens rebased on a copy of their text */
				uint32_t lo = m->nbody ? m->body[0].off : 0;
				uint32_t hi = m->nbody ? m->body[m->nbody - 1].off + m->body[m->nbody - 1].len : 0;
				char *t = pp_shared_allocate(hi - lo + 64);
				lex_token *v = m->nbody ? pp_shared_allocate(m->nbody * sizeof(lex_token)) : (lex_token *)pp_macro_empty_body;

				memcpy(t, m->src + lo, hi - lo);
				memset(t + (hi - lo), 0, 64);
				for (uint32_t j = 0; j < m->nbody; j++) {
					v[j] = m->body[j];
					v[j].off -= lo;
				}
				c = pp_shared_allocate(sizeof(*c));
				*c = *m;
				c->body = v;
				c->src = t;
				c->hash = h;
				if (m->pidx) {
					uint8_t *x = pp_shared_allocate(m->nbody + 1);

					memcpy(x, m->pidx, m->nbody);
					c->pidx = x;
				}
			}
			if (atomic_compare_exchange_strong_explicit(&pp_macro_canonical_table[i], &e, c, memory_order_acq_rel,
								    memory_order_acquire))
				return c;
		}
		if (e->hash == h && pp_macro_is_same(e, m))
			return e;
	}
	return NULL;	/* table crowded here: keep the per-TU one */
}

/* -Wvariadic-macros (-pedantic): "#define M(args...)"; out of line, pp_macro_define() is hot */
static __attribute__((noinline, cold)) void pp_macro_named_variadic_warn(pp *p, pp_context *c, const lex_token *vt, uint32_t b, uint32_t e)
{
	for (uint32_t i = b + 2; i + 1 < e && vt[i].kind != LEX_TOKEN_RIGHT_PAREN; i++)
		if (vt[i].kind == LEX_TOKEN_IDENTIFIER && vt[i + 1].kind == LEX_TOKEN_ELLIPSIS) {
			static const char *const g[] = { "pedantic", NULL };
			pp_dinfo d = { "warning", c->loc + vt[i + 1].off, NULL, 0, NULL, 0, NULL, 0 };

			if (pp_warnx(p, "variadic-macros", false, g, &d, "named variadic macros are a GNU extension"))
				PP_HEADER_CACHE_REGION_REJECT(p);	/* (a replay would not say it) */
			return;
		}
}

VPRIV void pp_macro_define(pp *p, pp_context *c, uint32_t b, uint32_t e)
{
	const lex_token *vt = c->vt;
	const pp_macro *m;
	const char *err;
	uint32_t eoff;
	bool site = c->f != &p->builtin && c->fe != 0;	/* a header: one macro per directive */
	_Atomic(const pp_macro *) *slot = site ? &((pp_skeleton *)c->fi)->mac[c->di - 1] : NULL;

	if (b >= e || vt[b].kind != LEX_TOKEN_IDENTIFIER) {
		PP_ERROR(p, c->loc + vt[b - 1].off, "macro name must be an identifier");
		return;
	}
	if (vt[b].id == KW_defined) {
		PP_ERROR(p, c->loc + vt[b].off, "'defined' cannot be used as a macro name");
		return;
	}
	m = slot ? atomic_load_explicit(slot, memory_order_acquire) : NULL;
	if (!m || unlikely(m->flags & PP_MACRO_FLAG_UNKNOWN_TOKEN))	/* -Winvalid-pp-token (a cached site knows it has none) */
		for (uint32_t i = b + 1; i < e; i++)
			if (unlikely(vt[i].kind == LEX_TOKEN_UNKNOWN))
				pp_invalid_tok(p, c->src + vt[i].off, vt[i].len, c->loc + vt[i].off);
	if (!m) {
		pp_macro *n = site ? pp_shared_allocate(sizeof(*n)) : varena_alloc(&p->perm, sizeof(*n));

		err = pp_define_parse(vt, b, e, n, site ? pp_memory_shared_allocate : pp_memory_permanent_allocate, &p->perm, &eoff);
		if (err) {
			/* pp_define_parse's two messages, each a format of its own (its id) */
			if (err[0] == '\'')
				PP_ERROR(p, c->loc + vt[eoff].off, "'##' cannot appear at either end of a macro expansion");
			else
				PP_ERROR(p, c->loc + vt[eoff].off, "invalid macro parameter list");
			return;
		}
		n->src = c->src;
		if (!n->nbody)
			n->body = pp_macro_empty_body;
		if ((n->flags & PP_MACRO_FLAG_BAD_HASH) && !p->o->asm_mode) {
			PP_ERROR(p, c->loc + vt[eoff].off, "'#' is not followed by a macro parameter");
			return;
		}
		if (site) {
			const pp_macro *z = NULL;

			if (!atomic_compare_exchange_strong_explicit(slot, &z, n, memory_order_acq_rel, memory_order_acquire))
				n = (pp_macro *)z;	/* the same definition, built by another TU */
			m = n;
		} else {
			m = unlikely(p->keep_macro_text) ? NULL : pp_macro_canonical_get(n);
			if (!m)
				m = n;
		}
	} else if ((m->flags & PP_MACRO_FLAG_BAD_HASH) && !p->o->asm_mode) {
		/* cached by an assembler TU; body offsets are this file's */
		uint32_t j = 0;

		while (j < m->nbody && !(m->body[j].kind == LEX_TOKEN_HASH && !(j + 1 < m->nbody && m->pidx && m->pidx[j + 1]) &&
					 !(j + 1 < m->nbody && m->body[j + 1].kind == LEX_TOKEN_IDENTIFIER && m->body[j + 1].id == KW_VA_OPT)))
			j++;
		PP_ERROR(p, c->loc + (j < m->nbody ? m->body[j].off : vt[b].off), "'#' is not followed by a macro parameter");
		return;
	}
	if (unlikely((m->flags & PP_MACRO_FLAG_VARIADIC) && p->o->nwargs) && c->f != &p->builtin && pp_warning_is_possible(p, PP_WARNING_VARIADIC_MACRO))
		pp_macro_named_variadic_warn(p, c, vt, b, e);
	{
		/* -Wmacro-redefined: a different definition replacing a live one */
		uintptr_t o = pp_macro_table_get(p, m->name);
		const pp_macro *om = (const pp_macro *)(o & ~(uintptr_t)1);

		if (unlikely(o && !(o & 1) && om != m && !om->builtin && !pp_macro_is_same(om, m))) {
			uint32_t nloc = c->loc + vt[b].off;
			pp_span r = { nloc, nloc + vt[b].len };
			pp_dinfo d = { "warning", nloc, &r, 0, NULL, 0, NULL, 0 };

			/* (in <built-in>: the predefines are exempt, -D on the command line is not, as clang) */
			if ((c->f != &p->builtin || vt[b].off >= p->o->predef_cmdl) &&
			    pp_warnx(p, "macro-redefined", true, NULL, &d, "'%.*s' macro redefined",
							    (int)vt[b].len, c->src + vt[b].off))
				pp_macro_note_at(p, om, nloc, "previous definition is here");
		}
	}
	pp_macro_define_record(p, m->name, m);
	if (unlikely(p->o->dump_defines))
		pp_macro_define_event_record(p, m->name, m, c->f == &p->builtin);
}
