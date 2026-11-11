/*
 * x86-64 instructions in AT&T syntax: the parser and the encoder, choosing
 * the encodings LLVM MC chooses (clang -c), so an object is byte-identical
 * with clang's for the same text.  Jumps to labels are variable items the
 * assembler relaxes (as.c: as_branch).
 */
#include "../../../as/asimpl.h"

enum { XR_NONE, XR_GPR, XR_XMM, XR_ST, XR_SEG, XR_RIP };
enum { XK_REG, XK_IMM, XK_MEM, XK_EXPR };	/* XK_EXPR: a bare expression (branch target, absolute memory) */
/* relocation modifiers: sym@MOD */
enum { XM_NONE, XM_PLT, XM_GOTPCREL, XM_TPOFF, XM_GOTTPOFF, XM_TLSGD, XM_TLSLD, XM_DTPOFF, XM_GOTOFF, XM_GOT };

typedef struct {
	uint8_t kind, cls, reg, size, hi8, star, needrex;	/* register: class, number, size; hi8: ah..bh */
	int8_t base, index;	/* memory: -1 none; base 16: rip */
	uint8_t scale, seg, mod;	/* seg: 0 none, 0x64 fs, 0x65 gs */
	Val v;			/* immediate / displacement / target */
	int hasv;
} XOp;

/* ---------------------------------------------------------------- operands */

static int x64_regname(const char *s, size_t n, XOp *o)
{
	static const char *const r64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
	static const char *const r32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
	static const char *const r16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
	static const char *const r8[] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil" };
	static const char *const r8h[] = { "ah", "ch", "dh", "bh" };
	char b[16];

	if (n >= sizeof b)
		return -1;
	for (size_t i = 0; i < n; i++)
		b[i] = (char)c_lower((unsigned char)s[i]);
	b[n] = 0;
	o->cls = XR_GPR;
	o->hi8 = 0;
	o->needrex = 0;
	for (int i = 0; i < 8; i++) {
		if (!strcmp(b, r64[i])) { o->reg = (uint8_t)i, o->size = 8; return 0; }
		if (!strcmp(b, r32[i])) { o->reg = (uint8_t)i, o->size = 4; return 0; }
		if (!strcmp(b, r16[i])) { o->reg = (uint8_t)i, o->size = 2; return 0; }
		if (!strcmp(b, r8[i])) { o->reg = (uint8_t)i, o->size = 1, o->needrex = i >= 4; return 0; }
	}
	for (int i = 0; i < 4; i++)
		if (!strcmp(b, r8h[i])) { o->reg = (uint8_t)(i + 4), o->size = 1, o->hi8 = 1; return 0; }
	if (b[0] == 'r' && c_digit((unsigned char)b[1])) {
		int r = atoi(b + 1);
		const char *t = b + 1;

		while (c_digit((unsigned char)*t))
			t++;
		if (r < 8 || r > 15)
			return -1;
		o->reg = (uint8_t)r;
		o->size = !*t ? 8 : !strcmp(t, "d") ? 4 : !strcmp(t, "w") ? 2 : !strcmp(t, "b") ? 1 : 0;
		return o->size ? 0 : -1;
	}
	if (!strncmp(b, "xmm", 3) && c_digit((unsigned char)b[3])) {
		int r = atoi(b + 3);

		if (r > 15)
			return -1;
		o->cls = XR_XMM, o->reg = (uint8_t)r, o->size = 16;
		return 0;
	}
	if (!strcmp(b, "rip")) { o->cls = XR_RIP; return 0; }
	if (!strcmp(b, "fs")) { o->cls = XR_SEG, o->reg = 0x64; return 0; }
	if (!strcmp(b, "es")) { o->cls = XR_SEG, o->reg = 0x26; return 0; }
	if (!strcmp(b, "ds")) { o->cls = XR_SEG, o->reg = 0x3e; return 0; }
	if (!strcmp(b, "cs")) { o->cls = XR_SEG, o->reg = 0x2e; return 0; }
	if (!strcmp(b, "ss")) { o->cls = XR_SEG, o->reg = 0x36; return 0; }
	if (!strcmp(b, "gs")) { o->cls = XR_SEG, o->reg = 0x65; return 0; }
	if (!strcmp(b, "st")) { o->cls = XR_ST, o->reg = 0; return 0; }
	return -1;
}

/* a register at p (after %): its name's end */
static const char *x64_reg(As *a, const char *p, const char *e, XOp *o)
{
	const char *q = p;

	while (q < e && (isidc((unsigned char)*q) || c_digit((unsigned char)*q)))
		q++;
	if (q == p || x64_regname(p, (size_t)(q - p), o)) {
		err(a, "invalid register name");
		return NULL;
	}
	if (o->cls == XR_ST) {
		const char *r = ws(q, e);

		if (r < e && *r == '(') {
			r = ws(r + 1, e);
			if (r >= e || !c_digit((unsigned char)*r)) { err(a, "invalid st register"); return NULL; }
			o->reg = (uint8_t)(*r++ - '0');
			r = ws(r, e);
			if (r >= e || *r != ')') { err(a, "invalid st register"); return NULL; }
			q = r + 1;
		}
	}
	return q;
}

/* an expression with an optional @MOD: [p, e) up to a top-level ( or , */
static int x64_expr(As *a, const char **pp, const char *e, Val *v, uint8_t *mod)
{
	const char *p = *pp, *q = p, *at = NULL;
	int depth = 0;

	*mod = XM_NONE;
	/* the expression's extent: a '(' that starts a base/index list ends it */
	while (q < e) {
		if (*q == '"') {
			q++;
			while (q < e && *q != '"')
				q++;
		} else if (*q == '(') {
			const char *r = ws(q + 1, e);

			if (depth == 0 && (r >= e || *r == '%' || *r == ','))
				break;
			depth++;
		} else if (*q == ')') {
			depth--;
		} else if (*q == ',' && depth == 0) {
			break;
		} else if (*q == '@' && depth == 0 && !at) {
			at = q;
		}
		q++;
	}
	if (at) {
		static const struct { const char *n; uint8_t m; } mods[] = {
			{ "PLT", XM_PLT }, { "GOTPCREL", XM_GOTPCREL }, { "TPOFF", XM_TPOFF }, { "GOTTPOFF", XM_GOTTPOFF },
			{ "TLSGD", XM_TLSGD }, { "TLSLD", XM_TLSLD }, { "DTPOFF", XM_DTPOFF }, { "GOTOFF", XM_GOTOFF }, { "GOT", XM_GOT },
		};
		const char *m = at + 1, *me = m;
		const char *x = p;

		while (me < q && isidc((unsigned char)*me))
			me++;
		for (size_t i = 0; i < sizeof mods / sizeof *mods; i++)
			if ((size_t)(me - m) == strlen(mods[i].n) && !memcmp(m, mods[i].n, (size_t)(me - m)))
				*mod = mods[i].m;
		if (*mod == XM_NONE)
			return err(a, "invalid variant '%.*s'", (int)(me - m), m);
		if (val_expr(a, &x, at, v))
			return -1;
		/* sym@MOD+k */
		if (ws(me, q) < q) {
			int64_t k;
			const char *y = me;

			if (abs_expr(a, &y, q, &k))
				return -1;
			v->c += k;
		}
		*pp = q;
		return 0;
	}
	if (val_expr(a, &p, q, v))
		return -1;
	*pp = p;
	return 0;
}

