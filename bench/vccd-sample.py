#!/usr/bin/env python3
"""vccd-sample.py: where vccd's CPU goes, from a macOS `sample` call tree.

    sample $(pgrep -f "build/src/vccd --sock") 25 1 -mayDie -file OUT.sample   # during a build
    bench/vccd-sample.py OUT.sample [N]

Self samples of blocked frames (cvwait, kevent, read, mutex waits) are dropped;
the rest are attributed to a stage by their call path (preprocessing pulled by
the parser, parse + sema, emit, assembler, fixdep, writing, the I/O thread)
and the N hottest functions are listed.  `sample` stops each thread to walk
its stack, so a sampled build is slower: use it for shares, not times.
"""
import re, sys, collections
pat = re.compile(r'^([\s+!:|]*)(\d+)\s+(\S+)\s+\(in ([^)]+)\)')
IDLE = {'__psynch_cvwait', 'kevent', 'read', '__psynch_mutexwait', 'swtch_pri', '__select', '__semwait_signal', '__accept', 'accept', 'waitpid', '__wait4'}
rows = []
for l in open(sys.argv[1]):
    if l.startswith('Total number in stack'): break
    m = pat.match(l)
    if m: rows.append((len(m.group(1)), int(m.group(2)), m.group(3)))
# self = n - sum(direct children); single pass with a stack of [depth, n, name, childsum]
stack, out = [], []
def pop():
    d, n, f, cs, path = stack.pop()
    out.append((n - cs, f, path))
for d, n, f in rows:
    while stack and stack[-1][0] >= d: pop()
    if stack: stack[-1][3] += n
    stack.append([d, n, f, 0, tuple(x[2] for x in stack) + (f,)])
while stack: pop()
def stage(p):
    s = set(p)
    if 'lex_prefetch_thread_run' in s: return 'io thread: read source files'
    if 'fixdep_run' in s: return 'fixdep (.cmd files)'
    if any(x.startswith('emit') for x in p): return 'emit: codegen'
    if any(x.startswith(('as_', 'a64_')) for x in p): return 'assembler'
    if s & {'driver_object_write', 'write_file', 'driver_depfile_write'}: return 'write .o/.d'
    if any(x.startswith('ar_') or x == 'var_main' for x in p): return 'ar'
    pp = any(x.startswith(('pp_', 'lex_tokenize', 'lex_')) or x in ('pp_macro_body_substitute', 'pp_directive_run', 'pp_file_enter', 'pp_header_cache_token_record', 'pp_header_cache_read_record', 'pp_token_expanded_next_slow', 'pp_include_find', 'pp_include_find_inner', 'pp_header_cache_entry_publish', 'eval') for x in p)
    if 'cc_run' in s: return 'preprocess (pulled by parser)' if pp else 'parse + sema'
    if pp: return 'preprocess'
    if 'forward' in s: return 'forward to vroomd'
    if 'driver_declaration_cache_materialize' in s or 'cc_run.cold.3' in s: return 'decl cache'
    return 'other'
busy = idle = 0
tot, top = collections.Counter(), collections.Counter()
for c, f, p in out:
    if c <= 0: continue
    if f in IDLE: idle += c; continue
    busy += c; st = stage(p); tot[st] += c; top[(st, f)] += c
print(f'busy samples {busy}  (blocked/idle {idle})')
for k, v in tot.most_common(): print(f'  {100 * v / busy:5.1f}%  {k}')
print('\nhottest functions (self):')
for (st, f), v in top.most_common(int(sys.argv[2]) if len(sys.argv) > 2 else 20): print(f'  {100 * v / busy:5.1f}%  {f:28s} [{st}]')
