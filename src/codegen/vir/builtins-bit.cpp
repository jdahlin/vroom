/* Lower integer bit builtins to VIR. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "common/literal.h"
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* Keep baseline x86 population counts in SSA so the allocator can place the
 * intermediates and the optimizer can simplify constant and masked inputs. */
static uint32_t virbuild_population_count(codegen_context *e, uint32_t ty,
                                          uint32_t x)
{
    if (!CODEGEN_IS_X64(e->compile_context) ||
        x86_feature_has(cc_x86_features_get(e->compile_context,
                                            e->walk_state->function_symbol),
                        X86_FEATURE_POPCNT))
        return virbuild_insn_append(e, VIR_OP_POPCNT, ty, 0, x, 0, 0);

    uint32_t m1 = virbuild_constant_integer(e, ty, INT64_C(0x5555555555555555));
    uint32_t m2 = virbuild_constant_integer(e, ty, INT64_C(0x3333333333333333));
    uint32_t m4 = virbuild_constant_integer(e, ty, INT64_C(0x0f0f0f0f0f0f0f0f));
    uint32_t ones =
        virbuild_constant_integer(e, ty, INT64_C(0x0101010101010101));
    uint32_t t = virbuild_insn_append(e, VIR_OP_LSHR, ty, 0, x,
                                      virbuild_constant_integer(e, ty, 1), 0);
    t = virbuild_insn_append(e, VIR_OP_AND, ty, 0, t, m1, 0);
    x = virbuild_insn_append(e, VIR_OP_SUB, ty, 0, x, t, 0);
    t = virbuild_insn_append(e, VIR_OP_LSHR, ty, 0, x,
                             virbuild_constant_integer(e, ty, 2), 0);
    t = virbuild_insn_append(e, VIR_OP_AND, ty, 0, t, m2, 0);
    x = virbuild_insn_append(e, VIR_OP_AND, ty, 0, x, m2, 0);
    x = virbuild_insn_append(e, VIR_OP_ADD, ty, 0, x, t, 0);
    t = virbuild_insn_append(e, VIR_OP_LSHR, ty, 0, x,
                             virbuild_constant_integer(e, ty, 4), 0);
    x = virbuild_insn_append(e, VIR_OP_ADD, ty, 0, x, t, 0);
    x = virbuild_insn_append(e, VIR_OP_AND, ty, 0, x, m4, 0);
    x = virbuild_insn_append(e, VIR_OP_MUL, ty, 0, x, ones, 0);
    return virbuild_insn_append(
        e, VIR_OP_LSHR, ty, 0, x,
        virbuild_constant_integer(e, ty, ty == VIR_TYPE_I32 ? 24 : 56), 0);
}

static uint32_t virbuild_population_parity(codegen_context *e, uint32_t ty,
                                           uint32_t x)
{
    return virbuild_insn_append(e, VIR_OP_AND, ty, 0,
                                virbuild_population_count(e, ty, x),
                                virbuild_constant_integer(e, ty, 1), 0);
}

static uint32_t virbuild_bswap16(codegen_context *e, uint32_t, uint32_t x)
{
    x = virbuild_insn_append(e, VIR_OP_BSWAP, VIR_TYPE_I32, 0,
                             virbuild_value_convert(e, x, VIR_TYPE_I32), 0, 0);
    return virbuild_insn_append(e, VIR_OP_LSHR, VIR_TYPE_I32, 0, x,
                                virbuild_constant_integer(e, VIR_TYPE_I32, 16),
                                0);
}

static uint32_t virbuild_count_leading_zeros(codegen_context *e, uint32_t ty,
                                             uint32_t x)
{
    return virbuild_insn_append(e, VIR_OP_CLZ, ty, VIR_INSN_FLAG_NZ, x, 0, 0);
}

static uint32_t virbuild_count_trailing_zeros(codegen_context *e, uint32_t ty,
                                              uint32_t x)
{
    return virbuild_insn_append(e, VIR_OP_CTZ, ty, VIR_INSN_FLAG_NZ, x, 0, 0);
}

static uint32_t virbuild_bswap(codegen_context *e, uint32_t ty, uint32_t x)
{
    return virbuild_insn_append(e, VIR_OP_BSWAP, ty, 0, x, 0, 0);
}

VPRIV codegen_value virbuild_bitop(codegen_context *e, int bi, int size,
                                   codegen_value a)
{
    typedef uint32_t (*lower_bit_builtin)(codegen_context *, uint32_t,
                                          uint32_t);
    static const struct {
        int id;
        lower_bit_builtin lower;
    } bindings[] = {
        {CC_BUILTIN_CLZ, virbuild_count_leading_zeros},
        {CC_BUILTIN_CLZL, virbuild_count_leading_zeros},
        {CC_BUILTIN_CLZLL, virbuild_count_leading_zeros},
        {CC_BUILTIN_CTZ, virbuild_count_trailing_zeros},
        {CC_BUILTIN_CTZL, virbuild_count_trailing_zeros},
        {CC_BUILTIN_CTZLL, virbuild_count_trailing_zeros},
        {CC_BUILTIN_POPCOUNT, virbuild_population_count},
        {CC_BUILTIN_POPCOUNTL, virbuild_population_count},
        {CC_BUILTIN_POPCOUNTLL, virbuild_population_count},
        {CC_BUILTIN_BSWAP32, virbuild_bswap},
        {CC_BUILTIN_BSWAP64, virbuild_bswap},
        {CC_BUILTIN_BSWAP16, virbuild_bswap16},
        {CC_BUILTIN_PARITY, virbuild_population_parity},
        {CC_BUILTIN_PARITYL, virbuild_population_parity},
        {CC_BUILTIN_PARITYLL, virbuild_population_parity},
    };
    lower_bit_builtin lower = NULL;
    for (unsigned i = 0; i < sizeof bindings / sizeof bindings[0]; i++) {
        if (bindings[i].id == bi) {
            lower = bindings[i].lower;
            break;
        }
    }
    if (!lower)
        virbuild_fail(e, VIRBUILD_FAILURE(unsupported_builtin));

    uint32_t ty = virbuild_integer_type(size);
    uint32_t x = virbuild_walker_value_get(e, &a, ty);
    int r = virbuild_result_register_pick(e, &a, NULL);
    virbuild_register_write(e, r, lower(e, ty, x));
    return VCC_LITERAL(codegen_value, .kind = CODEGEN_VALUE_REGISTER,
                       .register_number = (uint8_t)r);
}
