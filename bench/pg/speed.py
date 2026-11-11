#!/usr/bin/env python3
"""Repeat an exact PostgreSQL compile corpus; report speed only for complete successful rounds.

Uses every C entry in compile_commands.json, including generated/test sources.
Prepare all object dependencies first; missing inputs are failures, never exclusions.
Objects and complete per-job diagnostics remain in a separate output directory.
"""
import argparse, hashlib, json, os, pathlib, platform, resource, shlex, shutil, subprocess, time
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORK = pathlib.Path(os.environ.get('VROOM_WORK', ROOT/'work')).resolve()
DEFAULT_MODES = ['clang', 'clang-O0', 'vpp', 'vpp-batch', 'vpp-O0', 'vpp-batch-O0']

def corpus(build):
    records = []
    for entry in json.loads((build/'compile_commands.json').read_text()):
        if not entry['file'].endswith('.c'): continue
        command = entry.get('arguments') or shlex.split(entry['command'])
        args = []; i = 1
        while i < len(command):
            x = command[i]
            if x in ('-MQ', '-MF', '-MT', '-o'):
                i += 2; continue
            if x not in ('-MD', '-MMD'): args.append(x)
            i += 1
        records.append(dict(id=len(records), file=entry['file'], directory=entry.get('directory', str(build)),
                            original=command, args=args, output=entry.get('output')))
    if not records: raise RuntimeError('compile database has no C jobs')
    return records

def cpu():
    r = resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime + r.ru_stime

def invoke(cmd, directory, prefix):
    with prefix.with_suffix('.stdout').open('wb') as stdout, prefix.with_suffix('.stderr').open('wb') as stderr:
        try:
            return subprocess.run(cmd, cwd=directory, stdout=stdout, stderr=stderr, timeout=300).returncode
        except OSError as error:
            stderr.write((str(error)+'\n').encode()); return 127
        except subprocess.TimeoutExpired:
            stderr.write(b'compiler timed out after 300 seconds\n'); return 124

