/* avx.c: x86-64 assembler, family x64_i_avx: VEX-encoded instructions (AVX, AVX2) and a few BMI2 ones
 * (rorx ...), as a table of mnemonic, prefix, opcode map and operand form, encoded by x64_emit
 * through the vex fields of XForm.
 *
 * Operand order is AT&T (sources first); the vector length comes from the destination's width.
 * Returns X64_NEXT for a mnemonic it does not know (asm.c tries it after the SSE family and before
 * SSE4); a known mnemonic with operands of the wrong shape fails the assembly through
 * as_error_report.  The bytes must equal LLVM MC's: bench/vas-enc.py checks them. */
#include "x64as.h"

VPRIV int x64_i_avx(as_context *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- AVX/AVX2 (VEX): [imm,] src2/mem, src1, dst; L from the destination's width ---- */
	if (mn[0] == 'v') {
		/* form 0: rm, vvvv, reg (three operands); 1: rm, reg (vvvv unused); 2: shift by imm: imm, rm, vvvv=dst
		   (/d); 3: store: reg, rm (mov to memory); w 2: WIG (W0); imm: an imm8 first */
		static const struct { const char *n; uint8_t pp, map, op, form, w, imm, d; } vx[] = {
			{ "vpaddd", 1, 1, 0xfe, 0, 0, 0, 0 }, { "vpaddq", 1, 1, 0xd4, 0, 0, 0, 0 }, { "vpaddb", 1, 1, 0xfc, 0, 0, 0, 0 }, { "vpaddw", 1, 1, 0xfd, 0, 0, 0, 0 },
			{ "vpsubd", 1, 1, 0xfa, 0, 0, 0, 0 }, { "vpsubq", 1, 1, 0xfb, 0, 0, 0, 0 }, { "vpsubb", 1, 1, 0xf8, 0, 0, 0, 0 }, { "vpsubw", 1, 1, 0xf9, 0, 0, 0, 0 },
			{ "vpxor", 1, 1, 0xef, 0, 0, 0, 0 }, { "vpor", 1, 1, 0xeb, 0, 0, 0, 0 }, { "vpand", 1, 1, 0xdb, 0, 0, 0, 0 }, { "vpandn", 1, 1, 0xdf, 0, 0, 0, 0 },
			{ "vxorps", 0, 1, 0x57, 0, 0, 0, 0 }, { "vxorpd", 1, 1, 0x57, 0, 0, 0, 0 }, { "vandps", 0, 1, 0x54, 0, 0, 0, 0 }, { "vorps", 0, 1, 0x56, 0, 0, 0, 0 },
			{ "vpmuludq", 1, 1, 0xf4, 0, 0, 0, 0 }, { "vpmulld", 1, 2, 0x40, 0, 0, 0, 0 }, { "vpshufb", 1, 2, 0x00, 0, 0, 0, 0 },
			{ "vpunpcklqdq", 1, 1, 0x6c, 0, 0, 0, 0 }, { "vpunpckhqdq", 1, 1, 0x6d, 0, 0, 0, 0 }, { "vpunpckldq", 1, 1, 0x62, 0, 0, 0, 0 },
			{ "vpunpckhdq", 1, 1, 0x6a, 0, 0, 0, 0 }, { "vpunpcklbw", 1, 1, 0x60, 0, 0, 0, 0 }, { "vpunpckhbw", 1, 1, 0x68, 0, 0, 0, 0 },
			{ "vpcmpeqd", 1, 1, 0x76, 0, 0, 0, 0 }, { "vpcmpeqb", 1, 1, 0x74, 0, 0, 0, 0 }, { "vpcmpgtd", 1, 1, 0x66, 0, 0, 0, 0 },
			{ "vpsrlvq", 1, 2, 0x45, 0, 1, 0, 0 }, { "vpsllvq", 1, 2, 0x47, 0, 1, 0, 0 }, { "vpsrlvd", 1, 2, 0x45, 0, 0, 0, 0 }, { "vpsllvd", 1, 2, 0x47, 0, 0, 0, 0 },
			{ "vpsrld", 1, 1, 0xd2, 0, 0, 0, 0 }, { "vpslld", 1, 1, 0xf2, 0, 0, 0, 0 }, { "vpsrlq", 1, 1, 0xd3, 0, 0, 0, 0 }, { "vpsllq", 1, 1, 0xf3, 0, 0, 0, 0 }, { "vpsrad", 1, 1, 0xe2, 0, 0, 0, 0 },
			{ "vaesenc", 1, 2, 0xdc, 0, 0, 0, 0 }, { "vaesenclast", 1, 2, 0xdd, 0, 0, 0, 0 }, { "vaesdec", 1, 2, 0xde, 0, 0, 0, 0 }, { "vaesdeclast", 1, 2, 0xdf, 0, 0, 0, 0 },
			{ "vaesimc", 1, 2, 0xdb, 1, 0, 0, 0 },
			{ "vpclmulqdq", 1, 3, 0x44, 0, 0, 1, 0 }, { "vpalignr", 1, 3, 0x0f, 0, 0, 1, 0 }, { "vpblendd", 1, 3, 0x02, 0, 0, 1, 0 },
			{ "vperm2i128", 1, 3, 0x46, 0, 0, 1, 0 }, { "vperm2f128", 1, 3, 0x06, 0, 0, 1, 0 }, { "vinserti128", 1, 3, 0x38, 0, 0, 1, 0 },
			{ "vinsertf128", 1, 3, 0x18, 0, 0, 1, 0 }, { "vshufps", 0, 1, 0xc6, 0, 0, 1, 0 }, { "vpblendw", 1, 3, 0x0e, 0, 0, 1, 0 },
			{ "vpshufd", 1, 1, 0x70, 1, 0, 1, 0 }, { "vpshufhw", 2, 1, 0x70, 1, 0, 1, 0 }, { "vpshuflw", 3, 1, 0x70, 1, 0, 1, 0 },
			{ "vpermq", 1, 3, 0x00, 1, 1, 1, 0 }, { "vpermpd", 1, 3, 0x01, 1, 1, 1, 0 }, { "vaeskeygenassist", 1, 3, 0xdf, 1, 0, 1, 0 },
			{ "vpbroadcastd", 1, 2, 0x58, 1, 0, 0, 0 }, { "vpbroadcastq", 1, 2, 0x59, 1, 0, 0, 0 }, { "vpbroadcastb", 1, 2, 0x78, 1, 0, 0, 0 },
			{ "vpbroadcastw", 1, 2, 0x79, 1, 0, 0, 0 }, { "vbroadcasti128", 1, 2, 0x5a, 1, 0, 0, 0 }, { "vbroadcastss", 1, 2, 0x18, 1, 0, 0, 0 },
			{ "vpmovzxbd", 1, 2, 0x31, 1, 0, 0, 0 }, { "vpmovzxbq", 1, 2, 0x32, 1, 0, 0, 0 }, { "vpmovzxdq", 1, 2, 0x35, 1, 0, 0, 0 }, { "vpmovzxwd", 1, 2, 0x33, 1, 0, 0, 0 },
			{ "vpminub", 1, 1, 0xda, 0, 0, 0, 0 },
			{ "vptest", 1, 2, 0x17, 1, 0, 0, 0 },
			{ "vpsrld", 1, 1, 0x72, 2, 0, 1, 2 }, { "vpslld", 1, 1, 0x72, 2, 0, 1, 6 }, { "vpsrad", 1, 1, 0x72, 2, 0, 1, 4 },
			{ "vpsrlq", 1, 1, 0x73, 2, 0, 1, 2 }, { "vpsllq", 1, 1, 0x73, 2, 0, 1, 6 }, { "vpsrldq", 1, 1, 0x73, 2, 0, 1, 3 },
			{ "vpslldq", 1, 1, 0x73, 2, 0, 1, 7 }, { "vpsrlw", 1, 1, 0x71, 2, 0, 1, 2 }, { "vpsllw", 1, 1, 0x71, 2, 0, 1, 6 },
		};
		XOp *dst = nops ? &o[nops - 1] : NULL;

		for (size_t i = 0; dst && i < sizeof vx / sizeof *vx; i++) {
			int need = (vx[i].form == 0 ? 3 : 2) + vx[i].imm;

			if (strcmp(mn, vx[i].n) || nops != need)
				continue;
			if (vx[i].imm && o[0].kind != XK_IMM)
				continue;
			{
				XOp *rm = &o[vx[i].imm], *src1 = vx[i].form == 0 ? &o[vx[i].imm + 1] : NULL;
				uint8_t ops[3] = { 0x0f, (uint8_t)(vx[i].map == 2 ? 0x38 : 0x3a), vx[i].op };
				uint8_t ops1[2] = { 0x0f, vx[i].op };
				int64_t c = 0;
				XForm f = F(.w = vx[i].w == 1, .opsz16 = vx[i].pp == 1, .rep = vx[i].pp == 2 ? 0xf3 : vx[i].pp == 3 ? 0xf2 : 0,
					    .op = vx[i].map == 1 ? ops1 : ops, .nop = vx[i].map == 1 ? 2 : 3, .isz = vx[i].imm, .vex = 1);

				if (rm->kind == XK_IMM || (vx[i].form == 2 && rm->kind != XK_REG))
					continue;
				if (rm->kind == XK_EXPR)
					rm->kind = XK_MEM;
				if (dst->kind != XK_REG || dst->cls != XR_XMM || (src1 && (src1->kind != XK_REG || src1->cls != XR_XMM)))
					continue;
				/* (vinserti128/vperm2i128/broadcasts: L from the destination) */
				f.vl = dst->size == 32;
				if (vx[i].form == 2) {	/* imm, src, dst: dst in vvvv, /d */
					f.reg = vx[i].d, f.rm = rm, f.vvvv = dst->reg;
				} else {
					f.reg = dst->reg, f.rm = rm, f.vvvv = src1 ? src1->reg : 0;
					if (!src1)
						f.vvvv = 0;
				}
				if (vx[i].form == 1)
					f.vvvv = 0;	/* (1111 after inversion) */
				if (vx[i].imm) {
					x64_imm(a, &o[0], &f, &c);
					f.imm = c;
				}
				if (vx[i].form != 0 && vx[i].form != 2)
					f.vvvv = 0;
				return x64_emit(a, &f);
			}
		}
		if (!strcmp(mn, "vpmovmskb") && nops == 2 && o[0].kind == XK_REG &&
		    o[0].cls == XR_XMM && dst->kind == XK_REG && dst->cls == XR_GPR && dst->size == 4)
			return x64_emit(a, &F(.op=OPB(0x0f,0xd7), .nop=2, .opsz16=1, .vex=1,
					      .vl=o[0].size == 32, .reg=dst->reg, .rm=o));
		/* moves: vmovdqa/vmovdqu/vmovaps/vmovups (load 6f/28/10, store 7f/29/11), vmovd/vmovq */
		if (nops == 2 && (!strcmp(mn, "vmovdqa") || !strcmp(mn, "vmovdqu") || !strcmp(mn, "vmovaps") || !strcmp(mn, "vmovups") ||
				  !strcmp(mn, "vmovapd") || !strcmp(mn, "vmovupd"))) {
			bool dq = mn[5] == 'q', al = dq ? mn[6] == 'a' : mn[4] == 'a';
			int pp = dq ? (al ? 1 : 2) : mn[6] == 'd' ? 1 : 0;
			bool store = o[1].kind != XK_REG;
			XOp *r = store ? &o[0] : &o[1], *m = store ? &o[1] : &o[0];
			uint8_t ops[2] = { 0x0f, (uint8_t)(dq ? (store ? 0x7f : 0x6f) : al ? (store ? 0x29 : 0x28) : (store ? 0x11 : 0x10)) };

			if (r->kind != XK_REG || r->cls != XR_XMM)
				return as_error_report(a, "invalid operand for instruction");
			if (m->kind == XK_EXPR)
				m->kind = XK_MEM;
			/* MC: a register move whose source needs VEX.B takes the store form (2-byte VEX) */
			if (!store && m->kind == XK_REG && (m->reg & 8) && !(r->reg & 8)) {
				ops[1] = (uint8_t)(dq ? 0x7f : al ? 0x29 : 0x11);
				return x64_emit(a, &F(.opsz16 = pp == 1, .rep = pp == 2 ? 0xf3 : 0, .op = ops, .nop = 2, .reg = m->reg, .rm = r,
						      .vex = 1, .vl = r->size == 32));
			}
			return x64_emit(a, &F(.opsz16 = pp == 1, .rep = pp == 2 ? 0xf3 : 0, .op = ops, .nop = 2, .reg = r->reg, .rm = m,
					      .vex = 1, .vl = r->size == 32));
		}
		if (nops == 2 && (!strcmp(mn, "vmovd") || !strcmp(mn, "vmovq"))) {
			bool q = mn[4] == 'q';
			bool tox = o[1].kind == XK_REG && o[1].cls == XR_XMM;
			XOp *x = tox ? &o[1] : &o[0], *g = tox ? &o[0] : &o[1];
			uint8_t ops[2] = { 0x0f, (uint8_t)(tox ? 0x6e : 0x7e) };

			if (g->kind == XK_EXPR)
				g->kind = XK_MEM;
			if (q && g->kind != XK_REG && tox) {	/* vmovq m64, xmm: f3 0f 7e */
				uint8_t o2[2] = { 0x0f, 0x7e };
				return x64_emit(a, &F(.rep = 0xf3, .op = o2, .nop = 2, .reg = x->reg, .rm = g, .vex = 1));
			}
			if (q && g->kind != XK_REG && !tox) {	/* vmovq xmm, m64: 66 0f d6 */
				uint8_t o2[2] = { 0x0f, 0xd6 };
				return x64_emit(a, &F(.opsz16 = 1, .op = o2, .nop = 2, .reg = x->reg, .rm = g, .vex = 1));
			}
			if (q && g->kind == XK_REG && g->cls == XR_XMM) {	/* vmovq xmm, xmm: f3 0f 7e */
				uint8_t o2[2] = { 0x0f, 0x7e };
				return x64_emit(a, &F(.rep = 0xf3, .op = o2, .nop = 2, .reg = o[1].reg, .rm = &o[0], .vex = 1));
			}
			return x64_emit(a, &F(.w = q, .opsz16 = 1, .op = ops, .nop = 2, .reg = x->reg, .rm = g, .vex = 1));
		}
		/* vextracti128/vextractf128 $i, ymm, xmm/m: 66 0f3a 39/19 (reg = the ymm) */
		if (nops == 3 && o[0].kind == XK_IMM && (!strcmp(mn, "vextracti128") || !strcmp(mn, "vextractf128"))) {
			uint8_t ops[3] = { 0x0f, 0x3a, (uint8_t)(mn[8] == 'i' ? 0x39 : 0x19) };
			int64_t c = 0;
			XForm f = F(.opsz16 = 1, .op = ops, .nop = 3, .reg = o[1].reg, .rm = &o[2], .isz = 1, .vex = 1, .vl = 1);

			if (o[2].kind == XK_EXPR)
				o[2].kind = XK_MEM;
			x64_imm(a, &o[0], &f, &c);
			f.imm = c;
			return x64_emit(a, &f);
		}
		/* vpinsrq/vpinsrd $i, r/m, xmm, xmm; vpextrq/d/b $i, xmm, r/m */
		if (nops >= 3 && o[0].kind == XK_IMM && (!strncmp(mn, "vpinsr", 6) || !strncmp(mn, "vpextr", 6)) && mn[6] && !mn[7]) {
			bool ins = mn[2] == 'i';
			char k = mn[6];
			uint8_t ops[3] = { 0x0f, 0x3a, (uint8_t)(ins ? (k == 'b' ? 0x20 : 0x22) : (k == 'b' ? 0x14 : 0x16)) };
			int64_t c = 0;
			XForm f = F(.w = k == 'q', .opsz16 = 1, .op = ops, .nop = 3, .isz = 1, .vex = 1);

			if (ins && nops == 4) f.reg = o[3].reg, f.rm = &o[1], f.vvvv = o[2].reg;
			else if (!ins && nops == 3) f.reg = o[1].reg, f.rm = &o[2];
			else return as_error_report(a, "invalid operand for instruction");
			if (f.rm->kind == XK_EXPR)
				((XOp *)f.rm)->kind = XK_MEM;
			x64_imm(a, &o[0], &f, &c);
			f.imm = c;
			return x64_emit(a, &f);
		}
		/* vpblendvb ymm4, ymm3/m, ymm2, ymm1: 66 0f3a 4c /r is4 */
		if (nops == 4 && !strcmp(mn, "vpblendvb")) {
			uint8_t ops[3] = { 0x0f, 0x3a, 0x4c };
			XForm f = F(.opsz16 = 1, .op = ops, .nop = 3, .reg = o[3].reg, .rm = &o[1], .vvvv = o[2].reg, .isz = 1,
				    .vex = 1, .vl = o[3].size == 32);

			if (o[1].kind == XK_EXPR)
				o[1].kind = XK_MEM;
			f.imm = (int64_t)(o[0].reg << 4);
			return x64_emit(a, &f);
		}
	}
	/* rorx $imm, r/m, r (BMI2: VEX f2 0f3a f0) */
	if (nops == 3 && !strncmp(mn, "rorx", 4) && o[0].kind == XK_IMM && o[2].kind == XK_REG && o[2].cls == XR_GPR) {
		uint8_t ops[3] = { 0x0f, 0x3a, 0xf0 };
		int64_t c = 0;
		XForm f = F(.w = o[2].size == 8, .rep = 0xf2, .op = ops, .nop = 3, .reg = o[2].reg, .rm = &o[1], .isz = 1, .vex = 1);

		if (o[1].kind == XK_EXPR)
			o[1].kind = XK_MEM;
		x64_imm(a, &o[0], &f, &c);
		f.imm = c;
		return x64_emit(a, &f);
	}
	return X64_NEXT;
}

