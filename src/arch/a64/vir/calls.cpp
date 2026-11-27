/* arm64 lowering of VIR: calls, sibling calls and parameters. */

#include "common/attributes.h"
#include "lowering-internal.h"

VPRIV void a64_vir_call_lower(a64_vir_context *L, uint32_t b, uint32_t i,
                              const vir_insn *in)
{
    codegen_context *e = L->context;
    const vir_function *f = L->function;
    uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[in->b]);
    a64_vir_argument *a;
    int32_t stk;

    if (ng + 1 > L->operand_count)
        a64_vir_fail(L, VIRBUILD_FAILURE(call_operands));
    a = (a64_vir_argument *)(__builtin_alloca(sizeof(a64_vir_argument) *
                                              (ng + 1)));
    uint8_t gm = 0; /* the x registers the arguments are in */

    a64_vir_arguments_assign(CODEGEN_IS_DARWIN(e->compile_context),
                             &f->extra[in->b], a, ng, &stk);
    for (uint32_t k = 0; k < ng; k++) {
        uint8_t r = L->operand_registers[1 + k];

        if (a[k].reg != VIR_REGISTER_NONE) {
            if (r != a[k].reg)
                a64_vir_fail(L, VIRBUILD_FAILURE(argument_not_in_register));
            if (!a64_vir_register_is_float(r) &&
                a64_vir_general_register(r) < 8)
                gm |= (uint8_t)(1u << a64_vir_general_register(r));
            else if (!a64_vir_register_is_float(r))
                gm = 0xff; /* (not an argument register: assume all) */
            continue;
        }
        {
            int sz = a64_size_log2(a[k].size), rs;

            if (r != VIR_REGISTER_NONE && a64_vir_register_is_float(r) &&
                sz >= 2) {
                a64_record_emit_memory(e, A64_OP_STF, sz, 0,
                                       a64_vir_vector_register(r), SP,
                                       a[k].stack_offset);
                continue;
            }
            if (r != VIR_REGISTER_NONE && a64_vir_register_is_float(r)) {
                a64_record_emit_word(
                    e, a64_word_fmov_from_vector(a[k].size == 8, R_X16,
                                                 a64_vir_vector_register(r)));
                rs = R_X16;
            } else {
                rs = a64_vir_operand_general_register(L, 1 + k, R_X16);
            }
            a64_record_emit_memory(e, A64_OP_ST, sz, 0, rs, SP,
                                   a[k].stack_offset);
        }
    }
    bool musttail =
        in->op == VIR_OP_CALL && (in->flags & VIR_INSN_FLAG_CALL_MUSTTAIL);

    if (musttail && stk) /* the direct emitter says why */
        a64_vir_fail(L, VIRBUILD_FAILURE(musttail));
    if (musttail ||
        (!stk && virbuild_call_is_sibling(L->context, L->function, L->ret32,
                                          L->tail, b, i))) {
        a64_record *t;

        if (L->allocation->callee[1] &&
            L->operand_registers[0] !=
                VIR_REGISTER_NONE) /* (the callee's address may be in x16: through x17) */
            L->frame_is_at_risk = true;
        a64_vir_callee_saved_float_save(
            L, true,
            L->operand_registers[0] != VIR_REGISTER_NONE ? R_X17 : R_X16);
        t = a64_record_emit(e, A64_OP_TAIL);
        t->shift = L->is_before_save_point;

        if (L->operand_registers[0] == VIR_REGISTER_NONE) {
            const vir_insn *k = &f->insns[in->a];

            if (k->op != VIR_OP_SYMADDR || k->b || k->c)
                a64_vir_fail(
                    L, VIRBUILD_FAILURE(indirect_callee_without_register));
            t->condition = 2;
            t->reference = f->insns[in->a].a;
            codegen_reference_note(e, t->reference);
        } else {
            t->condition = 3;
            t->rn = (uint8_t)a64_vir_general_register(L->operand_registers[0]);
        }
        e->is_reachable = false;
        L->skip = i + 1;
        return;
    }
    if (stk > e->outgoing_argument_bytes_max)
        e->outgoing_argument_bytes_max = stk;
    e->has_calls = true;
    if (L->operand_registers[0] == VIR_REGISTER_NONE) {
        const vir_insn *k = &f->insns[in->a];

        if (k->op != VIR_OP_SYMADDR || k->b || k->c)
            a64_vir_fail(L, VIRBUILD_FAILURE(indirect_callee_without_register));
        codegen_reference_note(e, k->a);
        a64_record *c = a64_record_emit(e, A64_OP_BL);

        c->reference = k->a;
        c->condition = A64_CALL_ARGUMENT_REGISTERS, c->shift = gm;
    } else {
        a64_record *c = a64_record_emit(e, A64_OP_BLR);

        c->rn = (uint8_t)a64_vir_general_register(L->operand_registers[0]);
        c->condition = A64_CALL_ARGUMENT_REGISTERS, c->shift = gm;
    }
}

VPRIV void a64_vir_parameter_lower(a64_vir_context *L, const vir_insn *in,
                                   uint8_t res)
{
    codegen_context *e = L->context;
    const vir_function *f = L->function;
    uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[f->signature]);
    a64_vir_argument *a;
    int32_t stk;

    if (ng > A64_VIR_OPERAND_MAX || in->a >= ng)
        a64_vir_fail(L, VIRBUILD_FAILURE(lowering_parameter));
    a = (a64_vir_argument *)(__builtin_alloca(sizeof(a64_vir_argument) * ng));
    a64_vir_arguments_assign(CODEGEN_IS_DARWIN(e->compile_context),
                             &f->extra[f->signature], a, ng, &stk);
    if (a[in->a].reg != VIR_REGISTER_NONE) {
        if (res != VIR_REGISTER_NONE && res != a[in->a].reg)
            a64_vir_fail(L, VIRBUILD_FAILURE(parameter_not_in_register));
        return;
    }
    if (res == VIR_REGISTER_NONE)
        return;
    if (a64_vir_register_is_float(res)) {
        a64_record_emit_memory(e, A64_OP_LDF, in->type == VIR_TYPE_F64 ? 3 : 2,
                               0, a64_vir_vector_register(res), R_FP,
                               16 + a[in->a].stack_offset);
    } else {
        a64_record_emit_memory(e, A64_OP_LD, a64_size_log2(a[in->a].size), 0,
                               a64_vir_general_register(res), R_FP,
                               16 + a[in->a].stack_offset);
    }
}
