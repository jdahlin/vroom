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
    semantic_tree.cpp      typed expressions and statements
    instantiation.cpp      the unit of instantiation and its record (3.5)
    unsupported.cpp        the scaffolding result and its counters
  basic/basic.lookup.unqual.cpp, basic.lookup.argdep.cpp, basic.link.cpp, ...
  conv/conv.lval.cpp, conv.prom.cpp, ...
  expr/expr.add.cpp, ...   beside the parser's function for the same subclause
  dcl/dcl.init.list.cpp, dcl.meaning.cpp, ...
  class/, over/, temp/, except/
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
- Status (2026-11-17, S1b done): initializers and constant expressions at
  global namespace scope.  Expressions are typed with their value category
  (`expr/expr.pre.cpp` dispatches; literals, names, the built-in unary,
  multiplicative to comma operators, casts to arithmetic and pointer types,
  functional casts, subscripts, `sizeof`/`alignof`), with the standard
  conversions and usual arithmetic conversions (`expr/conv.cpp`) and
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
  (`sema/semantic_tree.cpp`, `semantic_kind` in `sema.h`), each implicit
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
  (`basic/basic.lookup.cpp`) for unqualified names (scopes outward,
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
  mangling of members (`class/class.pre.cpp`, `expr/expr.ref.cpp`; lowered
  to C structs whose layout is checked against sema's).  Unsupported with
  their subclause: unions, nested and local classes, bases, constructors,
  destructors, static data members, reference members, brace elision.
  `src/test/cxx/run/classes.cpp`; C5.1 848 -> 915 passing, 0 wrong.
  Then user-declared constructors and destructors (`class/class.ctor.cpp`):
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
  Then [dcl.link] (`dcl/dcl.link.cpp`: extern "C" and "C++", nested, the
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
  Then non-virtual base classes (`class/class.derived.cpp`): a base is an
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
  Then enumerations ([dcl.enum], `dcl/dcl.enum.cpp`): scoped and
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
  `class/class.pre.cpp`): integral bit-fields, named, unnamed and of
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