static int x64_operand(As *a, const char **pp, const char *e, XOp *o)
{
	const char *p = ws(*pp, e);

	memset(o, 0, sizeof *o);
	o->base = o->index = -1;
	if (p < e && *p == '*') {
		o->star = 1;
		p = ws(p + 1, e);
	}
	if (p < e && *p == '$') {
		o->kind = XK_IMM;
		p++;
		if (x64_expr(a, &p, e, &o->v, &o->mod))
			return -1;
		o->hasv = 1;
		*pp = p;
		return 0;
	}
	if (p < e && *p == '%') {
		XOp r;
		const char *q = x64_reg(a, p + 1, e, &r);

		if (!q)
			return -1;
		if (r.cls == XR_SEG && ws(q, e) < e && *ws(q, e) == ':') {
			o->seg = r.reg;
			p = ws(ws(q, e) + 1, e);
			goto mem;
		}
		{
			int star = o->star;

			*o = r;
			o->star = (uint8_t)star;
		}
		o->kind = XK_REG;
		o->base = o->index = -1;
		*pp = q;
		return 0;
	}
mem:
	if (p < e && *p != '(') {
		if (x64_expr(a, &p, e, &o->v, &o->mod))
			return -1;
		o->hasv = 1;
	}
	p = ws(p, e);
	if (p < e && *p == '(') {
		XOp r;

		o->kind = XK_MEM;
		p = ws(p + 1, e);
		if (p < e && *p == '%') {
			const char *q = x64_reg(a, p + 1, e, &r);

			if (!q)
				return -1;
			o->base = (int8_t)(r.cls == XR_RIP ? 16 : r.reg);
			if (r.cls != XR_RIP && (r.cls != XR_GPR || r.size != 8))
				return err(a, "invalid base register");
			p = ws(q, e);
		}
		if (p < e && *p == ',') {
			p = ws(p + 1, e);
			if (p < e && *p == '%') {
				const char *q = x64_reg(a, p + 1, e, &r);

				if (!q)
					return -1;
				if (r.cls != XR_GPR || r.size != 8 || r.reg == 4)
					return err(a, "invalid index register");
				o->index = (int8_t)r.reg;
				p = ws(q, e);
			}
			o->scale = 1;
			if (p < e && *p == ',') {
				int64_t s;

				p = ws(p + 1, e);
				if (abs_expr(a, &p, e, &s))
					return -1;
				if (s != 1 && s != 2 && s != 4 && s != 8)
					return err(a, "scale factor in address must be 1, 2, 4 or 8");
				o->scale = (uint8_t)s;
				p = ws(p, e);
			}
		}
		if (p >= e || *p != ')')
			return err(a, "unexpected token in memory operand");
		p++;
	} else {
		o->kind = o->seg ? XK_MEM : XK_EXPR;	/* a bare expression: a branch target or an absolute address */
	}
	*pp = p;
	return 0;
}

/* ---------------------------------------------------------------- encoding */

typedef struct {
	uint8_t b[24];
	int n;
	int fixat, fixtype, fixsize;	/* one fixup (the displacement or the immediate) */
	Val fixv;
	int fix2at, fix2type;	/* a second one: an immediate symbol after a displacement */
	Val fix2v;
	int x87;		/* (unused) */
} XEnc;

static _Thread_local int x64_lock;	/* "lock insn" on one line: after 0x66, as MC orders them */

static void eb(XEnc *x, int v) { x->b[x->n++] = (uint8_t)v; }
static void ew(XEnc *x, int64_t v, int n) { for (int i = 0; i < n; i++) eb(x, (int)((uint64_t)v >> 8 * i) & 0xff); }

static int fits8(int64_t v) { return v >= -128 && v <= 127; }
static int fits32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

/* a constant Val (no symbol): its value */
static int isconst(As *a, Val *v, int64_t *c)
{
	fold(a, v);
	if (v->add || v->sub)
		return 0;
	*c = v->c;
	return 1;
}

/* the relocation type of a displacement or immediate with modifier mod */
static int x64_rtype(int mod, int pcrel, int size, int sgn, int rexw, int gotx)
{
	switch (mod) {
	case XM_PLT: return R_X86_64_PLT32;
	case XM_GOTPCREL: return gotx == 2 ? (rexw ? R_X86_64_REX_GOTPCRELX : R_X86_64_GOTPCRELX) : R_X86_64_GOTPCREL;
	case XM_TPOFF: return size == 8 ? R_X86_64_TPOFF64 : R_X86_64_TPOFF32;
	case XM_GOTTPOFF: return R_X86_64_GOTTPOFF;
	case XM_TLSGD: return R_X86_64_TLSGD;
	case XM_TLSLD: return R_X86_64_TLSLD;
	case XM_DTPOFF: return size == 8 ? R_X86_64_DTPOFF64 : R_X86_64_DTPOFF32;
	case XM_GOTOFF: return R_X86_64_GOTOFF64;
	case XM_GOT: return R_X86_64_GOT32;
	}
	if (pcrel)
		return size == 8 ? R_X86_64_PC64 : size == 2 ? R_X86_64_PC16 : size == 1 ? R_X86_64_PC8 : R_X86_64_PC32;
	return size == 8 ? R_X86_64_64 : size == 2 ? R_X86_64_16 : size == 1 ? R_X86_64_8 : sgn ? R_X86_64_32S : R_X86_64_32;
}

/*
 * prefixes, REX, the opcode bytes, ModRM (+ SIB, displacement) for reg field
 * `reg` (a register number or /digit) and the r/m operand rm, then an
 * immediate of isz bytes.  w: REX.W; opsz16: 0x66.  gotx: 2 for mov-like
 * instructions whose GOTPCREL relaxes (REX_)GOTPCRELX, 1 for others that do.
 */
typedef struct {
	int w, opsz16, rep;		/* rep: a mandatory F2/F3 prefix */
	const uint8_t *op; int nop;
	int reg;			/* ModRM.reg (0..15) */
	const XOp *rm;			/* register or memory */
	int rex8;			/* an 8-bit register operand that needs REX (spl ...) */
	int64_t imm; int isz; Val immv; int immsym, immsgn, immmod;
	int gotx;
	int oreg;			/* a register in the last opcode byte, + 1 (0: none) */
} XForm;

