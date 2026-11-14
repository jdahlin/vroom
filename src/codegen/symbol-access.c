/*
 * symbol-access.c: codegen_symbol_needs_got: whether code must reach a symbol through the GOT.
 *
 * The answer follows clang's rules per target: on Darwin every symbol that may live in a dylib; on
 * Linux with -fPIC every preemptible symbol, with -fPIE the ones not defined here, hidden and
 * protected ones never; weak undefined symbols everywhere.  A wrong answer is a wrong relocation, so
 * change it against clang's output.  Called by the arm64 and x86-64 lowerings (x86-64's VIR path included).
 * On Windows the answer is sym_win64_indirect's: there is no GOT, but a pointer read first.
 */
#include "codegen-internal.h"

VPRIV bool codegen_symbol_needs_got(cc *c, uint32_t ref)
{
	if (CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_NAME)
		return CODEGEN_IS_DARWIN(c);
	if (CODEGEN_REFERENCE_KIND(ref) != CODEGEN_REFERENCE_SYMBOL)
		return false;
	if (CODEGEN_IS_WIN64(c))
		return sym_win64_indirect(c, ref) != 0;
	{
		const sym *y = &c->syms[codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(ref))];

		if (CODEGEN_IS_DARWIN(c) && y->name && y->sc != AST_STORAGE_STATIC && (y->kind == AST_SYMBOL_FUNCTION || y->kind == AST_SYMBOL_VARIABLE) &&
		    (!(y->flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)) || (y->flags & AST_SYMBOL_FLAG_WEAK) || (y->kind == AST_SYMBOL_FUNCTION && !y->body)))
			return true;	/* may live in a dylib: ld64 relaxes the GOT load when it does not */
		if (!CODEGEN_IS_DARWIN(c) && c->o->pic && y->name && y->sc != AST_STORAGE_STATIC && (y->kind == AST_SYMBOL_FUNCTION || y->kind == AST_SYMBOL_VARIABLE)) {
			/* clang: -fPIC: every preemptible symbol; -fPIE: those not defined here.
			 * Hidden/protected ones are direct. */
			const attrs *a = codegen_symbol_attributes(c, codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(ref)));

			if (a && a->visibility) {
				const char *v = c->strs[a->visibility - 1].s;

				if (!strcmp(v, "hidden") || !strcmp(v, "protected"))
					return false;
			}
			if (c->o->pic == 2)
				return true;
			if (a && a->alias)
				return false;	/* an alias or ifunc is defined here */
			if (!(y->flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)) || (y->kind == AST_SYMBOL_FUNCTION && !y->body))
				return true;
		}
		return (y->flags & AST_SYMBOL_FLAG_WEAK) && !(y->flags & AST_SYMBOL_FLAG_DEFINED);
	}
}

/*
 * Windows x64 (MinGW, as clang): how code reaches a symbol.  WIN64_IMP: through __imp_NAME, the
 * import table's pointer (dllimport); WIN64_REFPTR: through .refptr.NAME, a pointer in a discard
 * COMDAT this TU emits (the tail) which the MinGW runtime's pseudo-relocations fill if the symbol
 * turns out to live in a DLL: an extern variable not defined here and not hidden, a weak undefined
 * function or variable; 0: directly, RIP-relative (definitions, functions).
 */
VPRIV int sym_win64_indirect(cc *c, uint32_t ref)
{
	uint32_t s;
	const sym *y;
	const attrs *a;

	if (CODEGEN_REFERENCE_KIND(ref) != CODEGEN_REFERENCE_SYMBOL)
		return 0;
	s = codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(ref));
	y = &c->syms[s];
	a = codegen_symbol_attributes(c, s);
	if (!y->name || y->sc == AST_STORAGE_STATIC || (y->kind != AST_SYMBOL_FUNCTION && y->kind != AST_SYMBOL_VARIABLE) || (a && a->alias))
		return 0;
	if (a && a->dll == CC_DLLIMPORT)
		return WIN64_IMP;
	if ((y->flags & AST_SYMBOL_FLAG_WEAK) && !(y->flags & AST_SYMBOL_FLAG_DEFINED))
		return WIN64_REFPTR;
	if (y->kind == AST_SYMBOL_FUNCTION || (y->flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)))
		return 0;
	if (a && a->visibility) {
		const char *v = c->strs[a->visibility - 1].s;

		if (!strcmp(v, "hidden") || !strcmp(v, "protected"))
			return 0;
	}
	return WIN64_REFPTR;
}
