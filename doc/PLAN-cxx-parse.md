# The C++ parser, written against the standard

Status: plan, for review before any code (2026-11-16).  Continues
`PLAN-cxx-spec.md` after L3 (the preprocessor); its conventions hold here
(layout mirrors the standard, the standard's terms as names, comments cite
and quote, extensions in `ext/`, owners, no conditional operator).

Since 2026-11-17 the tools this plan names (`cxxlex`, `cxxparse`,
`cxxsema`) are gone: their outputs are the driver's, `vpp -x c++
-fsyntax-only -fcxx-dump=WHAT FILE` (`src/frontend/cplusplus/dump/dump.h`;
`-dump-tokens` is `tokens`, `-dump-raw-tokens` `raw-tokens`, `-E -dM`
`definitions`, `-dump-declarations` `declarations`, cxxparse's
`-declaration-claims` `declarator-claims`, `-dump-analysis` `analysis`,
`-selftest` `selftest`; `-fcxx-dump=help` lists them).  The text below keeps
the old names.

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
   written before a test corpus needs it (P9).  The syntax an extension adds
   is explicit, as the standard's is: productions in `ext/grammar.inc`
   (an alternative added to a standard nonterminal, or a nonterminal of the
   extension's own named with its vendor's prefix, `gnu-attribute-list`),
   which the code that parses it cites as `[ext:NAME]` and quotes;
   `cxx-spec-refs.py` checks the quotes against the rows and reports which
   rows are quoted.
4. **Designed for speed, tuned later.**  The data layout and interfaces
   are chosen now so that they can be micro-optimized and cached without a
   rewrite (below), but no tuning is done until the whole pipeline runs and
   profiles show where the time goes (Decision 4 of PLAN-cxx-spec).  One
   measurement per step (ns per token) so regressions are visible.

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

### Built to be optimized and cached later

Nothing here is tuned in P0-P9; these are constraints on the design so that
tuning and caching need no change of interfaces.

- **Flat, indexable storage.**  Nodes live in per-TU arenas, referred to by
  32-bit indexes, not pointers; children are contiguous ranges.  A tree can
  then be copied, memory-mapped, hashed and relocated as a block, and a
  later SoA layout or node packing is local to `syntax_tree`.
- **Tokens by index.**  The parser holds token indexes into the token
  stream's buffer, never copies of `token`; spellings stay in the source or
  the interned table.  A cached token stream (one per header) can replace
  the live one behind the same interface.
- **Pure functions of their inputs.**  Parsing a declaration depends only on
  its tokens and the `declared_names` state it reads; that state is
  recorded as a list of (name, kind) lookups so a cached result can be
  checked before reuse, as vcc's C preprocessor does for header replay
  (`docs/RESEARCH-preprocessor.md`).
- **Cache points named now**, each a boundary with a hashable key and no
  hidden state crossing it:
  - an included header's token stream (key: file identity + macro state
    read, already modelled in the C side's header cache);
  - a header's parsed declarations and the names they declare (key: the
    above + the `declared_names` lookups it made);
  - a template's tokens and tree, kept unparsed until used, instantiated
    later per argument list (key: template + arguments): the parser keeps
    template bodies as token ranges plus a tree, never only a tree;
  - inline function bodies and complete-class contexts parsed lazily (only
    when used or emitted), which [class.mem]/6 already forces us to defer.
- **Thread-safe by construction.**  No global mutable state: everything is
  on the parser or the TU, so TUs parse in parallel in vccd and a shared
  cache needs only its own lock.
- **Hot paths kept narrow.**  Token kind checks are table lookups on a
  small enum (no string compares outside `declared_names`), the keyword set
  is known from the lexer, and tentative parsing is a mark/rewind of an
  index, so SIMD scanning or a faster lookup table can drop in later.
- **Measured, not guessed.**  `cxxparse -stats` counts tokens, nodes,
  tentative parses and their rewinds, and lookups; those counters decide
  what gets optimized and what gets cached once the pipeline is complete.

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

Six checks.  While a step is written, V4a (clang's accept/reject) on the
affected tests is the main signal, with V1 and V2 as spot checks; all six
are run once, when the step is closed and before its PR is updated
(decided 2026-11-16: development speed first, robustness and completeness
passes later).

### V1. Grammar coverage (`bench/cxx-spec-refs.py`, <1 s; L2 of PLAN-cxx-spec)

- Every `[label]/N` in a comment exists in `docs/cxx-spec`; every production
  quoted before a function is, after white-space normalization, the text of
  the spec's `bnf` block.
- New here: per clause, the nonterminals of `docs/cxx-spec/grammar-index.md`
  against the ones a parser function quotes.  The output is the table
  "clause: productions implemented / in the clause / in this step's scope",
  A wrong quote fails; a missing production is listed, and does not block
  a step.
- Totals to reach: dcl 97, expr 59, class 25, temp 22, stmt 14, module 8,
  except 6, over 3, basic 1 (235 nonterminals).

### V2. The standard's examples (`src/test/cxx/parse/spec/LABEL.cpp`)

- `bench/cxx-spec-examples.py` extracts every code block of a subclause
  from `docs/cxx-spec` once; the files are committed and then annotated by
  hand.  The clauses have about 780 examples (dcl 179, temp 215, class 132,
  expr 83, basic 74, over 56, module 18, stmt 14, except 10).
- A line the standard marks `// error` or `// ill-formed` is annotated
  only when the parser disagrees with what the example says, with one of `// syntax-error` (the parser must report an error on this
  line) or `// semantic` (ill-formed for a reason the parser does not check;
  it must accept).  Everything else must parse with no `erroneous` node.
