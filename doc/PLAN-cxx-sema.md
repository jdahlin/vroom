# From the C++ syntax tree to code: semantics and lowering

Status: plan, for review before any code (2026-11-17).  Continues
`PLAN-cxx-parse.md` after P9; the conventions of `PLAN-cxx-spec.md` hold
(layout mirrors the standard, the standard's terms as names, comments cite
and quote, extensions in `ext/`, owners, no conditional operator).

The parser gives a syntax tree of the grammar's nonterminals with tokens by
index, and `declared_names`, a stand-in for lookup that knows only whether a
name is a type, a template or a namespace.  Code generation reads
`struct ast` (`src/include/frontend/ast.h`, records in `frontend/c/cc.h`):
types, tags with layout, symbols with linkage, and typed nodes of C's kinds
with every conversion explicit.  This plan is everything between the two:

```
tokens -> syntax tree -> [semantic analysis] -> semantic program -> [lowering] -> struct ast -> walker -> -O1 / VIR
           (P0-P9)        this plan, S1-S8       (new, section 3)    this plan      (exists)
```

"Resolver" and "sema" are one component here.  In C++ what a name means
cannot be settled before types, overload resolution and instantiation
([basic.lookup.argdep] needs argument types, [temp.dep] needs template
arguments), so there is no separate name-binding pass to write first.

## 1. Priorities

1. **The standard decides the meaning; the Itanium ABI decides the bytes.**
   Section 2 says which is which.
2. **End to end from the first step.**  S1 already compiles and runs
   programs through the driver.  Every later step widens all layers and ends
   with more programs running, never with a layer that nothing can execute.
3. **clang is an oracle, not a specification** (as for the parser): it
   answers narrow questions about our claims (section 4).  Its wording,
   recovery and warnings are not matched.
4. **Never a silent wrong answer.**  What is not written yet is an
   `unsupported` result naming the paragraph (section 5), counted and driven
   to zero, as `unparsed` was.
5. **Designed for speed and caching, tuned later** (PLAN-cxx-parse,
   "Built to be optimized and cached later").  The goal of `PLAN-cxx.md`
   (json's tests 30 times faster than clang -O2, through the instantiation
   cache) is why this front end exists; its measurements resume in S8.

## 2. How much of the gap the standard covers

The semantic clauses are in `docs/cxx-spec` and are what sema is written
against:

| Clause | Subclause files | "shall" | "ill-formed" | clang test/CXX files |
|---|---|---|---|---|
| basic | 56 | 128 | 30 | 67 |
| dcl | 62 | 224 | 88 | 173 |
| expr (with conv) | 79 | 147 | 46 | 120 |
| class (with special, access, derived) | 51 | 97 | 41 | 142 |
| over | 36 | 18 | 16 | 60 |
| temp | 61 | 122 | 50 | 224 |
| except | 9 | 11 | 4 | 21 |
| stmt | 21 | 25 | 12 | 11 |

That is the whole of analysis: lookup, types, conversions, initialization,
overload resolution, deduction and instantiation, constant evaluation, what
a constructor, a destructor or a `throw` does.  The standard covers the left
box of the picture above, and none of the right one.  What it does not say:

| Not in the standard | Where it is specified | Size |
|---|---|---|
| Name mangling | Itanium C++ ABI 5.1 | medium; substitutions and template arguments are the hard part |
| Class layout, empty bases, tail padding, virtual bases | Itanium ABI 2.1-2.4 | medium; C's layout code is the POD case |
| vtables, VTT, thunks, construction vtables, key function | Itanium ABI 2.5-2.6, 5.2 | large |
| RTTI objects, `dynamic_cast` | Itanium ABI 2.9, the runtime's `__dynamic_cast` | small |
| Guard variables, static initialization order, `__cxa_atexit` | Itanium ABI 3.3 | small |
| Passing and returning classes ("non-trivial for the purposes of calls") | Itanium ABI 3.1 + the psABI | small, but every call |
| Constructor/destructor variants (C1/C2, D0/D1/D2), array cookies | Itanium ABI 2.7, 5.1.4 | small |
| Vague linkage (COMDAT for inline functions, instantiations, vtables) | Itanium ABI 5.2 | small; `obj/` has COMDAT for COFF, to check for ELF |
| Exception tables, personality, `__cxa_*` | Itanium ABI level I/II, LSDA as GCC's | large in codegen, small in the front end |
| What the library expects of the compiler | `lib/support` ([support.initlist], [support.rtti], [cmp], ...) is in the spec; libstdc++'s `__builtin_*` and type-trait intrinsics are not | long tail, per corpus (S8) |
| Core issues resolved after n4861 | the CWG issues list; clang's `test/CXX/drs` is the oracle | per case, in `known-differences.txt` |
| GNU, clang and Microsoft extensions | their documentation; clang's behaviour | per corpus, in `ext/` |

