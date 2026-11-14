/*
 * symbol-name.c: the type and symbol helpers every backend needs: integer truncation to a type's width,
 * the canonical symbol of a declaration, a symbol's attributes, and the names symbols and
 * references are written under.
 *
 * codegen_integer_truncate wraps a constant to its type (sign or zero extended from the type's bits, bool to 0/1)
 * and is what every constant fold uses, so folding agrees with the hardware.  codegen_symbol_canonical follows the
 * `next` chain of a block-scope declaration to the file-scope symbol, so all references to one
 * object share one entry.  sym_name/codegen_reference_name_append write the assembler name (quoted when needed) and
 * codegen_fixed_function_names is the table of library functions the emitter may call by itself (memcpy, __udivti3,
 * soft-float helpers ...), numbered by the FN_* enum in codegen-internal.h: the two must stay in the same
 * order.
 */
#include "codegen-internal.h"

VPRIV i128 codegen_integer_truncate(cc *c, i128 v, ast_qualified_type t)
{
	int k = KIND(c, t);
	unsigned bits;

	if (k == AST_TYPE_BOOL)
		return v != 0;
	if (k == AST_TYPE_POINTER)
		return (i128)(uint64_t)v;
	bits = codegen_type_is_integer(c, t) && T(c, t)->n && !(T(c, t)->flags & AST_TYPE_FLAG_ATOMIC) ?
		T(c, t)->n : (unsigned)codegen_type_size(c, t) * 8;
	if (bits >= 128)
		return codegen_type_is_signed(c, t) ? v : (i128)(u128)v;
	{
		u128 m = ((u128)1 << bits) - 1;
		u128 x = (u128)v & m;

		if (codegen_type_is_signed(c, t) && (x >> (bits - 1)) & 1)
			return (i128)(x | ~m);
		return (i128)x;
	}
}

/* ---------------- names ---------------- */

static const char *const codegen_fixed_function_names[] = {
	"memcpy", "memset", "memmove", "__udivti3", "__divti3", "__umodti3", "__modti3", "strlen",
	"__ashlti3", "__lshrti3", "__ashrti3", "__floattisf", "__floattidf", "__floatuntisf", "__floatuntidf",
	"__fixsfti", "__fixdfti", "__fixunssfti", "__fixunsdfti",
	"__addtf3", "__subtf3", "__multf3", "__divtf3", "__eqtf2", "__netf2", "__lttf2", "__letf2", "__gttf2", "__getf2", "__unordtf2",
	"__extenddftf2", "__extendsftf2", "__trunctfdf2", "__trunctfsf2", "__floatsitf", "__floatditf", "__floatunsitf",
	"__floatunditf", "__floattitf", "__floatuntitf", "__fixtfsi", "__fixtfdi", "__fixunstfsi", "__fixunstfdi",
	"__fixtfti", "__fixunstfti", "__extendxftf2", "__trunctfxf2",
	"__extendbfsf2", "__truncsfbf2", "__truncdfbf2", "__truncxfbf2", "__trunctfbf2",
	"__extendhfsf2", "__truncsfhf2", "__truncdfhf2", "__truncxfhf2", "__trunctfhf2",
	"__floattixf", "__floatuntixf", "__fixxfti", "__fixunsxfti",
	"floorf", "ceilf", "truncf", "roundf", "rintf", "nearbyintf", "fabsf",
	"__main",
};

VPRIV uint32_t codegen_symbol_canonical(cc *c, uint32_t s)
{
	while (c->syms[s].next && c->syms[s].depth > 0)
		s = c->syms[s].next;
	return s;
}

VPRIV const attrs *codegen_symbol_attributes(cc *c, uint32_t s)
{
	static const attrs none;

	return c->syms[s].attrs ? &c->attr[c->syms[s].attrs] : &none;
}

VPRIV void codegen_symbol_name_append(cc *c, tbuf *b, uint32_t s)
{
	const sym *y;
	const attrs *a;

	s = codegen_symbol_canonical(c, s);
	y = &c->syms[s];
	a = codegen_symbol_attributes(c, s);
	if (a->asmname) {
		vtext_string_append(b, c->strs[a->asmname - 1].s);
		return;
	}
	if (!y->name) {
		vtext_string_append(b, ".Lcl.");
		vtext_unsigned_append(b, s);
		return;
	}
	if (CODEGEN_IS_DARWIN(c))
		vtext_char_append(b, '_');
	if ((WASM(c) || CODEGEN_IS_X64(c)) && codegen_symbol_name_is_quoted(c, s)) {
		/* LLVM's wasm and x86 assemblers require quotes around non-ASCII names. */
		vtext_char_append(b, '"');
		vtext_append(b, vintern_ents[y->name].s, vintern_ents[y->name].len);
		if (y->depth > 0 && y->kind == AST_SYMBOL_VARIABLE && y->sc == AST_STORAGE_STATIC) {
			vtext_char_append(b, '.');
			vtext_unsigned_append(b, s);
		}
		vtext_char_append(b, '"');
		return;
	}
	vtext_append(b, vintern_ents[y->name].s, vintern_ents[y->name].len);
	if (y->depth > 0 && y->kind == AST_SYMBOL_VARIABLE && y->sc == AST_STORAGE_STATIC) {
		vtext_char_append(b, '.');
		vtext_unsigned_append(b, s);
	}
}

/* wasm/x86 assembly: non-ASCII names need quotes (asm labels are explicit). */
VPRIV bool codegen_symbol_name_is_quoted(cc *c, uint32_t s)
{
	const sym *y;

	s = codegen_symbol_canonical(c, s);
	y = &c->syms[s];
	if (!y->name || codegen_symbol_attributes(c, s)->asmname)
		return false;
	for (uint32_t i = 0; i < vintern_ents[y->name].len; i++)
		if ((unsigned char)vintern_ents[y->name].s[i] >= 0x80)
			return true;
	return false;
}

VPRIV void codegen_reference_name_append(cc *c, tbuf *b, uint32_t r)
{
	uint32_t i = CODEGEN_REFERENCE_INDEX(r);

	switch (CODEGEN_REFERENCE_KIND(r)) {
	case CODEGEN_REFERENCE_SYMBOL:
		codegen_symbol_name_append(c, b, i);
		break;
	case CODEGEN_REFERENCE_STRING:
		vtext_string_append(b, ".L.str.");
		vtext_unsigned_append(b, i);
		break;
	case CODEGEN_REFERENCE_LABEL:
		vtext_string_append(b, ".Ltmp.lbl.");
		vtext_unsigned_append(b, i);
		break;
	case CODEGEN_REFERENCE_TABLE:
		vtext_string_append(b, ".Lswitch.values.");
		vtext_unsigned_append(b, i);
		break;
	case CODEGEN_REFERENCE_NAME:
		if (CODEGEN_IS_DARWIN(c))
			vtext_char_append(b, '_');
		if (i >= 0x8000000u) {
			vtext_string_append(b, codegen_fixed_function_names[i - 0x8000000u]);
		} else {
			const char *n = cc_builtin_name_get(i);

			if (!strncmp(n, "__builtin_", 10))
				n += 10;
			vtext_string_append(b, n);
		}
		break;
	}
}

VPRIV void codegen_reference_offset_append(cc *c, tbuf *b, uint32_t r, int64_t off)
{
	codegen_reference_name_append(c, b, r);
	if (off > 0) {
		vtext_char_append(b, '+');
		vtext_signed_append(b, off);
	} else if (off < 0) {
		vtext_signed_append(b, off);
	}
}
