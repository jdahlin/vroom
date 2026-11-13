/* vcc front end: scopes, binding and lookup of symbols and tags */
#include "ccimpl.h"

/* ---------------- scopes and symbols ---------------- */

VPRIV uint32_t *dslot(cc *c, uint32_t **dir, uint32_t id)
{
	uint32_t **pg = &dir[id >> DIR_BITS];

	if (unlikely(!*pg)) {
		*pg = halloc(c, DIR_PAGE * sizeof(uint32_t));
		memset(*pg, 0, DIR_PAGE * sizeof(uint32_t));
	}
	return &(*pg)[id & (DIR_PAGE - 1)];
}

VPRIV void scope_open(cc *c)
{
	if (c->depth >= 4095)
		fatal(c, tok(c)->loc, "scopes nested too deeply");
	c->smark[++c->depth] = c->nsstack;
}

/* the scope's bindings undone; chk: warn of its unused variables */
static inline __attribute__((always_inline)) void scope_pop(cc *c, bool chk)
{
	uint32_t m = c->smark[c->depth];

	/* (clang: none after an uncompilable error in the function, DefaultError warnings aside) */
	if (chk && c->depth > 0 && c->nsstack > m &&
	    (uint32_t)(c->errors - c->fnerr) <= c->nconv - c->fnconv)
		unused_vars(c, m);

	while (c->nsstack > m) {
		uint32_t s = c->sstack[--c->nsstack];
		sym *y = &c->syms[s];

		/* (bind allocated the page) */
		(y->kind == S_TAG ? c->tdir : c->odir)[y->name >> DIR_BITS][y->name & (DIR_PAGE - 1)] = y->shadow;
	}
	c->depth--;
}

VPRIV void scope_close(cc *c)
{
	scope_pop(c, true);
}

/* a prototype's scope: parameters and tags, nothing to warn of */
VPRIV void scope_close_proto(cc *c)
{
	scope_pop(c, false);
}

/* Save bindings from the defining parameter list for its function body. */
VPRIV void proto_binding(cc *c, uint32_t s)
{
	if (c->in_proto != 1)
		return;
	if (c->nptag == c->cptag) {
		uint32_t cap = c->cptag ? c->cptag * 2 : 16;
		uint32_t *p = halloc(c, (size_t)cap * sizeof(*p));

		if (c->nptag)
			memcpy(p, c->ptag, (size_t)c->nptag * sizeof(*p));
		c->ptag = p;
		c->cptag = cap;
	}
	c->ptag[c->nptag++] = s;
}

VPRIV uint32_t sym_new(cc *c, int kind, uint32_t name, qty ty, uint32_t loc)
{
	uint32_t i = c->nsym++;
	sym *s = &c->syms[i];

	memset(s, 0, sizeof(*s));
	s->kind = (uint8_t)kind;
	s->name = name;
	s->ty = ty;
	s->loc = loc;
	s->depth = c->depth;
	return i;
}

/* bind s in the current scope */
VPRIV void sym_bind(cc *c, uint32_t s)
{
	sym *y = &c->syms[s];
	uint32_t *slot;

	if (!y->name)
		return;
	slot = dslot(c, y->kind == S_TAG ? c->tdir : c->odir, y->name);
	y->shadow = *slot;
	y->depth = c->depth;
	*slot = s;
	c->sstack[c->nsstack++] = s;
}

/* in a lazy body (lazy.c): the file-scope binding visible at its definition */
static __attribute__((noinline)) uint32_t lz_visible(cc *c, uint32_t s)
{
	while (s >= c->lz_wm && c->syms[s].depth == 0)
		s = c->syms[s].shadow;
	return s;
}

/* a lookup in a lazy body or while recording a region (frontend/c/replay.c) */
static __attribute__((noinline)) uint32_t lookup_slow(cc *c, int ns, uint32_t id, uint32_t s)
{
	if (c->dr_on)
		dr_read(c, ns, id, s);
	return c->lz_wm && s >= c->lz_wm ? lz_visible(c, s) : s;
}

VPRIV uint32_t lookup(cc *c, uint32_t id)
{
	uint32_t s = dget(c->odir, id);

	return unlikely(c->lz_wm | c->dr_on) ? lookup_slow(c, 0, id, s) : s;
}

VPRIV uint32_t lookup_tag(cc *c, uint32_t id)
{
	uint32_t s = dget(c->tdir, id);

	return unlikely(c->lz_wm | c->dr_on) ? lookup_slow(c, 1, id, s) : s;
}

/*
 * x86-64 Linux: __builtin_ms_va_list, the Microsoft va_list (char *) of ms_abi functions, declared
 * where it is first named (typedef_id) rather than with the TU's other builtin types, so that the
 * symbol numbers of a TU without it, and with them the code generator's local names, stay as they
 * were.  Windows declares it with __builtin_va_list (translation-unit.c).
 */
VPRIV __attribute__((noinline, cold)) bool ms_valist_typedef(cc *c, uint32_t id)
{
	uint32_t s;

	if (c->o->target != PP_LINUX_X86_64 || memcmp(vintern_ents[id].s, "__builtin_ms_va_list", 20))
		return false;
	if (unlikely(c->dr_on))
		dr_pragma(c, 2);	/* (a declaration-cache region does not replay this) */
	s = sym_new(c, S_TYPEDEF, id, ty_ptr(c, prim(TY_CHAR)), 0);
	sym_bind(c, s);
	return true;
}