So: the spec covers the part with the most rules and the most lines, and
its paragraphs give the structure and the tests.  The ABI is a second,
smaller reference of the same kind and gets the same treatment:

- its sections cited by their HTML anchor, `[abi:vtable-components]` (2.5.2),
  `[abi:mangling-type]` (5.1.5), checked by `bench/cxx-spec-refs.py`.  The
  text has no licence that allows copying it into the tree (decided
  2026-11-17): `fetch-testsuites.sh itanium-abi` fetches `abi.html` and
  `abi-eh.html` at a pinned commit, and `bench/itanium-abi.py` writes the
  committed index `docs/itanium-abi-index.tsv` (label, file, number,
  title: 179 sections) and each section's text to `work/itanium-abi/`;
- its code apart from the clauses, in `abi/itanium/` (and later
  `abi/microsoft/` for Windows x64), so sema never knows a mangled name or
  an offset rule.

The implementation-defined answers are already data (`intro/`).

## 3. Architecture

### 3.1 Files

```
src/frontend/cplusplus/
  sema/                    not a clause: what every clause's analysis uses
    entities.cpp           declarations and their redeclaration chains
    types.cpp              the type table (hash-consed, canonical)
    semantic_tree.cpp      typed expressions and statements
    instantiation.cpp      the unit of instantiation and its record (3.5)
    unsupported.cpp        the scaffolding result and its counters
  basic/basic.lookup.unqual.cpp, basic.lookup.argdep.cpp, basic.link.cpp, ...
  conv/conv.lval.cpp, conv.prom.cpp, ...
  expr/expr.add.cpp, ...   beside the parser's function for the same subclause
  dcl/dcl.init.list.cpp, dcl.meaning.cpp, ...
  class/, over/, temp/, except/
  abi/itanium/mangling.cpp, layout.cpp, vtable.cpp, rtti.cpp, guard.cpp, calls.cpp
  lower/                   semantic program -> struct ast
src/bin/cxxsema/           the tool: -dump-entities, the claims of section 4
```

A subclause's file holds its grammar (written) and its rules (new): the
parser's function builds the syntax node, a second function in the same file
gives it meaning and quotes the paragraph it enforces.  One place per
subclause, and the citation checker sees both.

### 3.2 The semantic program

