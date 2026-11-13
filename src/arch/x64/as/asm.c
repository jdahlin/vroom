/*
 * x86-64 instructions in AT&T syntax: the parser and the encoder, choosing
 * the encodings LLVM MC chooses (clang -c), so an object is byte-identical
 * with clang's for the same text.  Jumps to labels are variable items the
 * assembler relaxes (as/variable-items.c: as_variable_item_branch_add).
 */
#include "x64as.h"

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
		b[i] = (char)as_char_to_lower((unsigned char)s[i]);
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
	if (b[0] == 'r' && as_char_is_digit((unsigned char)b[1])) {
		int r = atoi(b + 1);
		const char *t = b + 1;

		while (as_char_is_digit((unsigned char)*t))
			t++;
		if (r < 8 || r > 15)
			return -1;
		o->reg = (uint8_t)r;
		o->size = !*t ? 8 : !strcmp(t, "d") ? 4 : !strcmp(t, "w") ? 2 : !strcmp(t, "b") ? 1 : 0;
		return o->size ? 0 : -1;
	}
	if ((!strncmp(b, "xmm", 3) || !strncmp(b, "ymm", 3)) && as_char_is_digit((unsigned char)b[3])) {
		int r = atoi(b + 3);

		if (r > 15)
			return -1;
		o->cls = XR_XMM, o->reg = (uint8_t)r, o->size = b[0] == 'y' ? 32 : 16;
		return 0;
	}
	if (b[0] == 'k' && b[1] >= '0' && b[1] <= '7' && !b[2]) {
		o->cls = XR_MASK, o->reg = b[1] - '0', o->size = 8;
		return 0;
	}
	if (!strncmp(b, "zmm", 3) && as_char_is_digit((unsigned char)b[3])) {
		char *end;
		long r = strtol(b + 3, &end, 10);
		if (*end || r > 31) return -1;
		o->cls = XR_ZMM, o->reg = r, o->size = 64;
		return 0;
	}
	if (!strcmp(b, "rip")) { o->cls = XR_RIP; return 0; }
	if ((!strncmp(b, "cr", 2) || !strncmp(b, "db", 2) || !strncmp(b, "dr", 2)) && as_char_is_digit((unsigned char)b[2])) {
		int r = atoi(b + 2);
		const char *t = b + 2;

		while (as_char_is_digit((unsigned char)*t))
			t++;
		if (r > 15 || *t)
			return -1;
		o->cls = b[0] == 'c' ? XR_CR : XR_DR, o->reg = (uint8_t)r, o->size = 8;
		return 0;
	}
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
static const char *x64_reg(as_context *a, const char *p, const char *e, XOp *o)
{
	const char *q = p;

	while (q < e && (as_char_is_identifier((unsigned char)*q) || as_char_is_digit((unsigned char)*q)))
		q++;
	if (q == p || x64_regname(p, (size_t)(q - p), o)) {
		as_error_report(a, "invalid register name");
		return NULL;
	}
	if (o->cls == XR_ST) {
		const char *r = as_whitespace_skip(q, e);

		if (r < e && *r == '(') {
			r = as_whitespace_skip(r + 1, e);
			if (r >= e || !as_char_is_digit((unsigned char)*r)) { as_error_report(a, "invalid st register"); return NULL; }
			o->reg = (uint8_t)(*r++ - '0');
			r = as_whitespace_skip(r, e);
			if (r >= e || *r != ')') { as_error_report(a, "invalid st register"); return NULL; }
			q = r + 1;
		}
	}
	return q;
}

