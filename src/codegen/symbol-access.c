/*
 * symbol-access.c: sym_needs_got: whether code must reach a symbol through the GOT.
 *
 * The answer follows clang's rules per target: on Darwin every symbol that may live in a dylib; on
 * Linux with -fPIC every preemptible symbol, with -fPIE the ones not defined here, hidden and
 * protected ones never; weak undefined symbols everywhere.  A wrong answer is a wrong relocation, so
 * change it against clang's output.  Called by the arm64 and x86-64 lowerings (x86-64's VIR path included).
 */
#include "emitimpl.h"

VPRIV bool sym_needs_got(cc *c, uint32_t ref)
{
	if (RKIND(ref) == RK_NAME)
		return DARWIN(c);
	if (RKIND(ref) != RK_SYM)
		return false;
	{
		const sym *y = &c->syms[canon_sym(c, RIDX(ref))];

		if (DARWIN(c) && y->name && y->sc != AST_STORAGE_STATIC && (y->kind == AST_SYMBOL_FUNCTION || y->kind == AST_SYMBOL_VARIABLE) &&
		    (!(y->flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)) || (y->flags & AST_SYMBOL_FLAG_WEAK) || (y->kind == AST_SYMBOL_FUNCTION && !y->body)))
			return true;	/* may live in a dylib: ld64 relaxes the GOT load when it does not */
		if (!DARWIN(c) && c->o->pic && y->name && y->sc != AST_STORAGE_STATIC && (y->kind == AST_SYMBOL_FUNCTION || y->kind == AST_SYMBOL_VARIABLE)) {
			/* clang: -fPIC: every preemptible symbol; -fPIE: those not defined here.
			 * Hidden/protected ones are direct. */
			const attrs *a = sym_attrs(c, canon_sym(c, RIDX(ref)));

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