Sema does not annotate the syntax tree (decision 1 of PLAN-cxx-parse: the
tree is the grammar's).  It builds:

- **Entities** ([basic.pre]/3: variable, function, type, enumerator,
  namespace, template, ...): one record per entity, its declarations chained
  ([basic.link]), its scope, linkage, the syntax node of each declaration.
- **Types**: a table with one record per distinct type, so type identity is
  an index comparison; cv-qualifiers in the handle, as `ast_qualified_type`
  does.  Dependent types are types too.
- **A semantic tree** per function body and initializer: nodes are the
  operations the standard describes, typed, with value category, and with
  every implicit step explicit (conversion sequences, temporaries
  materialized, the constructor a `T x = y;` calls, the destructors a full
  expression and a scope run).  This is the form constant evaluation
  interprets, instantiation substitutes into, and lowering reads.

Same storage rules as the syntax tree: per-TU arenas, 32-bit indexes,
immutable once built, no global state, so headers' results can be shared
between TUs in vccd later.

### 3.3 How it meets the parser

- Sema runs per declaration, as each one is parsed, because the parser
  needs its answers: whether a name is a template, the value of a constant
  in a template argument.
- `declared_names` is an interface the parser asks.  Sema implements the
  same interface with the real [basic.lookup]; the stand-in stays as the
  implementation of `cxxparse` without sema until nothing uses it, then
  goes.  The parser does not change.
- Function bodies, default arguments and default member initializers stay
  token ranges and are analysed when needed ([class.mem]/6 forces it for
  classes; for templates and inline functions it is what makes unused code
  free).

### 3.4 Lowering to `struct ast`

One back end input for C and C++: the walker, VIR and every target stay as
they are.  The old front end did this, so it is known to work; what was
learnt there is in git history before PR #317 (`src/frontend/cxx/codegen/`)
and in `PLAN-cxx.md`'s statuses 24-35.

- A reference is a pointer; a member function a function with `this`
  first; a virtual call a load from the vtable and an indirect call; a
  pointer to member a `ptrdiff_t` or a pair; `new` a call and a constructor
  call; a lambda a class.
- Destructors at scope exit use what `__attribute__((cleanup))` uses today.
  With `options->eh` the walker also runs them when an exception unwinds
  (x86-64 ELF only); arm64 and the catch side are S7.
- To verify in S1, by reading the walker: symbols with vague linkage on ELF,
  a way to say "this symbol is the same function under two names"
  (C1/C2 constructors), per-symbol emission on use (X5 of PLAN-cxx).
- `struct ast` is C's layout-measured structure; a change to it reruns
  `vbench.py gate` and `vcc-same.py` for C.

### 3.5 Instantiation as a recorded unit

From the first template: an instantiation's result is recorded with its key
(template, arguments) and with everything it read from outside itself (the
lookups, the overload sets and specializations visible).  That record is
what vccd's instantiation cache checks before reuse (PLAN-cxx X4-X5,
RESEARCH-cplusplus section 6).  The cache itself is not built in this plan;
the record is, because adding it afterwards means finding every hidden
input again.

### 3.6 Diagnostics

One error per ill-formed declaration or statement, with the paragraph in
the message; no warnings, no notes, no recovery beyond dropping the
declaration.  The move to vcc's diagnostic system (decision 5 of
PLAN-cxx-spec) happens once, after S4, when the set of messages is stable.

## 4. Validation

The principle is the lexer's and the parser's: the front end writes down
what it concluded as **claims**, in a form clang can check by compiling,
and nobody compares printed ASTs or message texts.

### C1. Citations and rule coverage (`bench/cxx-spec-refs.py`, <1 s)

- As today: every `[label]/N` exists, every quote matches the spec.
- New: per subclause, its "shall" and "ill-formed" sentences against the
  ones a check quotes.  The table "clause: rules enforced / in the clause /
  in this step's scope" is the measure of how complete analysis is (772
  "shall", 287 "ill-formed" in the eight clauses).  A missing rule is
  listed and does not block a step; a wrong quote fails.

### C2. The standard's examples (`src/test/cxx/sema/spec/LABEL.cpp`)

- The files `cxx-spec-examples.py` extracted for the parser, with the
  annotation `// semantic` now meaning "must be diagnosed on this line".
- Comments that state a result (`// OK`, `// calls f(int)`, `// type is
  int&`, `// x is 3`) become claims by hand where they are precise.  The
  standard has about 780 examples in these clauses; they are the first test
  of every subclause.

### C3. clang's conformance tests by paragraph (`bench/cxx-sema.py`)

- `clang/test/CXX` is organized as the standard is
  (`basic/basic.lookup/basic.lookup.unqual/p3.cpp`), so each test names the
  paragraph it checks: 901 files with `drs`, 663 of them with `expected-error` lines
  (6236).  Ours: the lines with an error.  Compared per line as
  `bench/vcc-verify.py` does for C: "agree / we reject, clang accepts / we
  accept, clang rejects / unsupported".
- Each disagreement is fixed, or goes to `known-differences.txt` with the
  paragraph or the core issue.
- To fetch in S0 (`fetch-testsuites.sh llvm` has C, Sema, CXX, Parser
  today): `clang/test/SemaCXX`, `SemaTemplate`, `CodeGenCXX`, and
  `libcxx/test/std`.
- "We reject, clang accepts" must be 0 before a step closes; "we accept,
  clang rejects" is the backlog of rules, measured by C1.

### C4. Claims checked by clang

`cxxsema -claims=KIND FILE` writes a file clang compiles or a table
compared with clang's own output.  Each kind lands with the step that
produces it; clang's side is cached by content, options and version in
`work/cxx-sema-ref/`.

