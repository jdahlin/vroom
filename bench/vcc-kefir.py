#!/usr/bin/env python3
"""Linux Kefir end2end runtime contracts, Clang oracle and four ABI combinations.

Keeps host assertions enabled, each source's profile flags and all companion
objects. Compiler-internal asmgen/IR/AST/token snapshots are inventoried but are
not runtime tests. Unknown profiles and ARM-inapplicable assembly contracts are
explicit unsupported results, never passes. See kefir-source.json for provenance
and the documented compiler-driver adaptations. Every subprocess has retained
argv, environment overrides, stdout, stderr, status and a short timeout.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
from hostenv import BUILD, WORK, llvm

PIN = '7c59547068eaa0723b4db83d3fb70f448b0f0cb1'
TARGETS = ('x86_64-linux-gnu', 'aarch64-linux-gnu')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(cmd, cwd, prefix, timeout, env=None, stage=None):
    prefix.parent.mkdir(parents=True, exist_ok=True)
    record = {'argv': [str(x) for x in cmd], 'cwd': str(cwd), 'environment': env or {}}
    start = time.monotonic()
    error = None
    with prefix.with_suffix('.stdout').open('wb') as out, prefix.with_suffix('.stderr').open('wb') as err:
        try:
            proc = subprocess.Popen(cmd, cwd=cwd, stdout=out, stderr=err,
                                    env={**os.environ, 'LC_ALL': 'C', **(env or {})}, start_new_session=True)
            try:
                rc = proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
                rc, error = 124, 'timeout'
            if rc < 0:
                error = 'signal'
            elif rc in (126, 127):
                error = 'tool-error'
        except OSError as exc:
            err.write(str(exc).encode())
            rc, error = 127, 'tool-error'
    stderr = prefix.with_suffix('.stderr').read_text(errors='replace')
    if rc and re.search(r'unable to execute command|cannot execute|cannot find -l|cannot find .*crt|PLEASE submit a bug report|frontend command failed due to signal|internal compiler error|error while loading shared libraries|Could not open .*ld-linux', stderr, re.I):
        error = error or 'tool-error'
    if rc and stage in ('compile', 'assemble', 'link') and not stderr.strip() and not error:
        error = 'silent-tool-failure'
    record['stage'] = stage
    record.update(returncode=rc, error=error, seconds=round(time.monotonic()-start, 4))
    prefix.with_suffix('.json').write_text(json.dumps(record, indent=2))
    return record


def translate_flags(flags):
    """Translate only target selection; unknown semantic/profile options reject."""
    mapped, adaptations = [], []
    i = 0
    while i < len(flags):
        arg = flags[i]
        if arg == '--target' and i + 1 < len(flags) and flags[i+1] in ('host', 'host-none'):
            adaptations.append('profile target ' + flags[i+1] + ' replaced by selected Linux target')
            i += 2
            continue
        if arg in ('-c', '-Wcodegen-optimize'):
            adaptations.append(arg + ' mapped to adapter compile action' if arg == '-c' else arg + ' is Kefir backend-internal')
            i += 1; continue
        if arg == '-pie':
            mapped.append(arg); i += 1; continue
        if arg in ('-I', '-D', '-U', '-include'):
            if i + 1 == len(flags):
                return [], adaptations, 'missing argument for ' + arg
            mapped.extend(flags[i:i+2]); i += 2; continue
        if re.fullmatch(r'-O[0123sg]|-g[0-3]?|-std=(?:gnu|c)(?:11|17|18|23|2x)|-f(?:no-)?(?:common|PIC|pic|pie|PIE|signed-char|unsigned-char|fast-math|omit-frame-pointer)', arg) or arg.startswith(('-D', '-U', '-I')):
            mapped.append(arg); i += 1; continue
        return [], adaptations, 'unmapped profile option: ' + arg
    return mapped, adaptations, None


def profile(source, srcroot, out):
    path = Path(str(source) + '.profile')
    raw = path.read_text() if path.exists() else ''
    src = './' + str(source.relative_to(srcroot))
    base = ['--target', 'host-none', '-fPIC', '-pie', '-I', str(Path(src).parent),
            '-I', str(srcroot/'headers/kefir/runtime/common'), '-Wcodegen-optimize', '-c']
    result = {'raw': raw, 'base_flags': base, 'flags': [], 'environment': {}, 'adaptations': []}
    # Restrict evaluated shell contracts to the audited assignments/conditionals.
    # Unknown semantic environment variables must never silently disappear.
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('KEFIR_CFLAGS=') or re.match(r'export (?:LC_ALL|SOURCE_DATE_EPOCH|KEFIR_DISABLE_LONG_DOUBLE)=', line):
            continue
        if line in ('if [[ "x$USE_VALGRIND" == "xyes" ]]; then', 'fi'):
            continue
        result['error'] = 'unmapped profile statement: ' + line
        return result
    if not path.exists():
        result['raw_flags'] = base
        result['flags'], result['adaptations'], result['error'] = translate_flags(base)
        return result
    env = {'KEFIR_CFLAGS': shlex.join(base), 'SCRIPT': str(srcroot / 'source/tests/end2end/compile.sh'),
           'SRC_FILE': src, 'DST_FILE': str(out / 'unused.o'),
           'KEFIR_BIN_DIR': str(out), 'USE_VALGRIND': 'no', 'PLATFORM': 'linux',
           'LC_ALL': 'C', 'SOURCE_DATE_EPOCH': '', 'KEFIR_DISABLE_LONG_DOUBLE': ''}
    script = 'source "$1"; rc=$?; [ "$rc" = 0 ] || exit "$rc"; printf "%s\\0%s\\0%s\\0%s\\0" "$KEFIR_CFLAGS" "$LC_ALL" "$SOURCE_DATE_EPOCH" "$KEFIR_DISABLE_LONG_DOUBLE"'
    log = out / ('profile-' + source.name)
    r = invoke(['bash', '-c', script, 'profile', str(path)], srcroot, log, 10, env)
    if r['returncode'] or r['error']:
        result['error'] = 'profile evaluation failed'
        result['infrastructure_error'] = True
        return result
    values = log.with_suffix('.stdout').read_bytes().decode().split('\0')
    if len(values) != 5:
        result['error'] = 'unexpected profile output'
        result['infrastructure_error'] = True
        return result
    try:
        flags = shlex.split(values[0])
    except ValueError:
        result['error'] = 'invalid profile quoting'
        return result
    result['raw_flags'] = flags
    result['flags'], result['adaptations'], why = translate_flags(flags)
    result['environment'] = {k:v for k,v in zip(('LC_ALL','SOURCE_DATE_EPOCH','KEFIR_DISABLE_LONG_DOUBLE'), values[1:]) if v}
    if why:
        result['error'] = why
    return result


def eligibility(directory, target):
    mk = directory / 'Makefile.mk'
    if mk.exists():
        for line in mk.read_text().splitlines():
            if not line.strip() or line.startswith('KEFIR_END2END_TEST_NAME :=') or re.fullmatch(r'.*: override USE_VALGRIND=no', line):
                continue
            return 'unmapped per-test make contract' + (' (upstream extension support disabled by default)' if directory.name == 'extensions1' else '')
    run = directory / 'run.profile'
    if run.exists() and not re.fullmatch(r'export USE_VALGRIND=no(?:\s*#.*)?\s*', run.read_text()):
        return 'unmapped run.profile contract'
    for source in sorted(directory.iterdir()):
        if source.suffix not in ('.c', '.h'):
            continue
        text = source.read_text(errors='replace')
        if re.search(r'__KEFIRCC_DECIMAL(?:_BITINT_CONV)?_SUPPORT(?:_BID)?__', text):
            return 'guarded Kefir decimal capability contract: ' + source.name
        if target.startswith('aarch64') and re.search(r'__x86_64__|__amd64__|__i386__|__builtin_ia32|\b(?:asm|__asm__|__asm)\s*(?:(?:volatile|__volatile__)\s*)?\(', text):
            return 'target-specific x86/assembly contract: ' + source.name
    return None


def inventory(root, target):
    end = root / 'source/tests/end2end'
    records = []
    for directory in sorted(p for p in end.iterdir() if p.is_dir()):
        host = sorted(directory.glob('*.host.c'))
        test = sorted(directory.glob('*.kefir.c'))
        records.append({'name': directory.name, 'host': [str(p.relative_to(root)) for p in host],
                        'test': [str(p.relative_to(root)) for p in test],
                        'kind': 'runtime-pair' if host and test else 'compiler-internal-snapshot',
                        'unsupported': eligibility(directory, target) if host and test else 'no runtime host/test pair',
                        'makefile': (directory/'Makefile.mk').read_text() if (directory/'Makefile.mk').exists() else '',
                        'run_profile': (directory/'run.profile').read_text() if (directory/'run.profile').exists() else '',
                        'raw_profiles': {p.name:p.read_text() for p in sorted(directory.glob('*.profile'))}})
    return records


def one(case, args, root, out, includes, host_header):
    wd = out / 'cases' / case['name']; wd.mkdir(parents=True)
    result = {**case, 'status': 'unsupported', 'combinations': {}, 'profiles': {}}
    if case['unsupported']:
        result['reason'] = case['unsupported']; return result
    profiles = {}
    for name in case['test']:
        profiles[name] = profile(root/name, root, wd)
    result['profiles'] = profiles
    if any(p.get('error') for p in profiles.values()):
        if any(p.get('infrastructure_error') for p in profiles.values()):
            result['status'] = 'harness-error'
        result['reason'] = '; '.join(p['error'] for p in profiles.values() if p.get('error'))
        return result
    target = ['--target=' + args.target]
    hostflags = ['-std=c11', '-fno-common', '-fPIC', '-O3', '-DNDEBUG', '-DKEFIR_BUILD_RELEASE',
                 '-Wno-int-in-bool-context', '-UNDEBUG', '-I', str(root/'headers'), '-include', str(host_header)]
    testflags = ['-std=gnu17']
    result['host_flags'] = hostflags; result['test_base_flags'] = testflags
    objects = {}
    def compile_group(compiler, role):
        objects[(compiler, role)] = []
        for i, name in enumerate(case[role]):
            prefix = wd / f'{compiler}-{role}-{i}'
            obj = prefix.with_suffix('.o'); asm = prefix.with_suffix('.s')
            pr = profiles.get(name, {})
            flags = hostflags if role == 'host' else testflags + pr['flags']
            env = pr.get('environment', {})
            cmd = [args.clang if compiler == 'clang' else str(out/'vpp'), *target,
                   *(includes if compiler == 'vcc' else []), *flags, '-c' if compiler == 'clang' else '-S',
                   './'+name, '-o', str(obj if compiler == 'clang' else asm)]
            r = invoke(cmd, root, prefix, args.compile_timeout, env, stage='compile')
            if r['error']:
                return 'error', str(prefix.with_suffix('.json'))
            if r['returncode']:
                return 'compile-fail', str(prefix.with_suffix('.json'))
            if compiler == 'vcc':
                r = invoke([args.clang, *target, '-c', str(asm), '-o', str(obj)], root,
                           wd/f'{compiler}-{role}-{i}-assemble', args.compile_timeout, stage='assemble')
                if r['error']:
                    return 'error', str(wd/f'{compiler}-{role}-{i}-assemble.json')
                if r['returncode']:
                    return 'assemble-fail', str(wd/f'{compiler}-{role}-{i}-assemble.json')
            objects[(compiler, role)].append(str(obj))
        return None, None
    def link_run(host, test):
        label = host + '-' + test
        result.setdefault('combination_logs', {})[label] = {'link': str(wd/(label+'-link.json')), 'run': str(wd/(label+'-run.json'))}
        exe = wd / (label + '.exe')
        cmd = [args.clang, *target, '-no-pie', *objects[(host,'host')], *objects[(test,'test')],
               '-o', str(exe), '-pthread', '-latomic', '-lm']
        r = invoke(cmd, root, wd/(label+'-link'), args.compile_timeout, stage='link')
        if r['error']:
            return 'error'
        if r['returncode']:
            return 'link-fail'
        runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.target.startswith('aarch64') else []
        r = invoke([*runner, str(exe)], root, wd/(label+'-run'), args.timeout, stage='run')
        if r['error']:
            return 'error'
        return 'pass' if r['returncode'] == 0 else 'runtime-fail'
    for role in ('host', 'test'):
        status, log = compile_group('clang', role)
        if status:
            result.update(status='reference-'+status, reason=log); return result
    reference = link_run('clang','clang'); result['combinations']['clang-clang'] = reference
    if reference != 'pass':
        result['status'] = 'reference-' + reference; return result
    failures = {}
    for role in ('host','test'):
        status, log = compile_group('vcc', role)
        if status:
            failures[role] = status
            result.setdefault('compile_failures', {})[role] = {'status':status,'log':log}
    for host,test in [('clang','vcc'),('vcc','clang'),('vcc','vcc')]:
        blocked = [failures[role] for role,compiler in [('host',host),('test',test)] if compiler == 'vcc' and role in failures]
        result['combinations'][host+'-'+test] = ('error' if 'error' in blocked else 'compile-fail') if blocked else link_run(host,test)
    values = result['combinations'].values()
    result['status'] = 'vcc-error' if 'error' in values else 'pass' if all(s=='pass' for s in values) else 'vcc-fail'
    return result


def failed_run(counts):
    return not counts.get('pass') or any(k.startswith('vcc-') or k.endswith('-error') for k in counts)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('patterns', nargs='*', default=['*'])
    p.add_argument('--source', type=Path, default=Path(WORK)/'testsuites/kefir')
    p.add_argument('--target', choices=TARGETS, default=TARGETS[0])
    p.add_argument('--bin-dir', type=Path, default=Path(os.environ.get('VCC_BIN', BUILD)))
    p.add_argument('--output', type=Path)
    p.add_argument('-j', type=int, default=6)
    p.add_argument('--timeout', type=int, default=5)
    p.add_argument('--compile-timeout', type=int, default=30)
    args = p.parse_args(); args.clang = llvm('clang')
    if platform.system() != 'Linux' or not 1 <= args.j <= 6 or min(args.timeout,args.compile_timeout)<=0:
        p.error('Linux, 1..6 workers and positive timeouts required')
    if args.target.startswith('x86') and platform.machine() not in ('x86_64','AMD64'):
        p.error('x86 native execution requires an x86 host')
    required = [args.clang, str(args.bin_dir/'vpp'), llvm('llvm-objcopy'), 'bash'] + (['qemu-aarch64'] if args.target.startswith('aarch64') else [])
    if any(not shutil.which(t) for t in required):
        p.error('missing required tools: ' + ', '.join(t for t in required if not shutil.which(t)))
    out = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='vcc-kefir-'))
    if args.output: out.mkdir(parents=True, exist_ok=False)
    root = out/'source'
    rev = subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD'],text=True).strip()
    tree = subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD^{tree}'],text=True).strip()
    dirty = subprocess.check_output(['git','-C',str(args.source),'status','--porcelain'],text=True)
    if rev != PIN or dirty:
        p.error('source must be the clean pinned Kefir revision ' + PIN)
    root.mkdir()
    archive = subprocess.Popen(['git','-C',str(args.source),'archive',PIN],stdout=subprocess.PIPE)
    with tarfile.open(fileobj=archive.stdout, mode='r|') as tar:
        tar.extractall(root, filter='data')
    archive.stdout.close()
    if archive.wait():
        p.error('pinned source archive failed')
    shutil.copy2(args.bin_dir/'vpp', out/'vpp')
    shutil.copy2(__file__, out/'vcc-kefir.py'); shutil.copy2(Path(__file__).with_name('hostenv.py'),out/'hostenv.py')
    cases = inventory(root,args.target)
    selected = [c for c in cases if c['kind']=='runtime-pair' and any(fnmatch.fnmatchcase(c['name'],pat) for pat in args.patterns)]
    if not selected: p.error('no runtime pairs selected')
    rev = subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD'],text=True).strip()
    files = {str(f.relative_to(root)):sha(f) for f in root.rglob('*') if f.is_file()}
    mapping = {'source_revision':rev,'source_tree':tree,'source_dirty':dirty,'snapshot_method':'git archive '+PIN,'expected_pin':PIN,'target':args.target,'vpp_sha256':sha(out/'vpp'),
               'clang_version':subprocess.check_output([args.clang,'--version'],text=True),
               'inventory':cases,'selected':len(selected),'source_hashes':files,
               'scope':'runtime exit-status contracts only; four host/test compiler combinations; no asmgen snapshots',
               'driver_adaptations':['Kefir host-none target replaced with selected Linux target and system headers',
                                     'Kefir default C17 with GNU extensions maps to -std=gnu17; explicit profile -std flags retained',
                                     'Kefir -Wcodegen-optimize is backend-internal; runtime profile optimization levels are preserved',
                                     'host config generated by upstream detect-host-env.sh; no Kefir compiler identity is forged']}
    (out/'mapping.json').write_text(json.dumps(mapping,indent=2))
    q = subprocess.run([args.clang,'--target='+args.target,'-E','-x','c','-v','/dev/null'],capture_output=True,text=True,check=True)
    listing = q.stderr.split('#include <...> search starts here:',1)[1].split('End of search list.',1)[0]
    includes=[arg for line in listing.splitlines() if line.strip() for arg in ('-isystem',line.strip())]
    header=out/'host-env.h'
    tool_dir=out/'tools'; tool_dir.mkdir()
    (tool_dir/'objcopy').symlink_to(llvm('llvm-objcopy'))
    r=invoke(['sh',str(root/'scripts/detect-host-env.sh'),'--header'],root,out/'detect-host-env',args.compile_timeout,
             {'CC':shlex.join([args.clang,'--target='+args.target]),'CFLAGS':'-std=c11 -fno-common -fPIC -O3',
              'PATH':str(tool_dir)+os.pathsep+os.environ.get('PATH','')})
    if r['error'] or r['returncode']:
        raise SystemExit('upstream host detection failed; see '+str(out))
    shutil.copyfile(out/'detect-host-env.stdout',header)
    with ThreadPoolExecutor(max_workers=args.j) as pool:
        results=list(pool.map(lambda c:one(c,args,root,out,includes,header), selected))
    counts=dict(Counter(r['status'] for r in results))
    abnormalities=[]
    for log in sorted((out/'cases').glob('*/*.json')):
        record=json.loads(log.read_text())
        if record.get('error'):
            phase='run' if log.stem.endswith('-run') else 'assemble' if log.stem.endswith('-assemble') else 'link' if log.stem.endswith('-link') else 'compile/profile'
            abnormalities.append({'log':str(log), 'phase':phase, 'error':record['error'], 'returncode':record['returncode']})
    abnormal_counts=dict(Counter(e['phase']+':'+e['error'] for e in abnormalities))
    combos={name:dict(Counter(r['combinations'].get(name,'not-run') for r in results)) for name in ('clang-clang','clang-vcc','vcc-clang','vcc-vcc')}
    (out/'results.json').write_text(json.dumps({**mapping,'counts':counts,'combination_counts':combos,'abnormalities':abnormalities,'abnormal_counts':abnormal_counts,'results':results},indent=2))
    print(json.dumps(counts,sort_keys=True));print('Artifacts:',out)
    return int(failed_run(counts))


if __name__=='__main__':
    sys.exit(main())
