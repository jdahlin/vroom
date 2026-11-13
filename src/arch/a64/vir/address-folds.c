/* arm64 lowering of VIR: folding an address or an index chain into the instruction that uses it. */

#include "lowering-internal.h"

/* ptradd i (operands collected) with a constant offset, used once, by the next
 * instruction as its address: that adds the offset (its base's register is still
 * there unless an edit before it writes it) */
VPRIV bool vl_pfold(vl *L, uint32_t b, uint32_t i)
{
	const vir_function *f = L->f;
	const vir_insn *in = &f->insns[i], *nx = &f->insns[i + 1];
	uint8_t base = L->opr[0];
	int64_t k;

	if (L->opr[1] != VIR_REGISTER_NONE || !vl_iconst(f, in->b) || base == VIR_REGISTER_NONE || L->uses[i] != 1 ||
	    L->o->slot[i] != VIR_BLOCK_NONE || i + 1 >= f->blocks[b].first + f->blocks[b].insn_count || nx->a != i ||
	    (nx->op != VIR_OP_LOAD && !(nx->op == VIR_OP_STORE && nx->b != i)) || vl_vecacc(f, nx))
		return false;
	k = vl_k64(&f->insns[in->b]);
	if (k < -256 || k > 32760)
		return false;
	for (uint32_t x = L->ei; x < L->eend && L->o->edit[x].pos <= i + 1; x++)
		if (L->o->edit[x].pos == i + 1 && L->o->edit[x].dst == base)
			return false;
	L->pval = i;
	L->pbase = vl_gx(base);
	L->poff = k;
	return true;
}

enum { VX_MEM = 1, VX_EXT, VX_LSL, VX_MADDL };

/* the index operand (b) of c, a ptradd, add or sub of 64 bits, built by the instructions right
 * before c, each used once by the next, that c folds:
 *   VX_MEM   c a ptradd used once by the next instruction, a load or store through it:
 *            [base, index], the index extended (ext.4s/4u) and scaled by the access size (mul or
 *            shl);
 *   VX_EXT   a + (ext.4s/4u x << 0-4): add/sub (extended register);
 *   VX_LSL   a + (x << k): add/sub (shifted register);
 *   VX_MADDL a + ext.4s/4u x * K (a ptradd or add): smaddl/umaddl with K in a register.
 * The skipped instructions write no register, so x and a are still where they were when no
 * allocator edit comes in between.  x->first: the chain's first instruction, x->xv: x. */
VPRIV bool vl_cinfo(const vl *L, uint32_t b, uint32_t c, vlx *x)
{
	const vir_function *f = L->f;
	const vir_block *k = &f->blocks[b];
	const vir_insn *in = &f->insns[c], *m = &f->insns[c + 1];
	uint32_t j = in->b, lo = k->first + k->parameter_count, end = k->first + k->insn_count, w = 0;
	bool mem, sh = false;
	int kk = 0;

	if ((in->op != VIR_OP_PTRADD && in->op != VIR_OP_ADD && in->op != VIR_OP_SUB) || in->type == VIR_TYPE_I32 || vl_isconst(f, j) ||
	    (vl_isconst(f, in->a) && in->op != VIR_OP_PTRADD))
		return false;
	mem = in->op == VIR_OP_PTRADD && c + 1 < end && m->a == c && vl_once(L, c) &&
	      (m->op == VIR_OP_LOAD || (m->op == VIR_OP_STORE && m->b != c)) && !vl_vecacc(f, m);
	if (mem)
		w = VIR_INSN_FLAG_WIDTH_LOG2(m->flags);
	*x = (vlx){ c, j, mem ? VX_MEM : 0, 3, 0, 0 };
	if (j + 1 == c && j > lo && vl_once(L, j) && !vl_isconst(f, f->insns[j].a) && vl_iconst(f, f->insns[j].b) &&
	    (f->insns[j].op == VIR_OP_MUL || f->insns[j].op == VIR_OP_SHL)) {
		const vir_insn *q = &f->insns[j], *e = &f->insns[q->a];
		int64_t K = vl_ival(&f->insns[q->b]);

		kk = q->op == VIR_OP_SHL ? (int)K : vl_pow2(&f->insns[q->b]);
		if (q->op == VIR_OP_SHL && (K < 0 || K > 63))
			kk = -1;
		if (mem ? kk == (int)w && w : kk > 0) {
			x->first = j, x->xv = q->a, sh = true;
			j = q->a;
		} else if (!mem && q->op == VIR_OP_MUL && in->op != VIR_OP_SUB && q->a + 1 == j && q->a >= lo && vl_once(L, q->a) &&
			   e->op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(e->flags) == 2 && e->type == VIR_TYPE_I64 && !vl_isconst(f, e->a) &&
			   ((e->flags & VIR_INSN_FLAG_MS) ? K >= INT32_MIN && K <= INT32_MAX : K >= 0 && K <= UINT32_MAX)) {
			*x = (vlx){ q->a, e->a, VX_MADDL, (e->flags & VIR_INSN_FLAG_MS) ? 6 : 2, 0, K };
			return true;
		}
	}
	/* The shared no-overflow rule also admits shift.i32 -> sext.i64.
	 * Preserve the allocator's lifetimes and fold into the native SXTW form. */
	unsigned signed_scale;
	if(j+1==c && j>lo && vl_once(L,j) && vl_once(L,j-1) &&
	   vir_sign_extend_shift_match(f,j-1,&signed_scale) && signed_scale<=4 && !vl_isconst(f,f->insns[j-1].a)) {
		bool scaled_mem=mem && signed_scale==w;
		*x=(vlx){j-1,f->insns[j-1].a,scaled_mem?VX_MEM:VX_EXT,6,
		         scaled_mem?(signed_scale!=0):signed_scale,0};
		return true;
	}
	if (j + 1 == x->first && j >= lo && vl_once(L, j) && f->insns[j].op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(f->insns[j].flags) == 2 &&
	    f->insns[j].type == VIR_TYPE_I64 && !vl_isconst(f, f->insns[j].a) && (mem || kk <= 4)) {
		x->first = j, x->xv = f->insns[j].a, x->opt = f->insns[j].flags & VIR_INSN_FLAG_MS ? 6 : 2;
	}
	x->sh = mem ? sh : (uint8_t)(sh ? kk : 0);
	if (!mem)
		x->kind = x->opt != 3 ? VX_EXT : sh ? VX_LSL : 0;
	return x->kind != 0;
}

