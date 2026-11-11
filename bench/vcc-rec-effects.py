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
#include "vcc/arch/x64/emit/effects.c"
#include "vcc/arch/x64/emit/extensions.c"

static rec instruction(int op, int size)
{
    return (rec){ .op = op, .sz = size,
                  .r = { RAX, RCX, RDX, XNOREG } };
}
static int has(rmask value, int reg) { return (value & (UINT64_C(1) << reg)) != 0; }
int main(void)
{
    /* Every existing opcode needs metadata; new unknown records are opaque. */
    for (int op = 0; op < X_NOPS; op++) {
        rec i = instruction(op, 8);
        receffects f = x64_effects(NULL, &i);
        assert(op == X_RAW || f.def != X_ALL_REGS || f.use != X_ALL_REGS);
        if (f.fl & OD_PURE)
            assert(!(f.fl & (OD_LOAD | OD_STORE | OD_TRAP | OD_FP | OD_BARRIER)));
    }
    rec i = instruction(X_NOPS, 8);
    receffects f = x64_effects(NULL, &i);
    assert(f.def == X_ALL_REGS && f.use == X_ALL_REGS);
    assert((f.fl & OD_BARRIER) && !(f.fl & OD_PURE));

    /* Legacy scalar square root retains the destination's upper SIMD lanes
       and can raise an FP exception; it neither branches nor changes flags. */
    for (int size = 4; size <= 8; size += 4) {
        i = (rec){ .op=X_FSQRT, .sz=size, .r={XMM8,(XMM0 + 1),XNOREG,XNOREG} };
        f = x64_effects(NULL, &i);
        assert(has(f.def,XMM8) && has(f.use,XMM8) && has(f.use,(XMM0 + 1)));
        assert((f.fl & (OD_FP|OD_TRAP)) == (OD_FP|OD_TRAP));
        assert(!(f.fl & (OD_PURE|OD_WFLAGS)) && f.br == RB_NONE);
        assert(x64_flow_table()[X_FSQRT] == EM_FLOW_NEXT);
    }

    /* A local width proof must not cross labels or mistake a zero-count
       shift/partial/signed write for a full unsigned result. */
    xst state = { .nin=1, .in=&i };
    ecx context = { .x64=&state };
    i = instruction(X_LD,4);
    assert(x64_last_u32(&context,RAX));
    assert(!x64_last_u32(&context,RCX));
    i.cond=1; assert(!x64_last_u32(&context,RAX));
    i = instruction(X_MOV,1); assert(!x64_last_u32(&context,RAX));
    i.sz=4; assert(x64_last_u32(&context,RAX));
    i = instruction(X_ADDI,4); assert(x64_last_u32(&context,RAX));
    i = instruction(X_SHLI,4); i.imm=0; assert(!x64_last_u32(&context,RAX));
    i = instruction(X_BSF,4); assert(!x64_last_u32(&context,RAX));
    i = instruction(X_LABEL,4); assert(!x64_last_u32(&context,RAX));
    i = instruction(X_MOVI,8); i.imm=UINT32_MAX; assert(x64_last_u32(&context,RAX));
    i.imm=-1; assert(!x64_last_u32(&context,RAX));
    state.nin=0; assert(!x64_last_u32(&context,RAX));

    /* Whole-register liveness must retain high bits of partial GPR writes. */
    i = instruction(X_MOV, 1);
    f = x64_effects(NULL, &i);
    assert(has(f.def, RAX) && has(f.use, RAX) && has(f.use, RCX));
    i.sz = 4;
    f = x64_effects(NULL, &i);
    assert(has(f.def, RAX) && !has(f.use, RAX));
    i = instruction(X_SETCC, 4);
    f = x64_effects(NULL, &i);
    assert(has(f.def, RAX) && !has(f.use, RAX) && (f.fl & OD_RFLAGS));

    /* These printer expansions have hidden scratch registers and state. */
    i = instruction(X_FSETCC, 4);
    f = x64_effects(NULL, &i);
    assert(has(f.def, R11) && has(f.use, R11));
    assert((f.fl & (OD_RFLAGS | OD_WFLAGS)) == (OD_RFLAGS | OD_WFLAGS));
    i = instruction(X_DIV, 8);
    f = x64_effects(NULL, &i);
    assert(has(f.use, RAX) && has(f.use, RDX));
    assert(has(f.def, RAX) && has(f.def, RDX) && (f.fl & OD_TRAP));
    i = instruction(X_SHL, 8);
    f = x64_effects(NULL, &i);
    assert(has(f.use, RCX) && (f.fl & OD_RFLAGS)); /* count may be zero */
    i = instruction(X_REP, 1);
    f = x64_effects(NULL, &i);
    assert(has(f.use, RSI) && has(f.def, RSI) && has(f.def, RDI) && has(f.def, RCX));
    assert(f.fl & OD_RFLAGS); /* direction flag */
    i.cond = 1;
    f = x64_effects(NULL, &i);
    assert(has(f.use, RAX) && !has(f.def, RAX) && !has(f.def, RSI));

    /* TLS local-exec preserves flags; initial-exec adds; dynamic calls. */
    i = instruction(X_TLS, 8);
    f = x64_effects(NULL, &i);
    assert(!(f.fl & (OD_WFLAGS | OD_CALL)));
    i.cond = 1;
    f = x64_effects(NULL, &i);
    assert((f.fl & OD_WFLAGS) && !(f.fl & OD_CALL));
    i.cond = 2;
    f = x64_effects(NULL, &i);
    assert((f.fl & OD_CALL) && has(f.def, R11) && has(f.def, XMM15));
    assert(has(f.use, RSP) && !has(f.use, RDI)); /* descriptor replaces RDI */

    /* Aligned local slots use R12 even when the record names RBP. */
    wkst wk = { .falign = 32 };
    ecx e = { .wk = &wk };
    i = instruction(X_LD, 8);
    i.r[1] = RBP; i.off = -16;
    f = x64_effects(&e, &i);
    assert(has(f.use, R12) && !(f.fl & OD_PURE));
    wk.falign = 16;
    assert(!has(x64_effects(&e, &i).use, R12));

    /* Nonlocal resume reconstructs an aligned base before private loads. */
    i = instruction(X_RESUME_FRAME, 8);
    f = x64_effects(&e, &i);
    assert(!f.def && !f.use && !(f.fl & OD_WFLAGS)); /* standard frame: no-op */
    wk.falign = 64;
    f = x64_effects(&e, &i);
    assert(has(f.def, R12) && has(f.use, RBP));
    assert((f.fl & OD_BARRIER) && (f.fl & OD_WFLAGS));

    /* Scalar SSE preserves destination lanes and observes FP environment. */
    const int fp[] = { X_FADD, X_FDIV, X_CVTI2F, X_CVTF2F };
    for (unsigned k = 0; k < sizeof(fp) / sizeof(fp[0]); k++) {
        i = instruction(fp[k], 4); i.r[0] = XMM8; i.r[1] = XMM0;
        f = x64_effects(NULL, &i);
        assert(has(f.use, XMM8) && has(f.def, XMM8));
        assert((f.fl & OD_FP) && !(f.fl & OD_PURE));
    }
    i = instruction(X_X87, 10); i.cond = XF_UCOMIP;
    f = x64_effects(NULL, &i);
    assert((f.fl & OD_BARRIER) && (f.fl & OD_WFLAGS));
    i = instruction(X_RAW, 8);
    f = x64_effects(NULL, &i);
    assert(f.def == X_ALL_REGS && f.use == X_ALL_REGS && f.br == RB_INDIRECT);
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
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'ccs'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('machine record effect contracts: PASS')


if __name__ == '__main__':
    main()
