/* arm64 lowering of VIR: the switch instruction. */

#include "lowering-internal.h"

VPRIV void a64_vir_switch_lower(a64_vir_context *L, uint32_t b, const vir_insn *in)
{
	codegen_context *e = L->context;
	const vir_function *f = L->function;
	int x = a64_vir_value_is_64_bit(f, in->a), r = a64_vir_operand_general_register(L, 0, R_X16);
	uint32_t n = f->extra[in->c], stride = x ? 5 : 3;
	const uint32_t *cw = &f->extra[in->c + 1];
	uint32_t dl = a64_vir_successor_label(L, b, 0, in->b);
	uint64_t span = 0, lo = 0, cnt = 0;

	if (n) {
		lo = x ? (uint64_t)cw[1] << 32 | cw[0] : cw[0];
		uint64_t hi = x ? (uint64_t)cw[stride * (n - 1) + 3] << 32 | cw[stride * (n - 1) + 2] : cw[stride * (n - 1) + 1];

		span = hi - lo;
		for (uint32_t k = 0; k < n; k++) {
			uint64_t a = x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k];
			uint64_t h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1];

			cnt += h - a >= 64 ? 64 : h - a + 1;	/* (h - a + 1 wraps to 0 for a range of all 2^64 values) */
		}
	}
	if (n >= 6 && span < 4096 && span + 1 <= 4 * cnt && !e->unit->cleanups_disabled) {
		uint32_t tab = a64_label_new(e), off = e->jump_table_label_count;
		int ix = r;

		/* the index: the selector itself from 0 (a word through uxtw), else x17; never x16,
		 * which a64_jump_tables_expand' adr writes first, nor zr */
		if (lo)
			a64_record_emit_immediate(e, A64_OP_SUBI, x, ix = R_X17, r, (int64_t)lo);
		else if (r == R_X16 || r >= 31)
			a64_record_emit_three_registers(e, A64_OP_MOV, x, ix = R_X17, r, 0);
		a64_record_emit_immediate(e, A64_OP_SUBSI, x, ZR, ix, (int64_t)span);
		a64_branch_condition_emit(e, A64_CONDITION_HI, dl);
		if (e->jump_table_label_count + (uint32_t)span + 1 > e->jump_table_label_capacity) {
			e->jump_table_label_capacity = (e->jump_table_label_count + (uint32_t)span + 1) * 2;
			e->jump_table_labels = realloc(e->jump_table_labels, (size_t)e->jump_table_label_capacity * sizeof *e->jump_table_labels);
		}
		if (e->jump_table_count == e->jump_table_capacity) {
			e->jump_table_capacity = e->jump_table_capacity ? e->jump_table_capacity * 2 : 4;
			e->jump_tables = realloc(e->jump_tables, (size_t)e->jump_table_capacity * sizeof *e->jump_tables);
		}
		if (!e->jump_table_labels || !e->jump_tables)
			abort();
		uint32_t k = 0, l = 0;

		for (uint64_t v = lo; v <= lo + span; v++) {
			uint64_t h;

			while (k < n && (h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1]) < v)
				k++, l = 0;
			uint64_t a = k < n ? (x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k]) : 0;
			uint32_t t;

			if (k < n && a <= v) {
				if (!l)
					l = a64_vir_successor_label(L, b, 1 + k, cw[stride * k + stride - 1]);
				t = l;
			} else {
				t = dl;
			}
			e->jump_table_labels[e->jump_table_label_count++] = t;
			if (!(e->label_reference_counts[t] & CODEGEN_LABEL_EXTERNAL)) {
				e->label_reference_counts[t]++;
				e->label_reference_counts[t] |= CODEGEN_LABEL_EXTERNAL;
			}
		}
		e->jump_tables[e->jump_table_count++] = (struct codegen_jump_table){ tab, off, (uint32_t)span + 1 };
		a64_record *i = a64_record_emit(e, A64_OP_JT);
		i->rd = (uint8_t)ix;
		i->cond = ix != R_X17 && !x ? 2 : 0;
		i->label = tab;
		e->is_reachable = false;
		return;
	}
	for (uint32_t k = 0; k < n; k++) {
		uint64_t a = x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k];
		uint64_t h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1];
		uint32_t t = a64_vir_successor_label(L, b, 1 + k, cw[stride * k + stride - 1]);

		if (a == h) {
			a64_record_emit_immediate(e, A64_OP_SUBSI, x, ZR, r, x ? (int64_t)a : (int64_t)(int32_t)a);
			a64_branch_condition_emit(e, A64_CONDITION_EQ, t);
		} else {
			if (r == R_X17)
				a64_vir_fail(L, VIRBUILD_FAILURE(switch_lowering));
			a64_record_emit_immediate(e, A64_OP_SUBI, x, R_X17, r, (int64_t)a);
			a64_record_emit_immediate(e, A64_OP_SUBSI, x, ZR, R_X17, (int64_t)(h - a));
			a64_branch_condition_emit(e, A64_CONDITION_LS, t);
		}
	}
	a64_jump_emit(e, dl);
}