/* the chain's consumer c: its folded form (VX_MEM: the next load or store's address) */
static void vl_cact(vl *L, uint32_t c, uint8_t res)
{
	ecx *e = L->e;
	uint32_t op = L->f->insns[c].op;
	const vlx *x = &L->xx;
	int a = vl_ix(L, 0, R_X16), rd = res == VIR_REGISTER_NONE ? R_X16 : vl_gx(res), t;	/* (a base in x16: vl_store takes x17) */

	L->xc = VIR_BLOCK_NONE;
	switch (x->kind) {
	case VX_MEM:
		L->xval = c, L->xbase = a, L->xopt = x->opt, L->xsh = x->sh;
		return;
	case VX_EXT: {
		ins *n = put(e, op == VIR_OP_SUB ? I_SUB : I_ADD);	/* (extended register: cond 8 + option) */

		n->sz = 1, n->rd = (uint8_t)rd, n->rn = (uint8_t)a, n->rm = (uint8_t)L->xreg, n->cond = (uint8_t)(8 + x->opt), n->sh = x->sh;
		return;
	}
	case VX_LSL:
		op3s(e, op == VIR_OP_SUB ? I_SUB : I_ADD, 1, rd, a, L->xreg, 0, x->sh);
		return;
	default:	/* VX_MADDL */
		t = L->xreg == R_X17 || a == R_X17 ? R_X16 : R_X17;
		movi(e, 1, t, x->k);
		op4(e, x->opt == 6 ? I_SMADDL : I_UMADDL, 1, rd, L->xreg, t, a);
		return;
	}
}

/* instruction i (lowered with its operands collected) is in a chain vl_cinfo folds: true when it
 * is handled (nothing to emit, or the consumer's folded form).  Decided at the chain's first
 * instruction, where the index's register is known: between it and the consumer only remats and
 * reloads into other registers (a constant operand, the base), none before the load or store */
VPRIV bool vl_cfold(vl *L, uint32_t b, uint32_t i, uint8_t res)
{
	const vir_function *f = L->f;
	vlx x;

	if (L->xc != VIR_BLOCK_NONE && i > L->xfirst && i <= L->xc) {
		if (i == L->xc)
			vl_cact(L, i, res);
		return true;
	}
	for (uint32_t c = i; c <= i + 2 && c < f->blocks[b].first + f->blocks[b].insn_count; c++) {
		uint32_t op = f->insns[c].op, end;
		uint8_t r;

		if (op != VIR_OP_PTRADD && op != VIR_OP_ADD && op != VIR_OP_SUB)
			continue;
		if (!vl_cinfo(L, b, c, &x) || x.first != i)
			return false;
		r = L->opr[i == c ? 1 : 0];
		if (r == VIR_REGISTER_NONE || vl_fcls(r) || vl_gx(r) == R_X16 || vl_gx(r) == R_X17)
			return false;
		end = c + (x.kind == VX_MEM);
		for (uint32_t q = L->ei; q < L->eend && L->o->edit[q].pos <= end; q++) {	/* (those before are at i or earlier) */
			const vir_edit *d = &L->o->edit[q];

			if (d->pos > i && !(d->pos <= c && (d->kind == VIR_EDIT_REMATERIALIZE || d->kind == VIR_EDIT_RELOAD) && d->dst != r))
				return false;
		}
		L->xx = x, L->xfirst = i, L->xc = c, L->xreg = vl_gx(r);
		if (i == c)
			vl_cact(L, i, res);
		return true;
	}
	return false;
}
