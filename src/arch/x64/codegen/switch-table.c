/* A dense switch as a jump table: bounds check, then jmp *(table + 4 * index) with
 * label-difference words after the jump (in .text, no relocations). */
#include "x64impl.h"

VPRIV void x64_switch_table(codegen_context *e, int source, int size, const em_switch_case *cases, uint32_t n, uint32_t dflt)
{
	i128 lo = cases[0].lo, span = cases[n - 1].hi - lo;
	uint32_t tab = walk_label_new(e);
	int index = x64_treg(e);
	codegen_value offset = { .k = CODEGEN_VALUE_REGISTER, .r = (uint8_t)index };

	x64_op3(e, X_MOV, size, index, source, XNOREG);
	if (lo)
		offset = x64_bin(e, LEX_TOKEN_MINUS, size, false, offset, codegen_value_constant(lo));
	x64_cmpbr(e, LEX_TOKEN_GREATER, size, false, offset, codegen_value_constant(span), dflt);
	if (size == 4)	/* the 32-bit result is zero-extended; the compare saw it unsigned */
		x64_op3(e, X_MOV, 4, offset.r, offset.r, XNOREG);
	x64_op3(e, X_LEAL, 8, R11, XNOREG, XNOREG)->label = tab;
	x64_op3(e, X_LEASCALE, 8, offset.r, R11, offset.r)->sh = 2;
	x64_op3(e, X_LD, 4, offset.r, offset.r, XNOREG)->cond = 1;
	x64_op3(e, X_ADD, 8, R11, R11, offset.r);
	x64_op3(e, X_JMPR, 8, XNOREG, R11, XNOREG);
	x64_tfree(e, offset.r);
	x64_put(e, X_LABEL)->label = tab;
	uint32_t k = 0;
	for (i128 v = lo; v <= lo + span; v++) {
		while (k < n && cases[k].hi < v)
			k++;
		record *w = x64_put(e, X_JTE);
		w->label = k < n && cases[k].lo <= v ? cases[k].target : dflt;
		w->ref = tab;
	}
	e->reach = false;
}
