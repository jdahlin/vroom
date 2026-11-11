/* Native AVX-512 encodings. Operand classes describe the architectural forms;
 * the common encoder owns EVEX extensions, ModRM/SIB, tuple disp8 and fixups. */
#include "x64as.h"

enum { XE_NDS, XE_UNARY, XE_MOVE, XE_EXTRACT, XE_INSERT };
typedef struct {
	const char *name;
	uint8_t map, pp, op, w, form, imm, elem, narrow, encoding, nomask; /* encoding: 0 legacy table, 1 EVEX, 2 either */
} XEvex;
#define E(n,m,p,o,w,f,i,b,s,a,k) {n,m,p,o,w,f,i,b,s,a,k}
static const XEvex xevex[] = {
	E("vmovdqu8",1,3,0x6f,0,XE_MOVE,0,0,0,1,0),
	E("vmovdqu16",1,3,0x6f,1,XE_MOVE,0,0,0,1,0),
	E("vmovdqu32",1,2,0x6f,0,XE_MOVE,0,0,0,1,0),
	E("vmovdqu64",1,2,0x6f,1,XE_MOVE,0,0,0,1,0),
	E("vmovdqa32",1,1,0x6f,0,XE_MOVE,0,0,0,1,0),
	E("vmovdqa64",1,1,0x6f,1,XE_MOVE,0,0,0,1,0),
	E("vmovaps",1,0,0x28,0,XE_MOVE,0,0,0,0,0),
	E("vmovapd",1,1,0x28,1,XE_MOVE,0,0,0,0,0),
	E("vmovups",1,0,0x10,0,XE_MOVE,0,0,0,0,0),
	E("vmovupd",1,1,0x10,1,XE_MOVE,0,0,0,0,0),
	E("vpopcntd",2,1,0x55,0,XE_UNARY,0,4,0,1,0),
	E("vpopcntq",2,1,0x55,1,XE_UNARY,0,8,0,1,0),
	E("vpopcntb",2,1,0x54,0,XE_UNARY,0,0,0,1,0),
	E("vpopcntw",2,1,0x54,1,XE_UNARY,0,0,0,1,0),
	E("vpternlogd",3,1,0x25,0,XE_NDS,1,4,0,1,0),
	E("vpternlogq",3,1,0x25,1,XE_NDS,1,8,0,1,0),
	E("vpclmulqdq",3,1,0x44,0,XE_NDS,1,0,0,0,1),
	E("vpaddb",1,1,0xfc,0,XE_NDS,0,0,0,0,0),
	E("vpaddw",1,1,0xfd,0,XE_NDS,0,0,0,0,0),
	E("vpaddd",1,1,0xfe,0,XE_NDS,0,4,0,0,0),
	E("vpaddq",1,1,0xd4,1,XE_NDS,0,8,0,0,0),
	E("vpsubb",1,1,0xf8,0,XE_NDS,0,0,0,0,0),
	E("vpsubw",1,1,0xf9,0,XE_NDS,0,0,0,0,0),
	E("vpsubd",1,1,0xfa,0,XE_NDS,0,4,0,0,0),
	E("vpsubq",1,1,0xfb,1,XE_NDS,0,8,0,0,0),
	E("vpandd",1,1,0xdb,0,XE_NDS,0,4,0,1,0),
	E("vpandq",1,1,0xdb,1,XE_NDS,0,8,0,1,0),
	E("vpord",1,1,0xeb,0,XE_NDS,0,4,0,1,0),
	E("vporq",1,1,0xeb,1,XE_NDS,0,8,0,1,0),
	E("vpxord",1,1,0xef,0,XE_NDS,0,4,0,1,0),
	E("vpxorq",1,1,0xef,1,XE_NDS,0,8,0,1,0),
	E("vpandnd",1,1,0xdf,0,XE_NDS,0,4,0,1,0),
	E("vpandnq",1,1,0xdf,1,XE_NDS,0,8,0,1,0),
	E("vaddps",1,0,0x58,0,XE_NDS,0,4,0,0,0),
	E("vaddpd",1,1,0x58,1,XE_NDS,0,8,0,0,0),
	E("vmulps",1,0,0x59,0,XE_NDS,0,4,0,0,0),
	E("vmulpd",1,1,0x59,1,XE_NDS,0,8,0,0,0),
	E("vsubps",1,0,0x5c,0,XE_NDS,0,4,0,0,0),
	E("vsubpd",1,1,0x5c,1,XE_NDS,0,8,0,0,0),
	E("vminps",1,0,0x5d,0,XE_NDS,0,4,0,0,0),
	E("vminpd",1,1,0x5d,1,XE_NDS,0,8,0,0,0),
	E("vdivps",1,0,0x5e,0,XE_NDS,0,4,0,0,0),
	E("vdivpd",1,1,0x5e,1,XE_NDS,0,8,0,0,0),
	E("vmaxps",1,0,0x5f,0,XE_NDS,0,4,0,0,0),
	E("vmaxpd",1,1,0x5f,1,XE_NDS,0,8,0,0,0),
	E("vpminsb",2,1,0x38,0,XE_NDS,0,0,0,2,0),
	E("vpminsw",1,1,0xea,0,XE_NDS,0,0,0,2,0),
	E("vpminsd",2,1,0x39,0,XE_NDS,0,4,0,2,0),
	E("vpminsq",2,1,0x39,1,XE_NDS,0,8,0,1,0),
	E("vpminub",1,1,0xda,0,XE_NDS,0,0,0,2,0),
	E("vpminuw",2,1,0x3a,0,XE_NDS,0,0,0,2,0),
	E("vpminud",2,1,0x3b,0,XE_NDS,0,4,0,2,0),
	E("vpminuq",2,1,0x3b,1,XE_NDS,0,8,0,1,0),
	E("vpmaxsb",2,1,0x3c,0,XE_NDS,0,0,0,2,0),
	E("vpmaxsw",1,1,0xee,0,XE_NDS,0,0,0,2,0),
	E("vpmaxsd",2,1,0x3d,0,XE_NDS,0,4,0,2,0),
	E("vpmaxsq",2,1,0x3d,1,XE_NDS,0,8,0,1,0),
	E("vpmaxub",1,1,0xde,0,XE_NDS,0,0,0,2,0),
	E("vpmaxuw",2,1,0x3e,0,XE_NDS,0,0,0,2,0),
	E("vpmaxud",2,1,0x3f,0,XE_NDS,0,4,0,2,0),
	E("vpmaxuq",2,1,0x3f,1,XE_NDS,0,8,0,1,0),
	E("vpaddsb",1,1,0xec,0,XE_NDS,0,0,0,2,0),
	E("vpaddsw",1,1,0xed,0,XE_NDS,0,0,0,2,0),
	E("vpaddusb",1,1,0xdc,0,XE_NDS,0,0,0,2,0),
	E("vpaddusw",1,1,0xdd,0,XE_NDS,0,0,0,2,0),
	E("vpsubsb",1,1,0xe8,0,XE_NDS,0,0,0,2,0),
	E("vpsubsw",1,1,0xe9,0,XE_NDS,0,0,0,2,0),
	E("vpsubusb",1,1,0xd8,0,XE_NDS,0,0,0,2,0),
	E("vpsubusw",1,1,0xd9,0,XE_NDS,0,0,0,2,0),
	E("vextracti32x4",3,1,0x39,0,XE_EXTRACT,1,0,16,1,0),
	E("vextracti64x2",3,1,0x39,1,XE_EXTRACT,1,0,16,1,0),
	E("vextracti32x8",3,1,0x3b,0,XE_EXTRACT,1,0,32,1,0),
	E("vextracti64x4",3,1,0x3b,1,XE_EXTRACT,1,0,32,1,0),
	E("vextractf32x4",3,1,0x19,0,XE_EXTRACT,1,0,16,1,0),
	E("vextractf64x2",3,1,0x19,1,XE_EXTRACT,1,0,16,1,0),
	E("vextractf32x8",3,1,0x1b,0,XE_EXTRACT,1,0,32,1,0),
	E("vextractf64x4",3,1,0x1b,1,XE_EXTRACT,1,0,32,1,0),
	E("vinserti32x4",3,1,0x38,0,XE_INSERT,1,0,16,1,0),
	E("vinserti64x2",3,1,0x38,1,XE_INSERT,1,0,16,1,0),
	E("vinserti32x8",3,1,0x3a,0,XE_INSERT,1,0,32,1,0),
	E("vinserti64x4",3,1,0x3a,1,XE_INSERT,1,0,32,1,0),
};
#undef E

