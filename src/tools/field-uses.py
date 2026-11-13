#!/usr/bin/env python3
"""field-uses.py: where selected struct fields are used, from vcc's own front end.

  src/tools/field-uses.py SELECTION [--config NAME]... [-j N] [-o REPORT.tsv] [--bin-dir DIR] [--no-build]
  src/tools/field-uses.py SELECTION --commands FILE [--root DIR] [-o REPORT.tsv]

SELECTION: lines `OWNER.FIELD [NEW]` or `OWNER.*` (`#` starts a comment).  The
tool runs vpp -fsyntax-only with VCC_FIELD_USES=SELECTION over every translation
unit of vcc, in each configuration, and merges the reports of the mode
(frontend/c/field-uses.c, doc/quality/DESIGN-field-uses.md).  Its only user
is rename-table.py --fields.

Configurations (default: every one this host can run):
  files  make vcc VCC_UNITY=       unity  make vcc            x86  files for x86_64-apple-darwin (macOS)
  asan, tsan  files with -fsanitize=...                       wasm  make vcc VCC_WASM=1 VCC_UNITY=
  fuzz   make fuzz
The TU list is the Makefile's, never a glob: src/ld/*.c are pieces of ld/ld.c.

--commands FILE: no make; one `CWD<TAB>COMMAND` per line (CWD relative to --root,
default the checkout), for tests.

Report (tab separated, sorted by path and offset; paths relative to the root, an
absolute path is outside it and is never edited):
  use|other ROLE OWNER FIELD DEF WHERE PATH OFFSET LINE COL LEN EXPANSION CONFIGS
  members OWNER DEF NAME,NAME,... CONFIGS
  macro NAME CONFIGS
A TU that fails, or prints `tu ... errors`, fails the run.
"""
import os, platform, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LINUX = platform.system() == 'Linux'
ENV = {'VCC_EAGER': '1', 'VCC_NODC': '1', 'VPP_NOHC': '1'}
CONFIGS = ['files', 'unity', 'x86', 'asan', 'tsan', 'wasm', 'fuzz']
KEEP_PREFIX = ('-D', '-U', '-I', '-isystem', '-include', '-std=', '-f', '-m', '--target=')
KEEP_SEPARATE = ('-D', '-U', '-I', '-isystem', '-include')   # the value may be the next argument
DROP_WITH_ARG = ('-o', '-MF', '-MT', '-MQ', '-x', '-Xclang', '-Xlinker', '-L', '-l')


def build_dir():
    return os.environ.get('VCC_BUILD', f'{R}/build/linux/src' if LINUX else f'{R}/build/src')


def compile_args(argv):
    """(vpp arguments, the .c inputs) of one compiler command line"""
    flags, srcs = [], []
    i = 1
    while i < len(argv):
        a = argv[i]
        i += 1
        if a in DROP_WITH_ARG:
            i += 1
        elif a in KEEP_SEPARATE:
            flags += [a, argv[i]]
            i += 1
        elif a.startswith('-fsanitize=fuzzer') or a == '-fsanitize=fuzzer-no-link':
            pass
        elif a.startswith(KEEP_PREFIX):
            flags.append(a)
        elif a.startswith('-'):
            pass
        elif a.endswith('.c'):
            srcs.append(a)
    return flags, srcs


def split_command(line):
    argv = shlex.split(line)
    while argv and os.path.basename(argv[0]) == 'ccache':
        argv = argv[1:]
    return argv


def is_compiler(argv):
    return bool(argv) and re.search(r'(^|/)(clang|gcc|cc|vcc)(-[0-9.]+)?$', argv[0]) is not None


_sysdirs = {}


def system_dirs(cc):
    """Linux: the compiler's system include directories (clang's resource directory first), as
    -nostdinc -isystem DIR...: vpp then puts vcc's own arm_neon.h and arm_acle.h before clang's,
    which need clang's builtins.  None on macOS: vpp finds the SDK."""
    if platform.system() == 'Darwin':
        return []
    if cc not in _sysdirs:
        p = subprocess.run([cc, '-E', '-v', '-x', 'c', os.devnull], capture_output=True, text=True)
        m = re.search(r'#include <\.\.\.> search starts here:\n(.*?)\nEnd of search list', p.stderr, re.S)
        dirs = [l.strip() for l in m.group(1).splitlines()] if m else []
        _sysdirs[cc] = ['-nostdinc'] + [x for d in dirs for x in ('-isystem', d)] if dirs else []
    return _sysdirs[cc]


