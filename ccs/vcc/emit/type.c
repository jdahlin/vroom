/* vcc code generator: the type predicates the emitter needs, symbol names */
#include "emitimpl.h"

VPRIV i128 em_trunc_to(cc *c, i128 v, qty t)
{
	int k = KIND(c, t);
	uint64_t sz;

	if (k == TY_BOOL)
		return v != 0;
	if (k == TY_PTR)
		return (i128)(uint64_t)v;
	sz = tsize(c, t);
	if (sz >= 16)
		return is_signed(c, t) ? v : (i128)(u128)v;
	{
		unsigned bits = (unsigned)sz * 8;
		u128 m = ((u128)1 << bits) - 1;
		u128 x = (u128)v & m;

		if (is_signed(c, t) && (x >> (bits - 1)) & 1)
			return (i128)(x | ~m);
		return (i128)x;
	}
}

/* ---------------- names ---------------- */

static const char *const fixed_names[] = {
	"memcpy", "memset", "memmove", "__udivti3", "__divti3", "__umodti3", "__modti3", "strlen",
	"__ashlti3", "__lshrti3", "__ashrti3", "__floattisf", "__floattidf", "__floatuntisf", "__floatuntidf",
	"__fixsfti", "__fixdfti", "__fixunssfti", "__fixunsdfti",
	"__addtf3", "__subtf3", "__multf3", "__divtf3", "__eqtf2", "__netf2", "__lttf2", "__letf2", "__gttf2", "__getf2",
	"__extenddftf2", "__extendsftf2", "__trunctfdf2", "__trunctfsf2", "__floatsitf", "__floatditf", "__floatunsitf",
	"__floatunditf", "__floattitf", "__floatuntitf", "__fixtfsi", "__fixtfdi", "__fixunstfsi", "__fixunstfdi",
	"__fixtfti", "__fixunstfti", "__extendxftf2", "__trunctfxf2",
};

VPRIV uint32_t canon_sym(cc *c, uint32_t s)
{
	while (c->syms[s].next && c->syms[s].depth > 0)
		s = c->syms[s].next;
	return s;
}

VPRIV const attrs *sym_attrs(cc *c, uint32_t s)
{
	static const attrs none;

	return c->syms[s].attrs ? &c->attr[c->syms[s].attrs] : &none;
}

VPRIV void sym_name(cc *c, tbuf *b, uint32_t s)
{
	const sym *y;
	const attrs *a;

	s = canon_sym(c, s);
	y = &c->syms[s];
	a = sym_attrs(c, s);
	if (a->asmname) {
		tb_s(b, c->strs[a->asmname - 1].s);
		return;
	}
	if (!y->name) {
		tb_s(b, ".Lcl.");
		tb_u(b, s);
		return;
	}
	if (DARWIN(c))
		tb_c(b, '_');
	if ((WASM(c) || X86(c)) && sym_quoted(c, s)) {
		/* LLVM's wasm and x86 assemblers take only ASCII names unquoted (as clang writes them) */
		tb_c(b, '"');
		tb_put(b, vintern_ents[y->name].s, vintern_ents[y->name].len);
		if (X86(c) && y->depth > 0 && y->kind == S_VAR && y->sc == SC_STATIC) {
			tb_c(b, '.');
			tb_u(b, s);
		}
		tb_c(b, '"');
		return;
	}
	tb_put(b, vintern_ents[y->name].s, vintern_ents[y->name].len);
	if (y->depth > 0 && y->kind == S_VAR && y->sc == SC_STATIC) {
		tb_c(b, '.');
		tb_u(b, s);
	}
}

/* wasm, x86-64 assembly: the name needs quotes (a byte outside ASCII; never with an asm label) */
VPRIV bool sym_quoted(cc *c, uint32_t s)
{
	const sym *y;

	s = canon_sym(c, s);
	y = &c->syms[s];
	if (!y->name || sym_attrs(c, s)->asmname)
		return false;
	for (uint32_t i = 0; i < vintern_ents[y->name].len; i++)
		if ((unsigned char)vintern_ents[y->name].s[i] >= 0x80)
			return true;
	return false;
}

VPRIV void ref_name(cc *c, tbuf *b, uint32_t r)
{
	uint32_t i = RIDX(r);

	switch (RKIND(r)) {
	case RK_SYM:
		sym_name(c, b, i);
		break;
	case RK_STR:
		tb_s(b, ".L.str.");
		tb_u(b, i);
		break;
	case RK_LBL:
		tb_s(b, ".Ltmp.lbl.");
		tb_u(b, i);
		break;
	case RK_NAME:
		if (DARWIN(c))
			tb_c(b, '_');
		if (i >= 0x8000000u) {
			tb_s(b, fixed_names[i - 0x8000000u]);
		} else {
			const char *n = cc_builtin_name(i);

			if (!strncmp(n, "__builtin_", 10))
				n += 10;
			tb_s(b, n);
		}
		break;
	}
}

VPRIV void ref_off(cc *c, tbuf *b, uint32_t r, int64_t off)
{
	ref_name(c, b, r);
	if (off > 0) {
		tb_c(b, '+');
		tb_i(b, off);
	} else if (off < 0) {
		tb_i(b, off);
	}
}
