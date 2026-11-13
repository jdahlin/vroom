/* arm64 lowering of VIR: compares, their conditions and the branches that read them (cmp, ccmp, cbz, tbz, folds into a branch). */

#include "lowering-internal.h"

VPRIV const uint8_t a64_vir_integer_condition_codes[10] = { C_EQ, C_NE, C_LT, C_LE, C_GT, C_GE, C_LO, C_LS, C_HI, C_HS };

VPRIV const int8_t a64_vir_float_condition_codes[8] = { C_EQ, C_NE, C_MI, C_LS, C_GT, C_GE, C_VS, -1 };

VPRIV const uint8_t a64_vir_condition_swapped[10] = { 0, 1, 4, 5, 2, 3, 8, 9, 6, 7 };	/* icmp codes, operands swapped */

VPRIV int a64_vir_integer_compare_lower(a64_vir_context *L, const vir_insn *in)	/* the flags for an icmp; its code (swapped if the operands were) */
{
	ecx *e = L->e;
	int x = a64_vir_value_is_64_bit(L->f, in->a), a;

	if (L->opr[0] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(L->f, in->a) && L->opr[1] != VIR_REGISTER_NONE) {
		opi(e, I_SUBSI, x, ZR, a64_vir_general_register(L->opr[1]), a64_vir_constant_integer(&L->f->insns[in->a]));
		return a64_vir_condition_swapped[in->flags & 15];
	}
	a = a64_vir_operand_general_register(L, 0, R_X16);
	if (L->opr[1] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(L->f, in->b)) {
		int64_t k = a64_vir_constant_integer(&L->f->insns[in->b]);

		opi(e, I_SUBSI, x, ZR, a, k);
	} else {
		op3(e, I_SUBS, x, ZR, a, a64_vir_operand_general_register(L, 1, R_X17));
	}
	return in->flags & 15;
}

VPRIV void a64_vir_float_compare_lower(a64_vir_context *L, const vir_insn *in)
{
	int d = L->f->insns[in->a].type == VIR_TYPE_F64;

	word(L->e, 0x1e202000u | (unsigned)d << 22 | (unsigned)a64_vir_operand_float_register(L, 1) << 16 | (unsigned)a64_vir_operand_float_register(L, 0) << 5);
}

/*
 * Compares joined into one condition: icmp, then (icmp, and/or of the value so far and
 * that compare) once or more, then the brif or select reading the last and/or; every
 * value read once, nothing in between (the builder's virbuild_conditions_merge lays `a && b` out so).
 * One cmp, then a ccmp per further compare: no register holds a truth value.
 */
VPRIV bool a64_vir_compare_chain_lower(a64_vir_context *L, uint32_t b, uint32_t i)
{
	const vir_function *f = L->f;
	uint32_t end = f->blocks[b].first + f->blocks[b].insn_count, v = i, j = i + 1;

	for (;;) {
		const vir_insn *c, *k;

		if (j + 2 >= end || L->uses[v] != 1 || L->o->slot[v] != VIR_BLOCK_NONE || L->uses[j] != 1 || L->o->slot[j] != VIR_BLOCK_NONE)
			return false;
		c = &f->insns[j], k = &f->insns[j + 1];
		if (c->op != VIR_OP_ICMP || (k->op != VIR_OP_AND && k->op != VIR_OP_OR) || k->type != VIR_TYPE_I32 || k->a != v || k->b != j)
			return false;
		v = j + 1, j += 2;
		if (f->insns[j].op == VIR_OP_ICMP)
			continue;
		if ((f->insns[j].op != VIR_OP_BRIF && f->insns[j].op != VIR_OP_SELECT) || f->insns[j].a != v || L->uses[v] != 1 ||
		    L->o->slot[v] != VIR_BLOCK_NONE)
			return false;
		L->cclo = i, L->cchi = v;
		return true;
	}
}