| Kind | Our claim | clang's check |
|---|---|---|
| type | the type and value category of every expression | `static_assert(__is_same(decltype((E)), T))` appended to the TU |
| lookup | the declaration (file:line:col) each name refers to | `referencedDecl` in `-ast-dump=json` |
| overload | the function each call, operator and conversion selected | the same |
| constant | the value of every constant expression | `static_assert(E == V)` |
| layout | size, alignment, field and base offsets of every class | `static_assert` with `sizeof`/`alignof`/`__builtin_offsetof`; `-fdump-record-layouts` for bases and vptrs |
| vtable | the entries of every vtable and VTT | `-fdump-vtable-layouts` |
| symbols | each object's defined and undefined symbols, binding and COMDAT | `nm` of clang's `-O0` object: mangling, linkage and "what is emitted" at once |
| instantiation | the set of specializations instantiated | the symbols claim, plus `-ast-dump` for those with no code |

The type and constant claims are checked inside the test's own TU, so they
work on any input that compiles, including the libstdc++ headers: a
regression anywhere in lookup, conversions or deduction shows up as a wrong
type somewhere.

### C5. Programs that run

In the order they become possible:

1. vcc's C suites, the files that are also valid C++ (S1): the same
   expected output as today.  For a file inside `extern "C"` the object is
   compared with the C front end's; a difference is explained or a bug.
2. `src/test/cxx/run/`: our own small programs per feature, expected output
   from clang's build, on arm64 Linux (qemu), x86-64 Linux and macOS.
3. `bench/vcc-abi.py` with C++ cases (clang/vcc x caller/callee, linked
   together): class arguments and returns, virtual calls into the other
   compiler's vtables, exceptions thrown across.
4. llvm-test-suite's SingleSource C++ (123 files, fetched), YARPGen
   (fetched; generates C++ with a checksum) as differential fuzzing.
5. `libcxx/test/std`: thousands of small pass/fail programs organized by
   library clause; the count that pass is the number for S6-S8.
6. nlohmann/json's 71 test TUs (PLAN-cxx's goal), then the other targets of
   `bench/cxx-profile.py`.

### C6. Robustness (the ASan build)

Every input of C2-C5 and their token mutants: never a crash or a hang,
always a result or a diagnostic.  Recursion in instantiation and constant
evaluation is bounded by [implimits]'s quantities and reported as exceeded.
Run as a pass when a step closes, not during it.

### C7. Nothing below moved

`cxx-lex.py`, `cxx-pp.py` and `cxx-parse.py` give the numbers they gave;
with sema answering the parser's questions V4a must not get worse.  For C:
`vcc-same.py` byte-identical and the performance gate, whenever `ast.h`,
the walker or `obj/` changed.

### What is recorded

Per step, in this file: C1's table, C3's four numbers, claims wrong (must
be 0), `unsupported` over the corpus, programs passing in C5, and ns per
token for `-fsyntax-only` on json.hpp.

## 5. Keeping development fast

- **A walking skeleton first (S1).**  The risky interfaces (sema to
  lowering, lowering to the walker, the driver, linking and running) exist
  after the first step, on the smallest language that exercises them.
  Problems in them are found with 2000 lines written, not 60000.
- **`-std=gnu++17` first.**  It is what the corpus uses (libstdc++ 14 and
  json).  Concepts, `<=>` rewriting, `consteval`, coroutines, modules and
  the other C++20 additions are S9; the code is still written against n4861
  with each edition's rule a named feature (`diff/language-features.inc`).
- **`unsupported` instead of half an implementation.**  A construct whose
  rules are not written drops its declaration with `unsupported:
  [expr.new]`, counted.  `cxxsema -debug-unsupported` over the corpus,
  ranked by the declarations each one blocks, is the work list (as
  `vir-failures.py` is for VIR): the next thing to write is the one that
  frees most, in the standard's order inside it.
- **An inner loop under 5 s.**  `bench/cxx-sema.py LABEL` runs one
  subclause: its clang directory, its examples, its claims, with clang's
  side cached.  The full run is for closing a step.
