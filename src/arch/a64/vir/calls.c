/* arm64 lowering of VIR: calls, sibling calls and parameters. */

#include "lowering-internal.h"

/*
 * Why the function's calls cannot be sibling calls (NULL: those right before a ret can):
 * the callee must not see the frame, so no slot's address may leave the loads, stores
 * and copies that use it (as LLVM's TailCallElim: no escaping alloca).
 */
/* callee k (a symbol's address) returns twice: setjmp and the like */
static bool vl_twice(vl *L, const vir_ins *k)
{
	cc *c = L->e->c;
	uint32_t s;

	if (k->op != VO_SYMADDR || RKIND(k->a) != RK_SYM)
		return false;
	s = canon_sym(c, RIDX(k->a));
	return (sym_attrs(c, s)->flags & AF_TAIL_RETURNS_TWICE) ||
	       EM_IDENT_IS(c->syms[s].name, "setjmp") || EM_IDENT_IS(c->syms[s].name, "_setjmp") ||
	       EM_IDENT_IS(c->syms[s].name, "sigsetjmp") || EM_IDENT_IS(c->syms[s].name, "__sigsetjmp");
}

VPRIV const char *vl_notail(vl *L, uint8_t *d)
{
	const vir_fn *f = L->f;
	bool ch = true;

	memset(d, 0, f->nins);
	while (ch) {
		ch = false;
		for (uint32_t b = 0; b < f->nblk; b++)
			for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++) {
				vir_ins x = f->ins[i];
				vir_oit it;
				uint32_t *p;

				if (x.op == VO_ASM || x.op == VO_ASMGOTO || x.op == VO_FRAMEADDR || x.op == VO_RETADDR ||
				    x.op == VO_VA_START)
					return "asm or the frame's address";
				if (x.op == VO_CALL && vl_twice(L, &f->ins[x.a]))
					return "a call that returns twice";
				vir_oit_init(&it, (vir_fn *)f, &x, false);
				while ((p = vir_oit_next(&it))) {
					if (f->ins[*p].op != VO_SLOTADDR && !d[*p])
						continue;
					if (x.op == VO_PTRADD && it.pos == 0) {
						ch |= !d[i];
						d[i] = 1;
					} else if (!(((x.op == VO_LOAD || x.op == VO_STORE || x.op == VO_MEMSET) && it.pos == 0) ||
						     (x.op == VO_MEMCPY && it.pos <= 1))) {
						return "a slot's address escapes";
					}
				}
			}
	}
	return NULL;
}

/* from j in block k: past the instructions that only pass *res on (i2p, p2i; under ret32
 * trunc to i32 and ext.4: the low half is the result, the upper half unspecified), *res
 * following them; the first other instruction */
static uint32_t vl_pass(const vl *L, const vir_blk *k, uint32_t j, uint32_t *res)
{
	const vir_fn *f = L->f;

	for (; j < k->first + k->n && f->ins[j].a == *res; j++) {
		const vir_ins *x = &f->ins[j];

		if (!(x->op == VO_I2P || x->op == VO_P2I ||
		      (L->ret32 && ((x->op == VO_TRUNC && x->ty == VT_I32) || (x->op == VO_EXT && VIF_W(x->fl) == 2)))))
			break;
		*res = j;
	}
	return j;
}

/* v without the instructions that only pass a value on (vl_pass's): a reader may name the
 * value under them, since the lowering reads through i2p and p2i */
static uint32_t vl_under(const vl *L, uint32_t v)
{
	const vir_fn *f = L->f;

	for (;;) {
		const vir_ins *x = &f->ins[v];

		if (!(x->op == VO_I2P || x->op == VO_P2I ||
		      (L->ret32 && ((x->op == VO_TRUNC && x->ty == VT_I32) || (x->op == VO_EXT && VIF_W(x->fl) == 2)))))
			return v;
		v = x->a;
	}
}

/* the call at i, in b, is a sibling call: what the ret after it returns is its result (or
 * nothing), also through brs to blocks of only what passes the result on */
VPRIV bool vl_sibling(vl *L, uint32_t b, uint32_t i)
{
	const vir_fn *f = L->f;
	const vir_blk *k = &f->blk[b];
	const vir_ins *r, *t = &f->ins[f->ins[i].a];
	uint32_t res = i, j;

	if (!L->tail || f->ins[i].ty == VT_MULTI ||
	    (t->op == VO_SYMADDR && RKIND(t->a) == RK_SYM && !em_tail_target_ok(L->e, canon_sym(L->e->c, RIDX(t->a)))))
		return false;
	j = vl_pass(L, k, i + 1, &res);
	for (int hop = 0;; hop++) {
		uint32_t q;

		if (j >= k->first + k->n)
			return false;
		r = &f->ins[j];
		if (r->op != VO_BR || hop == 4)
			break;
		k = &f->blk[r->a];
		for (q = 0; q < k->nparam && vl_under(L, f->extra[r->c + q]) != vl_under(L, res); q++)
			;
		if (q < k->nparam)
			res = k->first + q;	/* (else the ret may name the value itself) */
		j = vl_pass(L, k, k->first + k->nparam, &res);
	}
	if (r->op != VO_RET)
		return false;
	return !f->extra[r->c] || (f->extra[r->c] == 1 && vl_under(L, f->extra[r->c + 1]) == vl_under(L, res));
}