/* a further compare of a chain: flags = its compare when the condition so far allows it (and: holds,
 * or: fails), else flags that make the joined condition false (and) or true (or) */
VPRIV void a64_vir_compare_chained_lower(a64_vir_context *L, const vir_insn *in, bool isor)
{
	static const uint8_t sat[16] = { 4, 0, 2, 0, 8, 0, 1, 0, 2, 0, 0, 8, 0, 4, 0, 0 };	/* nzcv satisfying a condition */
	ecx *e = L->e;
	int x = a64_vir_value_is_64_bit(L->f, in->a), code = in->flags & 15, cc, rn, rm = -1;
	int64_t k = 0;
	bool imm = false;
	uint32_t w;

	if (L->opr[0] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(L->f, in->a) && L->opr[1] != VIR_REGISTER_NONE) {	/* (as a64_vir_integer_compare_lower: operands swapped) */
		rn = a64_vir_general_register(L->opr[1]), k = a64_vir_constant_integer(&L->f->insns[in->a]), imm = true;
		code = a64_vir_condition_swapped[code];
	} else {
		rn = a64_vir_operand_general_register(L, 0, R_X16);
		if (L->opr[1] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(L->f, in->b))
			k = a64_vir_constant_integer(&L->f->insns[in->b]), imm = true;
		else
			rm = a64_vir_operand_general_register(L, 1, R_X17);
	}
	if (imm && (k < -31 || k > 31)) {
		rm = rn == R_X17 ? R_X16 : R_X17;
		movi(e, 1, rm, k);
		imm = false;
	}
	cc = a64_vir_integer_condition_codes[code];
	w = (imm && k < 0 ? 0x3a400000u : 0x7a400000u) | (unsigned)x << 31 | (unsigned)(isor ? L->ccrun ^ 1 : L->ccrun) << 12 |
	    (unsigned)(rn & 31) << 5 | sat[isor ? cc : cc ^ 1];
	if (imm)
		w |= 0x800u | (unsigned)(k < 0 ? -k : k) << 16;
	else
		w |= (unsigned)(rm & 31) << 16;
	word(e, w);
	L->ccrun = cc;
}

/* the flags of a condition value (operand 0): the fused compare's, else `cmp w, #0` and NE */
VPRIV int a64_vir_condition_flags_get(a64_vir_context *L, const vir_insn *in)
{
	if (L->fcc >= 0 && L->fval == in->a) {
		int c = L->fcc;

		L->fcc = -1;
		return c;
	}
	opi(L->e, I_SUBSI, a64_vir_value_is_64_bit(L->f, in->a), ZR, a64_vir_operand_general_register(L, 0, R_X16), 0);	/* (a condition of either width) */
	return C_NE;
}

VPRIV a64_vir_condition a64_vir_branch_condition_get(a64_vir_context *L, const vir_insn *in)
{
	if (L->fcc >= 0 && L->fval == in->a) {
		a64_vir_condition c = { L->fcc, L->fzr, L->fzx, L->fbit };

		L->fcc = -1, L->fzr = -1, L->fbit = -1;
		return c;
	}
	return (a64_vir_condition){ C_NE, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_value_is_64_bit(L->f, in->a), -1 };
}

VPRIV void a64_vir_branch_conditional_emit(ecx *e, a64_vir_condition c, bool inv, uint32_t l)
{
	int cc = inv ? c.cc ^ 1 : c.cc;

	if (c.r >= 0 && c.bit >= 0)
		tbz(e, cc == C_NE, c.r, c.bit, l);
	else if (c.r >= 0)
		cbz(e, cc == C_NE, c.x, c.r, l);
	else
		bcond(e, cc, l);
}

/* register r (VIR's) is still there at instruction upto: no edit after the current
 * instruction and up to upto writes it (x16, the edits' scratch: no edit at all) */