- **Checks in three tiers** (decided for the parser 2026-11-16): while
  writing, C3 and the claims on the affected subclause; when a step closes,
  everything; C6 and performance as passes of their own.
- **Tracks that do not touch each other's files.**  After S2 the shared
  tables are fixed and four tracks can run as separate sessions or PRs:
  expressions and overloading (S3, S5), classes with the ABI (S4), constant
  evaluation (S6a), exceptions in the back end (S7's codegen half, which
  needs no front end: its tests are C with `cleanup` and hand-written
  callers).  One file per subclause keeps conflicts to `sema/`'s headers.
- **Reuse what C has.**  Integer and floating constant folding, bigint and
  soft float (`common/`), record and bit-field layout (already clang's, the
  POD case of [abi:layout]), attributes, the builtin table, the target
  description.  They move behind an interface both front ends call rather
  than being copied (`quality.py`'s duplication gate counts copies).
- **No tuning, no warnings, no fix-its, no clang wording** until S8.  One
  timing per step so that a regression is seen.
- **Small PRs, stacked, one subclause group each**, with the numbers of
  section 4 in the description; `known-differences.txt` instead of a long
  discussion per clang disagreement.

## 6. Steps

Each lists the subclauses it owns, what it leaves `unsupported`, and its
gate.  Before S1: P9 closed (no `erroneous`, the `(` fallbacks gone on
libstdc++ and json), since sema cannot analyse a declaration kept as tokens.

### S0. Scaffolding (no rules)

- `sema/` tables, `cxxsema`, `unsupported`, `bench/cxx-sema.py` with C3 and
  the type claim, the extra suites fetched, the ABI text and its citations.
- Gate: every C3 test runs and reports "unsupported"; C7.
- Status (2026-11-17, done): `src/include/frontend/cplusplus/sema.h` and
  `sema/` (the type table, hash-consed with cv-qualifiers in the handle;
  entities with their declaration chains; `semantic_analysis` per
  declaration with the `unsupported` result naming the subclause), the
  first rule ([dcl.pre]/7, an empty-declaration), `syntax_tree::
  declarations` shared by `cxxparse` and `cxxsema`; `cxxsema`
  (`-dump-analysis`, `-debug-unsupported`, `-stats`, `-selftest`, in CI);
  `bench/cxx-sema.py` (C3); `fetch-testsuites.sh llvm` adds SemaCXX,
  SemaTemplate, CodeGenCXX and `libcxx/test/std`.  C3: 4185 runs of 2618
  tests, no crash; 470 declarations as clang, 0 we reject and clang
  accepts, 18 we accept and clang rejects (empty declarations under
  `-pedantic-errors`), 54001 unsupported, 9113 not parsed.  The work list:
  [dcl.pre] 23.0k, [namespace.def] 13.1k, [dcl.fct.def.general] 8.1k,
  [temp.pre] 6.9k.
- Status (2026-11-17, the rest of S0, done):
  - C4's type claim: `cxxsema -type-claims` and its encoding
    (`src/bin/cxxsema/type-claims.h`, the declarator claims' alias
    templates, the value category as decltype((E)) gives it), checked by
    `cxxsema -selftest`; `cxx-sema.py` has clang check the claims of every
    run (none until S1 types expressions), and `cxx-sema.py selftest`
    (in CI) runs that path on claims written by hand: 7 verified, 3
    refuted, 1 not checkable, as expected.
  - The ABI: no licence in its repository or its pages, so not in the
    tree; fetched and indexed as section 2 says.
  - C1's rule coverage: `cxx-spec-refs.py` counts the sentences that say
    "shall" or "ill-formed" (out of notes and examples) and those a comment
    quotes after citing their paragraph; `--rules CLAUSE` lists the rest.
    Now: basic 1/112, class 5/117, dcl 17/238, except 0/12, expr 7/152,
    module 2/23, over 2/26, stmt 1/33, temp 1/139 (the parser's syntactic
    rules).  "In this step's scope" is not counted: a step's scope is its
    list of subclauses above, which `--rules` per subclause can show.

### S1. The walking skeleton: C's subset of C++

- Fundamental types and declarators to types ([basic.fundamental],
  [dcl.meaning], [dcl.fct], [dcl.array], [dcl.ptr], [dcl.ref]); objects and
  functions at namespace and block scope with one-scope lookup; the built-in
  operators with the standard conversions ([conv], [expr.arith.conv],
  [expr.add] ...); statements ([stmt]); integral constant expressions for
  array bounds and `case`; enumerations and C-like structs
  ([dcl.enum], [class.prop] standard-layout); `extern "C"` ([dcl.link]).
- Lowering, mangling of functions over these types, the driver (the error
  in `driver/job.cpp` goes), `-c` and `-S` on all three targets.
- Unsupported: everything with a class member function, a template or an
  overload set of more than one.
- Gate: C5.1 and C5.2 run; type, constant, layout and symbols claims 0 wrong
  on those files.
- S1 goes in increments, each with its own gate on C3 and C4: S1a
  declarations and their types at global namespace scope; S1b initializers
  and integral constant expressions; S1c function bodies, statements and
  block scope; S1d lowering to `struct ast` and mangling; S1e the driver,
  then C5.
- Status (2026-11-17, S1a done): declarators to types and entities at
  global namespace scope, no initializers.  The fundamental types of
  Table 14 (`dcl/dcl.type.simple.cpp`), the decl-specifier rules of
  [dcl.spec], [dcl.stc] and [dcl.typedef], typedef-names; declarators
  ([dcl.meaning], `dcl/dcl.meaning.cpp`: pointers, references with
  collapsing, arrays with an integer-literal bound, functions with the
  parameter adjustments of [dcl.fct]/5); redeclarations and linkage
  ([basic.link], [basic.def], [basic.def.odr], [dcl.array]/8's bound taken
  from an earlier declaration).  clang's extensions are diagnostics of
  their own (`sema/diagnostic-messages.inc`: duplicate decl-specifiers,
  zero-length arrays, a declaration that declares nothing, Microsoft's
  `static` after a non-static declaration), errors only under
  `-pedantic-errors` as clang's are.  A name declared by a declaration that
  is unsupported or not parsed is held (`hold_names`), so its later
  declarations are unsupported rather than taken as the first.
  `cxxsema -declaration-claims` writes the declared types in the parser's
  claim format and `cxx-sema.py` has clang check them;
  `src/test/cxx/sema/gram/` has the cases.  C3: 4187 runs, no crash; 2137
  declarations as clang (470 at S0), 0 we reject and clang accepts, 49 we
  accept and clang rejects (rules not written yet), 52396 unsupported, 9121
  not parsed.  C4: 5460 declared types verified by clang, 0 refuted, 54 as
  one of an overload set's, 117 in declarations clang rejects, 29 not
  checkable.  Rule coverage: basic 4/112, dcl 26/238.  The work list:
  [namespace.def] 13.1k, [class.pre] 8.4k, [temp.pre] 6.9k,
  [dcl.fct.def.general] 4.9k, [dcl.pre] 3.8k (attributes),
  [basic.lookup.unqual] 2.8k.

### S2. Names and entities in full

- [basic.scope], [basic.lookup] (unqualified, qualified, argument-dependent,
  elaborated, using-directives), [basic.link], [basic.def.odr],
  [namespace.def], [namespace.udecl], [dcl.typedef], redeclarations and
  their consistency, [class.name], [class.mem] names, [class.member.lookup].
- Sema replaces `declared_names` for the parser.
- Gate: lookup claims 0 wrong on clang's tests and on the libstdc++ headers
  outside templates; C7 (V4a no worse).

### S3. Overloading and calls

- [over.match] and its contexts, [over.ics] ranking, [over.best.ics],
  [over.over], default arguments ([dcl.fct.default]), [over.oper] with
  [over.built], [over.literal]; function-to-pointer and the reference
  bindings of [dcl.init.ref].
- Gate: overload claims 0 wrong; CXX/over.

### S4. Classes and the object model (parallel with S3)

- [class.mem], [class.mfct], `this`, static members, [class.bit],
  [class.union], [class.derived], [class.virtual], [class.abstract],
  [class.access]; special member functions and when they are implicitly
  declared, defaulted, deleted, trivial ([special], [class.ctor],
  [class.copy], [class.dtor]); [class.init], [class.base.init],
  [class.cdtor]; initialization ([dcl.init], aggregates, lists);
  temporaries and copy elision ([class.temporary], [class.copy.elision]);
  [expr.new], [expr.delete]; [basic.start] dynamic initialization.
- `abi/itanium/`: layout, vtables and VTTs, thunks, constructor and
  destructor variants, guards, class arguments, mangling of everything
  nameable so far.
- Gate: layout, vtable and symbols claims 0 wrong; C5.3 without exceptions;
  run tests.

### S5. Conversions by user code, lambdas, the rest of [expr]

- [class.conv], conversion functions in overload resolution, explicit
  casts ([expr.static.cast], [expr.const.cast], [expr.reinterpret.cast],
  [expr.cast]), pointers to members, non-generic lambdas
  ([expr.prim.lambda]), range-based `for`, structured bindings.
- Gate: type claims 0 wrong on CXX/expr; `unsupported` 0 on SingleSource's
  template-free files.

### S6. Constant evaluation and templates

- S6a [expr.const], [dcl.constexpr], constant initialization: an
  interpreter over the semantic tree, with the limits of [implimits].
  (Parallel with S3-S5; S1's integral folding is its seed.)
- S6b [temp.param], [temp.arg], [temp.names], [temp.type], [temp.res] with
  two-phase lookup and [temp.dep], [temp.inst], [temp.spec], [temp.expl.spec],
  [temp.class.spec] with partial ordering, [temp.deduct] in all its contexts
  and SFINAE, [temp.variadic], [temp.alias], variable templates, generic
  lambdas, fold expressions, class template argument deduction.
- Instantiation recorded as 3.5 says, bodies only on use.
- Gate: each libstdc++ header alone with no `unsupported`; instantiation and
  type claims 0 wrong; the first `libcxx/test/std` numbers.

### S7. Exceptions and RTTI

- Front end: [except.throw], [except.handle], [except.ctor], [except.spec],
  `typeid`, `dynamic_cast`, the RTTI objects.
- Back end: landing pads and `.gcc_except_table` for arm64 and x86-64 in
  the walker and in VIR's lowerings; `bench/vcc-cfi.py` extended.
- Until then `-fno-exceptions` is the only mode: `throw` and `try` are
  errors as clang's.
- Gate: C5.3 with exceptions across compilers; C5.4.

### S8. Real code

- libstdc++ and libc++, json's 71 TUs passing their tests, the extensions
  and builtins they need (`ext/`), then PLAN-cxx's measurements: time
  against clang, the instantiation cache (X5), Skia.
- Diagnostics in vcc's system; tuning starts from `cxxsema -stats`.

### S9. The rest of C++20

- Concepts ([temp.constr], [expr.prim.req]), `<=>` and rewritten candidates
  ([over.match.oper]/3.4, [class.compare]), `consteval`, `constinit`,
  coroutines ([dcl.fct.def.coroutine]), modules, designated initializers,
  `char8_t`; default `-std` moves to what clang's is.

Order and what can overlap:

```
P9 -> S0 -> S1 -> S2 -> S3 --------> S5 -> S6b -> S8 -> S9
                    \-> S4 ---------/      /
                    \-> S6a --------------/
              S7 back end (from S1) ......... S7 front end (after S4)
```

## 7. Decisions to confirm

1. **One component for lookup and analysis**, entered per declaration from
   the parser through `declared_names`' interface; no separate resolver.
2. **A semantic program beside the syntax tree** (entities, types, typed
   tree), not annotations on syntax nodes and not `struct ast` built
   directly: constant evaluation and instantiation need the C++ form.
3. **Lowering to C's `struct ast`**, extended where needed, rather than a
   second input to the walker.
4. **The Itanium ABI vendored and cited like the standard**, its code in
   `abi/itanium/`.
5. **`-std=gnu++17` and `-fno-exceptions` first**; C++20's additions (S9)
   and exceptions (S7) after templates work.
6. **Claims checked by clang** as the main validation, clang's `test/CXX`
   as the conformance suite by paragraph, `libcxx/test/std` as the run
   corpus.
7. **`unsupported` as scaffolding**, measured to zero per corpus.
8. **Step order**: a C-subset skeleton, then names, then overloading and
   classes in parallel, templates after both, rather than the standard's
   clause order.
9. **Reuse of the C front end's folding, layout and attributes through
   shared interfaces**, not copies.
