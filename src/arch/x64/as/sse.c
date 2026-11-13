/* x86-64 assembler: SSE through SSE4.2, AES-NI, PCLMUL, SHA, CRC32, FS/GS base */
#include "x64as.h"

VPRIV int x64_i_sse(As *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- SSE ---- */
	{
		static const struct { const char *n; uint8_t pfx, op; } sse[] = {
			{ "addss", 0xf3, 0x58 }, { "addsd", 0xf2, 0x58 }, { "subss", 0xf3, 0x5c }, { "subsd", 0xf2, 0x5c },
			{ "mulss", 0xf3, 0x59 }, { "mulsd", 0xf2, 0x59 }, { "divss", 0xf3, 0x5e }, { "divsd", 0xf2, 0x5e },
			{ "sqrtss", 0xf3, 0x51 }, { "sqrtsd", 0xf2, 0x51 }, { "minss", 0xf3, 0x5d }, { "minsd", 0xf2, 0x5d },
			{ "maxss", 0xf3, 0x5f }, { "maxsd", 0xf2, 0x5f }, { "xorps", 0, 0x57 }, { "xorpd", 0x66, 0x57 },
			{ "andps", 0, 0x54 }, { "andpd", 0x66, 0x54 }, { "orps", 0, 0x56 }, { "andnps", 0, 0x55 },
			{ "ucomiss", 0, 0x2e }, { "ucomisd", 0x66, 0x2e }, { "comiss", 0, 0x2f }, { "comisd", 0x66, 0x2f },
			{ "cvtss2sd", 0xf3, 0x5a }, { "cvtsd2ss", 0xf2, 0x5a }, { "unpcklps", 0, 0x14 }, { "unpcklpd", 0x66, 0x14 },
			{ "pxor", 0x66, 0xef }, { "addps", 0, 0x58 }, { "mulps", 0, 0x59 }, { "subps", 0, 0x5c }, { "divps", 0, 0x5e },
		};

		if (nops == 2 && o[1].kind == XK_REG && o[1].cls == XR_XMM)
			for (size_t i = 0; i < sizeof sse / sizeof *sse; i++)
				if (!strcmp(mn, sse[i].n)) {
					if (o[0].kind == XK_EXPR)
						o[0].kind = XK_MEM;
					return x64_sse(a, sse[i].pfx, sse[i].op, 0, o[1].reg, &o[0]);
				}
		/* moves: load (reg dest) / store forms */
		{
			static const struct { const char *n; uint8_t pfx, ld, st; } mv[] = {
				{ "movss", 0xf3, 0x10, 0x11 }, { "movsd", 0xf2, 0x10, 0x11 }, { "movaps", 0, 0x28, 0x29 },
				{ "movapd", 0x66, 0x28, 0x29 }, { "movups", 0, 0x10, 0x11 }, { "movupd", 0x66, 0x10, 0x11 },
				{ "movdqa", 0x66, 0x6f, 0x7f }, { "movdqu", 0xf3, 0x6f, 0x7f },
			};

			if (nops == 2)
				for (size_t i = 0; i < sizeof mv / sizeof *mv; i++)
					if (!strcmp(mn, mv[i].n)) {
						for (int k = 0; k < 2; k++)
							if (o[k].kind == XK_EXPR)
								o[k].kind = XK_MEM;
						if (o[1].kind == XK_REG && o[1].cls == XR_XMM && !(o[0].kind == XK_REG && mv[i].ld == 0x28 && 0))
							return x64_sse(a, mv[i].pfx, mv[i].ld, 0, o[1].reg, &o[0]);
						if (o[0].kind == XK_REG && o[0].cls == XR_XMM)
							return x64_sse(a, mv[i].pfx, mv[i].st, 0, o[0].reg, &o[1]);
					}
		}
		/* movd, movq between xmm and gpr/memory */
		if ((!strcmp(mn, "movd") || !strcmp(mn, "movq")) && nops == 2 &&
		    ((o[0].kind == XK_REG && o[0].cls == XR_XMM) || (o[1].kind == XK_REG && o[1].cls == XR_XMM))) {
			int w = mn[3] == 'q';

			for (int k = 0; k < 2; k++)
				if (o[k].kind == XK_EXPR)
					o[k].kind = XK_MEM;
			if (o[1].kind == XK_REG && o[1].cls == XR_XMM && o[0].kind == XK_REG && o[0].cls == XR_XMM)
				return x64_sse(a, 0xf3, 0x7e, 0, o[1].reg, &o[0]);	/* movq xmm, xmm */
			if (o[1].kind == XK_REG && o[1].cls == XR_XMM) {
				if (w && o[0].kind == XK_MEM)
					return x64_sse(a, 0xf3, 0x7e, 0, o[1].reg, &o[0]);
				return x64_sse(a, 0x66, 0x6e, w, o[1].reg, &o[0]);
			}
			if (w && o[1].kind == XK_MEM)
				return x64_sse(a, 0x66, 0xd6, 0, o[0].reg, &o[1]);
			return x64_sse(a, 0x66, 0x7e, w, o[0].reg, &o[1]);
		}
		/* conversions: cvtsi2sd{l,q}, cvttsd2si{l,q} ... */
		if (!strncmp(mn, "cvt", 3) && nops == 2) {
			char t[24];
			size_t n = strlen(mn);
			int w = 0;

			memcpy(t, mn, n + 1);
			if (!strncmp(t, "cvtsi2s", 7) || !strncmp(t, "cvttss2si", 9) || !strncmp(t, "cvttsd2si", 9) || !strncmp(t, "cvtss2si", 8) ||
			    !strncmp(t, "cvtsd2si", 8)) {
				if (t[n - 1] == 'q' || t[n - 1] == 'l') {
					w = t[n - 1] == 'q';
					t[n - 1] = 0;
				} else {
					w = !strncmp(t, "cvtsi", 5) ? opsize(&o[0]) == 8 : opsize(&o[1]) == 8;
				}
				for (int k = 0; k < 2; k++)
					if (o[k].kind == XK_EXPR)
						o[k].kind = XK_MEM;
				if (!strcmp(t, "cvtsi2sd") || !strcmp(t, "cvtsi2ss"))
					return x64_sse(a, t[7] == 'd' ? 0xf2 : 0xf3, 0x2a, w, o[1].reg, &o[0]);
				if (!strcmp(t, "cvttsd2si") || !strcmp(t, "cvttss2si"))
					return x64_sse(a, t[5] == 'd' ? 0xf2 : 0xf3, 0x2c, w, o[1].reg, &o[0]);
				if (!strcmp(t, "cvtsd2si") || !strcmp(t, "cvtss2si"))
					return x64_sse(a, t[4] == 'd' ? 0xf2 : 0xf3, 0x2d, w, o[1].reg, &o[0]);
			}
		}
	}
	return X64_NEXT;
}

