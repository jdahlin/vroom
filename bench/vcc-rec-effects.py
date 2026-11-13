#!/usr/bin/env python3
"""Check register/effect contracts used by machine-record optimizations.

This host-only test needs a C compiler and the checkout, no target toolchain.
Use --sanitize to run the same contract checks with ASan and UBSan.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "arch/x64/codegen/effects.c"
#include "arch/x64/codegen/extensions.c"

static record instruction(int op, int size)
{
    return (record){ .opcode = op, .size = size,
                  .registers = { RAX, RCX, RDX, XNOREG } };
}
static int has(record_register_mask value, int reg) { return (value & (UINT64_C(1) << reg)) != 0; }
int main(void)
{
    /* Every existing opcode needs metadata; new unknown records are opaque. */
    for (int op = 0; op < X_NOPS; op++) {
        record i = instruction(op, 8);
        record_effects f = x64_effects(NULL, &i);
        assert(op == X_RAW || f.defines != X_ALL_REGS || f.used != X_ALL_REGS);
        if (f.flags & RECORD_EFFECT_PURE)
            assert(!(f.flags & (RECORD_EFFECT_LOAD | RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER)));
    }
    record i = instruction(X_NOPS, 8);
    record_effects f = x64_effects(NULL, &i);
    assert(f.defines == X_ALL_REGS && f.used == X_ALL_REGS);
    assert((f.flags & RECORD_EFFECT_BARRIER) && !(f.flags & RECORD_EFFECT_PURE));

    /* Scaled LEA is integer address arithmetic, with two explicit sources
       and no flags, memory or implicit register effects. */
    for (int size=4;size<=8;size+=4) {
        i = instruction(X_LEASCALE,size); i.shift=3;
        f = x64_effects(NULL,&i);
        assert(f.defines == X_BIT(RAX) && f.used == (X_BIT(RCX)|X_BIT(RDX)));
        assert(f.flags == RECORD_EFFECT_PURE && f.branch == RECORD_BRANCH_NONE);
        assert(x64_flow_table()[X_LEASCALE] == RECORD_FLOW_NEXT);
    }

    /* Legacy scalar square root retains the destination's upper SIMD lanes
       and can raise an FP exception; it neither branches nor changes flags. */
    for (int size = 4; size <= 8; size += 4) {
        i = (record){ .opcode=X_FSQRT, .size=size, .registers={XMM8,(XMM0 + 1),XNOREG,XNOREG} };
        f = x64_effects(NULL, &i);
        assert(has(f.defines,XMM8) && has(f.used,XMM8) && has(f.used,(XMM0 + 1)));
        assert((f.flags & (RECORD_EFFECT_FLOAT|RECORD_EFFECT_TRAP)) == (RECORD_EFFECT_FLOAT|RECORD_EFFECT_TRAP));
        assert(!(f.flags & (RECORD_EFFECT_PURE|RECORD_EFFECT_WRITES_FLAGS)) && f.branch == RECORD_BRANCH_NONE);
        assert(x64_flow_table()[X_FSQRT] == RECORD_FLOW_NEXT);
    }

    /* SSE4's fixed operands/results are not explicit record registers. */
    i = instruction(X_BLENDVB, 16);
    f = x64_effects(NULL, &i);
    assert(f.defines == X_BIT(XMM0 + 1));
    assert(f.used == (X_BIT(XMM0) | X_BIT(XMM0 + 1) | X_BIT(XMM0 + 2)));
    assert(f.flags == RECORD_EFFECT_PURE && f.branch == RECORD_BRANCH_NONE);
    assert(x64_flow_table()[X_BLENDVB] == RECORD_FLOW_NEXT);
    for (unsigned kind = 0; kind < 4; kind++) {
        i = instruction(X_PCMPSTR, 16); i.condition = kind;
        f = x64_effects(NULL, &i);
        assert(f.defines == X_BIT((kind & 1) ? RCX : XMM0));
        assert(f.used == (X_BIT(XMM0) | X_BIT(XMM0 + 1) |
                        ((kind & 2) ? 0 : X_BIT(RAX) | X_BIT(RDX))));
        assert((f.flags & RECORD_EFFECT_WRITES_FLAGS) && !(f.flags & (RECORD_EFFECT_READS_FLAGS | RECORD_EFFECT_FLOAT)));
        assert(f.branch == RECORD_BRANCH_NONE && x64_flow_table()[X_PCMPSTR] == RECORD_FLOW_NEXT);
    }
    /* A local width proof must not cross labels or mistake a zero-count
       shift/partial/signed write for a full unsigned result. */
    xst state = { .nin=1, .in=&i };
    codegen_context context = { .x64_context=&state };
    i = instruction(X_LD,4);
    assert(x64_last_u32(&context,RAX));
    assert(!x64_last_u32(&context,RCX));
    i.condition=1; assert(!x64_last_u32(&context,RAX));
    i = instruction(X_MOV,1); assert(!x64_last_u32(&context,RAX));
    i.size=4; assert(x64_last_u32(&context,RAX));
    i = instruction(X_ADDI,4); assert(x64_last_u32(&context,RAX));
    i = instruction(X_LEASCALE,4); assert(x64_last_u32(&context,RAX));
    i.size=8; assert(!x64_last_u32(&context,RAX));
    i = instruction(X_SHLI,4); i.immediate=0; assert(!x64_last_u32(&context,RAX));
    i = instruction(X_BSF,4); assert(!x64_last_u32(&context,RAX));
    i = instruction(X_LABEL,4); assert(!x64_last_u32(&context,RAX));
    i = instruction(X_MOVI,8); i.immediate=UINT32_MAX; assert(x64_last_u32(&context,RAX));
    i.immediate=-1; assert(!x64_last_u32(&context,RAX));
    state.nin=0; assert(!x64_last_u32(&context,RAX));

    /* Whole-register liveness must retain high bits of partial GPR writes. */
    i = instruction(X_MOV, 1);
    f = x64_effects(NULL, &i);
    assert(has(f.defines, RAX) && has(f.used, RAX) && has(f.used, RCX));
    i.size = 4;
    f = x64_effects(NULL, &i);
    assert(has(f.defines, RAX) && !has(f.used, RAX));
    i = instruction(X_SETCC, 4);
    f = x64_effects(NULL, &i);
    assert(has(f.defines, RAX) && !has(f.used, RAX) && (f.flags & RECORD_EFFECT_READS_FLAGS));

    /* These printer expansions have hidden scratch registers and state. */
    i = instruction(X_FSETCC, 4);
    f = x64_effects(NULL, &i);
    assert(has(f.defines, R11) && has(f.used, R11));
    assert((f.flags & (RECORD_EFFECT_READS_FLAGS | RECORD_EFFECT_WRITES_FLAGS)) == (RECORD_EFFECT_READS_FLAGS | RECORD_EFFECT_WRITES_FLAGS));
    i = instruction(X_DIV, 8);
    f = x64_effects(NULL, &i);
    assert(has(f.used, RAX) && has(f.used, RDX));
    assert(has(f.defines, RAX) && has(f.defines, RDX) && (f.flags & RECORD_EFFECT_TRAP));
    i = instruction(X_SHL, 8);
    f = x64_effects(NULL, &i);
    assert(has(f.used, RCX) && (f.flags & RECORD_EFFECT_READS_FLAGS)); /* count may be zero */
    i = instruction(X_REP, 1);
    f = x64_effects(NULL, &i);
    assert(has(f.used, RSI) && has(f.defines, RSI) && has(f.defines, RDI) && has(f.defines, RCX));
    assert(f.flags & RECORD_EFFECT_READS_FLAGS); /* direction flag */
    i.condition = 1;
    f = x64_effects(NULL, &i);
    assert(has(f.used, RAX) && !has(f.defines, RAX) && !has(f.defines, RSI));

    /* TLS local-exec preserves flags; initial-exec adds; dynamic calls. */
    i = instruction(X_TLS, 8);
    f = x64_effects(NULL, &i);
    assert(!(f.flags & (RECORD_EFFECT_WRITES_FLAGS | RECORD_EFFECT_CALL)));
    i.condition = 1;
    f = x64_effects(NULL, &i);
    assert((f.flags & RECORD_EFFECT_WRITES_FLAGS) && !(f.flags & RECORD_EFFECT_CALL));
    i.condition = 2;
    f = x64_effects(NULL, &i);
    assert((f.flags & RECORD_EFFECT_CALL) && has(f.defines, R11) && has(f.defines, XMM15));
    assert(has(f.used, RSP) && !has(f.used, RDI)); /* descriptor replaces RDI */

    /* Aligned local slots use R12 even when the record names RBP. */
    walk_state wk = { .frame_alignment = 32 };
    codegen_context e = { .walk_state = &wk };
    i = instruction(X_LD, 8);
    i.registers[1] = RBP; i.offset = -16;
    f = x64_effects(&e, &i);
    assert(has(f.used, R12) && !(f.flags & RECORD_EFFECT_PURE));
    wk.frame_alignment = 16;
    assert(!has(x64_effects(&e, &i).used, R12));

    /* Nonlocal resume reconstructs an aligned base before private loads. */
    i = instruction(X_RESUME_FRAME, 8);
    f = x64_effects(&e, &i);
    assert(!f.defines && !f.used && !(f.flags & RECORD_EFFECT_WRITES_FLAGS)); /* standard frame: no-op */
    wk.frame_alignment = 64;
    f = x64_effects(&e, &i);
    assert(has(f.defines, R12) && has(f.used, RBP));
    assert((f.flags & RECORD_EFFECT_BARRIER) && (f.flags & RECORD_EFFECT_WRITES_FLAGS));

    /* Scalar SSE preserves destination lanes and observes FP environment. */
    const int fp[] = { X_FADD, X_FDIV, X_CVTI2F, X_CVTF2F };
    for (unsigned k = 0; k < sizeof(fp) / sizeof(fp[0]); k++) {
        i = instruction(fp[k], 4); i.registers[0] = XMM8; i.registers[1] = XMM0;
        f = x64_effects(NULL, &i);
        assert(has(f.used, XMM8) && has(f.defines, XMM8));
        assert((f.flags & RECORD_EFFECT_FLOAT) && !(f.flags & RECORD_EFFECT_PURE));
    }
    i = instruction(X_X87, 10); i.condition = XF_UCOMIP;
    f = x64_effects(NULL, &i);
    assert((f.flags & RECORD_EFFECT_BARRIER) && (f.flags & RECORD_EFFECT_WRITES_FLAGS));
    i = instruction(X_RAW, 8);
    f = x64_effects(NULL, &i);
    assert(f.defines == X_ALL_REGS && f.used == X_ALL_REGS && f.branch == RECORD_BRANCH_INDIRECT);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-rec-effects-') as tmp:
        source = Path(tmp) / 'effects.c'
        binary = Path(tmp) / 'effects'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'src'), '-I', str(Path(R) / 'src' / 'include'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('machine record effect contracts: PASS')


if __name__ == '__main__':
    main()