static int x64_emit(As *a, XForm *f)
{
	XEnc x = { 0 };
	const XOp *rm = f->rm;
	int rex = 0x40 | (f->w ? 8 : 0) | (f->reg & 8 ? 4 : 0) | (f->oreg && (f->oreg - 1) & 8 ? 1 : 0);
	int seg = rm && rm->kind != XK_REG ? rm->seg : 0;
	uint8_t modrm[8];
	int nm = 0, dispsz = 0, ripfix = 0;
	int64_t disp = 0;
	Val dv = { 0, 0, 0 };
	int dsym = 0;

	if (rm && rm->kind == XK_REG) {
		if (rm->reg & 8) rex |= 1;
		modrm[nm++] = (uint8_t)(0xc0 | (f->reg & 7) << 3 | (rm->reg & 7));
	} else if (rm) {
		int base = rm->base, index = rm->index;

		if (rm->hasv) {
			dv = rm->v;
			dsym = !isconst(a, &dv, &disp);
		}
		if (base == 16) {	/* rip-relative */
			modrm[nm++] = (uint8_t)((f->reg & 7) << 3 | 5);
			dispsz = 4, ripfix = 1;
		} else if (base < 0 && index < 0) {	/* absolute: SIB with no base, no index */
			modrm[nm++] = (uint8_t)((f->reg & 7) << 3 | 4);
			modrm[nm++] = 0x25;
			dispsz = 4;
		} else {
			int md;

			if (base >= 0 && base & 8) rex |= 1;
			if (index >= 0 && index & 8) rex |= 2;
			md = dsym ? 2 : (base < 0 ? 0 : disp == 0 && (base & 7) != 5 ? 0 : fits8(disp) ? 1 : 2);
			if (base < 0) {
				/* no base: SIB base 101 with mod 00 means disp32 */
				modrm[nm++] = (uint8_t)((f->reg & 7) << 3 | 4);
				modrm[nm++] = (uint8_t)((rm->scale == 8 ? 3 : rm->scale == 4 ? 2 : rm->scale == 2 ? 1 : 0) << 6 | (index & 7) << 3 | 5);
				dispsz = 4;
			} else if (index >= 0 || (base & 7) == 4) {
				int sc = index >= 0 ? rm->scale : 1;

				modrm[nm++] = (uint8_t)(md << 6 | (f->reg & 7) << 3 | 4);
				modrm[nm++] = (uint8_t)((sc == 8 ? 3 : sc == 4 ? 2 : sc == 2 ? 1 : 0) << 6 | (index >= 0 ? index & 7 : 4) << 3 | (base & 7));
				dispsz = md == 1 ? 1 : md == 2 ? 4 : 0;
			} else {
				modrm[nm++] = (uint8_t)(md << 6 | (f->reg & 7) << 3 | (base & 7));
				dispsz = md == 1 ? 1 : md == 2 ? 4 : 0;
			}
		}
	}
	if (f->rex8)
		rex |= 0x40;
	if (seg)
		eb(&x, seg);
	if (f->opsz16)
		eb(&x, 0x66);
	if (x64_lock) {
		eb(&x, 0xf0);
		x64_lock = 0;
	}
	if (f->rep)
		eb(&x, f->rep);
	if (rex != 0x40 || f->rex8)
		eb(&x, rex);
	for (int i = 0; i < f->nop; i++)
		eb(&x, f->op[i] + (f->oreg && i == f->nop - 1 ? (f->oreg - 1) & 7 : 0));
	for (int i = 0; i < nm; i++)
		eb(&x, modrm[i]);
	if (dispsz) {
		if (dsym || ripfix) {
			int rt = x64_rtype(rm->mod, ripfix, 4, 1, f->w, f->gotx);

			x.fixat = x.n, x.fixtype = rt, x.fixsize = 4;
			x.fixv = dv;
			if (ripfix && !dsym && !rm->hasv)
				x.fixv = (Val){ 0, 0, 0 };
			if (ripfix)
				x.fixv.c -= 4 + f->isz;	/* from the end of the instruction */
			if (ripfix && !dsym) {
				/* a constant rip displacement: no fixup */
				x.fixtype = 0;
				ew(&x, disp, 4);
			} else {
				ew(&x, 0, 4);
			}
		} else {
			ew(&x, disp, dispsz);
		}
	}
	if (f->isz) {
		if (f->immsym) {
			x.fix2at = x.n, x.fix2type = x64_rtype(f->immmod, 0, f->isz, f->immsgn, f->w, 0);
			x.fix2v = f->immv;
			ew(&x, 0, f->isz);
		} else {
			ew(&x, f->imm, f->isz);
		}
	}
	{
		uint32_t fr = a->cur;
		uint64_t off;
		uint8_t *p;

		elf_code(a->o, fr);
		off = elf_frag(a->o, fr)->size;
		p = elf_grow(a->o, fr, (size_t)x.n);
		memcpy(p, x.b, (size_t)x.n);
		if (x.fixtype)
			elf_fixup(a->o, fr, off + (uint64_t)x.fixat, p + x.fixat, (uint32_t)x.fixtype, x.fixv);
		if (x.fix2type)
			elf_fixup(a->o, fr, off + (uint64_t)x.fix2at, p + x.fix2at, (uint32_t)x.fix2type, x.fix2v);
	}
	return 0;
}

/* raw bytes (no operands) */
static int x64_raw(As *a, const uint8_t *b, int n)
{
	uint32_t fr = a->cur;

	elf_code(a->o, fr);
	memcpy(elf_grow(a->o, fr, (size_t)n), b, (size_t)n);
	return 0;
}

/* ---------------------------------------------------------------- mnemonics */

static const char *const ccn[16][3] = {
	{ "o" }, { "no" }, { "b", "c", "nae" }, { "ae", "nb", "nc" }, { "e", "z" }, { "ne", "nz" }, { "be", "na" }, { "a", "nbe" },
	{ "s" }, { "ns" }, { "p", "pe" }, { "np", "po" }, { "l", "nge" }, { "ge", "nl" }, { "le", "ng" }, { "g", "nle" },
};

/* the condition a suffix names, -1 for none */
static int x64_cc(const char *s)
{
	for (int i = 0; i < 16; i++)
		for (int k = 0; k < 3 && ccn[i][k]; k++)
			if (!strcmp(s, ccn[i][k]))
				return i;
	return -1;
}

static int sfxsize(char c) { return c == 'b' ? 1 : c == 'w' ? 2 : c == 'l' ? 4 : c == 'q' ? 8 : 0; }

/* an operand's size from its register, 0 for none */
static int opsize(const XOp *o) { return o->kind == XK_REG && o->cls == XR_GPR ? o->size : 0; }

static _Thread_local int x64_isz;	/* the operation's size: a constant immediate is taken at it (MC) */

static int x64_imm(As *a, const XOp *o, XForm *f, int64_t *k)
{
	Val v = o->v;

	if (isconst(a, &v, k) && !o->mod) {
		if (x64_isz == 1)
			*k = (int8_t)*k;
		else if (x64_isz == 2)
			*k = (int16_t)*k;
		else if (x64_isz == 4)
			*k = (int32_t)*k;
		return 0;
	}
	f->immsym = 1;
	f->immv = o->v;
	f->immmod = o->mod;
	return 1;
}

/* the size of a two-operand instruction: its suffix, else a register operand's */
static int x64_size(As *a, int sfx, const XOp *s, const XOp *d)
{
	int sz = sfx ? sfx : opsize(d) ? opsize(d) : opsize(s);

	if (!sz)
		err(a, "cannot infer the operand size");
	return sz;
}

static int rex8(const XOp *o) { return o->kind == XK_REG && o->cls == XR_GPR && o->size == 1 && o->needrex; }

#define OPB(...) ((const uint8_t[]){ __VA_ARGS__ })
#define F(...) ((XForm){ __VA_ARGS__ })

/* a mnemonic stem and its size suffix: "addq" -> "add", 8 (tried when the full name is unknown) */
static int split_sfx(const char *mn, char *stem, int *sz)
{
	size_t n = strlen(mn);

	if (n < 2 || !sfxsize(mn[n - 1]))
		return 0;
	memcpy(stem, mn, n - 1);
	stem[n - 1] = 0;
	*sz = sfxsize(mn[n - 1]);
	return 1;
}

