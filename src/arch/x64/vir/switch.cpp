/* x86-64 lowering of VIR: the switch instruction (dense table and range chain). */

#include "lowering-internal.h"

/* Switch edges retain their allocator edits in separate stubs. Dense tables use
 * signed offsets from a RIP-relative base, so the dispatch stays position independent. */
VPRIV void x64_vir_constant_compare_emit(x64_vir_context *L, int reg, int size,
                                         uint64_t value)
{
    int64_t k = size == 4 ? (int64_t)(int32_t)value : (int64_t)value;
    if (k == (int32_t)k)
        x64_record_emit_three_registers(L->context, X64_OP_CMPI, size,
                                        X64_REGISTER_NONE, reg,
                                        X64_REGISTER_NONE)
            ->immediate = k;
    else {
        x64_record_emit_move_immediate(L->context, R11, k);
        x64_record_emit_three_registers(L->context, X64_OP_CMP, size,
                                        X64_REGISTER_NONE, reg, R11);
    }
}

VPRIV void x64_vir_switch_lower(x64_vir_context *L, uint32_t block,
                                const vir_insn *in, int selector)
{
    codegen_context *e = L->context;
    vir_function *f = L->function;
    bool wide = f->insns[in->a].type == VIR_TYPE_I64;
    uint32_t size = wide ? 8 : 4, n = vir_switch_count(f, in);
    uint32_t *labels = (uint32_t *)(malloc(((size_t)n + 1) * sizeof *labels));
    if (!labels)
        abort();
    for (uint32_t k = 0; k <= n; k++)
        labels[k] = x64_vir_edge_target(
            L, block, k, k ? vir_switch_case_get(f, in, k - 1).block : in->b);
    uint64_t lo, span;
    if (vir_switch_is_dense(f, in, &lo, &span)) {
        uint32_t table = walk_label_new(e);
        if (selector != R10 || (size == 4 && !x64_last_u32(e, R10)))
            x64_record_emit_three_registers(e, X64_OP_MOV, size, R10, selector,
                                            X64_REGISTER_NONE);
        if (lo) {
            x64_record_emit_move_immediate(
                e, R11, size == 4 ? (int64_t)(int32_t)lo : (int64_t)lo);
            x64_record_emit_three_registers(e, X64_OP_SUB, size, R10, R10, R11);
        }
        x64_vir_constant_compare_emit(L, R10, size, span);
        record *r = x64_record_emit(e, X64_OP_JCC);
        r->condition = X64_CONDITION_A;
        r->label = labels[0];
        x64_record_emit_three_registers(e, X64_OP_LEAL, 8, R11,
                                        X64_REGISTER_NONE, X64_REGISTER_NONE)
            ->label = table;
        r = x64_record_emit_three_registers(e, X64_OP_LDX, 4, R10, R11,
                                            X64_REGISTER_NONE);
        r->registers[3] = R10;
        r->shift = 2;
        r->condition = 1;
        x64_record_emit_three_registers(e, X64_OP_ADD, 8, R10, R10, R11);
        x64_record_emit_three_registers(e, X64_OP_JMPR, 8, X64_REGISTER_NONE,
                                        R10, X64_REGISTER_NONE);
        x64_label(e, table);
        uint32_t k = 0;
        for (uint64_t offset = 0; offset <= span; offset++) {
            uint64_t v = lo + offset;
            while (k < n && vir_switch_case_get(f, in, k).hi < v)
                k++;
            r = x64_record_emit(e, X64_OP_JTE);
            r->label = k < n && vir_switch_case_get(f, in, k).lo <= v
                           ? labels[k + 1]
                           : labels[0];
            r->reference = table;
        }
    } else {
        for (uint32_t k = 0; k < n; k++) {
            vir_switch_case c = vir_switch_case_get(f, in, k);
            uint64_t a = c.lo, h = c.hi;
            int reg = selector;
            if (a != h) {
                x64_record_emit_move_immediate(
                    e, R11, size == 4 ? (int64_t)(int32_t)a : (int64_t)a);
                x64_record_emit_three_registers(e, X64_OP_SUB, size, R10,
                                                selector, R11);
                reg = R10;
            }
            x64_vir_constant_compare_emit(L, reg, size, a == h ? a : h - a);
            record *r = x64_record_emit(e, X64_OP_JCC);
            r->condition = a == h ? X64_CONDITION_E : X64_CONDITION_BE;
            r->label = labels[k + 1];
        }
        x64_jump(e, labels[0]);
    }
    free(labels);
}
