#!/usr/bin/env python3
"""Host-only contracts for the x86 adapter and shared forward-copy scan."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "arch/x64/codegen/effects.c"
#include "codegen/rec/copies.c"
#include "arch/x64/codegen/copies.c"
#define OP(op_,sz_,d_,a_,b_) ((record){ .opcode=op_, .size=sz_, .registers={d_,a_,b_,XNOREG} })
#define CP(d,s) OP(X_MOV,8,d,s,XNOREG)
#define IMM(d) OP(X_MOVI,8,d,XNOREG,XNOREG)
#define STORE(s) OP(X_ST,8,XNOREG,RDI,s)
static uint32_t optimize(record *in, uint32_t count)
{
    walk_state wk = { .frame_alignment=16 };
    xst x = { .in=in, .nin=count };
    codegen_context e = { .walk_state=&wk, .x64_context=&x };
    unsigned n = 0;
    for (unsigned k = 0; k < count; k++) {
        if (in[k].opcode == X_MOV && x64_copy_forward_at(&e, k)) continue;
        in[n++] = in[k];
    }
    return n;
}
int main(void)
{
    /* A callee-saved variable's temporary is unnecessary for a store, if
     * that temporary is overwritten within the same straight-line block.
     */
    record positive[] = { CP(RAX,RBX), STORE(RAX), IMM(RAX) };
    assert(optimize(positive,3)==2);
    assert(positive[0].opcode==X_ST && positive[0].registers[2]==RBX);
    /* Both explicit reads are rewritten, not only the first occurrence. */
    record both[] = { CP(RAX,R13), OP(X_CMP,8,XNOREG,RAX,RAX), IMM(RAX) };
    assert(optimize(both,3)==2 && both[0].registers[1]==R13 && both[0].registers[2]==R13);
    /* Base and data fields of a store can independently consume a copy. */
    record address[] = { CP(RAX,RBX), OP(X_ST,8,XNOREG,RAX,RDX), IMM(RAX) };
    assert(optimize(address,3)==2 && address[0].registers[1]==RBX);

    /* No inter-block liveness assumption: the last use can still be outside
     * this block, so a copy without an in-block overwrite must remain.
     */
    record liveout[] = { CP(RAX,RBX), STORE(RAX) };
    assert(optimize(liveout,2)==2);
    record changed[] = { CP(RAX,RBX), IMM(RBX), STORE(RAX), IMM(RAX) };
    assert(optimize(changed,4)==4 && changed[2].registers[2]==RAX);
    record tied[] = { CP(RAX,RBX), OP(X_ADD,8,RAX,RAX,RCX), IMM(RAX) };
    assert(optimize(tied,3)==3 && tied[1].registers[1]==RAX);
    record shift[] = { CP(RCX,RBX), OP(X_SHL,8,RAX,RAX,RCX), IMM(RCX) };
    assert(optimize(shift,3)==3);
    record partial[] = { CP(RAX,RBX), OP(X_MOV,1,RAX,RCX,XNOREG), STORE(RAX), IMM(RAX) };
    assert(optimize(partial,4)==4 && partial[2].registers[2]==RAX);
    record narrow[] = { OP(X_MOV,4,RAX,RBX,XNOREG), STORE(RAX), IMM(RAX) };
    assert(optimize(narrow,3)==3);
    record persistent[] = { CP(RBX,RAX), STORE(RBX), IMM(RBX) };
    assert(optimize(persistent,3)==3);
    record xmm[] = { CP(XMM8,XMM0), OP(X_ST,8,XNOREG,RDI,XMM8), IMM(XMM8) };
    assert(optimize(xmm,3)==3);
    const int boundaries[]={X_LABEL,X_NAMED,X_RAW,X_BARRIER,X_CALL,X_JCC,X_JMP,X_JMPR};
    for (unsigned k=0;k<sizeof(boundaries)/sizeof(boundaries[0]);k++) {
        record a[]={CP(RAX,RBX),OP(boundaries[k],8,XNOREG,XNOREG,XNOREG),STORE(RAX),IMM(RAX)};
        assert(optimize(a,4)==4 && a[2].registers[2]==RAX);
    }
    const int fixed[]={RSP,RBP,R11,R12};
    for (unsigned k=0;k<sizeof(fixed)/sizeof(fixed[0]);k++) {
        record a[]={CP(RAX,fixed[k]),STORE(RAX),IMM(RAX)};
        assert(optimize(a,3)==3);
    }
    /* Failed scans are transactional, including bounded rewrite storage. */
    record many[259];
    many[0]=CP(RAX,RBX);
    for (unsigned k=1;k<=257;k++) many[k]=STORE(RAX);
    many[258]=IMM(RAX);
    assert(optimize(many,259)==259);
    for (unsigned k=1;k<=257;k++) assert(many[k].registers[2]==RAX);
    many[257]=IMM(RAX);
    assert(optimize(many,258)==257);
    for (unsigned k=0;k<256;k++) assert(many[k].registers[2]==RBX);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-rec-copies-') as tmp:
        source = Path(tmp) / 'copies.c'
        binary = Path(tmp) / 'copies'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'src'), '-I', str(Path(R) / 'src' / 'include'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('machine record forward-copy contracts: PASS')


if __name__ == '__main__':
    main()
