# C++ code generation on the new front end

Status: plan, for review before any code (2026-11-16).  Continues
`PLAN-cxx-spec.md` (lexer, preprocessor) and `PLAN-cxx-parse.md` (parser,
branch `cxx/spec-parse`); their conventions hold here.  It answers three
questions: what went wrong in `src/frontend/cxx/` (PLAN-cxx.md), how code
generation sits on the new front end, and what of the C code generator and
of the old C++ generator is reused.

Short answer: reuse the C code generator whole, by lowering a typed C++
tree to the shared AST it already takes; give that AST the three things C
never needed (scope-exit actions, exception edges, thunks) as
language-neutral nodes; carry the old generator's tests, harnesses and
tables over as they are and its ABI algorithms as reference, and none of
its code that touches tokens.  Code generation never reads the syntax tree:
between the parser and this plan there is a semantic plan that is not
written yet, and section 3.1 is what this plan needs from it.

## 1. What went wrong in the old front end

Read from the code and from PLAN-cxx.md's 156 status entries.

1. **There was never a tree.**  `cxx-internal.h:7` says "the front end is a
   recognizer that looks names up while it parses ... the AST comes next".
   It did not come.  Code generation started (Status 24) by building C
   nodes from inside the parse functions: about 380 calls to over 90
   `cxx_gen_*` functions sit in parser files, and the generator calls the
   parser back by moving `c->pos` (default member initializers, braced
   arguments, nested lists: `codegen/expression.cpp:105`,
   `constructor.cpp:395`, `aggregate.cpp:94`).
2. **Results travel through side channels.**  `struct cxx` has about 280
   fields, about 80 of them "the last ..." or "the next ..." values one parse
   function leaves for another (`etype`, `elval`, `enode`, `ctor_picked`).
   Every re-entry (an instantiation, a constexpr call, a lambda inside a
   function) snapshots them by hand, and a field left out is a wrong
   program: `ret_seen` (Status 144), `eunsure` (Status 156).
3. **Templates, default arguments, member initializers and constexpr
   bodies are token ranges parsed again at each use.**  Meaning is derived
   again every time, under mode flags (`tentative`, `instantiating`,
   `unevaluated`, `gen`, `const_eval`).  Mangling a dependent signature
   needed its own token parser (`mangle.cpp:140-1440`), which knows
   `enable_if_t` and `size_t` by spelling.
4. **"Not known" was an answer and "no code" an outcome.**  Nearly 100
   `cxx_gen_fail` sites leave a function declared and undefined; progress
   was counted as "functions with code".  What that hides came out only
   when a test ran: Status 140, 142, 143, 152 ("four miscompilations, all
   silent") and 151 (the first `std::cout << "x"` of a TU printed a
   pointer).  A false `static_assert` was an error only with
   `VCC_CXX_STATIC_ASSERT` set; 25 `VCC_CXX_*` variables steered the rest.
5. **C++ was approximated to fit C.**  Bases are one `char[n]` field,
   references pointers with a mark, and the only lifetime mechanism is
   C's `__attribute__((cleanup))` on a variable: a class temporary dies
   "at that block's end (later than C++ says, the end of the
   full-expression)" (`codegen/state.cpp:565`), one in `a && b` needs a
   generated helper function per type, `try` is an outlined lambda called
   through an assembly trampoline with a hand-written LSDA, a thunk is
   x86-64 assembly text.  So: x86-64 ELF only, no `dynamic_cast`, no
   virtual bases.
6. **One corpus decided the order.**  Each status entry is the next thing
   json or libc++ needed, so library names entered the logic
   (`std::forward` and `initializer_list` recognised by name), and fixed
   caps were raised when hit (`CXX_MAX_ARGS`, `bases[16]`, 24 candidates).

Where it ended: 10 of json's 70 test TUs pass with vcc alone, 152
functions without code (Status 154), and `driver/job.cpp:229` now rejects
a C++ job that is not `-fsyntax-only`.

What worked, and stays:

- **The boundary.**  Lowering to the shared AST gave the arm64 emitter, the
  x86-64 walker and VIR with a handful of lines changed in `src/codegen/`
  (`AST_SYMBOL_FLAG_LINKONCE`, pre-increment as an lvalue, landing pads
  for cleanup variables).  Whole json TUs linked and ran.
- **clang as oracle for the ABI**: 148 440 layouts, mangled names and each
  object's symbol set compared with clang's.
- **The measurements** (X0, Status 23 and 133): they are this plan's
  performance requirements, and PLAN-cxx's goal stands.

