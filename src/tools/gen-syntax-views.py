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
kinds), so that a position is known by the kind of what is there, and
identifier() and string_literal() where an alternative has at most one
of that token (an identifier also through a nonterminal that can be one,
type-name, unqualified-id); the others are hand-written in syntax.h
(SyntaxExtra<T>), as are other questions about tokens.

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
    # what passes on its one part without the optional ones
    'attribute': 'AttributeWithArguments',
    'attribute_specifier_seq': 'AttributeSpecifiers',
    'attribute_list': 'Attributes',
    'declarator_id': 'PackDeclaratorId',
    'gnu_attribute_list': 'GnuAttributes',
    'parameter_declaration_clause': 'ParameterClause',
    'base_specifier': 'AnnotatedBaseSpecifier',
    'base_specifier_list': 'BaseSpecifiers',
    'class_head_name': 'QualifiedClassHeadName',
    'conversion_declarator': 'PointerConversionDeclarator',
    'conversion_type_id': 'ConversionTypeWithDeclarator',
    'gnu_initializer_list': 'GnuInitializerClauses',
    'handler_seq': 'Handlers',
    'init_declarator': 'DeclaratorWithInitializer',
    'initializer_list': 'InitializerClauses',
    'mem_initializer_list': 'MemInitializers',
    'member_declarator': 'MemberDeclaratorWithParts',
    'noptr_declarator': 'SuffixedDeclarator',
    'template_argument_list': 'TemplateArguments',
    'using_declarator_list': 'UsingDeclarators',
}

# nonterminals the parser makes a node of even where the grammar lets one
# part pass on (an alternative of one nonterminal and optional parts);
# -fcxx-dump=syntax-conformance shows a position that holds the part
# itself, which is a nonterminal missing here
ALWAYS_NODE = {
    'decl-specifier-seq', 'defining-type-id', 'function-body',
    'defining-type-specifier-seq', 'enum-head', 'exception-declaration',
    'member-specification', 'new-type-id', 'noptr-abstract-declarator',
    'parameter-declaration', 'translation-unit', 'type-id',
    'type-parameter', 'type-specifier-seq',
}

# alternatives the pass-through rule does not see: a node of its own kind
# stands for them (__extension__ X, [ext:gnu_extension_keyword])
EXTRA_MEMBERS = {
    'declaration': ['gnu_extension_declaration'],
    'block-declaration': ['gnu_extension_declaration'],
    'member-declaration': ['gnu_extension_declaration'],
    'unary-expression': ['gnu_extension_expression'],
}


# children the parser puts where the grammar has none of their kind, each
# a choice of the parser: allowed by the conformance check, no accessor
KNOWN_CHILDREN = {
    # a member template's or member specialization's declaration is a
    # member-declaration ([class.mem]) rather than a declaration
    'template-declaration': ['member-declaration'],
    'explicit-specialization': ['member-declaration'],
    'explicit-instantiation': ['member-declaration'],
    # GCC's attributes after a using-declarator ([ext:gnu_attributes])
    'using-declaration': ['gnu-attribute-specifier'],
    'using-declarator-list': ['gnu-attribute-specifier'],
    # [ext:gnu_declarator]: a declarator with GCC's attributes inside
    'simple-declaration': ['gnu-declarator', 'requires-clause'],
    'init-declarator': ['gnu-declarator'],
    # a trailing requires-clause ([dcl.decl]/4) of the one declarator, which
    # passes through the init-declarator and member-declarator
    'member-declaration': ['requires-clause'],
    'member-declarator-list': ['requires-clause'],
    # the enum-head-name of an opaque-enum-declaration's elaborated form
    'elaborated-enum-specifier': ['enum-head-name'],
    # a constrained placeholder's type-constraint parsed as one
    'placeholder-type-specifier': ['placeholder-type-specifier'],
    'unqualified-id': ['placeholder-type-specifier'],
    'mem-initializer': ['placeholder-type-specifier'],
    'base-clause': ['placeholder-type-specifier'],
    # a member-specification's explicit instantiation (an error kept in
    # the tree) and __extension__ before a member-declaration
    'member-specification': ['explicit-instantiation', 'gnu-extension-declaration'],
}

