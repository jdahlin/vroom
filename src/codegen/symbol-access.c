/*
 * symbol-access.c: sym_needs_got: whether code must reach a symbol through the GOT.
 *
 * The answer follows clang's rules per target: on Darwin every symbol that may live in a dylib; on
 * Linux with -fPIC every preemptible symbol, with -fPIE the ones not defined here, hidden and
 * protected ones never; weak undefined symbols everywhere.  A wrong answer is a wrong relocation, so
 * change it against clang's output.  Called by the arm64 and x86-64 lowerings (x86-64's VIR path included).
 * On Windows the answer is sym_win64_indirect's: there is no GOT, but a pointer read first.
 */
#include "emitimpl.h"

VPRIV bool sym_needs_got(cc *c, uint32_t ref)
{
	if (RKIND(ref) == RK_NAME)
		return DARWIN(c);
	if (RKIND(ref) != RK_SYM)
		return false;
	if (WIN64(c))
		return sym_win64_indirect(c, ref) != 0;
	{
		const sym *y = &c->syms[canon_sym(c, RIDX(ref))];

		if (DARWIN(c) && y->name && y->sc != SC_STATIC && (y->kind == S_FUNC || y->kind == S_VAR) &&
		    (!(y->flags & (SF_DEFINED | SF_TENTATIVE)) || (y->flags & SF_WEAK) || (y->kind == S_FUNC && !y->body)))
			return true;	/* may live in a dylib: ld64 relaxes the GOT load when it does not */
		if (!DARWIN(c) && c->o->pic && y->name && y->sc != SC_STATIC && (y->kind == S_FUNC || y->kind == S_VAR)) {
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
			if (!(y->flags & (SF_DEFINED | SF_TENTATIVE)) || (y->kind == S_FUNC && !y->body))
				return true;
		}
		return (y->flags & SF_WEAK) && !(y->flags & SF_DEFINED);
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

	if (RKIND(ref) != RK_SYM)
		return 0;
	s = canon_sym(c, RIDX(ref));
	y = &c->syms[s];
	a = sym_attrs(c, s);
	if (!y->name || y->sc == SC_STATIC || (y->kind != S_FUNC && y->kind != S_VAR) || (a && a->alias))
		return 0;
	if (a && a->dll == CC_DLLIMPORT)
		return WIN64_IMP;
	if ((y->flags & SF_WEAK) && !(y->flags & SF_DEFINED))
		return WIN64_REFPTR;
	if (y->kind == S_FUNC || (y->flags & (SF_DEFINED | SF_TENTATIVE)))
		return 0;
	if (a && a->visibility) {
		const char *v = c->strs[a->visibility - 1].s;

		if (!strcmp(v, "hidden") || !strcmp(v, "protected"))
			return 0;
	}
	return WIN64_REFPTR;
}
