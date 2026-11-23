/* arm64 lowering of VIR: the switch instruction. */

#include "common/literal.h"
#include "lowering-internal.h"

VPRIV void a64_vir_switch_lower(a64_vir_context *L, uint32_t b,
                                const vir_insn *in)
{
    codegen_context *e = L->context;
    const vir_function *f = L->function;
    int x = a64_vir_value_is_64_bit(f, in->a),
        r = a64_vir_operand_general_register(L, 0, R_X16);
    uint32_t n = vir_switch_count(f, in);
    uint32_t dl = a64_vir_successor_label(L, b, 0, in->b);
    uint64_t span, lo;

    if (vir_switch_is_dense(f, in, &lo, &span) && !e->unit->cleanups_disabled) {
        uint32_t tab = a64_label_new(e), off = e->jump_table_label_count;
        int ix = r;

        /* the index: the selector itself from 0 (a word through uxtw), else x17; never x16,
		 * which a64_jump_tables_expand' adr writes first, nor zr */
        if (lo)
            a64_record_emit_immediate(e, A64_OP_SUBI, x, ix = R_X17, r,
                                      (int64_t)lo);
        else if (r == R_X16 || r >= 31)
            a64_record_emit_three_registers(e, A64_OP_MOV, x, ix = R_X17, r, 0);
        a64_record_emit_immediate(e, A64_OP_SUBSI, x, ZR, ix, (int64_t)span);
        a64_branch_condition_emit(e, A64_CONDITION_HI, dl);
        e->jump_table_labels = (uint32_t *)vvector_reserve(
            e->jump_table_labels, &e->jump_table_label_capacity,
            (size_t)e->jump_table_label_count + span + 1,
            sizeof *e->jump_table_labels);
        e->jump_tables = (struct codegen_jump_table *)vvector_reserve(
            e->jump_tables, &e->jump_table_capacity,
            (size_t)e->jump_table_count + 1, sizeof *e->jump_tables);
        uint32_t k = 0, l = 0;

        for (uint64_t v = lo; v <= lo + span; v++) {
            while (k < n && vir_switch_case_get(f, in, k).hi < v)
                k++, l = 0;
            uint32_t t;

            if (k < n && vir_switch_case_get(f, in, k).lo <= v) {
                if (!l)
                    l = a64_vir_successor_label(
                        L, b, 1 + k, vir_switch_case_get(f, in, k).block);
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
        e->jump_tables[e->jump_table_count++] = VCC_LITERAL(
            struct codegen_jump_table, tab, off, (uint32_t)span + 1);
        a64_record *i = a64_record_emit(e, A64_OP_JT);
        i->rd = (uint8_t)ix;
        i->condition = ix != R_X17 && !x ? 2 : 0;
        i->label = tab;
        e->is_reachable = false;
        return;
    }
    for (uint32_t k = 0; k < n; k++) {
        vir_switch_case c = vir_switch_case_get(f, in, k);
        uint64_t a = c.lo, h = c.hi;
        uint32_t t = a64_vir_successor_label(L, b, 1 + k, c.block);

        if (a == h) {
            a64_record_emit_immediate(e, A64_OP_SUBSI, x, ZR, r,
                                      x ? (int64_t)a : (int64_t)(int32_t)a);
            a64_branch_condition_emit(e, A64_CONDITION_EQ, t);
        } else {
            if (r == R_X17)
                a64_vir_fail(L, VIRBUILD_FAILURE(switch_lowering));
            a64_record_emit_immediate(e, A64_OP_SUBI, x, R_X17, r, (int64_t)a);
            a64_record_emit_immediate(e, A64_OP_SUBSI, x, ZR, R_X17,
                                      (int64_t)(h - a));
            a64_branch_condition_emit(e, A64_CONDITION_LS, t);
        }
    }
    a64_jump_emit(e, dl);
}