- Where an example states which reading an ambiguity has
  ([dcl.ambig.res], [stmt.ambig], [temp.names], [temp.res]) the test has a
  `-dump-syntax` snapshot of that declaration.
- An example that needs a later step's grammar is listed in the file's
  header (`// needs: P5`) and counted, not silently skipped.

### V3. Our own cases per production (`src/test/cxx/parse/gram/LABEL.cpp`)

- Only where clang's tests and the standard's examples leave a gap: an
  alternative or a rejection no test reaches (V1's table and a coverage
  count show which).
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
- **V4b declarator claims** (written once P1's declarators are stable, not
  during P1).  `cxxparse -declaration-claims` writes, for
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

Deferred until P1-P3 have grammar and error recovery to stress, then run
as a pass of its own.

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

Per step, in this file: V4a's numbers, the `unparsed` count over the
corpus, and anything surprising.

### Keeping the full run cheap

clang's side of V4 (its diagnostics and `-ast-dump` per test and option
set) never changes for a given clang and test file: it is cached in
`work/cxx-parse-ref/`, keyed by the file's contents, the options and
clang's version, so a full run is dominated by cxxparse alone.

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
- Status (2026-11-16, done): `parse/` (token stream, tree, declared names,
  parser with the splitter), `cxxparse`, `bench/cxx-parse.py`,
  `bench/cxx-spec-refs.py` (in CI), `bench/cxx-spec-examples.py` (87 files
  for P1's subclauses).  1729 runs of 1050 clang tests: tokens 1729/1729,
  no crash 1729/1729, declarations 1310/1310 where clang finds no syntax
  error; V1: 293 citations, 53 productions, 0 wrong.  ~250 ns per token, all
  of it phases 1-7.  The ASan and mutant runs (V5) are left for later
  (decided 2026-11-16: keep moving, catch those bugs in a later pass).

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
- Status (2026-11-16, grammar done; V4b's declarator claims were written
  with P3, see there): V4a over the
  whole corpus (1811 runs of 1133 tests): 9083 declarations accepted or
  rejected as clang does, none rejected that clang accepts, none accepted
  that clang rejects; 10692 kept as tokens for later steps; 48749 unparsed
  nodes.  Surprises: V4a counts only clang's Parse errors against an
  accepted declaration, but any clang error for a rejected one (clang's
  Parse/Sema split is not the standard's: `i = 4;` at namespace scope is a
  Sema error to it); [dcl.type]/2 and [dcl.typedef]/1 are checked by the
  parser, since they constrain the decl-specifier-seq's form; and
  known-differences.txt holds clang's limits ([implimits]),
  `#pragma clang attribute`, P2361R6 applied as a DR, and error recovery.
  With clang's side cached, a full run takes about 2 s.

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
- Status (2026-11-16, done): `dcl/dcl.attr.grammar.cpp` (attribute-specifier-seq
  in every place P1's grammar has one, balanced-token-seq, alignas kept as
  tokens, the standard attributes' argument forms, [dcl.attr.grammar]/2, /4's
  ellipsis and keywords as identifiers, /7's `[[`, likely with unlikely),
  `dcl.enum.cpp` (enum-specifier, opaque-enum-declaration, enumerators,
  using-enum-declaration), `namespace.udecl.cpp`, `dcl.asm.cpp`,
  attribute-declaration, and [dcl.type.elab]/1's sole-constituent rules.
  V4a over the clang corpus (1811 runs): 9556 declarations accepted or rejected
  as clang does (P1: 9083), none either way against it; 10212 kept as tokens
  (P1: 10692).  V2: 37 examples of the 13 subclauses, every error line
  annotated (two [dcl.attr.grammar]/7 errors are inside a function body, P4's;
  the rest semantic).  V1: 126 productions, dcl 67 of 97 nonterminals, 0 wrong.
  Surprises: clang's parser rejects a standard attribute or alignas that
  appertains to a type (after a decl-specifier-seq, ptr-operator, array or
  parameters), which [dcl.attr.grammar]/5 with each attribute's "may be
  applied to" makes ill-formed, so the parser checks it too; "at most once in
  each attribute-list" is not checked (P2156R1, C++23, applied by clang to
  every edition); known-differences.txt gains clang's attributes on
  using-declarations, its rejection of `[[...]] asm`, and `enum E;` as a
  redeclaration.  Not kept: a scoped enumeration's enumerators and the names
  a using-enum-declaration introduces (no type among them).

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
- Status (2026-11-16, grammar and both halves of V4b done; V5 is still
  open): one file per subclause in
  `expr/`: `expr.prim.cpp`, `expr.prim.id.cpp` (the id-expression of an
  expression, decltype-specifier `::`), `expr.post.cpp`, `expr.unary.cpp`
  (with sizeof, alignof, noexcept), `expr.new.cpp`, `expr.delete.cpp`,
  `expr.cast.cpp`, `expr.compound.cpp` (the twelve binary productions of
  [expr.mptr.oper] to [expr.log.or] as one table, [expr.comma],
  [expr.const]), `expr.ass.cpp` (with [expr.cond]); `dcl/dcl.init.cpp`
  (initializer, braced-init-list, initializer-list, designated
  initializers) and `dcl/dcl.ambig.res.cpp`.  Parsed where P1-P2 kept
  tokens: array bounds, static_assert, enumerator values, alignas,
  decltype, default arguments, noexcept-specifiers and every initializer.
  What is left as tokens is a function body (P4), a class body (P5) and
  whole declarations of later steps.
  - Ambiguities: [dcl.ambig.res]/2 is one function,
    `parenthesized_type_id`: a tentative `( type-id )`, for a cast only
    when a cast-expression can start after the `)` (so `(T())` alone is an
    expression and `(T()) * 2` a cast; clang reads both the same way).
    `~` is always the operator ([expr.unary.op]/10), `delete []` the array
    form ([expr.delete]/1), a new-type-id the longest sequence
    ([expr.new]/3).  `>` in a template-argument-list is P7's.
  - Names: an expression's name that `declared_names` does not know (a
    template, a class's member, a name declared in a part kept as tokens)
    is parsed as an id-expression, but `<` after it is P7's to read, and a
    declaration that then fails is kept as tokens, not reported.  Where a
    type-id is possible only if such a name is a type, the tokens are read
    a second time with the name as a type; if they are no type-id even
    then they are an expression (`sizeof(f(x, 0))`), otherwise the
    declaration is a later step's.  The parameters' scope now lasts to the
    end of the function declarator ([basic.scope.param]/1), so
    `noexcept(noexcept(x))` and `-> decltype(x)` find `x`.
  - Editions: braced-init-lists outside `= { }` before C++11 (N2672) and
    designators before C++20 (P0329R4) are clang's extensions there, so a
    later step's; `&&` after a new-type-id is no ptr-operator before
    C++11; `a[x, y]` is an expression-list from C++23 (P2128R6).  Also
    left to P9 as extensions: statement expressions, compound literals,
    `alignof` of an expression, `a ?: b`, C99's nested, array and mixed
    designators.  throw, co_await and co_yield stay P6's, lambdas P6's,
    folds, requires-expressions and `sizeof...` P7's, operator names P5's.
  - V4a (1927 runs: clang's 1724, 196 of the standard's examples, 7 of our
    own): 9163 declarations accepted or rejected as clang does, none
    either way against it, 11299 kept as tokens; 40771 unparsed nodes (P1:
    48749).  Fewer declarations are comparable than after P2 (9556): 625
    declarations of clang's tests (one C++20 run of each) that P2 accepted
    with an opaque expression are now kept whole, because the expression
    has a template-id (most of them), a lambda, a requires-expression, a
    type-trait keyword or an operator name.  Checked declaration by
    declaration against a build of P2: none of them is P3's grammar.
  - V4b for expressions: `cxxparse -expression-claims` prints each
    expression and the same tokens with the operands of every operator in
    parentheses; `bench/cxx-parse.py` has clang compile, after the
    expression's declaration, `static_assert(__is_same(decltype((E)),
    decltype((P))))` and `static_assert((E) == (P) || (E) != (E))`.  1661
    expressions verified by value, 840 by type only (no constant, or no
    number), none refuted, 373 not checkable (E does not compile where the
    assertion stands).  Swapping two rows of the precedence table is caught
    by the first test.  The parenthesizer leaves three things as written,
    because parentheses change them: the operand of decltype, a
    qualified-id (`&S::m`), and a call's own parentheses (`(T()) * 2`).
  - V4b for declarators (P1's open half): `cxxparse -declaration-claims`
    (`src/bin/cxxparse/declarator-claims.h`) works out, for every name a
    simple-declaration, function-definition or alias-declaration declares,
    the type its decl-specifier-seq and declarator give it, as [dcl.meaning]
    words it: T from the specifiers (Table 14's row found by counting them:
    `long unsigned int long` is `unsigned long long`), then each form of D
    applied to T from the outside in, the suffixes of a noptr-declarator
    from the last to the first.  The claim says how the type is built,
    `array_of<pointer_to<function<const_<pointer_to<int>>(char, ...)>>,
    (3)>`, and clang compiles `static_assert(__is_same(decltype(NAME),
    CLAIM))` after the declaration (NAME itself for a typedef-name;
    `__remove_extent(...)[]` for an array of unknown bound, which an
    initializer completes).  Over the corpus: 11874 declared types
    verified, 439 more as one of an overloaded name's overloads (a
    `static_cast` to the claimed type selects it), none refuted, 1915 in
    declarations clang rejects (what it then declares is its recovery's),
    165 not checkable (deleted and member functions, ambiguous names).
    Left out: a name with a placeholder type to deduce, of an unnamed class
    or enumeration, a declaration with a macro's tokens, a type that names
    one of its own parameters (`-> decltype(x)`), and a declaration with a
    vendor's attribute (clang's `[[gnu::noreturn]]` is part of its function
    type).  Checked that it catches mistakes, one at a time: the suffixes
    applied first to last (3 refuted in `gram/dcl.meaning.cpp`), the
    ptr-operator applied after its declarator (20), `long long` read as
    `long` (3), and the parser dropping a ptr-operator's cv-qualifiers (11).
    An ill-formed claim (an array of arrays of unknown bound) is an error
    inside the alias template, which the harness finds through clang's
    "in instantiation of" note.
  - V1: 167 quoted productions, 0 wrong; expr 34 of 57 nonterminals (the
    other 23 are lambdas, requires-expressions, folds, await, yield and
    throw), dcl 74 of 97.  V2: 72 examples of 25 subclauses extracted and
    annotated (3 syntax errors, the rest semantic); 15 parse whole now, the
    others need a function body, a class or a template (their `needs:`
    line).  V3: `src/test/cxx/parse/gram/`, seven files, one per group of
    productions, written so that a wrong grouping changes a value.  V6:
    `cxx-lex.py` 121/121.
  - [implimits]: expressions and initializers nest at most 1024 levels
    (casts and unary operators, the operands of `?:` and of assignments,
    parentheses, braces; a parenthesized expression is two of them), so the
    parser's recursion is bounded; a declaration past the limit gets that
    diagnostic even when a tentative parse found another.  Chains of binary
    operators, of commas and of postfix operators are loops and have no
    limit.  A first look at V5: ten token mutants of every test (12470
    inputs) and sixteen inputs nested 100000 deep end with status 0 or 1;
    the ASan run is still V5's.  Declarators and namespaces nest without a
    limit yet (P1's recursion).
  - Measured: 31 ms for the 83378 tokens of `CXX/drs` concatenated, about
    370 ns per token with phases 1-7 (7184 tentative parses, 2485 rewound).
  - Surprises: clang's parser rejects `T a = delete ("x");` as a deleted
    definition where the grammar has an initializer with a
    delete-expression (known-differences.txt); a braced initializer was a
    sibling of its declarator, not inside the init-declarator; `[` after a
    parameter's type was taken for a structured binding.  A cold run of
    clang's side takes about 50 minutes on this machine (Python reads each
    `-ast-dump` under one lock); cached, the whole run is 18 s.

### P4. Statements and function definitions

- [stmt] whole (14 nonterminals), [stmt.ambig], *condition* and
  *init-statement*, range-based for; [dcl.fct.def.general],
  [dcl.fct.def.default], [dcl.fct.def.delete]; block scope in
  `declared_names`.
- Tests: `CXX/stmt.stmt`, `Parser/cxx-stmt.cpp`, `cxx-condition.cpp`,
  `cxx0x-condition.cpp`, `cxx0x-for-range.cpp`, `cxx1z-init-statement.cpp`,
  `cxx2a-init-statement.cpp`, `cxx2b-label.cpp`,
  `cxx11-stmt-attributes.cpp`, `cxx-default-delete.cpp`.
- Status (2026-11-16, done; V5 is still open): one file per subclause in
  `stmt/`: `stmt.pre.cpp` (statement, condition, init-statement, the
  recovery point), `stmt.label.cpp`, `stmt.expr.cpp`, `stmt.block.cpp`,
  `stmt.select.cpp`, `stmt.iter.cpp` (with range-based for), `stmt.jump.cpp`,
  `stmt.dcl.cpp` (block-declaration), `stmt.ambig.cpp` and `stmt.ambig.h`;
  `dcl/dcl.fct.def.general.cpp` parses a function-body.  `= default` and
  `= delete` were P2's.  A function body re-enters its parameters' scope
  ([basic.scope.param]/1); a compound statement, a selection or iteration
  statement and each of their substatements are block scopes.
  - [stmt.ambig]: what the first tokens allow decides
    (`reading_at`): a decl-specifier no expression starts with is a
    declaration; a type-name or a simple type's keyword before `(` or `{` is
    tried as a declaration and then as an expression; a declared non-type
    name is an expression.  A name that is not declared is tried both ways
    with unknown names read as types; when both readings parse, or either
    needs a later step, the statement is kept as tokens: it is a template's
    dependent name or a name P5 and P7 will declare.
  - A statement is a recovery point as a declaration is: one that fails is
    `unparsed` when it uses a later step's grammar or an undeclared name,
    else `erroneous` with its diagnostic; a statement that does not end
    (`case v }`) is erroneous.  Only the outermost tentative parse recovers
    (`speculation`), so a statement inside a tentative parse still fails
    the parse.
  - Rules the grammar does not say and clang checks while parsing:
    [stmt.pre]/8 (only type-specifiers and constexpr in a condition's
    declaration), [dcl.ref]/5 (no reference to a reference, `int & &&r`),
    a function-definition only at namespace or class scope; `auto` is a
    storage class before C++11 ([diff.cpp03.dcl.dcl]/1) and `auto int x`,
    clang's extension, is P9's.
  - V4a: 1951 runs, 9236 declarations as clang, 0 rejected that clang
    accepts, 0 accepted that clang rejects, 11231 kept as tokens; 40719
    `unparsed` and 680 `erroneous` nodes.  What is still kept is mostly
    templates (P7), base clauses and ctor-initializers (P5, the `:`),
    operator-function-ids (P5) and lambdas (P6).  V4b: 0 refuted.  V1: 440
    citations, 181 productions, 0 wrong; stmt 13 of 14 nonterminals (the
    coroutine-return-statement is P6's).  V2: 21 examples of [stmt.*] and
    [dcl.fct.def.*] extracted; statements the standard shows outside a
    function (here and in earlier steps' examples) are wrapped in
    `void example() {` ... `}` on the example's blank first line, which
    `cxx-spec-examples.py` ignores when it compares.
  - Surprises: clang's errors after `#pragma` (`#pragma clang attribute
    push` and the like) are about the pragma, not the declaration before
    it, so `cxx-parse.py` does not compare a declaration whose only errors
    are on a pragma's line or the next; clang reads `auto(&a)->n` as an
    expression (P0849R8), so `auto` before `(` is tried both ways;
    `if consteval {` and other statements that open a brace without `)`
    before it needed the token splitter to know which braces are
    statements'.  Two of clang's tests are known differences
    (cwg1753's `n.cwg1753::~T()`, gh137861's attribute before a pragma).

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
- Status (2026-11-16): done.
  - A class body is read twice ([class.mem]/6).  The first pass, inside a
    tentative parse, reads the member-declarations with the
    complete-class contexts (function bodies, ctor-initializers, default
    member initializers, default arguments, nested class bodies) kept as
    tokens, and keeps the names it declared; the second pass reads it all
    with those names visible from the complete-class contexts.  A class's
    region in `declared_names` is carried by its class-name; bases join
    the region's lookup; `S::f` in a declarator-id puts the parameters,
    body and a static member's initializer in S's region.
  - The constructor decision: the class's own name before `(` (or an
    attribute-specifier-seq and `(`) followed by what can begin a
    parameter-declaration ends the decl-specifier-seq; a constructor does
    not redeclare the injected-class-name.  `S::S` names the constructor
    ([class.qual]/2).
  - Rules clang checks while parsing and the standard states:
    [class.mem]/12 (only a constructor, destructor or conversion function
    omits the decl-specifier-seq), /14 (each virt-specifier at most once),
    [class.friend]/2 (no class defined in a friend declaration), friend
    only in a member-declaration, [dcl.attr.grammar]/5 (attributes on a
    friend declaration only on a definition), [dcl.fct.def.default]/1 by
    the declarator-id (an identifier other than the class's, or an
    operator other than `=` and the comparisons, is not defaulted),
    [dcl.array]/7 (no omitted bound on a non-static data member),
    [over.literal]/1 on every piece of a concatenated string-literal.  In
    C++20 a function's name before `<` is a template-name ([temp.names]/2,
    P0846R0): such an expression is P7's (`name_kind::function`).
  - A later edition's or an extension's, so P9's: C++23's explicit object
    parameter (`this T self`), C++26's `friend T, U;`, Microsoft's ATL
    attributes and `= 0 { }`, attributes on a using-declaration.  A
    declaration kept as tokens that runs into the enclosing `}` is still
    erroneous (it ends with `;` or `}` in every step).  An undeclared
    class-or-decltype marks the declaration as naming something unknown.
  - The keyword table: `private` lexed as an identifier, because
    `gen-cplusplus-data.py` let an alias with only KEYWORD_OTHER override
    the keyword's own entry; fixed, `lex.key.inc` regenerated.
  - Harness fixes: `-verify` was taken as an option with a value, so the
    option after it was dropped (`-fms-extensions` among others): every
    harness that reads RUN lines through `cxx-pp.py` now runs those
    options (cxx-pp: 515 runs instead of 505, tokens 480 instead of 471;
    the newly run option sets that fail are preprocessor work, not this
    step's).  `cxx-parse.py` gives a test that includes `<...>` clang's
    resource directory (`stddef.h`), counts an error just after a part
    kept as tokens, on its last line, as that part's, and a claim whose
    type is inaccessible at the probe ("is a private member") as not
    checkable.
  - V4a: 2203 runs, 12247 declarations as clang, 0 rejected that clang
    accepts, 0 accepted that clang rejects, 10194 kept as tokens; 31388
    `unparsed` and 1032 `erroneous` nodes.  V4b: 0 refuted.  V1: 492
    citations, 213 productions, 0 wrong; class 25 of 25, over 4 of 4.  V2:
    155 examples of [class.*], [over.*] and [dcl.struct.bind] extracted
    and annotated (`needs:`, each error line `// syntax-error` when clang's
    parser reports it, else `// semantic`).  V6: 121/121.  Known
    differences: clang's FIXME in `cxx-ambig-init-templ.cpp` (`int k = b
    < c, unsigned int (x)` is two parameters) and a `_Pragma` from a macro
    inside a struct.

### P6. Lambdas, exceptions, coroutines

- [expr.prim.lambda], [except] (6 nonterminals: try-block,
  function-try-block, handlers, throw), [expr.await], [expr.yield],
  `co_return`.
- Tests: `Parser/cxx0x-lambda-expressions.cpp`, `cxx2b-lambdas.cpp`,
  `cxx1z-constexpr-lambdas.cpp`, `cxx-throw.cpp`, `cxx20-coroutines.cpp`,
  `CXX/expr/expr.prim/expr.prim.lambda`, `CXX/except`.
- Status: done (2026-11-16).
  - Files: `expr/expr.prim.lambda.cpp` (lambda-expression,
    lambda-introducer, lambda-capture, capture, init-capture,
    lambda-declarator), `expr/expr.throw.cpp`, `expr/expr.await.cpp`,
    `expr/expr.yield.cpp`, `except/except.pre.cpp` (try-block, handler-seq,
    handler, exception-declaration), `stmt/stmt.return.coroutine.cpp`; a
    function-try-block in `dcl.fct.def.general.cpp`, C++14's
    dynamic-exception-specification in `except/except.spec.cpp`.
  - Scopes: a lambda's captures are declared in a region of their own, its
    parameters in a function parameter scope the compound-statement
    re-enters; a handler's exception-declaration is in the handler's block;
    a function-try-block's handlers see the parameters ([except.handle]/11).
    A class body's first pass skips a function-try-block as one unparsed
    part, like a function-body.
  - Editions by feature (`language-features.inc`): lambdas (C++11),
    init-captures (C++14), `constexpr` lambdas and `*this` (C++17);
    `throw ( type-id-list )` until C++17 and `throw ( )` until C++20 (P0003R5,
    [diff.cpp14.except]/1, [diff.cpp17.except]/1; after that clang's
    extension, P9's, and an empty one a noexcept-specifier).  Each
    lambda-declarator specifier at most once ([dcl.spec]/2).
  - Later steps: a template-parameter-list after the introducer and packs in
    captures (P7), C++23's lambda without `()` before a specifier and
    `static` lambdas, `throw(...)` (P9).  In braces, `[` starts a lambda
    unless `=` follows its group (an array designator, P9).  `decltype` of a
    lambda gets no declarator claim (each closure type is unique).
  - `cxx-spec-refs.py` reads a grammar block whose head is indented
    ([expr.yield]).
  - V4a: 2244 runs, 12580 declarations as clang, 0 rejected that clang
    accepts, 0 accepted that clang rejects, 9980 kept as tokens; 30025
    `unparsed` and 1143 `erroneous` nodes.  V4b: 0 refuted.  V1: 514
    citations, 233 productions, 0 wrong; except 6 of 6.  V2: 41 examples of
    [expr.prim.lambda*], [except.*], [expr.await], [expr.yield],
    [expr.throw] and [dcl.fct.def.coroutine] extracted and annotated; those
    that need `std::` or generic lambdas are P7's.  V6: 121/121.

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
- Status: done (2026-11-16), except the known differences below.
  - Files: `temp/temp.pre.cpp` (template-declaration, template-head,
    requires-clause, constraint-logical-or/and, explicit-specialization,
    explicit-instantiation), `temp/temp.param.cpp`, `temp/temp.names.cpp`
    (template-ids, the `>` rule with split `>>`/`>=`/`>>=` tokens recorded in
    `syntax_tree::split_tokens`), `temp/temp.res.cpp` (typename-specifier),
    `temp/temp.concept.cpp` (concept-definition, deduction-guide),
    `expr/expr.prim.fold.cpp` (fold-expression, requires-expression and its
    requirements).
  - `declared_names`: template-parameter scopes, searched before the
    enclosing namespace for an out-of-class member ([temp.local]/8); template
    kinds (class, function, variable template, concept) from
    `parser::declared_kind`; a `dependent` flag per declaration and
    `qualifier_dependent` for a nested-name-specifier; a specialization's
    members are looked up in the primary template.  Implicit typename
    ([temp.res]/5) in the contexts the paragraph lists, from C++20.
  - Rules clang treats as syntax and the parser checks: a class template's
    name before `class X<...>`, `template` before a dependent
    template-name, a requires-clause only after a function declarator
    outside classes, parenthesized non-primary operands in a
    requires-clause, one declarator per template declaration and none after
    a class template's definition, no enum or attributes in an explicit
    instantiation, no template-id in a using-declarator, no ellipsis in a
    requirement-parameter-list.
  - V4a: 2244 runs, 20261 declarations as clang (12580 after P6), 0
    rejected that clang accepts, 27 accepted that clang rejects, 2231 kept
    as tokens (9980); 6363 `unparsed` and 1440 `erroneous` nodes.  What is
    kept is P8 (modules), P9 (extensions, `throw(...)` after C++17,
    undeclared names) and the TODO below; `unparsed` is not zero yet.
    V4b: 0 expressions refuted; 6 declared types refuted, all cwg254's `n`
    redeclared with clang's error inside the template.  The claims skip a
    template-name used as a deduced class type.  V1: 616 citations, 267
    productions, 0 wrong; temp 22 of 22.  V6: cxx-lex 121/121.
  - Known differences (we accept, clang rejects; 8 declarations): whether
    `A<U>` is the current instantiation (`temp.res/p3.cpp:28`); a decltype
    or sizeof operand that is type- or value-dependent
    (`dcl.spec.auto/p12-1y.cpp`, `cwg5xx.cpp:547`); a member template of a
    dependent object without `template` (`cwg4xx.cpp:626`,
    `cwg1xx.cpp:689`); a constrained parameter whose concept takes a
    template (`cxx2a-constrained-template-param.cpp`); parameter
    redefinition (`cxx2a-concepts-requires-expr.cpp:25`,
    `cxx-ambig-decl-expr-xfail.cpp`).
  - TODO (C++23/C++26, not now; each returns `beyond_this_step()`):
    pack indexing `T...[N]` (P2662R3), braced-init-list template arguments
    (CWG2450), variadic friends `friend Ts...;` (P2893R3), `static` lambdas
    and lambdas without `()` before a specifier (P1169R4, P1102R2),
    `decltype(auto)(x)` (P0849R8), placeholder-free `auto(x)`, `using enum`
    with a simple-template-id.

### P8. Modules

- [module] (8 nonterminals), [cpp.module]'s tokens from the preprocessor.
- Tests: `CXX/module`, `Parser/cxx-modules-*.cppm`.
- Status: done (2026-11-16).
  - `module/module.unit.cpp`: module-declaration, module-name,
    module-partition, module-name-qualifier, module-import-declaration (a
    module-name, a partition or a header-name), export-declaration (one
    declaration, braces, or `export import`), global-module-fragment and
    private-module-fragment; translation-unit's second alternative in
    `basic/basic.link.cpp`.  export-keyword, import-keyword and
    module-keyword are read by their phase-4 preprocessing kind
    (`at_phase4_keyword`); `export` elsewhere is the keyword.
  - Checked: no export-declaration inside another or in a class
    ([module.interface]/1), an import only at global namespace scope
    ([module.import]/1), no `module` or `import` in a module-name, a
    module-declaration only where translation-unit puts it.  Before C++20,
    `export template` (C++98) is P9's and any other `export` an error.
  - `cxxparse -dump-declarations` lists a module unit's declarations
    across its fragments.
  - V4a: 20338 declarations as clang, 0 rejected that clang accepts, 29
    accepted that clang rejects (P7's 8 declarations, and two "module not
    found": clang has no module files, not syntax), 2152 kept as tokens;
    6275 `unparsed`.  `CXX/module` keeps three declarations as tokens, all
    `__attribute__`/`__declspec` (P9).  V1: 622 citations, 275 productions,
    0 wrong; module 8 of 8.  V6: cxx-lex 121/121.  There are no `.cppm`
    tests in the harness's corpus (it reads `*.cpp`).

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

- Status (2026-11-16): in progress.
  - The corpus: libstdc++ 14's headers (copied from the Linux container to
    `work/cxx-sysroot`), each alone, and nlohmann/json (`work/cxx/json`,
    `json.hpp` with them, `-std=gnu++17`).  `cxxparse -debug-fallbacks`
    names the file and the token where a declaration stopped.
  - Extensions: `ext/extension-keywords.inc` maps the lexer's keywords an
    extension gives grammar (GNU's alternate spellings `__inline__`,
    `__const__`, `__signed__`, `__volatile__`, `__asm__`, `__alignof__`;
    `__attribute__`, `__restrict__`, `__extension__`; clang's type traits).
    `ext/gnu/attribute.cpp` (where `[[ ]]` can be, among decl-specifiers,
    after a declarator and a namespace name; clang's placement rules),
    `ext/gnu/asm_label.cpp`, `ext/gnu/extension_keyword.cpp`,
    `ext/clang/type_traits.cpp` (expression traits, type transformations,
    and a trait's keyword as a name when no `(` follows: libstdc++'s
    `__is_signed`).  Declarations with an extension get no claims.
  - The extensions' grammar (2026-11-17): `ext/grammar.inc`, 41
    productions of 12 extensions, every one quoted where it is parsed;
    `[ext:NAME]` citations (44) checked against `extensions.inc`.
  - Lookup fixes the corpus found: a function beside a function template
    keeps the name a template-name before C++20 ([temp.names]/2), a
    complete-class context searches the whole class first, and a typedef or
    alias of a class names its members, also when made before the class's
    definition (`gram/temp.names.cpp`, `gram/dcl.typedef.cpp`).
  - Numbers: libstdc++ declarations kept as tokens 54k -> 20.8k; json.hpp
    0 erroneous, 3140 kept as tokens (most in libstdc++).  V4a: 21088 as
    clang, 0 rejected that clang accepts, 32 accepted that clang rejects
    (new: `__attribute__((aligned(int)))` kept as tokens, an MSVC bit-field
    case), 1492 kept as tokens.  V1: 0 wrong.
  - Next: the `(` fallbacks (7.9k), `__int128` and clang's predeclared
    builtin types, `__complex__`, a switch to turn an extension off for its
    syntax-error test.

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
8. **Optimizable and cacheable by design, not tuned yet**: index-based
   storage, recorded lookups and named cache points (headers, templates,
   lazy bodies) from P0; optimization only after the full pipeline runs.
