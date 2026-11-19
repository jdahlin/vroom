#!/usr/bin/env python3
"""Generate the C++ front end's typed views of the syntax tree.

Writes src/include/frontend/cplusplus/syntax-views.inc, which
src/include/frontend/cplusplus/syntax.h includes, from:

  src/frontend/cplusplus/parse/syntax-kinds.inc   the node kinds, by the
                                                  nonterminal each is
  docs/cxx-spec/**.md                             the standard's bnf blocks
  src/frontend/cplusplus/ext/grammar.inc          the extensions' productions

Each node kind gets a view type, CamelCase.  A nonterminal whose positions
can hold nodes of several kinds (an alternative that is a single
nonterminal makes no node of its own: [dcl.pre]'s declaration is a
function-definition, a simple-declaration, ...) gets a sum type of that
name, with accepts() true for each kind and visit() calling a visitor with
the kind's own view; such a nonterminal that also has a node of its own
(additive-expression's a + b) names that node's view in OWN below.  A list
nonterminal (X-seq: X | X-seq X, X-list: X | X-list , X) gets a range of
its elements whether it is a node or the one element.  A node's child
accessors come from its productions: one for each nonterminal whose kinds
no other nonterminal of the production shares (the first child of those
kinds), so that a position is known by the kind of what is there; the
others are hand-written in syntax.h (SyntaxExtra<T>), as are questions
about tokens.

Usage: src/tools/gen-syntax-views.py [--check]
"""
import importlib.util
import os
import re
import sys

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(R, 'src', 'include', 'frontend', 'cplusplus', 'syntax-views.inc')
KINDS = os.path.join(R, 'src', 'frontend', 'cplusplus', 'parse', 'syntax-kinds.inc')
SYNTAX_H = os.path.join(R, 'src', 'include', 'frontend', 'cplusplus', 'syntax.h')

# the view of a sum's own node: the alternative of the nonterminal that is
# not a single nonterminal
OWN = {
    'abstract_declarator': 'TrailingAbstractDeclarator',
    'additive_expression': 'AdditiveOperation',
    'and_expression': 'AndOperation',
    'assignment_expression': 'Assignment',
    'attribute_specifier': 'StandardAttributeSpecifier',
    'balanced_token_seq': 'BalancedTokens',
    'brace_or_equal_initializer': 'EqualInitializer',
    'capture_list': 'Captures',
    'cast_expression': 'CastNotation',
    'class_or_decltype': 'QualifiedClassName',
    'compare_expression': 'ThreeWayComparison',
    'condition': 'ConditionDeclaration',
    'conditional_expression': 'ConditionalOperation',
    'constraint_logical_and_expression': 'ConstraintConjunction',
    'constraint_logical_or_expression': 'ConstraintDisjunction',
    'declaration_seq': 'Declarations',
    'declarator': 'TrailingReturnDeclarator',
    'designated_initializer_list': 'DesignatedInitializers',
    'elaborated_type_specifier': 'ElaboratedClassSpecifier',
    'enumerator_definition': 'EnumeratorWithValue',
    'enumerator_list': 'Enumerators',
    'equality_expression': 'EqualityOperation',
    'exclusive_or_expression': 'ExclusiveOrOperation',
    'expression': 'CommaExpression',
    'function_body': 'OrdinaryFunctionBody',
    'gnu_designation': 'GnuOldDesignation',
    'inclusive_or_expression': 'InclusiveOrOperation',
    'init_declarator_list': 'InitDeclarators',
    'initializer': 'ParenthesizedInitializer',
    'jump_statement': 'Jump',
    'lambda_capture': 'DefaultAndCaptures',
    'logical_and_expression': 'LogicalAndOperation',
    'logical_or_expression': 'LogicalOrOperation',
    'member_declaration': 'MemberSimpleDeclaration',
    'member_declarator_list': 'MemberDeclarators',
    'multiplicative_expression': 'MultiplicativeOperation',
    'new_declarator': 'PointerNewDeclarator',
    'new_initializer': 'ParenthesizedNewInitializer',
    'parameter_declaration_list': 'ParameterDeclarations',
    'pm_expression': 'PointerToMemberOperation',
    'postfix_expression': 'PostfixOperation',
    'primary_expression': 'ParenthesizedOrThis',
    'ptr_abstract_declarator': 'PointerAbstractDeclarator',
    'ptr_declarator': 'PointerDeclarator',
    'relational_expression': 'RelationalOperation',
    'requirement_seq': 'Requirements',
    'shift_expression': 'ShiftOperation',
    'simple_type_specifier': 'SimpleTypeName',
    'statement': 'AttributedStatement',
    'statement_seq': 'Statements',
    'template_id': 'OperatorTemplateId',
    'template_parameter_list': 'TemplateParameters',
    'unary_expression': 'UnaryOperation',
    'unqualified_id': 'DestructorTypeId',
}

