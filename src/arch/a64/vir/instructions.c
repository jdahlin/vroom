/* arm64 lowering of VIR: one instruction. */

#include "lowering-internal.h"

VPRIV void vl_ins(vl *L, uint32_t b, uint32_t i, uint8_t res)
{
	ecx *e = L->e;
	vir_function *f = L->f;
	const vir_insn *in = &f->insns[i];
	int x = in->type == VIR_TYPE_I64 || in->type == VIR_TYPE_PTR;
	int rd = res == VIR_REGISTER_NONE || vl_fcls(res) ? R_X16 : vl_gx(res);
	int vd = res == VIR_REGISTER_NONE || !vl_fcls(res) ? 31 : vl_gv(res);
	int d = in->type == VIR_TYPE_F64;

	if (i > L->tlo && i < L->thi)
		return;	/* (vl_tfold: in the branch's test) */
	if (vl_vec(L, in, res))
		return;
	switch (in->op) {
	case VIR_OP_PARAM:
		vl_param(L, in, res);
		return;
	case VIR_OP_PTRADD:
		if (vl_pfold(L, b, i))
			return;
		__attribute__((fallthrough));
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR: {
		if (L->cchi && i > L->cclo && i <= L->cchi) {	/* a chain's and/or: the flags hold it */
			if (i == L->cchi) {
				L->fcc = L->ccrun, L->fval = i, L->fzr = -1, L->fbit = -1;
				L->cchi = 0;
			}
			return;
		}
		if ((in->op == VIR_OP_PTRADD || in->op == VIR_OP_ADD || in->op == VIR_OP_SUB) && vl_cfold(L, b, i, res))
			return;
		if (in->op == VIR_OP_AND && vl_tfold(L, b, i))
			return;
		static const uint8_t rr[] = { [VIR_OP_ADD] = I_ADD, [VIR_OP_SUB] = I_SUB, [VIR_OP_AND] = I_AND, [VIR_OP_OR] = I_ORR,
					      [VIR_OP_XOR] = I_EOR, [VIR_OP_PTRADD] = I_ADD };
		static const uint8_t ri[] = { [VIR_OP_ADD] = I_ADDI, [VIR_OP_SUB] = I_SUBI, [VIR_OP_AND] = I_ANDI, [VIR_OP_OR] = I_ORRI,
					      [VIR_OP_XOR] = I_EORI, [VIR_OP_PTRADD] = I_ADDI };
		int a = vl_ix(L, 0, R_X16);

		if (L->opr[1] == VIR_REGISTER_NONE && vl_iconst(f, in->b))
			opi(e, ri[in->op], x, rd, a, in->op == VIR_OP_AND || in->op == VIR_OP_OR || in->op == VIR_OP_XOR ?
				(int64_t)vl_lval(&f->insns[in->b]) : vl_ival(&f->insns[in->b]));
		else
			op3(e, rr[in->op], x, rd, a, vl_ix(L, 1, R_X17));
		return;
	}
	case VIR_OP_PSUB:
		op3(e, I_SUB, 1, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VIR_OP_MUL:
		if (vl_cfold(L, b, i, res))
			return;
		if (L->opr[1] == VIR_REGISTER_NONE && vl_iconst(f, in->b) && vl_pow2(&f->insns[in->b]) >= 0) {
			int w = x ? 64 : 32, k = vl_pow2(&f->insns[in->b]);

			opbf(e, I_UBFM, x, rd, vl_ix(L, 0, R_X16), (w - k) & (w - 1), w - 1 - k);
			return;
		}
		op4(e, I_MADD, x, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17), ZR);
		return;
	case VIR_OP_SDIV: case VIR_OP_UDIV:
		op3(e, in->op == VIR_OP_SDIV ? I_SDIV : I_UDIV, x, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VIR_OP_SREM: case VIR_OP_UREM: {
		int a = vl_ix(L, 0, R_X16), c = vl_ix(L, 1, R_X17);

		if (a == R_X16 || c == R_X16)
			vl_fail(L, VIRFAIL(constant_remainder));
		op3(e, in->op == VIR_OP_SREM ? I_SDIV : I_UDIV, x, R_X16, a, c);
		op4(e, I_MSUB, x, rd, R_X16, c, a);
		return;
	}
	case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR: {
		if (in->op == VIR_OP_SHL && vl_cfold(L, b, i, res))
			return;
		if (in->op != VIR_OP_SHL && vl_tfold(L, b, i))
			return;
		int a = vl_ix(L, 0, R_X16), w = x ? 64 : 32;

		if (L->opr[1] == VIR_REGISTER_NONE && vl_iconst(f, in->b)) {
			int s = (int)(vl_ival(&f->insns[in->b]) & (w - 1));

			if (in->op == VIR_OP_SHL)
				opbf(e, I_UBFM, x, rd, a, (w - s) & (w - 1), w - 1 - s);
			else
				opbf(e, in->op == VIR_OP_LSHR ? I_UBFM : I_SBFM, x, rd, a, s, w - 1);
		} else {
			op3(e, in->op == VIR_OP_SHL ? I_LSLV : in->op == VIR_OP_LSHR ? I_LSRV : I_ASRV, x, rd, a, vl_ix(L, 1, R_X17));
		}
		return;
	}
	case VIR_OP_UMULHI: case VIR_OP_SMULHI:
		op3(e, in->op == VIR_OP_UMULHI ? I_UMULH : I_SMULH, 1, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VIR_OP_NEG:
		op3(e, I_SUB, x, rd, ZR, vl_ix(L, 0, R_X16));
		return;
	case VIR_OP_NOT:
		op3(e, I_ORN, x, rd, ZR, vl_ix(L, 0, R_X16));
		return;
	case VIR_OP_CLZ:
		op3(e, I_CLZ, x, rd, vl_ix(L, 0, R_X16), 0);
		return;
	case VIR_OP_CTZ:
		op3(e, I_RBIT, x, rd, vl_ix(L, 0, R_X16), 0);
		op3(e, I_CLZ, x, rd, rd, 0);
		return;
	case VIR_OP_POPCNT:
		word(e, f_in(1, 31, vl_ix(L, 0, R_X16)));
		word(e, 0x0e205800u | 31u << 5 | 31u);	/* cnt v31.8b, v31.8b */
		word(e, 0x0e31b800u | 31u << 5 | 31u);	/* addv b31, v31.8b */
		word(e, f_out(0, rd, 31));
		return;
	case VIR_OP_BSWAP:
		op3(e, I_REV, x, rd, vl_ix(L, 0, R_X16), 0);
		return;
	case VIR_OP_EXT: {
		if (vl_cfold(L, b, i, res))
			return;
		int w = VIR_INSN_FLAG_WIDTH_LOG2(in->flags), a = vl_ix(L, 0, R_X16);

		if (w == 3 || (w == 2 && (!x || L->lo32[i])))
			mov(e, 1, rd, a);	/* (ext.4 whose low half alone is read: a copy) */
		else if ((in->flags & VIR_INSN_FLAG_MS) && L->lo32[i])	/* only its low half is read: the i32 form */
			opbf(e, I_SBFM, 0, rd, a, 0, w == 2 ? 31 : (8 << w) - 1);
		else if (in->flags & VIR_INSN_FLAG_MS)
			opbf(e, I_SBFM, x, rd, a, 0, (8 << w) - 1);
		else if (w == 2)
			op3(e, I_MOV, 0, rd, a, 0);
		else
			opbf(e, I_UBFM, 0, rd, a, 0, (8 << w) - 1);
		return;
	}
	case VIR_OP_TRUNC:	/* an i32's upper half is never read: a copy (ext.4u clears it) */
		mov(e, 1, rd, vl_ix(L, 0, R_X16));
		return;
	case VIR_OP_P2I: case VIR_OP_I2P:	/* (unused: their users read the operand) */
		return;
	case VIR_OP_ICMP:
	case VIR_OP_FCMP: {
		int cc;

		if (in->op == VIR_OP_ICMP && L->cchi && i > L->cclo && i < L->cchi) {
			vl_ccmp(L, in, f->insns[i + 1].op == VIR_OP_OR);
			return;
		}
		if (in->op == VIR_OP_ICMP && !L->cchi && vl_chain(L, b, i)) {
			L->ccrun = vl_icc[vl_cmp(L, in)];
			return;
		}
		bool fuse = L->uses[i] == 1 && L->o->slot[i] == VIR_BLOCK_NONE && i + 1 < f->blocks[b].first + f->blocks[b].insn_count &&
			    (f->insns[i + 1].op == VIR_OP_BRIF || f->insns[i + 1].op == VIR_OP_SELECT) && f->insns[i + 1].a == i;

		/* against 0 for a branch: x == 0, x != 0, x <=u 0, x >u 0 as cbz, cbnz; x < 0, x >= 0 as
		 * tbnz, tbz of the sign bit; x <= -1, x > -1 too (when no edit in between writes x) */
		if (fuse && in->op == VIR_OP_ICMP && f->insns[i + 1].op == VIR_OP_BRIF && L->opr[0] != VIR_REGISTER_NONE &&
		    L->opr[1] == VIR_REGISTER_NONE && vl_iconst(f, in->b) && vl_kept(L, L->opr[0], i + 1)) {
			int64_t k = vl_ival(&f->insns[in->b]);
			int c = in->flags & 15, cz = -1, cb = -1;

			if (!k)
				cz = c == 0 || c == 7 ? C_EQ : c == 1 || c == 8 ? C_NE : -1, cb = c == 2 ? C_NE : c == 5 ? C_EQ : -1;
			else if (k == -1)
				cb = c == 3 ? C_NE : c == 4 ? C_EQ : -1;
			if (cz >= 0 || cb >= 0) {
				L->fcc = cz >= 0 ? cz : cb;
				L->fval = i;
				L->fzr = vl_gx(L->opr[0]);
				L->fzx = vl_x(f, in->a);
				L->fbit = cb >= 0 ? (L->fzx ? 63 : 31) : -1;
				return;
			}
		}
		if (in->op == VIR_OP_ICMP) {
			cc = vl_icc[vl_cmp(L, in)];
		} else {
			vl_fcmp(L, in);
			cc = vl_fccs[in->flags & 7];
		}
		/* used once, by the next instruction as its condition: that reads the flags */
		if (cc >= 0 && fuse) {
			L->fcc = cc;
			L->fval = i;
			L->fzr = -1, L->fbit = -1;
			return;
		}
		if (res == VIR_REGISTER_NONE)
			return;
		if (cc >= 0) {
			cset(e, rd, cc);
		} else {	/* one: less or greater */
			cset(e, rd, C_MI);
			op3s(e, I_CSINC, 0, rd, rd, ZR, 0, 0);
			e->in[e->nin - 1].cond = C_LE;
		}
		return;
	}
	case VIR_OP_SELECT: {
		int cc = vl_cond(L, in);

		if (in->type == VIR_TYPE_F32 || in->type == VIR_TYPE_F64)
			word(e, 0x1e200c00u | (unsigned)d << 22 | (unsigned)vl_fx(L, 2) << 16 | (unsigned)cc << 12 |
				(unsigned)vl_fx(L, 1) << 5 | (unsigned)vd);
		else
			op3s(e, I_CSEL, x, rd, vl_ix(L, 1, R_X16), vl_ix(L, 2, R_X17), 0, 0), e->in[e->nin - 1].cond = (uint8_t)cc;
		return;
	}
	case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV: {
		static const uint32_t op[] = { 0x1e202800u, 0x1e203800u, 0x1e200800u, 0x1e201800u };

		word(e, op[in->op - VIR_OP_FADD] | (unsigned)d << 22 | (unsigned)vl_fx(L, 1) << 16 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VIR_OP_FMA:	/* fmadd, fmsub (the product negated), fnmsub (the addend), fnmadd (both) */
		word(e, 0x1f000000u | (unsigned)d << 22 | (unsigned)((in->flags & VIR_INSN_FLAG_NEGC) != 0) << 21 | (unsigned)vl_fx(L, 1) << 16 |
			(unsigned)(!(in->flags & VIR_INSN_FLAG_NEGP) != !(in->flags & VIR_INSN_FLAG_NEGC)) << 15 | (unsigned)vl_fx(L, 2) << 10 |
			(unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_FNEG: case VIR_OP_FABS: case VIR_OP_FSQRT: {
		uint32_t op = in->op == VIR_OP_FNEG ? 0x1e214000u : in->op == VIR_OP_FABS ? 0x1e20c000u : 0x1e21c000u;

		word(e, op | (unsigned)d << 22 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VIR_OP_FEXT:
		word(e, 0x1e22c000u | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_FTRUNC:
		word(e, 0x1e624000u | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_S2F: case VIR_OP_U2F:
		word(e, (in->op == VIR_OP_S2F ? 0x1e220000u : 0x1e230000u) | (unsigned)vl_x(f, in->a) << 31 | (unsigned)d << 22 |
			(unsigned)vl_ix(L, 0, R_X16) << 5 | (unsigned)vd);
		return;
	case VIR_OP_F2S: case VIR_OP_F2U:
		word(e, (in->op == VIR_OP_F2S ? 0x1e380000u : 0x1e390000u) | (unsigned)x << 31 |
			(unsigned)(f->insns[in->a].type == VIR_TYPE_F64) << 22 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)rd);
		return;
	case VIR_OP_BITCAST:
		if (in->type == VIR_TYPE_F32 || in->type == VIR_TYPE_F64)
			word(e, f_in(d, vd, vl_ix(L, 0, R_X16)));
		else
			word(e, f_out(x, rd, vl_fx(L, 0)));
		return;
	case VIR_OP_LOAD:
		vl_load(L, i, in, res);
		return;
	case VIR_OP_STORE:
		vl_store(L, in);
		return;
	case VIR_OP_MEMCPY: case VIR_OP_MEMSET:
		vl_mem(L, in);
		return;
	case VIR_OP_ALOAD:	/* ldar: sequentially consistent with stlr */
		word(e, 0x08dffc00u | (unsigned)VIR_INSN_FLAG_WIDTH_LOG2(in->flags) << 30 | (unsigned)vl_ix(L, 0, R_X16) << 5 | (unsigned)rd);
		return;
	case VIR_OP_ASTORE: {
		int p = vl_ix(L, 0, R_X16), v = vl_ix(L, 1, R_X17);

		word(e, 0x089ffc00u | (unsigned)VIR_INSN_FLAG_WIDTH_LOG2(in->flags) << 30 | (unsigned)p << 5 | (unsigned)v);
		return;
	}
	case VIR_OP_AFENCE:
		word(e, 0xd5033bbfu);	/* dmb ish */
		return;
	case VIR_OP_BARRIER:
		return;
	case VIR_OP_CALL:
		vl_call(L, b, i, in);
		return;
	case VIR_OP_VA_START: {	/* Apple's: the first unnamed argument, after the named ones on the stack */
		vlarg none;
		int32_t stk;
		lval l = vl_addr(L, in->flags, R_X16);
		int t = l.k == L_REG && l.r == R_X16 ? R_X17 : R_X16;

		if (!DARWIN(e->c))
			vl_fail(L, VIRFAIL(variadic_start_lowering));
		vl_assign(true, &f->extra[f->signature], &none, 0, &stk);
		if (l.k == L_SYM) {
			addr_of_ref(e, R_X17, l.ref, l.off);
			l = (lval){ .k = L_REG, .r = R_X17 };
		}
		opi(e, I_ADDI, 1, t, R_FP, 16 + ((stk + 7) & ~7));
		mem(e, I_ST, 3, 0, t, l.k == L_FRAME ? FRAME : l.r, l.off);
		return;
	}
	case VIR_OP_BR: {
		uint32_t g = L->o->edge_indices[L->o->slot_offsets[b]], t = vl_chase(L, in->a);

		if (g != VIR_BLOCK_NONE && L->o->edge[g].count)
			vl_emoves(L, g);
		vl_goto(L, b, t);
		return;
	}
	case VIR_OP_BRIF: {	/* the jump that remains goes to what follows: the next block or the first edge's moves */
		vlcc cc = vl_brcond(L, in);
		bool tp = vl_plain(L, b, 0), fp = vl_plain(L, b, 1);

		if (tp && fp) {
			uint32_t t = vl_chase(L, in->b), el = vl_lbl(L, vl_chase(L, in->c));

			if (t == vl_fnext(L)) {
				vl_bc(e, cc, true, el);
				jump(e, vl_lbl(L, t));
			} else {
				vl_bc(e, cc, false, vl_lbl(L, t));
				vl_goto(L, b, vl_chase(L, in->c));
			}
		} else if (fp) {
			vl_bc(e, cc, true, vl_lbl(L, vl_chase(L, in->c)));
			jump(e, vl_succ(L, b, 0, in->b));
		} else {
			uint32_t el = vl_succ(L, b, 1, in->c), tl = vl_succ(L, b, 0, in->b);

			vl_bc(e, cc, false, tl);
			jump(e, el);
		}
		return;
	}
	case VIR_OP_SWITCH:
		vl_switch(L, b, in);
		return;
	case VIR_OP_RET:
		vl_ret(L);
		return;
	case VIR_OP_TRAP:
		put(e, I_BRK)->imm = 1;
		e->reach = false;
		return;
	default: {
		snprintf(L->e->vw->detail, sizeof L->e->vw->detail, "%s", vir_opcode_table[in->op].name ? vir_opcode_table[in->op].name : "?");
		vl_fail(L, VIRFAIL(unsupported_instruction));
	}
	}
}
