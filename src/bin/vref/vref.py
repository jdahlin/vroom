#!/usr/bin/env python3
"""Preview refactorings of one configured C or C++ translation unit."""

import argparse
import bisect
import collections
import difflib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


class Refusal(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise Refusal(message)


IDENT = re.compile(rb"[a-zA-Z_][a-zA-Z_0-9]*")
SCALAR = {b"int", b"long", b"short", b"char", b"float", b"double", b"unsigned"}


class Unit:
    def __init__(self, path, data, flags, overlay=None):
        self.data = data
        self.starts = []
        self.words = []
        self.bindings = collections.defaultdict(set)
        self.decls = {}
        self.macros = []
        self.language = None
        self.indirect = set()
        env = dict(
            os.environ,
            VCC_REF_QUERY="1",
            VCC_NODC="1",
            VCC_EAGER="1",
            VPP_NOHC="1",
            VREF_MAIN=str(path),
        )
        env.pop("VREF_OVERLAY", None)
        if overlay:
            env["VREF_OVERLAY"] = overlay
        compiler = Path(__file__).resolve().parent / "vref-lib/vpp"
        result = subprocess.run(
            [compiler, *flags, "-fsyntax-only", str(path)], stdout=subprocess.PIPE, env=env
        )
        require(result.returncode == 0, "frontend rejected the translation unit")
        header, _, payload = result.stdout.partition(b"\n")
        require(
            header == b"S " + str(len(data)).encode() and payload.startswith(data),
            "frontend source differs from requested snapshot",
        )
        for line in payload[len(data) :].splitlines():
            kind, first, second, extra = line.split(maxsplit=3)
            pos, value = int(first), int(second)
            if kind == b"T":
                require(extra == b"0", "source contains line-spliced tokens")
                self.starts.append(pos)
                self.words.append(data[pos : pos + value])
            elif kind == b"L":
                self.language = extra
            elif kind == b"M":
                self.macros.append((pos, value))
            elif pos < 0:
                self.indirect.add((value, extra))
            else:
                require(data[pos : pos + len(extra)] == extra, "source snapshot changed")
                self.bindings[pos].add(value)
                if kind in (b"V", b"P", b"F"):
                    self.decls[pos] = kind
        self.index = {pos: i for i, pos in enumerate(self.starts)}
        self.lines = [0] + [m.end() for m in re.finditer(b"\n", data)]

    def position(self, location):
        line, col = map(int, location.split(":"))
        require(1 <= line <= len(self.lines) and col >= 1, "invalid line or column")
        off = self.lines[line - 1] + col - 1
        end = self.lines[line] if line < len(self.lines) else len(self.data)
        require(off < end, "location outside line")
        i = bisect.bisect_right(self.starts, off) - 1
        require(i >= 0 and off < self.end(i), "select a source token")
        return i

    def end(self, i):
        return self.starts[i] + len(self.words[i])

    def target(self, i):
        targets = self.bindings.get(self.starts[i], ())
        require(len(targets) == 1, "select an unambiguous local, parameter, or static function")
        (target,) = targets
        require(target in self.decls, "selection has no supported main-file declaration")
        return target

    def occurrences(self, target):
        name = self.words[self.index[target]]
        require(
            (target, name) not in self.indirect, "occurrence outside editable source or in macro"
        )
        found = []
        for i, word in enumerate(self.words):
            if word != name:
                continue
            bindings = self.bindings.get(self.starts[i], ())
            require(len(bindings) == 1, "unresolved or multiply bound spelling: " + name.decode())
            if target in bindings:
                self.check_range(*self.span(i, i))
                found.append(i)
        return found

    def check_range(self, lo, hi, *, whole_macros=False):
        require(
            all(
                hi <= start or lo >= end or (whole_macros and lo <= start and end <= hi)
                for start, end in self.macros
            ),
            "edit intersects a macro expansion",
        )

    def span(self, first, last):
        return self.starts[first], self.end(last)

    def scalar(self, i):
        require(i > 0 and self.words[i - 1] in SCALAR, "expected a simple scalar declaration")
        return self.words[i - 1]

    def plain_local(self, i):
        self.scalar(i)
        require(
            self.decls.get(self.starts[i]) == b"V"
            and i >= 2
            and self.words[i - 2] in (b"{", b";", b"}")
            and self.words[i + 1 : i + 2] == [b";"],
            "expected an uninitialized scalar local",
        )
        return self.span(i - 1, i + 1)


def identifier(text):
    word = text.encode()
    require(IDENT.fullmatch(word), "expected an ASCII identifier")
    return word


def scalar(text):
    word = text.encode()
    require(word in SCALAR, "expected a simple scalar type")
    return word


def integer(text):
    value = text.encode()
    require(re.fullmatch(rb"(0|[1-9][0-9]*)", value), "expected a decimal integer literal")
    return value


def signature(unit, target, op, args):
    require(unit.decls[target] == b"F", "select a file-local static function definition")
    edits = []
    arity = None
    sites = unit.occurrences(target)
    require(
        all(unit.words[i + 1 : i + 2] == [b"("] for i in sites), "function address or indirect use"
    )
    decl = unit.index[target]
    for i in [decl] + [x for x in sites if x != decl]:
        # This milestone handles simple scalar parameters and single-token arguments.
        w = unit.words
        j = i + 2
        parts = []
        while j < len(w) and w[j] != b")":
            start = j
            while j < len(w) and w[j] not in (b",", b")"):
                j += 1
            require(j < len(w), "unterminated argument list")
            parts.append((start, j))
            if w[j] == b",":
                j += 1
        require(j < len(w), "unterminated signature")
        definition = i == decl
        if definition:
            require(w[j + 1 : j + 2] == [b"{"], "expected a single function definition")
            if len(parts) == 1 and w[parts[0][0] : j] == [b"void"]:
                parts = []
            for a, b in parts:
                require(
                    b - a == 2 and w[a] in SCALAR and unit.decls.get(unit.starts[a + 1]) == b"P",
                    "expected named scalar parameters",
                )
            arity = len(parts)
        else:
            require(
                len(parts) == arity and all(b - a == 1 for a, b in parts),
                "call must have one simple token per argument",
            )
        if op == "change-param-type":
            k = args.param - 1
            require(0 <= k < len(parts), "parameter index outside signature")
            if definition:
                a, _ = parts[k]
                edits.append((*unit.span(a, a), scalar(args.type)))
            continue
        lo, hi = unit.end(i + 1), unit.starts[j]
        require(
            b"/*" not in unit.data[lo:hi] and b"//" not in unit.data[lo:hi],
            "comments between arguments need manual placement",
        )
        values = [unit.data[unit.starts[a] : unit.end(b - 1)] for a, b in parts]
        if op == "add-param":
            typ, name, arg = scalar(args.type), identifier(args.name), integer(args.argument)
            require(name not in w, "new parameter name already occurs in source")
            values.append(typ + b" " + name if definition else arg)
        else:
            k = args.param - 1
            require(0 <= k < len(values), "parameter index outside signature")
            if op == "swap-param":
                n = args.other - 1
                require(0 <= n < len(values) and n != k, "invalid second parameter")
                values[k], values[n] = values[n], values[k]
            elif op == "remove-param":
                if definition:
                    p = unit.starts[parts[k][0] + 1]
                    require(len(unit.occurrences(p)) == 1, "parameter is used")
                else:
                    integer(values[k].decode())
                del values[k]
        edits.append((lo, hi, b", ".join(values) or (b"void" if definition else b"")))
    return edits


def extract(unit, start, stop, name):
    """Extract scalar expression statements. Pass referenced int locals by address."""
    w = unit.words
    require(
        start > 0 and stop >= start and w[start - 1] in (b"{", b";") and w[stop] == b";",
        "select complete expression statements",
    )
    functions = [i for i, p in enumerate(unit.starts[:start]) if unit.decls.get(p) == b"F"]
    require(functions, "extract inside a file-local static function")
    fn = functions[-1]
    require(
        fn >= 2 and w[fn - 2] == b"static" and w[fn - 1] in SCALAR | {b"void"},
        "expected a simple static function",
    )
    require(fn == 2 or w[fn - 3] in (b"}", b";"), "unsupported enclosing declaration")
    depth = 0
    for word in w[fn:start]:
        depth += (word == b"{") - (word == b"}")
        require(word != b"#" and not (word == b"}" and depth == 0), "selection outside function")
    require(depth > 0 and name not in w, "invalid location or helper name already in use")
    params = {}
    lo, hi = unit.span(start, stop)
    replacements = []
    for i in range(start, stop + 1):
        word = w[i]
        if re.fullmatch(rb"[0-9]+", word) or word in (
            b";",
            b"=",
            b"+=",
            b"-=",
            b"*=",
            b"+",
            b"-",
            b"*",
            b"/",
            b"%",
            b"(",
            b")",
            b"++",
            b"--",
        ):
            continue
        target = unit.target(i)
        di = unit.index[target]
        require(
            unit.decls[target] in (b"V", b"P")
            and di < start
            and unit.scalar(di) == b"int"
            and di >= 2
            and w[di - 2] in (b"(", b",", b"{", b";", b"}")
            and w[di + 1 : di + 2] in ([b","], [b")"], [b"="], [b";"]),
            "extraction supports ordinary int locals and parameters",
        )
        require(word not in params or params[word] == target, "ambiguous capture")
        params[word] = target
        replacements.append((unit.starts[i] - lo, unit.end(i) - lo, b"(*" + word + b")"))
    body = apply(unit.data[lo:hi], replacements)
    helper = (
        b"static void "
        + name
        + b"("
        + (b", ".join(b"int *" + p for p in params) or b"void")
        + b")\n{\n    "
        + body
        + b"\n}\n\n"
    )
    call = name + b"(" + b", ".join(b"&" + p for p in params) + b");"
    return [(unit.starts[fn - 2], unit.starts[fn - 2], helper), (lo, hi, call)]


def apply(data, edits):
    pieces = []
    cursor = 0
    for lo, hi, value in sorted(edits):
        require(cursor <= lo <= hi <= len(data), "overlapping source edits")
        pieces.extend((data[cursor:lo], value))
        cursor = hi
    return b"".join([*pieces, data[cursor:]])


def plan(unit, args, i):
    if args.op == "extract":
        return extract(unit, i, unit.position(args.end), identifier(args.name))
    target = unit.target(i)
    decl = unit.index[target]
    if args.op in SIGNATURE:
        return signature(unit, target, args.op, args)
    require(unit.decls[target] in (b"P", b"V"), "expected local variable or parameter")
    occurrences = unit.occurrences(target)
    if args.op == "rename":
        name = identifier(args.name)
        # The incomplete C++ frontend can accept duplicate declarations. Where
        # the destination spelling already exists, coverage is insufficient.
        require(name not in unit.words, "destination name already occurs in this translation unit")
        return [(*unit.span(k, k), name) for k in occurrences]
    require(unit.decls[target] == b"V", "use a signature edit for a parameter")
    if args.op == "change-type":
        unit.scalar(decl)
        require(
            decl >= 2
            and unit.words[decl - 2] in (b"{", b";", b"}")
            and unit.words[decl + 1 : decl + 2] in ([b";"], [b"="]),
            "expected a standalone scalar declaration",
        )
        for word in unit.words[decl + 1 :]:
            if word == b";":
                break
            require(word not in (b",", b"{", b"}"), "complex or multiple declarators")
        return [(*unit.span(decl - 1, decl - 1), scalar(args.type))]
    lo, hi = unit.plain_local(decl)
    if args.op == "delete-var":
        require(len(occurrences) == 1, "variable is used")
        begin = unit.data.rfind(b"\n", 0, lo) + 1
        newline = unit.data.find(b"\n", hi)
        if newline >= 0 and not unit.data[begin:lo].strip() and not unit.data[hi:newline].strip():
            lo, hi = begin, newline + 1
        require(
            b"/*" not in unit.data[lo:hi] and b"//" not in unit.data[lo:hi],
            "declaration contains a comment",
        )
        return [(lo, hi, b"")]
    other = decl + (-3 if args.op == "move-up" else 3)
    require(0 <= other < len(unit.words), "no adjacent declaration")
    left, right = unit.plain_local(other)
    return [(lo, hi, unit.data[left:right]), (left, right, unit.data[lo:hi])]


SIGNATURE = {"add-param", "swap-param", "remove-param", "change-param-type"}


def storage_rules(storage):
    """Derive growth, copy-out and teardown edits from a column-to-field map."""
    keys = {"columns", "count", "capacity", "items", "index", "growth_sites", "initial_capacity"}
    require(
        isinstance(storage, dict) and set(storage) in (keys, keys | {"index_type"}),
        "invalid storage description",
    )

    def name(value):
        require(isinstance(value, str) and IDENT.fullmatch(value.encode()), "expected identifier")
        return value

    count, capacity, index = (name(storage[k]) for k in ("count", "capacity", "index"))
    initial, items, columns = (storage[k] for k in ("initial_capacity", "items", "columns"))
    require(
        type(initial) is int and initial > 0 and isinstance(items, str) and items,
        "invalid record storage",
    )
    require(isinstance(columns, list) and len(columns) > 1, "expected parallel columns")
    grow = [
        f"if ( @{count} == @{capacity} ) {{ @{capacity} = @{capacity} ? @{capacity} * 2 : {initial} ;"
    ]
    copy = [f"if ( @{count} ) {{"]
    index_type = name(storage.get("index_type", "uint32_t"))
    loop = [f"for ({index_type} {index} = 0; {index} < {count}; {index}++) {{"]
    release = []
    for column in columns:
        require(
            isinstance(column, dict) and set(column) == {"array", "type", "field", "output"},
            "invalid column",
        )
        array, field = name(column["array"]), name(column["field"])
        typ, dest = column["type"], column["output"]
        require(
            isinstance(typ, str) and isinstance(dest, str) and typ and dest,
            "invalid type or output",
        )
        grow.append(
            f"@{array} = ( {typ} * ) realloc ( @{array} , @{capacity} * sizeof * @{array} ) ;"
        )
        copy.append(f"memcpy ( {dest} , @{array} , @{count} * sizeof ( {typ} ) ) ;")
        # Keep the destination a single lvalue when adding the subscript.
        require(
            re.fullmatch(r"[A-Za-z_]\w*(?: (?:->|\.) [A-Za-z_]\w*)*", dest, re.ASCII),
            "output must be a token-spaced variable or member path",
        )
        loop.append(f"    {''.join(dest.split())}[{index}] = {items}[{index}].{field};")
        release.append(f"free ( @{array} ) ;")
    return [
        {"match": " ".join([*grow, "}"]), "replace": "", "count": storage["growth_sites"]},
        {"match": " ".join([*copy, "}"]), "replace": "\n".join([*loop, "}"])},
        {"match": " ".join(release), "replace": ""},
    ]


def closing_brace(words, opening, limit):
    depth = 1
    for i in range(opening + 1, limit):
        depth += (words[i] == b"{") - (words[i] == b"}")
        if not depth:
            return i
    raise Refusal("unbalanced scope")


def recipe_scope(unit, within):
    words = unit.words
    header = within.encode().split()
    require(header and header[-1] == b"{", "within must end at the opening brace")
    starts = [
        i for i, w in enumerate(words) if w == header[0] and words[i : i + len(header)] == header
    ]
    require(len(starts) == 1, "expected one matching scope")
    first = starts[0] + len(header)
    last = closing_brace(words, first - 1, len(words))
    locals_by_name = collections.defaultdict(set)
    for pos, kind in unit.decls.items():
        i = unit.index[pos]
        if kind in (b"P", b"V") and starts[0] <= i < last:
            locals_by_name[words[i]].add(pos)

    return first, last, locals_by_name


def recipe_rule(unit, rule, scope_info):
    require(
        isinstance(rule, dict)
        and set(rule) <= {"match", "replace", "before", "after", "count", "scope"},
        "invalid recipe rule",
    )
    actions = set(rule) & {"replace", "before", "after"}
    require(
        len(actions) == 1 and isinstance(rule.get("match"), str),
        "rule needs match and one action",
    )
    action = actions.pop()
    require(isinstance(rule[action], str), "invalid replacement")
    matches = recipe_matches(
        unit, rule["match"], rule.get("scope", "body"), rule.get("count", 1), scope_info
    )
    return [recipe_replacement(unit, lo, hi, action, rule[action]) for lo, hi in matches]


def recipe_matches(unit, match, scope, expected, scope_info):
    first, last, locals_by_name = scope_info
    words = unit.words
    pattern = match.encode().split()
    require(pattern, "invalid pattern or replacement")
    literals = []
    targets = []
    for j, token in enumerate(pattern):
        if token.startswith(b"@"):
            token = token[1:]
            possible = locals_by_name[token]
            require(len(possible) == 1, "ambiguous or missing local: " + token.decode())
            targets.append((j, possible))
        literals.append(token)
    require(scope in ("body", "file"), "unknown rule scope")
    begin, end = (first, last) if scope == "body" else (0, len(words))
    matches = []
    for i in range(begin, end - len(pattern) + 1):
        if words[i] != literals[0] or words[i : i + len(pattern)] != literals:
            continue
        if all(unit.bindings.get(unit.starts[i + j]) == t for j, t in targets):
            matches.append(unit.span(i, i + len(pattern) - 1))
    require(
        type(expected) is int and expected > 0 and len(matches) == expected,
        f"{match}: expected {expected} matches, found {len(matches)}",
    )
    return matches


def recipe_replacement(unit, lo, hi, action, text):
    if action == "before":
        hi = lo
    elif action == "after":
        lo = hi
    replacement = text.encode()
    line = unit.data.rfind(b"\n", 0, lo) + 1
    indent = unit.data[line:lo]
    if indent.strip():
        indent = b""
    if not replacement and action == "replace":
        eol = unit.data.find(b"\n", hi)
        if eol >= 0 and not unit.data[line:lo].strip() and not unit.data[hi:eol].strip():
            lo, hi = line, eol + 1
    else:
        replacement = replacement.replace(b"\n", b"\n" + indent)
    require(
        b"/*" not in unit.data[lo:hi] and b"//" not in unit.data[lo:hi],
        "rule would discard a comment",
    )
    return lo, hi, replacement


def outline_if(unit, spec, within, scope_info):
    """Keep the guard at its original site and move its compound statement."""
    import textwrap

    require(
        isinstance(spec, dict)
        and {"match", "name", "parameters"}
        <= set(spec)
        <= {"match", "name", "parameters", "return_status"},
        "outline_if requires match, name and parameters",
    )
    require(
        isinstance(spec["match"], str)
        and isinstance(spec["name"], str)
        and isinstance(spec["parameters"], list),
        "invalid outline_if",
    )
    name = identifier(spec["name"])
    pattern = spec["match"].split()
    require(
        pattern[:2] == ["if", "("] and pattern[-2:] == [")", "{"],
        "outline_if must select a braced if",
    )
    require(name not in unit.words, "helper name already in use")
    (anchor,) = recipe_matches(unit, spec["match"], "body", 1, scope_info)
    begin, end = anchor
    opening = unit.index[end - 1]
    closing = closing_brace(unit.words, opening, scope_info[1])
    block_end = unit.end(closing)
    unit.check_range(begin, block_end)
    status = spec.get("return_status", False)
    require(type(status) is bool, "return_status must be boolean")
    words = unit.words[opening + 1 : closing]
    forbidden = set(
        b"return co_return co_await co_yield goto break continue case default this __func__ __FUNCTION__ __PRETTY_FUNCTION__ __builtin_alloca alloca asm __asm__ setjmp _setjmp sigsetjmp static #".split()
    )
    if status:
        require(
            unit.language == b"c" and within.split()[0:1] == ["int"],
            "status extraction requires an int-returning C function",
        )
        require(
            len(words) >= 3
            and words[-3] == b"return"
            and words[-2].isdigit()
            and words[-1] == b";"
            and (len(words) == 3 or words[-4] in (b";", b"}")),
            "expected a final unconditional return of a status",
        )
        require(
            len(pattern) == 5 and pattern[2].startswith("@"),
            "expected a single bound guard variable",
        )
        forbidden -= {b"return", b"static"}
    require(
        not forbidden.intersection(words),
        "block contains unsupported control flow or function context",
    )
    require(
        not any(token.startswith(b"__builtin_") or b"\n" in token for token in words),
        "block contains a builtin or multiline token",
    )
    # A conditional expression owns its colon. Any other colon may be a label.
    questions = 0
    for token in words:
        if token == b"?":
            questions += 1
        elif token == b":":
            require(questions > 0, "block contains a label")
            questions -= 1
    header_start = scope_info[0] - len(within.split())
    require(
        header_start == 0 or unit.words[header_start - 1] in (b";", b"}"),
        "expected the complete function header after a top-level declaration",
    )
    require(
        unit.words[:header_start].count(b"{") == unit.words[:header_start].count(b"}")
        and b"::" not in unit.words[header_start : scope_info[0]],
        "expected a file-scope function",
    )
    # Use the actual parameter declarator from the selected function.
    captures = {}
    args = []
    declarations = []
    for param in spec["parameters"]:
        require(isinstance(param, str), "invalid helper parameter")
        word = identifier(param)
        target = scope_info[2].get(word, ())
        require(len(target) == 1 and word not in args, "ambiguous helper parameter")
        (pos,) = target
        i = unit.index[pos]
        if status:
            start = i
            while start and unit.words[start - 1] == b"*":
                start -= 1
            start -= 1
            require(
                start > 0
                and IDENT.fullmatch(unit.words[start])
                and unit.words[start - 1] in (b";", b"{", b"(", b",")
                and unit.decls[pos] == b"V"
                and unit.words[i + 1] == b"="
                and pos < begin
                and (start != i - 1 or unit.words[start] == b"int"),
                "expected a simple scalar or pointer declaration",
            )
        else:
            start = i - 2
            require(
                unit.decls[pos] == b"P"
                and header_start < i < scope_info[0]
                and i >= 3
                and unit.words[i - 3] in (b"(", b",")
                and IDENT.fullmatch(unit.words[start])
                and unit.words[i - 1] == b"*"
                and unit.words[i + 1] in (b")", b","),
                "expected a named pointer parameter of the enclosing function",
            )
        declarations.append(unit.data[unit.starts[start] : unit.end(i)])
        if status:
            # An earlier alias could change the original local during the call.
            require(
                not any(
                    pos in unit.bindings.get(at, ())
                    for at in unit.starts[i + 1 : unit.index[begin]]
                ),
                "status capture used before the selected guard",
            )
        captures[pos] = word
        args.append(word)
    for i in range(opening + 1, closing):
        bindings = unit.bindings.get(unit.starts[i], ())
        for pos in bindings:
            if pos in captures and status:
                require(
                    unit.words[i - 1] not in (b"&", b"++", b"--")
                    and (unit.words[i - 1] != b"(" or IDENT.fullmatch(unit.words[i - 2]))
                    and unit.words[i + 1] not in b"= += -= *= /= %= &= |= ^= <<= >>= ++ --".split(),
                    "expected read-only use of status capture",
                )
            elif pos in captures:
                # Only C guarantees that an ordinary argument cannot bind to a reference.
                arg = (
                    unit.language == b"c"
                    and unit.words[i + 1] in (b",", b")")
                    and (
                        unit.words[i - 1] == b","
                        or (unit.words[i - 1] == b"(" and IDENT.fullmatch(unit.words[i - 2]))
                    )
                )
                require(
                    unit.words[i + 1] == b"->" or arg,
                    "unsupported use of captured pointer",
                )
            elif unit.decls.get(pos) in (b"P", b"V"):
                require(end <= pos < block_end, "block uses another local or parameter")
    body = unit.data[end : unit.starts[closing]]
    require(
        body.startswith(b"\n") and not body.rsplit(b"\n", 1)[-1].strip(),
        "expected a multiline compound statement",
    )
    body = textwrap.dedent(body.decode("utf-8")).strip("\n")
    body = textwrap.indent(body, "    ").encode()
    helper = (
        (b"static int " if status else b"static void ")
        + name
        + b"("
        + (b", ".join(declarations) or b"void")
        + b")\n{\n"
        + body
        + b"\n}\n\n"
    )
    indent = unit.data[unit.data.rfind(b"\n", 0, begin) + 1 : begin]
    require(not indent.strip(), "expected the guard on its own line")
    guard_end = unit.end(opening - 1)
    require(not unit.data[guard_end : unit.starts[opening]].strip(), "comment before block")
    at = unit.starts[header_start]
    return [
        (at, at, helper),
        (
            guard_end,
            block_end,
            b"\n"
            + indent
            + (b"    return " if status else b"    ")
            + name
            + b"("
            + b", ".join(args)
            + b");",
        ),
    ]


def verify_outline(before, after, edits):
    """Compare bindings token for token in the relocated body and at the new call."""
    (at, _, helper), (guard_end, block_end, call) = edits
    old_open = bisect.bisect_left(before.starts, guard_end)
    old_close = bisect.bisect_left(before.starts, block_end) - 1
    first = after.index[at]
    new_open = after.words.index(b"{", first)
    offset = new_open - old_open
    require(
        before.words[old_open : old_close + 1] == after.words[new_open : old_close + offset + 1],
        "moved block changed tokens",
    )
    captures = {
        after.words[i]: after.starts[i]
        for i in range(first, new_open)
        if after.decls.get(after.starts[i]) == b"P"
    }
    call_start = guard_end + len(helper)
    call_args = {
        after.words[i]: after.bindings.get(after.starts[i], set())
        for i in range(
            bisect.bisect_left(after.starts, call_start),
            bisect.bisect_left(after.starts, call_start + len(call)),
        )
        if after.words[i] in captures
    }
    for i in range(old_open + 1, old_close):
        expected = set()
        for dest in before.bindings.get(before.starts[i], ()):
            if dest < at:
                expected.add(dest)
            elif dest < guard_end:
                word = before.words[before.index[dest]]
                require(
                    word in captures and call_args.get(word) == {dest + len(helper)},
                    "helper argument resolves to a different declaration",
                )
                expected.add(captures[word])
            elif dest < block_end:
                expected.add(after.starts[before.index[dest] + offset])
            else:
                expected.add(dest + len(helper) + len(call) - (block_end - guard_end))
        require(
            expected == after.bindings.get(after.starts[i + offset], set()),
            "binding changed in moved block",
        )


def recipe_edits(unit, path):
    """Plan every rule against the same immutable, frontend-tokenized source."""
    try:
        import tomllib
    except ImportError as exc:
        raise Refusal("recipes require Python 3.11 or later") from exc

    with path.open("rb") as stream:
        spec = tomllib.load(stream)
    require(
        "within" in spec
        and set(spec) <= {"within", "rule", "storage", "outline_if"}
        and isinstance(spec["within"], str)
        and isinstance(spec.get("rule", []), list),
        "recipe needs within and rule",
    )
    rules = spec.get("rule", [])
    if "storage" in spec:
        rules = [*rules, *storage_rules(spec["storage"])]
    scope_info = recipe_scope(unit, spec["within"])
    edits = [edit for rule in rules for edit in recipe_rule(unit, rule, scope_info)]
    if "outline_if" in spec:
        require(not edits, "outline_if is a standalone recipe operation")
        edits.extend(outline_if(unit, spec["outline_if"], spec["within"], scope_info))
    require(len({lo for lo, _, _ in edits}) == len(edits), "rules share an edit start")
    require(edits, "empty recipe")
    return edits, "outline_if" in spec


def validate(before, after, edits, *, whole_macros=False):
    """Check preserved occurrences against their declaration after each edit."""
    regions = []
    delta = 0
    for lo, hi, replacement in sorted(edits):
        start = lo + delta
        end = start + len(replacement)
        after.check_range(start, end, whole_macros=whole_macros)
        regions.append((lo, hi, start, end))
        delta = end - hi

    starts = [r[0] for r in regions]

    def forward(pos):
        index = bisect.bisect_right(starts, pos) - 1
        if index < 0:
            return pos
        _, hi, _, end = regions[index]
        return pos + end - hi if pos >= hi else None

    rewritten = collections.defaultdict(set)
    for pos, kind in after.decls.items():
        if any(lo < hi and start <= pos < end for lo, hi, start, end in regions):
            rewritten[kind, after.words[after.index[pos]]].add(pos)
    for pos, targets in before.bindings.items():
        dest = forward(pos)
        if dest is None:
            continue
        expected = set()
        for target in targets:
            mapped = forward(target)
            if mapped is None:
                require(target in before.index, "cannot locate rewritten binding")
                key = before.decls.get(target), before.words[before.index[target]]
                candidates = rewritten[key]
                require(len(candidates) == 1, "cannot track declaration through edit")
                (mapped,) = candidates
            expected.add(mapped)
        require(expected == after.bindings.get(dest), "edit changes an untouched binding")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Pass frontend flags after --. Locations and parameter indices are one-based.",
    )
    commands = parser.add_subparsers(dest="op", required=True)
    specs = {
        "refs": [],
        "rename": ["name"],
        "change-type": ["type"],
        "delete-var": [],
        "move-up": [],
        "move-down": [],
        "add-param": ["type", "name", "argument"],
        "swap-param": ["param", "other"],
        "remove-param": ["param"],
        "change-param-type": ["param", "type"],
        "extract": ["end", "name"],
    }
    recipe = commands.add_parser("recipe", help="apply a scoped token rewrite recipe")
    recipe.add_argument("file", type=Path)
    recipe.add_argument("recipe", type=Path)
    for op, params in specs.items():
        command = commands.add_parser(op)
        command.add_argument("file", type=Path)
        command.add_argument("location", metavar="LINE:BYTE_COLUMN")
        for param in params:
            command.add_argument(param, type=int if param in ("param", "other") else str)
    argv = sys.argv[1:]
    split = argv.index("--") if "--" in argv else len(argv)
    args = parser.parse_args(argv[:split])
    flags = argv[split + 1 :]
    path = args.file.resolve()
    data = path.read_bytes()
    unit = Unit(path, data, flags)
    is_recipe = args.op == "recipe"
    i = None if is_recipe else unit.position(args.location)
    if args.op == "refs":
        for k in unit.occurrences(unit.target(i)):
            pos = unit.starts[k]
            line = bisect.bisect_right(unit.lines, pos)
            print(f"{args.file}:{line}:{pos - unit.lines[line - 1] + 1}")
        return
    edits, outlined = recipe_edits(unit, args.recipe) if is_recipe else (plan(unit, args, i), False)
    for lo, hi, _ in edits:
        unit.check_range(lo, hi, whole_macros=is_recipe)
    updated = apply(data, edits)
    with tempfile.NamedTemporaryFile() as overlay:
        overlay.write(updated)
        overlay.flush()
        after = Unit(path, updated, flags, overlay.name)
    validate(unit, after, edits, whole_macros=is_recipe)
    if outlined:
        verify_outline(unit, after, edits)
    if args.op == "rename":

        def bindings(u):
            return {
                (u.index[p], frozenset(u.index.get(t, -1) for t in targets))
                for p, targets in u.bindings.items()
            }

        require(
            len(unit.words) == len(after.words)
            and bindings(unit) == bindings(after)
            and {unit.index[p]: k for p, k in unit.decls.items()}
            == {after.index[p]: k for p, k in after.decls.items()},
            "rename changes name resolution",
        )
        require(len(after.occurrences(after.target(i))) == len(edits), "incomplete rename")
    require(data == path.read_bytes(), "source changed during query")
    label = os.fsencode(os.path.relpath(args.file))
    for line in difflib.diff_bytes(
        difflib.unified_diff,
        data.splitlines(True),
        updated.splitlines(True),
        b"a/" + label,
        b"b/" + label,
    ):
        sys.stdout.buffer.write(line)
        if not line.endswith(b"\n"):
            sys.stdout.buffer.write(b"\n\\ No newline at end of file\n")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as exc:
        sys.exit(f"vref: {exc}")
