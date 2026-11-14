/* Unsigned reciprocal recipes use baseline scalar A64 instructions only.
 * At most 12 instructions for unsigned32, 14 for unsigned64, including
 * normalization/constants and remainder (64-bit constants need up to four).
 * Unsigned32 recipes without correction merge extraction and final shift. */
#include "../../../codegen/codegen-internal.h"

/* One magic/divisor register plus a distinct quotient for correction/rem.
 * Include materializing a lazy address; never provoke an allocator retry. */
VPRIV bool a64_udivconst_ok(codegen_context *e, codegen_value a, const em_udivconst_recipe *r)
{
	unsigned available = CODEGEN_TEMPORARY_COUNT - (unsigned)__builtin_popcount((e->temporaries_in_use | e->temporaries_reserved) & ((1u << CODEGEN_TEMPORARY_COUNT) - 1));
	unsigned needed = (a.kind != CODEGEN_VALUE_REGISTER) + 1 + (r->add || r->remainder);
	return available >= needed;
}

VPRIV codegen_value a64_udivconst(codegen_context *e, codegen_value a, const em_udivconst_recipe *r)
{
	int x = r->bits == 64;
	int n = toreg(e, &a);
	int q = r->add || r->remainder ? treg(e) : n;
	int m = treg(e);

	/* A lazy narrowing cast may leave high garbage. MADD below is 64-bit. */
	if (!x)
		extend(e, n, n, 4, false);
	if (r->compare) {
		a64_record_emit_move_immediate(e, x, m, (int64_t)r->divisor);
		a64_record_emit_three_registers(e, A64_OP_SUBS, x, ZR, n, m);
		a64_record_emit_condition_set(e, q, A64_CONDITION_HS);
	} else {
		a64_record_emit_move_immediate(e, x, m, (int64_t)r->magic);
		if (x)
			a64_record_emit_three_registers(e, A64_OP_UMULH, 1, q, n, m);
		else
			a64_record_emit_four_registers(e, A64_OP_MADD, 1, q, n, m, ZR);
		if (!r->add) {
			if (!x || r->shift)
				shift_imm(e, A64_OP_LSRV, 1, r->remainder ? q : n, q,
				          (x ? 0 : 32) + r->shift);
		} else {
			if (!x)
				shift_imm(e, A64_OP_LSRV, 1, q, q, 32);
			a64_record_emit_three_registers(e, A64_OP_SUB, x, m, n, q);
			shift_imm(e, A64_OP_LSRV, x, m, m, 1);
			a64_record_emit_three_registers(e, A64_OP_ADD, x, q, m, q);
			if (r->shift)
				shift_imm(e, A64_OP_LSRV, x, r->remainder ? q : n, q, r->shift);
			else if (!r->remainder)
				a64_record_emit_move(e, x, n, q);
		}
	}
	if (r->remainder) {
		if (!r->compare)
			a64_record_emit_move_immediate(e, x, m, (int64_t)r->divisor);
		a64_record_emit_four_registers(e, A64_OP_MSUB, x, n, q, m, n);
	}
	if (q != n)
		tfree(e, q);
	tfree(e, m);
	return a;
}