VPRIV bool a64_vir_register_is_kept(const a64_vir_context *L, uint8_t r, uint32_t upto)
{
	for (uint32_t x = L->ei; x < L->eend && L->o->edit[x].pos <= upto; x++)
		if (L->o->edit[x].dst == r || a64_vir_general_register(r) == R_X16)
			return false;
	return true;
}

/* instruction j of block b is used once, by j + 1, and not spilled */
static bool a64_vir_value_is_used_by_next(const a64_vir_context *L, uint32_t b, uint32_t j)
{
	return L->uses[j] == 1 && L->o->slot[j] == VIR_BLOCK_NONE && j + 1 < L->f->blocks[b].first + L->f->blocks[b].insn_count &&
	       L->f->insns[j + 1].a == j;
}

/*
 * A test of bits feeding the branch: [lshr/ashr by a constant,] and with a constant[,
 * trunc][, icmp.eq/ne 0], brif, each used once by the next: tbz/tbnz on the first
 * operand's register for one bit, else tst and b.ne/b.eq (as LLVM).  Lowered at the
 * chain's first instruction i; the others up to the brif leave nothing.
 */
VPRIV bool a64_vir_test_branch_fold(a64_vir_context *L, uint32_t b, uint32_t i)
{
	const vir_function *f = L->f;
	const vir_insn *in = &f->insns[i];
	uint32_t j = i;
	int x = in->type != VIR_TYPE_I32, cc = C_NE, k = 0;
	uint64_t wm = x ? ~0ull : 0xffffffffull, m, tm = ~0ull;

	if (L->opr[0] == VIR_REGISTER_NONE || !a64_vir_value_is_used_by_next(L, b, i))
		return false;
	if (in->op == VIR_OP_LSHR || in->op == VIR_OP_ASHR) {	/* (a constant in a register too: the immediate) */
		if (!a64_vir_value_is_integer_constant(f, in->b) || (k = (int)a64_vir_constant_integer(&f->insns[in->b])) <= 0 || k >= (x ? 64 : 32))
			return false;
		if (!a64_vir_value_is_used_by_next(L, b, ++j))
			return false;
	}
	if (f->insns[j].op != VIR_OP_AND || !a64_vir_value_is_integer_constant(f, f->insns[j].b) || (f->insns[j].type != VIR_TYPE_I32) != (x != 0))
		return false;
	m = a64_vir_constant_logical_immediate(&f->insns[f->insns[j].b]) & wm;
	j++;
	if (f->insns[j].op == VIR_OP_TRUNC && f->insns[j].type == VIR_TYPE_I32 && a64_vir_value_is_used_by_next(L, b, j))
		tm = 0xffffffffull, j++;
	if (f->insns[j].op == VIR_OP_ICMP && (f->insns[j].flags & 15) <= 1 && a64_vir_value_is_integer_constant(f, f->insns[j].b) &&
	    !a64_vir_constant_integer(&f->insns[f->insns[j].b]) && a64_vir_value_is_used_by_next(L, b, j))
		cc = f->insns[j].flags & 15 ? C_NE : C_EQ, j++;
	if (f->insns[j].op != VIR_OP_BRIF || f->insns[j].a != j - 1)
		return false;
	m &= tm;
	if (in->op == VIR_OP_ASHR && k && (m >> ((x ? 64 : 32) - k)))
		return false;	/* (it would test copies of the sign) */
	m = (m << k) & wm;
	if (!m || !logimm_ok(m, x))
		return false;
	L->fcc = cc;
	L->fval = j - 1;
	L->fzr = -1, L->fbit = -1;
	L->tlo = i, L->thi = j;
	if (!(m & (m - 1)) && a64_vir_register_is_kept(L, L->opr[0], j)) {
		L->fzr = a64_vir_general_register(L->opr[0]);
		L->fbit = __builtin_ctzll(m);
		return true;
	}
	opi(L->e, I_ANDSI, x, ZR, a64_vir_general_register(L->opr[0]), (int64_t)m);
	return true;
}
