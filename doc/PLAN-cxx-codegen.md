# C++ code generation on the new front end

Status: plan, for review before any code (2026-11-16; revised the same day
after review).  Continues `PLAN-cxx-spec.md` (lexer, preprocessor) and
`PLAN-cxx-parse.md` (parser, branch `cxx/spec-parse`); their conventions
hold here.  It answers three questions: what went wrong in
`src/frontend/cxx/` (PLAN-cxx.md), how code generation sits on the new
front end, and what of the C code generator and of the old C++ generator
is reused.

Decided in review (2026-11-16):

- **One tree, for speed.**  The parser calls semantic analysis as it
  recognizes each construct, and what is built is the typed tree.  No
  syntax tree is kept in the compiler.  This reverses decision 1 of
  PLAN-cxx-parse (section 3.2 says what changes there).
- **The target is VIR.**  Section 3.5 has the two routes to it; which one
  is still to confirm.
- **Speed decides the architecture** (section 3.0): nothing analyzed twice
  in a build, nothing analyzed that is not used, no token parsed twice;
  steps S0-S7 (section 6) come before CG1.

Working assumption, to confirm: the typed tree is the shared AST the C
front end already builds (`struct ast`), grown with C++'s kinds, so C and
C++ share one tree format and one code generator.

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

One tree is also the old front end's shape: its parser drove everything.
What it lacked is in section 2: results as return values, no modes, no
unknown, templates as trees.  Those rules, not the number of trees, are
what separates this design from that one.

## 2. Principles

Each answers a cause above.

1. **One tree, built once.**  Tokens become typed nodes directly; nothing
   is parsed twice and no second representation of a body is kept.  A
   body that is never used is never parsed.
2. **Semantic analysis has no modes and no side channels.**  A rule takes
   typed operands and returns a typed node.  A tentative parse builds
   nothing that is kept; a template is a tree that is substituted, not
   tokens parsed again; the generator never moves the parser.  Lowering
   decides nothing the standard decides.
3. **No unknown and no silent absence.**  The tree handed to the code
   generator is complete and verified.  A construct not implemented yet,
   in analysis or in lowering, is an error that names it, and the compile
   fails; such errors are counted per corpus and are zero when the plan
   is done, as the parser's `unparsed` regions are.  No environment
   variable changes behaviour.
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

### 3.0 Performance architecture

What decides speed, from measurements already in `doc/`:

- 30x clang -O2 on json's 71 TUs is 8.1 s; the front end must stay at
  least 4.5x faster than clang's, or the TUs must share work
  (PLAN-cxx.md, "Can json reach 30x").
- On json, 86 % of clang's time is front end and 53 % instantiation;
  85 % of the function and class instantiations repeat one done in
  another TU (PLAN-cxx.md X0).
- The old front end spent 11.0 of its 13.7 s on headers every TU parsed
  again; with the headers parsed once (a fork server) the suite took
  3.7 s, with vcc generating about 1 % of the code (PLAN-cxx.md status
  23, 40).
- The C front end's largest measured wins: lazy header bodies taken
  zero-copy from the header cache (-45 % instructions), the header cache
  (-35 % cycles), a resident vccd (-20 %), prefix snapshots (-10 %).
  Declaration replay, which copies records into each TU, did not pay
  (`quality/MEASURE-replay.md`).
- The parser (read from the code, not measured): every declaration and
  statement is a tentative parse kept when it succeeds (`dcl.pre.cpp`,
  `stmt.pre.cpp`); every member-specification is parsed twice, the first
  pass thrown away (`class.mem.cpp`); every `(` tries a type-id first; a
  name is looked up two or three times; lookup scans linearly, a scope is
  a `malloc`, a token costs at least 112 bytes.  Recorded speed: 250-370
  ns per token, none on json.

So per-token speed matters, but less than doing less work, in this order:
never analyze the same header declaration, inline body or instantiation
twice in a build; never analyze a body nobody uses; never parse a token
twice.

