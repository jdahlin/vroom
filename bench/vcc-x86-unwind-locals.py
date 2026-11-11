#!/usr/bin/env python3
"""Recover promoted x86 callee-save registers through libgcc's real unwinder.

Runs on x86-64 Linux. Two generated callees keep RBX/R13-R15 homes live across
an external callback, with ordinary and 64-byte-aligned frames. The callback
unwinds to an assembly caller and verifies its original register sentinels.
No compiler build, external suite or work/ tree is needed.
"""
import argparse
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile

from hostenv import BUILD, llvm


GENERATED = r'''
extern int rl_unwind_capture(void);
#define BODY \
    long a = n + 1, b = n + 2, c = n + 3, d = n + 4; \
    a += 10; b += 20; c += 30; d += 40; \
    a += 1; a -= 1; a += 1; a -= 1; a += 1; a -= 1; \
    b += 1; b -= 1; b += 1; b -= 1; b += 1; b -= 1; \
    c += 1; c -= 1; c += 1; c -= 1; c += 1; c -= 1; \
    d += 1; d -= 1; d += 1; d -= 1; d += 1; d -= 1; \
    int result = rl_unwind_capture();
__attribute__((noinline)) int rl_unwind_plain(int n)
{
    BODY
    return (int)(a + b + c + d) + result;
}
__attribute__((noinline)) int rl_unwind_aligned(int n)
{
    volatile unsigned char aligned[64] __attribute__((aligned(64)));
    aligned[0] = (unsigned char)n;
    BODY
    return (int)(a + b + c + d) + result + aligned[0] - n +
           (((__UINTPTR_TYPE__)aligned & 63) != 0);
}
'''

CALLBACK = r'''
#include <stdint.h>
#include <stdio.h>
#include <unwind.h>
extern int rl_unwind_plain(int), rl_unwind_aligned(int);
extern int rl_unwind_wrapper(int (*)(int));
extern const char rl_unwind_return[];
static int seen, bad;
static _Unwind_Reason_Code visit(struct _Unwind_Context *context, void *unused)
{
    (void)unused;
    if (_Unwind_GetIP(context) != (uintptr_t)rl_unwind_return)
        return _URC_NO_REASON;
    static const unsigned registers[] = {3, 13, 14, 15};
    static const uintptr_t values[] = {0x12345, 0x23456, 0x34567, 0x45678};
    seen++;
    for (unsigned i = 0; i < 4; i++) {
        uintptr_t actual = _Unwind_GetGR(context, registers[i]);
        if (actual != values[i]) {
            fprintf(stderr, "DWARF register %u: got %#lx, expected %#lx\n",
                    registers[i], (unsigned long)actual, (unsigned long)values[i]);
            bad = 1;
        }
    }
    return _URC_NORMAL_STOP;
}
int rl_unwind_capture(void)
{
    _Unwind_Backtrace(visit, 0);
    return 0;
}
int main(void)
{
    int (*functions[])(int) = {rl_unwind_plain, rl_unwind_aligned};
    for (unsigned i = 0; i < 2; i++) {
        seen = bad = 0;
        int result = rl_unwind_wrapper(functions[i]);
        if (result != 126 || seen != 1 || bad) {
            fprintf(stderr, "callee %u: result=%d, caller frames=%d, bad=%d\n",
                    i, result, seen, bad);
            return 1;
        }
    }
    return 0;
}
'''

WRAPPER = r'''
.text
.globl rl_unwind_wrapper
.type rl_unwind_wrapper,@function
rl_unwind_wrapper:
.cfi_startproc
pushq %rbp
.cfi_def_cfa_offset 16
.cfi_offset %rbp,-16
movq %rsp,%rbp
.cfi_def_cfa_register %rbp
pushq %rbx
.cfi_offset %rbx,-24
pushq %r13
.cfi_offset %r13,-32
pushq %r14
.cfi_offset %r14,-40
pushq %r15
.cfi_offset %r15,-48
movq %rdi,%rax
movl $4,%edi
movq $0x12345,%rbx
movq $0x23456,%r13
movq $0x34567,%r14
movq $0x45678,%r15
call *%rax
.globl rl_unwind_return
rl_unwind_return:
popq %r15
.cfi_restore %r15
popq %r14
.cfi_restore %r14
popq %r13
.cfi_restore %r13
popq %rbx
.cfi_restore %rbx
popq %rbp
.cfi_restore %rbp
.cfi_def_cfa %rsp,8
ret
.cfi_endproc
.size rl_unwind_wrapper,.-rl_unwind_wrapper
.section .note.GNU-stack,"",@progbits
'''


def run(command):
    result = subprocess.run([str(x) for x in command], text=True,
                            capture_output=True, timeout=30)
    if result.returncode:
        raise RuntimeError(f'{command}: exit {result.returncode}\n'
                           f'{result.stdout}{result.stderr}')
    return result.stdout


def check_homes(assembly):
    for name in ['rl_unwind_plain', 'rl_unwind_aligned']:
        match = re.search(r'^' + name + r':.*?^\s*\.size\s+' + name + r'\s*,',
                          assembly, re.M | re.S)
        if not match:
            raise RuntimeError(f'missing assembly for {name}')
        # The source contains no inline asm. A home must receive a scalar
        # value before the callback; prologue save stores do not match.
        before = match[0].split('call')[0]
        for register in ['rbx', 'r13', 'r14', 'r15']:
            if not re.search(r'\bmovq\s+%\w+\s*,\s*%' + register + r'\b', before):
                raise RuntimeError(f'{name}: {register} not used as a promoted home')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    parser.add_argument('--clang', default=llvm('clang'))
    args = parser.parse_args()
    if platform.system() != 'Linux' or platform.machine() not in ['x86_64', 'amd64']:
        print('x86 unwind locals: skipped (requires native x86-64 Linux)')
        return 0
    with tempfile.TemporaryDirectory(prefix='vcc-x86-unwind-') as temporary:
        directory = Path(temporary)
        source, callback, wrapper = [directory / name for name in ['generated.c', 'callback.c', 'wrapper.s']]
        source.write_text(GENERATED)
        callback.write_text(CALLBACK)
        wrapper.write_text(WRAPPER)
        support = directory / 'support.o'
        run([args.clang, '-O1', '-fasynchronous-unwind-tables', '-c', callback, '-o', support])
        wrapper_object = directory / 'wrapper.o'
        run([args.clang, '-c', wrapper, '-o', wrapper_object])
        # First prove the external wrapper/callback harness with Clang.
        reference = directory / 'reference'
        run([args.clang, '-O1', '-fasynchronous-unwind-tables', source,
             support, wrapper_object, '-o', reference])
        run([reference])
        for level in [1, 2]:
            assembly = directory / f'generated-O{level}.s'
            run([args.vpp, '--target=x86_64-linux-gnu', f'-O{level}',
                 '-ffreestanding', '-fasynchronous-unwind-tables', '-S', source, '-o', assembly])
            check_homes(assembly.read_text())
            executable = directory / f'probe-O{level}'
            run([args.clang, assembly, support, wrapper_object, '-o', executable])
            run([executable])
            print(f'x86 unwind locals O{level}: ordinary and aligned frames OK')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