def make_commands(cfg, tmp, jobs):
    """[(cwd, vpp flags, source)] from `make -n` for one configuration"""
    base = {'files': ['vcc', 'VCC_UNITY='], 'unity': ['vcc'], 'wasm': ['vcc', 'VCC_WASM=1', 'VCC_UNITY='], 'fuzz': ['fuzz']}
    derived = {'x86': 'files', 'asan': 'files', 'tsan': 'files'}
    args = base[derived.get(cfg, cfg)]
    out = f'{tmp}/{derived.get(cfg, cfg)}'
    src = f'{R}/src'
    rel = os.path.relpath(out, src)
    cmd = ['make', '-C', src, '-n', '-B'] + args + [f'OUT={rel}', 'CCACHE=']
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        sys.exit(f'field-uses: {" ".join(cmd)} failed:\n{p.stderr}')
    unity = sorted(set(re.findall(r'\S*obj/unity/[\w.-]+\.c', p.stdout)))
    if unity:   # generated for real: the dry run only names them
        subprocess.run(['make', '-C', src, '-j', str(jobs), f'OUT={rel}', 'CCACHE='] + [os.path.relpath(u, src) if os.path.isabs(u) else u for u in unity],
                       check=True, capture_output=True)
    res = []
    for line in p.stdout.splitlines():
        try:
            argv = split_command(line)
        except ValueError:
            continue
        if not is_compiler(argv):
            continue
        flags, srcs = compile_args(argv)
        flags = system_dirs(argv[0]) + flags
        if cfg == 'x86':
            if platform.system() != 'Darwin':
                return []
            flags = [f for f in flags if not f.startswith('-mcpu=')] + ['--target=x86_64-apple-darwin']
        elif cfg == 'asan':
            flags += ['-fsanitize=address,undefined']
        elif cfg == 'tsan':
            flags += ['-fsanitize=thread']
        res += [(src, flags, s) for s in srcs]
    return res


def run_tu(job, sel, vpp, root):
    cwd, flags, source = job
    env = dict(os.environ)
    env.update(ENV)
    env['VCC_FIELD_USES'] = sel
    p = subprocess.run(['nice', '-n', '10', vpp, '-fsyntax-only', '-w'] + flags + [source], cwd=cwd, env=env, capture_output=True, text=True)
    return job, p


def norm_path(path, cwd, root):
    q = os.path.realpath(os.path.join(cwd, path))
    r = os.path.realpath(root)
    return os.path.relpath(q, r) if q == r or q.startswith(r + os.sep) else q


def norm_loc(text, cwd, root):
    """PATH:LINE[:COL] with the path normalised; '-' stays"""
    if text == '-':
        return text
    m = re.match(r'^(.*?)((?::\d+){1,2})$', text)
    return norm_path(m.group(1), cwd, root) + m.group(2) if m else text


def normalise(rec, cwd, root):
    k = rec[0]
    if k in ('use', 'other'):
        rec[4] = norm_loc(rec[4], cwd, root)
        rec[6] = norm_path(rec[6], cwd, root)
        rec[11] = norm_loc(rec[11], cwd, root)
        rec[3] = re.sub(r'\(anonymous at (.*?)\)', lambda m: '(anonymous at ' + norm_loc(m.group(1), cwd, root) + ')', rec[3])
    elif k == 'members':
        rec[2] = norm_loc(rec[2], cwd, root)
        rec[1] = re.sub(r'\(anonymous at (.*?)\)', lambda m: '(anonymous at ' + norm_loc(m.group(1), cwd, root) + ')', rec[1])
    return rec


def merge(reports):
    """reports: (records, configurations) pairs: the same record (all columns but EXPANSION) once, with its configurations"""
    seen = {}
    for recs, configs in reports:
        for rec in recs:
            if rec[0] == 'tu':
                continue
            key, body = (tuple(rec[:11]), rec[:12]) if rec[0] in ('use', 'other') else (tuple(rec), rec)
            seen.setdefault(key, [body, set()])[1].update(configs)
    return seen


