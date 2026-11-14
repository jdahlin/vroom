/* A dense switch as a jump table: bounds check, then jmp *(table + 4 * index) with
 * label-difference words after the jump (in .text, no relocations). */
#include "x64-internal.h"

VPRIV void x64_switch_table(codegen_context *e, int source, int size, const em_switch_case *cases, uint32_t n, uint32_t dflt)
{
	i128 lo = cases[0].lo, span = cases[n - 1].hi - lo;
	uint32_t tab = walk_label_new(e);
	int index = x64_treg(e);
	codegen_value offset = { .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)index };

	x64_record_emit_three_registers(e, X64_OP_MOV, size, index, source, X64_REGISTER_NONE);
	if (lo)
		offset = x64_bin(e, LEX_TOKEN_MINUS, size, false, offset, codegen_value_constant(lo));
	x64_cmpbr(e, LEX_TOKEN_GREATER, size, false, offset, codegen_value_constant(span), dflt);
	if (size == 4)	/* the 32-bit result is zero-extended; the compare saw it unsigned */
		x64_record_emit_three_registers(e, X64_OP_MOV, 4, offset.register_number, offset.register_number, X64_REGISTER_NONE);
	x64_record_emit_three_registers(e, X64_OP_LEAL, 8, R11, X64_REGISTER_NONE, X64_REGISTER_NONE)->label = tab;
	x64_record_emit_three_registers(e, X64_OP_LEASCALE, 8, offset.register_number, R11, offset.register_number)->shift = 2;
	x64_record_emit_three_registers(e, X64_OP_LD, 4, offset.register_number, offset.register_number, X64_REGISTER_NONE)->condition = 1;
	x64_record_emit_three_registers(e, X64_OP_ADD, 8, R11, R11, offset.register_number);
	x64_record_emit_three_registers(e, X64_OP_JMPR, 8, X64_REGISTER_NONE, R11, X64_REGISTER_NONE);
	x64_tfree(e, offset.register_number);
	x64_record_emit(e, X64_OP_LABEL)->label = tab;
	uint32_t k = 0;
	for (i128 v = lo; v <= lo + span; v++) {
		while (k < n && cases[k].hi < v)
			k++;
		record *w = x64_record_emit(e, X64_OP_JTE);
		w->label = k < n && cases[k].lo <= v ? cases[k].target : dflt;
		w->reference = tab;
	}
	e->is_reachable = false;
}
