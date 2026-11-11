#!/usr/bin/env python3
"""Frame-free x86 leaves: option policy, conservative exclusions, CFI and runtime."""
import argparse
from pathlib import Path
import re
import os
import subprocess
import tempfile

from hostenv import BUILD, llvm

SOURCE = r'''
#define NI __attribute__((noinline))
NI int fl_literal(void) { return 151; }
NI void fl_empty(void) {}
struct fl_interrupt_frame { unsigned long ip, cs, flags, sp, ss; };
__attribute__((interrupt)) void fl_interrupt(struct fl_interrupt_frame *frame) { (void)frame; }
NI int fl_other_literal(void) { return 152; }
NI void *fl_frame(void) { return __builtin_frame_address(0); }
NI void *fl_return(void) { return __builtin_return_address(0); }
NI void fl_raw(void) { __asm__ volatile("nop"); }
NI int fl_parameter(int x) { return x + 1; }
NI int fl_stack(int a,int b,int c,int d,int e,int f,int g) { return g; }
NI int fl_aligned(void)
{
    volatile unsigned char a[64] __attribute__((aligned(64)));
    a[0] = 7;
    return a[0];
}
NI int fl_callee(void) { return 11; }
NI int fl_call(void) { return fl_callee(); }
static __INTPTR_TYPE__ fl_env[5];
NI int fl_setjmp(void) { return __builtin_setjmp(fl_env); }
NI int fl_alloca(void)
{
    volatile int *p = __builtin_alloca(16);
    *p = 19;
    return *p;
}
NI int fl_clobber(void) { __asm__ volatile("" : : : "rbx"); return 23; }
int main(void)
{
    fl_empty();
    if (fl_literal() != 151 || fl_other_literal() != 152) return 1;
    if (!fl_frame() || !fl_return()) return 2;
    fl_raw();
    if (fl_parameter(5) != 6 || fl_stack(1,2,3,4,5,6,7) != 7) return 3;
    if (fl_aligned() != 7 || fl_call() != 11 || fl_setjmp() != 0) return 4;
    if (fl_alloca() != 19 || fl_clobber() != 23) return 5;
    return 0;
}
'''


def run(command):
    p = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=60)
    if p.returncode:
        raise RuntimeError(f'{command}: exit {p.returncode}\n{p.stdout}{p.stderr}')
    return p.stdout


def body(text, name):
    m = re.search(r'^' + name + r':[^\n]*\n(.*?)^\s*\.size\s+' + name + r'\s*,', text, re.M | re.S)
    if not m:
        raise RuntimeError('missing function ' + name)
    return m[1]


def framed(text):
    return bool(re.search(r'\bpushq\s+%rbp\b', text))


