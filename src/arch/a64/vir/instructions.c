/* arm64 lowering of VIR: one instruction. */

#include "lowering-internal.h"

VPRIV void vl_ins(vl *L, uint32_t b, uint32_t i, uint8_t res)
{
	ecx *e = L->e;
	vir_fn *f = L->f;
	const vir_ins *in = &f->ins[i];
	int x = in->ty == VT_I64 || in->ty == VT_PTR;
	int rd = res == VR_NONE || vl_fcls(res) ? R_X16 : vl_gx(res);
	int vd = res == VR_NONE || !vl_fcls(res) ? 31 : vl_gv(res);
	int d = in->ty == VT_F64;

	if (i > L->tlo && i < L->thi)
		return;	/* (vl_tfold: in the branch's test) */
	if (vl_vec(L, in, res))
		return;
	switch (in->op) {
	case VO_PARAM:
		vl_param(L, in, res);
		return;
	case VO_PTRADD:
		if (vl_pfold(L, b, i))
			return;
		__attribute__((fallthrough));
	case VO_ADD: case VO_SUB: case VO_AND: case VO_OR: case VO_XOR: {
		if (L->cchi && i > L->cclo && i <= L->cchi) {	/* a chain's and/or: the flags hold it */
			if (i == L->cchi) {
				L->fcc = L->ccrun, L->fval = i, L->fzr = -1, L->fbit = -1;
				L->cchi = 0;
			}
			return;
		}
		if ((in->op == VO_PTRADD || in->op == VO_ADD || in->op == VO_SUB) && vl_cfold(L, b, i, res))
			return;
		if (in->op == VO_AND && vl_tfold(L, b, i))
			return;
		static const uint8_t rr[] = { [VO_ADD] = I_ADD, [VO_SUB] = I_SUB, [VO_AND] = I_AND, [VO_OR] = I_ORR,
					      [VO_XOR] = I_EOR, [VO_PTRADD] = I_ADD };
		static const uint8_t ri[] = { [VO_ADD] = I_ADDI, [VO_SUB] = I_SUBI, [VO_AND] = I_ANDI, [VO_OR] = I_ORRI,
					      [VO_XOR] = I_EORI, [VO_PTRADD] = I_ADDI };
		int a = vl_ix(L, 0, R_X16);

		if (L->opr[1] == VR_NONE && vl_iconst(f, in->b))
			opi(e, ri[in->op], x, rd, a, in->op == VO_AND || in->op == VO_OR || in->op == VO_XOR ?
				(int64_t)vl_lval(&f->ins[in->b]) : vl_ival(&f->ins[in->b]));
		else
			op3(e, rr[in->op], x, rd, a, vl_ix(L, 1, R_X17));
		return;
	}
	case VO_PSUB:
		op3(e, I_SUB, 1, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VO_MUL:
		if (vl_cfold(L, b, i, res))
			return;
		if (L->opr[1] == VR_NONE && vl_iconst(f, in->b) && vl_pow2(&f->ins[in->b]) >= 0) {
			int w = x ? 64 : 32, k = vl_pow2(&f->ins[in->b]);

			opbf(e, I_UBFM, x, rd, vl_ix(L, 0, R_X16), (w - k) & (w - 1), w - 1 - k);
			return;
		}
		op4(e, I_MADD, x, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17), ZR);
		return;
	case VO_SDIV: case VO_UDIV:
		op3(e, in->op == VO_SDIV ? I_SDIV : I_UDIV, x, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VO_SREM: case VO_UREM: {
		int a = vl_ix(L, 0, R_X16), c = vl_ix(L, 1, R_X17);

		if (a == R_X16 || c == R_X16)
			vl_fail(L, VIRFAIL(constant_remainder));
		op3(e, in->op == VO_SREM ? I_SDIV : I_UDIV, x, R_X16, a, c);
		op4(e, I_MSUB, x, rd, R_X16, c, a);
		return;
	}
	case VO_SHL: case VO_LSHR: case VO_ASHR: {
		if (in->op == VO_SHL && vl_cfold(L, b, i, res))
			return;
		if (in->op != VO_SHL && vl_tfold(L, b, i))
			return;
		int a = vl_ix(L, 0, R_X16), w = x ? 64 : 32;

		if (L->opr[1] == VR_NONE && vl_iconst(f, in->b)) {
			int s = (int)(vl_ival(&f->ins[in->b]) & (w - 1));

			if (in->op == VO_SHL)
				opbf(e, I_UBFM, x, rd, a, (w - s) & (w - 1), w - 1 - s);
			else
				opbf(e, in->op == VO_LSHR ? I_UBFM : I_SBFM, x, rd, a, s, w - 1);
		} else {
			op3(e, in->op == VO_SHL ? I_LSLV : in->op == VO_LSHR ? I_LSRV : I_ASRV, x, rd, a, vl_ix(L, 1, R_X17));
		}
		return;
	}
	case VO_UMULHI: case VO_SMULHI:
		op3(e, in->op == VO_UMULHI ? I_UMULH : I_SMULH, 1, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VO_NEG:
		op3(e, I_SUB, x, rd, ZR, vl_ix(L, 0, R_X16));
		return;
	case VO_NOT:
		op3(e, I_ORN, x, rd, ZR, vl_ix(L, 0, R_X16));
		return;
	case VO_CLZ:
		op3(e, I_CLZ, x, rd, vl_ix(L, 0, R_X16), 0);
		return;
	case VO_CTZ:
		op3(e, I_RBIT, x, rd, vl_ix(L, 0, R_X16), 0);
		op3(e, I_CLZ, x, rd, rd, 0);
		return;
	case VO_POPCNT:
		word(e, f_in(1, 31, vl_ix(L, 0, R_X16)));
		word(e, 0x0e205800u | 31u << 5 | 31u);	/* cnt v31.8b, v31.8b */
		word(e, 0x0e31b800u | 31u << 5 | 31u);	/* addv b31, v31.8b */
		word(e, f_out(0, rd, 31));
		return;
	case VO_BSWAP:
		op3(e, I_REV, x, rd, vl_ix(L, 0, R_X16), 0);
		return;
	case VO_EXT: {
		if (vl_cfold(L, b, i, res))
			return;
		int w = VIF_W(in->fl), a = vl_ix(L, 0, R_X16);

		if (w == 3 || (w == 2 && (!x || L->lo32[i])))
			mov(e, 1, rd, a);	/* (ext.4 whose low half alone is read: a copy) */
		else if ((in->fl & VIF_MS) && L->lo32[i])	/* only its low half is read: the i32 form */
			opbf(e, I_SBFM, 0, rd, a, 0, w == 2 ? 31 : (8 << w) - 1);
		else if (in->fl & VIF_MS)
			opbf(e, I_SBFM, x, rd, a, 0, (8 << w) - 1);
		else if (w == 2)
			op3(e, I_MOV, 0, rd, a, 0);
		else
			opbf(e, I_UBFM, 0, rd, a, 0, (8 << w) - 1);
		return;
	}
	case VO_TRUNC:	/* an i32's upper half is never read: a copy (ext.4u clears it) */
		mov(e, 1, rd, vl_ix(L, 0, R_X16));
		return;
	case VO_P2I: case VO_I2P:	/* (unused: their users read the operand) */
		return;
	case VO_ICMP:
	case VO_FCMP: {
		int cc;

		if (in->op == VO_ICMP && L->cchi && i > L->cclo && i < L->cchi) {
			vl_ccmp(L, in, f->ins[i + 1].op == VO_OR);
			return;
		}
		if (in->op == VO_ICMP && !L->cchi && vl_chain(L, b, i)) {
			L->ccrun = vl_icc[vl_cmp(L, in)];
			return;
		}
		bool fuse = L->uses[i] == 1 && L->o->slot[i] == VIR_NOBLK && i + 1 < f->blk[b].first + f->blk[b].n &&
			    (f->ins[i + 1].op == VO_BRIF || f->ins[i + 1].op == VO_SELECT) && f->ins[i + 1].a == i;

		/* against 0 for a branch: x == 0, x != 0, x <=u 0, x >u 0 as cbz, cbnz; x < 0, x >= 0 as
		 * tbnz, tbz of the sign bit; x <= -1, x > -1 too (when no edit in between writes x) */
		if (fuse && in->op == VO_ICMP && f->ins[i + 1].op == VO_BRIF && L->opr[0] != VR_NONE &&
		    L->opr[1] == VR_NONE && vl_iconst(f, in->b) && vl_kept(L, L->opr[0], i + 1)) {
			int64_t k = vl_ival(&f->ins[in->b]);
			int c = in->fl & 15, cz = -1, cb = -1;

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
		if (in->op == VO_ICMP) {
			cc = vl_icc[vl_cmp(L, in)];
		} else {
			vl_fcmp(L, in);
			cc = vl_fccs[in->fl & 7];
		}
		/* used once, by the next instruction as its condition: that reads the flags */
		if (cc >= 0 && fuse) {
			L->fcc = cc;
			L->fval = i;
			L->fzr = -1, L->fbit = -1;
			return;
		}
		if (res == VR_NONE)
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
	case VO_SELECT: {
		int cc = vl_cond(L, in);

		if (in->ty == VT_F32 || in->ty == VT_F64)
			word(e, 0x1e200c00u | (unsigned)d << 22 | (unsigned)vl_fx(L, 2) << 16 | (unsigned)cc << 12 |
				(unsigned)vl_fx(L, 1) << 5 | (unsigned)vd);
		else
			op3s(e, I_CSEL, x, rd, vl_ix(L, 1, R_X16), vl_ix(L, 2, R_X17), 0, 0), e->in[e->nin - 1].cond = (uint8_t)cc;
		return;
	}
	case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV: {
		static const uint32_t op[] = { 0x1e202800u, 0x1e203800u, 0x1e200800u, 0x1e201800u };

		word(e, op[in->op - VO_FADD] | (unsigned)d << 22 | (unsigned)vl_fx(L, 1) << 16 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VO_FMA:	/* fmadd, fmsub (the product negated), fnmsub (the addend), fnmadd (both) */
		word(e, 0x1f000000u | (unsigned)d << 22 | (unsigned)((in->fl & VIF_NEGC) != 0) << 21 | (unsigned)vl_fx(L, 1) << 16 |
			(unsigned)(!(in->fl & VIF_NEGP) != !(in->fl & VIF_NEGC)) << 15 | (unsigned)vl_fx(L, 2) << 10 |
			(unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	case VO_FNEG: case VO_FABS: case VO_FSQRT: {
		uint32_t op = in->op == VO_FNEG ? 0x1e214000u : in->op == VO_FABS ? 0x1e20c000u : 0x1e21c000u;

		word(e, op | (unsigned)d << 22 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VO_FEXT:
		word(e, 0x1e22c000u | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	case VO_FTRUNC:
		word(e, 0x1e624000u | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	case VO_S2F: case VO_U2F:
		word(e, (in->op == VO_S2F ? 0x1e220000u : 0x1e230000u) | (unsigned)vl_x(f, in->a) << 31 | (unsigned)d << 22 |
			(unsigned)vl_ix(L, 0, R_X16) << 5 | (unsigned)vd);
		return;
	case VO_F2S: case VO_F2U:
		word(e, (in->op == VO_F2S ? 0x1e380000u : 0x1e390000u) | (unsigned)x << 31 |
			(unsigned)(f->ins[in->a].ty == VT_F64) << 22 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)rd);
		return;
	case VO_BITCAST:
		if (in->ty == VT_F32 || in->ty == VT_F64)
			word(e, f_in(d, vd, vl_ix(L, 0, R_X16)));
		else
			word(e, f_out(x, rd, vl_fx(L, 0)));
		return;
	case VO_LOAD:
		vl_load(L, i, in, res);
		return;
	case VO_STORE:
		vl_store(L, in);
		return;
	case VO_MEMCPY: case VO_MEMSET:
		vl_mem(L, in);
		return;
	case VO_ALOAD:	/* ldar: sequentially consistent with stlr */
		word(e, 0x08dffc00u | (unsigned)VIF_W(in->fl) << 30 | (unsigned)vl_ix(L, 0, R_X16) << 5 | (unsigned)rd);
		return;
	case VO_ASTORE: {
		int p = vl_ix(L, 0, R_X16), v = vl_ix(L, 1, R_X17);

		word(e, 0x089ffc00u | (unsigned)VIF_W(in->fl) << 30 | (unsigned)p << 5 | (unsigned)v);
		return;
	}
	case VO_AFENCE:
		word(e, 0xd5033bbfu);	/* dmb ish */
		return;
	case VO_BARRIER:
		return;
	case VO_CALL:
		vl_call(L, b, i, in);
		return;
	case VO_VA_START: {	/* Apple's: the first unnamed argument, after the named ones on the stack */
		vlarg none;
		int32_t stk;
		lval l = vl_addr(L, in->fl, R_X16);
		int t = l.k == L_REG && l.r == R_X16 ? R_X17 : R_X16;

		if (!DARWIN(e->c))
			vl_fail(L, VIRFAIL(variadic_start_lowering));
		vl_assign(true, &f->extra[f->sig], &none, 0, &stk);
		if (l.k == L_SYM) {
			addr_of_ref(e, R_X17, l.ref, l.off);
			l = (lval){ .k = L_REG, .r = R_X17 };
		}
		opi(e, I_ADDI, 1, t, R_FP, 16 + ((stk + 7) & ~7));
		mem(e, I_ST, 3, 0, t, l.k == L_FRAME ? FRAME : l.r, l.off);
		return;
	}
	case VO_BR: {
		uint32_t g = L->o->eidx[L->o->soff[b]], t = vl_chase(L, in->a);

		if (g != VIR_NOBLK && L->o->edge[g].n)
			vl_emoves(L, g);
		vl_goto(L, b, t);
		return;
	}
	case VO_BRIF: {	/* the jump that remains goes to what follows: the next block or the first edge's moves */
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
	case VO_SWITCH:
		vl_switch(L, b, in);
		return;
	case VO_RET:
		vl_ret(L);
		return;
	case VO_TRAP:
		put(e, I_BRK)->imm = 1;
		e->reach = false;
		return;
	default: {
		snprintf(L->e->vw->detail, sizeof L->e->vw->detail, "%s", vir_optab[in->op].name ? vir_optab[in->op].name : "?");
		vl_fail(L, VIRFAIL(unsupported_instruction));
	}
	}
}