## 2. Principles

Each answers a cause above.

1. **Three trees, one direction.**  Syntax tree (parser) → semantic tree
   (semantic analysis) → shared AST (lowering) → code.  Code generation
   reads the semantic tree: never tokens, never the syntax tree.
2. **Lowering decides nothing the standard decides.**  No lookup, overload
   resolution, deduction, conversion choice or constant evaluation.  If it
   needs a fact, semantic analysis recorded it.  Checked mechanically: the
   lowering includes the semantic tree's public header and nothing else of
   the front end (`bench/quality.py`, module includes).
3. **No unknown and no silent absence.**  The semantic tree handed over is
   complete and verified.  A construct the lowering does not implement yet
   is an error that names it, and the compile fails; such errors are
   counted per corpus and are zero when the plan is done, as the parser's
   `unparsed` nodes are.  No environment variable changes behaviour.
4. **The ABI is a specification too.**  The Itanium C++ ABI is cited as
   the standard is (`[abi 2.4]`), one file per section, and clang checks
   every claim made from it.
5. **C++ is not approximated.**  Where the shared AST cannot say something,
   it gets a node any language could use, implemented once in the code
   generator: no helper function per type, no assembly text from a front
   end.
6. **The language decides the order, corpora measure it.**  Steps follow
   clauses and ABI sections; real code is the last step, as the parser's
   P9.
7. **Built for the caches** of PLAN-cxx X5: lowering an entity is a
   function of that entity, and what reaches the code generator is
   (symbol, linkage, body).

## 3. Architecture

```
tokens ─ parse/ ─> syntax tree ─ semantic analysis ─> semantic tree
                   (grammar)     ([basic] [conv] [expr]   (entities, types,
                                  [over] [temp] ...)       resolved expressions)
                                                               │ lower/ + abi/
                                                               v
              src/codegen/ <───────────────────────── shared AST (struct ast)
   arm64 emitter, walker + x86-64, VIR at -O2; ELF, Mach-O, COFF
```

### 3.1 What code generation needs from the semantic tree

The semantic plan owns the tree; these are this plan's requirements on it,
in the standard's terms.

- **Only what exists at run time.**  Non-template entities and
  specializations ([temp.spec]); nothing dependent.
- **Complete types**: classes with members, bases and, for each virtual
  function, its final overrider ([class.virtual]/2).
- **Expressions carry type and value category** ([basic.lval]).  Every
  implicit conversion is a node ([conv]: lvalue-to-rvalue, array-to-pointer,
  derived-to-base with its path, temporary materialization [conv.rval]).
  Every call names its function, operators, conversion functions and
  constructors included, and says whether it is virtual.  A default
  argument is an expression at the call ([dcl.fct.default]/9).
- **Initializations are resolved** to their form ([dcl.init]/17) and, for
  a class prvalue, its result object ([basic.lval]/5): copy elision is in
  the tree, not found by the lowering.
- **Lifetimes are data.**  Each temporary has its point of destruction:
  the end of its full-expression ([class.temporary]/4) or the lifetime of
  the reference that extends it (/6).  Each variable has its storage
  duration, and for static storage whether initialization is constant,
  with the value, or dynamic ([basic.start.static]).  The lowering places
  the destructor calls; it does not decide whether there is one.
- **Implicitly-defined functions are ordinary bodies** written by semantic
  analysis ([class.ctor], [class.copy.ctor], [class.dtor], defaulted
  comparisons), with base and member initialization in the order of
  [class.base.init]/13.
- **Constants are values** ([expr.const]): scalars, objects by subobject,
  addresses as entity plus offset, member pointers.
- **Definitions on demand.**  The lowering asks for the definition of an
  entity it refers to ([basic.def.odr]); semantic analysis instantiates
  ([temp.inst], [temp.point]) and answers or diagnoses.
- **A verifier and a dump**, as `vir_verify` and `virtest`: every rule
  above is machine-checked before lowering, and the dump is the snapshot
  format of the tests.

### 3.2 Lowering and the ABI

```
src/frontend/cplusplus/
  abi/itanium/     one file per section of the Itanium C++ ABI
    layout ([abi 2.4]), member pointers (2.3), virtual tables (2.5),
    VTT and construction vtables (2.6), array cookies (2.7), guard
    variables (2.8), RTTI (2.9), calls and constructor/destructor
    variants (3.1), construction APIs (3.3), mangling (5.1), vague
    linkage (5.2), exceptions (the EH ABI's __cxa_* level)
  lower/           semantic tree -> shared AST, files by the clause lowered
```