1. **Layered, immutable, shared state.**  The tables of the typed tree
   (types, records, symbols, nodes, lists, pools) and C++'s entities live
   in two layers: a shared one, frozen and read lock-free by every TU
   thread of vccd, published as the header cache is
   (`pp_shared_allocate`); and the TU's own, in per-TU regions as
   `cc_translation_unit_init` has them (`vjob_region`).  One 32-bit index
   space says which layer an entry is in; a TU reads shared entries in
   place and never copies them.  The unit of sharing is a header-cache
   region analyzed at file scope, keyed as prefix snapshots are and
   validated by the lookups it made (PLAN-cxx X4, X5).  A fork after the
   common prefix (the old `cxx_fork_tus`) is the first version and stays
   the fallback.
2. **Bodies and templates on demand, analyzed once per build.**  Inline
   functions' and members' bodies and template definitions are token
   ranges, taken zero-copy where the header cache has the braces
   (`pp_lazy_body_skip`, as `frontend/c/lazy-body.cpp`).  The first use
   parses one into a typed tree, dependent for a template, in the shared
   layer.  Instantiation substitutes that tree and is cached by
   (template, arguments) with its recorded lookups.  [temp.res]/8 lets an
   uninstantiated template go undiagnosed; vccd analyzes an unused inline
   body once per build for its diagnostics.  Complete-class contexts
   ([class.mem]/6) are such ranges too, parsed when the class is
   complete: this replaces the member-specification's first pass.
3. **One pass in the parser, nothing built and rewound.**  Error recovery
   is not a tentative parse: a declaration or statement that fails is
   reported and skipped, its pending children truncated; one that
   succeeds is never rewound, so actions at `finish_node` stand.
   Disambiguation ([dcl.ambig.res], [stmt.ambig], the cast's `(`,
   template arguments) is a recognizer that builds nothing and answers
   from lookups; then the chosen reading is parsed once.  A name is looked
   up once per token (a cache keyed by token index), and each lookup
   records its token.  Scopes are C's: an identifier-indexed directory of
   the innermost binding, shadow chains and a scope stack
   (`frontend/c/scope.cpp`); members are in per-entity hashed tables in
   the layer that owns them; no `malloc` per scope.  Tokens stay compact
   (the header cache's 12-byte form), their values decoded when asked.
4. **Actions at `finish_node`.**  The node's kind selects the action, so
   the grammar functions stay as they are; the recorder of 3.2 is the
   other set.  Rules return 24-byte nodes of the shared AST with
   interned types (`cc_type_intern`, the function-type hash), C++'s
   entities in side tables of the same layers.  No mode flags: the
   recognizer is the speculation, unevaluated operands are nodes never
   lowered.
5. **Code generation per symbol, streamed and shared.**  Emission is a
   queue of (symbol, linkage, body): a non-inline definition is queued
   when its body is analyzed, so the code generator works while the
   parser goes on; LINKONCE symbols come in by reference as
   `codegen_work_enqueue` does.  A COMDAT function's object bytes are
   cached in vccd by mangled name and a hash of body and options
   (PLAN-cxx X5).  -O0 and -O1 take the direct paths, -O2 and up VIR, as
   for C: at 30 ns to build and 200 to allocate per VIR instruction
   (SPEC-vir 22.1), VIR at -O0 would be the largest cost.

```
tokens ─ parse/ ──────────> typed tree ─ lower/ + abi/ ─> C-level typed tree
         calls semantic     (struct ast with              (the same nodes,
         analysis per        C++'s kinds; the              rewritten when a
         construct           only tree kept)               body is emitted)
                                                                │
        src/codegen/: walker ─> VIR at -O2 (and the direct paths below it)
```

What exists for one function body, from tokens to bytes: the typed tree,
then VIR, then machine records.  clang builds its AST, LLVM IR, machine
instructions and the encoder's instructions.

### 3.1 The typed tree

`struct ast` is C's typed tree today: 24-byte nodes in index-addressed
tables, every conversion a node, types interned, records laid out by
clang's rules (`cc.h`).  C++ uses the same tables and adds kinds; it does
not encode itself in C's kinds, which was the old front end's fifth cause.

- **Two levels of one format.**  The C++ level has what C++ means (a
  reference type, a class with bases, a constructor call with its result
  object, a virtual call, a derived-to-base conversion with its path, a
  temporary with its point of destruction).  The C level is what the code
  generator takes.  A verifier stands between them, as `vir_verify` does
  for VIR: the code generator never sees a C++-level kind.
- **Shared with C**: types, records and their layout, symbols, constants,
  builtins, attributes, the scalar folding rules, the dump and the
  verifier.  Sizes are needed during analysis (`sizeof` in a template
  argument, `static_assert`), so C++ types must be laid out by the engine
  C already has, not by a second one after the fact.
- **Not shared**: the analysis itself (lookup, conversions and overloads
  are other rules in C++), and C++'s entities (namespaces, overload sets,
  templates), which are the front end's own tables beside the tree.