VPRIV void vl_call(vl *L, uint32_t b, uint32_t i, const vir_ins *in)
{
	ecx *e = L->e;
	const vir_fn *f = L->f;
	uint32_t ng = VSIG_NGROUPS(f->extra[in->b]);
	vlarg *a;
	int32_t stk;

	if (ng + 1 > L->nop)
		vl_fail(L, VIRFAIL(call_operands));
	a = __builtin_alloca(sizeof(vlarg) * (ng + 1));
	uint8_t gm = 0;	/* the x registers the arguments are in */

	vl_assign(DARWIN(e->c), &f->extra[in->b], a, ng, &stk);
	for (uint32_t k = 0; k < ng; k++) {
		uint8_t r = L->opr[1 + k];

		if (a[k].reg != VR_NONE) {
			if (r != a[k].reg)
				vl_fail(L, VIRFAIL(argument_not_in_register));
			if (!vl_fcls(r) && vl_gx(r) < 8)
				gm |= (uint8_t)(1u << vl_gx(r));
			else if (!vl_fcls(r))
				gm = 0xff;	/* (not an argument register: assume all) */
			continue;
		}
		{
			int sz = log2sz(a[k].size), rs;

			if (r != VR_NONE && vl_fcls(r) && sz >= 2) {
				mem(e, I_STF, sz, 0, vl_gv(r), SP, a[k].stk);
				continue;
			}
			if (r != VR_NONE && vl_fcls(r)) {
				word(e, f_out(a[k].size == 8, R_X16, vl_gv(r)));
				rs = R_X16;
			} else {
				rs = vl_ix(L, 1 + k, R_X16);
			}
			mem(e, I_ST, sz, 0, rs, SP, a[k].stk);
		}
	}
	if (!stk && vl_sibling(L, b, i)) {
		ins *t;

		if (L->o->callee[1] && L->opr[0] != VR_NONE)	/* (the callee's address may be in x16: through x17) */
			L->risk = true;
		vl_fcsave(L, true, L->opr[0] != VR_NONE ? R_X17 : R_X16);
		t = put(e, I_TAIL);
		t->sh = L->bare;

		if (L->opr[0] == VR_NONE) {
			const vir_ins *k = &f->ins[in->a];

			if (k->op != VO_SYMADDR || k->b || k->c)
				vl_fail(L, VIRFAIL(indirect_callee_without_register));
			t->cond = 2;
			t->ref = f->ins[in->a].a;
			note_ref(e, t->ref);
		} else {
			t->cond = 3;
			t->rn = (uint8_t)vl_gx(L->opr[0]);
		}
		e->reach = false;
		L->skip = i + 1;
		return;
	}
	if (stk > e->outmax)
		e->outmax = stk;
	e->calls = true;
	if (L->opr[0] == VR_NONE) {
		const vir_ins *k = &f->ins[in->a];

		if (k->op != VO_SYMADDR || k->b || k->c)
			vl_fail(L, VIRFAIL(indirect_callee_without_register));
		note_ref(e, k->a);
		ins *c = put(e, I_BL);

		c->ref = k->a;
		c->cond = CALL_ARGS, c->sh = gm;
	} else {
		ins *c = put(e, I_BLR);

		c->rn = (uint8_t)vl_gx(L->opr[0]);
		c->cond = CALL_ARGS, c->sh = gm;
	}
}

VPRIV void vl_param(vl *L, const vir_ins *in, uint8_t res)
{
	ecx *e = L->e;
	const vir_fn *f = L->f;
	uint32_t ng = VSIG_NGROUPS(f->extra[f->sig]);
	vlarg *a;
	int32_t stk;

	if (ng > VL_MAXOP || in->a >= ng)
		vl_fail(L, VIRFAIL(lowering_parameter));
	a = __builtin_alloca(sizeof(vlarg) * ng);
	vl_assign(DARWIN(e->c), &f->extra[f->sig], a, ng, &stk);
	if (a[in->a].reg != VR_NONE) {
		if (res != VR_NONE && res != a[in->a].reg)
			vl_fail(L, VIRFAIL(parameter_not_in_register));
		return;
	}
	if (res == VR_NONE)
		return;
	if (vl_fcls(res)) {
		mem(e, I_LDF, in->ty == VT_F64 ? 3 : 2, 0, vl_gv(res), R_FP, 16 + a[in->a].stk);
	} else {
		mem(e, I_LD, log2sz(a[in->a].size), 0, vl_gx(res), R_FP, 16 + a[in->a].stk);
	}
}