static int x64_alu(As *a, int n, int sz, XOp *o, int nops)
{
	XOp *s = &o[0], *d = &o[1];
	int w = sz == 8, p16 = sz == 2, b = sz == 1;

	x64_isz = sz;
	if (nops != 2)
		return err(a, "invalid operand count");
	if (s->kind == XK_IMM) {
		int64_t k = 0;
		XForm f = F(.w = w, .opsz16 = p16, .reg = n, .rm = d, .rex8 = rex8(d), .immsgn = 1);
		int sym = x64_imm(a, s, &f, &k);

		if (!b && !sym && fits8(k)) {
			f.op = OPB(0x83), f.nop = 1, f.isz = 1, f.imm = k;
		} else if (d->kind == XK_REG && d->reg == 0 && !d->hi8) {
			/* the accumulator's short form */
			XForm g = F(.w = w, .opsz16 = p16, .op = (const uint8_t[]){ (uint8_t)(n * 8 + (b ? 4 : 5)) }, .nop = 1,
				    .isz = b ? 1 : sz == 2 ? 2 : 4, .imm = k, .immsym = f.immsym, .immv = f.immv, .immmod = f.immmod, .immsgn = 1);
			uint8_t op = (uint8_t)(n * 8 + (b ? 4 : 5));

			g.op = &op;
			return x64_emit(a, &g);
		} else {
			f.op = b ? OPB(0x80) : OPB(0x81), f.nop = 1, f.isz = b ? 1 : sz == 2 ? 2 : 4, f.imm = k;
		}
		return x64_emit(a, &f);
	}
	if (s->kind == XK_REG) {
		uint8_t op = (uint8_t)(n * 8 + (b ? 0 : 1));

		return x64_emit(a, &F(.w = w, .opsz16 = p16, .op = &op, .nop = 1, .reg = s->reg, .rm = d, .rex8 = rex8(s) || rex8(d)));
	}
	if (d->kind == XK_REG) {
		uint8_t op = (uint8_t)(n * 8 + (b ? 2 : 3));

		/* (a load form relaxes GOTPCRELX, MC's list: adc add and cmp or sbb sub xor) */
		return x64_emit(a, &F(.w = w, .opsz16 = p16, .op = &op, .nop = 1, .reg = d->reg, .rm = s, .rex8 = rex8(d), .gotx = 2));
	}
	return err(a, "invalid operand for instruction");
}

/* a jump or call to a target expression: jumps relax (as.c), calls are rel32 */
static int x64_branch(As *a, int cc, XOp *t, int call)
{
	if (call) {
		XEnc x = { 0 };
		uint32_t fr = a->cur;
		uint64_t off;
		uint8_t *p;
		Val v = t->v;

		eb(&x, 0xe8);
		ew(&x, 0, 4);
		elf_code(a->o, fr);
		off = elf_frag(a->o, fr)->size;
		p = elf_grow(a->o, fr, 5);
		memcpy(p, x.b, 5);
		v.c -= 4;
		elf_fixup(a->o, fr, off + 1, p + 1, R_X86_64_PLT32, v);
		if (t->mod == XM_PLT)
			((ElfFix *)a->o->fixes.base)[a->o->nfix - 1].pad = 1;	/* sym@PLT: never resolved in place (MC) */
		return 0;
	}
	return as_branch(a, cc, t->v, t->mod);
}

/* SSE: [prefix] [REX] 0f op ModRM */
static int x64_sse(As *a, int pfx, int op, int w, int reg, const XOp *rm)
{
	uint8_t ops[2] = { 0x0f, (uint8_t)op };

	return x64_emit(a, &F(.rep = pfx == 0x66 ? 0 : pfx, .opsz16 = pfx == 0x66, .w = w, .op = ops, .nop = 2, .reg = reg, .rm = rm));
}

/* x87 with a memory operand: op /digit */
static int x64_x87m(As *a, int op, int digit, const XOp *m)
{
	uint8_t b = (uint8_t)op;

	return x64_emit(a, &F(.op = &b, .nop = 1, .reg = digit, .rm = m));
}

static int x64_x87r(As *a, int op, int base, int i)
{
	uint8_t b[2] = { (uint8_t)op, (uint8_t)(base + i) };

	return x64_raw(a, b, 2);
}