/* an expression with an optional @MOD: [p, e) up to a top-level ( or , */
static int x64_expr(as_context *a, const char **pp, const char *e, as_value *v, uint8_t *mod)
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
			const char *r = as_whitespace_skip(q + 1, e);

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

		while (me < q && as_char_is_identifier((unsigned char)*me))
			me++;
		for (size_t i = 0; i < sizeof mods / sizeof *mods; i++)
			if ((size_t)(me - m) == strlen(mods[i].n) && !memcmp(m, mods[i].n, (size_t)(me - m)))
				*mod = mods[i].m;
		if (*mod == XM_NONE)
			return as_error_report(a, "invalid variant '%.*s'", (int)(me - m), m);
		if (as_expression_value_parse(a, &x, at, v))
			return -1;
		/* sym@MOD+k */
		if (as_whitespace_skip(me, q) < q) {
			int64_t k;
			const char *y = me;

			if (as_expression_absolute_parse(a, &y, q, &k))
				return -1;
			v->c += k;
		}
		*pp = q;
		return 0;
	}
	if (as_expression_value_parse(a, &p, q, v))
		return -1;
	*pp = p;
	return 0;
}

static int x64_operand(as_context *a, const char **pp, const char *e, XOp *o)
{
	const char *p = as_whitespace_skip(*pp, e);

	memset(o, 0, sizeof *o);
	o->base = o->index = -1;
	if (p < e && *p == '*') {
		o->star = 1;
		p = as_whitespace_skip(p + 1, e);
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
		XOp r = {0};
		const char *q = x64_reg(a, p + 1, e, &r);

		if (!q)
			return -1;
		if (r.cls == XR_SEG && as_whitespace_skip(q, e) < e && *as_whitespace_skip(q, e) == ':') {
			o->seg = r.reg;
			p = as_whitespace_skip(as_whitespace_skip(q, e) + 1, e);
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
	/* a displacement may start with a parenthesis: (2f)(%rip), (a+b)*4(%rax) */
	if (p < e && (*p != '(' || (as_whitespace_skip(p + 1, e) < e && *as_whitespace_skip(p + 1, e) != '%' && *as_whitespace_skip(p + 1, e) != ','))) {
		if (x64_expr(a, &p, e, &o->v, &o->mod))
			return -1;
		o->hasv = 1;
	}
	p = as_whitespace_skip(p, e);
	if (p < e && *p == '(') {
		XOp r;

		o->kind = XK_MEM;
		p = as_whitespace_skip(p + 1, e);
		if (p < e && *p == '%') {
			const char *q = x64_reg(a, p + 1, e, &r);

			if (!q)
				return -1;
			if (r.cls == XR_GPR && r.size == 2 && r.reg == 2 && !o->hasv && as_whitespace_skip(q, e) < e && *as_whitespace_skip(q, e) == ')') {
				/* in/out (%dx): the port register */
				*o = r;
				o->kind = XK_REG, o->base = o->index = -1;
				*pp = as_whitespace_skip(q, e) + 1;
				return 0;
			}
			o->base = (int8_t)(r.cls == XR_RIP ? 16 : r.reg);
			if (r.cls != XR_RIP && (r.cls != XR_GPR || r.size != 8))
				return as_error_report(a, "invalid base register");
			p = as_whitespace_skip(q, e);
		}
		if (p < e && *p == ',') {
			p = as_whitespace_skip(p + 1, e);
			if (p < e && *p == '%') {
				const char *q = x64_reg(a, p + 1, e, &r);

				if (!q)
					return -1;
				if (r.cls != XR_GPR || r.size != 8 || r.reg == 4)
					return as_error_report(a, "invalid index register");
				o->index = (int8_t)r.reg;
				p = as_whitespace_skip(q, e);
			}
			o->scale = 1;
			if (p < e && *p == ',') {
				int64_t s;

				p = as_whitespace_skip(p + 1, e);
				if (as_expression_absolute_parse(a, &p, e, &s))
					return -1;
				if (s != 1 && s != 2 && s != 4 && s != 8)
					return as_error_report(a, "scale factor in address must be 1, 2, 4 or 8");
				o->scale = (uint8_t)s;
				p = as_whitespace_skip(p, e);
			}
		}
		if (p >= e || *p != ')')
			return as_error_report(a, "unexpected token in memory operand");
		p++;
	} else {
		o->kind = o->seg ? XK_MEM : XK_EXPR;	/* a bare expression: a branch target or an absolute address */
	}
	*pp = p;
	return 0;
}

/* ---------------------------------------------------------------- encoding */




VPRIV void eb(XEnc *x, int v) { x->b[x->n++] = (uint8_t)v; }
VPRIV void ew(XEnc *x, int64_t v, int n) { for (int i = 0; i < n; i++) eb(x, (int)((uint64_t)v >> 8 * i) & 0xff); }

VPRIV int fits8(int64_t v) { return v >= -128 && v <= 127; }
VPRIV int fits32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

/* a constant Val (no symbol): its value */
VPRIV int isconst(as_context *a, as_value *v, int64_t *c)
{
	as_value_fold(a, v);
	if (v->add || v->sub)
		return 0;
	*c = v->c;
	return 1;
}

/* the relocation type of a displacement or immediate with modifier mod */
VPRIV int x64_rtype(int mod, int pcrel, int size, int sgn, int rexw, int gotx)
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



VPRIV int x64_emit(as_context *a, XForm *f)
{
	XEnc x = { 0 };
	const XOp *rm = f->rm;
	int rex = 0x40 | (f->w ? 8 : 0) | (f->reg & 8 ? 4 : 0) | (f->oreg && (f->oreg - 1) & 8 ? 1 : 0);
	int seg = rm && rm->kind != XK_REG ? rm->seg : 0;
	uint8_t modrm[8];
	int nm = 0, dispsz = 0, ripfix = 0;
	int64_t disp = 0;
	as_value dv = { 0, 0, 0 };
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
			int64_t disp8 = disp;
			if (f->tuple) {
				/* EVEX disp8 is in units of the memory tuple. */
				disp8 = disp % f->tuple ? 128 : disp / f->tuple;
			}
			md = dsym ? 2 : (base < 0 ? 0 : disp == 0 && (base & 7) != 5 ? 0 : fits8(disp8) ? 1 : 2);
			if (md == 1) disp = disp8;
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
	if (f->opsz16 && !f->vex)
		eb(&x, 0x66);
	if (a->x64_lock_prefix) {
		eb(&x, 0xf0);
		a->x64_lock_prefix = 0;
	}
	if (f->vex) {
		/* VEX: R X B (inverted) and W from REX, mmmmm from the map, pp from the prefix */
		int R = !(rex & 4), X = !(rex & 2), B = !(rex & 1), W = !!(rex & 8);
		int mm = f->nop == 3 ? (f->op[1] == 0x38 ? 2 : 3) : 1;
		int pp = f->opsz16 ? 1 : f->rep == 0xf3 ? 2 : f->rep == 0xf2 ? 3 : 0;
		int tail = (~f->vvvv & 15) << 3 | (f->vl ? 4 : 0) | pp;

		if (f->tuple) {
			/* EVEX.512: register r/m bit 4 uses X; memory uses the SIB index. */
			if (rm->kind == XK_REG) X = !(rm->reg & 16);
			eb(&x, 0x62);
			eb(&x, R << 7 | X << 6 | B << 5 | !(f->reg & 16) << 4 | mm);
			eb(&x, W << 7 | (~f->vvvv & 15) << 3 | 4 | pp);
			eb(&x, f->zero << 7 | 0x40 | !(f->vvvv & 16) << 3 | f->mask);
		} else if (X && B && !W && mm == 1) {
			eb(&x, 0xc5);
			eb(&x, (uint8_t)(R << 7 | tail));
		} else {
			eb(&x, 0xc4);
			eb(&x, (uint8_t)(R << 7 | X << 6 | B << 5 | mm));
			eb(&x, (uint8_t)(W << 7 | tail));
		}
		eb(&x, f->op[f->nop - 1]);
	} else {
	if (f->rep)
		eb(&x, f->rep);
	if (rex != 0x40 || f->rex8)
		eb(&x, rex);
	for (int i = 0; i < f->nop; i++)
		eb(&x, f->op[i] + (f->oreg && i == f->nop - 1 ? (f->oreg - 1) & 7 : 0));
	}
	for (int i = 0; i < nm; i++)
		eb(&x, modrm[i]);
	if (dispsz) {
		if (dsym || ripfix) {
			int rt = x64_rtype(rm->mod, ripfix, 4, 1, f->w, f->gotx);

			x.fixat = x.n, x.fixtype = rt, x.fixsize = 4;
			x.fixv = dv;
			if (ripfix && !dsym && !rm->hasv)
				x.fixv = (as_value){ 0, 0, 0 };
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
		uint32_t fr = a->fragment_current;
		uint64_t off;
		uint8_t *p;

		elf_code(a->object, fr);
		off = elf_frag(a->object, fr)->size;
		p = elf_grow(a->object, fr, (size_t)x.n);
		memcpy(p, x.b, (size_t)x.n);
		if (x.fixtype)
			elf_fixup_value(a->object, fr, off + (uint64_t)x.fixat, p + x.fixat, (uint32_t)x.fixtype, &x.fixv);
		if (x.fix2type)
			elf_fixup_value(a->object, fr, off + (uint64_t)x.fix2at, p + x.fix2at, (uint32_t)x.fix2type, &x.fix2v);
	}
	return 0;
}

/* raw bytes (no operands) */
VPRIV int x64_raw(as_context *a, const uint8_t *b, int n)
{
	uint32_t fr = a->fragment_current;

	elf_code(a->object, fr);
	memcpy(elf_grow(a->object, fr, (size_t)n), b, (size_t)n);
	return 0;
}

/* ---------------------------------------------------------------- mnemonics */

static const char *const ccn[16][3] = {
	{ "o" }, { "no" }, { "b", "c", "nae" }, { "ae", "nb", "nc" }, { "e", "z" }, { "ne", "nz" }, { "be", "na" }, { "a", "nbe" },
	{ "s" }, { "ns" }, { "p", "pe" }, { "np", "po" }, { "l", "nge" }, { "ge", "nl" }, { "le", "ng" }, { "g", "nle" },
};

/* the condition a suffix names, -1 for none */
VPRIV int x64_cc(const char *s)
{
	for (int i = 0; i < 16; i++)
		for (int k = 0; k < 3 && ccn[i][k]; k++)
			if (!strcmp(s, ccn[i][k]))
				return i;
	return -1;
}

VPRIV int sfxsize(char c) { return c == 'b' ? 1 : c == 'w' ? 2 : c == 'l' ? 4 : c == 'q' ? 8 : 0; }

/* an operand's size from its register, 0 for none */
VPRIV int opsize(const XOp *o) { return o->kind == XK_REG && o->cls == XR_GPR ? o->size : 0; }


VPRIV int x64_imm(as_context *a, const XOp *o, XForm *f, int64_t *k)
{
	as_value v = o->v;

	if (isconst(a, &v, k) && !o->mod) {
		if (a->x64_operand_size == 1)
			*k = (int8_t)*k;
		else if (a->x64_operand_size == 2)
			*k = (int16_t)*k;
		else if (a->x64_operand_size == 4)
			*k = (int32_t)*k;
		return 0;
	}
	f->immsym = 1;
	f->immv = o->v;
	f->immmod = o->mod;
	return 1;
}

/* the size of a two-operand instruction: its suffix, else a register operand's */
VPRIV int x64_size(as_context *a, int sfx, const XOp *s, const XOp *d)
{
	int sz = sfx ? sfx : opsize(d) ? opsize(d) : opsize(s);

	if (!sz)
		as_error_report(a, "cannot infer the operand size");
	return sz;
}

VPRIV int rex8(const XOp *o) { return o->kind == XK_REG && o->cls == XR_GPR && o->size == 1 && o->needrex; }



/* a mnemonic stem and its size suffix: "addq" -> "add", 8 (tried when the full name is unknown) */
VPRIV int split_sfx(const char *mn, char *stem, int *sz)
{
	size_t n = strlen(mn);

	if (n < 2 || !sfxsize(mn[n - 1]))
		return 0;
	memcpy(stem, mn, n - 1);
	stem[n - 1] = 0;
	*sz = sfxsize(mn[n - 1]);
	return 1;
}

VPRIV int x64_alu(as_context *a, int n, int sz, XOp *o, int nops)
{
	XOp *s = &o[0], *d = &o[1];
	int w = sz == 8, p16 = sz == 2, b = sz == 1;

	a->x64_operand_size = sz;
	if (nops != 2)
		return as_error_report(a, "invalid operand count");
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
	return as_error_report(a, "invalid operand for instruction");
}

/* a jump or call to a target expression: jumps relax (as.c), calls are rel32 */
VPRIV int x64_branch(as_context *a, int cc, XOp *t, int call)
{
	if (call) {
		XEnc x = { 0 };
		uint32_t fr = a->fragment_current;
		uint64_t off;
		uint8_t *p;
		as_value v = t->v;

		eb(&x, 0xe8);
		ew(&x, 0, 4);
		elf_code(a->object, fr);
		off = elf_frag(a->object, fr)->size;
		p = elf_grow(a->object, fr, 5);
		memcpy(p, x.b, 5);
		v.c -= 4;
		elf_fixup(a->object, fr, off + 1, p + 1, R_X86_64_PLT32, v);
		if (t->mod == XM_PLT)
			((ElfFix *)a->object->fixes.base)[a->object->nfix - 1].pad = 1;	/* sym@PLT: never resolved in place (MC) */
		return 0;
	}
	return as_variable_item_branch_add(a, cc, t->v, t->mod);
}

/* SSE: [prefix] [REX] 0f op ModRM */
VPRIV int x64_sse(as_context *a, int pfx, int op, int w, int reg, const XOp *rm)
{
	uint8_t ops[2] = { 0x0f, (uint8_t)op };

	return x64_emit(a, &F(.rep = pfx == 0x66 ? 0 : pfx, .opsz16 = pfx == 0x66, .w = w, .op = ops, .nop = 2, .reg = reg, .rm = rm));
}

/* x87 with a memory operand: op /digit */
VPRIV int x64_x87m(as_context *a, int op, int digit, const XOp *m)
{
	uint8_t b = (uint8_t)op;

	return x64_emit(a, &F(.op = &b, .nop = 1, .reg = digit, .rm = m));
}

VPRIV int x64_x87r(as_context *a, int op, int base, int i)
{
	uint8_t b[2] = { (uint8_t)op, (uint8_t)(base + i) };

	return x64_raw(a, b, 2);
}

VPRIV int x64_as_instruction_assemble(as_context *a, const char *p, const char *e, size_t wn)
{
	char mn[32];
	XOp o[4];
	int nops = 0;

	if (wn >= sizeof mn)
		return as_error_report(a, "invalid instruction mnemonic");
	a->x64_operand_size = 0;
	for (size_t i = 0; i < wn; i++)
		mn[i] = (char)as_char_to_lower((unsigned char)p[i]);
	mn[wn] = 0;
	p = as_whitespace_skip(p + wn, e);
	/* prefixes written as their own word: lock, rep, data16, rex64 */
	if (!strcmp(mn, "lock") || !strcmp(mn, "rep") || !strcmp(mn, "repz") || !strcmp(mn, "repe") || !strcmp(mn, "repnz") ||
	    !strcmp(mn, "repne") || !strcmp(mn, "data16") || !strcmp(mn, "rex64") || !strcmp(mn, "cs") || !strcmp(mn, "notrack") ||
	    !strcmp(mn, "ds") || !strcmp(mn, "ss") || !strcmp(mn, "es") || !strcmp(mn, "fs") || !strcmp(mn, "gs")) {
		uint8_t b = !strcmp(mn, "lock") ? 0xf0 : !strcmp(mn, "data16") ? 0x66 : !strcmp(mn, "rex64") ? 0x48 :
			    !strcmp(mn, "cs") ? 0x2e : !strcmp(mn, "notrack") || !strcmp(mn, "ds") ? 0x3e : !strcmp(mn, "ss") ? 0x36 :
			    !strcmp(mn, "es") ? 0x26 : !strcmp(mn, "fs") ? 0x64 : !strcmp(mn, "gs") ? 0x65 : mn[3] == 'n' ? 0xf2 : 0xf3;

		if (p < e && b == 0xf0) {
			const char *q = p;
			int r;

			while (q < e && as_char_is_identifier((unsigned char)*q))
				q++;
			a->x64_lock_prefix = 1;
			r = x64_as_instruction_assemble(a, p, e, (size_t)(q - p));
			if (a->x64_lock_prefix) {	/* (an instruction without x64_emit: the prefix first) */
				a->x64_lock_prefix = 0;
			}
			return r;
		}
		if (x64_raw(a, &b, 1))
			return -1;
		if (p < e) {
			const char *q = p;

			while (q < e && as_char_is_identifier((unsigned char)*q))
				q++;
			return x64_as_instruction_assemble(a, p, e, (size_t)(q - p));
		}
		return 0;
	}
	if (a->register_alias_count) {
		/* register aliases (name = %reg) written out as their registers */
		static _Thread_local char rb[4096];
		size_t n = 0;
		int sub = 0;

		for (const char *q = p; q < e && n < sizeof rb - 16;) {
			if (as_char_is_identifier_start((unsigned char)*q) && (q == p || (!as_char_is_identifier((unsigned char)q[-1]) && q[-1] != '%' && q[-1] != '$' && q[-1] != '.'))) {
				const char *t = q;
				const char *rn;

				while (t < e && as_char_is_identifier((unsigned char)*t))
					t++;
				rn = as_register_alias_find(a, elf_sym_lookup(a->object, q, (uint32_t)(t - q)));
				if (rn && n + 10 < sizeof rb) {
					rb[n++] = '%';
					for (; *rn; rn++)
						rb[n++] = *rn;
					sub = 1;
				} else {
					memcpy(rb + n, q, (size_t)(t - q) < sizeof rb - 16 - n ? (size_t)(t - q) : 0);
					n += (size_t)(t - q);
				}
				q = t;
				continue;
			}
			rb[n++] = *q++;
		}
		if (sub)
			p = rb, e = rb + n;
	}
	while (p < e) {
		if (nops == 4)
			return as_error_report(a, "too many operands");
		if (x64_operand(a, &p, e, &o[nops]))
			return -1;
		XOp *op = &o[nops++];
		p = as_whitespace_skip(p, e);
		if (p < e && *p == '{') {
			XOp k = {0};
			const char *q = p + 1;
			if (op->kind != XK_REG || (op->cls != XR_ZMM && op->cls != XR_MASK) ||
			    q == e || *q++ != '%' || !(q = x64_reg(a, q, e, &k)) ||
			    k.cls != XR_MASK || !k.reg || q == e || *q++ != '}')
				return as_error_report(a, "invalid writemask");
			op->mask = k.reg;
			p = as_whitespace_skip(q, e);
			if (e - p >= 3 && !memcmp(p, "{z}", 3)) {
				op->zero = 1;
				p = as_whitespace_skip(p + 3, e);
			}
		}
		if (p < e && *p == ',')
			p = as_whitespace_skip(p + 1, e);
		else if (p < e)
			return as_error_report(a, "unexpected token in argument list");
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
	return as_x64_insn(a, mn, o, nops);
}

int as_x64_insn(as_context *a, char *mn, XOp *o, int nops)
{
	if (a->dwarf_row_is_pending) as_dwarf_row_emit(a);
	a->x64_operand_size = 0;
	for (int i = 0; i < nops; i++)
		if (o[i].kind == XK_REG && (o[i].cls == XR_ZMM || o[i].cls == XR_MASK))
			return x64_i_avx512(a, mn, o, nops);
	{
		static int (*const fam[])(as_context *, char *, XOp *, int) = {
			x64_i_crdr,
			x64_i_zero,
			x64_i_jump,
			x64_i_setcc,
			x64_i_x87,
			x64_i_sse,
			x64_i_avx,
			x64_i_sse4,
			x64_i_ssepk,
			x64_i_bits,
			x64_i_sys,
			x64_i_x87ar,
			x64_i_int,
			x64_i_movx,
		};

		for (size_t i = 0; i < sizeof fam / sizeof *fam; i++) {
			int r = fam[i](a, mn, o, nops);

			if (r != X64_NEXT)
				return r;
		}
	}
	return as_error_report(a, "invalid instruction mnemonic '%s'", mn);
}
