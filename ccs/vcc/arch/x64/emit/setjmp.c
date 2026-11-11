/* x86-64 nonlocal jumps: private resume snapshots keep the public
 * three-pointer builtin buffer layout compatible with clang. */
#include "x64impl.h"

/* buf: rbp, the resume label, rsp (as clang); 0 now, 1 at the label */
VPRIV val x64_setjmp(ecx *e, int rb)
{
	uint32_t resume = wk_newlabel(e), done = wk_newlabel(e);
	lval b0 = { .k = L_REG, .r = (uint8_t)rb }, b1 = b0, b2 = b0;
	rec *i;
	int r;
	uint32_t persistent = (1u << RBX) | (1u << R12) | (1u << R13) | (1u << R14) | (1u << R15);
	uint32_t saved = (e->x64->live & (X_TEMPS | X_FTEMPS) & ~(1u << rb)) | persistent;
	int64_t slots[32] = {0};

	/* Besides live expression temporaries, preserve every non-frame ABI
	   callee-save, including inherited caller values. A skipped callee can change
	   these even when this function did not promote a single local. Slots
	   belong to the destination frame and survive restoration of rbp/rsp.
	   The buffer-address temporary is deliberately excluded. R12 may belong
	   to a caller, or hold this function's reconstructed aligned frame base. */
	for (int q = 0; q < 32; q++) if (saved & (1u << q)) {
		int size = IS_XMM(q) ? 16 : 8;
		slots[q] = wk_slot(e, size, size);
		lval slot = { .k = L_FRAME, .off = slots[q] };
		x64_mem(e, X_ST, size, q, &slot, false);
	}

	b1.off = 8, b2.off = 16;
	x64_mem(e, X_ST, 8, RBP, &b0, false);
	i = x64_put(e, X_LEAL);
	i->r[0] = R11;
	i->label = resume;
	x64_mem(e, X_ST, 8, R11, &b1, false);
	x64_mem(e, X_ST, 8, RSP, &b2, false);
	x64_tfree(e, rb);
	r = x64_treg(e);
	x64_movi(e, r, 0);
	x64_jump(e, done);
	x64_label(e, resume);
	/* Frame alignment may be discovered after this site, including in an
	   inlined body. Rebuild its base before loading any private snapshots. */
	x64_put(e, X_RESUME_FRAME);
	x64_movi(e, r, 1);
	x64_label(e, done);
	for (int q = 0; q < 32; q++) if (saved & (1u << q)) {
		lval slot = { .k = L_FRAME, .off = slots[q] };
		x64_mem(e, X_LD, IS_XMM(q) ? 16 : 8, q, &slot, false);
	}
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV void x64_longjmp(ecx *e, int rb)
{
	lval b0 = { .k = L_REG, .r = (uint8_t)rb }, b1 = b0, b2 = b0;

	b1.off = 8, b2.off = 16;
	x64_mem(e, X_LD, 8, R11, &b1, false);
	x64_mem(e, X_LD, 8, RSP, &b2, false);
	x64_mem(e, X_LD, 8, RBP, &b0, false);
	x64_op3(e, X_JMPR, 8, XNOREG, R11, XNOREG);
	x64_tfree(e, rb);
}