VPRIV int x64_instruction(As *a, const char *p, const char *e, size_t wn)
{
	char mn[32], stem[32];
	XOp o[4];
	int nops = 0, sz = 0, cc;

	if (wn >= sizeof mn)
		return err(a, "invalid instruction mnemonic");
	x64_isz = 0;
	for (size_t i = 0; i < wn; i++)
		mn[i] = (char)c_lower((unsigned char)p[i]);
	mn[wn] = 0;
	p = ws(p + wn, e);
	/* prefixes written as their own word: lock, rep, data16, rex64 */
	if (!strcmp(mn, "lock") || !strcmp(mn, "rep") || !strcmp(mn, "repz") || !strcmp(mn, "repe") || !strcmp(mn, "repnz") ||
	    !strcmp(mn, "repne") || !strcmp(mn, "data16") || !strcmp(mn, "rex64") || !strcmp(mn, "cs") || !strcmp(mn, "notrack")) {
		uint8_t b = !strcmp(mn, "lock") ? 0xf0 : !strcmp(mn, "data16") ? 0x66 : !strcmp(mn, "rex64") ? 0x48 :
			    !strcmp(mn, "cs") ? 0x2e : !strcmp(mn, "notrack") ? 0x3e : mn[3] == 'n' ? 0xf2 : 0xf3;

		if (p < e && b == 0xf0) {
			const char *q = p;
			int r;

			while (q < e && isidc((unsigned char)*q))
				q++;
			x64_lock = 1;
			r = x64_instruction(a, p, e, (size_t)(q - p));
			if (x64_lock) {	/* (an instruction without x64_emit: the prefix first) */
				x64_lock = 0;
			}
			return r;
		}
		if (x64_raw(a, &b, 1))
			return -1;
		if (p < e) {
			const char *q = p;

			while (q < e && isidc((unsigned char)*q))
				q++;
			return x64_instruction(a, p, e, (size_t)(q - p));
		}
		return 0;
	}
	while (p < e) {
		if (nops == 4)
			return err(a, "too many operands");
		if (x64_operand(a, &p, e, &o[nops]))
			return -1;
		nops++;
		p = ws(p, e);
		if (p < e && *p == ',')
			p = ws(p + 1, e);
		else if (p < e)
			return err(a, "unexpected token in argument list");
	}
	/* string instructions with their implicit operands written out: as without */
	if (nops && (!strncmp(mn, "movs", 4) || !strncmp(mn, "stos", 4) || !strncmp(mn, "lods", 4) || !strncmp(mn, "cmps", 4) ||
		     !strncmp(mn, "scas", 4)) && strlen(mn) == 5 && sfxsize(mn[4])) {
		int implicit = 1;

		for (int i = 0; i < nops; i++)
			if (o[i].kind != XK_MEM && !(o[i].kind == XK_REG && o[i].reg == 0))
				implicit = 0;
		if (implicit) {
			int z = sfxsize(mn[4]);
			uint8_t base = !strncmp(mn, "movs", 4) ? 0xa4 : !strncmp(mn, "stos", 4) ? 0xaa : !strncmp(mn, "lods", 4) ? 0xac :
				       !strncmp(mn, "cmps", 4) ? 0xa6 : 0xae;
			uint8_t b[2] = { 0x48, (uint8_t)(base + (z != 1)) };

			if (z == 2)
				b[0] = 0x66;
			return z == 8 || z == 2 ? x64_raw(a, b, 2) : x64_raw(a, b + 1, 1);
		}
	}
	/* ---- no operands ---- */
	{
		static const struct { const char *n; uint8_t b[4]; int k; } zero[] = {
			{ "retq", { 0xc3 }, 1 }, { "ret", { 0xc3 }, 1 }, { "leave", { 0xc9 }, 1 }, { "leaveq", { 0xc9 }, 1 },
			{ "cltd", { 0x99 }, 1 }, { "cqto", { 0x48, 0x99 }, 2 }, { "cltq", { 0x48, 0x98 }, 2 }, { "cwtl", { 0x98 }, 1 },
			{ "cwtd", { 0x66, 0x99 }, 2 }, { "nop", { 0x90 }, 1 }, { "ud2", { 0x0f, 0x0b }, 2 }, { "hlt", { 0xf4 }, 1 },
			{ "mfence", { 0x0f, 0xae, 0xf0 }, 3 }, { "lfence", { 0x0f, 0xae, 0xe8 }, 3 }, { "sfence", { 0x0f, 0xae, 0xf8 }, 3 },
			{ "pause", { 0xf3, 0x90 }, 2 }, { "int3", { 0xcc }, 1 }, { "cpuid", { 0x0f, 0xa2 }, 2 }, { "rdtsc", { 0x0f, 0x31 }, 2 },
			{ "movsb", { 0xa4 }, 1 }, { "stosb", { 0xaa }, 1 }, { "movsq", { 0x48, 0xa5 }, 2 }, { "stosq", { 0x48, 0xab }, 2 },
			{ "movsl", { 0xa5 }, 1 }, { "stosl", { 0xab }, 1 }, { "cld", { 0xfc }, 1 }, { "std", { 0xfd }, 1 },
			{ "fchs", { 0xd9, 0xe0 }, 2 }, { "fabs", { 0xd9, 0xe1 }, 2 }, { "fldz", { 0xd9, 0xee }, 2 }, { "fld1", { 0xd9, 0xe8 }, 2 },
			{ "fsqrt", { 0xd9, 0xfa }, 2 }, { "fpatan", { 0xd9, 0xf3 }, 2 }, { "fxch", { 0xd9, 0xc9 }, 2 }, { "fucompp", { 0xda, 0xe9 }, 2 },
			{ "faddp", { 0xde, 0xc1 }, 2 }, { "fmulp", { 0xde, 0xc9 }, 2 }, { "fsubrp", { 0xde, 0xe9 }, 2 }, { "fsubp", { 0xde, 0xe1 }, 2 },
			{ "fdivrp", { 0xde, 0xf9 }, 2 }, { "fdivp", { 0xde, 0xf1 }, 2 }, { "fwait", { 0x9b }, 1 }, { "wait", { 0x9b }, 1 },
			{ "endbr64", { 0xf3, 0x0f, 0x1e, 0xfa }, 4 }, { "syscall", { 0x0f, 0x05 }, 2 }, { "sahf", { 0x9e }, 1 }, { "lahf", { 0x9f }, 1 },
			{ "fninit", { 0xdb, 0xe3 }, 2 }, { "frndint", { 0xd9, 0xfc }, 2 }, { "fprem", { 0xd9, 0xf8 }, 2 }, { "fscale", { 0xd9, 0xfd }, 2 },
		};

		if (!nops)
			for (size_t i = 0; i < sizeof zero / sizeof *zero; i++)
				if (!strcmp(mn, zero[i].n))
					return x64_raw(a, zero[i].b, zero[i].k);
	}
	/* ---- jumps, calls ---- */
	if (mn[0] == 'j' && nops == 1) {
		const char *c = mn + 1;

		if (!strcmp(mn, "jmp") || !strcmp(mn, "jmpq")) {
			if (o[0].star || o[0].kind == XK_REG || o[0].kind == XK_MEM) {
				o[0].kind = o[0].kind == XK_EXPR ? XK_MEM : o[0].kind;
				return x64_emit(a, &F(.op = OPB(0xff), .nop = 1, .reg = 4, .rm = &o[0], .gotx = 2));
			}
			return x64_branch(a, 16, &o[0], 0);
		}
		if ((cc = x64_cc(c)) >= 0)
			return x64_branch(a, cc, &o[0], 0);
	}
	if ((!strcmp(mn, "call") || !strcmp(mn, "callq")) && nops == 1) {
		if (o[0].star || o[0].kind == XK_REG || o[0].kind == XK_MEM) {
			o[0].kind = o[0].kind == XK_EXPR ? XK_MEM : o[0].kind;
			return x64_emit(a, &F(.op = OPB(0xff), .nop = 1, .reg = 2, .rm = &o[0], .gotx = 2));
		}
		return x64_branch(a, -1, &o[0], 1);
	}
	/* ---- setcc, cmovcc ---- */
	if (!strncmp(mn, "set", 3) && nops == 1 && (cc = x64_cc(mn + 3)) >= 0) {
		uint8_t op[2] = { 0x0f, (uint8_t)(0x90 + cc) };

		return x64_emit(a, &F(.op = op, .nop = 2, .reg = 0, .rm = &o[0], .rex8 = rex8(&o[0])));
	}
	if (!strncmp(mn, "cmov", 4) && nops == 2) {
		char t[16];
		size_t n = strlen(mn + 4);
		int s2 = 0;

		memcpy(t, mn + 4, n + 1);
		if ((cc = x64_cc(t)) < 0 && n > 1 && sfxsize(t[n - 1])) {
			s2 = sfxsize(t[n - 1]);
			t[n - 1] = 0;
			cc = x64_cc(t);
		}
		if (cc >= 0) {
			uint8_t op[2] = { 0x0f, (uint8_t)(0x40 + cc) };
			int s = s2 ? s2 : opsize(&o[1]);

			return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = op, .nop = 2, .reg = o[1].reg, .rm = &o[0]));
		}
	}
	/* ---- x87 ---- */
	if (mn[0] == 'f') {
		static const struct { const char *n; uint8_t op, d; } fm[] = {
			{ "fldt", 0xdb, 5 }, { "fstpt", 0xdb, 7 }, { "fldl", 0xdd, 0 }, { "flds", 0xd9, 0 }, { "fstpl", 0xdd, 3 },
			{ "fstps", 0xd9, 3 }, { "fstl", 0xdd, 2 }, { "fsts", 0xd9, 2 }, { "fildll", 0xdf, 5 }, { "fildq", 0xdf, 5 },
			{ "fildl", 0xdb, 0 }, { "filds", 0xdf, 0 }, { "fistpll", 0xdf, 7 }, { "fistpq", 0xdf, 7 }, { "fistpl", 0xdb, 3 },
			{ "fistps", 0xdf, 3 }, { "fnstcw", 0xd9, 7 }, { "fldcw", 0xd9, 5 }, { "fisttpll", 0xdd, 1 }, { "fadds", 0xd8, 0 },
			{ "faddl", 0xdc, 0 }, { "fmuls", 0xd8, 1 }, { "fmull", 0xdc, 1 }, { "fsubs", 0xd8, 4 }, { "fsubl", 0xdc, 4 },
			{ "fdivs", 0xd8, 6 }, { "fdivl", 0xdc, 6 }, { "fnstsw", 0xdd, 7 },
		};

		if (nops == 1 && o[0].kind != XK_REG) {
			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			for (size_t i = 0; i < sizeof fm / sizeof *fm; i++)
				if (!strcmp(mn, fm[i].n))
					return x64_x87m(a, fm[i].op, fm[i].d, &o[0]);
		}
		if (o[0].kind == XK_REG && o[0].cls == XR_ST) {
			int i = o[0].reg;

			if (!strcmp(mn, "fstp") && nops == 1) return x64_x87r(a, 0xdd, 0xd8, i);
			if (!strcmp(mn, "fst") && nops == 1) return x64_x87r(a, 0xdd, 0xd0, i);
			if (!strcmp(mn, "fld") && nops == 1) return x64_x87r(a, 0xd9, 0xc0, i);
			if (!strcmp(mn, "fxch") && nops == 1) return x64_x87r(a, 0xd9, 0xc8, i);
			if (!strcmp(mn, "fucomip") || !strcmp(mn, "fucomi") || !strcmp(mn, "fcomip") || !strcmp(mn, "fcomi") ||
			    !strcmp(mn, "fucompi") || !strcmp(mn, "fcompi")) {
				int u = mn[1] == 'u', pop = strchr(mn + 4, 'p') != NULL;

				return x64_x87r(a, pop ? 0xdf : 0xdb, u ? 0xe8 : 0xf0, i);
			}
			if (nops == 2 && o[1].kind == XK_REG && o[1].cls == XR_ST) {
				/* faddp %st, %st(i) ... (AT&T keeps the swapped fsub/fdiv names) */
				static const struct { const char *n; uint8_t op, base; } fp2[] = {
					{ "faddp", 0xde, 0xc0 }, { "fmulp", 0xde, 0xc8 }, { "fsubp", 0xde, 0xe0 }, { "fsubrp", 0xde, 0xe8 },
					{ "fdivp", 0xde, 0xf0 }, { "fdivrp", 0xde, 0xf8 },
				};

				for (size_t k = 0; k < sizeof fp2 / sizeof *fp2; k++)
					if (!strcmp(mn, fp2[k].n))
						return x64_x87r(a, fp2[k].op, fp2[k].base, o[1].reg);
			}
		}
	}
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
	/* ---- shld/shrd, bt*, prefetch ---- */
	{
		char st2[32];
		int s2 = 0;

		if (!split_sfx(mn, st2, &s2))
			strcpy(st2, mn);
		if ((!strcmp(st2, "shld") || !strcmp(st2, "shrd")) && nops == 3) {
			int z = s2 ? s2 : opsize(&o[2]), r = st2[2] == 'r';
			XOp *d = &o[2];

			if (d->kind == XK_EXPR)
				d->kind = XK_MEM;
			if (o[0].kind == XK_REG && o[0].reg == 1 && o[0].size == 1) {
				uint8_t ops[2] = { 0x0f, (uint8_t)(r ? 0xad : 0xa5) };

				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = ops, .nop = 2, .reg = o[1].reg, .rm = d));
			}
			{
				int64_t c = 0;
				uint8_t ops[2] = { 0x0f, (uint8_t)(r ? 0xac : 0xa4) };
				XForm f = F(.w = z == 8, .opsz16 = z == 2, .op = ops, .nop = 2, .reg = o[1].reg, .rm = d, .isz = 1);

				x64_imm(a, &o[0], &f, &c);
				f.imm = c;
				return x64_emit(a, &f);
			}
		}
		{
			static const char *const btn[4] = { "bt", "bts", "btr", "btc" };

			for (int k = 0; k < 4; k++)
				if (!strcmp(st2, btn[k]) && nops == 2) {
					int z = s2 ? s2 : opsize(&o[1]) ? opsize(&o[1]) : opsize(&o[0]);

					if (o[1].kind == XK_EXPR)
						o[1].kind = XK_MEM;
					if (o[0].kind == XK_IMM) {
						int64_t c = 0;
						XForm f = F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x0f, 0xba), .nop = 2, .reg = 4 + k, .rm = &o[1], .isz = 1);

						x64_imm(a, &o[0], &f, &c);
						f.imm = c;
						return x64_emit(a, &f);
					}
					{
						uint8_t ops[2] = { 0x0f, (uint8_t)(0xa3 + 8 * k) };

						return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = ops, .nop = 2, .reg = o[0].reg, .rm = &o[1]));
					}
				}
		}
		if (!strncmp(mn, "prefetch", 8) && nops == 1) {
			static const struct { const char *n; uint8_t op, d; } pf[] = {
				{ "prefetchnta", 0x18, 0 }, { "prefetcht0", 0x18, 1 }, { "prefetcht1", 0x18, 2 }, { "prefetcht2", 0x18, 3 },
				{ "prefetchw", 0x0d, 1 }, { "prefetch", 0x0d, 0 },
			};

			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			for (size_t i = 0; i < sizeof pf / sizeof *pf; i++)
				if (!strcmp(mn, pf[i].n)) {
					uint8_t ops[2] = { 0x0f, pf[i].op };

					return x64_emit(a, &F(.op = ops, .nop = 2, .reg = pf[i].d, .rm = &o[0]));
				}
		}
	}
	/* ---- x87 arithmetic with st registers and integer memory, fcmov ---- */
	if (mn[0] == 'f') {
		static const char *const ar[4] = { "fadd", "fmul", "fsub", "fdiv" };
		static const uint8_t base0[4] = { 0xc0, 0xc8, 0xe0, 0xf0 };	/* op %st(i), %st */

		for (int k = 0; k < 4; k++) {
			size_t l = strlen(ar[k]);

			if (!strncmp(mn, ar[k], l) && mn[l] == 'r' && !mn[l + 1] && nops == 2 && o[0].cls == XR_ST && o[1].cls == XR_ST) {
				/* fsubr/fdivr %st(i), %st and %st, %st(i) */
				if (o[1].reg == 0)
					return x64_x87r(a, 0xd8, base0[k] + 8, o[0].reg);
				return x64_x87r(a, 0xdc, base0[k] + 8, o[1].reg);
			}
			if (!strcmp(mn, ar[k]) && nops == 2 && o[0].kind == XK_REG && o[0].cls == XR_ST && o[1].cls == XR_ST) {
				if (o[1].reg == 0)
					return x64_x87r(a, 0xd8, base0[k], o[0].reg);
				return x64_x87r(a, 0xdc, base0[k], o[1].reg);
			}
			if (!strcmp(mn, ar[k]) && nops == 1 && o[0].kind == XK_REG && o[0].cls == XR_ST)
				return x64_x87r(a, 0xd8, base0[k], o[0].reg);
		}
		{
			static const struct { const char *n; uint8_t op, d; } im[] = {
				{ "fiaddl", 0xda, 0 }, { "fimull", 0xda, 1 }, { "fisubl", 0xda, 4 }, { "fisubrl", 0xda, 5 }, { "fidivl", 0xda, 6 },
				{ "fidivrl", 0xda, 7 }, { "fiadds", 0xde, 0 }, { "fimuls", 0xde, 1 }, { "fisubs", 0xde, 4 }, { "fidivs", 0xde, 6 },
				{ "fsubrs", 0xd8, 5 }, { "fsubrl", 0xdc, 5 }, { "fdivrs", 0xd8, 7 }, { "fdivrl", 0xdc, 7 },
				{ "fcoms", 0xd8, 2 }, { "fcoml", 0xdc, 2 }, { "fcomps", 0xd8, 3 }, { "fcompl", 0xdc, 3 },
				{ "fisttpl", 0xdb, 1 }, { "fisttps", 0xdf, 1 }, { "fnstenv", 0xd9, 6 }, { "fldenv", 0xd9, 4 },
			};

			if (nops == 1 && o[0].kind != XK_REG) {
				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				for (size_t i = 0; i < sizeof im / sizeof *im; i++)
					if (!strcmp(mn, im[i].n))
						return x64_x87m(a, im[i].op, im[i].d, &o[0]);
			}
		}
		if (!strncmp(mn, "fcmov", 5) && nops == 2 && o[0].cls == XR_ST) {
			static const struct { const char *n; uint8_t op, base; } cm[] = {
				{ "b", 0xda, 0xc0 }, { "e", 0xda, 0xc8 }, { "be", 0xda, 0xd0 }, { "u", 0xda, 0xd8 },
				{ "nb", 0xdb, 0xc0 }, { "ne", 0xdb, 0xc8 }, { "nbe", 0xdb, 0xd0 }, { "nu", 0xdb, 0xd8 },
			};

			for (size_t i = 0; i < sizeof cm / sizeof *cm; i++)
				if (!strcmp(mn + 5, cm[i].n))
					return x64_x87r(a, cm[i].op, cm[i].base, o[0].reg);
		}
	}
	/* ---- integer instructions: the full name, then stem + suffix ---- */
	{
		static const char *const alu[] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
		static const char *const sh[] = { "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar" };
		const char *m = mn;
		int k;

		for (int pass = 0; pass < 2; pass++) {
			if (pass) {
				if (!split_sfx(mn, stem, &sz))
					break;
				m = stem;
			}
			for (k = 0; k < 8; k++)
				if (!strcmp(m, alu[k])) {
					for (int i = 0; i < nops; i++)
						if (o[i].kind == XK_EXPR)
							o[i].kind = XK_MEM;
					return x64_alu(a, k, x64_size(a, sz, &o[0], nops > 1 ? &o[1] : &o[0]), o, nops);
				}
			for (k = 0; k < 8; k++)
				if (!strcmp(m, sh[k])) {
					XOp *d = &o[nops - 1];
					int s = x64_size(a, sz, d, d), digit = k == 6 ? 4 : k;

					if (d->kind == XK_EXPR)
						d->kind = XK_MEM;
					if (nops == 1)
						return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = s == 1 ? OPB(0xd0) : OPB(0xd1), .nop = 1,
								  .reg = digit, .rm = d, .rex8 = rex8(d)));
					if (o[0].kind == XK_REG && o[0].reg == 1 && o[0].size == 1)
						return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = s == 1 ? OPB(0xd2) : OPB(0xd3), .nop = 1,
								  .reg = digit, .rm = d, .rex8 = rex8(d)));
					{
						int64_t c;
						Val v = o[0].v;

						if (o[0].kind != XK_IMM || !isconst(a, &v, &c))
							return err(a, "invalid shift count");
						if (c == 1)
							return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = s == 1 ? OPB(0xd0) : OPB(0xd1), .nop = 1,
									  .reg = digit, .rm = d, .rex8 = rex8(d)));
						return x64_emit(a, &F(.w = s == 8, .opsz16 = s == 2, .op = s == 1 ? OPB(0xc0) : OPB(0xc1), .nop = 1,
								  .reg = digit, .rm = d, .rex8 = rex8(d), .isz = 1, .imm = c));
					}
				}
			if (!strcmp(m, "mov") && nops == 2) {
				XOp *s = &o[0], *d = &o[1];
				int z = x64_size(a, sz, s, d);

				for (int i = 0; i < 2; i++)
					if (o[i].kind == XK_EXPR)
						o[i].kind = XK_MEM;
				if (s->kind == XK_IMM) {
					int64_t c = 0;
					XForm f = F(.w = z == 8, .opsz16 = z == 2, .rex8 = rex8(d), .immsgn = 1);
					int sym = x64_imm(a, s, &f, &c);

					if (d->kind == XK_REG) {
						if (z == 8 && (sym || fits32(c))) {
							f.op = OPB(0xc7), f.nop = 1, f.reg = 0, f.rm = d, f.isz = 4, f.imm = c;
							return x64_emit(a, &f);
						}
						/* b0+r / b8+r with the whole immediate (movabs for 64 bits) */
						f.op = z == 1 ? OPB(0xb0) : OPB(0xb8), f.nop = 1, f.oreg = d->reg + 1;
						f.isz = z, f.imm = c, f.immsgn = 0;
						return x64_emit(a, &f);
					}
					f.op = z == 1 ? OPB(0xc6) : OPB(0xc7), f.nop = 1, f.reg = 0, f.rm = d, f.isz = z == 8 ? 4 : z, f.imm = c;
					return x64_emit(a, &f);
				}
				if (s->kind == XK_REG && s->cls == XR_GPR)
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0x88) : OPB(0x89), .nop = 1, .reg = s->reg,
							      .rm = d, .rex8 = rex8(s) || rex8(d)));
				if (d->kind == XK_REG)
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0x8a) : OPB(0x8b), .nop = 1, .reg = d->reg,
							      .rm = s, .rex8 = rex8(d), .gotx = 2));
			}
			if (!strcmp(m, "movabs") && nops == 2) {
				int64_t c = 0;
				XOp *d = &o[1];
				XForm f = F(.w = 1, .op = OPB(0xb8), .nop = 1, .oreg = d->reg + 1, .isz = 8);

				x64_imm(a, &o[0], &f, &c);
				f.imm = c;
				return x64_emit(a, &f);
			}
			if (!strcmp(m, "lea") && nops == 2) {
				int z = sz ? sz : opsize(&o[1]);

				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x8d), .nop = 1, .reg = o[1].reg, .rm = &o[0]));
			}
			if (!strcmp(m, "test") && nops == 2) {
				XOp *s = &o[0], *d = &o[1];
				int z = x64_size(a, sz, s, d);

				for (int i = 0; i < 2; i++)
					if (o[i].kind == XK_EXPR)
						o[i].kind = XK_MEM;
				if (s->kind == XK_IMM) {
					int64_t c = 0;
					XForm f = F(.w = z == 8, .opsz16 = z == 2, .rex8 = rex8(d), .immsgn = 1);

					x64_imm(a, s, &f, &c);
					f.imm = c, f.isz = z == 8 ? 4 : z;
					if (d->kind == XK_REG && d->reg == 0 && !d->hi8) {
						f.op = z == 1 ? OPB(0xa8) : OPB(0xa9), f.nop = 1;
						return x64_emit(a, &f);
					}
					f.op = z == 1 ? OPB(0xf6) : OPB(0xf7), f.nop = 1, f.reg = 0, f.rm = d;
					return x64_emit(a, &f);
				}
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0x84) : OPB(0x85), .nop = 1, .reg = s->reg,
						      .rm = d, .rex8 = rex8(s) || rex8(d), .gotx = 2));
			}
			{
				static const struct { const char *n; uint8_t d; } un[] = {
					{ "not", 2 }, { "neg", 3 }, { "mul", 4 }, { "div", 6 }, { "idiv", 7 },
				};

				for (size_t i = 0; i < sizeof un / sizeof *un; i++)
					if (!strcmp(m, un[i].n) && nops == 1) {
						int z = x64_size(a, sz, &o[0], &o[0]);

						if (o[0].kind == XK_EXPR)
							o[0].kind = XK_MEM;
						return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0xf6) : OPB(0xf7), .nop = 1,
								      .reg = un[i].d, .rm = &o[0], .rex8 = rex8(&o[0])));
					}
				if ((!strcmp(m, "inc") || !strcmp(m, "dec")) && nops == 1) {
					int z = x64_size(a, sz, &o[0], &o[0]);

					if (o[0].kind == XK_EXPR)
						o[0].kind = XK_MEM;
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0xfe) : OPB(0xff), .nop = 1,
							      .reg = m[0] == 'd', .rm = &o[0], .rex8 = rex8(&o[0])));
				}
			}
			if (!strcmp(m, "imul")) {
				int z = x64_size(a, sz, &o[0], &o[nops - 1]);

				x64_isz = z;
				for (int i = 0; i < nops; i++)
					if (o[i].kind == XK_EXPR)
						o[i].kind = XK_MEM;
				if (nops == 1)
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0xf6) : OPB(0xf7), .nop = 1, .reg = 5, .rm = &o[0]));
				if (nops == 2 && o[0].kind != XK_IMM)
					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x0f, 0xaf), .nop = 2, .reg = o[1].reg, .rm = &o[0]));
				{
					/* imul $k, src, dst (or imul $k, dst: src = dst) */
					const XOp *src = nops == 3 ? &o[1] : &o[1], *dst = nops == 3 ? &o[2] : &o[1];
					int64_t c = 0;
					XForm f = F(.w = z == 8, .opsz16 = z == 2, .reg = dst->reg, .rm = src, .immsgn = 1);
					int sym = x64_imm(a, &o[0], &f, &c);

					f.imm = c;
					if (!sym && fits8(c))
						f.op = OPB(0x6b), f.nop = 1, f.isz = 1;
					else
						f.op = OPB(0x69), f.nop = 1, f.isz = z == 2 ? 2 : 4;
					return x64_emit(a, &f);
				}
			}
			if ((!strcmp(m, "push") || !strcmp(m, "pop")) && nops == 1) {
				int pop = m[1] == 'o';

				if (o[0].kind == XK_REG)
					return x64_emit(a, &F(.op = pop ? OPB(0x58) : OPB(0x50), .nop = 1, .oreg = o[0].reg + 1));
				if (o[0].kind == XK_IMM && !pop) {
					int64_t c = 0;
					XForm f = F(.immsgn = 1);
					int sym = x64_imm(a, &o[0], &f, &c);

					f.imm = c;
					if (!sym && fits8(c))
						f.op = OPB(0x6a), f.nop = 1, f.isz = 1;
					else
						f.op = OPB(0x68), f.nop = 1, f.isz = 4;
					return x64_emit(a, &f);
				}
				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				return x64_emit(a, &F(.op = pop ? OPB(0x8f) : OPB(0xff), .nop = 1, .reg = pop ? 0 : 6, .rm = &o[0]));
			}
			if ((!strcmp(m, "bsf") || !strcmp(m, "bsr") || !strcmp(m, "popcnt") || !strcmp(m, "lzcnt") || !strcmp(m, "tzcnt")) && nops == 2) {
				int z = x64_size(a, sz, &o[0], &o[1]);
				uint8_t op = m[0] == 'p' ? 0xb8 : m[0] == 'l' ? 0xbd : m[0] == 't' ? 0xbc : m[2] == 'f' ? 0xbc : 0xbd;
				uint8_t ops[2] = { 0x0f, op };

				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .rep = m[0] == 'b' ? 0 : 0xf3, .op = ops, .nop = 2, .reg = o[1].reg, .rm = &o[0]));
			}
			if (!strcmp(m, "bswap") && nops == 1) {
				int z = x64_size(a, sz, &o[0], &o[0]);

				return x64_emit(a, &F(.w = z == 8, .op = OPB(0x0f, 0xc8), .nop = 2, .oreg = o[0].reg + 1));
			}
			if (!strcmp(m, "xchg") && nops == 2) {
				int z = x64_size(a, sz, &o[0], &o[1]);
				XOp *r = o[0].kind == XK_REG ? &o[0] : &o[1], *x = o[0].kind == XK_REG ? &o[1] : &o[0];

				if (x->kind == XK_EXPR)
					x->kind = XK_MEM;
				if (z != 1 && x->kind == XK_REG && (r->reg == 0 || x->reg == 0)) {
					/* the accumulator form 90+r */
					int other = r->reg == 0 ? x->reg : r->reg;

					return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = OPB(0x90), .nop = 1, .oreg = other + 1));
				}
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = z == 1 ? OPB(0x86) : OPB(0x87), .nop = 1, .reg = r->reg, .rm = x,
						      .rex8 = rex8(r) || rex8(x)));
			}
			if ((!strcmp(m, "cmpxchg") || !strcmp(m, "xadd")) && nops == 2) {
				int z = x64_size(a, sz, &o[0], &o[1]);
				uint8_t ops[2] = { 0x0f, (uint8_t)((m[1] == 'm' ? 0xb0 : 0xc0) + (z != 1)) };

				if (o[1].kind == XK_EXPR)
					o[1].kind = XK_MEM;
				return x64_emit(a, &F(.w = z == 8, .opsz16 = z == 2, .op = ops, .nop = 2, .reg = o[0].reg, .rm = &o[1], .rex8 = rex8(&o[0])));
			}
		}
	}
	/* ---- movz/movs: movzbl, movsbq, movslq ... ---- */
	if ((!strncmp(mn, "movz", 4) || !strncmp(mn, "movs", 4)) && nops == 2 && strlen(mn) == 6) {
		int from = sfxsize(mn[4]), to = sfxsize(mn[5]), sx = mn[3] == 's';

		if (from && to) {
			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			if (from == 4 && sx)
				return x64_emit(a, &F(.w = 1, .op = OPB(0x63), .nop = 1, .reg = o[1].reg, .rm = &o[0]));
			{
				uint8_t ops[2] = { 0x0f, (uint8_t)((sx ? 0xbe : 0xb6) + (from == 2)) };

				return x64_emit(a, &F(.w = to == 8, .opsz16 = to == 2, .op = ops, .nop = 2, .reg = o[1].reg, .rm = &o[0], .rex8 = rex8(&o[0])));
			}
		}
	}
	return err(a, "invalid instruction mnemonic '%s'", mn);
}
