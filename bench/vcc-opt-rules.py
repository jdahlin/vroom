#!/usr/bin/env python3
"""Bounded optimization checks of the direct emitter, compiled and run at every level.

The checks of the code's shape are written for the direct emitter, so they run
at -O0 and -O1 (DIRECT_LEVELS); -O2 and -O3 are VIR's (doc/OPTIMIZATION.md),
compiled and, with --run, run, but their shape is not checked here.  What VIR
does not yet do of these rules is in TODO.md.

  bench/vcc-opt-rules.py --target x86_64-linux-gnu --run
  bench/vcc-opt-rules.py --target aarch64-linux-gnu --run
  bench/vcc-opt-rules.py --target wasm64-unknown-emscripten --run

Only --run requires target tools: clang/lld and native execution or qemu-aarch64,
or qemu-x86_64 on another host; wasm uses vcc/vlink and node (Node 24+
for wasm64 table64). No libc or SDK is used.
The wasm path needs neither Emscripten nor LLVM; --node selects the JS engine.
The ordinary C regression suites also run the source files for correctness.
"""
import argparse
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from hostenv import BUILD, R, llvm


# the levels whose code the direct emitter makes: the shape checks run at these alone
DIRECT_LEVELS = (0, 1)

def run(cmd, env=None):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=60, env=env)
    if p.returncode:
        raise RuntimeError(f'{cmd}: exit {p.returncode}\n{p.stdout}{p.stderr}')
    return p.stdout


def body(assembly, name, wasm):
    m = re.search(r'^' + re.escape(name) + r':[^\n]*\n', assembly, re.M)
    if not m:
        raise RuntimeError(f'missing probe {name}')
    tail = assembly[m.end():]
    end = (re.search(r'^\s*end_function\b', tail, re.M) if wasm else
           re.search(r'^\s*\.size\s+' + re.escape(name) + r'\s*,', tail, re.M))
    if not end:
        raise RuntimeError(f'missing function end for {name}')
    return tail[:end.start()]


def check_localvalues(assembly, arch):
    wasm = arch.startswith('wasm')
    text = body(assembly, 'lvr_square', wasm)
    add = r'i32\.add\b' if wasm else r'\badd\s+w' if arch == 'aarch64' else r'\baddl\b'
    if len(re.findall(add, text)) != 1:
        raise RuntimeError(f'sibling value must be computed once\n{text}')
    text = body(assembly, 'lvr_reuse', wasm)
    if len(re.findall(add, text)) != 1:
        raise RuntimeError(f'declaration value must be reused in return\n{text}')
    for name in ['lvr_target_assign', 'lvr_target_cached', 'lvr_target_input']:
        text = body(assembly, name, wasm)
        if len(re.findall(add, text)) != 1:
            raise RuntimeError(f'{name}: declaration value must be reused in local assignment\n{text}')
    for name in ['lvr_target_dep_prior', 'lvr_target_cached_prior', 'lvr_target_hidden']:
        text = body(assembly, name, wasm)
        if len(re.findall(add, text)) != 2:
            raise RuntimeError(f'{name}: invalidated value must be recomputed\n{text}')
    text = body(assembly, 'lvr_target_compound', wasm)
    if len(re.findall(add, text)) != 2:
        raise RuntimeError(f'value must survive integer-cast initializer and compound assignment\n{text}')
    text = body(assembly, 'lvr_cast_float', wasm)
    if len(re.findall(add, text)) < 3:
        raise RuntimeError(f'floating conversion must remain a barrier\n{text}')
    text = body(assembly, 'lvr_target_volatile', wasm)
    if len(re.findall(add, text)) != 2:
        raise RuntimeError(f'volatile assignment must not use cached value\n{text}')
    text = body(assembly, 'lvr_decl', wasm)
    if len(re.findall(add, text)) != 1:
        raise RuntimeError(f'declaration value must be reused in later declaration\n{text}')
    text = body(assembly, 'lvr_independent', wasm)
    if len(re.findall(add, text)) != 2:
        raise RuntimeError(f'value must survive an independent local write\n{text}')
    text = body(assembly, 'lvr_independent_compound', wasm)
    if len(re.findall(add, text)) != 2:
        raise RuntimeError(f'value must survive an independent compound write\n{text}')
    text = body(assembly, 'lvr_intervening', wasm)
    if len(re.findall(add, text)) != 2:
        raise RuntimeError(f'value must survive a pure declaration\n{text}')
    for name in ['lvr_assignment_call', 'lvr_assignment_volatile', 'lvr_assignment_hidden_write']:
        text = body(assembly, name, wasm)
        if len(re.findall(add, text)) < 3:
            raise RuntimeError(f'{name}: effectful assignment must remain a barrier\n{text}')
    text = body(assembly, 'lvr_volatile', wasm)
    load = r'i32\.load\b' if wasm else r'\bldr\s+w' if arch == 'aarch64' else r'observed\(%rip\)'
    if len(re.findall(load, text)) != 2:
        raise RuntimeError(f'volatile reads must both remain\n{text}')
    text = body(assembly, 'lvr_atomic', wasm)
    atomic = r'i32\.load\b' if wasm else r'(?:\bldar\b|\.inst\b)' if arch == 'aarch64' else r'\bmovl\s+\(%r'
    if len(re.findall(atomic, text)) != 2:
        raise RuntimeError(f'atomic reads must both remain\n{text}')


def check_double_unary(assembly, arch):
    wasm = arch.startswith('wasm')
    for name in ['du_neg', 'du_not', 'du_call', 'du_volatile']:
        text = body(assembly, name, wasm)
        pattern = r'i(?:32|64)\.(?:sub|xor)\b' if wasm else r'\b(?:negs?|not[lq]?|mvn)\b'
        if re.search(pattern, text):
            raise RuntimeError(f'{name}: retained double unary operation\n{text}')
    if wasm and not re.search(r'i32\.xor\b', body(assembly, 'du_narrow', True)):
        raise RuntimeError('narrowing conversion must not be removed')


def check_repeated(assembly, arch):
    wasm = arch.startswith('wasm')
    operations = {'ri_sub': r'(?:\bsubl\b|\bsub\s+w|\bi32\.sub\b)',
                  'ri_xor': r'\b(?:xor[lq]?|i64\.xor)\b',
                  'ri_and': r'\b(?:and[lq]?|i32\.and)\b',
                  'ri_or': r'\b(?:or[lq]?|i32\.or)\b'}
    for name, op in operations.items():
        text = body(assembly, name, wasm)
        # Native backends may use xor as the zeroing instruction.
        if name == 'ri_xor' and not wasm:
            continue
        if re.search(op, text):
            raise RuntimeError(f'{name}: retained repeated operation\n{text}')
    if wasm:
        for name in ['ri_eq', 'ri_ne', 'ri_lt', 'ri_ge', 'ri_cmp_branch']:
            text = body(assembly, name, True)
            if re.search(r'i(?:32|64)\.(?:eq|ne|lt_s|ge_u)\b', text):
                raise RuntimeError(f'{name}: retained self-comparison\n{text}')
        if not re.search(r'f64\.eq\b', body(assembly, 'ri_float', True)):
            raise RuntimeError('floating self-comparison must retain NaN behavior')
    text = body(assembly, 'ri_volatile_ptr', wasm)
    loads = (r'i32\.load' if wasm else r'\bldr(?:sw)?\b' if arch == 'aarch64' else r'\bmov(?:l|slq)\s+\(%r')
    if len(re.findall(loads, text)) < 2:
        raise RuntimeError(f'volatile pointer must be read twice\n{text}')
    text = body(assembly, 'ri_atomic_ptr', wasm)
    atomic = (r'i32\.load' if wasm else r'(?:\bldar\b|\.inst\b)' if arch == 'aarch64' else r'\bmovl\s+\(%r')
    if len(re.findall(atomic, text)) < 2:
        raise RuntimeError(f'atomic pointer must be read twice\n{text}')

