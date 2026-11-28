#!/usr/bin/env python3
"""debug-values.py: the values a debugger reads from -g -O0 programs.

Each program of src/test/debug-info with a "/* PRINT names */" comment is
built by vpp and clang for the host, stopped at its line marked STOP under
gdb (else lldb), and each name printed: vcc's values are clang's.  At -O0
with VIR and, on x86-64, with the direct emitter.  Skipped without a
debugger.
"""
import glob, importlib.util, os, platform, re, shutil, subprocess, sys, tempfile

B = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ct', f'{B}/vcc-ctests.py')
ct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ct)

TESTS = f'{os.path.dirname(B)}/src/test/debug-info'


def values(program, source, line, names):
    """name -> what the debugger prints for it at source:line"""
    if shutil.which('gdb'):
        command = ['gdb', '-batch', '-nx', '-ex', f'break {source}:{line}', '-ex', 'run']
        for n in names:
            command += ['-ex', f'echo @{n}=', '-ex', f'output {n}', '-ex', 'echo \\n']
        command.append(program)
    else:
        command = [ct.llvm('lldb'), '-b', '-o', f'breakpoint set -f {source} -l {line}', '-o', 'run']
        for n in names:
            command += ['-o', f'script print("@{n}", lldb.frame.EvaluateExpression("{n}").GetValue())']
        command.append(program)
    out = subprocess.run(command, capture_output=True, text=True, timeout=60).stdout
    # (addresses differ between the two programs)
    return {m[1]: re.sub(r'0x[0-9a-f]+', '0x?', m[2]) for m in re.finditer(r'^@([^=\s]+)[= ](.*)$', out, re.M)}


def main():
    if not shutil.which('gdb') and not shutil.which(ct.llvm('lldb')):
        print('debug-values: skipped, no gdb or lldb')
        return
    failed = count = 0
    sources = []
    for source in sorted(glob.glob(f'{TESTS}/*.c')):
        with open(source) as f:
            text = f.read()
        if m := re.search(r'/\* PRINT (.*?) \*/', text):
            stop = next(n for n, line in enumerate(text.splitlines(), 1) if '/* STOP */' in line)
            sources.append((source, stop, m[1].split()))
    emitters = [[]]
    if platform.machine() == 'x86_64':
        emitters.append(['-fno-vir'])
    with tempfile.TemporaryDirectory() as tmp:
        for source, stop, names in sources:
            name = os.path.basename(source)
            subprocess.run([ct.llvm('clang'), '-g', '-O0', '-w', source, '-o', f'{tmp}/clang'], check=True)
            expected = values(f'{tmp}/clang', name, stop, names)
            for flags in emitters:
                subprocess.run([ct.VPP, '-g', '-O0', '-w', '-isystem', ct.RES, *flags, '-c', source,
                                '-o', f'{tmp}/vcc.o'], check=True)
                subprocess.run([ct.llvm('clang'), f'{tmp}/vcc.o', '-o', f'{tmp}/vcc'], check=True)
                got = values(f'{tmp}/vcc', name, stop, names)
                for n in names:
                    count += 1
                    if n not in expected or got.get(n) != expected[n]:
                        print(f'FAIL {name} {" ".join(flags)}: {n}: vcc {got.get(n)}, clang {expected.get(n)}')
                        failed += 1
    print(f'debug-values: {count} values, {failed} failures')
    sys.exit(bool(failed))


main()