`abi/` answers questions and builds no nodes (what is the offset of this
base, the name of this function, the slots of this vtable); it has per-CPU
parameters (the virtual bit of a member function pointer, guard width).
`lower/` is the only part of the front end that sees the shared AST.

| C++ | shared AST |
|---|---|
| reference | pointer |
| class | record with every field at the ABI's offset; a base is a field of its record type |
| member function, `this` | function with a first pointer parameter |
| class argument or result not trivial for calls ([abi 3.1]) | pointer to a temporary / hidden result pointer |
| constructor, destructor | functions per variant (C1, C2, D0, D1, D2), aliases where equal |
| automatic object or temporary with a destructor | cleanup node (3.3 B) |
| vtable, VTT, type_info | constant data, weak in a COMDAT by the key-function rule |
| virtual call | load of the slot, indirect call |
| thunk | thunk function (3.3 E) |
| `throw` | `__cxa_allocate_exception`, construction, `__cxa_throw` |
| `try`/`catch` | try node (3.3 C), `__cxa_begin_catch`/`__cxa_end_catch` |
| static local | guard variable, `__cxa_guard_acquire`/`release`/`abort` |
| dynamic initialization | one function per TU in `.init_array`, `__cxa_atexit` |
| `new`/`delete`, `dynamic_cast`, `typeid` | calls and loads per the ABI |
| member pointer | integer or pair ([abi 2.3]) |
| builtin | `CC_BUILTIN_*` node (the old `builtins.list`) |
| lambda | nothing: its closure type is a class |