# list nonterminals whose node nests (expression , assignment-expression:
# the comma's left operand is an expression node): no range
NOT_LISTS = {'expression'}


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

    def single(self, alternative, nt=None, strict=False):
        """the nonterminal an alternative of nt passes on, or None: one
        nonterminal and what is optional (without the optional parts the
        parser makes no node, unless nt is in ALWAYS_NODE), or, when all of
        it is optional, its one nonterminal; strict: one nonterminal alone"""
        t = alternative.split()
        if strict or nt in ALWAYS_NODE:
            if len(t) == 1 and t[0] in self.g:
                return t[0]
            return None
        required = [x for x in t if not x.endswith('_opt')]
        if not required:
            required = [base(x) for x in t if base(x) in self.g]
        if len(required) == 1 and required[0] in self.g:
            return required[0]
        return None

    def symbols(self, alternatives, stack=()):
        """the nonterminals of the alternatives that make nodes, a
        nonterminal that makes none ([temp.names]' template-argument)
        replaced by its own symbols, as the tree has them"""
        out = []
        for a in alternatives:
            row = []
            for sym in a.split():
                b = base(sym)
                if b not in self.g:
                    continue
                if b in self.kind_of or b in stack or self.member_kinds(b) and not self.transparent(b):
                    row.append(b)
                    continue
                for inner in self.symbols(self.g[b], stack + (b,)):
                    row.extend(inner)
            out.append(row)
        return out

    def transparent(self, nt):
        """a nonterminal that makes no node and passes none on
        (clang-type-trait-argument-list): its parts stand in the parent"""
        return nt not in self.kind_of and not self.member_kinds(nt)

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
            s = self.single(a, nt)
            if s:
                out |= self.member_kinds(s, stack + (nt,))
        if not stack:
            self.memo[nt] = out
        return out

    def derives_identifier(self, nt, stack=()):
        """whether a position of nt can be a bare identifier token
        (type-name, unqualified-id): the parser makes no node of it then"""
        if nt == 'identifier':
            return True
        if nt in stack or nt not in self.g:
            return False
        for a in self.g[nt]:
            t = [x for x in a.split() if not x.endswith('_opt')]
            if t == ['identifier']:
                return True
            s = self.single(a, nt)
            if s and self.derives_identifier(s, stack + (nt,)):
                return True
        return False

    def tokens(self, alternative):
        """the token positions of an alternative that get an accessor:
        identifier (also through a nonterminal that can be one) and
        string-literal"""
        out = []
        for sym in alternative.split():
            b = base(sym)
            if b == 'string-literal':
                out.append('string_literal')
            elif b == 'identifier' or (b in self.g and self.derives_identifier(b)):
                out.append('identifier')
        return out

    def own_alternatives(self, nt):
        return sorted(a for a in self.g.get(nt, ()) if not self.single(a, nt, strict=True))

    def list_element(self, nt):
        """the element of a list nonterminal (X-seq: X | X-seq X, X-list:
        X | X-list , X), or None"""
        alternatives = self.g.get(nt, set())
        singles = [self.single(a, nt) for a in alternatives if self.single(a, nt)]
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
    missing = []
    for k, nt in G.kinds:
        kinds = G.member_kinds(nt) if nt in G.g else {k}
        if len(kinds) > 1:
            if k not in OWN:
                missing.append(k)
            kind_type[k] = OWN.get(k, k)
        else:
            kind_type[k] = camel(k)
    if missing:
        sys.exit('gen-syntax-views.py: sums with a node of their own, name it in OWN: %s' % ' '.join(missing))
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
        if e and e in G.g and G.member_kinds(e) and nt in G.kind_of and nt not in NOT_LISTS:
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
    tokens = {}  # type -> [identifier, string_literal]
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
        for row in G.symbols(alternatives):
            seen = set()
            for b in row:
                if not G.member_kinds(b):
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
        # token accessors: a token kind at most once in each alternative
        counts = {}
        for a in alternatives:
            row = G.tokens(a)
            for tok in set(row):
                counts[tok] = max(counts.get(tok, 0), row.count(tok))
        tokens[t] = [tok for tok, c in sorted(counts.items())
                     if c == 1 and tok not in extra.get(t, ())
                     and tok not in {n for n, _ in out}]

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
        for tok in tokens.get(t, []):
            w('    SyntaxElement %s() const;' % tok)
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
        for tok in tokens.get(t, []):
            w('')
            w('inline SyntaxElement %s::%s() const' % (t, tok))
            w('{')
            w('    return token_of_kind(token_kind::%s);' % {'identifier': 'identifier', 'string_literal': 'literal'}[tok])
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
        within = sorted((x for x in sums if k in sums[x] and x != kind_type[k]),
                        key=lambda x: (len(sums[x]), x))
        w('            return syntax_call<%s>(f, %s(s.context(), s.index()));' %
          (', '.join(within), kind_type[k]))
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
    w('')
    # the conformance check's tables (dump/syntax.cpp)
    order = [k for k, _ in G.kinds]
    words = (len(order) + 63) // 64

    def bits(ks):
        v = [0] * words
        for k in ks:
            i = order.index(k)
            v[i // 64] |= 1 << (i % 64)
        return '{%s}' % ', '.join('0x%xu' % x for x in v)

    w('#ifdef SYNTAX_VIEWS_CONFORMANCE')
    w('/* per node kind, the kinds its productions allow as a child node')
    w('   ((unparsed) and (erroneous) anywhere) */')
    w('static const uint64_t syntax_allowed_children[][%d] = {' % words)
    for k, nt in G.kinds:
        allowed = {'unparsed', 'erroneous'}
        if nt in G.g:
            alternatives = G.g[nt]
            if len(G.member_kinds(nt)) > 1:
                alternatives = G.own_alternatives(nt)
            for row in G.symbols(alternatives):
                for b in row:
                    allowed |= G.member_kinds(b)
            for b in KNOWN_CHILDREN.get(nt, ()):
                allowed |= G.member_kinds(b)
        else:
            allowed = set(order)
        w('    %s, /* %s */' % (bits(allowed), k))
    w('};')
    w('/* the list kinds: a child of the same kind is a list not flattened */')
    list_kinds = sorted(G.kind_of[nt] for nt, t in nt_type.items()
                        if t in lists and nt in G.kind_of)
    w('static const uint64_t syntax_list_kinds[%d] = %s;' % (words, bits(list_kinds)))
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