def unwind_leaf(args, d):
    # Interrupt a true call-free leaf: unwinding a normal callback would itself
    # force a frame and would never exercise the optimized function's FDE.
    source = d / 'signal-leaf.c'
    source.write_text('extern volatile int fl_signal_done;\n'
                      '__attribute__((noinline)) int fl_signal_leaf(void) {\n'
                      '  while (!fl_signal_done) {} return 29; }\n')
    assembly = d / 'signal-leaf.s'
    run([args.vpp, '--target=x86_64-linux-gnu', '-O2',
         '-fasynchronous-unwind-tables', '-S', source, '-o', assembly])
    if framed(body(assembly.read_text(), 'fl_signal_leaf')):
        raise RuntimeError('signal unwind probe is not frame-free')
    obj = d / 'signal-leaf.o'
    run([args.clang, '--target=x86_64-linux-gnu', '-c', assembly, '-o', obj])
    symbols = run([llvm('llvm-nm'), '-S', obj])
    match = re.search(r'^[0-9a-fA-F]+ ([0-9a-fA-F]+) T fl_signal_leaf$', symbols, re.M)
    if not match:
        raise RuntimeError('missing signal leaf size')
    size = int(match[1], 16)
    wrapper = d / 'signal-wrapper.s'
    wrapper.write_text('.text\n.globl fl_signal_wrapper\n.type fl_signal_wrapper,@function\n'
                       'fl_signal_wrapper:\n.cfi_startproc\npush %rbp\n'
                       '.cfi_def_cfa_offset 16\n.cfi_offset %rbp,-16\nmov %rsp,%rbp\n'
                       '.cfi_def_cfa_register %rbp\ncall fl_signal_leaf\n'
                       '.globl fl_signal_return\nfl_signal_return:\npop %rbp\n'
                       '.cfi_def_cfa %rsp,8\nret\n.cfi_endproc\n'
                       '.size fl_signal_wrapper,.-fl_signal_wrapper\n'
                       '.section .note.GNU-stack,"",@progbits\n')
    callback = d / 'signal-callback.c'
    callback.write_text(r'''#include <stdint.h>
#include <signal.h>
#include <sys/time.h>
#include <unwind.h>
extern int fl_signal_leaf(void), fl_signal_wrapper(void);
extern const char fl_signal_return[];
volatile int fl_signal_done;
static volatile sig_atomic_t leaf_seen, caller_seen;
static _Unwind_Reason_Code visit(struct _Unwind_Context *context, void *unused)
{
    (void)unused;
    uintptr_t ip = _Unwind_GetIP(context), start = (uintptr_t)fl_signal_leaf;
    if (ip >= start && ip < start + FL_LEAF_BYTES) leaf_seen = 1;
    if (ip == (uintptr_t)fl_signal_return) caller_seen = 1;
    return _URC_NO_REASON;
}
static void interrupt_leaf(int sig)
{
    (void)sig;
    fl_signal_done = 1;
    _Unwind_Backtrace(visit, 0);
}
int main(void)
{
    /* Resolve unwinder PLT entries before entering the signal handler. */
    _Unwind_Backtrace(visit, 0);
    leaf_seen = caller_seen = 0;
    if (signal(SIGALRM, interrupt_leaf) == SIG_ERR) return 1;
    struct itimerval timer = { .it_value = { .tv_usec = 10000 } };
    if (setitimer(ITIMER_REAL, &timer, 0)) return 2;
    if (fl_signal_wrapper() != 29) return 3;
    return !leaf_seen || !caller_seen;
}
''')
    exe = d / 'signal-unwind'
    run([args.clang, '-no-pie', '-DFL_LEAF_BYTES=' + str(size),
         callback, wrapper, obj, '-o', exe])
    run([exe])


