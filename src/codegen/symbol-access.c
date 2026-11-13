/* vcc code generator: whether a symbol is reached through the GOT */
#include "emitimpl.h"

VPRIV bool sym_needs_got(cc *c, uint32_t ref)
{
	if (RKIND(ref) == RK_NAME)
		return DARWIN(c);
	if (RKIND(ref) != RK_SYM)
		return false;
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