def format_report(seen):
    def sk(item):
        b = item[1][0]
        if b[0] in ('use', 'other'):
            return (0, b[6], int(b[7]), b[0], b[3], b[2])
        return (1, b[0], tuple(b[1:]), 0, '', '')
    out = []
    for _, (body, cf) in sorted(seen.items(), key=sk):
        out.append('\t'.join(body + [','.join(sorted(cf))]))
    return out


def read_report(path):
    """records (lists) of a report written by this tool, each with its CONFIGS"""
    recs = []
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        if not f or not f[0]:
            continue
        recs.append(f)
    return recs


def merge_files(paths):
    """merge report files made earlier (one per run): rename-table.py --report A --report B"""
    seen = {}
    for p in paths:
        for f in read_report(p):
            if f[0] in ('use', 'other'):
                key = tuple(f[:11]); body, cf = f[:12], set(f[12].split(','))
            else:
                key = tuple(f[:-1]); body, cf = f[:-1], set(f[-1].split(','))
            e = seen.setdefault(key, [body, set()])
            e[1] |= cf
    return seen


def run(selection, configs, jobs, vpp, build, root, commands=None):
    tmp = f'{R}/build/field-uses-tmp'
    if commands is None and build:
        subprocess.run(['make', '-C', f'{R}/src', '-j', str(jobs), 'vcc'], check=True, capture_output=True)
    work = []   # (config, [(cwd, flags, source)])
    if commands is not None:
        jobs_l = []
        for line in open(commands):
            line = line.rstrip('\n')
            if not line.strip() or line.startswith('#'):
                continue
            cwd, cmd = line.split('\t', 1)
            argv = split_command(cmd)
            flags, srcs = compile_args(argv)
            jobs_l += [(os.path.join(root, cwd), flags, s) for s in srcs]
        work.append(('commands', jobs_l))
    else:
        for cfg in configs:
            work.append((cfg, make_commands(cfg, tmp, jobs)))
    reports, failed = [], []
    sel = os.path.abspath(selection)
    for cfg, tus in work:
        with ThreadPoolExecutor(jobs) as ex:
            results = list(ex.map(lambda j: run_tu(j, sel, vpp, root), tus))
        recs = []
        for (cwd, flags, source), p in results:
            lines = [l.split('\t') for l in p.stdout.splitlines() if l]
            if p.returncode or any(l[0] == 'tu' and l[2] != 'ok' for l in lines):
                failed.append(f'{cfg}: {source} (in {os.path.relpath(cwd, root)}) exit {p.returncode}\n  {" ".join([vpp, "-fsyntax-only"] + flags + [source])}\n{p.stderr[:600]}')
                continue
            recs += [normalise(l, cwd, root) for l in lines]
        reports.append((recs, [cfg]))
    return merge(reports), failed, work


def main():
    av = sys.argv[1:]
    if not av or av[0] in ('-h', '--help'):
        sys.exit(__doc__)

    def opt(name, default=None):
        if name in av:
            i = av.index(name)
            v = av[i + 1]
            del av[i:i + 2]
            return v
        return default
    out = opt('-o')
    jobs = int(opt('-j', '4'))
    root = opt('--root', R)
    commands = opt('--commands')
    vpp = opt('--bin-dir')
    vpp = f'{os.path.abspath(vpp)}/vpp' if vpp else f'{build_dir()}/vpp'
    build = '--no-build' not in av
    av = [a for a in av if a != '--no-build']
    configs = []
    while '--config' in av:
        configs.append(opt('--config'))
    if not configs:
        configs = [c for c in CONFIGS if c != 'x86' or platform.system() == 'Darwin']
        configs = [c for c in configs if c != 'wasm' or os.environ.get('VCC_WASM')]
    bad = [c for c in configs if c not in CONFIGS]
    if bad:
        sys.exit(f'field-uses: unknown configuration {bad[0]} (one of {", ".join(CONFIGS)})')
    sel = av[0]
    seen, failed, work = run(sel, configs, jobs, vpp, build, root, commands)
    for f in failed:
        print('FAIL ' + f, file=sys.stderr)
    text = '\n'.join(format_report(seen)) + '\n'
    if out:
        open(out, 'w').write(text)
    else:
        sys.stdout.write(text)
    ntu = sum(len(t) for _, t in work)
    print(f'field-uses: {ntu} TUs in {len(work)} configurations, {len(failed)} failed', file=sys.stderr)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
