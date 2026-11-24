# From the C++ syntax tree to code: semantics and lowering

Status: plan, for review before any code (2026-11-17).  Continues
`PLAN-cxx-parse.md` after P9; the conventions of `PLAN-cxx-spec.md` hold
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
- spec-driven like the standard (decided 2026-11-17): the code of a
  section is written against it and cites it, and its grammar is checked.
  The mangling grammar's notation (not its prose) is committed as
  `docs/itanium-abi-grammar.tsv` (section, nonterminal, alternative: 292
  alternatives in 28 sections), which `bench/itanium-abi.py` extracts
  with the index.  The mangler has one function per nonterminal, whose
  comment quotes the productions it writes after the section's citation
  (`[abi:mangle.name]` then `<unscoped-name> ::= <unqualified-name>`);
  `cxx-spec-refs.py` checks each quoted alternative is that section's and
  counts, per section, the alternatives quoted of all (C1), and
  `--abi-missing LABEL` lists the rest.  A rule in the prose (what is not
  mangled, when a substitution is made) is cited by its section and said
  in the comment's own words;
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
    semantic-tree.cpp      typed expressions and statements
    instantiation.cpp      the unit of instantiation and its record (3.5)
    unsupported.cpp        the scaffolding result and its counters
  basic/name-lookup.cpp, translation-unit.cpp, ...
  expression/standard-conversions.cpp, binary-expression.cpp, ...
                           beside the parser's function for the same construct
  declaration/initializer.cpp, declarator-meaning.cpp, ...
  class/, overload/, template/, exception/
  abi/itanium/mangling.cpp, layout.cpp, vtable.cpp, rtti.cpp, guard.cpp, calls.cpp
  lower/                   semantic program -> struct ast; compile.cpp the driver's entry
  dump/                    the developer outputs, the claims of section 4:
                           vpp -x c++ -fsyntax-only -fcxx-dump=WHAT (dump.h)
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
- The ABI the same way (section 2): every `[abi:label]` exists, every
  quoted production of the mangling grammar is the cited section's, and
  the coverage per section is reported.
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
| layout | size, data size, alignment, field and base offsets of every class | `-fdump-record-layouts-complete`, the record of the class's name (bases, bit-fields and the data size are in it, private members too; nothing has to be nameable at the end of the TU) |
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
  Table 14 (`declaration/simple-type-specifier.cpp`), the decl-specifier rules of
  [dcl.spec], [dcl.stc] and [dcl.typedef], typedef-names; declarators
  ([dcl.meaning], `declaration/declarator-meaning.cpp`: pointers, references with
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
- Status (2026-11-17, S1b done): initializers and constant expressions at
  global namespace scope.  Expressions are typed with their value category
  (`expression/expression.cpp` dispatches; literals, names, the built-in unary,
  multiplicative to comma operators, casts to arithmetic and pointer types,
  functional casts, subscripts, `sizeof`/`alignof`), with the standard
  conversions and usual arithmetic conversions (`expression/standard-conversions.cpp`) and
  constant evaluation in three states (constant, not constant, unknown:
  an ill-formed verdict rests only on the first two; long double and
  floating-point under an FENV/`float_control`/`clang fp` pragma are
  unknown).  Array bounds are converted constant expressions; initializers
  are copy, direct and list ([dcl.init], [dcl.init.list] with narrowing,
  [dcl.init.aggr] for arrays without brace elision, [dcl.init.string],
  [dcl.init.ref]); `constexpr` variables, and const integral variables are
  usable in constant expressions.  New extension diagnostics: narrowing
  (an error by default, as clang's) and a string literal to `char *`.
  `cxxsema -type-claims` writes each expression's type and, from C++11, a
  constant's value, which `cxx-sema.py` has clang check with
  `static_assert((E) == V)`.  C3: 4189 runs, no crash; 2723 declarations
  as clang, 0 we reject and clang accepts, 59 we accept and clang rejects,
  51950 unsupported, 9072 not parsed.  C4: 6474 declared types verified, 0
  refuted; 1098 expression types verified, 0 refuted, 118 not checkable;
  757 constants verified, 0 refuted.  Rule coverage: basic 4/112, dcl
  38/238, expr 18/152.
- Status (2026-11-17, S1c done): function definitions with a compound
  body.  Block scopes (`open_scope`, `lookup` outward; parameters in their
  own scope, [basic.scope.param]/1 and [basic.scope.block]/3 against
  redeclaring them; automatic variables are not constant), every statement
  of [stmt] but range-`for`, `if constexpr` and computed `goto`: labels
  ([stmt.label]), `case` constants converted to the condition's promoted
  type with duplicates and narrowing ([stmt.switch]), `break`/`continue`
  placement, `goto` and `switch` jumps past an initialized declaration
  ([stmt.dcl]/3), conditions ([stmt.pre]), `return` ([stmt.return]/2 with
  clang's `-Wreturn-mismatch`, an error by default).  Calls to one
  function, by name or through a pointer: argument count
  ([over.match.viable]/2), copy-initialized parameters, void through an
  ellipsis ([expr.call]/12).  Unsupported for now: a function or `extern`
  declared at block scope, a reference returned from a local by name
  (implicit move, [class.copy.elision]/3), C++26's `_`.  A name held by an
  unsupported declaration, also inside `extern "C" { }` and in templates
  or using-declarations, makes the name's entity unknown (overloads the
  analysis did not see).  `-Wno-error=X` demotes a warning that is an
  error by default (`-Wno-error=narrowing`; `bench/clangdiag.py` now loads
  clang's anonymous group aliases).  Type claims inside a body are placed
  after the enclosing block statement and only at that scope; the
  harnesses leave claims in files with line markers unchecked, compare
  nothing after clang's fatal error, and give clang `-fno-signed-char` as
  the driver does for aarch64 Linux.  `src/test/cxx/sema/gram/stmt.*.cpp`
  has the cases.  C3: 4191 runs, no crash; 3515 declarations as clang, 0
  we reject and clang accepts, 67 we accept and clang rejects, 51133
  unsupported, 8952 not parsed, 189 after a fatal error.  C4: 9110
  declared types verified, 0 refuted; 16222 expression types verified, 0
  refuted, 1262 not checkable; 5065 constants verified, 0 refuted.
- Status (2026-11-17, S1d in progress): the semantic tree (3.2) and the
  ABI grammar are in.  Analysis builds a `semantic_node` for every
  expression, initializer and statement it accepts
  (`sema/semantic-tree.cpp`, `semantic_kind` in `sema.h`), each implicit
  conversion a `conversion` node of its own (lvalue-to-rvalue,
  array-to-pointer, integral, boolean, null pointer, ...), a variadic
  argument's default promotion included.  A function's entity has its
  `function_body` (outermost block, parameter entities), a variable's its
  initializer; a block-scope declaration is a `declaration_statement` per
  variable, a condition that declares a `condition_declaration`.
  `cxxsema -dump-tree` prints the trees; `cxxsema -check-tree`
  (`src/bin/cxxsema/tree-check.h`) checks that every well-formed
  definition has a complete tree and every recorded expression a node of
  its type and value category, with conditions bool prvalues and operands
  converted; `cxx-sema.py` runs it on every test (0 problems, C3 and C4
  unchanged).  The Itanium ABI's grammar is data
  (`docs/itanium-abi-grammar.tsv`) and `cxx-spec-refs.py` checks quoted
  productions.  The mangler (`abi/itanium/mangling.cpp`) has one function
  per nonterminal of the S1 subset (<mangled-name>, <encoding>, <name>,
  <local-name> with discriminators, <bare-function-type>, <type> with
  builtin, qualified, pointer, reference, function and array types,
  <substitution>), each quoting its alternatives after an `[abi:label]`:
  51/292 of the ABI's alternatives quoted, 0 wrong.  Global variables,
  `main` and (later) C-linkage names are not mangled; clang's and GCC's
  `L` for internal linkage is cited as `[impl.gnu]`.  `cxxsema -symbols`
  lists each symbol, and `cxx-sema.py`'s symbols claim compiles every
  file clang accepts at -O0 and finds each defined symbol in its object
  (`llvm-nm`): 681 verified, 0 refuted, 71 not emitted (unused internal
  or inline entities).
- Status (2026-11-17, S1d lowering): the semantic tree reaches the code
  generator.  C's module has a public builder
  (`include/frontend/c/ast-builder.h`, `frontend/c/ast-builder.cpp`) that
  makes types, symbols, nodes, strings, labels and cases through C's own
  constructors and runs `codegen_unit_emit`;
  `frontend/cplusplus/lower/lower.cpp` maps each semantic node to the C
  node of the same operation.  A reference is a pointer (a name of one is
  its target dereferenced, a binding takes the address, a prvalue's
  temporary is a compound literal of the block); comparisons, `!` and
  `&&`/`||` are C's int converted to bool; a condition that declares and
  an init-statement are blocks around the statement (`while` with a
  declaration: a `for` whose body declares, tests and breaks); a glvalue
  comma or conditional selects an address; static locals carry the
  mangled name as their assembler name; inline functions are LINKONCE and
  weak (no COMDAT group yet).  Dynamic initialization, class and
  enumeration types, long double constants and an assignment used as an
  object fail the translation unit with a reason.  The mangler now gives
  an internal-linkage variable of the global namespace clang's `_ZL`
  name (`[impl.clang]`): symbols 691 verified, 0 refuted, 61 not emitted.
  `cxxsema -S [-O0..3]` prints arm64 assembly; `bench/cxx-run.py`
  compiles `src/test/cxx/run` (statements, references, operators and
  conversions, data and linkage) with it and with clang++ -O2, runs both
  under qemu: 16/16 runs as clang's at -O0 to -O3, and the -O0 objects
  define clang -O0's symbols with its bindings (in `ci/premerge.sh`).
  `cxx-sema.py` otherwise unchanged (C3, C4, 0 tree problems).
  The lowering claim: on every run clang accepts, `cxx-sema.py` has
  `cxxsema -S -O0` assemble (clang -cc1as) and checks that the object
  defines exactly clang -O0's functions and variables: 65 files lowered,
  0 refuted; 1340 not lowered, by reason: a declaration not well-formed
  for us 1307, an error of our parser or analysis 30, dynamic
  initialization 3.  A static local is emitted with its function whether
  referred to or not, as clang does (`ast_builder_symbol_keep`).  What
  lowers is bounded by S2-S4 (namespaces, classes, templates), not by the
  lowering.
- Status (2026-11-17, S1e): the driver compiles C++.  `driver/job.cpp`
  calls `cplusplus_compile` (`frontend/cplusplus/lower/compile.cpp`, a C
  interface in `include/frontend/cplusplus/compile.h`) where it called C's
  front end; the error for C++ is gone (a C-hosted vcc, which has no C++
  front end, still gives one).  The C++ preprocessor takes the driver's
  resolved predefines (clang++'s for the target, -D, -U, -include) as its
  command line with its own predefined macros off, less the `__cplusplus`
  and `__STDC*` lines it defines itself; the search path keeps its quote,
  angled and system parts; char, wchar_t and long double follow the
  target.  -fsyntax-only analyses, -S and -c lower and emit with the
  options C has; a construct not handled is "C++ code generation does not
  handle WHAT yet" at its location.  vcc, vpp and vccd link the cplusplus
  module (Makefile `CXX_OBJ`).  `bench/cxx-run.py` now compiles with `vpp
  -c` (vcc's object writer, not clang's assembler): 16/16 run as clang's
  at -O0 to -O3, -O0 symbols clang's.  arm64 Darwin: the four programs run
  natively as clang++'s at -O0 to -O3 (by hand); x86-64 Linux compiles,
  not run here.  The C suites as C++ (vcc, gcc-torture execute,
  tcc-tests2, c90: 2199 programs, `vpp -x c++ -O2 -c`, run under qemu
  against clang++'s build): 59 run as clang's, 0 differ; 79 more compile
  but reference `abort()` with C++ linkage, which clang -O2 deletes as
  unreachable; 1505 stop at a declaration the analysis does not handle.
  What those need, by files: unqualified lookup ([basic.lookup.unqual];
  the compiler's `__builtin_*` among the names) 738, classes
  ([class.pre]) 448, files the parser does not finish 369, elaborated
  type specifiers 256, GNU extensions 169.
- Status (2026-11-17, C5.1's harness): `bench/cxx-c-suites.py` compiles
  the programs of the C suites vcc-ctests.py runs (gcc torture, tcc
  tests2, inlining, vcc's; their skips and flags, a C `-std=` dropped) as
  C++20 with the driver at -O0 and -O2, runs them under qemu against
  clang++ -O2's build, and checks the -O0 object's symbols against clang++
  -O0's.  `extern "C"` around a file, as C5.1 had it, is ill-formed
  (`main` "shall not be declared with a linkage-specification",
  [basic.start.main]/3), so the comparison is with clang++.  The corpus
  is what clang++ -O0 compiles and links: the run time's headers have no
  `extern "C"` until [dcl.link] is implemented, so a run-time function
  the program declares with C++ linkage (`abort()`, `printf`) is linked
  to the C one by `--defsym`, on both sides; a function that flows off
  its end (undefined in C++, [stmt.return]/2) is out.  Baseline: 2268
  programs, 1718 valid C++; at -O0 and -O2 822 runs pass
  (`bench/cxx-c-suites.passing`, a lost one is an error), 0 wrong, 0
  symbol differences; 111 rejected (56 programs, one only at -O2: C-style casts between
  pointers and integers or dropping const reported ill-formed instead of
  unsupported, 29; parser errors on GNU syntax, 12; ...); 2503 not yet,
  by what is missing: [class.pre] 1084, [dcl.fct.def.general] 354,
  declarations the parser kept as tokens 331, [basic.lookup.unqual] 240, [ext.gnu] 231.
  The run found and fixed: a static local of `main` mangled with main's
  type (`_ZZ4mainvE1r`; clang and GCC write `_ZZ4mainE1r`).  The driver's
  error now names the subclause whose rules are missing ("the rules of
  [class.pre] are not implemented yet") at the construct.
  Next: the 56 rejected programs, then S2-S4.
- Status (2026-11-17, casts): the cast notation has [expr.cast]/4's
  conversions for the fundamental types and pointers to them, in its
  order, decided before any diagnostic: the implicit ones, then a
  const_cast between similar pointers ([expr.const.cast]/3), a pointer to
  another pointer type ([expr.static.cast]/13, [expr.reinterpret.cast]/6,
  /7, /8: the same address), a pointer to an integer at least as wide
  (/4), an integer to a pointer (/5); between a pointer and a floating
  type it is ill-formed ([expr.cast]), a pointer to a narrower integer
  too (/4).  Before, a cast that was not an implicit conversion was
  reported ill-formed by [dcl.init]'s rule.  A pointer to a class stays
  unsupported (a base/derived static_cast moves the address).  The
  semantic tree has four new conversion kinds (qualification_cast,
  pointer_cast, pointer_to_integral, integral_to_pointer), lowered to C's
  casts; the reinterpreting ones are not constant expressions
  ([expr.const]/5).  cxx-c-suites: 848 runs pass (+26), 0 wrong, 53
  rejected (-58: no cast among them); cxx-run 20/20 with
  `src/test/cxx/run/casts.cpp`; cxx-sema.py: declarations as clang 3570
  (+44), symbols 707 verified, lowering 66 files, 0 refuted anywhere,
  errors of our parser or analysis among the lowering's reasons 30 -> 16.

### S2. Names and entities in full

- [basic.scope], [basic.lookup] (unqualified, qualified, argument-dependent,
  elaborated, using-directives), [basic.link], [basic.def.odr],
  [namespace.def], [namespace.udecl], [dcl.typedef], redeclarations and
  their consistency, [class.name], [class.mem] names, [class.member.lookup].
- Sema replaces `declared_names` for the parser.
- Gate: lookup claims 0 wrong on clang's tests and on the libstdc++ headers
  outside templates; C7 (V4a no worse).
- Status (2026-11-17, done outside templates and overloads): one lookup
  (`basic/name-lookup.cpp`) for unqualified names (scopes outward,
  using-directives placed at the nearest enclosing namespace of both,
  transitively; class member lookup), qualified names ([namespace.qual]
  with inline namespaces, nested-name-specifiers through typedefs),
  argument-dependent lookup in calls, elaborated type specifiers
  ([basic.lookup.elab], [basic.scope.pdecl]/7) and hiding
  ([basic.scope.hiding]).  Bindings are per scope, so a name can be bound
  in several (namespace aliases, using-declarations, block-scope extern
  declarations with [basic.link]/6,7's linkage).  Namespaces: definitions,
  extension, inline and unnamed ones, aliases, using-directives,
  using-declarations at namespace and block scope and as members naming a
  base's member; qualified definitions of namespace members and nested
  classes.  Mangling: namespace prefixes as substitutions, `St`,
  `12_GLOBAL__N_1`, internal `L`, C-linkage names in namespaces.
  Unsupported with their subclause: a set of overloads found by a
  using-declaration ([over]), inheriting constructors, names found in two
  namespaces through directives that are functions.
  cxx-sema: declarations as clang 8036 -> 24559, 13 -> 0 we reject,
  declared types, types, constants, symbols and lowering 0 refuted, files
  lowered 108 -> 206; cxx-parse unchanged (6 refuted declared types as
  before); cxx-run 116/116 (`namespaces.cpp`, `qualified-names.cpp`,
  `hiding-and-elaborated.cpp`, `using.cpp`, `block-linkage.cpp`); C5.1
  2199 -> 2206 passing, 0 wrong.  Lookup claims (`-fcxx-dump=lookup-claims`:
  each name's use and the first declaration of what it finds, against the
  declaration clang's `-ast-dump=json` refers to, followed back through its
  redeclaration chain): 9388 as clang, 0 refuted, 3332 not checkable (no
  DeclRefExpr in clang's AST, a line clang rejects, or clang's first
  declaration in one we do not analyse); [dcl.link]/7's C-linkage functions
  and variables shared across namespaces.  The developer tools cxxlex,
  cxxparse and cxxsema are gone: their outputs are `vpp -x c++
  -fsyntax-only -fcxx-dump=...`.  `declared_names` still serves the parser:
  sema runs after it, so replacing it waits for parsing and analysis per
  declaration (S5's templates need the same).

### S3. Overloading and calls

- [over.match] and its contexts, [over.ics] ranking, [over.best.ics],
  [over.over], default arguments ([dcl.fct.default]), [over.oper] with
  [over.built], [over.literal]; function-to-pointer and the reference
  bindings of [dcl.init.ref].
- Gate: overload claims 0 wrong; CXX/over.
- Status (2026-11-18, overload resolution, default arguments, deleted
  functions; operators not started): overload sets are a chain of
  functions (`entity::next_overload`, declared by `translation-unit.cpp` and
  `class-specifier.cpp` with [over.load]'s checks); `overload/` has one file per
  subclause: implicit conversion sequences (`implicit-conversion-sequence.cpp`, a class
  conversion exactly as the analysed classes have no conversion functions
  or explicit constructors), their ranking (`conversion-ranking.cpp`, with
  clang's reading of CWG2352 in /3.2.1 and /3.2.5), viability and the best
  viable function (`overload-resolution.cpp`; constructors use it too) and calls by
  name, member access included (`call-candidates.cpp`): the callee node,
  its claim and access are of the function chosen.  Default arguments
  (`declaration/default-argument.cpp`) of namespace-scope functions, merged per /4;
  a function whose defaults are not analysed or are another scope's is
  `unknown_defaults` and a call that needs them is unsupported.
  `= delete` (`declaration/deleted-definition.cpp`).  An aggregate argument is
  lowered as a compound literal.  Tests: `src/test/cxx/run/overloading.cpp`,
  `default-arguments.cpp`.  `bench/cxx-sema.py`: 26169 declarations as
  clang (S2: 24590), 0 we reject that clang accepts, 447 we accept that
  clang rejects (S2: 413), 0 refuted; lookup 10063 verified (S2: 9388),
  symbols 1458 (S2: 1400), lowering 211 files (S2: 206); CXX/over 1101 as
  clang, 0 wrong.  cxx-run 124/124; cxx-c-suites 0 wrong (its eight
  `#pragma pack` entries in the passing list fail on main too).
- Status (2026-11-18, operator functions): `overload/operator-function.cpp` names an
  operator-function-id (operator@ as an identifier number, so declaration
  and lookup are unchanged) and checks [over.oper]/7-10 and [over.inc];
  `overload/operator-candidates.cpp` forms the member, non-member and (C++20)
  rewritten and reversed operator== candidates of a unary, binary,
  compound assignment, increment or decrement operator with an operand of
  class or enumeration type, and makes the expression a call of the
  function chosen (`!` around a rewritten !=); [over.match.best]/2.8-2.9's
  tie-breaks are in `best_viable_function`.  Built-in candidates are not
  formed: none is viable for an operand of class type (no conversion
  functions), and for an enumeration operand the operator function is
  taken only when it beats every built-in candidate (all arguments Exact
  Match, a comparison's own parameter-type-list).  A declaration not
  analysed that has the keyword operator makes every operator on a class
  or enumeration unsupported from there (`operators_held`).  The mangler
  writes [abi:mangling-operator]'s codes.  Also: a braced-init-list
  returned for a class lowers as a compound literal; an lvalue reference
  returned by name is no implicitly movable entity.  Tests:
  `src/test/cxx/run/operators.cpp`.  `bench/cxx-sema.py`: 26564
  declarations as clang, 0 we reject that clang accepts, 448 we accept
  that clang rejects, 0 refuted; lookup 10128, symbols 1484, lowering 212
  files; CXX/over 1234 as clang (1101 before), 0 wrong; type claims fell
  from 33262 to 32792 (constants 13335 to 13182): operators of enumeration
  operands in translation units with an operator declaration analysis did
  not follow are no longer taken as built-in.  cxx-run 128/128,
  cxx-c-suites 0 wrong.  Left, as `not_supported`: operator=, (), [], ->,
  ->*, comma, unary &, <=> and the allocation functions (=, () and []:
  below), members defined
  outside their class, [over.ics.list] and [over.match.list], member functions'
  default arguments, static and non-static members in one set.  The parser
  takes `(a < b) + (b < a)` for a template-id (the function is not parsed).
- Status (2026-11-18, [over.over]): `overload/function-address.cpp`'s
  `target_expression` analyses an expression for a target type: an
  unqualified name of an overload set of non-member functions (in
  parentheses, after &) is the function whose type is the target's
  function type (named_entity takes the token for it).  The targets: an
  initialized object or reference, the left side of =, a function's
  parameter (an argument found by argument-dependent lookup is analysed for
  its parameter; one of an overloaded callee is not analysed), the return
  value, a cast.  A function that differs only in its exception
  specification or return type, a member function (a pointer to member)
  and a qualified name are not analysed.  Test:
  `src/test/cxx/run/overload-address.cpp`.  `bench/cxx-sema.py`: 26592
  declarations as clang, 0 wrong, 0 refuted; lookup 10161; CXX/over 1259
  as clang.  cxx-run 132/132, cxx-c-suites 0 wrong.
- Status (2026-11-18, [over.literal]): `overload/literal-operator.cpp`.  A
  literal-operator-id names the function operator""X (an identifier, so
  declaration and lookup are any function's); its declaration is checked
  against /1 (an empty string-literal), /2 (a namespace-scope function),
  /3 (the parameter-declaration-clauses, no default argument) and /6 (no C
  language linkage); it mangles as `li <source-name>`.  Suffixes without a leading underscore ([usrlit.suffix]/1, the standard
  library's `operator""h`, `""sv`) are accepted, as clang warns only.  A
  user-defined-literal ([lex.ext]) is a call of the operator unqualified
  lookup finds: an integer literal's with parameter unsigned long long
  (/3), a character literal's with the type of ch (/6), a string literal's
  as (str, len) by overload resolution (/5).  Not analysed: literal
  operator templates, raw literal operators called for numbers, floating
  literals, suffixes without `_` ([usrlit.suffix]: the standard
  library's).  Test: `src/test/cxx/run/user-defined-literals.cpp`.
  `bench/cxx-sema.py`: 26683 declarations as clang, 0 we reject that
  clang accepts, 448 we accept that clang rejects, 0 refuted; symbols
  1487.  cxx-run 136/136, cxx-c-suites 0 wrong.
- Status (2026-11-18, operator=, () and []): [over.ass], [over.call] and
  [over.sub] in `overload/operator-function.cpp` (non-static members alone; static
  operator() and operator[], C++23's, and C++23's operator[] of several
  parameters not analysed) and `operator-candidates.cpp` (member candidates
  alone, Table 15).  a = b with a user-declared operator= is overload
  resolution among them when one is a copy assignment operator
  ([class.copy.assign]/1; otherwise the implicit one would be a candidate:
  not analysed); the built-in class assignment is kept for classes whose
  subobjects declare none (`assigned_bitwise`).  A user-declared copy
  assignment makes a class no POD for layout ([abi:pod]).  Also: two
  user-defined conversion sequences ranked alike are not_supported, not an
  ambiguity ([over.ics.rank]/3.3); && before C++11 is clang's extension.
  Test: `src/test/cxx/run/operators-member.cpp`.  `bench/cxx-sema.py`:
  27208 declarations as clang, 0 we reject that clang accepts, 448 we
  accept that clang rejects, 0 refuted; lookup 10591, symbols 1496,
  lowering 216.  cxx-run 140/140, cxx-c-suites 0 wrong.
- Status (2026-11-18, comma, ->*, unary & and ->): the comma, ->* and
  unary & operator functions go through `overloaded_operator` like the
  other binary and unary ones (the built-in ->* and .*, pointers to
  members, are not analysed); [over.ref]'s x->m applies operator-> while
  the result is of class type (`member-access.cpp`; a chain of 16, or a cycle,
  is not analysed).  Mangled cm, pm, ad and pt.  Test:
  `src/test/cxx/run/operators-rest.cpp`.  `bench/cxx-sema.py`: 27290
  declarations as clang, 0 wrong, 448 we accept that clang rejects, 0
  refuted; lookup 10786.  cxx-run 144/144, cxx-c-suites 0 wrong.
- Status (2026-11-18, <=>): operator<=> is declared (C++20), mangled ss,
  and called for x <=> y of class or enumeration operands; the relational
  operators have its rewritten candidates, x <=> y and the synthesized
  y <=> x ([over.match.oper]/3.4), interpreted as (x <=> y) @ 0 or
  0 @ (y <=> x) (/8) when its result is of arithmetic type.  Not analysed:
  the built-in <=> and the comparison category types of <compare> (so
  neither a reversed x <=> y nor std::strong_ordering results), and an
  enumeration's rewritten candidates.  Test:
  `src/test/cxx/run/three-way.cpp`.  `bench/cxx-sema.py`: 27309
  declarations as clang, 0 wrong, 448 we accept that clang rejects, 0
  refuted.  cxx-run 148/148, cxx-c-suites 0 wrong.  Left: the allocation
  functions.
- Status (2026-11-18, members defined outside their class): a declarator-id
  X::operator@ (`declarator-meaning.cpp`) defines the member operator function
  declared in X.  Test: `src/test/cxx/run/operators-out-of-class.cpp`.
  `bench/cxx-sema.py`: 27310 declarations as clang, 0 wrong, 0 refuted
  (clang's tests define them mostly in templates).  cxx-run 152/152,
  cxx-c-suites 0 wrong.
- Status (2026-11-18, member functions' default arguments): a complete-class
  context ([class.mem]/6), analysed when the class is complete, before the
  member function bodies (`member_default_arguments`, default-argument.cpp);
  an operator function's other than operator()'s is ill-formed
  ([over.oper]/10).  Not analysed: a default argument that may name a
  non-static member or uses this (/8, /9), and default
  arguments added by a definition outside the class (/6: the member's calls
  then do not know its defaults).  Test:
  `src/test/cxx/run/member-default-arguments.cpp`.  `bench/cxx-sema.py`:
  27330 declarations as clang, 0 wrong, 448 we accept that clang rejects,
  0 refuted.  cxx-run 156/156, cxx-c-suites 0 wrong.
- Status (2026-11-18, constructors' default arguments): deferred like
  other member functions' (`defer_default_arguments`) and taken by
  `construct` for the parameters a call leaves; a constructor whose
  default arguments make it a default constructor is one.  Not analysed:
  a constructor of first parameter X, X& or X&& whose other parameters
  have default arguments (a copy or move constructor, [class.copy.ctor]/1),
  and an implicitly-defined special member that would be ill-formed
  (deleted, [class.default.ctor]/2).  Test:
  `src/test/cxx/run/constructor-default-arguments.cpp`.
  `bench/cxx-sema.py`: 27355 declarations as clang, 0 wrong, 448 we accept
  that clang rejects, 0 refuted.  cxx-run 160/160, cxx-c-suites 0 wrong.
- Status (2026-11-18, [over.ics.list]): a braced-init-list argument of an
  overloaded call with one element that is no list, for parameters of
  scalar type, has the element's conversion sequence (/10.1); the call
  then list-initializes the parameter (`resolve_call`, `list_clauses`
  shared with `list_initialization`).  Not analysed: other lists (/10.2
  {}), and class (/7, /8), std::initializer_list (/5), array (/6) and
  reference (/9) parameters.  Test: `src/test/cxx/run/overload-braced.cpp`.
  `bench/cxx-sema.py` unchanged (27355 as clang, 0 wrong: clang's tests
  pass lists to classes).  cxx-run 164/164, cxx-c-suites 0 wrong.
- Status (2026-11-18, new and delete): the allocation and deallocation
  functions ([basic.stc.dynamic]): declarations of operator new, new[],
  delete and delete[] with their rules (/allocation/1, /deallocation/1-3,
  [class.free]/1: a member one is static), the implicit global ones of /2
  (the sized operator delete from C++14, N3778; no std::align_val_t forms),
  mangled nw, na, dl, da.  new-expressions (`expression/new-expression.cpp`):
  new-type-id or ( type-id ), one array bound (its size checked for
  overflow at run time as clang does, SIZE_MAX when it does not fit),
  placement arguments, class-specific allocation functions found before the
  global ones unless ::new (/11), default-, value- and direct-initialization
  and braced-init-lists of one object; the initialization skipped for a null
  pointer from an allocation function that is not an implicit one (/20).
  delete-expressions (`expression/delete-expression.cpp`): the destructor, then the usual
  deallocation function of [expr.delete]/10 (the sized global one for one
  object, as clang), nothing for a null pointer.  Semantic nodes
  new_object, allocated, array_size, delete_object, lowered as statement
  expressions.  Not analysed: arrays of classes with constructors or
  destructors (cookies), an array with a new-initializer, more than one
  bound, a placeholder type, new-extended alignment, value-initialization
  of a class, a destroying operator delete, an operand of class type, the
  deallocation when the initialization throws (/25); a new-expression when
  an operator new or delete spelled in the translation unit was not
  analysed and lookup may miss it.  Test: `src/test/cxx/run/new-delete.cpp`
  (it does not count calls of the replaceable functions: clang -O2 omits
  them, /12).  `bench/cxx-sema.py`: 28046 declarations as clang, 0 wrong,
  449 we accept that clang rejects, 0 refuted.  cxx-run 168/168,
  cxx-c-suites 0 wrong (its `#pragma pack` programs are not yet, as at the
  previous commit).

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
- Status (2026-11-17): started.  Classes without bases, constructors or
  destructors: data members with the Itanium layout (and GNU `aligned`),
  member functions in and outside the class, `this`, const member
  functions, static member functions, access, `.`/`->`, aggregate
  initialization, the trivial copy and assignment, classes by value;
  mangling of members (`class/class-specifier.cpp`, `expression/member-access.cpp`; lowered
  to C structs whose layout is checked against sema's).  Unsupported with
  their subclause: unions, nested and local classes, bases, constructors,
  destructors, static data members, reference members, brace elision.
  `src/test/cxx/run/classes.cpp`; C5.1 848 -> 915 passing, 0 wrong.
  Then user-declared constructors and destructors (`class/constructor.cpp`):
  in and outside the class, mem-initializers in member order, overload
  resolution among constructors for exact and standard conversions,
  `T x;`, `T x(a, b)`, `T x = a`, `T x{}`, `T(args)`; destructors called
  at block end, return, break and continue; C1/C2 and D1/D2 symbols as
  clang's.  Unsupported: temporaries with destructors, members of class
  type with constructors or destructors, classes passed by address, gotos
  past live objects, static objects with destructors.
  `src/test/cxx/run/constructors.cpp`.  LLVM's SingleSource/Regression/C++
  still needs [dcl.link] (`extern "C"` in the run-time headers),
  [class.derived], dynamic initialization and [dcl.enum].
  Then [dcl.link] (`declaration/linkage-specification.cpp`: extern "C" and "C++", nested, the
  brace-less form as extern; C language linkage names unmangled) with
  extern "C" in src/test/rt's headers, and dynamic initialization of
  namespace-scope variables ([basic.start.dynamic]) as clang's:
  `__cxx_global_var_init[.N]` per variable, called in definition order
  by `_GLOBAL__sub_I_<file>` from .init_array; address constants
  ([expr.const]/11) stay static data.  C5.1 915 -> 950.  First LLVM
  import: `src/test/cxx/run/llvm-global_type.cpp`; the others need
  `__builtin_va_list` (the run time's stdio.h), [class.derived],
  [dcl.enum], [class.local], __cxa_atexit.  Open: gcc/pr58277-2's static
  referenced only from unreachable code is dropped by vcc's -O0 code
  generator, kept by clang (symbols).
  Then `__builtin_va_list` (a predeclared typedef of AAPCS64's
  std::__va_list: 32 bytes, mangled St9__va_list, lowered to C's
  va_list; copied only from a va_list), so programs include the run
  time's <stdio.h>: C5.1 950 -> 1096.  [dcl.array]/8's earlier bound is
  the one an initializer fills; a namespace-scope initializer analysis
  cannot fold and that needs nothing of the running program (clang folds
  an out-of-range shift) is not lowered as dynamic initialization.
  LLVM import: `llvm-2003-05-14-array-init.cpp`.
  Then non-virtual base classes (`class/base-clause.cpp`): a base is an
  unnamed member subobject (`entity::base_subobject`; a non-empty one is
  also a field), laid out as [abi:layout] (empty bases at offset 0, a
  non-POD base's tail padding reused: lowering then refuses the C
  struct); member lookup through bases ([class.member.lookup], ambiguity
  unsupported), [class.access.base] and [class.protected]; [conv.ptr]/3
  and references to a base ([dcl.init.ref], now for classes generally:
  copy constructors with `const T&` work), the derived-to-base Conversion
  in overload resolution; [class.base.init] constructs bases and members
  of class type (named or default), and a destructor destroys them after
  its body ([class.dtor]/13, a `return` in it too).  Unsupported: virtual
  bases, a pointer conversion to a base at a nonzero offset (the null
  check), an implicitly-defined default constructor, destructor or copy
  constructor that has to call a subobject's, arrays of such classes,
  aggregates with an empty base.  An empty class is lowered with one char
  of padding (Itanium passes an empty class like clang's C does not:
  not checked across compilers yet).  `src/test/cxx/run/derived.cpp`,
  LLVM's `llvm-2003-06-08-BaseType.cpp`; C5.1 unchanged at 1096.  The
  rest of SingleSource/Regression/C++ needs [dcl.mptr], virtual
  functions, [dcl.enum], [class.local], bit-fields, classes passed by
  address, __cxa_atexit and exceptions.
  Then the implicitly-declared special members that are not trivial
  ([class.default.ctor]/1, [class.copy.ctor]/6, [class.dtor]/4): entities
  like user-declared ones (`entity::implicit`, inline: C2/D2 symbols),
  declared at the class's completion, defined after its member function
  bodies (`implicit_definitions`): the default constructor constructs the
  bases and members, the copy constructor copies each (its own copy
  constructor, else the bytes), the destructor destroys them.  A block's
  object of a class with a destructor but no constructor is now lowered.
  `src/test/cxx/run/implicit-members.cpp`; C5.1 unchanged at 1096.
  Unsupported still: arrays of such classes, such classes by value.
  Then classes non-trivial for the purposes of calls as parameters
  ([abi:calls]): lowered as a pointer parameter (the callee's uses are
  the object at it); the caller constructs a temporary (by the copy
  constructor, or a prvalue's constructor in place), passes its address
  and destroys it at the end of the full-expression (expression
  statements, returns, declarations; `full_expression`).  Calls between
  vcc and clang objects agree both ways (checked by hand on a
  caller/callee pair).  Unsupported: such an argument in a conditional
  operand or outside a statement, and returning such a class (Itanium
  returns it through x8 whatever its size: C's code generators need a
  record flag for that).  `src/test/cxx/run/by-address.cpp`, LLVM's
  `llvm-2003-09-29-NonPODsByValue.cpp`; C5.1 unchanged at 1096.
  Then returning them: the C record of such a class is marked
  `AST_RECORD_FLAG_CALLS_IN_MEMORY`, which arm64's direct emitter returns
  through x8 and x86-64's classifies as memory whatever the size (VIR
  gives up on record results and falls back to the direct emitter).  A
  prvalue result initializes the object it is the initializer of (a
  variable, a return value, an argument's temporary); any other use
  materializes a temporary destroyed at the end of the full-expression;
  when every return names one automatic variable, it is the result
  object (clang's NRVO).  Results agree with clang's in both directions at
  -O0 and -O2 (by hand, a split caller/callee pair).  Unsupported: such a
  result in a conditional operand.  `src/test/cxx/run/returns.cpp`.
  cxx-sema.py against main: the 60 declarations we rejected that clang
  accepts are back to 0, and nothing is refuted.  Now unsupported: an
  implicit special member whose subobject's constructor or destructor is
  missing or inaccessible, `&` of a class prvalue (clang's
  -Waddress-of-temporary), any non-class child of a decl-specifier-seq
  before the class it defines (`} __attribute__((packed))`; the class name
  is held), a second `_` member (C++26), MS `sealed`/`abstract`, and a
  GNU attribute in a declarator other than `aligned`, `packed` (a member's
  alignment is 1, lowered with C's per-field packed), `unused` and `used`
  (not on an internal name): one without arguments was ignored before,
  which let `packed` members pass with the wrong layout.  A variable's
  `aligned` is unsupported (gcc/memcpy-bi leaves the passing list).  A
  variable whose type is an unnamed class with no typedef name for
  linkage has internal linkage under its plain name, as in clang.  The
  harness's clang -O0 symbol list uses the driver's
  `-mconstructor-aliases`.  C5.1 1094 passing, 0 wrong.
  Then enumerations ([dcl.enum], `declaration/enum-specifier.cpp`): scoped and
  unscoped, fixed underlying types, clang's underlying and promoted types
  for the others, enumerators as constants bound in the enclosing scope
  (and in the enumeration's, for `E::e`), in any scope and in classes;
  [conv.prom]/4-5 for the operands of the built-in operators, switch,
  case, subscripts and ellipsis arguments; [conv.integral] to integers
  only implicitly; casts both ways, `static_cast` between arithmetic and
  enumeration types; mangled as a <class-enum-type>; lowered as the
  underlying type.  `typedef struct s {...} s;` and `typedef enum e {...}
  e;` ([dcl.typedef]/3).  Then local and nested classes with data members
  only ([class.local], [class.nest]; member functions in them unsupported:
  they need a <local-name> or a deeper <nested-name>), class names found
  by elaborated-type-specifiers in block and class scope
  ([basic.lookup.elab]), `Outer::Inner` as a type, and static data members
  of classes of namespace scope ([class.static.data]: declared in the
  class, defined outside it, `S::m`).  Known laxness: a `case` of an
  integer in a switch on a scoped enumeration is accepted.
  `src/test/cxx/run/enums.cpp`, `local-classes.cpp`, `static-members.cpp`;
  C5.1 1206 passing, 1204 after the merge with main (which made a
  variable's `aligned` unsupported), 0 wrong.  bench/cxx-sema.py not run
  (needs clang 23).
  Then GCC's attributes ([ext:gnu_attributes], `ext/gnu/attribute.cpp`)
  wherever GCC takes them: before a declaration (the 251 function
  definitions `__attribute__((noinline))` sent to the constructor path),
  among the decl-specifiers, after a declarator and after a function's
  parameters.  noinline (noipa), always_inline, noreturn, weak, used,
  section("...") and aligned on functions and variables (automatic ones
  too) lower to the C symbol's flags, section and alignment; packed on a
  non-static data member gives it alignment 1 in sema's layout and C's
  (`ast_builder_type_record_complete`'s packed[]); the attributes that
  change no code (unused, noclone, const, pure, format, ...) are
  accepted.  Member functions' and static data members' symbol attributes
  stay unsupported.  Fixed on the way: `ast_builder_integer` sign-extended
  an unsigned 64-bit constant to C's 128 bits (`(unsigned __int128)
  0xfedcba9876543210ULL` was negative).  `src/test/cxx/run/gnu-attributes.cpp`;
  cxx-c-suites reads the C suite's vcc-xfail (autoinit_zero is vcc's
  arm64 gap, not C++'s).  C5.1 1206 -> 1563, 0 wrong; not yet by what is
  missing: [basic.lookup.unqual] 666, declarations kept as tokens 441,
  [ext.gnu] 206, [class.mem] 190, [class.union] 98.
  Then GCC's library builtins ([ext:gnu_library_builtins],
  `ext/gnu/library-builtins.cpp`): `__builtin_abort`, `memcpy`, `printf`
  and the other C library functions' `__builtin_` forms (clang's
  LibBuiltin prototypes) are declared at their first use, when
  unqualified lookup finds nothing, as functions of the global namespace
  with C language linkage named by the library function (its symbol);
  `__builtin_abort` is noreturn.  The others (`__builtin_expect`,
  `_constant_p`, `_trap`, the overflow builtins) stay unsupported.
  gcc/pr79286 is skipped: its uninitialized argument is undefined in C++
  and clang++ -O2 deletes main.  `src/test/cxx/run/library-builtins.cpp`.
  C5.1 1563 -> 2033, 0 wrong.
  Then the compiler builtins, from one registry (`ext/gnu/builtin-registry.cpp`,
  `include/frontend/cplusplus/builtin-registry.h`): a row for each builtin
  whose call is not one function type with the standard conversions: its
  form (how a call is checked and typed), its fold (how a constant
  expression evaluates it: `ext/gnu/builtin-fold.cpp`, the integer, floating
  and string builtins and the overflow ones) and its lowering.  A builtin
  of the C front end's table with a signature of scalars and void or char
  pointers is the function of that prototype (`compiler_builtin`); the
  C library functions' `__builtin_` forms come from that table or clang's
  generated one (`frontend/c/libbuiltins.inc`: the 80 hand-written
  prototypes are gone); `__builtin_launder`, `_bswapg`, `_invoke` (the
  INVOKE expression, `ext/clang/builtin-invoke.cpp`), `_verbose_trap`,
  `_assume_dereferenceable`, `_memcpy_inline`, `_char_memchr`, the carry
  builtins, `__is_trivially_constructible`, `__is_destructible` and
  `__is_nothrow_destructible`, `__builtin_is_virtual_base_of`, atomics on
  enumerations and trivially copyable classes.  A refusal of an extension
  names its construct (`unsupported_construct::detail`: the source text, or
  the builtin and what about its call).  Not yet: `_Complex` and
  `__float128` expressions (so `__builtin_complex` answers 0 to
  `__has_builtin`), vector builtins and GCC `vector_size` types,
  `__builtin_clear_padding`, `__builtin_coro_*`, `__is_final`,
  `__is_standard_layout`, `__is_pod`, `__is_literal_type` and the other
  traits `bench/cxx-libcxx-builtins.py` lists; `src/test/cxx/run/gnu-builtin-*.cpp`
  (the ones in xfail.txt are what is missing), `src/test/cxx/sema/gram/ext.gnu-builtins.cpp`.
  libc++'s first refusal at [ext.gnu] (19 headers: `__builtin_assume_aligned`,
  then `__builtin_invoke`) is gone; the sweep of its 109 headers stops
  elsewhere (`__algorithm/simd_utils.h`: an alias template of an
  ext_vector_type and a lambda in a variable template).
  Then unions ([class.union]): a class with the class-key union has
  every non-static data member at offset 0, the largest's size and the
  strictest alignment; aggregate initialization initializes the first
  member alone ([dcl.init.aggr]/16, C's union initializer); union in
  elaborated-type-specifiers, the class-key agreeing with the class's
  ([dcl.type.elab]/3); lowered to a C union.  Unsupported: members of
  class type, anonymous unions and unnamed structs ([class.union.anon],
  they were taken for nested classes before), bases.
  `src/test/cxx/run/unions.cpp`; C5.1 2033 -> 2077, 0 wrong.
  Then bit-fields ([class.bit], `member_bit_field` in
  `class/class-specifier.cpp`): integral bit-fields, named, unnamed and of
  width zero, laid out as clang's for aarch64 Linux (a bit-field moves to
  its type's alignment only when it would cross it; every bit-field
  raises the class's alignment), lowered as C bit-fields with each bit
  offset checked against C's layout; [conv.prom]/5's promotions (an
  `expression_meaning::bit_field_width` that a conversion clears, a cast
  to the bit-field's own type too), & and sizeof of a bit-field and a
  reference bound to one unsupported, unnamed bit-fields skipped by
  aggregate initialization.  Unsupported: enumeration bit-fields, padding
  bits (a width wider than the type), initializers, attributes, and
  bit-fields in unions.  `src/test/cxx/run/bit-fields.cpp`;
  C5.1 2077 -> 2211, 0 wrong.
  Next: member functions of local and nested classes, in-class
  initializers of static members, unions, virtual functions.
  Then (2026-11-18, while S3 and S6 run in other sessions) preparation
  for the rest of the step, "S4: what remains" below.  The layout is no
  longer made member by member in the class rules: `abi/itanium/layout.cpp`
  (`itanium_class_layout`) runs the ABI's procedure ([abi:class-types]
  I-IV) at the closing }, over the entities alone (so an instantiated
  class is laid out by the same call), with I and III, the virtual table
  pointer and the virtual bases, as the places [class.virtual] and
  [class.mi] fill in; a virtual function declared after the data members
  moves all of them, which the old way could not do.  `entity::data_size`
  is the size for a POD for the purpose of layout.  The gate's layout
  claim exists: `-fcxx-dump=layout-claims` (per complete class: size, data
  size, alignment, each base's and member's offset, bit-fields by bit)
  against clang's `-fdump-record-layouts-complete`, per record by name, in
  `bench/cxx-sema.py` (and its `selftest`: hand-written claims, five of
  them wrong).  First run: 8586 classes as clang's, 8 refuted, two bugs,
  both fixed: an empty class is the ABI's ([abi:definitions]: a
  zero-width bit-field does not make a base non-empty, so `struct D : C {
  int : 0; }` with `C { int : 0; }` has data size 0), an unnamed bit-field
  is no member for [abi:pod] (a private one left the class a POD), and a
  class with nothing but a zero-width bit-field has its alignment as its
  size (it was 1).  Now 8594 as clang's, 0 refuted, 470 not checkable (a
  local class's name twice, a declaration clang rejects).  C5.3 has its
  harness: `bench/vcc-abi.py` runs `src/test/cxx/abi/NAME/` (caller.cpp,
  callee.cpp, clang/vcc x caller/callee, as CI's `abi` step): `classes`
  passes (layouts, bit-fields, unions, empty bases, classes passed by
  address and returned through x8, constructors and destructors of the
  other object, static members, namespaces, dynamic initialization, a
  callback); `tail_padding` and `virtual_calls` are `vcc-xfail` until
  their items below land (an XPASS is an error).  `bench/itanium-abi.py`
  dropped the text under a heading without an anchor, which was all of
  2.4's procedure: `work/itanium-abi/class-types.txt` has it now (the
  index and grammar are unchanged), and the script finds `work/` from a
  worktree.  Unchanged by the move: cxx-sema's other numbers (0 refuted
  anywhere; one more type claim, the size above), cxx-run 116/116, vpp -S
  of every run test byte for byte, C5.1 2206 passing and 0 wrong as with
  the binary before (bench/cxx-c-suites.passing is stale on main: 8
  listed runs need [cpp.pragma] or lost `_ZL1m`, 15 new ones are not
  listed), vcc-abi 35 pass, 2 xfail.

- Status (2026-11-20, object lifetimes in lowering): the refusals that
  named a context ("outside a statement", "in a conditional operand", "in
  a condition or init-statement", "a goto where an object with a
  destructor is live") are replaced by the rules they were special cases
  of: a full-expression is lowered as one C expression wherever it is
  ([intro.execution]/5); a temporary constructed under ?:, && or || has a
  flag tested before its destruction ([class.temporary]/4, clang's); a
  prvalue initializes its result object itself, through ?: too
  ([dcl.init]/17.6.1, `initialize_into`), and `temporary_object` is the
  one materialization ([basic.lval]/7) the call, construct and argument
  paths share; a goto destroys what is live at it and not at its label
  ([stmt.dcl]/2); a condition's and init-statement's variables go
  through the block's declaration path ([stmt.pre]/4, [stmt.while]/2,
  [stmt.for]/1: the condition's variable outlives the step).  A labeled
  declaration-statement is in its block (it was destroyed at once).
  cxx-run -O0123 1672 passed (4 xfails fixed), cxx-sema lowering 565 ->
  571 files as clang ([class.temporary] 33 -> 27), symbols 2956 -> 2964,
  cxx-c-suites unchanged; lower.cpp 6490 -> about 6600 lines with the
  new rules, C node helpers taking most of the growth back.

  Trivial assignment from a prvalue materializes the source before copying
  the class data bytes, preserving a derived member in reused tail padding
  (`tail-padding-assign-prvalue.cpp`).

#### S4: what remains

In an order where each item's tests can pass when it lands.  "Needs"
names what another track must have first; the counts are declarations of
clang's CXX, SemaCXX and SemaTemplate unsupported for that subclause
(2026-11-18), the size of what each item frees.

| # | What | Subclauses (unsupported) | Files | Needs | Gate |
|---|---|---|---|---|---|
| R0 | constexpr member functions and constructors, for S6's evaluator: `constexpr` on a member function (inline, `entity::constexpr_`) and on a constructor, in the class and out of it; an invocation of one is `unknown` to the constant rules and counted (`constexpr_calls`), so a static initializer invoking one stays `unsupported` [basic.start.static] until S6 evaluates it; C++11's implicitly const constexpr member stays `unsupported`. C++20 constexpr destructor definitions lower for runtime calls. Constant evaluation of destructor calls is still deferred.  **Done** (2026-11-18): `test/cxx/run/constexpr-members.cpp`, cxx-sema 0 rejected, 0 refuted | [dcl.constexpr] | `class/class-specifier.cpp`, `class/constructor.cpp`, `expression/postfix-expression.cpp`, `declaration/simple-declaration.cpp` | - | cxx-sema 0 we reject, cxx-run |
| R1 | Layout without virtuals, complete: a component that would share its address with one of its type moves ([abi:class-types] II-2, II-3: now `unsupported`); `aligned` and `packed` on a class, `#pragma pack`; enumeration and wide bit-fields, bit-fields in unions; anonymous unions and structs  **In progress** (2026-11-18): done: components that conflict move (II-2, II-3: an empty-subobject map in `layout.cpp`), bit-fields of enumeration type and in unions, anonymous unions and structs as members (`entity::anonymous_object`; `test/cxx/run/anonymous-unions.cpp`, `bit-fields-enum-union.cpp`): layout 10139 as clang (was 9565), 0 refuted.  Left: `aligned`/`packed` on a class, `#pragma pack` (the C++ token stream does not carry the pragma yet; C's is `frontend/c/pragma.cpp`), bit-fields wider than their type, anonymous unions at namespace and block scope, a mem-initializer of an anonymous union's member | [class.derived] 2603 (most are bases analysis cannot name: templates), [class.union.anon] 183, [class.bit] 159, [class.union] 170, [cpp.pragma] | `abi/itanium/layout.cpp`, `class/class.union*.cpp`, `bit-field.cpp` | - | layout claims 0 refuted |
| R2 | A class whose layout is not C's, lowered: a base's tail padding reused, an empty base or member off its natural offset.  `ast_builder_type_record_complete` takes explicit offsets, or the base is lowered as its members  **Done** (2026-11-18): `lower.cpp` `class_type` builds the C struct at the layout's offsets (padding fields; a base whose tail padding is reused is a char array of its data size, `c_fields` maps members to C fields), an empty base moved off offset 0 is addressed at its offset, and a trivial assignment of a class with tail padding copies its data size (`data_size_assignment`, as clang): `test/cxx/run/empty-subobjects.cpp`, `tail-padding.cpp`, vcc-abi `cxx/tail_padding` passes | - (lowering refuses: "a class whose layout is not C's") | `lower/lower.cpp`, `frontend/c/ast-builder.h` | C's gates when `struct ast` changes (`vbench.py gate`, `vcc-same.py`) | `cxx/tail_padding` passes |
| R3 | Member functions and static data members of nested and local classes (`<nested-name>`, `<local-name>` with discriminators), a nested class defined outside its class, in-class initializers of static members, default member initializers  **Mostly done** (2026-11-18): member functions, constructors and static members of nested classes (bodies at the outermost class's }), member functions of local classes (`<local-name>`, local symbols unless the function is inline), default member initializers ([class.base.init]/9.1), const static integral members initialized in the class (reads folded, no odr-use); an in-class member function definition is reread in its class's scope; tests `nested-class-members`, `local-class-members`, `default-member-initializers`, `static-member-constants`.  Left: a nested class defined outside its class, local classes that need a discriminator, default member initializers of class type, arrays and references, aggregate initialization of classes with them ([over.match.list]), inline and constexpr static members (R9), a local class's use of the enclosing function's variables (refused: whether it is an odr-use is not analysed) | [class.mem] 947, [class.nest] 190, [class.local] 127, [class.static.data] 29 | `class/class.nest.cpp`, `class.local.cpp`, `abi/itanium/mangling.cpp` | - | symbols claims; run tests |
| R4 | Special member functions in full: `= default` and `= delete`, when an implicit one is deleted, move constructors, copy and move assignment (implicit and user-declared), reference and const members, arrays of class type (construction and destruction in order), class members of unions  **In progress** (2026-11-18): done: constructors `= delete` (except `X(X&)` and `X(X&&)`, whose choice on a return needs implicit move into another class), public `X() = default`, `X(const X&) = default`, `~X() = default` (a trivial defaulted default constructor calls nothing); move constructors, and a returned local moved when its class declares one ([class.copy.elision]/3); `static_cast<T&&>`/`<T&>` of a glvalue of the same type; reference members (laid out as pointers, no POD for layout) and const members; an implicitly deleted default constructor ([class.default.ctor]/2) or copy assignment ([class.copy.assign]/2, /7) used is ill-formed; automatic one-dimensional arrays of class type, elements constructed in order and destroyed in reverse; `owned_array::append` of its own element (a use after free that misplaced anonymous unions).  Tests `defaulted-deleted`, `move-constructors`, `reference-members`, `class-arrays`; declarations 30261 as clang (29261 before R4), 0 refuted.  Left: the implicit move constructor and move assignment ([class.copy.ctor]/8: a subobject with a move constructor keeps the implicit members unsupported), an implicit copy assignment that calls a subobject's `operator=`, a deleted destructor, defaulted members that are private, constexpr or not the implicit ones' form, static and multidimensional arrays of class type and their initializers, class members of unions | [dcl.fct.def.general] 1085, [class.ctor] 1491, [class.copy.ctor] 278, [class.default.ctor] 111, [class.copy.assign] | `class/constructor.cpp`, `declaration/function-definition.cpp`, `declaration/deleted-definition.cpp` | S3: `operator=` and constructors chosen by [over.match] with reference binding and cv-qualifiers | CXX/special; symbols; run tests |
| R5 | Initialization of classes in full: brace elision, aggregates with bases, list-initialization and its constructors, value- and zero-initialization, lifetime extension ([class.temporary]/6), temporaries in conditional operands, the rest of copy elision  **In progress** (2026-11-18): done: brace elision ([dcl.init.aggr]/16) with too many clauses ill-formed; aggregates by edition (bases from C++17, default member initializers from C++14, no user-declared constructors from C++20); list-initialization through constructors with the narrowing check ([dcl.init.list]/3.7); implicitly deleted default constructors used are ill-formed; `T()` value-initialization of a class without constructors, `T(x)` of the same type a copy; redeclaring a function a using-declaration named is unsupported.  Tests `brace-elision`, `list-constructors`, `value-initialization`.  Left: lifetime extension, temporaries in conditional operands, a missing member with a default member initializer, narrowing into a reference parameter, C++03 value-initialization | [dcl.init.aggr] 215, [dcl.init.list] 86, [dcl.init] 32, [class.temporary] 11, [class.copy.elision] 49 | `declaration/initializer.cpp` | S3: [over.match.list], [dcl.init.ref] (S3 owns its file) | CXX/dcl.init; type claims; run tests |
| R6 | Virtual functions: `virtual`, `override`, `final`, pure, abstract classes; overriding and covariant returns; layout I (the virtual table pointer, the primary base); virtual tables ([abi:vtable-components]), their symbols and vague linkage with the key function ([abi:vague-vtable]), virtual calls, `this`-adjusting thunks, the pointer set during construction and destruction ([class.cdtor]), `__cxa_pure_virtual`, D0  **In progress** (2026-11-18): done (`class/virtual-function.cpp`): `virtual`, `override`, `final`, pure-specifiers, overriding through the direct base's virtual table ([class.virtual]/2, /4, /5, [class.mem]/12, /14), abstract classes (objects, members, arrays and by-value uses of them ill-formed or unsupported); layout I (the vptr at offset 0, or the first base as primary; layout claims write `vptr`); `_ZTV` with offset to top 0, null RTTI, `__cxa_pure_virtual` (in `src/test/rt`), strong where the key function is defined, weak otherwise; constructors (implicit and copy too) store the vptr after the bases; virtual calls through the slot, qualified ones direct; a dynamic class is no aggregate and not trivially copyable.  Test `virtual-functions`; declarations 31089 as clang (30507 before), accepts unchanged, layout 11869 verified, 0 refuted.  Left: a primary base not first (more than one dynamic base and non-virtual thunks: R7, 2026-11-18), covariant returns, ~~virtual destructors and D0 (R8), destructors of dynamic classes (vptr reset)~~ (done in R8, 2026-11-18), copy assignment of dynamic classes, virtual operator functions, local classes, a virtual call whose object address has side effects (lowering refuses), vtable claims against `-fdump-vtable-layouts` | [dcl.fct.spec] 1127, [class.virtual], [class.abstract] | `class/virtual-function.cpp`, `class.abstract.cpp`, `abi/itanium/vtable.cpp`, `layout.cpp` | R8 for D0; RTTI slots are null until S7 (`-fno-rtti`) | vtable claims (`-fcxx-dump=vtable-claims` against `-fdump-vtable-layouts`, to write with the item); layout claims; `cxx/virtual_calls` passes |
| R7 | Virtual bases: layout III, vbase and vcall offsets, VTT and construction virtual tables, virtual thunks, C1 and D1 no longer C2's and D2's  **In progress** (2026-11-18): done: more than one dynamic base (R6's leftover): a class's virtual table group ([abi:vtable-general]: `vtable_group`, `vtable_adjustments` in `sema.h`), secondary tables with their offsets to top, overriders called through non-virtual thunks `_ZThn<offset>_` (defined with the function, `lower.cpp` `thunk_definitions`), constructors storing every table's address point; pointer conversions to a base at a nonzero offset with the null check ([conv.ptr]/3).  Test `multiple-inheritance`; vcc-abi `cxx/virtual_calls` passes (xfail removed); declarations 31098 as clang, accepts unchanged, layout 11878, 0 refuted.  Left: a primary base that is not the first base, a conversion through two bases at nonzero offsets, thunks of variadic functions or ones returning a class by address; then virtual bases themselves.  2026-11-18: virtual bases done but for virtual thunks: `virtual` base-specifiers, lookup and conversions through them (the vbase offset read from the virtual table; a pointer with side effects through a temporary), layout III (`nv_size`/`nv_alignment`; layout claims list virtual bases), vbase offsets and the vcall offsets of a virtual base's primary table, `_ZTT` and `_ZTC` in clang's VTTBuilder order, C2/D2 taking the VTT and skipping virtual bases, C1 the full body, D1 calling D2 then destroying the virtual bases, an inline structor's C1/D1 only where called.  Tests `virtual-bases`, `virtual-bases-shapes` (tables word for word as clang -O0's), vcc-abi `cxx/virtual_bases`; cxx-run 280/280, declarations 32224 as clang, accepts unchanged, layout 12805, 0 refuted.  2026-11-18: overrides across virtual bases: the final overrider of a virtual base's slot taken from the class's other bases (`virtual_base_overrider`, ambiguous ones unsupported), vcall offsets from the overrider's subobject, virtual thunks `_ZTv0_n<offset>_` (D1/D0 ones for a virtual destructor in a virtual base) and non-virtual D1/D0 thunks of a destructor in a secondary table (R8's leftover); a pointer converted to a base inside a virtual base; a constexpr constructor defined in a class with virtual bases ill-formed ([dcl.constexpr]/3, as clang).  Tests `virtual-thunks` (tables and thunk symbols as clang -O0's), `virtual-base-conversions`, vcc-abi `cxx/virtual_bases` with `virtual ~A` and B overriding A's function; cxx-run 288/288, declarations 32244 as clang, accepts unchanged, layout 12814, 0 refuted.  Left (`unsupported` [class.mi]): a nearly empty virtual base as primary base ([abi:class-types] I: layout, shared vptr, its tables and VTT entries), a virtual thunk for a table at a nonzero offset inside a virtual base (`_ZTv<nv>_`), a final overrider that is not unique; constexpr constructors declared only or defaulted in such a class stay unsupported | [class.mi] 597 | `class/class.mi.cpp`, `abi/itanium/layout.cpp`, `vtable.cpp` | R6 | vtable and layout claims; an ABI case with a diamond |
| R8 | `new` and `delete`: allocation and deallocation functions looked up and called, placement, array cookies ([abi:array-cookies]), initializers; `operator new`/`delete` in the test run time (`src/test/rt`, on malloc)  **In progress** (2026-11-18): done: virtual destructors (declared, `= default`, overriding a base's, the implicit one virtual when a base's is: [class.virtual]/7, [class.dtor]/13) with their D1/D0 pair of slots ([abi:vtable-components]; D2 in the slot for an inline one, as clang), the deleting destructor D0 calling the deallocation function of [class.dtor]/12 (class-specific or global, sized), `delete p` through the table ([expr.delete]/3); destructors of dynamic classes store the vptr on entry ([class.cdtor]/4); arrays of classes with constructors or destructors in new[]/delete[] (elements constructed in order, destroyed in reverse), the array cookie (max(8, alignof), count before the elements, the whole size to `_ZdaPvm`, SIZE_MAX on overflow), new semantic node `array_count`; `new T()` with a user-provided default constructor ([dcl.init]/8.1).  Fixed on the way: const-default-constructible with default member initializers ([dcl.init]/7.4).  Tests `virtual-destructors`, `new-delete-arrays`; vcc-abi `cxx/new_delete` (allocated in one half, deleted in the other); declarations 31454 as clang (31098 before), 0 we reject, accepts unchanged (504), layout 12161, 0 refuted; cxx-run 248/248.  Operator new/delete stay in each test (none in `src/test/rt`).  Left (`not_supported`): pure virtual destructors, a virtual destructor in a secondary table (D1/D0 thunks), a dynamic class with a deleted or inaccessible subobject destructor or class deallocation function, explicit destructor calls `p->~T()`, an array with a new-initializer, value-initialization of a class without a user-provided default constructor (8.2), Apple arm64's cookie (lowering error), a constructor that throws (/25), destroying operator delete, new-extended alignment, one prvalue of the class in a braced list with constructors (CWG2311) | [expr.new] 601, [expr.delete] 106 | `expression/new-expression.cpp`, `delete-expression.cpp`, `abi/itanium/calls.cpp` | S3: operator functions declared and overloaded | run tests; an ABI case allocating in one object and freeing in the other |
| R9 | The rest of [basic.start]: destructors of static objects (`__cxa_atexit`, `__dso_handle`), local statics with dynamic initialization and their guards ([abi:guards]), inline variables  **In progress** (2026-11-18): done (`lower/lower.cpp`): a namespace-scope object with a destructor registers it with `__cxa_atexit(D, &obj, &__dso_handle)` in its `__cxx_global_var_init` ([basic.start.term]/3, [abi:dso-dtor]; a constant-initialized one gets an init function for the registration alone, as clang); a block-scope static with dynamic initialization or a destructor is initialized under its guard `_ZGV...` ([stmt.dcl]/4, [abi:guards]: acquire load of the first byte, `__cxa_guard_acquire`, the initialization, the registration, `__cxa_guard_release`, as clang -O0); `src/test/rt` has `__cxa_atexit`, `__dso_handle` and the guard functions.  Tests `static-destructors`, `local-statics`; cxx-run 256/256, declarations 31454 as clang, accepts unchanged, 0 refuted.  Left (lowering errors): inline variables with dynamic initialization or a destructor (guarded, COMDAT), guarded statics in inline functions, static arrays with destructors (`__cxx_global_array_dtor`), temporaries in a guarded initializer, `__cxa_guard_abort` (no exceptions yet), thread_local (`unsupported` [dcl.stc]) | [basic.start.term], [stmt.dcl] | `basic/basic.start*.cpp`, `abi/itanium/guard.cpp`, `src/test/rt` | S6a decides which initializers are constant (a constexpr constructor is unsupported today, so no object is constant-initialized by one; clang emits such an object as data, with no start-up code) | symbols claims; run tests |
| R10 | Access in full and friends: friend functions and classes, access of nested classes and through using-declarations, [class.protected] everywhere  **In progress** (2026-11-18): done (`class/friend.cpp`): in a class that is not local, friend classes (class-key identifier of the innermost enclosing namespace, declared before or later, [namespace.memdef]/3; a qualified elaborated-type-specifier; C++11's friend type-name) and friend function declarations by an unqualified name (a function declared before, or one first declared there: unbound in its namespace until declared, found by argument-dependent lookup through the granting class, [basic.lookup.argdep]/4.3); `accessible()` grants friends and friends of derived classes ([class.protected]), a base is accessible in a friend of the derived class ([class.access.base]/5.2); a nested class redeclared with another access is ill-formed ([class.access.spec]/4); `test/cxx/run/friends.cpp`.  cxx-sema 31814 as clang (was 31454), 0 we reject, 494 we accept and clang rejects (was 504).  Left (`unsupported`): friend definitions in the class ([class.friend]/7, hidden friends), friend operator functions and members of other classes, friends of local classes, Microsoft's friend lookup, an implicit constructor using a friend's private constructor or destructor; the naming class of [class.access.base]/5 (CXX/class.access/p4.cpp test9), private destructors of variables and temporaries (p4.cpp test3, test14)   2026-11-23: friends by the paragraphs of [class.friend] and [temp.friend] (`class/friend.cpp`, `template/friend-template.cpp`, each paragraph mapped in the file's head): /3's forms (friend first, no cv-qualifier, a type that is no class ignored: `friend int;`, a template's `friend T;`), [dcl.type.elab]/2, /3 (a typedef- or template-name, a class-key that disagrees), a class nested in the granting class, /6 (no qualified definition, none in a local class), /8 (no storage-class-specifier), /11 (local classes: friend classes of the block, friend functions with a prior block-scope declaration), [temp.friend]/2, /3 (friend class templates grant to every specialization, qualified or not; before, they were accepted and granted nothing), /4 (`template<class T> friend struct A<T>::B;` when C's arguments are the parameters), /6, /7, /9's first sentence, [temp.pre]/2 (`template<class U> friend S<U>;`).  cxx-sema 84403 as clang (was 84314), 679 we reject (was 689); `test/cxx/sema/gram/class.friend.cpp`.  Then: members of other classes matched by parameters, cv and return type (operators, const members), implicitly-declared special members befriended (their constexpr/noexcept repeated, for classes whose special members are trivial); [temp.friend]/4 in general: C's arguments deduce the parameters from each specialization (as a partial specialization's), the declaration analysed again quietly with the deduced arguments, the member it validly redeclares befriended (classes when the specialization begins, functions when it is complete), A<T>::D::g and non-deducible parameters ill-formed; [temp.friend]/9: constrained non-template friend definitions per specialization (unsatisfied: not viable), constrained friend templates whose constraint names an enclosing parameter distinct per specialization.  cxx-sema 84604 as clang (was 84314 before R10's rework), unsupported 39718 (was 39934).  Left: a friend template defined by two specializations is still visible to ordinary lookup ([namespace.memdef]/3 hidden friends), friend function declarations whose name is a class's, an elaborated-type-specifier in a friend function's return type (friend struct S* g();). | [class.friend] 765, [class.access.base] 26 | `class/class.access*.cpp`, `friend.cpp` | S3 for friend operator functions | CXX/class.access: "we accept, clang rejects" down |
| R11 | Classes in calls, checked across compilers for every shape: empty classes as arguments (clang's C++ and C differ), homogeneous aggregates with bases, classes with bit-fields and unions in registers, by-address classes on the stack  **In progress** (2026-11-18): done: vcc-abi `cxx/call_shapes` (arm64 Linux, clang/vcc x caller/callee under qemu) passes: empty classes as arguments between others and as results (ignored, as clang's C++ ABI), homogeneous float and double aggregates with members in a base, a float/double mix, classes non-trivial for the purposes of calls through the copy constructor alone or the destructor alone (by address, results through x8), a dynamic class by value and as a result, reference members, a union with bit-fields in registers, a class over 16 bytes (the caller's copy), arguments past the eighth register on the stack.  Fixed: a named local returned from a function whose class is trivially copyable with a non-trivial destructor is the result object ([class.copy.elision]/1.1 as clang applies it; it was copied and destroyed).  vcc-abi 39 pass; cxx-run 260/260; cxx-sema unchanged.  Left: classes through `...` (va_arg is not parsed: [expr.call] `unsupported` for a class argument to an ellipsis); the C++ cases run on arm64 Linux only: `--host` (Apple arm64) fails in vcc's C++ front end on the system headers ([dcl.fct]), `--target x86_64` needs an x86-64 host, `win64` the llvm-mingw sysroot | [abi:calls] | `abi/itanium/calls.cpp`, `lower/` | - | ABI cases generated per shape |

Pointers to members ([dcl.mptr] 454, [expr.mptr.oper] 19) are S5's, with
their ABI ([abi:member-pointers]) in `abi/itanium/`.

Where the tracks meet, to keep the three sessions out of each other's
lines:

- `include/frontend/cplusplus/sema.h` is the one file all three edit.
  `struct entity` carries every class's fields, and R6-R7 add a dozen
  more (primary base, virtual table pointer, virtual functions, virtual
  bases).  They go into a record of their own per class, in a table
  beside `class_members` and `class_bases`, added by R6; the fields
  `entity` has today stay where they are until S3 and S6 have merged.
- Lowering is `lower/`, files by topic (2026-11-20, split from the
  6500-line `lower.cpp`): `lower-internal.h` (the `c_lowering` class and
  the C node helpers), `types.cpp`, `symbols.cpp`, `scopes.cpp`,
  `initialization.cpp`, `values.cpp`, `operators-and-calls.cpp`,
  `expression.cpp`, `statements.cpp`, `member-pointers.cpp`,
  `new-delete.cpp`, `vtables.cpp`, `virtual-bases.cpp`,
  `static-storage.cpp`, `definitions.cpp`; each compiles alone.  New
  lowering goes into the file of its topic.
- [dcl.init.ref] and the copy constructor's selection are S3's; R4 and R5
  call them and do not re-implement them.
- A class template's instantiation (S6b) completes a class from entities,
  not from syntax: `itanium_class_layout` and the implicit special
  members already take the class entity alone, and R3-R7 keep to that.
- Constant initialization (S6a) changes which variables R9 initializes at
  start-up; the symbols claim is the check on both sides.

### S5. Conversions by user code, lambdas, the rest of [expr]

- [class.conv], conversion functions in overload resolution, explicit
  casts ([expr.static.cast], [expr.const.cast], [expr.reinterpret.cast],
  [expr.cast]), pointers to members, non-generic lambdas
  ([expr.prim.lambda]), range-based `for`, structured bindings.
- Gate: type claims 0 wrong on CXX/expr; `unsupported` 0 on SingleSource's
  template-free files.
- Status (2026-11-18, step 1: placeholder types and decltype):
  `declaration/decltype-specifier.cpp` gives decltype(E) its type
  ([dcl.type.decltype]/1: the entity's declared type for an
  unparenthesized id-expression or class member access, else T&&, T& or T
  by value category), the operand analysed as an unevaluated operand.
  `declaration/placeholder-type-specifier.cpp` has [dcl.spec.auto] and [dcl.type.auto.deduct]:
  auto and decltype(auto) are `type_kind::placeholder` types that
  `decl_specifier_seq` accepts only where `placeholder_allowed` says (a
  simple-declaration's or function-definition's decl-specifier-seq, a
  condition's); a variable with an initializer (= e, (e), {e}) has its type
  deduced as [temp.deduct.call] deduces an invented parameter U
  (`deduce_type`, with the P/A adjustment of /2-3 now
  `adjust_call_deduction`, shared with `deduce_from_call`), all declarators
  of a declaration the same U and all of them variables (/7, CWG1265),
  at namespace and block scope and in
  init-statements and conditions; a non-member function's return type is
  deduced from its return statements (/3, /8, /9; C++14 on), the entity's
  type replaced at the first; a name of an entity whose placeholder is not
  deduced yet is ill-formed (/11), and one never defined has no
  symbol; `auto f(...) -> T` ([dcl.fct]/2, in
  `trailing_return`, called by `declarator`).  decltype(auto) for
  variables and return types, alone (/5, checked at the declaration).  Also: [dcl.init.list]/3.6
  (std::initializer_list) and a structured binding declaration are
  unsupported where they used to be rejected or accepted wrongly once
  decltype let them be reached; [expr.cond]/4 with a bit-field converted to
  the more cv-qualified type is a prvalue; [expr.cast]/4's static_cast to
  `cv void*` then const_cast; & of any qualified-id naming a
  non-static member (&C::m, in a member function of C too) is
  [dcl.mptr]'s, unsupported.  A deduced return type mangles as nothing for a
  non-template function (`-O0` symbols as clang's).  Tests:
  `src/test/cxx/run/auto.cpp`, `decltype.cpp`, `auto-return.cpp`.  Not
  analysed: copy-list-initialization from a braced-init-list
  (std::initializer_list<U>), a deduction `deduce_type` does not make
  ([temp.deduct.call]/4's derived class), a redeclaration with a
  placeholder of a name already declared in its scope, and a function
  declared without a placeholder whose name has a function with a
  placeholder return type (/13; a later declaration without a placeholder
  of a variable declared with one is analysed as any redeclaration), member functions, static data members and
  function templates with placeholders, a variable whose deduced type has
  internal linkage by [basic.link]/8, a trailing-return-type that names a
  parameter or with a derived-declarator-type-list, decltype in a
  member-specification or with a type-dependent operand, decltype(auto)
  of an implicitly movable entity (P2266R3), generic parameter types, auto
  template parameters, class template argument deduction, constrained
  placeholders, and lambdas, range-based for and structured bindings
  (their own steps).  `bench/cxx-sema.py`: declarations 49855 -> 50855 as
  clang, 8 we reject and clang accepts (the same 8), 2285 -> 2352 we
  accept and clang rejects, unsupported [dcl.spec.auto] 3694 -> 1692;
  declared types 25899 -> 26681, types 43087 -> 44826, constants 17608 ->
  18347, lookup 17154 -> 18983, symbols 2171 -> 2216, layout 13223 ->
  13274, all 0 refuted; lowering 356 -> 362 files, 1 refuted (as before).
  After the review's fixes, rebased on main: declarations 50852 -> 50913
  as clang, 0 we reject and clang accepts, 2352 -> 2305 we accept and
  clang rejects, [dcl.spec.auto] unsupported 1707 -> 1704; types 44844 ->
  44815 and constants 18362 -> 18333 (claims in declarations clang
  rejects, now rejected), the other claims as before, all 0 refuted;
  lowering 362 files, 0 refuted;
  one more `tree` problem (p4-cxx0x.cpp's decltype(static_cast<int&&>(i)),
  the static_cast to an rvalue reference that already has it elsewhere).
  cxx-run 376/376 (94 programs).
- Status (2026-11-18, step 2: conversion functions and explicit):
  `class/conversion-function.cpp` declares conversion functions in their class
  ([class.conv.fct]/1, /2; declared or defined there, `explicit` too,
  constexpr, cv-qualified), all named `operator <conversion>` and told
  apart by type, mangled `cv<type>`; `conversion_function_for` is
  [over.match.conv] (non-explicit ones, explicit ones in a
  direct-initialization to the type itself; best by implicit object
  sequence, then the second standard conversion sequence,
  [over.match.best]/2.2), used by [dcl.init]/17.7 (`initialize_with`),
  [dcl.init.ref]/5 to a non-class type (one conversion function where
  [over.match.ref]'s candidates differ), `contextual_bool` and
  `implicit_conversion_sequence` (a user-defined sequence that keeps its
  second standard conversion sequence; [over.ics.rank]/3.3 compares two of
  one function, a converting constructor's too).  [class.conv.ctor],
  [over.match.ctor]/1: explicit constructors (the keyword) are no
  candidates in copy-initialization (`construct`'s `copy`) nor in
  [over.match.copy]'s converting constructors.  [class.bit] moved to
  `class/bit-field.cpp` (class-specifier.cpp under 2000 lines).  The type-claims
  dump no longer stops at a member function's body (claims after it were
  lost).  Started from another session's uncommitted patch (declarations,
  explicit-specifier, mangling, the [dcl.init] uses).  Tests:
  `src/test/cxx/run/conversion-functions.cpp`,
  `explicit-constructors.cpp`.  Not analysed: definitions outside the
  class and explicit calls (`x.operator int()`), virtual conversion
  functions and templates, inherited ones ([class.conv.fct]/4), a class
  source to a class target or reference ([over.match.copy],
  [over.match.ref]), built-in operator candidates ([over.built]) for an
  operand of a class with conversion functions (`!`, `&&`, `||` without an
  operator function are), `explicit(bool)`, an explicit constructor that
  is defaulted or chosen in list-initialization ([over.match.list]), two
  conversion functions [over.ics.rank]/3.2.1 would tell apart by a proper
  subsequence to another type, and casts (step 3).  `bench/cxx-sema.py`:
  declarations 50913 -> 52255 as clang, 0 we reject and clang accepts,
  2305 -> 2322 we accept and clang rejects, unsupported 71971 -> 70612;
  declared types 26684 -> 26768, types 44815 -> 65306, constants 18333 ->
  27062 (most from the dump fix), lookup 18983 -> 19436, symbols 2215 ->
  2221, layout 13283 -> 14283, all 0 refuted; lowering 362 -> 363 files, 0
  refuted; `tree` problems 42 -> 34 (the remaining conditional of two class
  prvalues with destructors is old).  cxx-run 384/384 (96 programs).
- Status (2026-11-18, step 3: explicit casts): one function,
  `explicit_conversion` (`expression/cast-expression.cpp`), has the rules of
  [expr.static.cast], [expr.const.cast] and [expr.reinterpret.cast];
  `cast_kinds` says which casts it may use: one for `static_cast`,
  `const_cast` and `reinterpret_cast` (`named_cast`, `expression/postfix-expression.cpp`),
  all of them in [expr.cast]/4's order for the cast notation and
  [expr.type.conv]'s `T(x)`.  To a class it is [expr.static.cast]/4's
  direct-initialization (`class_direct_initialization`, shared with
  `T(x)`; `(B)3`, `static_cast<B>(3)`, a typedef-name's `T(x)`); from a
  class to another type a conversion function (`conversion_function_call`,
  explicit ones too: `static_cast<bool>(f)`, `int(a)`, `(int)a`).  Between
  pointers to classes one derived from the other (`class_pointer_cast`):
  to a base by `pointer_to_base`, to a derived class by the new
  `pointer_to_derived` ([expr.static.cast]/11; conversion
  `base_to_derived`, lowered as the base's offset
  subtracted after a null check, the code `derived_to_base` shares in
  `null_checked_offset`); the cast notation through public non-virtual
  bases only.  To a reference (`reference_cast`): the same glvalue, or the
  lvalue's address converted as a pointer and the object it points to
  ([expr.static.cast]/2, [expr.const.cast]/4, [expr.reinterpret.cast]/11).
  Casting away constness ([expr.const.cast]/7) is checked for pointers to
  what is no pointer, arrays by their elements' cv-qualifiers.  Tests:
  `src/test/cxx/run/explicit-casts.cpp`.  Not analysed: `dynamic_cast`
  ([expr.dynamic.cast], needs RTTI), a reference cast that may bind a
  temporary ([expr.static.cast]/4: to const or an rvalue reference of
  another type), a prvalue or xvalue operand of a reference cast of
  another type, references to functions, casting away constness below the
  first level of a multi-level pointer without a const_cast, the cast
  notation through a private or virtual base ([expr.cast]/4's
  inaccessible bases) or to an incomplete class ([expr.cast]/5),
  `reinterpret_cast` of std::nullptr_t and of a bit-field to its own type,
  a class with no constructors from another class, Microsoft's extra
  casts (unsupported in its mode).  `bench/cxx-sema.py`: declarations
  53608 -> 54051 as clang, 12 we reject and clang accepts (the same 12),
  2392 -> 2396 we accept and clang rejects, unsupported 69209 -> 68762;
  declared types 26974 -> 26976, types 65885 -> 67244, constants 27345 ->
  27370, lookup 19680 -> 20039, symbols 2276 -> 2346, layout 14735 ->
  14767, all 0 refuted; lowering 371 -> 401 files, 0 refuted; `tree`
  problems 34 -> 26 (the casts to a reference now have their own nodes).
  cxx-run 420/420 (105 programs); cxx-c-suites 0 wrong, no pass lost (the
  four lost passes it reports, [cpp.pragma] and pr109318, fail at the
  starting commit too).
  After review (2026-11-18): the pointer conversions between a class and
  its base on one non-virtual path are one routine, `pointer_along_path`
  (`class/base-clause.cpp`), for both directions and for the cast
  notation (which skips the access checks, [expr.cast]/4); the steps come
  from `nonvirtual_path`, which `single_nonvirtual_path` shares.  One
  conversion per base at a nonzero offset, and `null_checked_offset` folds
  a chain of them into one null check, so an upcast across two nonzero
  offsets is no longer refused.  An ambiguous base of a downcast is
  ill-formed ([expr.static.cast]/11), no longer unsupported.  Now
  analysed: the cast notation through a private or protected base and to
  an incomplete class (the reinterpret_cast, as clang),
  `reinterpret_cast<long>(nullptr)` ([expr.reinterpret.cast]/4),
  `static_cast<bool>(nullptr)` ([conv.bool]), a cast to a base class
  slicing a derived glvalue (`static_cast<B>(d)`, a class with no
  constructors), and a reference to const or rvalue reference to a
  non-class type bound to a temporary (`static_cast<const int &>(3)`,
  `static_cast<int &&>(4)`, `(const unsigned char &)i`: `reference_binding`,
  the temporary's address).  Still not analysed: such a temporary in a
  constant expression (unsupported by the evaluator) or of a class type,
  the cast notation through a virtual or ambiguous base, slicing a
  prvalue through a virtual base.  `bench/cxx-sema.py`: declarations
  54051 -> 54065 as clang, 12 we reject and clang accepts, 2396 we accept
  and clang rejects, 0 refuted; cxx-run 420/420 (105 programs).
- Status (2026-11-18, step 4: pointers to members): [dcl.mptr]'s
  declarator (`pointer_operator`, `declaration/declarator-meaning.cpp`; an enumeration or
  namespace is ill-formed, so are reference and void members), `&C::m`
  ([expr.unary.op]/4, `member_pointer_constant`), `.*` and `->*`
  (`expression/pointer-to-member-operator.cpp`: node `member_pointer_access`, the object
  converted to the member's class, a member function's result called in
  `call`), [conv.mem]'s null and base-to-derived conversions and
  [expr.static.cast]/12's way back (`expression/pointer-to-member-conversions.cpp`;
  `member_pointer_along_path` sums the offsets of `accessible_path`, the
  path walk `pointer_along_path` now shares), equality with composite types,
  [conv.bool], assignment, overload ranking (to bool, from null).  Lowered as
  the Itanium/AArch64 ABI: a data member pointer is a ptrdiff_t, null -1; a
  member function pointer {ptr, adj} with adj twice the this-adjustment, a
  virtual function as its vtable offset and adj's low bit; static data
  initialized at namespace scope, -1 for an uninitialized one.  Mangled `M
  class member` (clang's).  Tests: `src/test/cxx/run/member-pointers.cpp`,
  `member-pointers-initialization.cpp`.  Not analysed: member functions with
  a cv- or ref-qualifier (not in the type table), member pointers as
  template arguments and in deduction, an implicit conversion sequence
  through [conv.mem]/2 (rank unknown), const_cast and reinterpret_cast of
  member pointers, relational comparison, `.*` on a prvalue class, members
  of anonymous unions, composite types of member function pointers of
  different classes, conversion functions to a member pointer.  Not
  lowered: zero-initializing an aggregate holding a data member pointer
  other than as a constant, a call through a member pointer whose result
  is returned through memory.  `bench/cxx-sema.py`: declarations 54065 ->
  54506 as clang, 12 we reject and clang accepts (the same 12), 2396 ->
  2398 we accept and clang rejects, unsupported 68748 -> 68305; declared
  types 26976 -> 27010, types 67258 -> 67644, constants 27370 -> 27490,
  lookup 20042 -> 20331, symbols 2346 -> 2385, layout 14767 -> 14806, all 0
  refuted; lowering 401 -> 418 files, 0 refuted.  cxx-run 428/428 (107
  programs); cxx-c-suites 0 wrong, no pass lost but the known [cpp.pragma]
  and pr109318.
- Status (2026-11-18, step 4 after review): [conv.qual] of member pointers
  (`same_pointer_level`: "pointer to member of class C_i" is a P_i of
  `similar` and `qualification_convertible`; conversion
  `member_pointer_qualification`, the same value), alone or after
  [conv.mem]/2, in implicit conversions, overload ranking (Exact Match,
  [over.ics.rank]/3.2.5), the composite type of `==` (the union of the
  members' cv-qualifiers) and static_casts both ways.  `.*` on a prvalue
  materializes its temporary ([basic.lval]/7) and is an xvalue, so `mk().*pm
  = 1` and `&(mk().*pm)` are ill-formed for their real reason; a prvalue of
  a class with a destructor, or an aggregate's temporary, stays
  unsupported [class.temporary].  `&` of a bit-field is one check citing
  [class.bit]/3, ill-formed (clang's error) for `&s.b` and `&S::b`.
  Tests added to `member-pointers.cpp`.  `bench/cxx-sema.py`: declarations
  54506 -> 54535 as clang, 12 we reject and clang accepts (the same), 2398
  we accept and clang rejects, all 0 refuted.  cxx-run 428/428 (107
  programs); cxx-c-suites 0 wrong; quality redundant_tokens 58937,
  duplicate_runs_across_files 351, unchanged.
- Status (2026-11-18, step 5: non-generic lambdas): a closure is a local
  class (`lambda-expression.cpp`) with its function call operator (const
  unless `mutable`, deduced or trailing return type), its captures
  unnamed members (`lambda-capture.cpp`: by copy, by reference,
  `[=]`/`[&]`, `this`, `*this`, init-captures, an enclosing lambda's
  captures for a nested one, [expr.prim.lambda.capture]/14), its object an
  initializer list of them.  A captureless lambda has the conversion
  function to pointer to function and clang's static `__invoke`
  (`closure-type.cpp`); `f(args)` on a closure takes the call
  operator over the surrogate.  Mangling: `Ul<params>E[n]_` in inline and
  templated contexts (a lambda's call operator among them), clang's
  per-function `$_N` elsewhere, numbered in the order lowering makes the
  symbols (`number_closures`; a specialization for such a closure gets
  its symbol at its first use); a function template specialization
  internal by its arguments has no `L`.  Not supported: generic lambdas,
  `constexpr`/`consteval` lambdas and the call operator's implicit
  constexpr (a constant evaluation of a closure's function is unknown),
  default arguments, packs, implicit capture of a variable usable in
  constant expressions (the odr-use question), lambdas outside a
  function body, an init-capture with a trailing-return-type, the
  conversion of a variadic lambda; a closure capturing a class with a
  destructor fails in lowering (a temporary with a destructor).  Test
  `lambdas.cpp`.  `bench/cxx-sema.py`: declarations 55698 -> 55862 as
  clang, 12 we reject and clang accepts (the same), 2453 -> 2467 we accept
  and clang rejects, all 0 refuted; lowering 428 -> 435 files, [expr.prim.lambda]
  no longer among the reasons.  cxx-run 460/460 (115 programs);
  cxx-c-suites 0 wrong; quality redundant_tokens 59310 -> 59684,
  duplicate_runs_across_files 353 -> 360.
- Status (2026-11-20, step 5: lambda captures and closures): one capture
  algorithm in `lambda-capture.cpp`.  A local variable usable in constant
  expressions is captured only by an odr-use ([basic.def.odr]/4: its names
  stay entity nodes until the body is analysed, a read is none, the rest
  are captured and rewritten: `pending_uses`); an unevaluated operand
  captures nothing.  Arrays are captured by copy element by element
  (/15), structured bindings (C++20, /4), packs (`[xs...]`, `[&xs...]`,
  `[...ys = xs]`: one capture per element, /17).  A generic lambda (an
  `auto` parameter, a template-head with packs and non-type parameters)
  decides its implicit captures where the lambda-expression is
  (`implicit_captures`: the names of its body), its specializations
  use them; `constexpr` and `consteval` lambdas, the call operator's
  implicit constexpr (a constant evaluation of a closure), lambdas in
  unevaluated operands and in default arguments of member functions
  (`d_` scope) and default member initializers (`M` data-member prefix,
  one closure type for the initializer each constructor analyses).
  Mangling: the template-param-decls of a lambda's template-head (`Ty`,
  `Tn <type>`, `Tp`; none for an invented one) and `Xsp T_ E` for an own
  non-type pack.  Found: `default_arguments` held a reference into the
  entity table across the analysis of an argument.  Not supported (tests
  in `xfail.txt`, `lambda-capture-*`): the conversion of a generic lambda
  to a pointer to function, a lambda of an alias template, a closure type
  as a template argument (linkage of its members), an `auto` parameter
  pack (the abbreviated function template's own limit).  Tests:
  `src/test/cxx/run/lambda-capture-*.cpp`, `sema/gram/lambda-capture.ill-formed.cpp`.
- Status (2026-11-18, step 5 after review): linkage across translation
  units.  A specialization internal only by local classes or closure types
  of an inline or templated function has its template's linkage
  (`specialization_visible`, clang's VisibleNoLinkage; one predicate,
  `visible_local_class`, beside `closure_numbered`'s walk), a function
  returning such a closure is weak, not local, and a static local of a
  function emitted weak is weak (`V`, [dcl.inline]/6): `cnt(mk())` in two
  units shares one counter.  A template parameter is a substitution
  candidate by its position, as clang keys it (`...EiS1_` for a closure of
  a function template).  [expr.prim.lambda.closure]/13: a closure type with
  a lambda-capture (every one before C++20) has a deleted default
  constructor and copy assignment operator.  A capture by reference of what
  an enclosing lambda captured by copy has that lambda's member's type,
  const unless it is mutable (`enclosing_capture_type`).  A closure type's
  copy constructor is mangled, so a capture of a class with a user copy
  constructor compiles.  Helpers in place of copies: `begin_synthesized`/
  `finish_synthesized`, `semantic_tree::set_list`, `this_object`,
  `make_members_contiguous`.  `bench/cxx-run.py` links programs of several
  units (`/* cxx-run-units: */`, `src/test/cxx/run/units/`):
  `vague-linkage.cpp`; cases added to `lambdas.cpp`.  `bench/cxx-sema.py`:
  declarations 55862 -> 55874 as clang, 12 we reject and clang accepts
  (the same), 2467 -> 2464 we accept and clang rejects, all 0 refuted;
  declared types 27332 -> 27327 verified (declarations now rejected as
  clang rejects them carry no claims).  cxx-run 464/464 (116 programs);
  cxx-c-suites 2272 pass, 0 wrong, as before; quality redundant_tokens
  59684 -> 59295, duplicate_runs_across_files 360 -> 353.
- Status (2026-11-18, step 6: range-based for): [stmt.ranged]/1's
  rewrite, built in the semantic tree (`iteration-statement.cpp`,
  `range_based_for`): a block of the init-statement, the unnamed
  exposition variables `auto &&range`, `auto begin`, `auto end`
  (`exposition_variable`, deduced by step 1's `deduced_initialization`),
  and a for statement whose condition, step and element are `begin !=
  end`, `++begin` and `*begin` through the operator functions or the
  built-in operators (`increment_of`, `indirection_of`, split out of
  their syntax-driven callers); the for-range-declaration is declared
  by `declare_with_initializer` with the element as its initializer
  (its `supplied` parameter).  begin-expr and end-expr: an array's `range`
  and `range + N`, a class's members `begin`/`end` (overload
  resolution among them), else `begin(range)`/`end(range)` by
  argument-dependent lookup alone (`range_call`); different begin and
  end types.  Lowering is the existing block and for statement's
  (destructors of the loop variable per iteration, break, continue).
  Test `range-for.cpp`.  Not supported: structured bindings in the
  for-range-declaration (step 7), a braced-init-list as the range
  (std::initializer_list), an attribute-specifier-seq on the
  declaration, member or free `begin`/`end` templates, and any class
  prvalue bound to a reference: a class temporary as the range
  (`for (int x : R{})`, with a trivial R too) and `const E &` bound
  to a class prvalue from `*begin` (both refused at reference_binding's
  [class.temporary]).  A range of other than array or class type
  takes `begin(range)` by argument-dependent lookup (an enumeration's
  namespace); none found, as for `int *`, is ill-formed, as clang
  reports.  A for-range-declaration defining a class or enumeration
  is ill-formed ([stmt.ranged]/2).  2026-11-18 review: the supplied
  initializer a parameter, `binary_of` and `indirection_of` shared with
  the expression paths; declarations 55926 -> 55935 as clang (types
  69149 -> 69145, constants 27979 -> 27975: claims of declarations now
  rejected as clang rejects them), the rest unchanged.
  `bench/cxx-sema.py`: declarations 55874 -> 55926 as clang, 12 we
  reject and clang accepts (the same), 2464 we accept and clang rejects
  (the same), all 0 refuted; declared types 27327 -> 27328, types
  69136 -> 69149, lookup 20986 -> 21395, symbols 2456 -> 2483 verified;
  constants 27981 -> 27979 (for-range-dereference.cpp: its functions
  now rejected as clang rejects them carry no claims); lowering 435 ->
  437 files.  cxx-run 468/468 (117 programs); quality unchanged.

- Status (2026-11-18, step 7: structured bindings): `structured-binding.cpp`
  (`structured_binding_declaration`, called from `init_declarators` and
  `for_range_declaration`): the bindings' names bound before the
  initializer (/2: naming one there is ill-formed), e declared by
  `binding_variable` (step 1's `deduced_initialization`; an array with
  no ref-qualifier copied element by element from an unnamed reference),
  then /3 arrays, /4 tuple-like types (`std::tuple_size<E>` instantiated,
  `value`; `get<i>` in E's associated namespaces, every template of the
  name a candidate with i as explicit argument through
  `template_candidates`' new `specified`; `std::tuple_element<i,
  E>::type`; r_i of type Ti& or Ti&&) and /5 classes (direct members or
  one base's, access checked at the declaration).  A binding named
  (`structured_binding_use`) is rebuilt as `*(e + i)`, `r_i` or `e.m_i`;
  decltype of it is the referenced type ([dcl.type.decltype]/1.1).  e of
  static storage duration (static, namespace scope) mangled `DC
  <source-name>+ E` ([abi:mangle.name]; `_ZDC1a1bE`, `_ZN1nDC1g1hEE`,
  `_ZZ3usevEDC1m1oE`, clang's symbols).  Lowering: a const object of
  static storage duration initialized dynamically is no longer emitted
  read-only (`const P q = p;` faulted).  Tests `structured-bindings.cpp`,
  `structured-bindings-tuple.cpp`, `structured-bindings-static.cpp`.
  Not supported: capturing a binding in a lambda; thread_local; C++26
  constexpr bindings; with static storage duration an array copy, /4 and
  /5 members of a base or not public; a member `get` (member templates,
  [temp.mem]) or one only a hidden friend declares; an incomplete
  `std::tuple_size<E>` when a declaration naming tuple_size was not
  analysed; anonymous union members, virtual bases.
  `bench/cxx-sema.py`: declarations 55935 -> 56000 as clang, 12 we reject
  and clang accepts (the same), 2464 we accept and clang rejects (the
  same), all 0 refuted; types 69145 -> 69364, constants 27975 -> 28001,
  lookup 21395 -> 21465, symbols 2483 -> 2496, layout 15020 -> 15025,
  lowering 437 -> 439 files.  cxx-run 480/480 (120 programs);
  cxx-c-suites 0 wrong; quality unchanged.
  After review (2026-11-18): std::tuple_size/tuple_element found in an
  inline namespace of std (libc++'s std::__1: a tuple-like type was bound
  by its members, wrong code), get<i> in the inline namespaces of the
  associated ones ([basic.lookup.argdep]/2, `associated_namespaces`
  shared with ADL); a const object whose class has a destructor that
  runs is emitted writable ([class.dtor]/5, clang's .data); `auto [x, x]`
  ill-formed (redefinition) except C++26's `_`; the binding decltype
  reads kept in `expression_meaning`.  Test
  `structured-bindings-inline.cpp`.  Still not supported: copying a class
  with an rvalue-reference member is accepted though its implicit copy
  constructor is deleted ([class.copy.ctor]/10; the class copies its
  bytes, the fix is not local); a mutable member ([dcl.stc]).
  cxx-sema unchanged; cxx-run 488/488 (122 programs).

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
- Plan (2026-11-18): stubs first.  The final interfaces and files land
  before the rules, each body answering `unsupported` with its subclause,
  so that `sema.h` is settled once for the sessions working beside S3 and
  S4.  Three commits, on top of S3's commit of its shared surface (`sema.h`,
  the call node of `expression/postfix-expression.cpp`, `overload/`); each keeps cxx-run and
  cxx-c-suites where they are, cxx-sema's "we reject, clang accepts" at 0,
  and moves `unsupported` only between labels.
  1. One entry point for constant expressions (S6a).  There is none today:
     each site reads `expression_meaning::evaluation` itself.
     `constant_expression` and `converted_constant_expression`
     ([expr.const]/10) in one block of `sema.h`; array bounds, case labels,
     enumerators, bit-field widths, GNU `aligned`, constexpr variables and
     list-initialization's narrowing go through them with no change of
     behaviour; `static_assert` (reported as [dcl.pre] today), `alignas`,
     `noexcept(expr)` and `if constexpr`, which are not analysed, get a
     stub under their own subclause.
  2. The evaluator's skeleton (S6a).  A value type in place of one
     `uint64_t`: integer (128 bits), floating (long double through
     `common/softfloat.h`), pointer or lvalue designator, array, class
     object, union, reference; the integer and double cases filled, the
     others `unsupported` [expr.const].  New files beside
     `expression/constant-expression.cpp`: the dispatch over every `semantic_kind`, a
     store for locals and parameters, the step and depth limits of
     [implimits] (sema has none).  `constexpr` functions declarable (the
     rejections at `simple-declaration.cpp` and `class-specifier.cpp` go, the entity is
     marked, a call asks the evaluator); `consteval` and `constinit` stubs.
     Lowering's `constant_initializer` takes the evaluator's value: a
     `call` marked constant would be lowered as a call in static data
     today.  A `constant-claims` dump kind, apart from `type-claims`' fifth
     field.
  3. The template skeleton (S6b).  Analysis has no template code: `template/`
     is the parser's, `template_entity`, `template_specialization` and
     `pack` are unused, and a template's name is bound to
     `entity_kind::none`, which makes every later use of it unknown.  The
     name bound to its entity; dependent, template-parameter and
     placeholder kinds in `type_kind`; `sema/instantiation.cpp` with 3.5's
     record as an interface; one semantic function per subclause
     ([temp.param], [temp.arg], [temp.names], [temp.res], [temp.dep],
     [temp.inst], [temp.explicit], [temp.expl.spec], [temp.class.spec],
     [temp.deduct], [temp.variadic], [temp.alias], [temp.deduct.guide],
     [expr.prim.fold]); the reports under another subclause corrected (a
     qualified template-id as a type is [dcl.type.simple] today, one as a
     base [class.derived], a partial specialization's head [class.pre]);
     stubs for `<template-args>` and `<template-param>` in the mangler.
  After them: S6a's statements, locals, loops, non-member constexpr
  functions, arrays and aggregates need nothing more of S3; constexpr
  constructors, member functions and `this` wait for S4; S6b's rules for
  S3 and S5.  3.3 is not implemented (analysis runs after the whole
  translation unit is parsed, and bodies are parsed in full with the
  parser's guesses about dependent names): the skeleton keeps that and
  analyses a template's parsed body again per instantiation; analysis per
  declaration is a change of its own, as S2's status says.
- Status (2026-11-18): commit 1 done, on S3's 1fad9cdb.
  `constant_expression` ([expr.const]/11), `integral_constant_expression`
  (/8, with C++03's narrower rule), `converted_constant_expression` and
  `bool_constant_expression` (/10) in `expression/constant-expression.cpp`; array bounds,
  case labels, enumerators, bit-field widths and GNU `aligned` ask them
  (a constexpr variable's initializer is an `initialized_value`, not an
  expression: commit 2 moves it).  A noexcept-specifier is reported as
  [except.spec] and an alignment-specifier among the decl-specifiers as
  [dcl.align] (a leading one is still [dcl.attr]); `if constexpr` already
  was [stmt.if].  `static_assert_declaration` exists and answers
  [expr.const]: with [dcl.pre]/6 enforced cxx-sema had 11 declarations we
  reject and clang accepts and 310 refuted claims, none from the rule
  itself: _Complex literals typed double, `__has_builtin` and
  `__has_cpp_attribute`, `alignof` of a class with an aligned attribute,
  `static_cast` of a floating value to an enumeration, `sizeof` of an
  array whose bound an earlier declaration gave.  Those are S6a's first
  work: [expr.const] is now third in cxx-sema's list (10841 declarations,
  static_assert's).  cxx-sema 0 we reject, 0 refuted (types 33262,
  constants 13335, lookup 10063 verified); cxx-run 124/124; cxx-c-suites
  2206 pass, 0 wrong, the 8 lost passes S3's commit has (`#pragma pack`).
  Commit 2 done: the evaluator's skeleton.  `constant_value` (integer to
  128 bits, floating, long double, pointer, lvalue, array, class object,
  union object) and [implimits]/2's two quantities in `sema.h`;
  `expression/constant-evaluation.cpp`: `evaluate_constant`, a case for every
  `semantic_kind` (a node analysis folded gives its value, integer or
  double; the others unsupported [expr.const]), `evaluate_call` and
  `evaluate_statement` with the depth and full-expression counters.
  `constant_expression` and a constexpr variable's initializer ask it for
  what analysis did not fold.  Non-member constexpr functions are
  declared and called ([dcl.constexpr]/1: inline; /3's rules on the body
  not checked), their invocations `unknown` rather than `not_constant`; an
  initializer of a variable of static storage duration that invokes one
  is unsupported [basic.start.static], since /2 makes it constant
  initialization when it can be (clang folds `int g = square(7);`).
  Constexpr member functions stay unsupported (S4), as do `consteval`
  ([dcl.constexpr]) and `constinit` ([dcl.constinit]).  Not done, for
  when the evaluator gives values: lowering a value as static data, and
  a `constant-claims` dump kind (constants ride on `type-claims`).
  `src/test/cxx/run/constexpr-functions.cpp`.  cxx-sema: declarations as
  clang 26169 -> 26813, 0 we reject, 0 refuted (types 40156, constants
  15574, lookup 11445, symbols 1461 verified), we accept and clang
  rejects 447 -> 511 (the bodies' rules); cxx-run 128/128; cxx-c-suites
  unchanged.
  Commit 3 done, after S3's operators (cb2daf6d): the template skeleton.
  `type_kind` has `template_parameter`, `dependent` and `placeholder` (no
  analysis makes them yet); `template/declared-templates.cpp` gives
  template-declarations, explicit specializations and instantiations and
  deduction guides their functions, each answering by what it declares
  ([temp.class], [temp.fct], [temp.alias], [temp.concept], [temp.mem],
  [temp.pre] for a declarator's, [temp.expl.spec], [temp.explicit],
  [temp.deduct.guide]), and `deduce_from_call` ([temp.deduct.call]);
  `sema/instantiation.cpp` and `sema.h` have 3.5's record (key, inputs,
  result) and `instantiate` ([temp.inst]), called by nothing yet.  One
  change from the plan above: a template's name is not bound to a
  `template_entity`.  36 places treat a held name (`entity_kind::none`) as
  unknown, and a use of a template's name stays unsupported either way;
  instead hold_names marks it `held_by_template`, and a use of it is
  [temp.names] rather than [basic.lookup.unqual].  A qualified
  template-id as a type, a template-id base and one in a
  nested-name-specifier are [temp.names]; a specialization's class-head
  [temp.spec].  The mangler's `<template-args>` are left for the first
  instantiation.  cxx-sema's list now starts [temp.class] 16779,
  [basic.lookup.unqual] 11965 (17102 at S2), [expr.const] 11002,
  [temp.names] 9666, [temp.fct] 5023; with S3's operators, declarations
  as clang 27232, 0 we reject and clang accepts, 0 refuted; cxx-run
  132/132; cxx-c-suites unchanged.
  Then [dcl.pre]/6 on, with what enforcing it had shown wrong: GCC's
  imaginary constants (`0.0j`, typed as without the suffix) unsupported
  [ext.gnu]; `__has_builtin` of the builtin templates
  (`__make_integer_seq`, `__type_pack_element`, ...: clang's
  BuiltinTemplates.td, added to `gen-cplusplus-data.py`'s list and their
  rows to `feature-checks.inc`, asked of clang 23.1.4 as the rest was of
  23.1.6) and `__has_cpp_attribute` of `__name__` spellings (`gnu::__const__`,
  `__gnu__::const`) answer as clang (the C preprocessor's `-E` still does
  not: a separate path); a typedef-name of a declaration analysis did not
  follow no longer finds the class its specifier defined (`typedef struct
  S {...} S __attribute__((aligned(8)))`); a floating value cast to an
  enumeration whose underlying type is bool truncates ([conv.fpint]/1:
  0.5 is false, 2.5 has no value, as clang 23; CWG1094 changes it); a weak
  variable is not usable in constant expressions (clang); lookup claims
  name an entity only from its first declaration (a static data member's
  in-class one has no declared name, so its definition was claimed).
  cxx-sema: declarations as clang 27232 -> 27704, 0 we reject and clang
  accepts, 0 refuted (types 39708 -> 47474, constants 15419 -> 18879,
  lookup 11524 -> 12628 verified); [expr.const] is out of the first ten
  (static_assert's 11002); cxx-run 132/132; cxx-c-suites unchanged.
  The evaluator (S6a), on 9cc43621: `expression/constant-evaluation.cpp` runs
  non-member constexpr functions on values of the integer types to 64
  bits, bool, enumerations, float and double: parameters and locals in one
  frame per invocation, reads of variables usable in constant expressions,
  conversions (through `arithmetic_value`), the unary, binary, logical,
  conditional and comma operators with [expr.pre]/4's undefined cases not
  constant (overflow, division by zero, a shift out of range), assignment,
  compound assignment and increment on the frame's objects; block, if, while,
  do, for, break, continue, return, within [implimits]/2's limits.  Not
  modelled, unsupported [expr.const]: pointers, references, arrays,
  classes, member functions, switch, goto, static locals, long double and
  128-bit integers, a signed left shift of a negative value (the rule
  changed in C++20).  Inside an evaluation analysis's `not_constant` does
  not decide ([expr.const]/5.8, /5.16), only at the outermost node.  A body
  analysed in part is marked on its `function_body` node and not run.
  References and pointers into one array (libc++'s `__compute_comp_type`):
  a reference is the pointer to the object it is bound to (`bind_reference`
  of a glvalue, a reference parameter or local), `[conv.array]`, `P + J`,
  `P - J`, `P - Q`, the comparisons, `++`/`--`, `[]` and `*` on a pointer
  to an element of an array (the range-based for statement's `range`,
  `range + N`, `begin != end`, `++begin`); out-of-range moves, the null
  pointer and one past the end are not constant; a pointer to a single
  object moved, a reference to a temporary and `p += n` stay unsupported.
  Test `constexpr-array-references`.
  The value goes back: `constant_expression` sets the meaning's value
  (array bounds, enumerators, case labels, static_assert), and a constexpr
  variable or a variable of static storage duration whose initializer
  invokes a constexpr function gets a `constant` node in its place, which
  lowering emits as data ([basic.start.static]/2: `int g = square(7);` has
  no dynamic initializer).  The folds are the evaluator's own, written
  apart from `arithmetic()` (binary-expression.cpp is S3's while it works
  there); sharing them is for after S3.  Shared since, on S3's b9a0ee64:
  `expression/folding.cpp` has `fold_binary` and `fold_unary` on values as
  `expression_meaning` keeps them, which `arithmetic()`, `comparison()`,
  the unary operators and the evaluator (its operators, compound
  assignment and increment) all call; cxx-sema, cxx-run (156/156) and
  cxx-c-suites unchanged.  The evaluator's values are claims too: with -fcxx-dump=type-claims,
  `evaluate_analysed_expressions` evaluates each analysed prvalue of a
  well-formed declaration that analysis left unknown, and the claim's
  value field carries what it found, which cxx-sema has clang check as
  `static_assert((E) == VALUE)` (no separate constant-claims kind: the
  existing claims had the place and the check).  The first run refuted
  one value: a constexpr function's `static_cast<E>(2.5)` to an
  enumeration whose underlying type is bool, whose truncation to 2 analysis
  makes not constant and the tree did not say; the cast's step to bool is
  now an integral conversion, which the evaluator gives no value outside 0
  and 1.  cxx-sema: constants 18887 -> 19048 verified, 0 refuted; we
  accept and clang rejects 520 -> 514.
  [dcl.constexpr]'s rules on declarations and bodies, as clang 23
  enforces them (probed per edition): /1, every declaration of a function
  has constexpr or none does; C++11 (before N3652, `relaxed_constexpr`): no
  iteration statement in the body and no void return type; until C++23
  (P2448R2, `constexpr_without_constant_result`): a function that returns
  a value has a return statement (/6, flowing off the end is never
  constant).  Not yet: /1 on a declaration that declares no variable or
  function (`constexpr int;`, in simple-declaration.cpp where S3 works); /3.5.3's
  variable of non-literal type (no literal-type test yet); and clang's
  "never produces a constant expression" (/6 for any argument values),
  which is behind its errors on static and thread_local variables, goto,
  asm, throw and C++11's modifications: it needs the evaluator to run with
  unknown parameters.  cxx-sema: declarations as clang 28590 -> 28608, 0
  we reject and clang accepts, 0 refuted; we accept and clang rejects
  514 -> 496; cxx-run 156/156; cxx-c-suites unchanged.  cxx-sema: declarations as clang
  27704 -> 27826, 0 we reject and clang accepts, 0 refuted (types 47348,
  constants 18837, lookup 12628 verified); cxx-run 132/132 with
  constexpr-functions.cpp's constant uses; cxx-c-suites unchanged.
  The evaluator on review (constness throughout, a call of a function
  defined after the expression unknown, a read of a non-const global not
  constant), then switch statements and arrays, on S3's f436482e.  A switch
  ([stmt.switch]/5) finds its case or default label in its body
  (`switch_target`, not into a nested switch) and runs the body seeking it:
  statements before the label are passed over, declarations still declare
  ([stmt.dcl]/2), a label inside another statement is unknown; its break
  ends it, a continue goes on to the loop.  Arrays of the evaluated types,
  nested too, live in `constant_evaluation::elements`, an object's value
  holding its range: a local array initialized by a list, zero or nothing
  ([dcl.init.aggr]/5, [basic.indet]), a constexpr one (global, or local
  read outside its function) evaluated from its definition on first use
  and not modifiable ([expr.const]/5.16); `E1[E2]` on an array converted
  to a pointer designates an element, an index outside the array not
  constant ([expr.add]/4).  Pointers, string literals and arrays passed to
  functions stay unknown.  `src/test/cxx/run/constexpr-switch-arrays.cpp`.
  cxx-sema: declarations as clang 29354 -> 29407, 0 we reject and clang
  accepts, 0 refuted (constants 19550 -> 19634); cxx-run 176/176;
  cxx-c-suites unchanged.
  Then [dcl.constexpr]'s remaining rules but one.  /1: a declaration
  with constexpr and no declarator is ill-formed (`constexpr int;`).  /6,
  clang's "never produces a constant expression" (-Winvalid-constexpr, an
  error by default until C++23, `sema_constexpr_never_constant`):
  `potential_constant` evaluates the body once with the parameters
  opaque, a read of one unknown.  What fails before depending on them
  fails for every argument; an unknown operand does not stop the operands
  beside it (a binary operator's, a call's arguments) nor an unknown
  condition both arms of a conditional, as clang's evaluation goes on.
  Reads of indeterminate or volatile objects do not count there (clang
  does not), and a call of a function with C language linkage is unknown
  (a library builtin clang may evaluate).  The evaluator also gained
  [expr.const]/5.2 (C++23)'s passing through a static or thread_local
  local that is not usable in constant expressions.  Not yet: /3.5.3's
  variable of non-literal type (no literal-type test), and what /6
  catches through classes, pointers, references and new/delete.
  cxx-sema: declarations as clang 29407 -> 29470, 0 we reject and clang
  accepts, we accept and clang rejects 500 -> 477, 0 refuted; cxx-run
  176/176; cxx-c-suites unchanged.
  Literal types ([basic.types]/10, `basic/literal-types.cpp`):
  `literal_type` answers unknown where analysis cannot tell (an implicit
  default constructor or a defaulted destructor, whether constexpr; an
  incomplete class), and only a sure non-literal type is an error: until
  C++23 a constexpr function's return and parameter types (P2448R2) and
  the variables its body defines (/3.5.3, P2242R3).  A user-provided
  destructor not declared constexpr, a non-aggregate whose constructors
  are none of them constexpr (copy and move aside), a volatile or
  non-literal member or base make a class non-literal.  cxx-sema:
  declarations as clang 29470 -> 29473, 0 we reject and clang accepts,
  we accept and clang rejects 477 -> 474, 0 refuted; cxx-run 176/176;
  cxx-c-suites unchanged.
  Rebased on S4's R0 and R1 (8bcd463d): R0's placeholder that made a
  static initializer invoking a constexpr function unsupported
  [basic.start.static] goes, the evaluator decides it (a constexpr member
  function's call is unknown to it, so unsupported [expr.const] still);
  /6's check is not made in a system header, as clang's is not (libstdc++
  declares constexpr functions on builtins), which needed a line marker's
  flag 3 kept (`source_files::in_system_header`).  cxx-sema: declarations
  as clang 30080 (S4's tip alone 28677), 0 we reject and clang accepts, 0
  refuted (types 39366, constants 15708; S4's tip 32273 and 12230);
  cxx-run 184/184; cxx-c-suites 2214 pass, 0 wrong.
  Class objects in the evaluator: constexpr constructors (member
  initializers, then the body) and member functions through `this`,
  members, `*`, `&`, class prvalues as temporaries, copies by value, a
  constexpr variable of class type evaluated and emitted as data
  ([basic.start.static]/2); a non-literal class type of a constexpr
  variable is ill-formed.  Unknown still: bases, unions, bit-fields,
  value-initialization of a class, implicit default constructors.  An
  implicit special member's call is the evaluator's, not "not constant"
  ([class.copy.ctor]/12), and a member of array type takes the object's cv
  on its elements ([basic.type.qualifier]/3).  cxx-sema: declarations as
  clang 30185, 0 we reject and clang accepts, 0 refuted (types 39465,
  constants 15796); cxx-run 188/188 (constexpr-classes.cpp);
  cxx-c-suites 2214 pass, 0 wrong.
  Pointers in the evaluator, on the evaluator above: assignment,
  `p += n` and `p -= n` on a pointer object; a single object is an array of
  one element ([basic.compound]/3.3), so `&x + 1` is a pointer one past it
  (not dereferenceable, comparable with `&x`, `e - 1` back); `&a[i]` is a
  pointer into its array, `a[i]` and `*p` as before; variables of static
  storage duration are objects of the evaluation on first use (a usable
  scalar with its value, a constexpr pointer or object evaluated from its
  definition, any other with an address and no readable value:
  [expr.const]/5.8; volatile, weak and thread-local ones too), so `&g`, a
  reference to a global and a constexpr pointer variable
  (`constexpr const int *p = a + 1;`, `constexpr const char *s = "abc";`)
  are constant; a string literal is one object per literal for the whole
  evaluation (it stays when the scope that evaluated it ends:
  [lex.string]/9; what lies below it stays too), and the comparison of the
  address of a literal with another pointer is not constant, as clang's.  A
  pointer carries the serial of its object (`constant_value::stamp`), so
  the slot a later block reuses is not taken for the object that went (two
  dead locals' addresses compare unknown, not equal).  A constexpr
  variable of pointer type is checked by `evaluate_variable`
  (`permitted_result`, [expr.const]/13: static storage, a literal, null;
  the address of a local constexpr variable is not), and lowering emits its
  read as its initializer (clang folds it; the variable is not emitted for
  a read).  Offsets of unsigned types above what a count holds are not
  constant ([expr.add]/4), a member call on a null or one-past-the-end
  pointer neither, and a null argument passed to a function with
  `nonnull` is unknown (`gnu_nonnull`; clang: not constant).  Test
  `constexpr-pointers.cpp`.  cxx-sema: declarations as clang 83682 ->
  83899, unsupported 40598 -> 40383 ([expr.const] 3333 -> 3108), 0 refuted
  more (constants 23); we accept and clang rejects 4260 -> 4256; the two
  more "we reject and clang accepts" are clang's own crash on
  SemaCXX/builtins.cpp (its verdicts after line 61 are none); verified
  claims fall by the declarations now diagnosed (`static_assert(&x == &y)`
  and the like); cxx-run 770/770 programs; cxx-c-suites unchanged (3023
  pass, same as before).  Still unknown: `new` and `delete`, unions,
  bit-fields, `goto`, base classes and derived-to-base pointers, arrays of
  class type, destructors, member pointers, long double and 128-bit values,
  pointer results lowered as data.

  The evaluator and lowering, consolidated (a review found most of what
  the evaluator refused was in the tree already, not missing from it):
  `element_destructor` and `initializer_member` are semantic_analysis's, one
  answer for lowering and the evaluator; zero-initialization of classes with
  bases and `construct.op` ([dcl.init]/8.2); base subobjects (pointer and
  glvalue conversions both ways are moves between element places, the
  implicit constructors and copies run); an object made again at its place
  keeps its elements (a copy after a constructor initialized the members was
  corrupt: the members' storage was above the invocation and went with it);
  implicit special members run their synthesized definitions, a class or
  array assignment is member by member (`assign_value`); destructors (locals
  and by-value parameters at scope end, last first, through break, continue
  and return; temporaries at the end of the full-expression of an
  expression statement, a return or a declaration; a destructor's members
  and bases after its body; a temporary elsewhere stays unknown); unions (one
  slot, the active member, activation by assignment or constructor, copies)
  and bit-fields (modulo 2^width on every store); constant pointers as tree
  data (`pointer_tree`: `&x`, array plus index, a literal decayed, null),
  so a pointer a constexpr function returns is constant-initialized; virtual
  calls (`virtual constexpr` is declared; the final overrider by the dynamic
  class's table slots, `this` the overrider's subobject); `new` and
  `delete` (a heap object until freed, the destructor run, a leak makes a
  constexpr variable's initializer not constant; the allocation function is
  not called).  `goto` is not constant, as clang's.
  `-DVCC_TRACE_UNKNOWN` prints each refusal's line.
  Not done: array `new` (analysis refuses it), virtual bases,
  `dynamic_cast`/`typeid`, member
  pointers, long double, arrays of unknown shape from brace elision (an
  analysis bound bug it showed: `Y b[] = {1,2,3,4,5,6}` with a default
  member initializer has bound 6).  cxx-sema (default suites): unsupported
  40598 -> 39978, declarations as clang 83682 -> 84266, 0 refuted more;
  `--suites all` adds CodeGenCXX, Parser and Sema (1890 runs, no crash: 42
  more declarations as clang, nothing new refused or refuted); cxx-run
  778/778; cxx-c-suites unchanged.

  S6b's first rules: function templates.  A namespace-scope
  function-definition after `template<class T, typename U>` (type
  parameters, no defaults, packs or constraints) is a `template_entity`
  whose type is its function type over `type_kind::template_parameter`
  types (its declarator analysed with each parameter's name a typedef of
  one), recorded in `function_templates`; its name stays the one
  hold_names binds.  A call by that name (`template_call`, postfix-expression.cpp)
  analyses the arguments, deduces ([temp.deduct.call]/2-3, forwarding
  references; `deduce_type` for T, cv T, pointers, references, arrays and
  function types) and instantiates (sema/instantiation.cpp): the
  function-definition analysed again in a scope where each parameter
  names its argument, the caller's state kept aside, declare() making a
  function of the template's namespace that no name binds
  (`declare_specialization`), inline (emitted where used, weak), with the
  parameters' names bound in it for its body.  One record per template
  and argument list (3.5's `instantiations`), so recursion finds its own
  specialization.  Itanium names: `<template-args>`, the template's
  return and parameter types with `T_`, the template name as a
  substitution candidate (symbols as clang's: `_Z1hIidET_PS0_RKT0_S0_`).
  Errors in a specialization's body make the template's declaration
  ill-formed, as clang reports them; a constexpr specialization that
  breaks /3's rules is still constexpr but not constant
  ([dcl.constexpr]/7); a name the body finds among a namespace's
  declarations after the template makes the lookup unknown ([temp.res]/10,
  no two-phase lookup yet); `T::x` is unsupported [temp.res].  Unusable
  (unsupported as before) when the name is declared again (an overload,
  an explicit specialization) or argument-dependent lookup finds other
  functions.  Not yet: explicit template arguments, qualified calls
  (`N::f(x)`), declarations without a body, member templates, class
  templates.  cxx-sema: declarations as clang 30185 -> 31651, 0 we reject
  and clang accepts, 0 refuted (lookup 14031, symbols 1675); we accept and
  clang rejects 482 -> 1081 (templates clang rejects at their definition,
  whose rules on non-dependent constructs are not checked before an
  instantiation); cxx-run 192/192 (function-templates.cpp); cxx-c-suites
  2214 pass, 0 wrong.  ASan over SemaTemplate and temp.deduct.call: clean
  but for a use-after-free in the parser's recovery
  (`parser::unparsed_declaration`, SemaTemplate/explicit-instantiation.cpp),
  which is older than this.
  Then explicit template arguments and qualified calls: `template_callee`
  (postfix-expression.cpp) takes `f`, `f<A...>`, `N::f` and `N::f<A...>` (type
  arguments, [temp.arg.type]/1; a namespace's template found by
  [namespace.qual], no argument-dependent lookup); deduction starts from
  the arguments given and leaves out the parameters they determine, whose
  arguments convert ([temp.arg.explicit]/7).  A specialization's
  declaration and definition are now instantiated apart ([temp.inst]/5):
  the declaration where a call names it, the definition at once for a
  constexpr template and otherwise after the translation unit's
  declarations ([temp.point]/7, as clang), and not for a call in an
  unevaluated operand (sizeof) or a default argument (`unevaluated`).  A
  definition analysis has no rules for makes the template's declaration
  unsupported.  cxx-sema: declarations as clang 31770, 0 we reject and
  clang accepts, 0 refuted (types 40137, lookup 14272, symbols 1723); we
  accept and clang rejects 943; cxx-run 192/192; cxx-c-suites 2214 pass,
  0 wrong.
  Then class templates of namespace scope with type parameters
  (`class_template_definition`, declared-templates.cpp): a template-id names a
  specialization (`template_type`, from a decl-specifier or after a
  nested-name-specifier), instantiated there (`instantiate_class`,
  instantiation.cpp) by analysing the class-specifier again with the
  parameters bound to the arguments, the injected-class-name bound to the
  specialization ([temp.local]/1); member functions are defined as the
  class's.  A template-id with a dependent argument in a template's own
  declaration is a dependent type ([temp.dep.type]/9), and `TT<T>`
  parameters deduce from a specialization's arguments.  Mangling: the
  template name as a candidate, `S_` for it, `Sa`/`Sb` for std's (not
  `Ss`, `Si`, `So`, `Sd`).  An instantiation's errors make the use
  unsupported, not the template ([temp.inst]/1: clang reports them "in
  instantiation of" at the use).  Not yet: default template arguments,
  non-type parameters, partial and explicit specializations, member
  templates, a member's default arguments ([temp.inst]/3), a function type
  as an argument ([temp.spec]/8), friends ([class.friend]/6: the template
  is unsupported), and instantiation only where a complete type is needed
  ([temp.inst]/2: today a template-id instantiates).  cxx-sema:
  declarations as clang 36772, 0 we reject and clang accepts, 0 refuted
  (types 41066, lookup 14639, symbols 1743, layouts 10142); we accept and
  clang rejects 1862; cxx-run 196/196; cxx-c-suites unchanged.
  Then default template-arguments and non-type parameters of integral
  type, for class and function templates (`declare_template_parameters`,
  `template_argument_for`, `default_template_arguments`, declared-templates.cpp).
  A non-type argument is a converted constant expression
  ([temp.arg.nontype]/2); in an instantiation the parameter's name is a
  constant, as an enumerator (`bind_template_argument`); in the template's
  own declaration it is an entity without a value, so only a template-id
  argument may name it (a value-dependent argument, deduced as
  [temp.deduct.type]/8's `TT<i>`).  Defaults are analysed with the
  arguments before them bound ([temp.param]/12; [temp.deduct]/5 for a
  function template's undeduced parameter), and [temp.param]/14 holds for
  class templates.  Mangling: `L <type> <value> E`, `n` for a negative
  value, `X T_ E` for a parameter.  Not yet: non-type parameters of other
  types (pointers, enumerations, class types), a parameter used in a
  function template's own declarator other than as a template-id argument
  (an array bound), defaults across redeclarations ([temp.param]/13).  The
  evaluator's object of a member call that is a variable from outside the
  evaluation is now unknown rather than not constant ([expr.const]/5.8
  forbids reading it, not naming it).  cxx-sema: declarations as clang
  37781, 0 we reject and clang accepts, 0 refuted (types 41644, lookup
  14684, symbols 1772, layouts 10166); cxx-run 200/200; cxx-c-suites
  unchanged.
  Then partial and explicit specializations of class templates
  (`class_specialization_definition`, `matching_specialization`,
  declared-templates.cpp): each a record of its own (`function_template::primary`)
  with its template-id's arguments in terms of its parameters; an
  instantiation deduces them from its arguments exactly
  ([temp.class.spec.match]/2), an explicit specialization of the arguments
  first, and analyses that class-specifier with the specialization's
  parameters bound (`instantiation_record::pattern`).  Not yet: partial
  ordering when two match ([temp.class.order]), one declared after an
  instantiation of its template, members defined out of the class.  Layout:
  a derived class continues after a base's NonVirtualSize, as clang (an
  empty base moved past the base's data counts), which the specializations
  exposed (SemaCXX/empty-class-layout.cpp).  cxx-sema: declarations as
  clang 38285, 0 we reject and clang accepts, 0 refuted (symbols 1778,
  layouts 10178); cxx-run 204/204; cxx-c-suites unchanged.
  Then template-ids in expressions and after `::` ([temp.names]): a
  simple-template-id naming a class template in a nested-name-specifier
  names the specialization, whose members are looked up there
  (`nested_name_specifier`, name-lookup.cpp: `traits<int>::kind()`,
  `N::q<int>::g()`); `box<int>(...)` and `box<int>{...}` are explicit
  type conversions to the specialization ([expr.type.conv]/1,
  `postfix`); a static data member or static member function reached
  through an object is the member, the object evaluated first as a
  comma's left operand ([expr.ref]/6.1, /6.3.1; not an overloaded one).
  Lowering folds `==`/`!=` of a null pointer and the address of a
  non-weak variable of static storage duration or function, as clang's
  code generator does (otherwise the variable is referenced and emitted).
  Not yet: `template` before a simple-template-id, a dependent
  specialization before `::` ([temp.res]), a qualified template-id in an
  expression's type conversion, member typedefs and static data members
  of class templates (S4).  cxx-sema: declarations as clang 38372, 0 we
  reject and clang accepts, 0 refuted (symbols 1791, layouts 10189,
  lowering 280); [temp.names] unsupported 8297 (from ~9.5k); cxx-run
  208/208; cxx-c-suites unchanged.
  Then declarations before definitions and members defined outside the
  class ([temp.class], [temp.fct], [temp.mem.func]).  `template<class T>
  struct S;` declares the class template (`class_template_declaration`);
  its definition completes the same record, taking the declaration's
  default template-arguments when the parameters' names agree
  ([temp.param]/10), and a specialization named before then is an
  incomplete class that a use after the definition defines
  (`define_class_instantiation`).  `template<class T> T f(T);` declares a
  function template (`function_template::declarator`); a later declaration
  or the definition with an equivalent head and function type
  ([temp.over.link]/6, the same parameter names) is the same template, and
  a specialization used before the definition is defined after the
  translation unit's declarations, or never.  `template<class T> R
  S<T>::f(...) {...}` and a partial specialization's member
  ([temp.class.spec.mfunc]/1) are kept (`template_member_definitions`) and
  analysed for each specialization of the class, made before or after
  (`define_template_member`): the declarator's `S<T>::` names the
  specialization, the member is defined inline (weak, emitted where used,
  as clang's).  Its errors make it unsupported, not ill-formed (clang
  instantiates only the members used, [temp.inst]/4).  A default
  template-argument the arguments make ill-formed is unsupported at the
  use (clang reports it at the default); a template call whose argument
  names a function is unsupported ([temp.deduct.call]/6's overload sets).
  Not yet: out-of-class constructors (the parser keeps them as tokens),
  destructors, operators, member templates, static data members, renamed
  parameters, defaults merged for function templates, a declaration with
  default arguments.  cxx-sema: declarations as clang 38372 -> 40721, 0
  we reject and clang accepts, 0 refuted (types 42619, lookup 14842,
  symbols 1845, layouts 10190, lowering 284); [temp.class] unsupported
  6401 -> under 2196; cxx-run 212/212 (template-members-out-of-class.cpp);
  cxx-c-suites unchanged.
  Then dependent names ([temp.res], [temp.dep]).  A typename-specifier
  (`typename_type`, typename-specifier.cpp) and C++20's implicit typename in a
  simple-type-specifier (`qualified_type_name`) with a dependent
  nested-name-specifier (a template-parameter, a dependent specialization,
  or such a name before it: `T::a::b`) are a dependent type of its own
  (`entity_kind::dependent_member`, one per qualifier and name,
  [temp.dep.type]/6 and /9), so a function template's declarator can use
  `typename T::type`; a non-deduced context ([temp.deduct.type]/5.1), the
  specialization's declarator analysed again with the arguments resolves
  it.  `nested_name_specifier` reports such a qualifier only to a caller
  that allows it.  Mangling: `N T_ <source-name>... E`, each prefix a
  candidate (clang 23: `_Z2g4I1AENT_1a1bES2_S3_S1_`).  [temp.dep]/3: a
  class template specialization's bases whose base-specifier names a
  template-parameter are marked (`entity::dependent_base`), and unqualified
  lookup passes over the class when the name is only in such a base
  (`in_dependent_base_only`; unknown when another base or
  -fms-compatibility could find it): `x` in a member of `D<T> : T` was
  `T::x`, now the global `x` as clang's.  Bodies were already analysed per
  specialization, so `T::value`, `T::f()`, `t.m` and dependent calls work
  there.  Measured before going further: the lookup_horizon approximation
  of two-phase lookup costs 8 declarations in cxx-sema, so binding
  non-dependent names at the definition ([temp.nondep]) and ADL at the
  point of instantiation ([temp.dep.candidate]) are not done; the
  [basic.lookup.unqual] count (14.6k) is names whose declarations analysis
  has no rules for, not templates'.  Not yet: `typename
  T::template X<...>`, `struct T::X`, a dependent qualified name as an
  expression in a template's own declarator (an array bound, a default
  argument), member templates (S4).  cxx-sema: declarations as clang
  40790, 0 we reject and clang accepts, 0 refuted (types 42663, lookup
  14907, symbols 1845, layouts 10190); cxx-run 216/216
  (template-dependent-names.cpp); cxx-c-suites unchanged (the #pragma pack
  tests pr109318 and pr79737-1 not yet, [cpp.pragma]).
  Then explicit instantiations ([temp.explicit]) and explicit
  specializations of function templates ([temp.expl.spec]).
  `named_specialization` (declared-templates.cpp) finds the function template's
  specialization a declaration names: its declarator-id the template's name
  or a simple-template-id of it (`declarator_template_id` lets
  declarator() take one), the arguments not given deduced from the declared
  function type ([temp.deduct.decl]/1), which shall be the
  specialization's.  `template int f<int>(int);` and `template long
  f(long);` instantiate it, `template struct S<int>;` the class
  (`class_explicit_instantiation`), `template void S<long>::g(long);` one
  member function (`member_explicit_instantiation`); the subject is marked
  (`entity::explicitly_instantiated`, a member function through its class,
  not the special members, [temp.explicit]/10), and lowering emits it
  whether used or not, weak (clang's weak_odr).  `extern template` (/11)
  declares it: no definition emitted, not even an inline one, as clang.
  `template<> int f<int>(int) {...}` (or deduced) makes the specialization
  with its own inline and constexpr ([temp.expl.spec]/12), its first
  declaration the explicit specialization (lookup claims name it), never
  instantiated; declared first and defined later works.  Unsupported (as
  clang's errors or not known): an explicit specialization after a use, a
  duplicate explicit instantiation, one after an explicit specialization, an
  explicit instantiation declaration after a use, an undefined template,
  deleted or defaulted specializations, members of class templates
  explicitly specialized, member templates, static data members, overloaded
  templates.  An increment whose operand is not a variable is no longer
  "not constant" before evaluation (a reference a constexpr call returns
  may designate a temporary of the evaluation, [expr.const]/5.16).
  cxx-sema: declarations as clang 40790 -> 41545, 0 we reject and clang
  accepts, 0 refuted (types 42846, lookup 14998, symbols 1904, layouts
  10241); [temp.explicit] unsupported ~3.8k -> 1569, [temp.expl.spec] 1695;
  cxx-run 220/220 (template-explicit.cpp); cxx-c-suites unchanged (the
  #pragma pack tests not yet, [cpp.pragma]).
  Then variadic function templates ([temp.variadic], [expr.prim.fold];
  template/variadic-template.cpp).  A function template's last template-parameter
  may be a type parameter pack (`class... Ts`); its function parameter
  pack (`Ts... args`, `const Ts &...rest`, the last parameter) has type
  `type_kind::pack_expansion` of the pattern in the template's own
  declaration, and the pack named anywhere else there is unsupported.
  Deduction ([temp.deduct.call]/1) deduces the pattern against each
  trailing argument; the pack's argument is a `template_argument` of kind
  pack whose elements are stored once (`make_pack`, so equal packs are the
  same key), an empty one when nothing deduces it ([temp.arg.explicit]/4).
  An instantiation binds the pack's name to an entity of kind pack of its
  elements' typedef-names; the declarator's parameter pack becomes one
  parameter per element (`parameter_pack_expansion`, parameter-declaration-clause.cpp), whose
  name the body finds as a pack of their variables.  A pattern is analysed
  once per element (`expanding`, `expansion_element`: declared_in gives the
  element for a pack's name, a pack named outside a pattern is unknown).
  Pack expansions in a call's arguments (`g(args...)`, `g(h(args)...)`,
  mixed with other arguments; the parser now keeps the `...` as the
  initializer-list's child), `sizeof...` of either pack ([expr.sizeof]/4)
  and fold expressions, unary and binary, left and right, over the
  built-in operators of [expr.mul] to [expr.log.or] and the comma on
  operands of neither class nor enumeration type, with Table 17's values
  for `&&` and `||` of an empty pack ([temp.variadic]/10).  Mangling: `J
  ... E` for a pack's arguments and `Dp <type>` for the expansion
  (`_Z5countIJidcEEiDpT_`, as clang).  Not yet: packs of class templates
  (below), non-type packs (`int... Ns`), explicit arguments for a pack, packs not last,
  overloaded function templates (so no head/tail recursion), pack
  expansions in braced or parenthesized initializers, template-argument
  lists and other contexts, folds over assignment operators, `.*`,
  `->*` or class operands, a nested pack expansion within a pattern.
  cxx-sema: declarations as clang 41545 -> 41733, 0 we reject and clang
  accepts, 0 refuted (types 43363, lookup 15142, symbols 1905, layouts
  10241, lowering 295); cxx-run 224/224 (variadic-templates.cpp);
  cxx-c-suites unchanged.
  Then class templates with a type parameter pack (the last parameter;
  [temp.arg]/1: the arguments from its position on are its elements, an
  empty pack when none) and pack expansions in a template-argument-list
  (`box<int, Ts...>`, the ellipsis after a type-id or as the list's
  child; `type_id_expansion` analyses the pattern), in an instantiation
  only: a template's own declaration naming a pack of dependent
  arguments is unsupported, as are partial specializations of a variadic
  primary or with a pack.  Member functions of such a class may have a
  function parameter pack of the class's pack and use `sizeof...`.
  cxx-sema: declarations as clang 41733 -> 42029, 0 we reject and clang
  accepts, 0 refuted (types 43369, lookup 15144, symbols 1905, layouts
  10243); cxx-run 224/224; cxx-c-suites unchanged.
  Then overloaded function templates and partial ordering ([temp.over],
  [over.match.best]/2.4-2.5, [temp.func.order], [temp.deduct.partial],
  [temp.class.order]).  A function template whose name already names
  function templates of the same namespace is their redeclaration only
  when equivalent ([temp.over.link]/7), otherwise another template of the
  name (`function_template_overloads`); one whose name names the scope's
  functions (no using-directive, inline namespace or unknown names in the
  scope) keeps them as candidates (`template_name_functions`), the name
  held by an entity of its own.  A call (`template_call`) deduces each
  template with `deduce_from_call`'s trial mode: a deduction that fails
  (a parameter count, a conflict, a P and A of other shapes) is no
  candidate, one [temp.deduct.call]/4 might still rescue (a class, a
  pointer to class or function, a multi-level qualification conversion)
  unsupported; each candidate's declaration is instantiated (not its
  definition, only the chosen's), then `best_viable_function` with the
  tie-breakers: a non-template beats a specialization, and of two
  specializations the more specialized template's
  (template/partial-ordering.cpp: per call argument, the parameter's type or
  its pack's pattern; references and top-level cv removed, /9's
  lvalue/cv rule, [temp.deduct.type]/10's pack rule, /11's trailing pack).
  Head/tail recursive variadic templates work.  Two matching class
  partial specializations are ordered the same way (deducing one's
  pattern from the other's).  Unsupported still: an ambiguity or an
  order analysis cannot tell (templates with non-type parameters, types
  with template-ids or dependent names, pointers to members), explicit
  template arguments when the name has several templates, explicit
  specializations and instantiations of such a name ([temp.func.order]/1.4),
  a non-template function declared after a template of its name, taking
  the address of an overloaded template.  cxx-sema: declarations as clang
  42029 -> 42350, 0 we reject and clang accepts, 0 refuted (types 43630,
  lookup 15236, symbols 1910, layouts 10271, lowering 295 -> 298); cxx-run
  228/228 (template-overloads.cpp); cxx-c-suites 2214 pass, 0 wrong.
  Then substitution failure ([temp.deduct]/8).  Each candidate of an
  overloaded name (`template_call`) is deduced and its declaration
  instantiated with `substituting` set: an ill-formed result there (the
  declarator's types, a default template-argument) is no candidate, the
  diagnostics, expressions and name uses it made taken back, the
  instantiation record marked `substitution_failed`; a class defined
  meanwhile is outside the immediate context (`define_class_outside`
  counts its failures, which stay errors).  The one invalid form
  analysis tells yet is /11's member a class does not contain:
  `typename T::type`, `typename only_int<T>::type` (an explicit or partial
  specialization giving the member for some arguments), in the return
  type, a parameter or a default template-argument, the class defined
  and its names all known (else unsupported).  Also: two templates whose
  types differ only in `typename Q::name` are different templates
  ([temp.over.link]/7: deduce_type compares the names and qualifiers);
  an argument of class type for a P that is not a class template-id, or
  a pointer to class for a non-pointer P, is a deduction failure, not
  unsupported; a typename-specifier's template-id no longer holds the
  class template's name in a declaration analysis keeps as names
  (hold_names); `N8only_intIT_E4typeE` as a dependent prefix.  Not yet:
  SFINAE with a single template and no other candidate (an error, as
  clang's "no matching function": unsupported), `enable_if` on a
  constant (static data members, `sizeof` of a dependent type and
  member typedefs are S4's or not analysed), `decltype` in the return
  type, a non-type argument that does not convert, [temp.deduct.call]/4's
  derived-to-base and qualification conversions, [temp.deduct.funcaddr]
  and [temp.deduct.conv].  cxx-sema: declarations as clang 42350 ->
  42357, 0 we reject and clang accepts, 0 refuted (types 43630 -> 43689,
  lookup 15236, symbols 1910 -> 1911, layouts 10271, lowering 298);
  cxx-run 232/232 (template-sfinae.cpp); cxx-c-suites 2214 pass, 0 wrong.

  Then alias-declarations, alias templates and variable templates
  ([dcl.typedef]/2, [temp.alias], [temp.pre]/1, [temp.inst]/7).
  `using X = T;` declares a typedef-name at namespace and block scope
  (not yet in a class: [class.mem]; not with a class or enum defined or
  declared in its defining-type-id, nor with attributes).  An alias
  template of namespace scope (a function_template record with
  `is_alias`) has its defining-type-id analysed once with the parameters'
  types; a simple-template-id naming it (template_type ->
  alias_template_type) is that type-id analysed again where each
  template-parameter's name is its argument, so `ptr<T>` in a function
  template's parameter is `T*` (deduction and mangling see the
  substituted type), and an ill-formed substitution is a deduction
  failure while a candidate is substituted.  A variable template of
  namespace scope (`is_variable`; one init-declarator with an
  initializer, not static or extern) is analysed per specialization at
  its first use as an id-expression (`pi<int>`, variable_template_id ->
  instantiate_variable): its simple-declaration again in the
  instantiation's scope, declare() diverted to
  declare_variable_specialization, which makes an inline variable of the
  template's namespace whose name is not bound (clang's linkonce_odr,
  mangled `_Z4zeroIiE`, weak); a constexpr one is usable in constant
  expressions.  Not yet: an alias template with a pack, a
  cv-qualified alias type named by a template-id (template_type takes no
  cv), redeclarations, member alias templates and static data member
  templates (S4), a variable template named with a qualifier, with
  dependent arguments (in a template's definition), its address as a
  template argument, explicit and partial specializations of variable
  templates, and a variable template whose initializer the evaluator
  cannot fold (a floating `T(3.14L)`).  Also: a C++26 type-aware
  `operator new` (first parameter of class type) is unsupported, no
  longer an error once `size_t` is an alias-declaration.  cxx-sema:
  declarations as clang 42357 -> 43376, 0 we reject and clang accepts, 0
  refuted (types 43689 -> 44005, constants 17851, lookup 15236 -> 15323,
  symbols 1911 -> 1926, layouts 10271 -> 10291, lowering 298 -> 302);
  cxx-run 240/240 (template-aliases.cpp, variable-templates.cpp);
  cxx-c-suites 2214 pass, 0 wrong.

  Then template template-parameters, unnamed template-parameters and
  non-type parameters of other types ([temp.param]/1, /6, /7,
  [temp.arg.template], [temp.arg.nontype]).  A template-parameter's form
  is read once (`template_parameter_form`, variadic-template.cpp: key or
  template-head, ellipsis, identifier or declarator, default), so an
  unnamed one (`template<class, int = 3>`) is a parameter like the
  others whose name is not bound.  `template<class> class C` is an
  entity of kind template_entity with a function_template record of its
  own (`is_parameter`, `is_class`, its own parameters in a scope of
  theirs, made after the template's: declare_template_parameters moves
  the template's index past them); `C<int>` in the template's own
  declaration is a dependent type ([temp.dep.type]/9), and in an
  instantiation `C` names the argument's class template.  An argument
  (`template_template_argument`) is a class template's name, qualified
  or not (a template_argument of kind template_name, the template's
  entity in value), or the template's own template template-parameter
  (template_entity); its parameters must be the same as the parameter's
  ([temp.arg.template]/3's matching beyond that, P0522's included, is
  unsupported), and a default is an id-expression.  [temp.deduct.type]/8's
  `TT<T>` deduces TT from A's primary template under the same rule.
  Non-type parameters may now be of enumeration type (an argument of
  that enumeration, [expr.const]/10), std::nullptr_t (nullptr), a
  pointer to an object or function or an lvalue reference to one (`&x`,
  `x` for an array, a function, or a reference's object: a variable or
  function of namespace scope of exactly the type referred to; the
  argument's value is the entity, 0 for a null pointer).  In an
  instantiation such a parameter's name is an entity without rules for
  its uses but as another template-argument.  Mangling as clang 23: a
  template argument `3box` / `N1N3bagE` (the template a candidate),
  `T_` for a template template-parameter (a candidate), its
  specialization `T_IiE`, an address `XadL_Z1gEE`, a reference `L_Z1gE`,
  a null pointer `LPi0E`, an enumerator `L1E5E`.  Not yet: `auto` and
  class-type parameters ([dcl.spec.auto], structural classes), packs of
  template template-parameters or non-type parameters, a template
  template-parameter's own default arguments or nested template
  template-parameters, alias templates as arguments, partial ordering of
  function templates with template template-parameters, the use of a
  pointer or reference parameter's value in an expression, an
  integral argument for an enumeration parameter (and the reverse).
  cxx-sema: declarations as clang 43376 -> 45710, 0 we reject and clang
  accepts, 0 refuted (types 44005 -> 44343, constants 17851 -> 17931,
  lookup 15323 -> 15354, symbols 1926 -> 1942, layouts 10291 -> 10320,
  lowering 302 -> 308); cxx-run 244/244
  (template-template-parameters.cpp); cxx-c-suites 2214 pass, 0 wrong.

### S7. Exceptions and RTTI

- Front end: [except.throw], [except.handle], [except.ctor], [except.spec],
  `typeid`, `dynamic_cast`, the RTTI objects.
- Back end: landing pads and `.gcc_except_table` for arm64 and x86-64 in
  the walker and in VIR's lowerings; `bench/vcc-cfi.py` extended.
- Until then `-fno-exceptions` is the only mode: `throw` and `try` are
  errors as clang's.
- Status (2026-11-24): try-blocks with handlers, throw-expressions and
  `throw;` are analysed and lowered ([except.pre], [except.handle],
  [except.throw]; doc/PLAN-vir-eh.md E8): `bench/vcc-eh.py` runs
  `src/test/eh/cxx_*.cpp` against clang++ under libstdc++'s unwinder.
  Left: function-try-blocks, exception specifications and `noexcept`
  ([except.spec]), constructors that throw, `typeid`, `dynamic_cast`.
- Gate: C5.3 with exceptions across compilers; C5.4.

### S8. Real code

- libstdc++ and libc++, json's 71 TUs passing their tests, the extensions
  and builtins they need (`ext/`), then PLAN-cxx's measurements: time
  against clang, the instantiation cache (X5), Skia.
- Status: the C++ constant evaluator folds float and double infinity,
  quiet NaN and signaling NaN builtins used by libc++'s `<limits>`, plus
  `__builtin_isinf_sign` and `__builtin_issignaling` on those types.
  Signaling NaNs keep their exact bits in `__builtin_bit_cast` and
  emitted data. A declaration imported through two using-declaration paths
  enters the overload set once ([namespace.udecl]/1, /9), so Skia's
  `SkBlendModeBlender.cpp` gets past the `std::abs(float)` calls in
  `SkPoint.h`. For specializations such as `sk_sp<SkImage>` with an
  incomplete class argument, non-virtual member definitions with known
  return types now wait until used ([temp.inst]/3, /11).
  `SkBlendModeBlender.cpp` gets past `SkRefCnt.h` and first fails on template
  argument deduction in libc++'s `atomic_flag.h`.
- Pointer arguments can deduce template parameters from a derived class's
  base specialization ([temp.deduct.call]/4.3). Skia's libc++ atomic load
  calls get past deduction in `atomic_flag.h` and first fail on a deleted
  overload in `__atomic/support/c11.h`.
- Atomic lvalues retain their type for reference binding and overload resolution,
  and `__builtin_addressof` takes their address. `SkBlendModeBlender.cpp`
  now first stops at a defaulted special member in `SkRefCnt.h`.
- Deleted move constructors are declared and diagnosed when selected by
  overload resolution ([dcl.fct.def.delete]/2). `SkBlendModeBlender.cpp`
  now first stops at deallocation lookup inside `SkRefCntBase`.
- Allocation and deallocation lookup uses the analysed members and bases of
  a laid-out class ([class.mem]/6). `SkBlendModeBlender.cpp` now first stops
  at a defaulted special member in `SkBlender.h`.
- Private and protected defaulted default constructors retain access for
  calls, friends and derived classes. `SkBlendModeBlender.cpp` now reaches
  a friend function template in libc++ `unreachable_sentinel.h`.
- Friend function templates defined in a class instantiate with its lexical
  scope and participate in ADL ([temp.friend]/2, [basic.lookup.argdep]/4.3).
  `SkBlendModeBlender.cpp` now stops in libc++ `__new/allocate.h` on
  [temp.class.spec].
- A refused, parsed class-template specialization holds its class-head name
  without hiding names in its arguments or base clause. `SkBlendModeBlender.cpp`
  now stops at a base-class lookup in `SkSpan.h`.
- A namespace-qualified template-id names a base and its constructor
  initializer ([class.derived]/2, [class.base.init]/3). `SkBlendModeBlender.cpp`
  now stops at a constrained constructor in `SkSpan.h`.
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

## 8. Lessons from the first front end

The first front end (`src/frontend/cxx`, removed in 7283c431) met these on
libc++, Skia and nlohmann/json.  Its run tests are kept as
`src/test/cxx/run/old-*.cpp` (most in `xfail.txt` today).

- Converting-constructor cycle (1cdd55d5): binding a temporary made by a
  converting constructor picked the same constructor again; Skia's
  bentleyottmann (`std::tuple`, `std::get`, `std::tie`) nested 479 binds and
  overflowed the stack.  Fixed by a nesting bound of 16 (cause not found).
  Today: reference binding to converted temporaries is refused
  (`old-converting-bind` xfail); the lowering needs a bound or a cycle check
  when it lands (TODO.md).
- Deep nesting in general: today expressions have the [implimits] bound
  (20000 nested parentheses: a clean error) and instantiation stops at depth
  64 (`sema/instantiation.cpp`).  Handled.
- `const T&` bound to a bit-field took the bit-field's address (5ad83e27);
  180 of Skia's 213 failures (`SkToBool(fBitfields.fAntiAlias)`).  Fix: a
  temporary, [dcl.init.ref].  Today: `old-bitfield-const-ref` xfail ([class.bit]).
- `__builtin_common_type` as a base class (76295ba6): libc++'s
  `common_type` derives from it, so `common_type<int, long>::type` was
  unknown (json's `value_in_range_of`).  Today: `old-builtin-common-type`
  xfail ([temp.arg]); `builtin-common-type` passes.
- Layout of libc++ containers (ae1da9a9): `[[no_unique_address]]` (empty
  takes no space, non-POD-for-layout gives up tail padding), packed classes,
  constant bit-field widths, `__builtin_offsetof`, `__datasizeof`.  Without
  them `std::string`, `vector`, `map` had no size.  Today: checked by
  `bench/cxx-sema.py` class layouts against clang.
- Traits for classes not POD for layout (7e8e60d4): trivially
  destructible/assignable answered only for POD classes, so `optional<T>`
  of a `T` with `~T()` (SkMatrix) chose no base and nothing holding one got
  code.  Today: answered from the special members ([class.dtor]); check with
  `old-nothrow-traits` when it passes.
- Mangling (b02fd567, 83d27fc1, a8e7e70f, f6e7b5c8, 682b96ef): clang 23's
  `Tn` for value parameters of dependent type, `T::name()` in a parameter's
  type, array bounds and `_Float16`/`__fp16`, `DT` for decltype returns,
  `enable_if` overloads that differ only in the return type.  Today: the
  `-O0` symbols are compared with clang's by `bench/cxx-run.py`.
- Member template bodies parsed late lost their template depth (e0d34d7c).
  Today: the parser keeps the scope ([temp.mem]).

## GNU vectors and x86 SIMD intrinsics ([ext:gnu_vector_size])

Why: clang's `mmintrin.h` and friends (`<immintrin.h>`, which the lexer's
`simd-search.h` includes on x86) are written with `vector_size` types, so the
C++ front end cannot compile any x86 SIMD code without them.  The code
generator already has vectors (C's `AST_TYPE_VECTOR`, the `AST_CAST_VECTOR_*`
casts, vector arithmetic in VIR); what the C++ analysis lacks is the analysis
side, and it lowers through `ast_builder_*` to what C's parser would have made.
Shared with C, not copied: the layout and validity rule
(`include/frontend/vector-layout.h`, used by `cc_type_vector` and by
`vector_layout` / `gnu_vector_type` in `ext/clang/ext-vector.cpp`).

- V1 **Done** (2026-11-20): the type.  `type_kind::vector`;
  `__attribute__((vector_size(N)))` on a typedef-name or variable
  (`declaration/simple-declaration.cpp`, like `mode`); size and alignment
  (x86-64: the whole size; arm64: at most 16, `vector_alignment_uncapped`);
  Itanium mangling `Dv N _ T`; lowering to C's vector type; clang's "invalid
  vector element type" and size errors.  Test: `src/test/cxx/run/gnu-vector-size.cpp`
  (cxx-run, -O0 symbols = clang's).  An expression of vector type is still
  refused (`expression/expression.cpp`).
- V2 **Done** (2026-11-20): vector values of one type: initialization from
  a vector of the type (`initialize_with`), `=` (`assignment_of`), the identity
  conversion (`implicit_conversion`), so arguments, results, `sizeof` and
  vector members copied with their class.  Test:
  `src/test/cxx/run/gnu-vector-values.cpp` (bytes read back by memcpy; cxx-run
  -O0..3, arm64 calling convention against clang's).  Not yet: a conversion
  between two vector types, `+=` and the other operators (V3), `v[i]` (V4),
  list-initialization and constant evaluation of a vector.
- V3 **Done** (2026-11-20): the operators (`ext/gnu/vector-operators.cpp`):
  `+ - * / % & | ^ << >>` and the comparisons lane by lane on two vectors of
  one type, or a vector and a scalar (converted to the element type; an
  integer constant must fit, clang's truncation rule); comparisons give a
  signed integer vector of the same lanes (-1 / 0); unary `+ - ~`; the
  compound assignments `op=`.  The nodes are as C's front end builds them,
  so code generation is shared.  Test: `src/test/cxx/run/gnu-vector-operators.cpp`
  (lanes against clang, cxx-run -O0..3).  Not yet: two different vector types
  (clang's lax conversions), `&&`, `||`, `!`, `?:` on vectors, `++`/`--`,
  constant evaluation, templates on vector types.
- V4 **Done** (2026-11-20): `v[i]` (an lvalue lane of a vector object, a prvalue
  of an rvalue; lowered through a pointer to the first lane as C's parser does;
  `&v[i]` is clang's error), list-initialization (the elements in order, the
  rest zero, vectors in arrays, classes and static storage, braces elided
  as for arrays), casts between a vector and a vector or a same-size scalar
  (`conversion_kind::vector_cast`: the bits), `__builtin_shufflevector`
  (`semantic_kind::vector_shuffle`, C's builtin node) and
  `__builtin_convertvector` on GNU vectors.  Tests: `gnu-vector-subscript`,
  `-init`, `-casts`, `-shuffle` in `src/test/cxx/run`.  Moved to V5:
  `__builtin_elementwise_*` (C has `abs` only), `__builtin_nontemporal_*`.
  Not yet: a vector in a constant expression, `v.xyzw` (ext_vector), vector
  parameters of templates.
- V5 **Done for SSE2** (2026-11-20): clang's `<emmintrin.h>` (with `mmintrin.h`,
  `xmmintrin.h`) compiles in C++ for x86-64, and the lexer's `simd-search.h`
  (`lexer-simd` branch) compiled by vcc's C++ front end finds the same bytes
  as clang's.  What it took: `nodebug`, `min_vector_width`, `target`
  attributes (no effect yet: the -m options decide); GNU attributes after a
  `*` (`void *__attribute__((malloc)) f`, `<mm_malloc.h>`); a vector typedef of
  a lesser `aligned`; `_Float16` and `__bf16` as types (`clang_float16`:
  sizes, mangling, vector elements; no arithmetic); compound literals of
  vector type (`semantic_kind::compound_literal`); `__builtin_elementwise_min`
  and `_max` of integer vectors (shared with C: `cc_vector_minmax`, as
  `b ^ ((a ^ b) & (a < b))`); `__builtin_ia32_pmovmskb128` (C's builtin table,
  lane by lane as `movmskps` is; codegen could match it to `pmovmskb`) and
  vector letters (`F`, `B`, `H`) in the C++ view of the table's signatures, so
  `ast_builder_builtin_call` expands the x86 builtins as C does.  The
  headers define hundreds of `static inline` functions over builtins vcc has
  no rules for: such a function of a system header whose analysis is
  unsupported is left undefined (`defer_unsupported_body`), one whose
  lowering fails is dropped (`function_definition_or_drop`), and a use of
  either is a refusal with the reason.  Tests: `src/test/cxx/run/x86-sse2-intrinsics.cpp`
  (`bench/cxx-x86.py`: native, vcc -O0..3 against clang; not in CI yet:
  needs the x86-64 job).  Follow-up (2026-11-20, `cxx/sse2-builtins`):
  `__builtin_elementwise_add_sat` and `_sub_sat` (integer vectors, vector
  operations only, shared with C), `__builtin_nontemporal_store` and `_load`
  (a plain store and load), and the SSE2 builtins that are shuffles, shifts and
  conversions in `cc_builtin_x86_sse2_build` (C's, so C has them too):
  `pshufd`, `pshuflw`, `pshufhw`, the byte shifts, the immediate shifts of
  16-, 32- and 64-bit lanes (counts at and past the width give 0, or the
  sign for `psra`), `cvttps2dq`, `cvttpd2dq`, `vec_ext_v8hi`, `vec_set_v8hi`,
  `undef128`, `emms`; the table's signature letters `I` (v4si), `Q` (v2di),
  `E` (v2df) and `N` (short).  Test: `x86-sse2-ops.cpp`, whose output
  `bench/cxx-x86.py` compares with clang's.  Not yet (of emmintrin.h):
  the saturating narrows (`packs*`), `pavg*`, `pmulh*`, `pmaddwd`, `pmuludq`,
  `psadbw`, the shifts by a vector count (`psll*128`), the float compares and
  min/max, `cvtps2dq` and `cvtpd2dq` (rounding), `ucomi`/`comi`, `rcp`/`rsqrt`.
  Not yet: the other `__builtin_ia32_*` (the table
  has ~30; each needs lanes or an instruction), `__builtin_nontemporal_*`,
  the other `__builtin_elementwise_*`, the AVX2 and AVX-512 paths of
  `simd-search.h` (256- and 512-bit vectors, `__target__` features to the
  assembler's VEX encoding, mask registers), `_Float16` arithmetic.
- `ext_vector_type` (clang_ext_vector_type) shares V2-V4 (element
  swizzles `.xyzw` are its own).


### C++ self-host status after #469

The x86-64 Linux build includes the C++ front end in the self-built
compiler. Call deduction counts an explicit type pack as one parameter
([temp.arg.explicit]/7, /9), leaving the parameters after that pack
deducible in calls such as `syntax_call<Sums...>(f, v)`. The bootstrap
also exercises aggregate braced assignment ([expr.ass]/8), constructor
argument packs in a new-expression ([temp.variadic]/5), and zero
initialization before an implicit default constructor ([dcl.init]/8).

The rebuilt compiler exposed two incorrect code paths in the front end.
A conversion function's definition must start with an empty parameter
list ([class.conv.fct]/1). A call to an inherited operator must address
the selected base subobject ([over.match.funcs]/4), including a later
closure base of the syntax visitor.

On x86-64 Linux, build a C++ seed with vcc and then compare the two
rebuilds it produces at the default `-O3`:

```sh
make -C src -j 6 self SELF_LANG=c++ SELF=../build/linux/cxx-seed
make -C src -j 6 self-check SELF_LANG=c++ SELF=../build/linux/cxx-check \
    SELF_CC="$PWD/build/linux/cxx-seed/vpp"
```

The two rebuilds produced 66 byte-identical objects. The C++ run suite
passed 2428 checks at `-O0` through `-O3`, with 98 expected failures.