A base as a real field matters beyond tidiness: the C calling-convention
code classifies a record by its fields (SysV eightbytes, AAPCS64
homogeneous aggregates), and the old `char[n]` field for bases cannot give
`struct D : B` the classification of its `double`s (not verified by a run;
CG2's cross-compiler tests decide).

### 3.3 Changes to the shared code generator

All language-neutral, each leaving C's output byte for byte
(`bench/vcc-same.py`) within `vbench.py gate`'s 1 %.

- **A. The input is an AST, not the C parser.**  `codegen_unit_emit` takes
  `cc *`; it reads the 24 tables of `struct ast` but also calls `cc_eval`
  (50 sites), `cc_symbol_global_find` (the parser's scopes) and reads three
  exception symbols outside `struct ast`.  The lowering fills a `struct
  ast` through a builder interface (types, records, symbols, nodes, lists,
  pools) and calls no `cc_*` parser function.  This finishes PLAN-cxx X1
  and review item Q-BND-01.
- **B. A cleanup node.**  `AST_NODE_CLEANUP`: a protected region, an
  action, a kind (normal and exceptional, or exceptional only: a partly
  constructed object) and an optional "active" flag (a temporary in
  `a && b`).  `__attribute__((cleanup))` becomes one use of it, so there
  is one mechanism: today's is a variable-only stack of 1024 in the walker
  and a second copy in the arm64 emitter.  The limit "a statement
  expression's value with cleanups" goes.
- **C. A try node and landing pads.**  x86-64 has pads for cleanups and a
  terminate pad (`arch/x64/codegen/eh.cpp`); they grow handlers, action
  records and the type table, then the same for ELF arm64, Mach-O arm64
  and Windows x64 (`__gxx_personality_seh0`).  VIR has no unwind edge
  (SPEC-vir 8.1): a function with an exceptional cleanup or a try is not
  built as VIR until VIR has one, a `failure.h` reason.  To check first:
  the walker's VIR instance sets `eh_fn = 0` with no such reason
  (`walk/statement.cpp:2221`), so at -O2 a function with cleanups may get
  no landing pads today (read, not run).
- **D. COMDAT from the code generator.**  A LINKONCE symbol is a plain
  `.weak` today; `obj/` and `as/` already write ELF groups, Mach-O weak
  definitions and COFF COMDATs.
- **E. Thunks as a kind of function**: adjust the first argument by a
  constant or through the vtable, jump to the target.  A dozen lines per
  CPU, and the only correct form for a variadic virtual function.
- **F. Emission by symbol.**  `codegen_unit_emit` takes a whole TU and
  pulls LINKONCE symbols in by reference.  The lowering needs the same one
  level up (lower a body, and so instantiate it, only when referenced):
  first as a worklist closed before the code generator runs, behind an
  interface that is already a queue of (symbol, linkage, body), which is
  where vccd's code-by-symbol cache (X5) goes later.

### 3.4 Alternatives not taken

- **Code from the syntax tree**: the old front end's first cause.
- **Our own walker over the semantic tree, calling `LO()`**: a third
  walker beside the shared one (7.6k lines) and arm64's (9.5k), repeating
  every C-level decision (argument classification, switches, folding,
  inlining).
- **Straight to VIR**: no unwind edges, `alloca` or tail calls; no builder
  contract for outside users (SPEC-vir 1.4); its lowerings read `cc`'s
  symbol table; and it gives up the -O0/-O1 path, which is where vcc's
  case for C++ is (X0: the front end is 86 % of clang's time on json).
- **Porting `cxx/codegen/`**: every file reaches `c->pos` or `struct cxx`.

## 4. What is reused from the old generator

| | |
|---|---|
| **As it is** | `src/test/cxx/run/` (174 programs) and `mangle/`; `bench/cxx-run-tests.py`, `cxx-mangle.py`, `cxx-layout.py`, `cxx-strict.py`, `cxx-llvm.py`, `cxx-json.py`, `cxx-skia.py` (black boxes: they get the new binary); `codegen/builtins.list` and its generator; the x86-64 landing pads; `AST_SYMBOL_FLAG_LINKONCE` and the record flags |
| **As reference**, rewritten against entities, citing the ABI | the core of `mangle.cpp` (types, substitutions, nested names, operators, variants, ABI tags, local names); vtable groups, the type_info emitters and the key-function rule of `codegen/vtable.cpp`; base order, empty bases, `no_unique_address` and tail padding from `cxx_class_end`; `memptr.cpp`; initialization order and `__cxa_atexit` from `codegen/global.cpp`; destruction order from `codegen/destructor.cpp` |
| **Not carried** | `typing.cpp`, `expression/`, `statement.cpp`, `declaration/`, `instantiate/`; the token paths of `mangle.cpp`; temporaries in `codegen/state.cpp`; the try trampoline and assembly thunks; every `VCC_CXX_*` variable; the fork server |
| **The status log** | each silent miscompilation in PLAN-cxx.md is a test the new generator must pass; most are in `run/` already |

The old front end is frozen (no new feature) and stays as the baseline the
harnesses compare with until CG6 passes `run/`; then it goes, with its
hooks (`Makefile:146`, `cxx.h`, `driver/job.cpp`).

## 5. Validation

clang is the oracle, never the specification, as before.

- **V1 layout**: every class's size, alignment and member offsets as
  `static_assert`s clang compiles (`cxx-layout.py`).
- **V2 names and linkage**: per object, the defined symbols, their binding
  (`T`, `W`, `V`) and COMDAT groups equal clang's (`cxx-run-tests.py`).
- **V3 tables**: each vtable, VTT and type_info compared with clang's
  object entry by entry, relocations by symbol (new: `bench/cxx-vtable.py`).
- **V4 lifetimes**: generated programs whose constructors and destructors
  log; the log equals the clang build's.  With exceptions on, the same
  with "throw at the Nth call" for every N, which walks every unwind path
  (new: `bench/cxx-lifetime.py`).  This is the old front end's weakest
  spot made a test.
- **V5 across compilers**: `bench/vcc-abi.py` with C++ cases: clang and
  vcc as caller and callee, class and derived class, thrower and catcher.
- **V6 run**: `src/test/cxx/run/`, then llvm-test-suite's C++ programs
  (`cxx-llvm.py`), at -O1, -O2 and -O3, on arm64 and x86-64.
- **V7 nothing silent**: a TU either gets an object that defines what
  clang's defines, or an error; "unimplemented" errors counted per corpus.
- **V8 C did not move**: `vcc-same.py`, `vbench.py gate`, the C suites.

Per step, recorded here: V1-V6 counts, the unimplemented count, ns per
semantic-tree node lowered.

## 6. Steps

Each names what it needs from semantic analysis.  CG0 needs nothing and
can start now, beside the parser.

- **CG0 The boundary, C only.**  3.3 A (builder interface; the code
  generator's reads outside `struct ast` listed, then moved or passed in),
  B (the cleanup node, `__attribute__((cleanup))` through it in the walker
  and the arm64 emitter), D, and C's check of VIR.  Done when a test
  program builds a small AST with no `cc_*` call, a cleanup node in it,
  and gets a running object on arm64 and x86-64 at -O1 and -O2; V8 holds.
- **CG1 Functions over scalars.**  Needs: fundamental types, standard
  conversions, the built-in operators, [stmt], [dcl.fct], namespaces.
  The semantic tree's header, verifier and dump are reviewed against 3.1.
  Lowering of built-in expressions, statements, functions, references;
  mangling of functions in namespaces.  The driver's `-x c++ -c` reaches
  the new pipeline.  Tests: the programs of `run/` without classes.
- **CG2 Classes as data.**  Needs: [class.mem], [class.derived] without
  `virtual`.  Layout, member access, `this`, static members, member
  pointers, passing and returning classes; mangling complete for
  non-template entities.  Tests: V1, V5.
- **CG3 Object lifetime.**  Needs: [class.ctor], [class.dtor],
  [class.copy], [dcl.init], [class.temporary].  Constructor and destructor
  variants, destructors on every way out of a scope, temporaries at the
  end of the full-expression, lifetime extension, conditional temporaries,
  elision through result objects, arrays, `new`/`delete` with cookies.
  Cleanup nodes carry their exceptional kind already.  Tests: V4.
- **CG4 Static storage.**  Needs: [expr.const] values.  Constant against
  dynamic initialization, order within the TU, `__cxa_atexit`, guarded
  static locals, inline variables, `thread_local`.
- **CG5 Vague linkage and demand.**  Needs: [temp] instantiation.  Inline
  functions and implicit instantiations in COMDATs, explicit
  instantiation, internal linkage; the lowering's worklist and 3.3 F.
  Tests: V2 on two-TU programs.
- **CG6 Polymorphism.**  Needs: [class.virtual], virtual bases.  Vtables,
  virtual calls, thunks (3.3 E), virtual bases with vcall and vbase
  offsets, VTT and construction vtables, RTTI, `dynamic_cast`, `typeid`.
  Tests: V3, V5.  The old front end is deleted when `run/` passes.
- **CG7 Exceptions.**  Needs: [except].  3.3 C; `throw`, handlers,
  unwinding through cleanups, function-try-blocks, partly constructed
  objects, `noexcept`.  Targets in order: ELF x86-64, ELF arm64, Mach-O
  arm64, Windows x64.  Tests: V4 with throws, V5.
- **CG8 The rest, by clause.**  Range-based for, structured bindings,
  `initializer_list` arrays, defaulted comparisons, lambdas' conversion to
  function pointer, the builtins, inline assembly.  Coroutines are not in
  this plan.
- **CG9 Real code and the numbers.**  llvm-test-suite, json's 71 TUs with
  every test passing by vcc alone, Skia; -O2 through VIR; then PLAN-cxx
  X5's caches and its 30x measurement.

## Open questions

- **Records with a base's tail padding reused**: two fields overlap.  Do
  the walker's copies and argument classification accept a record whose
  fields are given by offset?  CG0 reads, CG2 tests.
- **`cc_eval` on lowered nodes**: it is the C front end's evaluator;
  whether it moves with the AST or the lowering never needs it (constants
  arrive as values) is decided in CG0.
- **Three trees cost memory and time.**  Lowering is per function and its
  input can be freed after; measured from CG1 against Status 133's 0.6 s
  front end per json TU.
- **Unwind edges in VIR** belong in PLAN-vir; until then -O2 C++ with
  exceptions takes the -O1 path per function.
- **Debug information** for C++ types is a later plan.

## Decisions to confirm

1. **Reuse the C code generator through the shared AST** (three trees),
   rather than a C++ walker or VIR directly.
2. **The shared AST grows three language-neutral nodes** (cleanup, try,
   thunk) and the code generator stops taking `cc *`: changes to
   `src/codegen/` made for C++ but tested on C.
3. **Unimplemented is an error.**  No function is left without code, no
   `VCC_CXX_*` variable.
4. **The old generator**: tests, harnesses and `builtins.list` as they
   are; mangling, layout and vtables as reference for a rewrite citing the
   ABI; the rest dropped; the old front end frozen now and deleted at CG6.
5. **Vertical slices**: the semantic plan is cut so that CG1 runs on its
   first step, instead of all semantic clauses before any code.  This is
   the parser plan's decision 4 (the pipeline runs from day one) one layer
   down, and it tests the contract of 3.1 early.
6. **Exceptions are designed in from CG0** (a cleanup's kind) and built at
   CG7, not added afterwards as `-fno-exceptions` first would have it.
7. **The Itanium C++ ABI as Markdown in `docs/cxx-abi/`** for citations,
   as `docs/cxx-spec/` is, if its licence allows; else cited by section
   and URL.
8. **x86-64 Linux and arm64 macOS from CG1**; exceptions reach arm64 and
   Windows x64 in CG7's order.
