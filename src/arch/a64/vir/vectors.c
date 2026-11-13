/* arm64 lowering of VIR: vector instructions (NEON as encoded words). */

#include "lowering-internal.h"

#define VW3(op, m, n, d)	word(e, (op) | (unsigned)(m) << 16 | (unsigned)(n) << 5 | (unsigned)(d))

#define VW2(op, n, d)		word(e, (op) | (unsigned)(n) << 5 | (unsigned)(d))

/* instruction i if it makes or consumes a vector: true when it was lowered here.  v31 is the
 * scratch of the two compares that need one */
VPRIV bool vl_vec(vl *L, const vir_insn *in, uint8_t res)
{
	ecx *e = L->e;
	vir_function *f = L->f;
	int vd = res == VIR_REGISTER_NONE ? 31 : vl_gv(res);
	/* two 64-bit lanes: the size bit of every lane-wise instruction used here */
	uint32_t D = in->type == VIR_TYPE_F64X2 || in->type == VIR_TYPE_I64X2 || (in->op == VIR_OP_VREDUCE && f->insns[in->a].type == VIR_TYPE_I64X2) ? 0x00400000u : 0;

	switch (in->op) {
	case VIR_OP_LOAD:
		if (!vir_type_is_vector(in->type))
			return false;
		VW2(0x3dc00000u, vl_ix(L, 0, R_X16), vd);	/* ldr q, [x] */
		return true;
	case VIR_OP_STORE:
		if (!vir_type_is_vector(f->insns[in->b].type))
			return false;
		VW2(0x3d800000u, vl_ix(L, 0, R_X16), vl_fx(L, 1));	/* str q, [x] */
		return true;
	case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV: {
		static const uint32_t op[] = { 0x4e20d400u, 0x4ea0d400u, 0x6e20dc00u, 0x6e20fc00u };

		if (!vir_type_is_vector(in->type))
			return false;
		VW3(op[in->op - VIR_OP_FADD] | D, vl_fx(L, 1), vl_fx(L, 0), vd);
		return true;
	}
	case VIR_OP_FMA: {	/* fmla, fmls: into the addend's register (negated first for negc), through v31 when the result's is a factor's */
		int a, b, c, t;

		if (!vir_type_is_vector(in->type))
			return false;
		a = vl_fx(L, 0), b = vl_fx(L, 1), c = vl_fx(L, 2);
		t = vd == c ? vd : vd == a || vd == b ? 31 : vd;
		if (t == 31 && (a == 31 || b == 31))
			vl_fail(L, VIRFAIL(vector_fma_without_register));
		if (t != c)
			VW3(0x4ea01c00u, c, c, t);
		if (in->flags & VIR_INSN_FLAG_NEGC)
			VW2(0x6ea0f800u | D, t, t);
		VW3((!(in->flags & VIR_INSN_FLAG_NEGP) != !(in->flags & VIR_INSN_FLAG_NEGC) ? 0x4ea0cc00u : 0x4e20cc00u) | D, b, a, t);
		if (t != vd)
			VW3(0x4ea01c00u, t, t, vd);
		return true;
	}
	case VIR_OP_FNEG: case VIR_OP_FABS: case VIR_OP_FSQRT:
		if (!vir_type_is_vector(in->type))
			return false;
		VW2((in->op == VIR_OP_FNEG ? 0x6ea0f800u : in->op == VIR_OP_FABS ? 0x4ea0f800u : 0x6ea1f800u) | D, vl_fx(L, 0), vd);
		return true;
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_MUL: case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR: {
		static const uint32_t op[] = { [VIR_OP_ADD] = 0x4ea08400u, [VIR_OP_SUB] = 0x6ea08400u, [VIR_OP_MUL] = 0x4ea09c00u,
					       [VIR_OP_AND] = 0x4e201c00u, [VIR_OP_OR] = 0x4ea01c00u, [VIR_OP_XOR] = 0x6e201c00u };

		if (!vir_type_is_vector(in->type))
			return false;
		VW3(op[in->op] | (in->op == VIR_OP_ADD || in->op == VIR_OP_SUB ? D : 0), vl_fx(L, 1), vl_fx(L, 0), vd);
		return true;
	}
	case VIR_OP_NEG: case VIR_OP_NOT:
		if (!vir_type_is_vector(in->type))
			return false;
		VW2(in->op == VIR_OP_NEG ? 0x6ea0b800u | D : 0x6e205800u, vl_fx(L, 0), vd);
		return true;
	case VIR_OP_SELECT:
		if (!vir_type_is_vector(in->type))
			return false;
		vl_fail(L, VIRFAIL(vector_select));
	case VIR_OP_VSPLAT:
		if (L->opr[0] != VIR_REGISTER_NONE && vl_fcls(L->opr[0]))
			VW2(D ? 0x4e080400u : 0x4e040400u, vl_gv(L->opr[0]), vd);	/* dup v.4s, v.s[0] (.2d, .d[0]) */
		else	/* (a constant: its bits, an fconst's too, through x16) */
			VW2(D ? 0x4e080c00u : 0x4e040c00u, vl_ix(L, 0, R_X16), vd);	/* dup v.4s, w (.2d, x) */
		return true;
	case VIR_OP_VSERIES:
		movi(e, 1, R_X16, D ? 0 : 0x0000000100000000ll);
		word(e, f_in(1, vd, R_X16));
		movi(e, 1, R_X16, D ? 1 : 0x0000000300000002ll);
		word(e, q_in_hi(vd, R_X16));
		return true;
	case VIR_OP_VICMP: {
		/* eq ne slt sle sgt sge ult ule ugt uge: the instruction, operands swapped, inverted */
		static const struct { uint32_t op; uint8_t sw, inv; } t[10] = {
			{ 0x6ea08c00u, 0, 0 }, { 0x6ea08c00u, 0, 1 }, { 0x4ea03400u, 1, 0 }, { 0x4ea03c00u, 1, 0 },
			{ 0x4ea03400u, 0, 0 }, { 0x4ea03c00u, 0, 0 }, { 0x6ea03400u, 1, 0 }, { 0x6ea03c00u, 1, 0 },
			{ 0x6ea03400u, 0, 0 }, { 0x6ea03c00u, 0, 0 },
		};
		int a = vl_fx(L, 0), b = vl_fx(L, 1), c = in->flags & 15;

		VW3(t[c].op | D, t[c].sw ? a : b, t[c].sw ? b : a, vd);
		if (t[c].inv)
			VW2(0x6e205800u, vd, vd);
		return true;
	}
	case VIR_OP_VFCMP: {
		int a = vl_fx(L, 0), b = vl_fx(L, 1);

		switch (in->flags & 15) {
		case 0: VW3(0x4e20e400u | D, b, a, vd); break;	/* oeq */
		case 1: VW3(0x4e20e400u | D, b, a, vd); VW2(0x6e205800u, vd, vd); break;	/* une */
		case 2: VW3(0x6ea0e400u | D, a, b, vd); break;	/* olt: b > a */
		case 3: VW3(0x6e20e400u | D, a, b, vd); break;	/* ole: b >= a */
		case 4: VW3(0x6ea0e400u | D, b, a, vd); break;	/* ogt */
		case 5: VW3(0x6e20e400u | D, b, a, vd); break;	/* oge */
		default:	/* uno: not (a == a and b == b); one: a > b or b > a */
			if (a == 31 || b == 31 || vd == 31)
				vl_fail(L, VIRFAIL(vector_compare_without_scratch));
			if ((in->flags & 15) == 6) {
				VW3(0x4e20e400u | D, a, a, 31);
				VW3(0x4e20e400u | D, b, b, vd);
				VW3(0x4e201c00u, 31, vd, vd);
				VW2(0x6e205800u, vd, vd);
			} else {
				VW3(0x6ea0e400u | D, b, a, 31);
				VW3(0x6ea0e400u | D, a, b, vd);
				VW3(0x4ea01c00u, 31, vd, vd);
			}
			break;
		}
		return true;
	}
	case VIR_OP_VBITSEL: {
		int m = vl_fx(L, 0), b = vl_fx(L, 1), c = vl_fx(L, 2);

		if (vd == c && vd != m) {
			VW3(0x6ea01c00u, m, b, vd);	/* bit: b where the mask is set */
		} else if (vd == b && vd != m) {
			VW3(0x6ee01c00u, m, c, vd);	/* bif: c where it is clear */
		} else {
			if (vd != m)
				VW3(0x4ea01c00u, m, m, vd);
			VW3(0x6e601c00u, c, b, vd);	/* bsl */
		}
		return true;
	}
	case VIR_OP_VS2F:
		VW2(0x4e21d800u | D, vl_fx(L, 0), vd);	/* scvtf */
		return true;
	case VIR_OP_VINSERT: {
		uint32_t wide = in->type == VIR_TYPE_F64X2 || in->type == VIR_TYPE_I64X2;
		uint32_t imm5 = (wide ? 8u : 4u) | (in->c << (wide ? 4 : 3));
		int a = vl_fx(L, 0);
		bool fp = L->opr[1] != VIR_REGISTER_NONE && vl_fcls(L->opr[1]);
		int b = fp ? vl_fx(L, 1) : vl_ix(L, 1, R_X16);
		if (fp && b == vd && a != vd) {
			VW3(0x4ea01c00u, b, b, 31);
			b = 31;
		}
		if (a != vd) VW3(0x4ea01c00u, a, a, vd);
		VW2((fp ? 0x6e000400u : 0x4e001c00u) | imm5 << 16, b, vd);
		return true;
	}
	case VIR_OP_VEXTRACT: {
		uint32_t wide = in->type == VIR_TYPE_F64 || in->type == VIR_TYPE_I64;
		uint32_t imm5 = (wide ? 8u : 4u) | (in->b << (wide ? 4 : 3));
		if (in->type == VIR_TYPE_F32 || in->type == VIR_TYPE_F64)
			VW2(0x5e000400u | imm5 << 16, vl_fx(L, 0), vd);
		else
			VW2((wide ? 0x4e003c00u : 0x0e003c00u) | imm5 << 16, vl_fx(L, 0), res == VIR_REGISTER_NONE ? R_X16 : vl_gx(res));
		return true;
	}
	case VIR_OP_VREV:
		if (D) {	/* (two lanes: the halves swapped) */
			int a = vl_fx(L, 0);

			word(e, 0x6e004000u | (unsigned)a << 16 | (unsigned)a << 5 | (unsigned)vd);
			return true;
		}
		VW2(0x4ea00800u, vl_fx(L, 0), vd);	/* rev64 .4s, then the halves swapped: ext #8 */
		word(e, 0x6e004000u | (unsigned)vd << 16 | (unsigned)vd << 5 | (unsigned)vd);
		return true;
	case VIR_OP_VREDUCE:
		VW2(D ? 0x5ef1b800u : 0x4eb1b800u, vl_fx(L, 0), 31);	/* addv s31, v.4s (addp d31, v.2d) */
		word(e, f_out(D != 0, res == VIR_REGISTER_NONE ? R_X16 : vl_gx(res), 31));
		return true;
	default:
		return false;
	}
}

#undef VW3

#undef VW2