def run(mode, jobs, workers, clang, vpp, destination):
    destination.mkdir(parents=True, exist_ok=False)
    commands = []
    for job in jobs:
        args = job['args']
        if mode.endswith('-O0'):
            args = [x for x in args if not x.startswith('-O') and not x.startswith('-g')] + ['-O0', '-g0']
        obj = destination/f"{job['id']:05d}.o"
        commands.append(dict(id=job['id'], file=job['file'], directory=job['directory'], object=str(obj),
                             argv=[clang if mode.startswith('clang') else vpp, *args, '-o', str(obj)]))
    (destination/'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
    t0 = time.perf_counter(); c0 = cpu(); load = os.getloadavg()
    batch_returncodes = []
    if mode.startswith('vpp-batch'):
        by_directory = {}
        for command in commands: by_directory.setdefault(command['directory'], []).append(command)
        statuses = {}
        for number, (directory, group) in enumerate(by_directory.items()):
            prefix = destination/f'batch-{number}'
            jobsfile = destination/f'batch-{number}.jobs'; stats = destination/f'batch-{number}.stats'
            jobsfile.write_text('\n'.join('\x1f'.join(['-']+c['argv'][1:]) for c in group)+'\n')
            rc = invoke([vpp, '--batch', str(jobsfile), '-j', str(workers), '-C', directory, '--stats', str(stats)], directory, prefix)
            batch_returncodes.append(rc)
            rows = stats.read_text().splitlines() if stats.exists() else []
            for i, command in enumerate(group):
                try: statuses[command['id']] = int(rows[i].split('\t')[0])
                except (IndexError, ValueError): statuses[command['id']] = 125
    else:
        def one(command):
            return command['id'], invoke(command['argv'], command['directory'], destination/f"job-{command['id']:05d}")
        with ThreadPoolExecutor(max_workers=workers) as pool: statuses = dict(pool.map(one, commands))
    wall = time.perf_counter()-t0; used_cpu = cpu()-c0
    results = []
    for command in commands:
        obj = pathlib.Path(command['object']); rc = statuses[command['id']]
        ok = rc == 0 and obj.is_file() and obj.stat().st_size > 0
        results.append(dict(id=command['id'], file=command['file'], returncode=rc, object_present=obj.is_file(), success=ok))
    successful = sum(r['success'] for r in results)
    complete = successful == len(jobs) and all(rc == 0 for rc in batch_returncodes)
    report = dict(mode=mode, wall=wall, cpu=used_cpu, attempted=len(jobs), successful=successful,
                  failed=len(jobs)-successful, complete=complete, batch_returncodes=batch_returncodes,
                  load_before=load, load_after=os.getloadavg(), jobs=results)
    (destination/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    # A failed batch retains the entire diagnostic stream. Replay only its failed
    # jobs after measurement to additionally preserve a diagnostic per source.
    if mode.startswith('vpp-batch') and not complete:
        for command, result in zip(commands, results):
            if not result['success']:
                args = command['argv'][:-1]+[str(destination/f"replay-{command['id']:05d}.o")]
                invoke(args, command['directory'], destination/f"failed-{command['id']:05d}")
    return report

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('-j', type=int, default=6); p.add_argument('-r', type=int, default=2)
    p.add_argument('--build-dir', type=pathlib.Path, default=pathlib.Path(os.environ.get('VCC_PG_BUILD', WORK/'build-pg-clang')))
    p.add_argument('--output', type=pathlib.Path, default=None)
    default_bin = ROOT/'build'/('linux/ccs' if platform.system() == 'Linux' else 'ccs')
    p.add_argument('--vpp', default=os.environ.get('VPP', str(pathlib.Path(os.environ.get('VCC_BIN', default_bin))/'vpp')))
    p.add_argument('--clang', default=os.environ.get('VCC_PG_CLANG') or (os.environ['LLVM']+'/bin/clang' if os.environ.get('LLVM') else '/opt/homebrew/opt/llvm/bin/clang' if platform.system() == 'Darwin' else shutil.which('clang')))
    p.add_argument('modes', nargs='*', choices=DEFAULT_MODES)
    a = p.parse_args()
    if a.j < 1 or a.r < 1: p.error('workers and rounds must be positive')
    output = (a.output or pathlib.Path(os.environ.get('TMPDIR','/tmp'))/('pg-speed-'+str(time.time_ns()))).resolve()
    output.mkdir(parents=True, exist_ok=False)
    jobs = corpus(a.build_dir.resolve()); modes = a.modes or DEFAULT_MODES
    manifest = dict(build=str(a.build_dir.resolve()), jobs=jobs, workers=a.j, rounds=a.r, modes=modes,
                    compile_database_sha256=hashlib.sha256((a.build_dir/'compile_commands.json').read_bytes()).hexdigest())
    for name, path in [('clang',a.clang),('vpp',a.vpp)]:
        executable = pathlib.Path(shutil.which(path) or path)
        manifest[name] = dict(path=str(executable), sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    (output/'corpus.json').write_text(json.dumps(manifest, indent=2)+'\n')
    reports = []
    print(f'{len(jobs)} C jobs; {a.r} rounds; -j {a.j}; artifacts {output}', flush=True)
    for round in range(a.r):
        for mode in modes if round % 2 == 0 else modes[::-1]:
            result = run(mode,jobs,a.j,a.clang,a.vpp,output/f'round-{round+1}-{mode}')
            reports.append(result)
            (output/'results.json').write_text(json.dumps(reports,indent=2)+'\n')
            if result['complete']:
                print(f"round {round+1} {mode}: {result['successful']}/{len(jobs)} successful, {result['wall']:.3f}s wall, {result['cpu']:.3f}s CPU",flush=True)
            else:
                print(f"round {round+1} {mode}: FAILED, {result['successful']}/{len(jobs)} successful; no throughput or speedup claimed",flush=True)
    changed = [name for name in ('clang', 'vpp')
               if hashlib.sha256(pathlib.Path(manifest[name]['path']).read_bytes()).hexdigest() != manifest[name]['sha256']]
    if changed:
        print('compiler changed during campaign: '+', '.join(changed)+'; no throughput or speedup claimed')
        (output/'invalid.json').write_text(json.dumps(dict(changed_compilers=changed))+'\n')
        return 1
    if any(not r['complete'] for r in reports): return 1
    best = {m:min((r for r in reports if r['mode']==m),key=lambda r:r['wall']) for m in modes}
    for mode, r in best.items():
        reference = best.get('clang-O0' if mode.endswith('-O0') else 'clang')
        comparison = f"; vs matching clang flags {reference['wall']/r['wall']:.2f}x" if reference else ''
        print(f"{mode}: best {r['wall']:.3f}s wall, {r['cpu']:.3f}s CPU, {r['successful']/r['wall']:.1f} successful jobs/s{comparison}")
    return 0

if __name__ == '__main__': raise SystemExit(main())
