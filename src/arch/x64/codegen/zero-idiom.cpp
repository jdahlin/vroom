/* Zero and compare-with-zero idioms, after the two-address pass (the records are what the
 * printer and the direct encoder see): `cmp $0, r` is `test r, r` (same flags but AF, and
 * three bytes shorter) and `mov $0, r` is `xor r, r` where the flags are dead (a 32-bit
 * xor zero-extends and is a dependency-breaking idiom).  What clang and gcc always do, so
 * there is no option to turn it off.
 */
#include "common/literal.h"
#include "x64-internal.h"

#define X_ZERO_SCAN 24

/* the flags are dead after record k: the next record to touch them writes them, with no read
 * before it, within X_ZERO_SCAN records; a label is passed (the path that falls through to
 * it is the one that counts), any other branch, a read or a long scan says no */
static bool x64_zero_flags_dead(const codegen_context *e, uint32_t k)
{
    const x64_lowering_context *x = e->x64_context;

    for (uint32_t j = k + 1; j < x->record_count && j <= k + X_ZERO_SCAN; j++) {
        const record *i = &x->records[j];
        record_effects f;

        if (i->opcode == X64_OP_LABEL)
            continue;
        if (i->opcode >= X64_OP_COUNT || i->opcode == X64_OP_RAW ||
            i->opcode == X64_OP_NAMED || i->opcode == X64_OP_EHMARK)
            return false;
        f = x64_record_effects(e, i);
        if (f.flags & RECORD_EFFECT_READS_FLAGS)
            return false;
        if (f.flags & RECORD_EFFECT_WRITES_FLAGS)
            return true;
        if (f.branch != RECORD_BRANCH_NONE)
            return false;
    }
    return false;
}

VPRIV void x64_zero_idioms(codegen_context *e)
{
    x64_lowering_context *x = e->x64_context;

    for (uint32_t k = 0; k < x->record_count; k++) {
        record *i = &x->records[k];

        if (i->opcode == X64_OP_CMPI && i->immediate == 0 &&
            i->registers[1] < XMM0) {
            i->opcode = X64_OP_TEST;
        } else if (i->opcode == X64_OP_MOVI && i->immediate == 0 &&
                   (i->size == 4 || i->size == 8) && i->registers[0] < XMM0 &&
                   x64_zero_flags_dead(e, k)) {
            i->opcode = X64_OP_XOR;
            i->size = 4;
            i->registers[1] = i->registers[0];
            i->registers[2] = i->registers[0];
        }
    }
}