def mixed_cache(args, d):
    (d / 'leaf-header.h').write_text('#ifndef FL_HEADER_H\n#define FL_HEADER_H\n'
                                    '__attribute__((noinline)) int fl_header(void) { return 151; }\n'
                                    '#endif\n')
    source = d / 'cache.c'
    source.write_text('#include "leaf-header.h"\nint cache_use(void) { return fl_header(); }\n')
    rows = []
    for i in range(12):
        flag = '-fomit-frame-pointer' if i % 2 else '-fno-omit-frame-pointer'
        rows.append('\x1f'.join(['-', '--target=x86_64-linux-gnu', '-O2', flag,
                                  '-S', str(source), '-o', str(d / f'cache-{i}.s')]))
    jobs = d / 'cache-jobs'
    jobs.write_text('\n'.join(rows) + '\n')
    for replay in [False, True]:
        env = dict(os.environ, VCC_DR='1' if replay else '0', VCC_DR_GAIN='0')
        p = subprocess.run([args.vpp, '--batch', str(jobs), '-j', '1', '-C', str(d)],
                           env=env, capture_output=True, text=True, timeout=60)
        if p.returncode:
            raise RuntimeError('mixed cache batch: ' + p.stdout + p.stderr)
        for i in range(12):
            if framed(body((d / f'cache-{i}.s').read_text(), 'fl_header')) != (i % 2 == 0):
                raise RuntimeError(f'mixed cache leaked frame policy in job {i}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    ap.add_argument('--clang', default=llvm('clang'))
    ap.add_argument('--no-run', action='store_true', help='cross-check code generation without native execution')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-leaf-frame-') as temp:
        d = Path(temp)
        source = d / 'leaf.c'
        source.write_text(SOURCE)
        variants = [
            ('O0', ['-O0'], False),
            ('O1', ['-O1'], True),
            ('O2', ['-O2'], True),
            ('O3', ['-O3'], True),
            ('no-cfi', ['-O2', '-fno-asynchronous-unwind-tables'], True),
            ('O0-explicit', ['-O0', '-fomit-frame-pointer'], True),
            ('keep', ['-O2', '-fno-omit-frame-pointer'], False),
            ('last-omit', ['-O2', '-fno-omit-frame-pointer', '-fomit-frame-pointer'], True),
            ('last-keep', ['-O2', '-fomit-frame-pointer', '-fno-omit-frame-pointer'], False),
        ]
        for name, flags, omit in variants:
            assembly = d / (name + '.s')
            run([args.vpp, '--target=x86_64-linux-gnu', *flags, '-S', source, '-o', assembly])
            text = assembly.read_text()
            for function in ['fl_literal', 'fl_empty', 'fl_callee', 'fl_other_literal']:
                if framed(body(text, function)) == omit:
                    raise RuntimeError(f'{name}: wrong frame policy for {function}')
            for function in ['fl_frame', 'fl_return', 'fl_raw', 'fl_parameter',
                             'fl_stack', 'fl_aligned', 'fl_call', 'fl_setjmp', 'fl_alloca', 'fl_clobber', 'fl_interrupt']:
                if not framed(body(text, function)):
                    raise RuntimeError(f'{name}: required frame omitted in {function}')
            if not args.no_run:
                exe = d / name
                run([args.clang, '-no-pie', assembly, '-o', exe])
                run([exe])
        # The existing compiler-wide optimization bypass retains its frame.
        assembly = d / 'noopt.s'
        env = dict(os.environ, VCC_NOOPT='1')
        p = subprocess.run([args.vpp, '--target=x86_64-linux-gnu', '-O2', '-S',
                            str(source), '-o', str(assembly)], env=env,
                           capture_output=True, text=True, timeout=60)
        if p.returncode or not framed(body(assembly.read_text(), 'fl_literal')):
            raise RuntimeError('VCC_NOOPT did not retain its frame: ' + p.stderr)
        # Prefix hardening stays before the first instruction, with a frameless
        # CFA (rsp + 8) and valid .loc rows when debug/unwind data are requested.
        assembly = d / 'hardening.s'
        run([args.vpp, '--target=x86_64-linux-gnu', '-O2', '-g',
             '-fasynchronous-unwind-tables', '-fcf-protection=branch',
             '-fpatchable-function-entry=5,2', '-S', source, '-o', assembly])
        text = assembly.read_text()
        literal = body(text, 'fl_literal')
        if framed(literal) or literal.count('\tnop\n') != 3 or '\tendbr64\n' not in literal:
            raise RuntimeError('hardening prefix or frame policy changed')
        if '.cfi_startproc' not in literal or '.cfi_endproc' not in literal or '.loc' not in literal:
            raise RuntimeError('missing frameless CFI/debug directives')
        obj = d / 'hardening.o'
        run([args.clang, '--target=x86_64-linux-gnu', '-c', assembly, '-o', obj])
        run([llvm('llvm-dwarfdump'), '--verify', obj])
        # Even a literal-only function must keep the canary's frame.
        run([args.vpp, '--target=x86_64-linux-gnu', '-O2', '-fstack-protector-all',
             '-S', source, '-o', assembly])
        if not framed(body(assembly.read_text(), 'fl_literal')):
            raise RuntimeError('stack-protector frame omitted')
        mixed_cache(args, d)
        if not args.no_run:
            unwind_leaf(args, d)
    print('x86 leaf frames: policy, mixed caches, exclusions, hardening and debug PASS' +
          ('' if args.no_run else '; native runtime and signal unwind PASS'))


if __name__ == '__main__':
    main()
