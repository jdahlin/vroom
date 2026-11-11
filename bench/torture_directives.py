"""Conservative DejaGNU subset for the two 64-bit Linux torture targets.

Selectors follow target-supports-dg.exp; capabilities below follow predicates in
GCC's target-supports.exp. Unknown predicates remain unknown, including beneath
negation. No Tcl is executed. A false conjunct can establish non-applicability
without guessing the value of another target's effective-target predicate.
"""
import fnmatch
import re
import shlex


def words(text):
    """Read Tcl list words (nested braces and quoted words), without expansion."""
    out, i = [], 0
    while i < len(text):
        if text[i].isspace():
            i += 1
            continue
        if text[i] == '{':
            depth, start = 1, i + 1
            i += 1
            while i < len(text) and depth:
                if text[i] == '\\':
                    i += 2
                    continue
                depth += (text[i] == '{') - (text[i] == '}')
                i += 1
            if depth:
                raise ValueError('unclosed brace')
            out.append(words(text[start:i-1]))
        elif text[i] == '"':
            i += 1
            start = i
            while i < len(text) and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            if i >= len(text):
                raise ValueError('unclosed quote')
            out.append(text[start:i])
            i += 1
        else:
            start = i
            while i < len(text) and not text[i].isspace() and text[i] != '}':
                i += 1
            if start == i:
                raise ValueError('unexpected brace')
            out.append(text[start:i])
    return out


def truth_and(a, b):
    return False if a is False or b is False else None if a is None or b is None else True


def truth_or(a, b):
    return True if a is True or b is True else None if a is None or b is None else False


def selector(expr, target):
    if isinstance(expr, str):
        if '-*-' in expr or '*-*-*' == expr or expr.count('-') >= 2:
            triple = 'x86_64-pc-linux-gnu' if target.startswith('x86') else 'aarch64-unknown-linux-gnu'
            return fnmatch.fnmatchcase(triple, expr)
        if expr in {'indirect_calls', 'indirect_jumps', 'nonlocal_goto', 'label_values',
                    'untyped_assembly', 'non_strict_prototype', 'int32plus', 'ptr32plus',
                    'size32plus', 'int128', 'double64', 'double64plus', 'lp64', 'exceptions',
                    'named_sections', 'fpic', 'large_initializer', 'alloca', 'return_address',
                    'c99_runtime', 'int32', 'stdint_types', 'inf', 'inff'}:
            return True
        if expr in {'ia32', 'ilp32', 'newlib_nano_io', 'freestanding', 'stack_size'}:
            return False
        if expr == 'sse2_runtime':
            return target.startswith('x86')
        return None
    if expr and expr[0] == 'target':
        expr = expr[1:]
    if not expr:
        return None
    if '||' in expr:
        i = expr.index('||')
        return truth_or(selector(expr[:i], target), selector(expr[i+1:], target))
    if '&&' in expr:
        i = expr.index('&&')
        return truth_and(selector(expr[:i], target), selector(expr[i+1:], target))
    if expr[0] == '!':
        value = selector(expr[1:], target)
        return None if value is None else not value
    value = False
    for item in expr:
        value = truth_or(value, selector(item, target))
    return value


def directives(src):
    for line in src.splitlines():
        start = re.search(r'\{\s*dg-[\w-]+\b', line)
        if start:
            # Extract one balanced directive; reject multiple directives per line.
            depth, quoted, end = 0, False, None
            for i in range(start.start(), len(line)):
                if i and line[i-1] == '\\':
                    continue
                if line[i] == '"':
                    quoted = not quoted
                if not quoted:
                    depth += (line[i] == '{') - (line[i] == '}')
                    if not depth:
                        end = i + 1
                        break
            if end is None or re.search(r'\{\s*dg-', line[end:]):
                raise ValueError('multiline or multiple directives')
            yield words(line[start.start():end])[0]