# alternatives the pass-through rule does not see: a node of its own kind
# stands for them (__extension__ X, [ext:gnu_extension_keyword])
EXTRA_MEMBERS = {
    'declaration': ['gnu_extension_declaration'],
    'block-declaration': ['gnu_extension_declaration'],
    'unary-expression': ['gnu_extension_expression'],
}


def load_refs():
    spec = importlib.util.spec_from_file_location(
        'cxx_spec_refs', os.path.join(R, 'bench', 'cxx-spec-refs.py'))
    module = importlib.util.module_from_spec(spec)
    argv = sys.argv
    sys.argv = [argv[0]]
    try:
        spec.loader.exec_module(module)
    finally:
        sys.argv = argv
    return module


def camel(snake):
    return ''.join(w[:1].upper() + w[1:] for w in snake.split('_'))


def snake(nonterminal):
    return re.sub(r'[^a-z0-9]+', '_', nonterminal.lower()).strip('_')


def base(symbol):
    return symbol[:-4] if symbol.endswith('_opt') else symbol


def plural(name):
    if name.endswith('y') and not name.endswith(('ay', 'ey', 'oy')):
        return name[:-1] + 'ies'
    if name.endswith(('s', 'x')):
        return name + 'es'
    return name + 's'


class Grammar:
    def __init__(self):
        refs = load_refs()
        self.g = {k: set(v[1]) for k, v in refs.spec_grammar().items() if not v[2]}
        for _, _, nonterminal, alternative in refs.extension_grammar():
            if alternative:
                self.g.setdefault(nonterminal, set()).add(refs.normalize(alternative))
        text = open(KINDS, encoding='utf-8').read()
        self.kinds = re.findall(r'^SYNTAX_NODE\((\w+), "([^"]+)"', text, re.M)
        self.nonterminal_of = dict(self.kinds)
        self.kind_of = {nt: k for k, nt in self.kinds}
        self.memo = {}

    def single(self, alternative):
        """the nonterminal an alternative passes on, or None"""
        t = alternative.split()
        if len(t) == 1 and t[0] in self.g:
            return t[0]
        return None

    def member_kinds(self, nt, stack=()):
        """the node kinds a position of nonterminal nt can hold"""
        if nt in self.memo:
            return self.memo[nt]
        out = set()
        if nt in stack:
            return out
        if nt in self.kind_of:
            out.add(self.kind_of[nt])
        for k in EXTRA_MEMBERS.get(nt, ()):
            out.add(k)
        for a in self.g.get(nt, ()):
            s = self.single(a)
            if s:
                out |= self.member_kinds(s, stack + (nt,))
        if not stack:
            self.memo[nt] = out
        return out

    def own_alternatives(self, nt):
        return sorted(a for a in self.g.get(nt, ()) if not self.single(a))

    def list_element(self, nt):
        """the element of a list nonterminal (X-seq: X | X-seq X, X-list:
        X | X-list , X), or None"""
        alternatives = self.g.get(nt, set())
        singles = [self.single(a) for a in alternatives if self.single(a)]
        if len(singles) != 1:
            return None
        e = singles[0]
        rest = alternatives - {e}
        if rest and rest <= {'%s %s' % (nt, e), '%s , %s' % (nt, e)}:
            return e
        return None


def extras():
    """type -> names declared in syntax.h's SyntaxExtra<type>"""
    text = open(SYNTAX_H, encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'template <>\s*struct SyntaxExtra<(\w+)>\s*:\s*SyntaxRef\s*\{(.*?)\n\};', text, re.S):
        out[m.group(1)] = set(re.findall(r'(\w+)\([^()]*\) const', m.group(2)))
    return out