- **C does not pay.**  The node stays 24 bytes; what only C++ needs is in
  side tables indexed by type, record or symbol.  One thing does not fit
  today: the node's flag byte is full, and C++ needs a third value
  category (xvalue).
- **What the tree must record for code generation**, in the standard's
  terms: type and value category of every expression ([basic.lval]);
  every implicit conversion ([conv]); the function each call names and
  whether the call is virtual; the form of each initialization
  ([dcl.init]/17) and the result object of each class prvalue
  ([basic.lval]/5); each temporary's point of destruction
  ([class.temporary]/4, /6); each variable's storage duration and whether
  its initialization is constant, with the value ([basic.start.static]);
  implicitly-defined functions as ordinary bodies ([class.ctor],
  [class.dtor]) in the order of [class.base.init]/13.

### 3.2 The parser and semantic analysis

- **Grammar functions call actions.**  A function of `parse/` still
  implements the productions it quotes, but where it made a node it now
  calls an action with what it recognized, and gets a typed node or
  declaration back.
- **Two sets of actions, the parser compiled once for each**, as the
  walker is compiled once per lowering target.  *Semantic analysis* is
  the compiler.  *The recorder* builds today's `-dump-syntax` tree with
  `declared_names` as its only knowledge: it keeps `cxxparse`, the
  grammar coverage and every test of PLAN-cxx-parse running without
  semantic analysis, and costs the compiler nothing.
- **Rules take typed operands.**  An action is thin: it turns what the
  grammar gave it into operands and calls the rule (`build a call from
  this callee and these arguments`).  Instantiation calls the same rules
  (3.3), so each rule is written once.
- **The grammar gets real answers.**  Whether a name is a type or a
  template comes from [basic.lookup] in full, instantiating a class when
  a qualified name looks into it (`X<int>::type * p`).  The class of an
  object expression is known when `x.f<T>()` needs it
  ([basic.lookup.classref]/1).  `declared_names` ("no types") cannot
  answer either; it stays the recorder's.
- **What is still syntax, and short-lived.**  A declarator is read
  outside-in and means inside-out ([dcl.meaning]), so its parts are
  records that live until the declaration is analyzed.  A complete-class
  context ([class.mem]/6) and a body nobody has used yet are token ranges.
- **Tentative parsing builds nothing that is kept.**  The standard's
  disambiguation rules are syntactic ([dcl.ambig.res]/1, [stmt.ambig]/1:
  "could possibly be a declaration"), so a recognizer that builds nothing
  decides them.  Where nodes must be built to decide, the node tables are
  truncated to a mark; no declaration is entered and no diagnostic issued
  inside a tentative parse.  This is where one tree is harder than two,
  and where the old front end's mode flags came from.