def flags_for(src, target='x86_64-linux-gnu', action='compile'):
    flags = []
    try:
        for parts in directives(src):
            name, *args = parts
            if name in ('dg-options', 'dg-additional-options'):
                if not args or not isinstance(args[0], str) or len(args) > 2:
                    return None, 'malformed options'
                applies = selector(args[1], target) if len(args) == 2 else True
                if applies is None:
                    return None, 'unknown options selector'
                if applies:
                    opts = shlex.split(args[0])
                    if any(x in ('-m32', '-mx32', '-mbig-endian', '-mlittle-endian') for x in opts):
                        return None, 'unsupported target-changing options'
                    flags = opts if name == 'dg-options' else flags + opts
            elif name == 'dg-do':
                if not args or args[0] != action or len(args) > 2:
                    return None, 'unsupported dg-do action'
                applies = selector(args[1], target) if len(args) == 2 else True
                if applies is not True:
                    return None, 'dg-do target mismatch' if applies is False else 'unknown dg-do selector'
            elif name == 'dg-require-effective-target':
                applies = selector(args, target)
                if applies is not True:
                    return None, 'unavailable effective target' if applies is False else 'unsupported effective target ' + str(args)
            elif name in ('dg-skip-if', 'dg-xfail-if'):
                if not 2 <= len(args) <= 4:
                    return None, 'malformed ' + name
                applies = selector(args[1], target)
                if applies is False:
                    continue
                # Full flag-glob semantics remain unsupported when the target
                # matches. This never silently runs a skipped configuration.
                if applies is None or len(args) > 2:
                    return None, 'unsupported ' + name + ' selector or flags'
                if name == 'dg-skip-if':
                    return None, 'dg-skip-if: ' + args[0]
                # An applicable XFAIL is still run and counted as pass/fail.
            elif name in ('dg-require-alias', 'dg-require-weak') and args == ['']:
                pass  # ELF on both Linux targets supports symbol aliases and weak symbols.
            elif name == 'dg-require-stack-size' and len(args) == 1:
                pass  # No gcc,stack_size board limit; upstream returns immediately.
            elif name == 'dg-add-options' and args in (['ieee'], ['stack_size']):
                pass  # add_options_for_ieee/stack_size add no flags on these targets.
            else:
                return None, 'unsupported ' + name
    except (ValueError, TypeError):
        return None, 'malformed or unsupported directive syntax'
    return flags, ''


def replaces_default_options(src, target):
    """Whether an applicable dg-options replaces suite default options."""
    for parts in directives(src):
        if parts[0] == 'dg-options' and (len(parts) == 2 or selector(parts[2], target) is True):
            return True
    return False


def legacy_options(path, opt):
    """Recognize complete .x scripts used by builtins; reject any extra Tcl."""
    if not path.exists():
        return None, ''
    src = '\n'.join(line.strip() for line in path.read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith('#'))
    # These guards are false on both hosted Linux targets (target-supports.exp).
    src = src.removeprefix('load_lib target-supports.exp\n')
    for guard in ('if { ! [check_effective_target_nonlocal_goto] } {\nreturn 1\n}\n',
                  'if [istarget "epiphany-*-*"] {\nreturn 1\n}\n',
                  'if [istarget "nvptx-*-*"] {\nreturn 1\n}\n',
                  'if { [check_effective_target_freestanding] } {\nreturn 1;\n}\n'):
        src = src.replace(guard, '')
    if src in ('return 0', 'return 0;'):
        return None, ''
    m = re.fullmatch(r'set additional_flags (.+)\nreturn 0', src)
    if m:
        parsed = words(m.group(1))
        if len(parsed) == 1 and isinstance(parsed[0], str):
            return shlex.split(parsed[0]), ''
    for pattern in ('*-flto*', '*-Og*'):
        expected = 'set torture_eval_before_compile {\nif {[string match {' + pattern + '} "$option"]} {\ncontinue\n}\n}\nreturn 0'
        if src == expected and not fnmatch.fnmatchcase('-O' + opt, pattern):
            return None, ''
    return None, 'unsupported legacy .x script'