static int xev_vec(const XOp *o, int size)
{
	return o->kind == XK_REG && o->cls == XR_XMM && o->size == size;
}
static int xev_memory(const XOp *o)
{
	return o->kind == XK_MEM || o->kind == XK_EXPR;
}
static int xev_kmov(As *a, char *mn, XOp *o, int n)
{
	if (strncmp(mn, "kmov", 4) || strlen(mn) != 5) return X64_NEXT;
	int sz = mn[4] == 'd' ? 4 : mn[4] == 'l' ? 0 : sfxsize(mn[4]);
	if (!sz) return X64_NEXT;
	if (n != 2) return err(a, "invalid mask move operands");
	for (int i = 0; i < n; i++)
		if (o[i].mask || o[i].zero || o[i].broadcast) return err(a, "invalid mask move decorator");
	int sk = o[0].kind == XK_REG && o[0].cls == XR_K;
	int dk = o[1].kind == XK_REG && o[1].cls == XR_K;
	int sg = o[0].kind == XK_REG && o[0].cls == XR_GPR;
	int dg = o[1].kind == XK_REG && o[1].cls == XR_GPR;
	int op, reg, pp, w;
	XOp rm;
	if (dk && (sk || xev_memory(&o[0]))) op = 0x90, reg = o[1].reg, rm = o[0];
	else if (sk && xev_memory(&o[1])) op = 0x91, reg = o[0].reg, rm = o[1];
	else if (dk && sg) op = 0x92, reg = o[1].reg, rm = o[0];
	else if (sk && dg) op = 0x93, reg = o[1].reg, rm = o[0];
	else return err(a, "invalid mask move operands");
	if ((sg && o[0].size != (sz == 8 ? 8 : 4)) || (dg && o[1].size != (sz == 8 ? 8 : 4)))
		return err(a, "invalid mask move register width");
	if (sg || dg) pp = sz == 1 ? 1 : sz >= 4 ? 3 : 0, w = sz == 8;
	else pp = sz == 1 || sz == 4 ? 1 : 0, w = sz >= 4;
	if (rm.kind == XK_EXPR) rm.kind = XK_MEM;
	uint8_t bytes[] = {0x0f, (uint8_t)op};
	XForm f = F(.vex=1, .op=bytes, .nop=2, .reg=reg, .rm=&rm, .w=w,
		.opsz16=pp==1, .rep=pp==3?0xf2:0);
	return x64_emit(a, &f);
}