- **Bodies on demand.**  An inline function's body is parsed and analyzed
  when it is used, or once per build in vccd for its diagnostics: the
  standard asks for a diagnostic even when it is never used, and only
  templates are exempt ([temp.res]/8).

What changes in PLAN-cxx-parse: decision 1 (a tree of the grammar's
nonterminals, with semantic analysis building from it later) is replaced
by actions; `parse/syntax_tree` becomes the recorder's; steps P2-P9, the
grammar coverage and the clang comparisons stay as they are.

### 3.3 Templates

With no syntax tree, a template's body is either tokens parsed again (the
old front end's third cause) or a typed tree with dependent parts.  It is
the second.

- A template is analyzed once, at its definition: non-dependent names
  are bound there ([temp.res]), and what depends on a parameter
  ([temp.dep]) becomes a dependent node (a dependent type, an unresolved
  call, a member of an unknown specialization).
- Instantiation substitutes: it walks the pattern and calls the rules of
  3.2 with the substituted operands.  It never returns to tokens.
- Dependent kinds are a third use of the node format.  Like the C++
  level, they never reach the code generator; unlike it, they are never
  lowered.
- An instantiation stays a function of (template, arguments, the
  declarations its lookups found), as PLAN-cxx X4 requires for the cache.

### 3.4 Lowering and the ABI

```
src/frontend/cplusplus/
  abi/itanium/     one file per section of the Itanium C++ ABI
    layout ([abi 2.4]), member pointers (2.3), virtual tables (2.5),
    VTT and construction vtables (2.6), array cookies (2.7), guard
    variables (2.8), RTTI (2.9), calls and constructor/destructor
    variants (3.1), construction APIs (3.3), mangling (5.1), vague
    linkage (5.2), exceptions (the EH ABI's __cxa_* level)
  lower/           C++ level -> C level, files by the clause lowered
```

`abi/` answers questions and builds no nodes (the offset of this base,
the name of this function, the slots of this vtable); semantic analysis
asks it too, for sizes.  It has per-CPU parameters (the virtual bit of a
member function pointer, guard width).