def generate():
    G = Grammar()
    extra = extras()
    # the type of each node kind, and of each nonterminal with kinds
    kind_type = {}
    sums = {}  # type -> sorted kinds
    nt_type = {}
    lists = {}  # type -> element nonterminal
    for k, nt in G.kinds:
        kinds = G.member_kinds(nt) if nt in G.g else {k}
        if len(kinds) > 1:
            if k not in OWN:
                sys.exit('gen-syntax-views.py: %s is a sum and its own node: name it in OWN' % k)
            kind_type[k] = OWN[k]
        else:
            kind_type[k] = camel(k)
    for k in OWN:
        if k not in G.nonterminal_of or len(G.member_kinds(G.nonterminal_of[k])) < 2:
            sys.exit('gen-syntax-views.py: OWN names %s, which is no sum' % k)
    for nt in sorted(G.g):
        kinds = G.member_kinds(nt)
        if not kinds:
            continue
        if len(kinds) == 1:
            nt_type[nt] = kind_type[next(iter(kinds))]
            continue
        t = camel(snake(nt))
        nt_type[nt] = t
        sums[t] = sorted(kinds)
        e = G.list_element(nt)
        if e and e in G.g and G.member_kinds(e):
            lists[t] = e
    for k, nt in G.kinds:
        nt_type.setdefault(nt, kind_type[k])
    types = sorted(set(kind_type.values()) | set(sums))
    clash = set(kind_type.values()) & set(sums)
    if clash:
        sys.exit('gen-syntax-views.py: a node view and a sum of one name: %s' % sorted(clash))

    def kinds_of_type(t):
        if t in sums:
            return set(sums[t])
        return {k for k, v in kind_type.items() if v == t}

    # child accessors of each node kind
    accessors = {}  # type -> [(name, type)]
    for k, nt in G.kinds:
        t = kind_type[k]
        if nt not in G.g:
            accessors[t] = []
            continue
        alternatives = G.own_alternatives(nt) if t != nt_type.get(nt) or len(G.member_kinds(nt)) > 1 else sorted(G.g[nt])
        if len(G.member_kinds(nt)) == 1:
            alternatives = sorted(G.g[nt])
        symbols = {}
        repeated = set()
        for a in alternatives:
            seen = set()
            for s in a.split():
                b = base(s)
                if b not in G.g or not G.member_kinds(b):
                    continue
                if b in seen:
                    repeated.add(b)
                seen.add(b)
                symbols[b] = G.member_kinds(b)
        out = []
        for b, ks in sorted(symbols.items()):
            if b in repeated:
                continue
            if any(o != b and ks & oks for o, oks in symbols.items()):
                continue
            name = snake(b)
            if name in extra.get(t, ()):
                continue
            out.append((name, nt_type[b]))
        accessors[t] = out

    lines = []
    w = lines.append
    w('/* generated by src/tools/gen-syntax-views.py from parse/syntax-kinds.inc,')
    w('   the standard\'s grammar (docs/cxx-spec) and ext/grammar.inc: do not edit */')
    w('')
    w('#ifdef SYNTAX_VIEWS_DECLARE')
    for t in types:
        w('struct %s;' % t)
    w('/* a member of a sum, or a sum within it, converts to the sum */')
    for t in sorted(sums):
        members = sorted({kind_type[k] for k in sums[t]} - {t})
        subsums = sorted(x for x in sums if x != t and set(sums[x]) < set(sums[t]))
        for m in members + subsums:
            w('template <> inline constexpr bool syntax_converts<%s, %s> = true;' % (m, t))
    w('#endif')
    w('')
    w('#ifdef SYNTAX_VIEWS_ACCESSORS')
    w('/* SyntaxRef::name(): the node as that view, {} if it is not one */')
    seen = set()
    for nt, t in sorted(nt_type.items(), key=lambda x: snake(x[0])):
        name = snake(nt)
        if name in seen:
            continue
        seen.add(name)
        w('%s %s() const;' % (t, name))
    for k, t in sorted(kind_type.items()):
        name = re.sub(r'(?<!^)(?=[A-Z])', '_', t).lower()
        if name in seen or t in sums:
            continue
        seen.add(name)
        w('%s %s() const;' % (t, name))
    w('#endif')
    w('')
    w('#ifdef SYNTAX_VIEWS_DEFINE')
    for t in types:
        nt = None
        sum_ = t in sums
        w('')
        if sum_:
            nonterminals = sorted(n for n, v in nt_type.items() if v == t)
            w('/* %s: one of %s */' % (', '.join(nonterminals), ', '.join(sums[t])))
        else:
            ks = sorted(kinds_of_type(t))
            w('/* the %s node */' % ', '.join(G.nonterminal_of[k] for k in ks))
        w('struct %s : SyntaxExtra<%s> {' % (t, t))
        w('    using SyntaxExtra<%s>::SyntaxExtra;' % t)
        if sum_:
            w('    %s() = default;' % t)
            w('    template <class V>')
            w('        requires syntax_converts<V, %s>' % t)
            w('    %s(V v) : SyntaxExtra<%s>(v.context(), v.index())' % (t, t))
            w('    {')
            w('    }')
        w('    static bool accepts(syntax_kind k);')
        if sum_:
            w('    template <class F> decltype(auto) visit(F &&f) const;')
            w('    template <class T, class... M>')
            w('    decltype(auto) visit(T *self, M... members) const')
            w('    {')
            w('        return visit(Overloaded{bind_member(self, members)...});')
            w('    }')
        if t in lists:
            e = lists[t]
            w('    SyntaxRange<%s> %s() const;' % (nt_type[e], plural(snake(e))))
        for name, rt in accessors.get(t, []):
            w('    %s %s() const;' % (rt, name))
        w('};')
    w('')
    # definitions
    for t in types:
        w('')
        w('inline bool %s::accepts(syntax_kind k)' % t)
        w('{')
        ks = sorted(kinds_of_type(t))
        if len(ks) == 1:
            w('    return k == syntax_kind::%s;' % ks[0])
        else:
            words = [0] * ((len(G.kinds) + 63) // 64)
            order = [k for k, _ in G.kinds]
            for k in ks:
                i = order.index(k)
                words[i // 64] |= 1 << (i % 64)
            w('    static const uint64_t kinds[] = {%s};' % ', '.join('0x%xu' % x for x in words))
            w('    return syntax_kind_in(k, kinds);')
        w('}')
        if t in sums:
            w('')
            w('template <class F> decltype(auto) %s::visit(F &&f) const' % t)
            w('{')
            w('    return syntax_visit(*this, f);')
            w('}')
        if t in lists:
            e = lists[t]
            own = [k for k in sums[t] if kind_type[k] != nt_type[e] and k not in sums.get(nt_type[e], [])]
            w('')
            w('inline SyntaxRange<%s> %s::%s() const' % (nt_type[e], t, plural(snake(e))))
            w('{')
            w('    if (!*this)')
            w('        return {};')
            cond = ' || '.join('is(syntax_kind::%s)' % k for k in own)
            w('    if (%s)' % cond)
            w('        return SyntaxRange<%s>(context(), children(), size());' % nt_type[e])
            w('    return SyntaxRange<%s>(context(), child());' % nt_type[e])
            w('}')
        for name, rt in accessors.get(t, []):
            w('')
            w('inline %s %s::%s() const' % (rt, t, name))
            w('{')
            w('    return first<%s>();' % rt)
            w('}')
    w('')
    w("/* the view of s's kind if that kind is a member of S, else s itself */")
    w('template <class S, class F> decltype(auto) syntax_visit(const S &s, F &f)')
    w('{')
    w('    if (!s)')
    w('        return f(s);')
    w('    switch (s.kind()) {')
    for k, _ in G.kinds:
        w('    case syntax_kind::%s:' % k)
        w('        if constexpr (syntax_converts<%s, S>)' % kind_type[k])
        w('            return f(%s(s.context(), s.index()));' % kind_type[k])
        w('        break;')
    w('    default:')
    w('        break;')
    w('    }')
    w('    return f(s);')
    w('}')
    w('')
    seen = set()
    for nt, t in sorted(nt_type.items(), key=lambda x: snake(x[0])):
        name = snake(nt)
        if name in seen:
            continue
        seen.add(name)
        w('')
        w('inline %s SyntaxRef::%s() const' % (t, name))
        w('{')
        w('    return as<%s>();' % t)
        w('}')
    for k, t in sorted(kind_type.items()):
        name = re.sub(r'(?<!^)(?=[A-Z])', '_', t).lower()
        if name in seen or t in sums:
            continue
        seen.add(name)
        w('')
        w('inline %s SyntaxRef::%s() const' % (t, name))
        w('{')
        w('    return as<%s>();' % t)
        w('}')
    w('#endif')
    return '\n'.join(lines) + '\n'


def main():
    text = generate()
    if '--check' in sys.argv[1:]:
        old = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if old != text:
            sys.exit('%s is out of date: run src/tools/gen-syntax-views.py' % os.path.relpath(OUT, R))
        return
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write(text)


if __name__ == '__main__':
    main()
