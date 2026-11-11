/* x86-64 assembler: AVX/AVX2 (VEX) and BMI2 */
#include "x64as.h"

VPRIV int x64_i_avx(As *a, char *mn, XOp *o, int nops)
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
				/* MC: a commutative operation takes the 2-byte VEX when swapping its sources frees VEX.B */
				if (vx[i].form == 0 && !vx[i].imm && vx[i].map == 1 && !vx[i].w && rm->kind == XK_REG && (rm->reg & 8) &&
				    !(f.vvvv & 8) &&
				    (!strcmp(mn, "vpxor") || !strcmp(mn, "vpor") || !strcmp(mn, "vpand") || !strcmp(mn, "vpaddd") ||
				     !strcmp(mn, "vpaddq") || !strcmp(mn, "vpaddb") || !strcmp(mn, "vpaddw") || !strcmp(mn, "vpmuludq") ||
				     !strcmp(mn, "vpcmpeqd") || !strcmp(mn, "vpcmpeqb") || !strcmp(mn, "vxorps") || !strcmp(mn, "vxorpd") ||
				     !strcmp(mn, "vandps") || !strcmp(mn, "vorps"))) {
					int t = f.vvvv;

					f.vvvv = rm->reg;
					((XOp *)rm)->reg = (uint8_t)t;
				}
				if (vx[i].imm) {
					x64_imm(a, &o[0], &f, &c);
					f.imm = c;
				}
				if (vx[i].form != 0 && vx[i].form != 2)
					f.vvvv = 0;
				return x64_emit(a, &f);
			}
		}
		/* moves: vmovdqa/vmovdqu/vmovaps/vmovups (load 6f/28/10, store 7f/29/11), vmovd/vmovq */
		if (nops == 2 && (!strcmp(mn, "vmovdqa") || !strcmp(mn, "vmovdqu") || !strcmp(mn, "vmovaps") || !strcmp(mn, "vmovups") ||
				  !strcmp(mn, "vmovapd") || !strcmp(mn, "vmovupd"))) {
			bool dq = mn[5] == 'q', al = dq ? mn[6] == 'a' : mn[4] == 'a';
			int pp = dq ? (al ? 1 : 2) : mn[6] == 'd' ? 1 : 0;
			bool store = o[1].kind != XK_REG;
			XOp *r = store ? &o[0] : &o[1], *m = store ? &o[1] : &o[0];
			uint8_t ops[2] = { 0x0f, (uint8_t)(dq ? (store ? 0x7f : 0x6f) : al ? (store ? 0x29 : 0x28) : (store ? 0x11 : 0x10)) };

			if (r->kind != XK_REG || r->cls != XR_XMM)
				return err(a, "invalid operand for instruction");
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
			else return err(a, "invalid operand for instruction");
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
	/* BMI1/BMI2 on GPRs (VEX 0f38; W from the destination, an l/q suffix allowed) */
	{
		static const struct { const char *n; uint8_t pp, op, form, d; } bm[] = {
			/* form 0: src2/m, src1 (vvvv), dst; 1: ctl (vvvv), r/m, dst; 2: r/m, dst (vvvv), /d */
			{ "andn", 0, 0xf2, 0, 0 }, { "pdep", 3, 0xf5, 0, 0 }, { "pext", 2, 0xf5, 0, 0 }, { "mulx", 3, 0xf6, 0, 0 },
			{ "bextr", 0, 0xf7, 1, 0 }, { "bzhi", 0, 0xf5, 1, 0 }, { "sarx", 2, 0xf7, 1, 0 }, { "shlx", 1, 0xf7, 1, 0 },
			{ "shrx", 3, 0xf7, 1, 0 },
			{ "blsr", 0, 0xf3, 2, 1 }, { "blsmsk", 0, 0xf3, 2, 2 }, { "blsi", 0, 0xf3, 2, 3 },
		};
		char t[16];
		size_t n = strlen(mn);

		if (n < sizeof t) {
			memcpy(t, mn, n + 1);
			for (int pass = 0; pass < 2; pass++) {
				for (size_t i = 0; i < sizeof bm / sizeof *bm; i++) {
					XOp *d = &o[nops - 1];
					uint8_t ops[3] = { 0x0f, 0x38, bm[i].op };

					if (strcmp(t, bm[i].n) || nops != (bm[i].form == 2 ? 2 : 3) || d->kind != XK_REG || d->cls != XR_GPR)
						continue;
					for (int k = 0; k < nops - 1; k++)
						if (o[k].kind == XK_EXPR)
							o[k].kind = XK_MEM;
					XForm f = F(.w = d->size == 8, .opsz16 = 0, .rep = bm[i].pp == 2 ? 0xf3 : bm[i].pp == 3 ? 0xf2 : 0,
						    .op = ops, .nop = 3, .vex = 1);
					if (bm[i].pp == 1)
						f.opsz16 = 1;
					if (bm[i].form == 0)
						f.reg = d->reg, f.rm = &o[0], f.vvvv = o[1].reg;
					else if (bm[i].form == 1)
						f.reg = d->reg, f.rm = &o[1], f.vvvv = o[0].reg;
					else
						f.reg = bm[i].d, f.rm = &o[0], f.vvvv = d->reg;
					return x64_emit(a, &f);
				}
				if (n > 1 && (t[n - 1] == 'l' || t[n - 1] == 'q'))
					t[n - 1] = 0;
				else
					break;
			}
		}
	}
	/* kmovb/w/d/q between a GPR and a mask register: VEX 0f 92 (to k) / 93 (from k) */
	if (nops == 2 && !strncmp(mn, "kmov", 4) && mn[4] && !mn[5] && strchr("bwdq", mn[4])) {
		int to = o[1].kind == XK_REG && o[1].cls == XR_K;
		XOp *k = to ? &o[1] : &o[0], *g = to ? &o[0] : &o[1];
		char s = mn[4];
		uint8_t ops[2] = { 0x0f, (uint8_t)(to ? 0x92 : 0x93) };

		if (k->kind != XK_REG || k->cls != XR_K || g->kind != XK_REG || g->cls != XR_GPR)
			return err(a, "invalid operand for instruction");
		return x64_emit(a, &F(.w = s == 'q', .opsz16 = s == 'b', .rep = s == 'q' || s == 'd' ? 0xf2 : 0, .op = ops, .nop = 2,
				      .reg = to ? k->reg : g->reg, .rm = to ? g : k, .vex = 1));
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

/* ---- AVX-512 (EVEX): the instructions the kernel's crypto uses, and the
   AVX ones whenever an operand needs EVEX (zmm, xmm16-31, a mask) ---- */
VPRIV int x64_i_evex(As *a, char *mn, XOp *o, int nops)
{
	/* form 0: [imm,] rm, vvvv, reg; 1: [imm,] rm, reg; 2: imm, rm, vvvv = dst (/d); 3: [imm,] reg, rm (store)
	   w: 0 W0 (and WIG), 1 W1; n: the disp8 scale, 0 the vector length; only: no VEX form */
	static const struct { const char *n; uint8_t pp, map, op, form, w, imm, d, dn, only; } ev[] = {
		{ "vpxord", 1, 1, 0xef, 0, 0, 0, 0, 0, 1 }, { "vpxorq", 1, 1, 0xef, 0, 1, 0, 0, 0, 1 },
		{ "vpord", 1, 1, 0xeb, 0, 0, 0, 0, 0, 1 }, { "vporq", 1, 1, 0xeb, 0, 1, 0, 0, 0, 1 },
		{ "vpandd", 1, 1, 0xdb, 0, 0, 0, 0, 0, 1 }, { "vpandq", 1, 1, 0xdb, 0, 1, 0, 0, 0, 1 },
		{ "vpandnd", 1, 1, 0xdf, 0, 0, 0, 0, 0, 1 }, { "vpandnq", 1, 1, 0xdf, 0, 1, 0, 0, 0, 1 },
		{ "vpternlogd", 1, 3, 0x25, 0, 0, 1, 0, 0, 1 }, { "vpternlogq", 1, 3, 0x25, 0, 1, 1, 0, 0, 1 },
		{ "vprord", 1, 1, 0x72, 2, 0, 1, 0, 0, 1 }, { "vprorq", 1, 1, 0x72, 2, 1, 1, 0, 0, 1 },
		{ "vprold", 1, 1, 0x72, 2, 0, 1, 1, 0, 1 }, { "vprolq", 1, 1, 0x72, 2, 1, 1, 1, 0, 1 },
		{ "vpermi2d", 1, 2, 0x76, 0, 0, 0, 0, 0, 1 }, { "vpermi2q", 1, 2, 0x76, 0, 1, 0, 0, 0, 1 },
		{ "vpermt2d", 1, 2, 0x7e, 0, 0, 0, 0, 0, 1 }, { "vpermt2q", 1, 2, 0x7e, 0, 1, 0, 0, 0, 1 },
		{ "vbroadcasti32x4", 1, 2, 0x5a, 1, 0, 0, 0, 16, 1 }, { "vbroadcasti64x2", 1, 2, 0x5a, 1, 1, 0, 0, 16, 1 },
		{ "vbroadcasti32x8", 1, 2, 0x5b, 1, 0, 0, 0, 32, 1 }, { "vbroadcasti64x4", 1, 2, 0x5b, 1, 1, 0, 0, 32, 1 },
		{ "vextracti32x4", 1, 3, 0x39, 3, 0, 1, 0, 16, 1 }, { "vextracti64x2", 1, 3, 0x39, 3, 1, 1, 0, 16, 1 },
		{ "vextracti32x8", 1, 3, 0x3b, 3, 0, 1, 0, 32, 1 }, { "vextracti64x4", 1, 3, 0x3b, 3, 1, 1, 0, 32, 1 },
		{ "vinserti32x4", 1, 3, 0x38, 0, 0, 1, 0, 16, 1 }, { "vinserti64x2", 1, 3, 0x38, 0, 1, 1, 0, 16, 1 },
		{ "vinserti32x8", 1, 3, 0x3a, 0, 0, 1, 0, 32, 1 }, { "vinserti64x4", 1, 3, 0x3a, 0, 1, 1, 0, 32, 1 },
		/* with a VEX form too */
		{ "vaesenc", 1, 2, 0xdc, 0, 0, 0, 0, 0, 0 }, { "vaesenclast", 1, 2, 0xdd, 0, 0, 0, 0, 0, 0 },
		{ "vaesdec", 1, 2, 0xde, 0, 0, 0, 0, 0, 0 }, { "vaesdeclast", 1, 2, 0xdf, 0, 0, 0, 0, 0, 0 },
		{ "vpclmulqdq", 1, 3, 0x44, 0, 0, 1, 0, 0, 0 }, { "vpshufb", 1, 2, 0x00, 0, 0, 0, 0, 0, 0 },
		{ "vpaddd", 1, 1, 0xfe, 0, 0, 0, 0, 0, 0 }, { "vpaddq", 1, 1, 0xd4, 0, 1, 0, 0, 0, 0 },
		{ "vpsubd", 1, 1, 0xfa, 0, 0, 0, 0, 0, 0 }, { "vpsubq", 1, 1, 0xfb, 0, 1, 0, 0, 0, 0 },
		{ "vpunpcklqdq", 1, 1, 0x6c, 0, 1, 0, 0, 0, 0 }, { "vpunpckhqdq", 1, 1, 0x6d, 0, 1, 0, 0, 0, 0 },
		{ "vpalignr", 1, 3, 0x0f, 0, 0, 1, 0, 0, 0 }, { "vpshufd", 1, 1, 0x70, 1, 0, 1, 0, 0, 0 },
		{ "vpslld", 1, 1, 0x72, 2, 0, 1, 6, 0, 0 }, { "vpsrld", 1, 1, 0x72, 2, 0, 1, 2, 0, 0 },
		{ "vpsllq", 1, 1, 0x73, 2, 1, 1, 6, 0, 0 }, { "vpsrlq", 1, 1, 0x73, 2, 1, 1, 2, 0, 0 },
		{ "vpslldq", 1, 1, 0x73, 2, 0, 1, 7, 0, 0 }, { "vpsrldq", 1, 1, 0x73, 2, 0, 1, 3, 0, 0 },
		{ "vpbroadcastd", 1, 2, 0x58, 1, 0, 0, 0, 4, 0 }, { "vpbroadcastq", 1, 2, 0x59, 1, 1, 0, 0, 8, 0 },
	};
	int need = a->x64_kmask || a->x64_kz, vl = 16;

	if (mn[0] != 'v')
		return X64_NEXT;
	for (int i = 0; i < nops; i++)
		if (o[i].kind == XK_REG && o[i].cls == XR_XMM) {
			if (o[i].size == 64 || o[i].reg > 15)
				need = 1;
			if (o[i].size > vl)
				vl = o[i].size;
		}
	/* moves: vmovdqu8/16/32/64 (f2/f3), vmovdqa32/64 (66): 6f load, 7f store */
	if (nops == 2 && (!strncmp(mn, "vmovdqu", 7) || !strncmp(mn, "vmovdqa", 7)) && mn[7]) {
		const char *k = mn + 7;
		int u = mn[6] == 'u', w = !strcmp(k, "16") || !strcmp(k, "64");
		int ok = u ? (!strcmp(k, "8") || !strcmp(k, "16") || !strcmp(k, "32") || !strcmp(k, "64")) : (!strcmp(k, "32") || !strcmp(k, "64"));
		int pp = !u ? 1 : (!strcmp(k, "8") || !strcmp(k, "16")) ? 3 : 2;
		int store = o[1].kind != XK_REG;
		XOp *r = store ? &o[0] : &o[1], *m = store ? &o[1] : &o[0];
		uint8_t ops[2] = { 0x0f, (uint8_t)(store ? 0x7f : 0x6f) };

		if (!ok)
			return X64_NEXT;
		if (r->kind != XK_REG || r->cls != XR_XMM)
			return err(a, "invalid operand for instruction");
		if (m->kind == XK_EXPR)
			m->kind = XK_MEM;
		return x64_emit(a, &F(.w = w, .opsz16 = pp == 1, .rep = pp == 2 ? 0xf3 : pp == 3 ? 0xf2 : 0, .op = ops, .nop = 2,
				      .reg = r->reg, .rm = m, .vex = 1, .evex = 1, .ll = vl == 64 ? 2 : vl == 32, .dn = vl));
	}
	for (size_t i = 0; i < sizeof ev / sizeof *ev; i++) {
		int want = (ev[i].form == 0 ? 3 : 2) + ev[i].imm;

		if (strcmp(mn, ev[i].n) || nops != want || (ev[i].imm && o[0].kind != XK_IMM))
			continue;
		/* a GPR source: vpbroadcastd/q %r, %zmm (7c) */
		XOp *rm = &o[ev[i].imm], *dst = &o[nops - 1];
		int op = ev[i].op;

		if ((!strcmp(mn, "vpbroadcastd") || !strcmp(mn, "vpbroadcastq")) && rm->kind == XK_REG && rm->cls == XR_GPR)
			op = 0x7c, need = 1;
		if (!ev[i].only && !need)
			return X64_NEXT;	/* (the VEX form) */
		{
			uint8_t ops3[3] = { 0x0f, (uint8_t)(ev[i].map == 2 ? 0x38 : 0x3a), (uint8_t)op };
			uint8_t ops2[2] = { 0x0f, (uint8_t)op };
			int64_t c = 0;
			XForm f = F(.w = ev[i].w, .opsz16 = ev[i].pp == 1, .rep = ev[i].pp == 2 ? 0xf3 : ev[i].pp == 3 ? 0xf2 : 0,
				    .op = ev[i].map == 1 ? ops2 : ops3, .nop = ev[i].map == 1 ? 2 : 3, .isz = ev[i].imm, .vex = 1, .evex = 1,
				    .ll = vl == 64 ? 2 : vl == 32, .dn = ev[i].dn ? ev[i].dn : vl);

			if (rm->kind == XK_EXPR)
				rm->kind = XK_MEM;
			switch (ev[i].form) {
			case 0: f.reg = dst->reg, f.rm = rm, f.vvvv = o[ev[i].imm + 1].reg; break;
			case 1: f.reg = dst->reg, f.rm = rm; break;
			case 2: f.reg = ev[i].d, f.rm = rm, f.vvvv = dst->reg; break;
			case 3: f.reg = rm->reg, f.rm = dst; f.ll = rm->size == 64 ? 2 : rm->size == 32; break;
			}
			if (dst->kind == XK_EXPR)
				dst->kind = XK_MEM;
			if (ev[i].imm) {
				x64_imm(a, &o[0], &f, &c);
				f.imm = c;
			}
			return x64_emit(a, &f);
		}
	}
	return need ? err(a, "unsupported AVX-512 instruction") : X64_NEXT;
}