VPRIV int x64_i_evex(As *a, char *mn, XOp *o, int n)
{
	if (mn[0] == 'k') return xev_kmov(a, mn, o, n);
	if (mn[0] != 'v') return X64_NEXT;
	const XEvex *d = NULL;
	for (size_t i = 0; i < sizeof xevex / sizeof *xevex; i++)
		if (!strcmp(mn, xevex[i].name)) { d = &xevex[i]; break; }
	if (!d) return X64_NEXT;
	int required = d->encoding == 1;
	for (int i = 0; i < n; i++)
		if (o[i].mask || o[i].zero || o[i].broadcast ||
		    (o[i].kind == XK_REG && o[i].cls == XR_XMM && (o[i].size == 64 || o[i].reg >= 16))) required = 1;
	if (!required && d->encoding != 2) return X64_NEXT;
	int count = (d->form == XE_NDS || d->form == XE_INSERT ? 3 : 2) + d->imm;
	if (n != count) return err(a, "invalid EVEX operand count");
	XOp *dst = &o[n - 1], *src = &o[d->imm];
	int size = dst->size, reg = dst->reg, v = 0, opcode = d->op;
	XOp rm = *src;
	if (d->form == XE_EXTRACT) {
		size = src->size;
		if (!xev_vec(src, size) || size <= d->narrow || (size != 32 && size != 64) ||
		    !(xev_vec(dst, d->narrow) || xev_memory(dst))) return err(a, "invalid EVEX extract operands");
		reg = src->reg, rm = *dst;
	} else if (d->form == XE_MOVE && xev_memory(dst)) {
		size = src->size;
		if (!xev_vec(src, size)) return err(a, "invalid EVEX move operands");
		reg = src->reg, rm = *dst;
		opcode += opcode == 0x6f ? 0x10 : 1;
	} else {
		if (!xev_vec(dst, size)) return err(a, "invalid EVEX destination");
		int rsize = d->form == XE_INSERT ? d->narrow : size;
		if (!(xev_vec(src, rsize) || xev_memory(src))) return err(a, "invalid EVEX source width");
		if (d->form == XE_NDS || d->form == XE_INSERT) {
			if (!xev_vec(&o[n - 2], size)) return err(a, "invalid EVEX second source");
			v = o[n - 2].reg;
		}
		if (d->form == XE_INSERT && size <= d->narrow) return err(a, "invalid EVEX insert width");
	}
	if (size != 16 && size != 32 && size != 64) return err(a, "invalid EVEX vector width");
	for (int i = 0; i < n; i++) {
		if ((o[i].mask || o[i].zero) && &o[i] != dst) return err(a, "mask must decorate destination");
		if (o[i].broadcast && (&o[i] != src || !xev_memory(src))) return err(a, "invalid broadcast operand");
	}
	if (dst->zero && xev_memory(dst)) return err(a, "zeroing requires register destination");
	/* LLVM/GNU syntax accepts a standalone {z} but ignores it without a mask. */
	if (!dst->mask) dst->zero = 0;
	if (d->nomask && dst->mask) return err(a, "instruction does not support masking");
	if (src->broadcast && (!d->elem || src->broadcast != size / d->elem)) return err(a, "invalid broadcast width");
	if (rm.kind == XK_EXPR) rm.kind = XK_MEM;
	uint8_t bytes[] = {0x0f, d->map == 1 ? 0x0f : d->map == 2 ? 0x38 : 0x3a, (uint8_t)opcode};
	XForm f = F(.evex=required, .vex=!required, .op=d->map==1?bytes+1:bytes, .nop=d->map==1?2:3,
		.reg=reg, .rm=&rm, .w=d->w, .opsz16=d->pp==1, .rep=d->pp==2?0xf3:d->pp==3?0xf2:0,
		.vvvv=v, .vl=size==64?2:size==32?1:0, .mask=dst->mask, .zero=dst->zero,
		.broadcast=!!src->broadcast, .tuple=src->broadcast?d->elem:d->narrow?d->narrow:size);
	if (d->imm) {
		int64_t imm = 0;
		if (o[0].kind != XK_IMM) return err(a, "EVEX control must be an 8-bit immediate");
		if (!x64_imm(a, &o[0], &f, &imm) && (imm < -128 || imm > 255))
			return err(a, "EVEX control must be an 8-bit immediate");
		f.imm = imm, f.isz = 1;
	}
	return x64_emit(a, &f);
}
