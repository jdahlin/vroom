#!/usr/bin/env python3
"""Exercise x86 interrupt entry/IRETQ, integer/FP preservation and diagnostics.

Runs natively on x86-64 Linux. UIRET is inspected, since executing it requires
an OS-enabled user-interrupt facility. --bin-dir selects a private vpp build.
"""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile
from hostenv import BUILD, llvm

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', BUILD))
a = p.parse_args()
root = Path(__file__).resolve().parent.parent
vpp = str(Path(a.bin_dir).resolve() / 'vpp')
clang = llvm('clang')
src = root / 'ccs/vcc/tests/interrupt'
base = ['--target=x86_64-linux-gnu', '-fasynchronous-unwind-tables']

def run(cmd):
    subprocess.run(list(map(str, cmd)), check=True, timeout=30)

if platform.machine() not in ('x86_64', 'amd64') or platform.system() != 'Linux':
    raise SystemExit('vcc-interrupt.py requires native x86-64 Linux')
with tempfile.TemporaryDirectory(prefix='vcc-interrupt-') as tmp:
    d = Path(tmp)
    for opt in ('-O0', '-O1', '-O2'):
        for gregs in (False, True):
            flags = ['-mgeneral-regs-only', '-DGREGS'] if gregs else []
            for mode in ('-S', '-c'):
                obj = d / ('handler.s' if mode == '-S' else 'handler.o')
                run([vpp, *base, opt, *flags, mode, src / 'return.c', '-o', obj])
                run([clang, obj, src / 'entry.S', '-o', d / 'return'])
                run([d / 'return'])
    # A local target attribute restores FP under a global no-SSE2 setting.
    fp_source = (src / 'return.c').read_text()
    for declaration in ('static void disturb', 'static void check', 'void handler0', 'void handler1', 'int main'):
        fp_source = fp_source.replace(declaration, '__attribute__((target("sse2"))) ' + declaration)
    (d / 'local-fp.c').write_text(fp_source)
    for flag in ('-mno-sse2', '-mgeneral-regs-only'):
        run([vpp, *base, flag, '-O2', '-c', d / 'local-fp.c', '-o', d / 'local-fp.o'])
        run([clang, d / 'local-fp.o', src / 'entry.S', '-o', d / 'local-fp'])
        run([d / 'local-fp'])
    # The kernel entry/return flags also apply when interrupt frames are used.
    (d / 'thunk.S').write_text('.text\n.globl __x86_return_thunk\n__x86_return_thunk:\nret\n.section .note.GNU-stack,"",@progbits\n')
    hardening = ['-fcf-protection=branch', '-fpatchable-function-entry=3,1',
                 '-mfunction-return=thunk-extern', '-mindirect-branch-cs-prefix',
                 '-fstack-protector-all']
    for mode in ('-S', '-c'):
        obj = d / ('hardened.s' if mode == '-S' else 'hardened.o')
        run([vpp, *base, '-O2', *hardening, mode, src / 'return.c', '-o', obj])
        if mode == '-S':
            text = obj.read_text()
            if 'endbr64' not in text or '__x86_return_thunk' not in text:
                raise SystemExit('interrupt integration lost kernel entry/return protection')
        run([clang, obj, src / 'entry.S', d / 'thunk.S', '-o', d / 'hardened'])
        run([d / 'hardened'])
    cases = [
        'int __attribute__((interrupt)) f(void *p) { return 0; }',
        'void __attribute__((interrupt)) f(void) {}',
        'void __attribute__((interrupt)) f(int p) {}',
        'void __attribute__((interrupt)) f(void *p, unsigned e) {}',
        'void __attribute__((interrupt)) f(void *p, unsigned long e, int extra) {}',
        'void __attribute__((interrupt)) f(void *p) {} void g(void) { f(0); }',
    ]
    for source in cases:
        (d / 'invalid.c').write_text(source)
        for compiler in (vpp, clang):
            r = subprocess.run([compiler, *base, '-fsyntax-only', str(d / 'invalid.c')],
                               capture_output=True, text=True, timeout=30)
            if r.returncode == 0:
                raise SystemExit(f'{compiler} accepted invalid interrupt declaration: {source}')
    # User-interrupt entry has RIP/RFLAGS/RSP, preceded by the vector word.
    # The handler validates this frame and exits before executing UIRET.
    (d / 'uintr.c').write_text('''
#ifndef __UINTR__
#error missing user-interrupt predefine
#endif
struct frame { unsigned long ip, flags, sp; };
extern void exit(int);
__attribute__((interrupt)) void handler(struct frame *p, unsigned long vector) {
    exit(vector != 41 || p->ip != 42 || p->flags != 43 || p->sp != 44);
}
int main(void) {
    __asm__ volatile("push $44; push $43; push $42; push $41; jmp handler");
    return 1;
}
''')
    run([vpp, *base, '-muintr', '-mgeneral-regs-only', '-S', d/'uintr.c', '-o', d/'uintr.s'])
    if '0xf3, 0x0f, 0x01, 0xec' not in (d/'uintr.s').read_text():
        raise SystemExit('missing UIRET encoding')
    run([clang, d/'uintr.s', '-o', d/'uintr'])
    run([d/'uintr'])
    local_ui = (d/'uintr.c').read_text()
    local_ui = local_ui.replace('#ifndef __UINTR__\n#error missing user-interrupt predefine\n#endif', '')
    local_ui = local_ui.replace('__attribute__((interrupt))', '__attribute__((interrupt, target("uintr")))')
    local_ui += '\n__attribute__((interrupt)) void ordinary(void *p) {}\n'
    (d/'local-ui.c').write_text(local_ui)
    run([vpp, *base, '-mgeneral-regs-only', '-S', d/'local-ui.c', '-o', d/'local-ui.s'])
    assembly = (d/'local-ui.s').read_text()
    if '0xf3, 0x0f, 0x01, 0xec' not in assembly or '0x48, 0xcf' not in assembly:
        raise SystemExit('function-local UIRET selection leaked or was ignored')
    run([clang, d/'local-ui.s', '-o', d/'local-ui'])
    run([d/'local-ui'])
print('interrupt: 16 entry/return executions, 6 diagnostic comparisons, global/local UIRET frame/encoding passed')
