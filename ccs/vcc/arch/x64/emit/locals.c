/* x86 persistent scalar homes. Selection and escape policy live in opt/locals.c;
   this file only supplies physical homes and preserves the System V ABI. */
#include "x64impl.h"

/* A home adds a save and restore; a parameter also needs an entry load.
   Require at least four weighted references to repay that memory traffic.
   Loop references already carry the shared prepass's loop weight. */
static bool x64_local_profitable(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	cval cv;

	if (e->E->luse[s] < 4)
		return false;
	/* wk_decl binds these constants without any storage. Reserving a physical
	   home would add callee saves while removing no memory accesses. */
	if (!(y->flags & SF_PARAM) && y->sc != SC_REGISTER && y->body &&
	    !(e->E->sflags[s] & (SA_WRITTEN | SA_CWRITE)) &&
	    !side_effects(c, y->body) && cc_eval(c, y->body, &cv) && cv.kind == CV_INT)
		return false;
	return true;
}

/* Inline asm already saves every allocated or explicitly clobbered register
   and restores it on fallthrough and all goto edges (asm.c). Persistent homes
   therefore remain usable even when another asm names these same registers. */
VPRIV void x64_locals_begin(ecx *e, uint32_t s)
{
	static const uint8_t regs[] = { RBX, R13, R14, R15 };
	xst *X = e->x64;
	unsigned n = sizeof regs;

	X->nlreg = 0;
	if (!e->E->o->opt || e->E->noopt || !e->E->nlreg)
		return;
	if (n > (unsigned)e->E->nlreg) n = (unsigned)e->E->nlreg;
	unsigned selected = em_select_locals(e, s, X->lsym, n, em_local_gpr_type);
	/* At most four profitability checks; no second walk or candidate buffer.
	   Filtered homes remain unused; refilling can move into shared selection
	   if measurements justify a richer profitability interface. */
	for (unsigned i = 0; i < selected; i++)
		if (x64_local_profitable(e, X->lsym[i]))
			X->lsym[X->nlreg++] = X->lsym[i];
	for (unsigned i = 0; i < X->nlreg; i++) {
		X->lreg[i] = regs[i];
		X->lsave[i] = wk_slot(e, 8, 8);
		lval save = { .k = L_FRAME, .off = X->lsave[i], .align = 8 };

		x64_store(e, X->lreg[i], &save, 8);
	}
}

/* Arguments have first passed through their normal ABI homes. No incoming
   stack displacement changes: callee saves use ordinary local slots. */
VPRIV void x64_locals_params(ecx *e)
{
	xst *X = e->x64;
	wkst *W = e->wk;

	for (unsigned i = 0; i < X->nlreg; i++) {
		uint32_t s = X->lsym[i];

		if (W->sgen[s] == W->gen) {
			lval from = { .k = L_FRAME, .off = W->soff[s], .align = 8 };
			qty ty = e->c->syms[s].ty;

			x64_load(e, X->lreg[i], &from, wk_size(e->c, ty), wk_signed(e->c, ty));
		}
	}
}

VPRIV bool x64_local(ecx *e, uint32_t s, lval *l)
{
	xst *X = e->x64;

	/* An inlined body can refer to the same symbol in recursive expansion.
	   Its independent bindings must remain in the inliner's frame slots. */
	if (e->wk->inst)
		return false;
	for (unsigned i = 0; i < X->nlreg; i++)
		if (X->lsym[i] == s) {
			*l = (lval){ .k = L_VREG, .r = X->lreg[i], .align = sym_align(e->c, s) };
			return true;
		}
	return false;
}

VPRIV void x64_locals_restore(ecx *e)
{
	xst *X = e->x64;

	for (unsigned i = 0; i < X->nlreg; i++) {
		lval save = { .k = L_FRAME, .off = X->lsave[i], .align = 8 };

		x64_load(e, X->lreg[i], &save, 8, false);
	}
}