VPRIV int x64_i_sse4(As *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
		/* SSE4.1 byte blend has an implicit xmm0 mask (optional in AT&T). */
		if (!strcmp(mn, "pblendvb") && (nops == 2 || (nops == 3 &&
		    o[0].kind == XK_REG && o[0].cls == XR_XMM && o[0].reg == 0))) {
			XOp *src = &o[nops - 2], *dst = &o[nops - 1];
			if (src->kind == XK_EXPR) src->kind = XK_MEM;
			if (dst->kind == XK_REG && dst->cls == XR_XMM &&
			    (src->kind == XK_MEM || (src->kind == XK_REG && src->cls == XR_XMM))) {
				uint8_t ops[] = { 0x0f, 0x38, 0x10 };
				XForm f = F(.opsz16 = 1, .op = ops, .nop = 3, .reg = dst->reg, .rm = src);
				return x64_emit(a, &f);
			}
		}
		/* SSE4.2 packed string comparisons: 66 0f 3a 60..63 /r ib. */
		if (nops == 3 && o[0].kind == XK_IMM && o[2].kind == XK_REG && o[2].cls == XR_XMM) {
			static const char *const names[] = { "pcmpestrm", "pcmpestri", "pcmpistrm", "pcmpistri" };
			for (unsigned k = 0; k < 4; k++) if (!strcmp(mn, names[k])) {
				if (o[1].kind == XK_EXPR) o[1].kind = XK_MEM;
				if (o[1].kind != XK_MEM && !(o[1].kind == XK_REG && o[1].cls == XR_XMM))
					break;
				int64_t imm = 0;
				uint8_t ops[] = { 0x0f, 0x3a, (uint8_t)(0x60 + k) };
				XForm f = F(.opsz16 = 1, .op = ops, .nop = 3, .reg = o[2].reg, .rm = &o[1], .isz = 1);
				x64_imm(a, &o[0], &f, &imm);
				f.imm = imm;
				return x64_emit(a, &f);
			}
		}
	/* ---- SSSE3/SSE4, AES-NI, PCLMUL, SHA, CRC32 (0f 38 / 0f 3a maps), FS/GS base ---- */
	{
		static const struct { const char *n; uint8_t pfx, map, op, imm; } x3[] = {
			{ "pshufb", 0x66, 0x38, 0x00, 0 }, { "phaddw", 0x66, 0x38, 0x01, 0 }, { "phaddd", 0x66, 0x38, 0x02, 0 },
			{ "pmaddubsw", 0x66, 0x38, 0x04, 0 }, { "psignb", 0x66, 0x38, 0x08, 0 }, { "pmulhrsw", 0x66, 0x38, 0x0b, 0 },
			{ "pabsb", 0x66, 0x38, 0x1c, 0 }, { "pabsw", 0x66, 0x38, 0x1d, 0 }, { "pabsd", 0x66, 0x38, 0x1e, 0 },
			{ "pmovzxbw", 0x66, 0x38, 0x30, 0 }, { "pmovzxbd", 0x66, 0x38, 0x31, 0 }, { "pmovzxbq", 0x66, 0x38, 0x32, 0 },
			{ "pmovzxwd", 0x66, 0x38, 0x33, 0 }, { "pmovzxwq", 0x66, 0x38, 0x34, 0 }, { "pmovzxdq", 0x66, 0x38, 0x35, 0 },
			{ "pmovsxbw", 0x66, 0x38, 0x20, 0 }, { "pmovsxdq", 0x66, 0x38, 0x25, 0 }, { "pmuldq", 0x66, 0x38, 0x28, 0 },
			{ "pcmpeqq", 0x66, 0x38, 0x29, 0 }, { "movntdqa", 0x66, 0x38, 0x2a, 0 }, { "packusdw", 0x66, 0x38, 0x2b, 0 },
			{ "pminsb", 0x66, 0x38, 0x38, 0 }, { "pminsd", 0x66, 0x38, 0x39, 0 }, { "pminuw", 0x66, 0x38, 0x3a, 0 }, { "pminud", 0x66, 0x38, 0x3b, 0 },
			{ "pmaxsb", 0x66, 0x38, 0x3c, 0 }, { "pmaxsd", 0x66, 0x38, 0x3d, 0 }, { "pmaxuw", 0x66, 0x38, 0x3e, 0 }, { "pmaxud", 0x66, 0x38, 0x3f, 0 },
			{ "pmulld", 0x66, 0x38, 0x40, 0 }, { "pcmpgtq", 0x66, 0x38, 0x37, 0 }, { "ptest", 0x66, 0x38, 0x17, 0 },
			{ "aesimc", 0x66, 0x38, 0xdb, 0 }, { "aesenc", 0x66, 0x38, 0xdc, 0 }, { "aesenclast", 0x66, 0x38, 0xdd, 0 },
			{ "aesdec", 0x66, 0x38, 0xde, 0 }, { "aesdeclast", 0x66, 0x38, 0xdf, 0 },
			{ "sha1nexte", 0, 0x38, 0xc8, 0 }, { "sha1msg1", 0, 0x38, 0xc9, 0 }, { "sha1msg2", 0, 0x38, 0xca, 0 },
			{ "sha256rnds2", 0, 0x38, 0xcb, 0 }, { "sha256msg1", 0, 0x38, 0xcc, 0 }, { "sha256msg2", 0, 0x38, 0xcd, 0 },
			{ "palignr", 0x66, 0x3a, 0x0f, 1 }, { "pclmulqdq", 0x66, 0x3a, 0x44, 1 }, { "aeskeygenassist", 0x66, 0x3a, 0xdf, 1 },
			{ "sha1rnds4", 0, 0x3a, 0xcc, 1 }, { "pblendw", 0x66, 0x3a, 0x0e, 1 }, { "blendps", 0x66, 0x3a, 0x0c, 1 },
			{ "roundss", 0x66, 0x3a, 0x0a, 1 }, { "roundsd", 0x66, 0x3a, 0x0b, 1 }, { "pcmpistri", 0x66, 0x3a, 0x63, 1 },
		};
		XOp *src = NULL, *dst = NULL, *im = NULL;

		/* op [imm,] xmm/m, xmm; sha256rnds2 writes its implicit %xmm0 first */
		if (nops >= 2 && o[nops - 1].kind == XK_REG && o[nops - 1].cls == XR_XMM) {
			dst = &o[nops - 1], src = &o[nops - 2];
			if (nops == 3 && o[0].kind == XK_IMM)
				im = &o[0];
			else if (nops == 3 && !(o[0].kind == XK_REG && o[0].cls == XR_XMM && o[0].reg == 0 && !strcmp(mn, "sha256rnds2")))
				src = NULL;
		}
		if (src && (src->kind == XK_MEM || src->kind == XK_EXPR || (src->kind == XK_REG && src->cls == XR_XMM)))
			for (size_t i = 0; i < sizeof x3 / sizeof *x3; i++)
				if (!strcmp(mn, x3[i].n) && !!im == !!x3[i].imm) {
					uint8_t ops[3] = { 0x0f, x3[i].map, x3[i].op };
					int64_t c = 0;
					XForm f = F(.opsz16 = x3[i].pfx == 0x66, .op = ops, .nop = 3, .reg = dst->reg, .rm = src, .isz = im ? 1 : 0);

					if (src->kind == XK_EXPR)
						src->kind = XK_MEM;
					if (im) {
						x64_imm(a, im, &f, &c);
						f.imm = c;
					}
					return x64_emit(a, &f);
				}
		/* crc32[bwlq] r/m, r32/r64: f2 (66) 0f 38 f0/f1 */
		if (!strncmp(mn, "crc32", 5) && nops == 2 && o[1].kind == XK_REG && o[1].cls == XR_GPR) {
			int z = mn[5] ? sfxsize(mn[5]) : o[0].kind == XK_REG ? o[0].size : 0;
			uint8_t ops[3] = { 0x0f, 0x38, (uint8_t)(z == 1 ? 0xf0 : 0xf1) };

			if (!z || (mn[5] && mn[6]))
				return err(a, "invalid operand for instruction");
			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .rep = 0xf2, .rex8 = z == 1 && o[0].kind == XK_REG && o[0].needrex,
					      .op = ops, .nop = 3, .reg = o[1].reg, .rm = &o[0]));
		}
		/* pinsrd/pextrd/pinsrq/pextrq/pextrb/pinsrb */
		if (nops == 3 && o[0].kind == XK_IMM && (!strcmp(mn, "pinsrd") || !strcmp(mn, "pinsrq") || !strcmp(mn, "pinsrb") ||
		    !strcmp(mn, "pextrd") || !strcmp(mn, "pextrq") || !strcmp(mn, "pextrb"))) {
			bool ins = mn[1] == 'i';
			XOp *x = ins ? &o[2] : &o[1], *g = ins ? &o[1] : &o[2];
			uint8_t ops[3] = { 0x0f, 0x3a, (uint8_t)(ins ? (mn[5] == 'b' ? 0x20 : 0x22) : (mn[5] == 'b' ? 0x14 : 0x16)) };
			int64_t c = 0;
			XForm f = F(.w = mn[5] == 'q', .opsz16 = 1, .op = ops, .nop = 3, .reg = x->reg, .rm = g, .isz = 1);

			if (x->kind != XK_REG || x->cls != XR_XMM)
				return err(a, "invalid operand for instruction");
			if (g->kind == XK_EXPR)
				g->kind = XK_MEM;
			x64_imm(a, &o[0], &f, &c);
			f.imm = c;
			return x64_emit(a, &f);
		}
		/* rd/wr fs/gs base: f3 (rex.w) 0f ae /0../3 */
		if (nops == 1 && o[0].kind == XK_REG && o[0].cls == XR_GPR && (o[0].size == 8 || o[0].size == 4) &&
		    (!strcmp(mn, "rdfsbase") || !strcmp(mn, "rdgsbase") || !strcmp(mn, "wrfsbase") || !strcmp(mn, "wrgsbase"))) {
			uint8_t ops[2] = { 0x0f, 0xae };

			return x64_emit(a, &F(.w = o[0].size == 8, .rep = 0xf3, .op = ops, .nop = 2,
					      .reg = (mn[0] == 'w' ? 2 : 0) + (mn[2] == 'g'), .rm = &o[0]));
		}
		if (nops == 0 && (!strcmp(mn, "emms") || !strcmp(mn, "fnclex") || !strcmp(mn, "mwaitx") || !strcmp(mn, "monitorx"))) {
			static const uint8_t b0[] = { 0x0f, 0x77 }, b1[] = { 0xdb, 0xe2 }, b2[] = { 0x0f, 0x01, 0xfb }, b3[] = { 0x0f, 0x01, 0xfa };

			return mn[0] == 'e' ? x64_raw(a, b0, 2) : mn[1] == 'n' ? x64_raw(a, b1, 2) : mn[0] == 'm' && mn[1] == 'w' ? x64_raw(a, b2, 3) : x64_raw(a, b3, 3);
		}
		if (nops == 1 && (!strcmp(mn, "fnsave") || !strcmp(mn, "frstor") || !strcmp(mn, "fnstenv") || !strcmp(mn, "fldenv"))) {
			uint8_t ops[1] = { (uint8_t)(mn[2] == 's' || mn[1] == 'r' ? 0xdd : 0xd9) };

			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			return x64_emit(a, &F(.op = ops, .nop = 1, .reg = !strcmp(mn, "fnsave") ? 6 : !strcmp(mn, "frstor") ? 4 :
					      !strcmp(mn, "fnstenv") ? 6 : 4, .rm = &o[0]));
		}
	}
	return X64_NEXT;
}