def check_cmpzero(assembly, arch):
    if not arch.startswith('wasm'):
        return
    for name in ['cz_u32', 'cz_reverse', 'cz_u64', 'cz_signed', 'cz_effect', 'cz_volatile']:
        text = body(assembly, name, True)
        if not re.search(r'i(?:32|64)\.eqz\b', text):
            raise RuntimeError(f'{name}: expected zero-test instruction\n{text}')
    for name in ['cz_signed_le', 'cz_dynamic']:
        text = body(assembly, name, True)
        if not re.search(r'i32\.le_[su]\b', text):
            raise RuntimeError(f'{name}: comparison must stay relational\n{text}')


def check_cmpbounds(assembly, arch):
    wasm = arch.startswith('wasm')
    compare = r'\b(?:cmp[lq]?|[cs]et[a-z]+|i(?:32|64)\.(?:lt|gt|le|ge)_[su])\b'
    for name in ['cb_low', 'cb_high', 'cb_reverse', 'cb_effect', 'cb_branch', 'cb_volatile', 'cb_smax', 'cb_u64', 'cb_atomic',
                 'cb_expr_enum', 'cb_expr_enum_zero', 'cb_expr_smin', 'cb_expr_smin_reverse', 'cb_expr_s64min', 'cb_expr_umax',
                 'cb_expr_u64max', 'cb_expr_trunc', 'cb_expr_zero', 'cb_expr_complement', 'cb_expr_shift', 'cb_expr_div', 'cb_expr_effect',
                 'cb_expr_effect_left', 'cb_expr_volatile', 'cb_expr_atomic']:
        text = body(assembly, name, wasm)
        if re.search(compare, text):
            raise RuntimeError(f'{name}: retained comparison against a bound\n{text}')
    for name in ['cb_effect', 'cb_branch', 'cb_expr_effect', 'cb_expr_effect_left']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected one call to next\n{text}')
    for name in ['cb_impure_rhs', 'cb_impure_tree']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected one call to next\n{text}')
    loads = {'x86_64': (r'observed\(%rip\)', r'mov[lq]\s+\(%r'),
             'aarch64': (r'\bldr\b[^\n]*observed', r'(?:\bldar\b|\.inst\b)'),
             'wasm32': (r'i32\.load', r'i32\.load'),
             'wasm64': (r'i32\.load', r'i32\.load')}
    for name in ['cb_volatile', 'cb_expr_volatile']:
        if not re.search(loads[arch][0], body(assembly, name, wasm)):
            raise RuntimeError(f'{name}: volatile read disappeared')
    for name in ['cb_atomic', 'cb_expr_atomic']:
        if not re.search(loads[arch][1], body(assembly, name, wasm)):
            raise RuntimeError(f'{name}: atomic read disappeared')


