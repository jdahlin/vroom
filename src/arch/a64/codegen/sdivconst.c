/* Signed constant division: explicit signed high product and truncation to zero. */
#include "../../../codegen/codegen-internal.h"

VPRIV bool a64_sdivconst_ok(codegen_context *e, codegen_value a, const em_sdivconst_recipe *r)
{
	unsigned available = CODEGEN_TEMPORARY_COUNT - (unsigned)__builtin_popcount((e->temporaries_in_use | e->temporaries_reserved) & ((1u << CODEGEN_TEMPORARY_COUNT) - 1));
	unsigned needed = (a.kind != CODEGEN_VALUE_REGISTER) + 1 + (r->add || r->remainder);
	return available >= needed;
}

VPRIV codegen_value a64_sdivconst(codegen_context *e, codegen_value a, const em_sdivconst_recipe *r)
{
	int x = r->bits == 64, n = toreg(e, &a);
	int q = r->add || r->remainder ? treg(e) : n;
	int m = treg(e);

	if (!x)
		extend(e, n, n, 4, true);
	a64_record_emit_move_immediate(e, 1, m, r->magic);
	if (x)
		a64_record_emit_three_registers(e, A64_OP_SMULH, 1, q, n, m);
	else {
		/* Both inputs are sign-extended32; the full product fits in64. */
		a64_record_emit_four_registers(e, A64_OP_MADD, 1, q, n, m, ZR);
		shift_imm(e, A64_OP_ASRV, 1, q, q, 32);
	}
	if (r->add)
		a64_record_emit_three_registers(e, A64_OP_ADD, x, q, q, n);
	if (r->shift)
		shift_imm(e, A64_OP_ASRV, x, q, q, r->shift);
	shift_imm(e, A64_OP_LSRV, x, m, q, r->bits - 1);
	a64_record_emit_three_registers(e, A64_OP_ADD, x, q, q, m);
	if (r->remainder) {
		a64_record_emit_move_immediate(e, x, m, (int64_t)r->divisor);
		a64_record_emit_four_registers(e, A64_OP_MSUB, x, n, q, m, n);
	} else {
		if (r->negative)
			a64_record_emit_three_registers(e, A64_OP_SUB, x, q, ZR, q);
		if (q != n)
			a64_record_emit_move(e, x, n, q);
	}
	if (q != n)
		tfree(e, q);
	tfree(e, m);
	return a;
}