VPRIV int x64_i_ssepk(As *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- SSE2 packed integer and more SSE (66/none 0f op /r) ---- */
	{
		static const struct { const char *n; uint8_t pfx, op; } pk[] = {
			{ "paddb", 0x66, 0xfc }, { "paddw", 0x66, 0xfd }, { "paddd", 0x66, 0xfe }, { "paddq", 0x66, 0xd4 },
			{ "psubb", 0x66, 0xf8 }, { "psubw", 0x66, 0xf9 }, { "psubd", 0x66, 0xfa }, { "psubq", 0x66, 0xfb },
			{ "pand", 0x66, 0xdb }, { "pandn", 0x66, 0xdf }, { "por", 0x66, 0xeb },
			{ "pcmpeqb", 0x66, 0x74 }, { "pcmpeqw", 0x66, 0x75 }, { "pcmpeqd", 0x66, 0x76 },
			{ "pcmpgtb", 0x66, 0x64 }, { "pcmpgtw", 0x66, 0x65 }, { "pcmpgtd", 0x66, 0x66 },
			{ "punpcklbw", 0x66, 0x60 }, { "punpcklwd", 0x66, 0x61 }, { "punpckldq", 0x66, 0x62 }, { "punpcklqdq", 0x66, 0x6c },
			{ "punpckhbw", 0x66, 0x68 }, { "punpckhwd", 0x66, 0x69 }, { "punpckhdq", 0x66, 0x6a }, { "punpckhqdq", 0x66, 0x6d },
			{ "packsswb", 0x66, 0x63 }, { "packuswb", 0x66, 0x67 }, { "packssdw", 0x66, 0x6b },
			{ "pmullw", 0x66, 0xd5 }, { "pmuludq", 0x66, 0xf4 }, { "pmaxub", 0x66, 0xde }, { "pminub", 0x66, 0xda },
			{ "pmaxsw", 0x66, 0xee }, { "pminsw", 0x66, 0xea }, { "pmulhw", 0x66, 0xe5 }, { "pmulhuw", 0x66, 0xe4 },
			{ "pmaddwd", 0x66, 0xf5 }, { "pavgb", 0x66, 0xe0 }, { "pavgw", 0x66, 0xe3 }, { "psadbw", 0x66, 0xf6 },
			{ "paddusb", 0x66, 0xdc }, { "paddusw", 0x66, 0xdd }, { "psubusb", 0x66, 0xd8 }, { "psubusw", 0x66, 0xd9 },
			{ "paddsb", 0x66, 0xec }, { "paddsw", 0x66, 0xed }, { "psubsb", 0x66, 0xe8 }, { "psubsw", 0x66, 0xe9 },
			{ "psrlw", 0x66, 0xd1 }, { "psrld", 0x66, 0xd2 }, { "psrlq", 0x66, 0xd3 }, { "psraw", 0x66, 0xe1 },
			{ "psrad", 0x66, 0xe2 }, { "psllw", 0x66, 0xf1 }, { "pslld", 0x66, 0xf2 }, { "psllq", 0x66, 0xf3 },
			{ "unpckhps", 0, 0x15 }, { "unpckhpd", 0x66, 0x15 }, { "andnpd", 0x66, 0x55 }, { "orpd", 0x66, 0x56 },
			{ "cvtdq2pd", 0xf3, 0xe6 }, { "cvtdq2ps", 0, 0x5b }, { "cvttps2dq", 0xf3, 0x5b }, { "cvtps2dq", 0x66, 0x5b },
			{ "cvtps2pd", 0, 0x5a }, { "cvtpd2ps", 0x66, 0x5a }, { "cvttpd2dq", 0x66, 0xe6 }, { "cvtpd2dq", 0xf2, 0xe6 },
			{ "addpd", 0x66, 0x58 }, { "mulpd", 0x66, 0x59 }, { "subpd", 0x66, 0x5c }, { "divpd", 0x66, 0x5e },
			{ "maxps", 0, 0x5f }, { "minps", 0, 0x5d }, { "maxpd", 0x66, 0x5f }, { "minpd", 0x66, 0x5d },
			{ "sqrtps", 0, 0x51 }, { "sqrtpd", 0x66, 0x51 }, { "rcpps", 0, 0x53 }, { "rsqrtps", 0, 0x52 },
			{ "movhlps", 0, 0x12 }, { "movlhps", 0, 0x16 },
		};
		static const struct { const char *n; uint8_t op, d; } pki[] = {	/* shift by an immediate: 66 0f op /d ib */
			{ "psrlw", 0x71, 2 }, { "psraw", 0x71, 4 }, { "psllw", 0x71, 6 }, { "psrld", 0x72, 2 }, { "psrad", 0x72, 4 },
			{ "pslld", 0x72, 6 }, { "psrlq", 0x73, 2 }, { "psllq", 0x73, 6 }, { "psrldq", 0x73, 3 }, { "pslldq", 0x73, 7 },
		};

		if (nops == 2 && o[1].kind == XK_REG && o[1].cls == XR_XMM) {
			if (o[0].kind == XK_IMM) {
				for (size_t i = 0; i < sizeof pki / sizeof *pki; i++)
					if (!strcmp(mn, pki[i].n)) {
						int64_t c = 0;
						uint8_t ops[2] = { 0x0f, pki[i].op };
						XForm f = F(.opsz16 = 1, .op = ops, .nop = 2, .reg = pki[i].d, .rm = &o[1], .isz = 1);

						x64_imm(a, &o[0], &f, &c);
						f.imm = c;
						return x64_emit(a, &f);
					}
			}
			for (size_t i = 0; i < sizeof pk / sizeof *pk; i++)
				if (!strcmp(mn, pk[i].n)) {
					if (o[0].kind == XK_EXPR)
						o[0].kind = XK_MEM;
					return x64_sse(a, pk[i].pfx, pk[i].op, 0, o[1].reg, &o[0]);
				}
		}
		/* with an immediate: pshufd $k, src, dst; shufps; cmpXXss as cmpss $pred */
		if (nops == 3 && o[0].kind == XK_IMM && o[2].kind == XK_REG && o[2].cls == XR_XMM) {
			static const struct { const char *n; uint8_t pfx, op; } si[] = {
				{ "pshufd", 0x66, 0x70 }, { "pshuflw", 0xf2, 0x70 }, { "pshufhw", 0xf3, 0x70 }, { "shufps", 0, 0xc6 },
				{ "shufpd", 0x66, 0xc6 }, { "cmpss", 0xf3, 0xc2 }, { "cmpsd", 0xf2, 0xc2 }, { "cmpps", 0, 0xc2 }, { "cmppd", 0x66, 0xc2 },
			};

			for (size_t i = 0; i < sizeof si / sizeof *si; i++)
				if (!strcmp(mn, si[i].n)) {
					int64_t c = 0;
					uint8_t ops[2] = { 0x0f, si[i].op };
					XForm f = F(.rep = si[i].pfx == 0x66 ? 0 : si[i].pfx, .opsz16 = si[i].pfx == 0x66, .op = ops, .nop = 2,
						    .reg = o[2].reg, .rm = &o[1], .isz = 1);

					if (o[1].kind == XK_EXPR)
						o[1].kind = XK_MEM;
					x64_imm(a, &o[0], &f, &c);
					f.imm = c;
					return x64_emit(a, &f);
				}
		}
		/* pextrw $k, %xmm, %r32 (66 0f c5); pinsrw $k, %r32|m16, %xmm (66 0f c4) */
		if (nops == 3 && o[0].kind == XK_IMM && (!strcmp(mn, "pextrw") || !strcmp(mn, "pinsrw"))) {
			int64_t c = 0;
			int ext = mn[1] == 'e';
			XForm f = F(.opsz16 = 1, .op = ext ? OPB(0x0f, 0xc5) : OPB(0x0f, 0xc4), .nop = 2, .reg = o[2].reg,
				    .rm = &o[1], .isz = 1);

			if (o[1].kind == XK_EXPR)
				o[1].kind = XK_MEM;
			x64_imm(a, &o[0], &f, &c);
			f.imm = c;
			return x64_emit(a, &f);
		}
		/* cmpeqss ...: the predicate in the name */
		if (!strncmp(mn, "cmp", 3) && nops == 2 && o[1].kind == XK_REG && o[1].cls == XR_XMM) {
			static const char *const pr[8] = { "eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord" };
			size_t n = strlen(mn);

			if (n > 5) {
				const char *ty = mn + n - 2;
				uint8_t pfx = !strcmp(ty, "ss") ? 0xf3 : !strcmp(ty, "sd") ? 0xf2 : !strcmp(ty, "pd") ? 0x66 : !strcmp(ty, "ps") ? 1 : 0;

				for (int k = 0; k < 8 && pfx; k++)
					if (strlen(pr[k]) == n - 5 && !memcmp(mn + 3, pr[k], n - 5)) {
						uint8_t ops[2] = { 0x0f, 0xc2 };
						XForm f = F(.rep = pfx == 0x66 || pfx == 1 ? 0 : pfx, .opsz16 = pfx == 0x66, .op = ops, .nop = 2,
							    .reg = o[1].reg, .rm = &o[0], .isz = 1, .imm = k);

						if (o[0].kind == XK_EXPR)
							o[0].kind = XK_MEM;
						return x64_emit(a, &f);
					}
			}
		}
		/* movlpd/movhpd/movlps/movhps: load 12/16, store 13/17 */
		{
			static const struct { const char *n; uint8_t pfx, ld; } lh[] = {
				{ "movlpd", 0x66, 0x12 }, { "movhpd", 0x66, 0x16 }, { "movlps", 0, 0x12 }, { "movhps", 0, 0x16 },
			};

			if (nops == 2)
				for (size_t i = 0; i < sizeof lh / sizeof *lh; i++)
					if (!strcmp(mn, lh[i].n)) {
						for (int k = 0; k < 2; k++)
							if (o[k].kind == XK_EXPR)
								o[k].kind = XK_MEM;
						if (o[1].kind == XK_REG && o[1].cls == XR_XMM)
							return x64_sse(a, lh[i].pfx, lh[i].ld, 0, o[1].reg, &o[0]);
						return x64_sse(a, lh[i].pfx, lh[i].ld + 1, 0, o[0].reg, &o[1]);
					}
		}
		if ((!strcmp(mn, "movmskps") || !strcmp(mn, "movmskpd") || !strcmp(mn, "pmovmskb")) && nops == 2)
			return x64_sse(a, mn[7] == 'd' || mn[0] == 'p' ? 0x66 : 0, mn[0] == 'p' ? 0xd7 : 0x50, 0, o[1].reg, &o[0]);
	}
	return X64_NEXT;
}
