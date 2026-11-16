# The C++ parser, written against the standard

Status: plan, for review before any code (2026-11-16).  Continues
`PLAN-cxx-spec.md` after L3 (the preprocessor); its conventions hold here
(layout mirrors the standard, the standard's terms as names, comments cite
and quote, extensions in `ext/`, owners, no conditional operator).

The parser turns the tokens of phase 7 into a syntax tree of the grammar's
nonterminals.  It is syntactic analysis ([lex.phases]/1.7 "syntactically ...
analyzed") plus the one piece of semantics the grammar itself depends on:
whether a name is a *type-name*, a *template-name*, a *namespace-name* or
none of them.  Types, conversions, overload resolution, constant evaluation
and instantiation are later plans.

## Priorities

1. **The standard decides.**  A function implements the productions it
   quotes; a rule that is not grammar (a disambiguation, a "shall") is quoted
   next to the check.  n4861 is the text; a later paper is named where it is
   followed.
2. **clang is an oracle, not a specification.**  It is asked two questions:
   "is this declaration syntactically valid?" and "what does this declarator
   declare?".  Its wording, its recovery and the number of its diagnostics
   are not matched.  Where clang and the standard differ, the standard is
   followed and the case goes into `known-differences.txt` with the
   paragraph.
3. **Extensions stay apart.**  The parser of the standard's grammar accepts
   no extension.  Each extension is a row of `ext/extensions.inc`, its code
   in `ext/VENDOR/`, entered through one call at the place in the grammar
   where it applies and only when `behavior.has(extension::NAME)`.  None is
   written before a test corpus needs it (P9).
4. **Compliance before speed** (Decision 4 of PLAN-cxx-spec): one
   measurement per step (ns per token), no tuning.

## Architecture

### Files

```
src/frontend/cplusplus/
  parse/                 not a clause: what every clause's parser uses
    token_stream.cpp     phases 5-7 over the preprocessor, lookahead, marks
    syntax_tree.cpp      nodes, their owner, the dump
    parser.cpp           the parser object, expect/skip, tentative parsing
    declared_names.cpp   the name kinds the grammar needs (below)
  basic/basic.link.cpp   translation-unit
  dcl/dcl.pre.cpp, dcl.spec.cpp, dcl.type.simple.cpp, dcl.decl.cpp, ...
  expr/expr.prim.cpp, expr.post.cpp, ...
  stmt/, class/, over/, except/, temp/, module/
src/bin/cxxparse/cxxparse.cpp
src/test/cxx/parse/spec/, gram/   (the old front end's ok-*/bad-*.cpp
                        cases stay beside them: its harness reads parse/*.cpp)
```

One file per subclause that has grammar, as for `lex/` and `cpp/`; a
subclause with rules but no productions (`[dcl.ambig.res]`, `[stmt.ambig]`,
`[temp.res]`) gets a file for its disambiguation.

### Implementation language

C++20 without exceptions, RTTI or the standard library, as before, but with
more of the language where it makes the code say what the standard says
(decided 2026-11-16):

- a class per syntax-tree node category with typed accessors
  (`simple_declaration::decl_specifier_seq()`), instead of untyped child
  indexes;
- templates for the typed containers and views (`node_list<T>`,
  `owned_array<T>`), for `expect<T>`-style results and for the
  visitor over node kinds;
- RAII for everything scoped: a tentative parse (`tentative_parse` rewinds
  unless committed), a scope of declared names, a suppressed-diagnostics
  region, a nesting-depth guard.

Not used: virtual dispatch in the tree (a kind tag and a `switch`, so the
dump, the claims and later passes are exhaustive by `-Wswitch`), anything
needing the host's libstdc++.

### Tokens (`parse/token_stream`)

- `preprocessor::next()` → `token` through `convert` and
  `convert_string_literals` (phase 6), exactly what `cxxlex -dump-tokens`
  prints today.
- Unbounded lookahead over a buffer, `mark()`/`rewind()` for tentative
  parsing, and splitting `>>` (and `>>=`, `>=`) into `>` when a
  template-argument-list ends ([temp.names]/3).
- The parser-phase pragmas the preprocessor passes on are tokens here; they
  are skipped with their line until an extension handles one.

### The syntax tree (`parse/syntax_tree`)

- A node is a nonterminal of the grammar: its kind is the nonterminal's name
  with `-` as `_` (`simple_declaration`, `ptr_declarator`,
  `noptr_abstract_declarator`), its children are the nonterminals and tokens
  of the alternative that matched, in order, and it knows its source range.
- A production that only passes its operand on makes no node:
  `additive-expression: multiplicative-expression` leaves the
  multiplicative-expression in place.  So `1` is a `literal`, not seventeen
  nested nodes.
- Nodes are immutable after construction and owned by the translation
  unit's tree (one owner, no per-node frees).
- `cxxparse -dump-syntax` prints it as an indented list of nonterminals and
  token spellings: our own format, the snapshot format of the tests.
- Two scaffolding kinds, both counted and both at zero when the parser is
  complete: `unparsed` (a balanced token sequence of a part of the grammar
  that is not written yet, e.g. an initializer before P3) and `erroneous`
  (what was skipped after a syntax error).

### Names the grammar needs (`parse/declared_names`)

The grammar is not context-free: `T * x;`, `T(a);`, `a < b > c;` depend on
what `T` and `a` were declared as.  The parser keeps exactly that:

- scopes that follow the declarative regions that exist syntactically
  (namespace, block, class, function prototype, template parameter:
  [basic.scope]);
- per name its kind: type (*typedef-name*, *class-name*, *enum-name*),
  template (and which kind: class, alias, function, variable, concept),
  namespace (or alias), or other;
- lookup that is [basic.lookup.unqual] and [basic.lookup.qual] cut down to
  these kinds: enclosing scopes, using-directives, qualified names through
  namespaces and classes, base classes when they are not dependent.

No types, no overload sets, no redeclaration checking.  Inside a template a
name that depends on a template parameter is not looked up; `typename` and
`template` say what it is ([temp.res]/2, [temp.names]/4), which is the reason
those keywords exist.  This component is replaced by the real [basic.lookup]
when sema is written; until then it is small and its limits are listed in
the step that hits them.

### Ambiguities

Every disambiguation rule is tentative parsing, as the standard words them
("could possibly be a declaration"): [dcl.ambig.res]/1-3, [stmt.ambig],
[expr.unary.op]/sizeof vs type-id ([dcl.ambig.res]/2), [temp.names]/3
(`<` after a template-name), [temp.res] (`typename`), [class.mem]/complete
-class context (bodies, default arguments, noexcept and default member
initializers are kept as token ranges and parsed at the class's `}`),
[dcl.fct]/trailing return vs abstract declarator.  One file each, with the
standard's examples as the first tests.

### Diagnostics

- Our own ids (`cplusplus_parse_*` in the `vcc` namespace of the diagnostic
  table), one per kind of failure, the message naming the production and
  the subclause: `expected a declarator ([dcl.decl])`.
- [intro.compliance]/2.2 asks for "at least one diagnostic message" for an
  ill-formed program: one per erroneous declaration or statement is the
  goal, then recovery by skipping to the `;` or the matching `}` at that
  nesting depth.  clang's fix-its, typo correction and cascaded recovery
  are not copied.
- The harnesses compare the line of the first syntax error per top-level
  declaration, never the text.

## Validation

Six checks, in the order they are run for every step.  The earlier a check,
the more it speaks for the standard and the less for clang; a step moves on
to the next check only when the one before is green.

### V1. Grammar coverage (`bench/cxx-spec-refs.py`, <1 s; L2 of PLAN-cxx-spec)

- Every `[label]/N` in a comment exists in `docs/cxx-spec`; every production
  quoted before a function is, after white-space normalization, the text of
  the spec's `bnf` block.
- New here: per clause, the nonterminals of `docs/cxx-spec/grammar-index.md`
  against the ones a parser function quotes.  The output is the table
  "clause: productions implemented / in the clause / in this step's scope",
  and a step is not done while one of its productions is missing.
- Totals to reach: dcl 97, expr 59, class 25, temp 22, stmt 14, module 8,
  except 6, over 3, basic 1 (235 nonterminals).

### V2. The standard's examples (`src/test/cxx/parse/spec/LABEL.cpp`)

- `bench/cxx-spec-examples.py` extracts every code block of a subclause
  from `docs/cxx-spec` once; the files are committed and then annotated by
  hand.  The clauses have about 780 examples (dcl 179, temp 215, class 132,
  expr 83, basic 74, over 56, module 18, stmt 14, except 10).
- A line the standard marks `// error` or `// ill-formed` gets one of two
  annotations: `// syntax-error` (the parser must report an error on this
  line) or `// semantic` (ill-formed for a reason the parser does not check;
  it must accept).  Everything else must parse with no `erroneous` node.
- Where an example states which reading an ambiguity has
  ([dcl.ambig.res], [stmt.ambig], [temp.names], [temp.res]) the test has a
  `-dump-syntax` snapshot of that declaration.
- An example that needs a later step's grammar is listed in the file's
  header (`// needs: P5`) and counted, not silently skipped.

### V3. Our own cases per production (`src/test/cxx/parse/gram/LABEL.cpp`)

- One accepted input per alternative of every production in scope, and one
  rejected input per place the grammar ends an alternative (missing `;`,
  missing declarator, unbalanced bracket).
- Snapshot of `-dump-syntax` next to each file, rewritten with `--bless`
  and reviewed as a diff (as `vcc-diag.py --bless`).

### V4. clang as oracle (`bench/cxx-parse.py`, clang's tests from work/testsuites)

Per `%clang_cc1` RUN line of `clang/test/Parser/*.cpp` and `clang/test/CXX/`,
at the granularity of a namespace-scope declaration, because whole files in
one step's subset are rare (26 of 213 in Parser/, 19 of 229 in dcl+basic):

- **V4a accept/reject.**  clang's side: the declarations where clang
  reports an error of its Parse category (DiagnosticParseKinds.td, already
  in `diagnostic-messages.inc`), by line.  Ours: the declarations with an
  `erroneous` node.  Compared per declaration that has no `unparsed` node;
  the result is "agree / we reject, clang accepts / we accept, clang
  rejects / not comparable yet".  Each disagreement is fixed or goes to
  `known-differences.txt` with its reason: the standard's paragraph, or the
  extension clang accepted.
- **V4b declarator claims.**  `cxxparse -declaration-claims` writes, for
  every declarator-id it parsed, the type the declarator gives it
  ([dcl.meaning]) in an encoding built from the tree, not from the source
  text: `pointer_to<array_of<function<int(char)>, 3>>`.  The file is the
  TU followed by a prelude of those alias templates and
  `static_assert(__is_same(decltype(name), CLAIM))` lines, and clang
  compiles it.  This is the lexer's `-literal-claims` idea: clang checks
  our reading of `*`, `&`, `[]`, `()` precedence, cv-qualifiers and
  specifier combinations without anyone comparing printed ASTs.  From P3
  the same for expressions: the tree printed fully parenthesized, clang
  checks `static_assert((ORIGINAL) == (PARENTHESIZED))` for constant
  operands.
- The census (clang's AST, C++20, 2026-11-16) of namespace-scope
  declarations inside P1's subset (variables, functions without a body,
  typedefs, aliases, static_assert, using-directives, namespace aliases):
  Parser 1106 of 2787, dcl 1342 of 3509, basic 347 of 1016, drs 1999 of
  5515, expr 694 of 1828, class 800 of 3375, temp 899 of 4706.  About a
  third of 24000; the rest arrive with P4-P7.

### V5. Robustness (`bench/cxx-parse.py --mutate`, the ASan build)

- Every V2-V4 input through `build/ccs-asan/cxxparse`.
- Token mutants of the accepted cases (delete, duplicate, swap, insert a
  bracket): never a crash or a hang, always either a tree without
  `erroneous` or at least one diagnostic.  Depth is bounded by a guard that
  reports [implimits]'s nesting levels as exceeded.
- Later, with the fuzz targets of the other layers: `fuzz-cxxparse`.

### V6. Nothing below moved

`bench/cxx-lex.py` and `bench/cxx-pp.py` give the numbers they gave before
the step; `cxxparse -dump-tokens` is byte-identical to `cxxlex
-dump-tokens` (the token stream adds nothing).

### What is recorded

Per step, in this file: V1's table, V2 examples passing / annotated /
waiting, V3 cases, V4a's four numbers, V4b claims checked, `unparsed` and
`erroneous` node counts over the corpus, ns per token.

## Steps

Each step is one PR stacked on the one before.  "Opaque" means: parsed as a
balanced token sequence into an `unparsed` node until the step that owns it.

### P0. Scaffolding (no grammar)

- `token_stream`, `syntax_tree`, `parser` (expect, skip-to-recovery-point,
  `tentative_parse`), `declared_names`, `cxxparse` (`-dump-tokens`,
  `-dump-syntax`, `-fsyntax-only`, `-declaration-claims`).
- `translation-unit: declaration-seq_opt` ([basic.link]) with every
  declaration opaque up to its `;` or `}`: the whole pipeline runs on every
  test from day one.
- The harnesses: `cxx-spec-refs.py` (V1), `cxx-spec-examples.py` (V2's
  extraction), `cxx-parse.py` with the declaration splitter, the V4a
  comparison and `--mutate`.
- Done when: V6 holds; every clang test and every extracted example goes
  through without a crash (ASan); the declaration boundaries agree with
  clang's top-level declarations on the files that have no syntax error.

### P1. Declarations: specifiers, declarators, namespaces

The first grammar, about 55 nonterminals.

- [dcl.pre]: *declaration-seq*, *declaration*, *block-declaration*,
  *simple-declaration*, *empty-declaration*, *static_assert-declaration*
  (its condition opaque), *alias-declaration*, *nodeclspec-function
  -declaration*.
- [dcl.spec], [dcl.stc], [dcl.fct.spec], [dcl.typedef], [dcl.constexpr],
  [dcl.constinit], [dcl.inline]: *decl-specifier-seq* and the rule that
  ends it ([dcl.spec]/3: the longest sequence, a name is a type only if no
  *defining-type-specifier* came before).
- [dcl.type], [dcl.type.cv], [dcl.type.simple]: fundamental types and their
  combinations (Table 14), *type-name* through `declared_names`,
  *nested-name-specifier* through namespaces, `auto`; `decltype(` opaque.
- [dcl.decl], [dcl.name], [dcl.ptr], [dcl.ref], [dcl.mptr] (syntax),
  [dcl.array] (bound opaque), [dcl.fct]: *init-declarator-list*,
  declarators and abstract declarators, *parameter-declaration-clause* with
  `...`, cv- and ref-qualifiers, *trailing-return-type*,
  *noexcept-specifier* opaque; initializers opaque.
- [dcl.ambig.res]/1 and /3 (declaration against function-style cast needs
  only "could be a declaration", so no expression parser yet).
- [basic.namespace], [namespace.def] (nested and inline), [namespace.alias],
  [namespace.udir], [dcl.link].
- Opaque, with the name declared so later declarations parse: a
  *class-specifier* or *enum-specifier* body (`struct S { ... }` makes `S`
  a class-name), a function body.  A *template-declaration* is opaque as a
  whole and declares nothing: a declaration that uses its name is "not
  comparable" in V4a until P7.
- Tests: V2 for the subclauses above (about 90 examples); clang's
  `Parser/cxx-decl.cpp`, `cxx0x-decl.cpp`, `cxx-reference.cpp`,
  `cxx0x-rvalue-reference.cpp`, `cxx-variadic-func.cpp`,
  `cxx-extern-c-array.cpp`, `cxx-extra-semi.cpp`, `cxx-namespace-alias.cpp`,
  `cxx-using-directive.cpp`, `cxx1z-nested-namespace-definition.cpp`,
  `cxx2a-inline-nested-namespace-definition.cpp`, `cxx11-type-specifier.cpp`,
  `cxx-keyword-identifiers.cpp`, `CXX/dcl.dcl/basic.namespace`,
  `CXX/dcl.dcl/dcl.link`, `CXX/dcl.decl/dcl.meaning`, `CXX/dcl.decl/dcl.name`
  whole, and the P1 declarations of every other file (the census).
- Done when: V1 lists every production above; V2's examples of these
  subclauses pass; V4a has no unexplained disagreement on comparable
  declarations; V4b's claims compile.

### P2. The rest of [dcl] that needs no expressions

- [dcl.attr.grammar]: *attribute-specifier-seq* everywhere the grammar has
  it, *balanced-token-seq*, `alignas(` opaque; the standard attributes'
  argument rules ([dcl.attr.*]).
- [dcl.enum], [enum.udecl]: *enum-specifier*, *opaque-enum-declaration*,
  enumerators (values opaque), `using enum`.
- [dcl.type.elab], [namespace.udecl], [dcl.asm].
- Tests: `Parser/cxx0x-attributes.cpp`, `cxx-attributes.cpp`,
  `cxx11-base-spec-attributes.cpp` (later part), `cxx1z-attributes.cpp`,
  `cxx98-enum.cpp`, `cxx20-using-enum.cpp`, `cxx-using-declaration.cpp`,
  `cxx1z-using-declaration.cpp`, `CXX/dcl.dcl/dcl.attr` (17),
  `CXX/dcl.dcl/dcl.enum`.

### P3. Expressions and initializers

- [expr.prim] without lambdas and requires-expressions, [expr.post],
  [expr.unary] (with `sizeof`, `alignof`, `noexcept`, `new`, `delete`),
  [expr.cast], [expr.mptr.oper] to [expr.comma], [expr.const]'s
  *constant-expression* (the grammar; evaluation is sema).
- [dcl.init]: *initializer*, *brace-or-equal-initializer*,
  *braced-init-list*, designated initializers.  [dcl.type.decltype],
  [dcl.align].
- [dcl.ambig.res]/2 (type-id against expression), [expr.unary.op] and the
  cast-expression ambiguity, `>` inside a template-argument (deferred to P7).
- Every `unparsed` node of P1-P2 that held an expression disappears; V4b
  gains the parenthesized-expression claims.
- Tests: `CXX/expr` (101 files), `Parser/cxx-casting.cpp`, `cxx-typeid.cpp`,
  `cxx2a-spaceship.cpp`, `cxx2a-designated-init.cpp`,
  `cxx11-brace-initializers.cpp`, `cxx-ambig-paren-expr.cpp`,
  `cxx-ambig-decl-expr.cpp`, `cxx2b-subscript.cpp`.

### P4. Statements and function definitions

- [stmt] whole (14 nonterminals), [stmt.ambig], *condition* and
  *init-statement*, range-based for; [dcl.fct.def.general],
  [dcl.fct.def.default], [dcl.fct.def.delete]; block scope in
  `declared_names`.
- Tests: `CXX/stmt.stmt`, `Parser/cxx-stmt.cpp`, `cxx-condition.cpp`,
  `cxx0x-condition.cpp`, `cxx0x-for-range.cpp`, `cxx1z-init-statement.cpp`,
  `cxx2a-init-statement.cpp`, `cxx2b-label.cpp`,
  `cxx11-stmt-attributes.cpp`, `cxx-default-delete.cpp`.

### P5. Classes

- [class] (25 nonterminals): *class-specifier*, *member-specification*,
  member declarators, bit-fields, [class.derived] base clauses,
  [class.access], [class.base.init] *ctor-initializer*, [class.conv.fct],
  [over.oper] and [over.literal] ids, [dcl.friend], [dcl.struct.bind].
- Complete-class contexts ([class.mem]/6) parsed at the closing brace;
  class scope and non-dependent bases in `declared_names`; the
  constructor-or-declaration decision ([class.ctor], [dcl.spec]/3).
- Tests: `CXX/class*`, `CXX/special` (134 files), `Parser/cxx-class.cpp`,
  `cxx-member-initializers.cpp`, `cxx0x-member-initializers.cpp`,
  `cxx0x-override-control-keywords.cpp`, `cxx-friend.cpp`,
  `cxx-default-args.cpp`, `cxx1z-decomposition.cpp`,
  `cxx2a-bitfield-init.cpp`.

### P6. Lambdas, exceptions, coroutines

- [expr.prim.lambda], [except] (6 nonterminals: try-block,
  function-try-block, handlers, throw), [expr.await], [expr.yield],
  `co_return`.
- Tests: `Parser/cxx0x-lambda-expressions.cpp`, `cxx2b-lambdas.cpp`,
  `cxx1z-constexpr-lambdas.cpp`, `cxx-throw.cpp`, `cxx20-coroutines.cpp`,
  `CXX/expr/expr.prim/expr.prim.lambda`, `CXX/except`.

### P7. Templates

- [temp] (22 nonterminals): *template-declaration*, parameters,
  *template-id* and the `<` rule ([temp.names]/3), `typename` and
  `template` disambiguators ([temp.res]), pack expansions and
  fold-expressions, explicit instantiation and specialization, deduction
  guides, [temp.concept], *requires-clause*, *requires-expression*,
  constrained parameters and abbreviated templates.
- `declared_names`: template parameter scope, template kinds, "member of an
  unknown specialization" as the answer for dependent qualified names.
- The last opaque kind goes away; `unparsed` is zero on the whole corpus.
- Tests: `CXX/temp` (210 files), the 25 `Parser/cxx*template*`,
  `*concept*`, `*requires*`, `*fold*` files, `CXX/drs` (83 files) whole.

### P8. Modules

- [module] (8 nonterminals), [cpp.module]'s tokens from the preprocessor.
- Tests: `CXX/module`, `Parser/cxx-modules-*.cppm`.

### P9. Real code and the extensions it needs

- Gate: the translation units the old front end is measured on parse with
  no `erroneous` node: libc++'s and libstdc++'s headers one by one, the
  nlohmann/json test TUs, the Skia TUs.
- Each construct that stops one is an extension with a row, a file and a
  test: expected first are `__attribute__` (`ext/gnu/attribute.cpp`),
  `__extension__`, `__typeof__`, asm labels, `__restrict`, statement
  expressions, `__builtin_*` type traits spelled as keywords
  (`ext/clang/`), `__declspec` (`ext/microsoft/`).  With an extension off,
  its syntax is the syntax error the standard says it is, and a test says
  so.
- clang's tests that were in `known-differences.txt` only for an extension
  come back as each lands.

After P9 the tree is complete and the semantic clauses start in the
standard's order ([basic.lookup] in full, [conv], [expr]'s types,
[expr.const] evaluation, [over], [temp] instantiation): a separate plan.

## Decisions to confirm

1. **Tree shape**: nodes are the grammar's nonterminals, pass-through
   productions make no node (proposed above), rather than a clang-like AST
   designed for sema.  Sema later builds its own entities from this tree.
2. **Step order**: declarations before expressions (PLAN-cxx-spec said
   basic, expr, stmt, dcl), because every test is a sequence of
   declarations and P1 needs no expression parser.
3. **`declared_names` inside the parser** as a deliberately small stand-in
   for [basic.lookup], replaced when sema arrives.
4. **Opaque regions** as scaffolding, measured to zero, instead of waiting
   for a complete grammar before the first clang test runs.
5. **Diagnostics**: our own ids and texts naming the production; clang's
   Parse diagnostics only as the oracle's accept/reject signal.
6. **No extension before P9**, including `__attribute__`: clang tests that
   use one are "known difference: extension" until then.
7. **A separate tool `cxxparse`** rather than more modes in `cxxlex`.
