/* vcc code generator: reference logging and symbol bindings */
#include "../../../codegen/emitimpl.h"

/* ---------------- bindings ---------------- */

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

VPRIV int64_t bind_slot(ecx *e, uint32_t s)
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

/* the callee-saved register local s of the emitted function lives in, or -1 */
VPRIV int lreg_of(ecx *e, uint32_t s)
{
	for (int i = 0; i < e->nlr; i++)
		if (e->lrsym[i] == s)
			return e->lrreg[i];
	return -1;
}

/* bind s to its register (lreg_of) for this instance */
VPRIV lval bind_lreg(ecx *e, uint32_t s)
{
	bind *b = binding(e, s);
	lval l = { .k = L_VREG };

	b->inst = e->f->inst;
	b->kind = B_LREG;
	b->kv = 0;
	b->reg = 0xff;
	b->v = vreg(lreg_of(e, s));
	l.r = b->v.r;
	l.align = talign(e->c, e->c->syms[s].ty);
	return l;
}

/* the lvalue of a local (auto/param/register) variable */
VPRIV lval local_lval(ecx *e, uint32_t s)
{
	cc *c = e->c;
	bind *b = binding(e, s);
	lval l = { 0 };
	qty t = c->syms[s].ty;

	l.align = talign(c, t);
	if (b->kind == B_LREG && b->inst) {
		l.k = L_VREG;
		l.r = b->v.r;
		return l;
	}
	if ((b->kind == B_NONE || b->inst == 0) && lreg_of(e, s) >= 0)
		return bind_lreg(e, s);	/* declaration jumped over */
	if (b->kind == B_REG && b->inst) {
		/* read-only, its address never taken: a copy in a fresh slot will do */
		l.k = L_FRAME;
		l.off = slot(e, 8, 8);
		mem(e, I_ST, 3, 0, b->v.r, FRAME, l.off);
		return l;
	}
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