def check_pow2(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    probes = ['opt_u32_div8', 'opt_u32_rem8', 'opt_u32_divhigh',
              'opt_u32_remhigh', 'opt_u32_div1', 'opt_u32_rem1',
              'opt_u64_div8', 'opt_u64_rem8', 'opt_u64_divhigh',
              'opt_u64_remhigh', 'opt_cast_div', 'opt_cast_rem',
              'opt_truncated_div', 'opt_signed_literal_div',
              'opt_compound_div', 'opt_compound_rem',
              'opt_compound_div64', 'opt_compound_rem64']
    for name in probes:
        text = body(assembly, name, wasm)
        if re.search(divide, text):
            raise RuntimeError(f'{name}: unsigned power-of-two retained divide/remainder\n{text}')
    # Both the optimized result and the discarded result must evaluate next().
    # The RHS comma controls additionally prevent treating a folded expression
    # as an effect-free literal and accidentally dropping its left operand.
    for name in ['effect_div', 'effect_rem', 'effect_zero',
                 'effect_denominator', 'effect_remdenominator']:
        text = body(assembly, name, wasm)
        calls = re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+next\b', text)
        if len(calls) != 1:
            raise RuntimeError(f'{name}: expected exactly one call to next\n{text}')
    # These are outside this rule. Keep a genuine divide in the dynamic
    # control; signed division may legitimately use another correct sequence.
    text = body(assembly, 'dynamic_div', wasm)
    if not re.search(divide, text):
        raise RuntimeError(f'dynamic_div: missing dynamic divide\n{text}')


def check_identities(assembly, arch):
    wasm = arch.startswith('wasm')
    # Reject the relevant arithmetic only within each named function. Address
    # and stack arithmetic may legitimately survive direct lowering.
    operations = {
        'mul': r'\b(?:i(?:32|64)\.mul|[i]?mul[lq]?)\b',
        'div': r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b',
        'or': r'\b(?:i(?:32|64)\.or|orr|or[lq]?)\b',
        'xor': r'\b(?:i(?:32|64)\.xor|eor|xor[lq]?)\b',
        'and': r'\b(?:i(?:32|64)\.and|and[lq]?)\b',
        'shift': r'\b(?:i(?:32|64)\.(?:shl|shr_[su])|[al]s[lr]|s[ah][lr][lq]?)\b',
    }
    probes = {
        'mul': ['id_mul', 'id_mulzero', 'id_umul64'],
        'div': ['id_div', 'id_remone', 'id_uremone64', 'id_sdiv64', 'id_sremone64'],
        'or': ['id_or', 'id_orones', 'id_uorones', 'id_uorones64'],
        'xor': ['id_xor'],
        'and': ['id_and', 'id_andzero', 'id_uand', 'id_uand64'],
        'shift': ['id_shl', 'id_shr'],
    }
    for operation, names in probes.items():
        for name in names:
            text = body(assembly, name, wasm)
            # x86 zeroing XOR is a valid implementation of a constant zero;
            # the XOR probe returns its argument and has no reason to zero it.
            if re.search(operations[operation], text):
                raise RuntimeError(f'{name}: retained identity {operation}\n{text}')
    for name, operation in [('id_add', 'add'), ('id_sub', 'sub')]:
        text = body(assembly, name, wasm)
        if wasm:
            redundant = re.search(r'i(?:32|64)\.const\s+0\s*\n\s*i(?:32|64)\.' + operation + r'\b', text)
        elif arch == 'x86_64':
            redundant = re.search(r'\b' + operation + r'[lq]?\s+\$0\s*,', text)
        else:
            redundant = re.search(r'\b' + operation + r'\s+[^\n]*,\s*#0\s*(?:\n|$)', text)
        if redundant:
            raise RuntimeError(f'{name}: retained zero {operation}\n{text}')
    for name in ['id_effect_mulzero', 'id_effect_andzero', 'id_effect_remone',
                 'id_effect_orones', 'id_effect_mulone', 'id_rhs_add',
                 'id_rhs_mulzero', 'id_rhs_andones']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+id_next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected exactly one call to id_next\n{text}')
    for name in ['id_volatile_zero', 'id_volatile_compound']:
        text = body(assembly, name, wasm)
        if wasm:
            read = re.search(r'\bi32\.load\b', text)
        elif arch == 'aarch64':
            read = re.search(r'\bldr\s', text)
        else:
            read = re.search(r'\bmov[lq]?\s+[^\n,]*\([^)]*\)[^\n,]*,\s*%', text)
        if not re.search(r'\bid_input\b', text) or not read:
            raise RuntimeError(f'{name}: dropped volatile operand read\n{text}')


def check_fallthrough(assembly, arch):
    # Inspect native control flow without requiring a full assembly snapshot.
    # Empty lines, comments and debug/CFI directives emit no instructions.
    # Every other directive and inline-asm block remains a barrier, just as an
    # opaque machine record does in the pass.
    # This extraction retains one bounded forward scan. Cascading deletions
    # may expose an earlier branch; ft_forward exercises those semantically,
    # while this simple positive probe already has an adjacent label run.
    probes = ['ft_adjacent']
    jump = re.compile(r'^\s*(?:jmpq?|b)\s+(\.L[\w.$]+)\s*(?:[#;].*)?$')
    label = re.compile(r'^(\.L[\w.$]+):\s*(?:[#;].*)?$')
    for name in probes:
        lines = []
        for line in body(assembly, name, False).splitlines():
            if re.match(r'^\s*(?:\.loc\b|\.cfi_\w+\b)', line):
                continue
            if re.match(r'^\s*(?:#|//)\s*(?:APP|NO_APP)\b', line):
                lines.append(line)
                continue
            if not line.strip() or re.match(r'^\s*(?:#|//)', line):
                continue
            lines.append(line)
        for i, line in enumerate(lines):
            target = jump.match(line)
            if not target:
                continue
            for following in lines[i + 1:]:
                destination = label.match(following)
                if not destination:
                    break
                if destination[1] == target[1]:
                    raise RuntimeError(f'{name}: jump to adjacent label {target[1]}')
    # The backwards goto remains a true control-flow edge; the pass must not
    # remove a branch merely because a matching label exists elsewhere.
    text = body(assembly, 'ft_backward', False)
    seen = set()
    for line in text.splitlines():
        destination = label.match(line)
        if destination:
            seen.add(destination[1])
        # Conditional or unconditional backwards edges are equally essential.
        branch = re.match(r'\s*(?:j\w+|b(?:\.\w+)?)\s+(\.L[\w.$]+)', line)
        if branch and branch[1] in seen:
            break
    else:
        raise RuntimeError('ft_backward: missing backward control-flow edge')


def check_loopback(assembly, arch):
    # Restrict the assertion to the first loop body: returning from the
    # function may use a separate outer-block branch after end_loop.
    for name, expected in [('loop_return', 0), ('loop_break', 0),
                           ('loop_continue', 1), ('loop_both', 0),
                           ('loop_normal', 1)]:
        text = body(assembly, name, True)
        loop = re.search(r'^\s*loop\s*$([\s\S]*?)^\s*end_loop\b', text, re.M)
        if not loop:
            raise RuntimeError(f'{name}: missing loop control')
        branches = re.findall(r'^\s*br\s+0\s*$', loop[1], re.M)
        if len(branches) != expected:
            raise RuntimeError(f'{name}: expected {expected} loop backedges, found {len(branches)}\n{loop[1]}')


def check_unreachable(assembly, arch):
    # These functions contain a dead call after a direct goto, return, break,
    # or continue. Inspect symbol references, rather than instruction counts:
    # register allocation and frame cleanup can independently change size.
    for name in ['dc_goto', 'dc_return', 'dc_break', 'dc_continue', 'dc_labels', 'dc_vla']:
        text = body(assembly, name, False)
        if re.search(r'\bdc_effect\b', text):
            raise RuntimeError(f'{name}: retained unreachable effect call\n{text}')
    for name in ['dc_goto', 'dc_return']:
        text = body(assembly, name, False)
        if re.search(r'\bdc_seen\b', text):
            raise RuntimeError(f'{name}: retained unreachable volatile store\n{text}')
    # Effects on both conditional paths and cleanup transfers must survive.
    text = body(assembly, 'dc_condition', False)
    if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+dc_effect\b', text)) != 2:
        raise RuntimeError(f'dc_condition: lost a reachable conditional effect\n{text}')
    text = body(assembly, 'dc_cleanup', False)
    if not re.search(r'\b(?:callq?|bl|b|jmpq?)\s+dc_clean\b', text):
        raise RuntimeError(f'dc_cleanup: lost reachable cleanup\n{text}')
    if arch == 'x86_64':
        # The a64 optimizer currently deletes this block before the shared
        # record pass. x86 directly exercises its conservative opaque barrier.
        # Unreachable after its goto: kept whole, or removed whole as arm64 and clang do
        # (the x86 walker drops unreachable statements); never the asm without its fallthrough.
        text = body(assembly, 'dc_raw_barrier', False)
        if ('dc_opaque_barrier' in text) != bool(re.search(r'\bcallq?\s+dc_effect\b', text)):
            raise RuntimeError(f'dc_raw_barrier: removed opaque asm or its fallthrough\n{text}')
    else:
        for name in ['dc_debugtrap', 'dc_builtin_debugtrap']:
            text = body(assembly, name, False)
            if not re.search(r'\bbrk\b', text) or not re.search(r'\bbl\s+dc_effect\b', text):
                raise RuntimeError(f'{name}: removed resumable trap or continuation\n{text}')


def check_signedpow2(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    probes = ['sp_div32_8', 'sp_rem32_8', 'sp_div32_neg8', 'sp_rem32_neg8',
                 'sp_div64_16', 'sp_rem64_16', 'sp_div64_neg16', 'sp_rem64_neg16',
                 'sp_div32_min', 'sp_rem32_min', 'sp_div64_min', 'sp_rem64_min',
                 'sp_div32_neg1', 'sp_rem32_neg1', 'sp_div64_neg1', 'sp_rem64_neg1',
                 'sp_div32_2', 'sp_rem32_2', 'sp_div32_high', 'sp_rem32_high',
                 'sp_div64_2', 'sp_rem64_2', 'sp_div64_high', 'sp_rem64_high',
                 'sp_nested', 'sp_narrow',
                 'sp_volatile_div', 'sp_compound', 'sp_char', 'sp_short']
    if wasm:
        probes = ['sp_div32_min', 'sp_div64_min', 'sp_div32_neg1',
                  'sp_rem32_neg1', 'sp_div64_neg1', 'sp_rem64_neg1']
        # LLVM also keeps these compact wasm operations: the engine lowers
        # constant division. Do not expand a bias sequence into many records.
        for name, op in [('sp_div32_8', 'div_s'), ('sp_rem32_8', 'rem_s')]:
            if not re.search(r'\bi32\.' + op + r'\b', body(assembly, name, True)):
                raise RuntimeError(f'{name}: lost compact wasm {op}')
    for name in probes:
        text = body(assembly, name, wasm)
        if re.search(divide, text):
            raise RuntimeError(f'{name}: retained signed power-of-two divide/remainder\n{text}')
    for name in ['sp_effect_div', 'sp_effect_rem']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+sp_next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected exactly one call to sp_next\n{text}')
    for name in ['sp_volatile_div', 'sp_compound']:
        text = body(assembly, name, wasm)
        if wasm:
            read = re.search(r'\bi32\.load\b', text)
            store = re.search(r'\bi32\.store\b', text)
        elif arch == 'aarch64':
            read = re.search(r'\bldr(?:sw)?\s', text)
            store = re.search(r'\bstr\s', text)
        else:
            read = re.search(r'\bmov(?:slq|[lq])?\s+[^\n,]*\([^)]*\)[^\n,]*,\s*%', text)
            store = re.search(r'\bmov[lq]?\s+%[^\n,]*,\s*[^\n]*\(', text)
        if not read or (name == 'sp_compound' and not store):
            raise RuntimeError(f'{name}: dropped volatile access\n{text}')


def check_extensions(assembly, arch):
    if arch != 'x86_64':
        return
    for name in ['ke_load', 'ke_add', 'ke_index']:
        text = body(assembly, name, False)
        if re.search(r'\bmovl\s+%(\w+)\s*,\s*%\1\b', text):
            raise RuntimeError(f'{name}: repeated unsigned extension\n{text}')


def check_locals(assembly, arch):
    if arch != 'x86_64':
        return
    # Selected scalar parameters bind to their homes at the ABI boundary.
    # Ignore callee-save slots and unrelated FP/variadic save areas: only a
    # store of the named incoming integer register reveals the old detour.
    probes = {
        'rl_bind': ['rdi|edi|di|dil'],
        'rl_bind_narrow': ['rdi|edi|di|dil', 'rsi|esi|si|sil',
                           'rdx|edx|dx|dl', 'rcx|ecx|cx|cl'],
        'rl_bind_mixed': ['rdi|edi|di|dil', 'rsi|esi|si|sil', 'rdx|edx|dx|dl'],
        'rl_bind_sret': ['rsi|esi|si|sil'],
        'rl_bind_variadic': ['rdi|edi|di|dil'],
    }
    homes = r'(?:[re]bx|bx|bl|r1[345][dwb]?)'
    for name, registers in probes.items():
        text = body(assembly, name, False)
        for register in registers:
            if re.search(r'\bmov\w*\s+%(?:' + register +
                         r')\s*,\s*-?[0-9]*\(%rbp\)', text):
                raise RuntimeError(f'{name}: selected parameter retained ABI spill\n{text}')
            if not re.search(r'\bmov\w*\s+%(?:' + register +
                             r')\s*,\s*%' + homes + r'\b', text):
                raise RuntimeError(f'{name}: missing direct ABI register binding\n{text}')
    text = body(assembly, 'rl_bind_stack', False)
    if not re.search(r'\bmovq\s+16\(%rbp\)\s*,\s*%' + homes + r'\b', text):
        raise RuntimeError(f'rl_bind_stack: missing direct incoming stack binding\n{text}')
    text = body(assembly, 'rl_cold', False)
    if re.search(r'%(?:rbx|ebx|r1[345][dwb]?)\b', text):
        raise RuntimeError(f'rl_cold: unprofitable preserved register binding\n{text}')
    # Confirm all four clobbered homes are actually used after the opaque asm;
    # memory-only lowering would pass the runtime checks without testing the
    # promotion/asm interaction. Snapshot stores and restores do not match.
    for name in ['rl_asm_four', 'rl_asm_goto_four']:
        text = body(assembly, name, False)
        marker = re.search(r'^.*mov \$123, %rbx;.*$', text, re.M)
        if not marker:
            raise RuntimeError(f'{name}: missing actual register clobbers')
        after = text[marker.end():]
        for register in ['rbx', 'r13', 'r14', 'r15']:
            if not re.search(r'\bmovq\s+%' + register + r'\s*,\s*%', after):
                raise RuntimeError(f'{name}: {register} not live after asm\n{after}')
    # These two straight-line-return probes need only their scalar loop state.
    # A save/restore pair (or one short-lived call spill) may touch a frame slot
    # twice. Repeated accesses to the same slot expose a stack-resident loop
    # local, regardless of register numbering or prologue layout.
    for name in ['rl_loop', 'rl_calls']:
        text = body(assembly, name, False)
        accesses = re.findall(r'(-?[0-9]+)\(%rbp\)', text)
        for slot in set(accesses):
            if accesses.count(slot) > 2:
                raise RuntimeError(f'{name}: repeated loop-local frame access at {slot}(%rbp)\n{text}')
        if not re.search(r'%(?:rbx|ebx|r1[345][dwb]?)\b', text):
            raise RuntimeError(f'{name}: missing preserved register local\n{text}')


def check_copies(assembly, arch):
    text = body(assembly, 'cp_stores', False)
    # Infer the preserved homes from ABI binding, without fixing register
    # numbers or total move counts. The first store reuses pointer/value
    # operands directly; an extra copy of either home is unnecessary.
    pointer = re.search(r'\bmovq\s+%rdi\s*,\s*%(\w+)\b', text)
    value = re.search(r'\bmovq\s+%rsi\s*,\s*%(\w+)\b', text)
    if not pointer or not value:
        raise RuntimeError(f'cp_stores: missing register homes for reused parameters\n{text}')
    first_store = re.search(r'\bmovq\s+%(\w+)\s*,\s*\(%(\w+)\)', text)
    if not first_store or (first_store[1], first_store[2]) != (value[1], pointer[1]):
        raise RuntimeError(f'cp_stores: retained copies of first-store operands\n{text}')
    # The runtime probes cover implicit CL/RAX/RDX operands, atomics, partial
    # writes, calls, labels and asm; keep those assertions semantic rather
    # than prescribing their physical register allocation.


def check_sdivconst(assembly, arch, optimized=True):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_s|sdiv|idiv[lq]?)\b'
    suffixes = ['p3', 'n3', 'p6', 'n6', 'p12', 'n12', 'p7', 'n7', 'p10', 'n10', 'p37', 'n37',
                'plarge', 'nlarge', 'pmax', 'nmax']
    for width in [32, 64]:
        selected = suffixes + (['p9', 'n9', 'p129', 'n129', 'pwide', 'nwide'] if width == 64 else [])
        for operation in ['q', 'r']:
            for suffix in selected:
                name = f'sd{width}_{operation}_{suffix}'
                text = body(assembly, name, wasm)
                retained = re.search(divide, text) is not None
                # Wasm has no economical signed high-product instruction.
                if retained != (wasm or not optimized):
                    expectation = 'retained' if wasm or not optimized else 'expanded'
                    raise RuntimeError(f'{name}: expected {expectation} signed division\n{text}')
            name = f'sd{width}_dynamic_{operation}'
            if not re.search(divide, body(assembly, name, wasm)):
                raise RuntimeError(f'{name}: lost genuine runtime division')
    for name in ['sd_effect', 'sd_effect_r', 'sd_rhs', 'sd_rhs_r', 'sd_compound_rhs']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected one call to next\n{text}')


def check_udivconst64(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i64\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    divisors = [3,5,7,10,11,14,19,37,63,4294967295,4294967297,
                9223372036854775807,9223372036854775809,18446744073709551615,
                1099511640121,281474976722345,72057594038002501,1152921504625935719]
    selected = [9223372036854775809,18446744073709551615] if wasm else divisors
    probes = ['u64_q'+str(d) for d in selected]
    if not wasm:
        probes += ['u64_r'+str(d) for d in divisors]
        probes += ['oa_div10', 'u64_compound', 'u64_compoundrem']
    for name in probes:
        text = body(assembly,name,wasm)
        if re.search(divide,text):
            raise RuntimeError(f'{name}: retained unsigned64 constant division\n{text}')
    for name in ['u64_effect','u64_effectrem','u64_rhs','u64_rhsrem']:
        text = body(assembly,name,wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+next\b',text)) != 1:
            raise RuntimeError(f'{name}: expected one call to next\n{text}')
    if wasm:
        for name in ['u64_q3','u64_r3']:
            if not re.search(divide,body(assembly,name,True)):
                raise RuntimeError(f'{name}: expanded unsupported wasm high product')
        if not re.search(r'i64\.ge_u\s*\n\s*i64\.extend_i32_u',body(assembly,'u64_q18446744073709551615',True)):
            raise RuntimeError('u64 high-divisor comparison must produce an i64 quotient')
    else:
        high = r'\bumulh\b' if arch=='aarch64' else r'\bmulq\b'
        if not re.search(high,body(assembly,'u64_q10',False)):
            raise RuntimeError('u64_q10: missing genuine high64 product')
    if not re.search(divide,body(assembly,'u64_dynamic',wasm)):
        raise RuntimeError('u64_dynamic: lost genuine runtime division')


def check_udivconst(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    divisors = [3, 5, 6, 7, 9, 10, 11, 14, 19, 31, 37, 1000, 65535,
                65537, 2147483647, 2147483649, 4294967295]
    # Wasm deliberately retains expensive correction/remainder sequences.
    # The simple quotient recipe uses one widened multiplication and shift.
    selected = [3, 5, 6, 9, 10, 11, 1000, 65535, 65537, 2147483649, 4294967295] if wasm else divisors
    probes = ['ud_q' + str(d) for d in selected] + ['ud_truncated', 'ud_negative_literal']
    if not wasm:
        probes += ['ud_r' + str(d) for d in divisors]
        probes += ['ud_cast', 'ud_castrem', 'ud_compound', 'ud_compoundrem']
    for name in probes:
        text = body(assembly, name, wasm)
        if re.search(divide, text):
            raise RuntimeError(f'{name}: retained constant division/remainder\n{text}')
    for name in ['ud_effect', 'ud_effectrem', 'ud_rhs', 'ud_rhsrem', 'ud_compound_rhs']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected one call to next\n{text}')
    if wasm:
        for name in ['ud_q7', 'ud_r3']:
            if not re.search(divide, body(assembly, name, True)):
                raise RuntimeError(f'{name}: expanded an expensive wasm fallback')
    if arch == 'aarch64' and not re.search(divide, body(assembly, 'ud_pressure', False)):
        raise RuntimeError('ud_pressure: missing conservative register-pressure fallback')
    if not re.search(divide, body(assembly, 'ud_dynamic', wasm)):
        raise RuntimeError('ud_dynamic: lost genuine runtime division')


def check_switches(assembly, arch):
    # The old sparse chain contains equality/range tests only. A balanced
    # dispatch must split both halves, with bounded native decision paths.
    wasm = arch.startswith('wasm')
    for name in ['sw_sparse32', 'sw_ranges32', 'sw_unsigned32', 'sw_unsigned64',
                 'sw_signed64', 'sw_broad64', 'sw_cross32']:
        text = body(assembly, name, wasm)
        split = (r'\bi(?:32|64)\.lt_[su]\b' if wasm else
                 r'\bb\.(?:lt|lo)\b' if arch == 'aarch64' else r'\bj(?:l|b)\s')
        if not re.search(split, text):
            raise RuntimeError(f'{name}: missing balanced split\n{text}')
    text = body(assembly, 'sw_sparse32', wasm)
    if wasm:
        depth = maximum = 0
        for line in text.splitlines():
            if re.match(r'^\s*if\s*$', line):
                depth += 1
                maximum = max(maximum, depth)
            elif re.match(r'^\s*end_if\b', line):
                depth -= 1
        if maximum != 4:
            raise RuntimeError(f'sw_sparse32: expected four balanced split levels, got {maximum}')
        if not re.search(r'\bbr_table\b', body(assembly, 'sw_dense_branch', True)):
            raise RuntimeError('sw_dense_branch: lost existing wasm branch table')
        return
    # Count the maximum conditional decisions along the actual emitted CFG.
    # The probe is acyclic and contains no calls/loops; labels/directives do
    # not count as instructions. This rejects the former 31-test chain.
    instructions, labels = [], {}
    for line in text.splitlines():
        label = re.match(r'^([.\w$]+):', line)
        if label:
            labels[label[1]] = len(instructions)
        elif line.strip() and not line.lstrip().startswith(('.', '#', '//')):
            instructions.append(line.strip())
    memo, visiting = {}, set()
    def path(i):
        if i >= len(instructions):
            return 0
        if i in memo:
            return memo[i]
        if i in visiting:
            raise RuntimeError('sw_sparse32: unexpected dispatch cycle')
        visiting.add(i)
        line = instructions[i]
        op = line.split()[0]
        branch = re.match(r'(?:j\w+|b(?:\.\w+)?)\s+([.\w$]+)', line)
        if op.startswith('ret'):
            result = 0
        elif branch and branch[1] in labels:
            target = path(labels[branch[1]])
            result = target if op in ['jmp', 'jmpq', 'b'] else 1 + max(target, path(i + 1))
        else:
            result = path(i + 1)
        visiting.remove(i)
        memo[i] = result
        return result
    maximum = path(0)
    if maximum > 10:
        raise RuntimeError(f'sw_sparse32: {maximum} decisions, expected <=10 for 31 cases')


def check_switchvalues(assembly, arch, optimized=True):
    wasm = arch.startswith('wasm')
    positive = ['sv_return', 'sv_assign', 'sv_signed64', 'sv_unsigned64',
                'sv_signed32', 'sv_narrow', 'sv_short', 'sv_range', 'sv_twice',
                'sv_effect', 'sv_volatile']
    for name in positive:
        text = body(assembly, name, wasm)
        tables = re.findall(r'\.Lswitch\.values\.\d+', text)
        if bool(tables) != optimized:
            raise RuntimeError(f'{name}: result-table eligibility changed\n{text}')
        if optimized and name == 'sv_twice' and len(set(tables)) != 2:
            raise RuntimeError('two switches must have distinct tables')
    for name in ['sv_cleanup', 'sv_fallthrough', 'sv_missing', 'sv_label',
                 'sv_nested', 'sv_sparse']:
        if '.Lswitch.values.' in body(assembly, name, wasm):
            raise RuntimeError(f'{name}: unsafe switch converted to table')
    if optimized:
        text = body(assembly, 'sv_effect', wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+sv_next\b', text)) != 1:
            raise RuntimeError('switch selector call must execute once')
    # Native inlining initially declines tables; wasm keeps a real function.
    if not wasm and '.Lswitch.values.' in body(assembly, 'sv_inline_twice', False):
        raise RuntimeError('inline table identity is not enabled')


def check_tails(assembly, arch, level, cfi=False, disabled=False):
    wasm = arch.startswith('wasm')
    positive = {
        'tc_i32': 'tc_sum32', 'tc_u32': 'tc_unsigned',
        'tc_i64': 'tc_pack64', 'tc_literal64': 'tc_pair64',
        'tc_uliteral64': 'tc_pairu64', 'tc_ptr': 'tc_pointer',
        'tc_ptr_swap': 'tc_pointer_pair', 'tc_constants': 'tc_pack32',
        'tc_cycle6': 'tc_six', 'tc_duplicate': 'tc_six', 'tc_empty': 'tc_zero',
    }
    negative = {
        'tc_conversion': 'tc_sum32', 'tc_arithmetic': 'tc_sum32',
        'tc_effect_argument': 'tc_sum32',
        'tc_cast': 'tc_sum32', 'tc_local': 'tc_sum32', 'tc_address': 'tc_load',
        'tc_cleanup': 'tc_sum32', 'tc_vla': 'tc_sum32', 'tc_stack9': 'tc_nine',
        'tc_vararg': 'tc_vararg_target', 'tc_fp': 'tc_fp_target',
        'tc_narrow': 'tc_byte_target', 'tc_noopt': 'tc_sum32',
        'tc_disabled': 'tc_sum32', 'tc_notail': 'tc_notail_target',
        'tc_twice': 'tc_twice_target', 'tc_caller_twice': 'tc_sum32',
    }
    (positive if arch == 'aarch64' else negative)['tc_cycle8'] = 'tc_eight'
    call = r'\bcall\s+' if wasm else r'\b(?:callq?|bl)\s+'
    jump = r'\b(?:jmpq?|b)\s+'
    for name, callee in positive.items():
        text = body(assembly, name, wasm)
        call_target = call + re.escape(callee) + r'\b'
        tail_target = jump + re.escape(callee) + r'\b'
        if wasm or level == 0 or disabled:
            if not re.search(call_target, text) or re.search(r'\breturn_call(?:_indirect)?\b', text):
                raise RuntimeError(f'{name}: expected ordinary call to {callee}\n{text}')
            continue
        tail = re.search(tail_target, text)
        if not tail or re.search(call_target, text):
            raise RuntimeError(f'{name}: missing direct tail transfer to {callee}\n{text}')
        # The transfer must follow frame restoration and end the instruction
        # stream. Avoid counting incidental moves or fixing register choices.
        after = text[tail.end():]
        if re.search(r'^\s*[a-z][\w.]*\s', after, re.M):
            raise RuntimeError(f'{name}: executable code follows tail transfer\n{text}')
        before = text[:tail.start()]
        if arch == 'x86_64' and re.search(r'\bpushq\s+%rbp\b', before):
            pop = re.search(r'\bpopq\s+%rbp\b', before)
            if not pop:
                raise RuntimeError(f'{name}: tail transfer retains frame pointer\n{text}')
            if cfi and not re.search(r'\.cfi_def_cfa\s+%rsp\s*,\s*8\b', before[pop.end():]):
                raise RuntimeError(f'{name}: tail transfer retains stale CFA\n{text}')
        elif arch == 'aarch64' and re.search(r'\bstp\s+x29\s*,\s*x30\s*,\s*\[sp', before):
            if not re.search(r'\bldp\s+x29\s*,\s*x30\s*,\s*\[sp\]\s*,\s*#16\b', before):
                raise RuntimeError(f'{name}: tail transfer retains link/frame save\n{text}')
            if cfi and not re.search(r'\.cfi_def_cfa\s+(?:sp|wsp|w31)\s*,\s*0\b', before):
                raise RuntimeError(f'{name}: tail transfer retains stale CFA\n{text}')
    for name, callee in negative.items():
        text = body(assembly, name, wasm)
        if not re.search(call + re.escape(callee) + r'\b', text):
            raise RuntimeError(f'{name}: expected conservative ordinary call to {callee}\n{text}')
    text = body(assembly, 'tc_indirect', wasm)
    indirect = r'\bcall_indirect\b' if wasm else r'\bcallq?\s+\*' if arch == 'x86_64' else r'\bblr\s'
    if not re.search(indirect, text):
        raise RuntimeError(f'tc_indirect: expected ordinary indirect call\n{text}')


def check_deadresults(assembly, arch):
    # Assert the semantic operation family, not register choices, instruction
    # totals or a full assembly snapshot. The discarded result still requires
    # evaluating effects and potentially trapping instructions.
    for name in ['dr_mul', 'dr_mul64', 'dr_diamond', 'dr_chain',
                 'dr_zero_left', 'dr_ones', 'dr_rem_one',
                 # These controls also discard a multiplication, proving the
                 # consumer ran while preserving their flags and ABI results.
                 'dr_overflow', 'dr_carry', 'dr_partial', 'dr_fp_result']:
        text = body(assembly, name, False)
        if re.search(r'\bimul[lq]?\b', text):
            raise RuntimeError(f'{name}: retained dead pure multiplication\n{text}')
    for name in ['dr_div', 'dr_remainder']:
        text = body(assembly, name, False)
        if not re.search(r'\bdiv[lq]?\b', text):
            raise RuntimeError(f'{name}: removed potentially trapping division\n{text}')
    text = body(assembly, 'dr_volatile', False)
    if not re.search(r'\bmovl\s+(?:-?[0-9]+)?\(%(?!rbp\b|rsp\b)\w+\)\s*,\s*%', text):
        raise RuntimeError(f'dr_volatile: removed volatile operand read\n{text}')
    text = body(assembly, 'dr_call', False)
    if len(re.findall(r'\bcallq?\s+dr_effect\b', text)) != 1:
        raise RuntimeError(f'dr_call: expected exactly one effectful call\n{text}')
    text = body(assembly, 'dr_fp_div', False)
    if not re.search(r'\bdivsd\b', text):
        raise RuntimeError(f'dr_fp_div: removed returned floating-point operation\n{text}')
    text = body(assembly, 'dr_opaque', False)
    if 'dr_opaque_barrier' not in text or not re.search(r'\bimul[lq]?\b', text):
        raise RuntimeError(f'dr_opaque: opaque whole-function fallback was lost\n{text}')


def scalar_memory_accesses(text, arch):
    """Count accesses in probes with one data object; ignore frame/literals.

    A memory RMW supplies one read and one write. No arbitrary CFG or alias
    inference is needed: these named probes contain only a single update.
    """
    if arch.startswith('wasm'):
        return (len(re.findall(r'\b(?:i32|i64|f32|f64)\.load\w*\b', text)),
                len(re.findall(r'\b(?:i32|i64|f32|f64)\.store\w*\b', text)))
    reads = writes = 0
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(('.', '#', '//')):
            continue
        fields = line.split(None, 1)
        if len(fields) != 2:
            continue
        op, operands = fields
        if arch == 'aarch64':
            if '[' not in operands or re.search(r'\[(?:sp|x29)\b|\.LC', operands):
                continue
            reads += op.startswith('ld')
            writes += op.startswith('st')
            continue
        if op.startswith('lea'):
            continue
        for index, operand in enumerate(operands.split(',')):
            if '(' not in operand or re.search(r'\(%(?:rbp|rsp)\b|\.LC', operand):
                continue
            destination = index == len(operands.split(',')) - 1
            if op.startswith('mov'):
                writes += destination
                reads += not destination
            else:
                reads += 1
                writes += destination and not op.startswith(('cmp', 'test'))
    return reads, writes


def check_postfix(assembly, arch):
    probes = ['pe_global_post', 'pe_global_pre', 'pe_global_postdec', 'pe_global_predec',
              'pe_global_unused', 'pe_global_cast', 'pe_global_post64', 'pe_byte_post',
              'pe_byte_pre', 'pe_bool_post', 'pe_bool_pre', 'pe_float_post', 'pe_double_post',
              'pe_pointer_post', 'pe_post', 'pe_pre', 'pe_postdec', 'pe_predec', 'pe_unused',
              'pe_short_post', 'pe_char_post', 'pe_plain', 'pe_address_post']
    for name in probes + ['pe_bits_post', 'pe_bits_unused']:
        text = body(assembly, name, arch.startswith('wasm'))
        expected = (2 if name.startswith('pe_bits_') else 1, 1)
        actual = scalar_memory_accesses(text, arch)
        if actual != expected:
            raise RuntimeError(f'{name}: expected reads/writes {expected}, found {actual}\n{text}')
    text = body(assembly, 'pe_address_post', arch.startswith('wasm'))
    if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+pe_address\b', text)) != 1:
        raise RuntimeError(f'pe_address_post: address must be evaluated once\n{text}')
    if arch == 'x86_64':
        for name in ['pe_atomic_post', 'pe_atomic_pre']:
            text = body(assembly, name, False)
            if not re.search(r'\block\s+(?:cmpxchg|xadd)\w*\b', text):
                raise RuntimeError(f'{name}: lost atomic RMW protocol\n{text}')
    if not arch.startswith('wasm'):
        text = body(assembly, 'pe_fixed_address', False)
        if scalar_memory_accesses(text, arch) != (1, 1):
            raise RuntimeError(f'pe_fixed_address: expected one scalar update\n{text}')

def check_bitrules(assembly, arch):
    wasm = arch.startswith('wasm')
    multiply = r'\b(?:imul[lq]?|mul|madd|i(?:32|64)\.mul)\b'
    for name in ['br_mul32', 'br_signed32', 'br_mul64', 'br_signed64',
                 'br_high32', 'br_high64', 'br_left', 'br_narrow', 'br_extend',
                 'br_compound', 'br_call', 'br_pointer_mul', 'br_volatile']:
        text = body(assembly, name, wasm)
        if re.search(multiply, text):
            raise RuntimeError(f'{name}: retained power-of-two multiply\n{text}')
    for name in ['br_not32', 'br_not64', 'br_not_signed', 'br_not_left',
                 'br_xor_compound', 'br_pointer_xor', 'br_volatile_xor']:
        text = body(assembly, name, wasm)
        good = (len(re.findall(r'\bi(?:32|64)\.xor\b', text)) == 1 if wasm else
                bool(re.search(r'\b(?:mvn|orn)\b', text)) if arch == 'aarch64' else
                bool(re.search(r'\bnot[lq]?\b', text)))
        if not good:
            raise RuntimeError(f'{name}: missing single complement\n{text}')
    for name, callee in [('br_call', 'br_next'), ('br_rhs', 'br_next'),
                         ('br_pointer_mul', 'br_pointer'), ('br_pointer_xor', 'br_pointer')]:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+' + callee + r'\b', text)) != 1:
            raise RuntimeError(f'{name}: expected one operand call\n{text}')
    for name in ['br_volatile', 'br_volatile_xor']:
        text = body(assembly, name, wasm)
        load = (r'\bi32\.load\b' if wasm else
                r'\bldr\s+w[^\n]*:lo12:br_observed' if arch == 'aarch64' else
                r'\bmovl\s+br_observed\(%rip\),')
        if not re.search(load, text):
            raise RuntimeError(f'{name}: lost volatile global load\n{text}')


def check_mulconst(assembly, arch):
    wasm = arch.startswith('wasm')
    probes = [(f'mc_{tag}_{factor}', shift) for tag in ['u32', 's32', 'u64', 's64']
              for factor, shift in [(3, 1), (5, 2), (9, 3)]]
    probes += [('mc_commuted', 2), ('mc_narrow', 1), ('mc_compound', 3),
               ('mc_post', 2), ('mc_pointer3', 1), ('mc_call5', 2), ('mc_volatile9', 3)]
    for name, shift in probes:
        text = body(assembly, name, wasm)
        if wasm:
            if not re.search(r'\bi(?:32|64)\.mul\b', text):
                raise RuntimeError(f'{name}: wasm should retain multiply\n{text}')
        elif arch == 'aarch64':
            if re.search(r'\b(?:madd|mul)\b', text) or not re.search(
                    r'\badd\s+[^\n]+,\s*lsl\s*#' + str(shift) + r'\b', text):
                raise RuntimeError(f'{name}: missing one shifted add\n{text}')
        elif re.search(r'\bimul[lq]?\b', text) or not re.search(
                r'\blea[lq]\s+[^\n]*,[^\n]*,' + str(1 << shift) + r'\)', text):
            raise RuntimeError(f'{name}: missing one scaled LEA\n{text}')
    # The direct volatile load is independent of the pre-existing x86
    # volatile-postincrement issue. Its multiply must still read the global.
    text = body(assembly, 'mc_volatile9', wasm)
    observed_load = (r'\bi32\.load\b' if wasm else
                     r'\bldr\s+w[^\n]*:lo12:mc_observed' if arch == 'aarch64' else
                     r'\bmovl\s+mc_observed\(%rip\),')
    if not re.search(observed_load, text):
        raise RuntimeError(f'mc_volatile9: lost volatile global read\n{text}')


def check_unusedinit(assembly, arch, optimized=True):
    wasm = arch.startswith('wasm')
    multiply = r'\b(?:imul[lq]?|mul|madd|i(?:32|64)\.mul)\b'
    positives = ['ui_pure', 'ui_unsigned', 'ui_conditional', 'ui_local_read']
    if optimized:
        for name in positives:
            text = body(assembly, name, wasm)
            if re.search(multiply, text):
                raise RuntimeError(f'{name}: retained unused pure integer initializer\n{text}')
    elif arch != 'aarch64':
        text = body(assembly, 'ui_pure', wasm)
        if not re.search(multiply, text):
            raise RuntimeError(f'ui_pure: unexpectedly optimized disabled initializer\n{text}')
    for name in ['ui_call', 'ui_comma', 'ui_short_effect', 'ui_braced', 'ui_vla', 'ui_cleanup']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl|b|jmpq?)\s+ui_touch\b', text)) != 1:
            raise RuntimeError(f'{name}: expected exactly one retained effect call\n{text}')
    text = body(assembly, 'ui_cleanup', wasm)
    if not re.search(r'\b(?:callq?|bl|b|jmpq?)\s+ui_clean\b', text):
        raise RuntimeError(f'ui_cleanup: lost cleanup call\n{text}')
    text = body(assembly, 'ui_volatile_read', wasm)
    read = r'\bi32\.load\b' if wasm else r'\bldr(?:sw)?\s' if arch == 'aarch64' else r'\bmov(?:slq|[lq])?\s+[^\n,]*\([^)]*\)[^\n,]*,\s*%'
    if 'ui_input' not in text or not re.search(read, text):
        raise RuntimeError(f'ui_volatile_read: lost volatile initializer read\n{text}')
    text = body(assembly, 'ui_load', wasm)
    if not re.search(read, text):
        raise RuntimeError(f'ui_load: lost initializer memory read\n{text}')
    # Existing arm64 record optimization already removes an unused integer
    # divide. Require retention only where the baseline actually retains it.
    if arch != 'aarch64':
        text = body(assembly, 'ui_div', wasm)
        if not re.search(r'\b(?:idiv[lq]?|i32\.div_s)\b', text):
            raise RuntimeError(f'ui_div: lost potentially trapping division\n{text}')
        for name in ['ui_used', 'ui_noopt', 'ui_atomic_destination', 'ui_volatile_destination']:
            text = body(assembly, name, wasm)
            if not re.search(multiply, text):
                raise RuntimeError(f'{name}: excluded initializer was omitted\n{text}')
        if not wasm:
            text = body(assembly, 'ui_fixed', False)
            if not re.search(multiply, text):
                raise RuntimeError(f'ui_fixed: lost asm-register initializer\n{text}')
    text = body(assembly, 'ui_float_conversion', wasm)
    if arch == 'aarch64':
        # The a64 printer emits FP instructions as .inst. Ignore register
        # fields, using the opcode from arch/a64/as/enc.c's fdiv entry.
        words = [int(word, 16) for word in re.findall(r'\.inst\s+0x([0-9a-fA-F]+)', text)]
        fp_divide = any(word & 0xffe0fc00 == 0x1e601800 for word in words)
        conversion = any(word & 0xfffffc00 == 0x1e780000 for word in words)
    else:
        fp_divide = re.search(r'\b(?:divsd|f64\.div)\b', text)
        conversion = re.search(r'\b(?:cvttsd2si\w*|i32\.trunc(?:_sat)?_f64_s)\b', text)
    if not fp_divide or not conversion:
        raise RuntimeError(f'ui_float_conversion: lost internal FP divide/conversion\n{text}')
    if optimized:
        text = body(assembly, 'ui_call', wasm)
        after_call = re.split(r'\b(?:callq?|bl|b|jmpq?)\s+ui_touch\b', text, maxsplit=1)[1]
        if wasm:
            if not re.match(r'\s*drop\b', after_call):
                raise RuntimeError(f'ui_call: initializer result retained in destination local\n{text}')
        elif arch == 'x86_64':
            if re.search(r'\bmov\w*\s+%[^\n,]+,\s*-?[0-9]+\(%rbp\)', after_call):
                raise RuntimeError(f'ui_call: initializer result retained in frame destination\n{text}')


def x64_start(check_preserved):
    if not check_preserved:
        return 'call main\nmov %eax, %edi\nmov $60, %eax\nsyscall\n'
    # External assembly caller: generated main must preserve all registers used
    # by local promotion, including through nested generated callees and asm.
    registers = [('rbx', 0x12345), ('r13', 0x23456), ('r14', 0x34567), ('r15', 0x45678)]
    text = ''.join(f'mov ${value}, %{reg}\n' for reg, value in registers)
    text += 'call main\ntest %eax, %eax\njnz 1f\n'
    for reg, value in registers:
        text += f'cmp ${value}, %{reg}\njne 2f\n'
    return text + ('1: mov %eax, %edi\nmov $60, %eax\nsyscall\n'
                   '2: mov $97, %edi\nmov $60, %eax\nsyscall\n')


def execute(args, directory, assembly, arch, source, level):
    obj = directory / 'probe.o'
    exe = directory / ('probe.wasm' if arch.startswith('wasm') else 'probe')
    if arch.startswith('wasm'):
        run([args.vpp, f'--target={args.target}', f'-O{level}',
             '-ffreestanding', '-fno-stack-protector', '-c', source, '-o', obj])
        # vlink selects its wasm implementation through this executable name.
        # For wasm32 the input object's imported memory selects 32-bit layout.
        linker = directory / 'wasm-ld'
        if not linker.exists():
            linker.symlink_to(Path(shutil.which(args.vlink) or args.vlink).resolve())
        run([linker] + (['-mwasm64'] if arch == 'wasm64' else []) +
            ['--no-entry', '--export=main', obj, '-o', exe])
        script = directory / 'run.js'
        script.write_text('const fs=require("fs");\n'
                          'WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {})'
                          '.then(({instance})=>{process.exit(instance.exports.main(0,' +
                          ('0n' if arch == 'wasm64' else '0') + '));})'
                          '.catch(e=>{console.error(e);process.exit(99);});\n')
        run([args.node, *args.node_flags, script, exe])
    else:
        if getattr(args, 'tail_obj', False):
            run([args.vpp, f'--target={args.target}', f'-O{level}', '-ffreestanding',
                 '-fno-stack-protector',
                 '-fasynchronous-unwind-tables' if args.cfi else '-fno-asynchronous-unwind-tables',
                 *(['-foptimize-sibling-calls'] if args.sibling_calls == 'on' else
                   ['-fno-optimize-sibling-calls'] if args.sibling_calls == 'off' else []),
                 '-c', source, '-o', obj])
            relocations = run([args.readobj, '-r', obj])
            jump = re.search(r'\bR_AARCH64_JUMP26\s+tc_sum32\b', relocations)
            if level in DIRECT_LEVELS and bool(jump) != (level >= 1 and args.sibling_calls != 'off'):
                raise RuntimeError(f'direct object: unexpected sibling-call relocation\n{relocations}')
            if not re.search(r'\bR_AARCH64_CALL26\s+tc_sum32\b', relocations):
                raise RuntimeError(f'direct object: lost conservative ordinary-call relocation\n{relocations}')
        else:
            assembler = args.vpp if arch == 'aarch64' else args.clang
            run([assembler, f'--target={args.target}', '-c', assembly, '-o', obj])
        start = directory / 'start.s'
        start.write_text('.text\n.globl _start\n_start:\n' +
                         (x64_start(args.rule in ['locals', 'localjmp', 'tails', 'deadresults', 'mulconst', 'all'])
                          if arch == 'x86_64' else
                          'bl main\nmov x8, #93\nsvc #0\n'))
        run([args.clang, f'--target={args.target}', '-fuse-ld=lld',
             '-nostdlib', '-static', '-Wl,-e,_start', start, obj, '-o', exe])
        runner = [args.qemu] if arch == 'aarch64' else []
        if arch == 'x86_64' and (args.qemu_x86 or platform.machine() != 'x86_64'):
            runner = [args.qemu_x86 or 'qemu-x86_64']
        run(runner + [exe])
        if arch == 'aarch64' and source.name == 'switch_values.c' and level == 1:
            # Exercise the direct emitter's per-function table data and its
            # interaction with PIC, line tables, and unwind directives.
            run([args.vpp, f'--target={args.target}', '-O2', '-ffreestanding',
                 '-fno-stack-protector', '-fPIC', '-g', '-fasynchronous-unwind-tables',
                 '-c', source, '-o', obj])
            run([args.clang, f'--target={args.target}', '-fuse-ld=lld',
                 '-nostdlib', '-static', '-Wl,-e,_start', start, obj, '-o', exe])
            run(runner + [exe])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    ap.add_argument('--target', default='x86_64-linux-gnu')
    ap.add_argument('--rule', choices=['pow2', 'signedpow2', 'identities', 'fallthrough', 'loopback', 'unreachable', 'locals', 'localjmp', 'copies', 'extensions', 'switches', 'tails', 'udivconst', 'udivconst64', 'sdivconst', 'deadresults', 'mulconst', 'bitrules', 'cmpbounds', 'cmpzero', 'repeated', 'doubleunary', 'localvalues', 'postfix', 'unusedinit', 'switchvalues', 'all'], default='all')
    ap.add_argument('--run', action='store_true', help='also execute literal-answer correctness checks')
    ap.add_argument('--cfi', action='store_true', help='also check unwind state with tail calls')
    ap.add_argument('--sibling-calls', choices=['default', 'on', 'off'], default='default',
                    help='override sibling-call optimization for tail-call probes')
    ap.add_argument('--tail-obj', action='store_true',
                    help='execute tails via arm64 direct C-to-object emission and check relocations')
    ap.add_argument('--readobj', default=llvm('llvm-readobj'))
    ap.add_argument('--clang', default=llvm('clang'))
    ap.add_argument('--vlink', default=str(Path(BUILD) / 'vlink'))
    ap.add_argument('--node', default='node')
    ap.add_argument('--qemu', default='qemu-aarch64')
    ap.add_argument('--qemu-x86', help='force x86 emulator; auto qemu-x86_64 on non-x86 hosts')
    args = ap.parse_args()
    arch = args.target.split('-')[0]
    if arch not in ['x86_64', 'aarch64', 'wasm32', 'wasm64']:
        ap.error('target must be x86_64, aarch64, wasm32 or wasm64')
    if args.rule == 'localjmp' and arch != 'x86_64':
        ap.error('localjmp tests x86 callee-save preservation')
    if args.tail_obj and (arch != 'aarch64' or args.rule != 'tails' or not args.run):
        ap.error('--tail-obj requires --rule tails --target aarch64-linux-gnu --run')
    args.node_flags = []
    if args.run and arch == 'wasm64':
        # Older V8 requires a flag; current Node enables memory64 by default
        # and rejects the removed experimental option. Table64 needs Node 24+.
        if '--experimental-wasm-memory64' in run([args.node, '--v8-options']):
            args.node_flags.append('--experimental-wasm-memory64')
    rules = {'pow2': ('unsigned_pow2.c', check_pow2),
             'signedpow2': ('signed_pow2.c', check_signedpow2),
             'identities': ('integer_identities.c', check_identities),
             'fallthrough': ('branch_fallthrough.c', check_fallthrough),
             'loopback': ('while_backedge.c', check_loopback),
             'unreachable': ('unreachable_records.c', check_unreachable),
             'locals': ('register_locals.c', check_locals),
             'localjmp': ('register_locals_setjmp.c', None),
             'copies': ('record_copies.c', check_copies),
             'extensions': ('known_extensions.c', check_extensions),
             'switches': ('switch_dispatch.c', check_switches),
             'switchvalues': ('switch_values.c', check_switchvalues),
             'tails': ('tail_calls.c', check_tails),
             'udivconst': ('unsigned_divconst.c', check_udivconst),
             'udivconst64': ('unsigned_divconst64.c', check_udivconst64),
             'sdivconst': ('signed_divconst.c', check_sdivconst),
             'deadresults': ('dead_results.c', check_deadresults),
             'mulconst': ('multiply_constants.c', check_mulconst),
             'bitrules': ('integer_bit_rules.c', check_bitrules),
             'cmpbounds': ('integer_cmp_bounds.c', check_cmpbounds),
             'repeated': ('repeated_integer.c', check_repeated),
             'doubleunary': ('double_unary.c', check_double_unary),
             'localvalues': ('local_value_reuse.c', check_localvalues),

             'cmpzero': ('integer_cmp_zero.c', check_cmpzero),
             'postfix': ('postfix_eval.c', check_postfix),
             'unusedinit': ('unused_scalar_init.c', check_unusedinit)}
    selected = rules if args.rule == 'all' else {args.rule: rules[args.rule]}
    with tempfile.TemporaryDirectory(prefix='vcc-opt-rules-') as tmp:
        directory = Path(tmp)
        for rule, (filename, check) in selected.items():
            if rule == 'localjmp' and arch != 'x86_64':
                continue
            source = Path(R) / 'src/test/vcc' / filename
            for level in range(4):
                assembly = directory / f'{rule}-O{level}.s'
                run([args.vpp, f'--target={args.target}', f'-O{level}',
                     '-ffreestanding', '-fno-stack-protector',
                     *(['-fasynchronous-unwind-tables'] if args.cfi else []),
                     *(['-foptimize-sibling-calls'] if args.sibling_calls == 'on' else
                       ['-fno-optimize-sibling-calls'] if args.sibling_calls == 'off' else []),
                     '-S', source, '-o', assembly])
                checked = ((arch == 'x86_64' and level >= 1) if rule in ['locals', 'copies', 'extensions', 'deadresults'] else
                           (not arch.startswith('wasm') and level >= 1) if rule in ['fallthrough', 'unreachable'] else
                           arch.startswith('wasm') if rule == 'loopback' else True)
                checked = checked and level in DIRECT_LEVELS and check is not None and (rule not in ['switches', 'udivconst', 'udivconst64', 'mulconst', 'bitrules', 'cmpbounds', 'cmpzero', 'repeated', 'doubleunary', 'localvalues'] or level >= 1)
                if checked:
                    if rule in ['sdivconst', 'unusedinit', 'switchvalues']:
                        check(assembly.read_text(), arch, optimized=level >= 1)
                    elif rule == 'tails':
                        check(assembly.read_text(), arch, level, args.cfi, args.sibling_calls == 'off')
                    else:
                        check(assembly.read_text(), arch)
                if rule == 'switchvalues' and arch.startswith('wasm') and level >= 1:
                    run([args.clang, f'--target={args.target}', '-c', assembly, '-o', directory / 'clang-switch-values.o'])
                if args.run:
                    execute(args, directory, assembly, arch, source, level)
                print(f'{args.target} {rule} O{level}: ' +
                      ('codegen' if checked else 'compile') +
                      (' and execution' if args.run else '') + ' OK')
            if rule in ['sdivconst', 'unusedinit', 'switchvalues']:
                # Check the debugging escape hatch independently of O0.
                assembly = directory / (rule + '-noopt.s')
                run([args.vpp, f'--target={args.target}', '-O1',
                     '-ffreestanding', '-fno-stack-protector', '-S', source, '-o', assembly],
                    env={**os.environ, 'VCC_NOOPT': '1'})
                check(assembly.read_text(), arch, optimized=False)
                print(f'{args.target} {rule} O1 VCC_NOOPT: codegen OK')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as e:
        print(e, file=sys.stderr)
        sys.exit(1)