/* Full-width EVEX byte operations and VEX mask operations used by the lexer. */
VPRIV int x64_i_avx512(as_context *a, char *mn, XOp *o, int n)
{
	enum { LOAD, BINARY, TEST, TO_MASK, SHIFT, COMPARE, K_STORE, K_UNARY, K_BINARY };
	static const struct {
		const char *name;
		uint8_t form, map, opcode, prefix, w, tuple;
	} ops[] = {
		{"vmovdqu64", LOAD, 1, 0x6f, 2, 1, 64}, {"vmovdqa64", LOAD, 1, 0x6f, 1, 1, 64},
		{"vbroadcasti32x4", LOAD, 2, 0x5a, 1, 0, 16}, {"vpbroadcastb", LOAD, 2, 0x78, 1, 0, 1},
		{"vpermt2b", BINARY, 2, 0x7d, 1, 0, 64}, {"vpermb", BINARY, 2, 0x8d, 1, 0, 64},
		{"vpandq", BINARY, 1, 0xdb, 1, 1, 64}, {"vporq", BINARY, 1, 0xeb, 1, 1, 64},
		{"vpshufb", BINARY, 2, 0x00, 1, 0, 64}, {"vpsrlw", SHIFT, 1, 0x71, 1, 0, 64},
		{"vptestmb", TEST, 2, 0x26, 1, 0, 64}, {"vpcmpeqb", TEST, 1, 0x74, 1, 0, 64},
		{"vpcmpub", COMPARE, 3, 0x3e, 1, 0, 64}, {"vpmovb2m", TO_MASK, 2, 0x29, 2, 0, 64},
		{"kmovq", K_STORE, 1, 0x91, 0, 1, 0}, {"knotq", K_UNARY, 1, 0x44, 0, 1, 0},
		{"korq", K_BINARY, 1, 0x45, 0, 1, 0}, {"kandq", K_BINARY, 1, 0x41, 0, 1, 0},
	};
	for (size_t i = 0; i < sizeof ops / sizeof *ops; i++) {
		if (strcmp(mn, ops[i].name)) continue;
		int form = ops[i].form;
		bool imm = form == SHIFT || form == COMPARE;
		bool binary = form == BINARY || form == TEST || form == COMPARE || form == K_BINARY;
		if (n != 2 + binary + imm) break;
		XOp *dst = &o[n-1], *rm = &o[imm];
		uint8_t bytes[] = {0x0f, ops[i].map == 2 ? 0x38 : 0x3a, ops[i].opcode};
		XForm f = F(.op=bytes, .nop=3, .vex=1, .tuple=ops[i].tuple, .w=ops[i].w,
			    .opsz16=ops[i].prefix == 1, .rep=ops[i].prefix == 2 ? 0xf3 : 0);
		if (ops[i].map == 1) bytes[1] = bytes[2], f.nop = 2;
		if (form == K_STORE) {
			if (rm->kind != XK_REG || rm->cls != XR_MASK ||
			    (dst->kind != XK_MEM && dst->kind != XK_EXPR)) break;
			f.reg = rm->reg;
			f.rm = dst;
			return x64_emit(a, &f);
		}
		int cls = form >= K_UNARY ? XR_MASK : XR_ZMM;
		int dstcls = form == TEST || form == COMPARE || form == TO_MASK ? XR_MASK : cls;
		if (dst->kind != XK_REG || dst->cls != dstcls || (dst->zero && dstcls == XR_MASK)) break;
		if (rm->kind == XK_REG) {
			if (rm->cls != cls || rm->mask || rm->zero) break;
			if (form == LOAD && (ops[i].tuple == 16 || ops[i].tuple == 1)) break;
		} else if (cls == XR_MASK || form == TO_MASK || rm->kind == XK_IMM) break;
		if (binary) {
			XOp *src = &o[imm+1];
			if (src->kind != XK_REG || src->cls != cls || src->mask) break;
			f.vvvv = src->reg;
		}
		if (imm) {
			int64_t value;
			if (o->kind != XK_IMM || !isconst(a, &o->v, &value)) break;
			f.isz = 1;
			f.imm = value;
		}
		if (form >= K_UNARY && dst->mask) break;
		f.mask = dst->mask;
		f.zero = dst->zero;
		f.reg = dst->reg;
		f.rm = rm;
		f.vl = form == K_BINARY;
		if (form == SHIFT) f.reg = 2, f.vvvv = dst->reg;
		return x64_emit(a, &f);
	}
	return as_error_report(a, "unsupported operands for '%s'", mn);
}
