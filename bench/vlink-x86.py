#!/usr/bin/env python3
"""vlink-x86.py: vlink against ld.lld on what the x86-64 kernel's links need (<1 s).

  bench/vlink-x86.py [-v]

Each case assembles small x86-64 objects with clang, links them with vlink
and with ld.lld, and compares what the case is about: the program headers
(AT(): physical addresses), the loaded image (objcopy -O binary), or the
relocations --emit-relocs keeps (offset, type, symbol, addend).
"""
import os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

VLINK = f'{BUILD}/vlink'
LLD = shutil.which('ld.lld') or llvm('ld.lld')
CLANG = shutil.which('clang') or llvm('clang')
RE, OC, AR = llvm('llvm-readelf'), llvm('llvm-objcopy'), llvm('llvm-ar')

SRC = {
    # a mergeable section with relocations: its entries are equal before relocation (__ex_table)
    'ex.s': """
	.text
	.globl _start
_start:	nop
1:	nop
2:	ret
	.pushsection __ex_table, "aM", @progbits, 8
	.long 1b - .
	.long 2b - .
	.long 1b - .
	.long 2b - .
	.popsection
	.data
	.quad _start
	.quad helper
""",
    'helper.s': """
	.text
	.globl helper
helper:	call _start
	ret
""",
    'start.s': """
	.text
	.globl _start
_start:	call helper
	ret
""",
    # an archive member only -u pulls in
    'entry.s': """
	.text
	.globl efi_entry
efi_entry:
	ret
""",
}
LDS = """
SECTIONS {
	. = 0xffffffff81000000;
	.text : AT(ADDR(.text) - 0xffffffff80000000) { *(.text) }
	__ex_table : { *(__ex_table) }
	.data : { *(.data) }
	/DISCARD/ : { *(.note.*) }
}
"""
PIELDS = """
SECTIONS {
	. = 0;
	.text : { *(.text) }
	__ex_table : { *(__ex_table) }
	.data : { *(.data) }
	.rela.dyn : { *(.rela.*) }
	ASSERT(SIZEOF(.rela.dyn) == 0, "run-time relocations")
	/DISCARD/ : { *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash) *(.note.*) }
}
"""

# (name, extra link args, what to compare)
CASES = [
    ('AT(): physical addresses', '-T {d}/k.lds {d}/ex.o {d}/helper.o', 'phdrs'),
    ('SHF_MERGE with relocations: not merged', '-T {d}/k.lds {d}/ex.o {d}/helper.o', 'image'),
    ('--emit-relocs', '--emit-relocs -T {d}/k.lds {d}/ex.o {d}/helper.o', 'relocs'),
    ('-pie, -u pulls an archive member', '-pie -u efi_entry -T {d}/pie.lds {d}/helper.o {d}/start.o {d}/libe.a', 'image'),
    ('-pie: an absolute address is a run-time relocation (the script refuses it)',
     '-pie -T {d}/pie.lds {d}/ex.o {d}/helper.o', 'error'),
]


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def view(path, what):
    if what == 'phdrs':
        out = run([RE, '-l', '-W', path]).stdout
        return [l.split()[:6] for l in out.splitlines() if l.strip().startswith(('LOAD', 'NOTE'))]
    if what == 'image':
        b = path + '.bin'
        run([OC, '-O', 'binary', path, b])
        with open(b, 'rb') as f:
            return f.read()
    out = run([RE, '-r', '-W', path]).stdout
    rows = []
    for l in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+)\s+[0-9a-f]+\s+(R_X86_64_\w+)\s+[0-9a-f]+\s+(\S+)\s*([+-])\s*([0-9a-f]+)', l)
        if m:
            rows.append(m.groups())
    return sorted(rows)


def main():
    verbose = '-v' in sys.argv
    d = tempfile.mkdtemp(prefix='vlink-x86-')
    for n, s in SRC.items():
        with open(f'{d}/{n}', 'w') as f:
            f.write(s)
        subprocess.run([CLANG, '--target=x86_64-linux-gnu', '-c', f'{d}/{n}', '-o', f'{d}/{n[:-2]}.o'], check=True)
    subprocess.run([AR, 'rc', f'{d}/libe.a', f'{d}/entry.o'], check=True)
    with open(f'{d}/k.lds', 'w') as f:
        f.write(LDS)
    with open(f'{d}/pie.lds', 'w') as f:
        f.write(PIELDS)
    ok = 0
    for name, args, what in CASES:
        a = args.format(d=d).split()
        rv = run([VLINK, '-m', 'elf_x86_64'] + a + ['-o', f'{d}/v'])
        rl = run([LLD, '-m', 'elf_x86_64'] + a + ['-o', f'{d}/l'])
        if what == 'error':
            ev = rv.stderr.split('error:', 1)[-1].strip().splitlines()[:1]
            el = rl.stderr.split('error:', 1)[-1].strip().splitlines()[:1]
            same = bool(rv.returncode) and bool(rl.returncode) and ev == el
            why = f'vlink rc {rv.returncode} {ev} / lld rc {rl.returncode} {el}'
            ok += same
            if not same:
                print(f'FAIL  {name}: {why}')
            elif verbose:
                print(f'ok    {name}')
            continue
        if rv.returncode or rl.returncode:
            same = bool(rv.returncode) == bool(rl.returncode)
            why = f'vlink rc {rv.returncode} {rv.stderr.strip()[:200]} / lld rc {rl.returncode} {rl.stderr.strip()[:200]}'
        else:
            same = view(f'{d}/v', what) == view(f'{d}/l', what)
            why = f'{what} differ' + (f'\n  vlink {view(f"{d}/v", what)}\n  lld   {view(f"{d}/l", what)}' if verbose else '')
        if same and not (rv.returncode or rl.returncode):
            ok += 1
            if verbose:
                print(f'ok    {name}')
        else:
            print(f'FAIL  {name}: {why}')
    print(f'vlink-x86: {ok}/{len(CASES)} ok')
    shutil.rmtree(d, ignore_errors=True)
    return 0 if ok == len(CASES) else 1


if __name__ == '__main__':
    sys.exit(main())
