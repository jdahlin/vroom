/* vcc code generator: reference logging and symbol bindings */
#include "emitimpl.h"

VPRIV void note_ref(ecx *e, uint32_t ref)
{
	if (!e->reach || e->dry)
		return;
	claim_ref(e, ref);
}

VPRIV void claim_ref(ecx *e, uint32_t ref)
{
	emitter *E = e->E;
	cc *c = e->c;

	rlog(e, RL_REF, ref);
	if (RKIND(ref) == RK_STR) {
		uint32_t i = RIDX(ref);

		if (!atomic_load_explicit(&E->strdone[i], memory_order_relaxed) &&
		    !atomic_exchange(&E->strdone[i], 1))
			enqueue(E, 0x80000000u | i);
		return;
	}
	if (RKIND(ref) != RK_SYM)
		return;
	{
		uint32_t s = canon_sym(c, RIDX(ref));
		const sym *y = &c->syms[s];

		if (unlikely((y->flags & SF_WEAK) && !(y->flags & SF_DEFINED)) &&
		    !atomic_load_explicit(&E->extref[s], memory_order_relaxed) &&
		    !atomic_exchange(&E->extref[s], 1))
			e->weakref[e->nweakref++] = s;
		if (atomic_load_explicit(&E->claimed[s], memory_order_relaxed))
			return;
		if (y->kind == S_FUNC) {
			if (unlikely(y->flags & SF_LAZY))
				efatal(e, y->loc, "internal error: '%s' is referenced but its body was never parsed",
				       vintern_ents[y->name].s);
			if (!(y->flags & SF_DEFINED) || y->sc != SC_STATIC || !y->body)
				return;
		} else if (y->kind == S_VAR) {
			bool local_static = y->depth > 0 && y->sc == SC_STATIC;
			bool file_static = y->depth == 0 && y->sc == SC_STATIC;

			if (!(local_static || file_static || !y->name))
				return;
			if (!(y->flags & (SF_DEFINED | SF_TENTATIVE)) && !local_static && y->name)
				return;
		} else {
			return;
		}
		if (!atomic_exchange(&E->claimed[s], 1))
			enqueue(E, s);
	}
}

/* ---------------- bindings ---------------- */

VPRIV bind *binding(ecx *e, uint32_t s)
{
	return &e->b[s];
}

VPRIV int regvar_reg(cc *c, uint32_t s)
{
	const attrs *a = sym_attrs(c, s);
	const char *n;

	if (c->syms[s].sc != SC_REGISTER || !a->asmname)
		return -1;
	n = c->strs[a->asmname - 1].s;
	if (!strcmp(n, "sp"))
		return SP;
	if ((n[0] == 'x' || n[0] == 'w') && n[1] >= '0' && n[1] <= '9')
		return atoi(n + 1);
	return -1;
}

VPRIV int32_t bind_slot(ecx *e, uint32_t s)
{
	cc *c = e->c;
	bind *b = binding(e, s);
	qty t = c->syms[s].ty;
	uint32_t al = talign(c, t);

	if (c->syms[s].align > al)
		al = c->syms[s].align;
	b->inst = e->f->inst;
	b->kind = B_SLOT;
	b->kv = 0;
	b->reg = 0xff;
	if (c->syms[s].sc == SC_REGISTER) {
		int rr = regvar_reg(c, s);

		if (rr >= 0)
			b->reg = (uint8_t)rr;
	}
	b->slot = slot(e, tsize(c, t) ? tsize(c, t) : 1, al);
	return b->slot;
}

/* the lvalue of a local (auto/param/register) variable */
VPRIV lval local_lval(ecx *e, uint32_t s)
{
	cc *c = e->c;
	bind *b = binding(e, s);
	lval l = { 0 };
	qty t = c->syms[s].ty;

	l.align = talign(c, t);
	if (b->kind == B_NONE || b->inst == 0) {
		/* declaration not executed (jumped over, or dead): give it a slot */
		bind_slot(e, s);
	} else if (b->kind == B_CONST) {
		val v = b->v;
		int r;

		bind_slot(e, s);
		if (is_wide(c, t)) {
			toreg2(e, &v, is_signed(c, t));
			mem(e, I_ST, 3, 0, v.r, FRAME, b->slot);
			mem(e, I_ST, 3, 0, v.r2, FRAME, b->slot + 8);
		} else {
			r = toreg(e, &v);
			mem(e, I_ST, log2sz(tsize(c, t)), 0, r, FRAME, b->slot);
		}
		val_free(e, &v);
	}
	if (b->kind == B_INDIRECT) {
		int r = treg(e);

		mem(e, I_LD, 3, 0, r, FRAME, b->slot);
		l.k = L_REG;
		l.r = (uint8_t)r;
		return l;
	}
	l.k = L_FRAME;
	l.off = b->slot;
	return l;
}

VPRIV uint32_t sym_align(cc *c, uint32_t s)
{
	uint32_t a = talign(c, c->syms[s].ty);

	if (c->syms[s].align > a)
		a = c->syms[s].align;
	return a;
}

VPRIV bool is_local_var(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	return y->kind == S_VAR && y->depth > 0 && (y->sc == SC_AUTO || y->sc == SC_REGISTER || (y->flags & SF_PARAM) ||
						   (y->sc == SC_NONE && !(y->flags & SF_GLOBAL)));
}