Lowering rewrites a body's C++-level nodes when the body is emitted,
after the TU's analysis is complete, so nothing reads the C++ level of
that body afterwards.  A body another TU may still read (vccd's caches)
is copied first.  A body that is never emitted is never lowered.

| C++ level | C level |
|---|---|
| reference | pointer |
| class | record with every field at the ABI's offset; a base is a field of its record type |
| member function, `this` | function with a first pointer parameter |
| class argument or result not trivial for calls ([abi 3.1]) | pointer to a temporary / hidden result pointer |
| constructor, destructor | functions per variant (C1, C2, D0, D1, D2), aliases where equal |
| automatic object or temporary with a destructor | cleanup node (3.6 B) |
| vtable, VTT, type_info | constant data, weak in a COMDAT by the key-function rule |
| virtual call | load of the slot, indirect call |
| thunk | thunk function (3.6 E) |
| `throw` | `__cxa_allocate_exception`, construction, `__cxa_throw` |
| `try`/`catch` | try node (3.6 C), `__cxa_begin_catch`/`__cxa_end_catch` |
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

### 3.5 To VIR

VIR is typed SSA for one target (SPEC-vir 1.2): machine types, resolved
callees, ABI signatures, explicit loads and stores.  Two routes:

- **Through the C level and the existing builder** (proposed).  The
  walker's second instance already turns the C level into VIR: SSA
  construction, slot promotion and the target's argument classification
  are there.  C++ adds nothing to the builder.
- **Straight from the front end.**  Today nothing builds VIR but that
  walker: SSA construction runs on its pre-IR (`src/codegen/vir/`), not
  on VIR, classification is keyed on the shared AST's types, and
  `symaddr` refers to the shared AST's symbols.  A front end emitting VIR
  itself writes all three again.

Either way VIR needs, for C++:

- **An unwind edge.**  A call that may throw cannot be expressed
  (SPEC-vir 8.1 has no such terminator).
- **What 1.0 leaves to "the ordinary path"** (SPEC-vir 1.3): tail calls
  (thunks), `alloca`, `setjmp`, inline assembly, integers wider than 64
  bits.  Through the C level a function with one of these still gets
  code, from the direct path; with VIR as the only path each is an error
  or VIR work first.
- **Data stays outside it.**  VIR is functions; globals, strings and
  vtables are emitted from the tree's symbols.
- **A cheap pipeline for unoptimized builds**, if C++ always goes through
  VIR: its specification says no VIR code runs at `-O0` and `-O1`, and
  budgets 30 ns per instruction to build and 200 (target 60) to allocate
  registers (SPEC-vir 22.1).

### 3.6 Changes to the shared code generator

All language-neutral, each leaving C's output byte for byte
(`bench/vcc-same.py`) within `vbench.py gate`'s 1 %.

- **A. The input is an AST, not the C parser.**  `codegen_unit_emit` takes
  `cc *`; it reads the 24 tables of `struct ast` but also calls `cc_eval`
  (50 sites), `cc_symbol_global_find` (the parser's scopes) and reads three
  exception symbols outside `struct ast`.  The C++ front end fills a
  `struct ast` through a builder interface (types, records, symbols,
  nodes, lists, pools) and calls no `cc_*` parser function.  This
  finishes PLAN-cxx X1 and review item Q-BND-01, and semantic analysis
  needs it before code generation does.
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
  and Windows x64 (`__gxx_personality_seh0`).  In VIR this is the unwind
  edge of 3.5.  To check first: the walker's VIR instance sets
  `eh_fn = 0` with no failure reason (`walk/statement.cpp:2221`), so at
  -O2 a function with cleanups may get no landing pads today (read, not
  run).
- **D. COMDAT from the code generator.**  A LINKONCE symbol is a plain
  `.weak` today; `obj/` and `as/` already write ELF groups, Mach-O weak
  definitions and COFF COMDATs.
- **E. Thunks as a kind of function**: adjust the first argument by a
  constant or through the vtable, jump to the target.  A dozen lines per
  CPU, and the only correct form for a variadic virtual function.
- **F. Emission by symbol.**  `codegen_unit_emit` takes a whole TU and
  pulls LINKONCE symbols in by reference.  The front end needs the same
  one level up (instantiate, analyze and lower a body only when it is
  referenced): first as a worklist closed before the code generator runs,
  behind an interface that is already a queue of (symbol, linkage, body),
  which is where vccd's code-by-symbol cache (X5) goes later.

### 3.7 Alternatives not taken

- **A syntax tree kept beside the typed tree** (this plan's first draft,
  and PLAN-cxx-parse's decision 1).  For it: grammar and semantics apart,
  tentative parsing with nothing to undo, a template's syntax tree as the
  thing instantiated.  Against, and deciding: one more allocation and
  walk for every construct analyzed, and it cannot be two clean passes
  anyway, since the grammar needs types mid-parse (3.2).
- **Tokens for templates**: the old front end's third cause.
- **Our own walker over the C++ level, calling `LO()`**: a third walker
  beside the shared one (7.6k lines) and arm64's (9.5k), repeating every
  C-level decision (argument classification, switches, folding, inlining).
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

- **V0 the parser's own checks** (PLAN-cxx-parse V1-V6) run on the
  recorder build and stay as they are.  New with semantic analysis: both
  builds accept and reject the same declarations on the corpus, except
  where the recorder's name table is known not to suffice (3.2), listed.
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

Per step, recorded here: V1-V6 counts, the unimplemented count, and ns
per token from tokens to typed tree and per node lowered, against Status
133's 0.6 s of front end per json TU.

## 6. Steps

First the performance base of 3.0, each step measured and its numbers
recorded here:

- **S0 Measure.**  `cxxparse -stats` reports time; `bench/cxx-parse-speed.py`
  reports ns per token, tentative parses and rewound tokens against clang
  `-fsyntax-only` on json.hpp, libstdc++'s headers and `CXX/drs`.
  Status (2026-11-16, done): `cxxparse -stats` prints nanoseconds and peak
  resident bytes, `-tokens-only` the same for phases 1-7 alone;
  `bench/cxx-parse-speed.py [--headers [PAT]]` (`CXX/drs` left out: it is a
  test directory, not one TU).  Baseline, M1 Pro, best of 3:

  | input | tokens | phases 1-7 | parser | vcc | clang -E | clang -fsyntax-only |
  |---|---|---|---|---|---|---|
  | json.hpp (libstdc++ 14, gnu++17) | 844 642 | 1403 ns/token | 236 ns/token | 1.38 s | 0.12 s | 0.98 s |
  | each libstdc++ header alone (1 run) | 7.52 M | 1458 | 165 | 12.2 s | | 7.0 s |

  So the front end with no semantics is 0.57-0.71x clang's with them, and
  phases 1-7 are 86 % of its time: about 9x clang -E.  A sample of
  `-tokens-only` on json.hpp: `memcmp` and `strlen` from
  `lexer::scan_preprocessing_op_or_punc` (a linear scan of every
  operator's spelling per punctuator, `lex.operators.cpp`) and
  `lookup_keyword`, then the lexer itself; the cost of PLAN-cxx-spec's
  decision 4 (compliance first).  The parser does 148 000 tentative
  parses and rewinds 565 613 tokens on json.hpp, two thirds of its tokens.
  Consequence for the order: the input path comes first.  S1 is preceded
  by S0b, phases 1-7 at clang -E's speed or better (the operator and
  keyword tables as direct lookups, the lexer's fast paths), and S2's
  compact tokens and S5's shared header cache matter more than the
  parser's own cost.
- **S1 The parser without build-and-rewind** (3.0 item 3): recovery, the
  member-specification as one pass with ranges, the recognizer at the hot
  disambiguation sites, one lookup per token.  Gate: PLAN-cxx-parse's
  V1-V6 numbers unchanged.
- **S2 Regions and lookup tables**: `vjob_region` arenas, C's scope
  directory, compact tokens, no `malloc` on the hot path.
- **S3 Actions and the first typed nodes**: the class-free programs of
  `src/test/cxx/run/` at -O1 and -O2 (CG1's start).
- **S4 Bodies on demand** (3.0 item 2), with the share skipped on json.
- **S5 The shared layer in vccd** (3.0 item 1); json's CPU with it on and
  off.
- **S6 Instantiation once per build**, with a verify mode.
- **S7 Code generation per symbol, streamed, and the code cache**: objects
  byte-identical with the caches on and off.

Then CG0-CG9 on that base:

The parser's steps go on against the recorder at their own pace.  Semantic
analysis has no plan yet; each step below names what it needs from it.
CG0 needs none, and semantic analysis needs CG0's first half itself.

- **CG0 The tree and the boundary, C only.**  3.6 A (the builder
  interface; the code generator's reads outside `struct ast` listed, then
  moved or passed in), the C++-level and dependent kinds with the
  verifier that keeps them from the code generator (3.1), room for the
  third value category; then 3.6 B (the cleanup node,
  `__attribute__((cleanup))` through it in the walker and the arm64
  emitter), D, and C's check of VIR.  Done when a test program builds a
  small AST with no `cc_*` call, a cleanup node in it, and gets a running
  object on arm64 and x86-64 at -O1 and -O2; V8 holds.
- **CG1 Functions over scalars.**  Needs: the parser's P3 and P4 with the
  actions of 3.2; fundamental types, standard conversions, the built-in
  operators, [stmt], [dcl.fct], namespaces.  Lowering of references;
  mangling of functions in namespaces.  The driver's `-x c++ -c` reaches
  the new pipeline, and the first C++ function reaches VIR.  Tests: the
  programs of `run/` without classes.
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
- **CG5 Vague linkage and demand.**  Needs: [temp] instantiation (3.3).
  Inline functions and implicit instantiations in COMDATs, explicit
  instantiation, internal linkage; the worklist and 3.6 F.  Tests: V2 on
  two-TU programs.
- **CG6 Polymorphism.**  Needs: [class.virtual], virtual bases.  Vtables,
  virtual calls, thunks (3.6 E), virtual bases with vcall and vbase
  offsets, VTT and construction vtables, RTTI, `dynamic_cast`, `typeid`.
  Tests: V3, V5.  The old front end is deleted when `run/` passes.
- **CG7 Exceptions.**  Needs: [except]; VIR's unwind edge (3.5).  3.6 C;
  `throw`, handlers, unwinding through cleanups, function-try-blocks,
  partly constructed objects, `noexcept`.  Targets in order: ELF x86-64,
  ELF arm64, Mach-O arm64, Windows x64.  Tests: V4 with throws, V5.
- **CG8 The rest, by clause.**  Range-based for, structured bindings,
  `initializer_list` arrays, defaulted comparisons, lambdas' conversion to
  function pointer, the builtins, inline assembly.  Coroutines are not in
  this plan.
- **CG9 Real code and the numbers.**  llvm-test-suite, json's 71 TUs with
  every test passing by vcc alone, Skia; then PLAN-cxx X5's caches and
  its 30x measurement at -O2 through VIR.

## Open questions

- **The third value category** has no bit in the node's flag byte.
- **Tentative parsing with real analysis**: how much can the build-nothing
  recognizer decide, and what must be undone when nodes are built?  The
  parser's counters (tentative parses, rewinds) on json's headers say how
  much this matters.
- **Records with a base's tail padding reused**: two fields overlap.  Do
  the walker's copies and argument classification accept a record whose
  fields are given by offset?  CG0 reads, CG2 tests.
- **`cc_eval` on C++**: it is the C front end's evaluator over this tree.
  C++ needs a far larger one ([expr.const]: calls, objects); whether they
  share the scalar part is decided in CG0.
- **Sharing a pattern's non-dependent subtrees with its instances** saves
  work in the hottest path (instantiation is 53 % of clang's time on
  json), but lowering rewrites nodes.  First cut: substitution copies.
- **Debug information** for C++ types is a later plan.

## Decisions to confirm

Decided: one tree (the parser calls semantic analysis; no syntax tree
kept); the target is VIR.

1. **The typed tree is the shared AST**, grown with C++-level and
   dependent kinds behind a verifier, rather than a tree of C++'s own
   beside C's.
2. **The route to VIR is through the C level and the existing builder**
   (3.5), which also keeps the direct paths below `-O2`; or VIR straight
   from the front end as the only path.
3. **The parser is compiled against two sets of actions** (3.2), so its
   tests keep running on the recorder.
4. **The shared AST grows three language-neutral nodes** (cleanup, try,
   thunk) and the code generator stops taking `cc *`: changes to
   `src/codegen/` made for C++ but tested on C.
5. **Unimplemented is an error.**  No function is left without code, no
   `VCC_CXX_*` variable.
6. **The old generator**: tests, harnesses and `builtins.list` as they
   are; mangling, layout and vtables as reference for a rewrite citing the
   ABI; the rest dropped; the old front end frozen now and deleted at CG6.
7. **Vertical slices**: semantic analysis is cut so that CG1 runs on its
   first step, instead of all semantic clauses before any code.
8. **Exceptions are designed in from CG0** (a cleanup's kind) and built at
   CG7, not added afterwards as `-fno-exceptions` first would have it.
9. **The Itanium C++ ABI as Markdown in `docs/cxx-abi/`** for citations,
   as `docs/cxx-spec/` is, if its licence allows; else cited by section
   and URL.
10. **x86-64 Linux and arm64 macOS from CG1**; exceptions reach arm64 and
    Windows x64 in CG7's order.
