# Research: C++ for fast code in the front end, and the checks that keep it fast

Status: research note, 2026-11-18.  No code depends on it.  Companion of
`RESEARCH-cxx-style.md`, which covers the language subset and the lint; this
note is about speed: what the C++ front end (`src/frontend/cplusplus`) costs
today and why, how the fastest compilers write their source, and which rules
and checks would keep new code fast.

`PLAN-cxx-spec.md` decides "compliance first: optimize only once the tests
pass", and `PLAN-cxx-sema.md` says "designed for speed and caching, tuned
later".  Neither is questioned here.  The question is which habits of today
are tuning that can wait, and which are design that gets more expensive to
change with every file written.

## 1. The baseline

Instructions retired by the whole process (`/usr/bin/time -l`, best of
three; macOS, M1 Pro; clang 23.1.4; vcc at `4b465243`), `-fsyntax-only`,
with the build's flags.  The two source files are C dialect, so all four
front ends can read them.

| Input | Tokens after preprocessing | vcc C | clang C | vcc C++ | clang++ |
|---|---|---|---|---|---|
| empty file | 1 | 31 M | 517 M | 31 M | 518 M |
| four C headers (`stdio.h`, `string.h`, `stdlib.h`, `stdint.h`) | 9,020 | 97 M | 588 M | 662 M | 612 M |
| `frontend/pp/expand.cpp` | 40,932 | 143 M | 688 M | 1,227 M | 768 M |
| `vir/verify.cpp` | 32,903 | 136 M | 665 M | 1,120 M | 753 M |

The same without each tool's start-up, per token:

| | vcc C | clang C | clang++ | vcc C++ |
|---|---|---|---|---|
| `expand.cpp`, instructions per token | 2,700 | 4,200 | 6,100 | 29,200 |
| `verify.cpp`, instructions per token | 3,200 | 4,500 | 7,100 | 33,100 |
| the four headers, total | 66 M | 71 M | 94 M | 631 M |

- vcc starts 17 times cheaper than clang (31 M against 518 M instructions),
  in both languages.  That is the compile server's design and it holds.
- Per token the C++ front end runs **10 times the instructions of vcc's C
  front end and 4.7 times clang++'s**.  On headers it is 6.7 times clang++.
- Analysis is not where it goes: stopping after parsing costs 1,213 M on
  `expand.cpp`, the whole of `-fsyntax-only` 1,227 M.

The front end on itself (its unity translation unit, 332,030 tokens):
4.25 G instructions, 12,800 per token, 0.41 s, 65 MB at peak (195 bytes per
token).

Heap calls, counted with an interposed `malloc` (parsing only):

| Input | Tokens | `malloc` + `calloc` + `realloc` calls | Bytes requested |
|---|---|---|---|
| four C headers, vcc C++ | 9,020 | 58,613 | 227 MB |
| `class/class.ctor.cpp`, vcc C++ | 40,103 | 102,144 (2.5 per token) | 388 MB |
| the front end's unity TU, vcc C++ | 332,030 | 325,209 | 1,294 MB |
| `frontend/pp/expand.cpp`, vcc C | 40,932 | 1,066 | 0.7 MB |

The C front end allocates from regions; the C++ front end asks the heap a
hundred times as often.

Sizes of the types stored per token, node, type and entity:

| Type | Bytes | Padding |
|---|---|---|
| `preprocessing_token` | 24 | 0 |
| `token` | 80 | 15 |
| `syntax_child` / `syntax_node` | 4 / 20 | 0 |
| `type` / `qualified_type` | 32 / 4 | |
| `semantic_node` | 48 | |
| `entity` | 216 | 47 |
| `diagnostic_argument` | 256 | |

## 2. Where it goes

A sampling profile of the parse of the unity translation unit (`sample`,
152 samples at the top of the stack):

| Share | Function |
|---|---|
| 17.8 % | `lexer::scan_preprocessing_op_or_punc` |
| 23.0 % | `memcmp` and its stub |
| 24.4 % | `strlen` and its stub |
| 9.2 % | `lexer::lex` |
| 5.9 % | `declared_names::find` |
| 5.3 % | `token_stream::make_through` |
| 3.9 % | `open` |
| 3.9 % | `lexer::scan_identifier` |
| 3.3 % each | `memmove`, `source_file::index_lines` |

**Finding 1: one table scan is most of the time.**  For every operator or
punctuator, `scan_preprocessing_op_or_punc` (`lex/lex.operators.cpp`) walks
all 61 rows of the operator table and calls `strlen` and `memcmp` on each.
An experiment, not committed: one line that skips a row whose first
character differs.

| Input | Before | With the line | Change |
|---|---|---|---|
| four C headers | 684 M | 395 M | -42 % |
| `expand.cpp` | 1,224 M | 758 M | -38 % |
| the unity TU | 4,227 M | 2,698 M | -36 % |

With it `expand.cpp` costs what clang++ does, start-up included.  The code
is a faithful reading of `[lex.pptoken]/3.3` ("the longest sequence of
characters that could constitute a preprocessing token") and wrong for
speed in a way no review of its correctness finds.

**Finding 2: a growable buffer per call.**  49,377 of the 58,613
allocations for the four headers ask for between 2 and 4 KB: `vbuffer`
takes 4,096 bytes on its first append (`common/buffer.h`), and the front
end declares a local `owned_buffer` in 81 places.  One is in
`lexer::convert` (`lex/lex.token.cpp`), which runs for every token and
fills the buffer with the spelling of every identifier, to look the keyword
up by its characters.  That is 192 MB requested to produce 9,020 tokens.

**Finding 3: an owner per function.**  253 local `owned_array`,
`owned_buffer` or `owned_string` objects in 139 functions (`clang-query`,
`lower/` not counted): each call allocates on first use and frees on return.
It is the idiom the ownership rule teaches ("memory is held by owners"),
which is right for lifetime and silent about cost.

Findings 2 and 3 are not tuning.  They are how 139 functions are written,
and the count grows with each subclause implemented.

**Not findings.**  No record type larger than 16 bytes is passed by value
(0 parameters; 5 functions return one); 16 range-`for` loops copy a record
per element, 11 of them one of the larger types; one function has a stack
frame over 1 KB (a dump); 11 enumerations could have a narrower underlying
type.  Dispatch has no virtual call, no function pointer and no type-erased
callable.  Names are interned numbers already.

## 3. What fast compilers do

Read from each project's source and documents (URLs under Sources).

| Practice | Who | Reported effect | How it is held |
|---|---|---|---|
| A fixed, asserted size for each hot record | Carbon: `Inst` 16 bytes, parse node 8, token kind 1.  Zig: token 5 bytes, AST node 13.  Clang: `Stmt` at most 8.  rustc: `Token` 24, `Expr` 64, about a hundred types | Zig's AST rework: parsing 28 % fewer instructions, 22 % faster.  rustc `Expr` 104 to 72 bytes: under 3 %; 72 to 64: nothing | `static_assert(sizeof(Inst) == 16, ...)`; Zig `assert(@sizeOf(Data) == 8)`; rustc `static_assert_size!`: "Some nodes are used a lot. Make sure they don't unintentionally get bigger" |
| Typed 32-bit indexes into tables | Carbon `IdBase` and `ValueStore`, Zig, Cranelift `PrimaryMap`, rustc `newtype_index!`, Sorbet | a flat AST against a pointer one in a toy interpreter: 1.5 to 2.4 times faster, freeing was 38 % of the pointer version | wrapper types; nothing for speed itself |
| Struct of arrays; rare operands in a side array | Zig `MultiArrayList` and `extra_data`; Carbon keeps token kinds in an array of their own, "by far the hottest token query"; Cranelift `ListPool` (a list handle is 4 bytes against 24) | `zig fmt` 11 % faster, 19 % less peak memory | size assertions on the fixed part |
| Arena allocation, freed as a whole | rustc, oxc, mold, Clang | oxc: about 20 % from an arena for its AST.  rustc 2016 to 2020: "at least 2-3x faster", mostly from avoiding allocations and `memcpy` | nothing mechanical |
| Interned names compared as integers | rustc, mold, Sorbet | rustc: up to 4 % | rustc lint `symbol_intern_string_literal` |
| Scanner dispatch on the first byte, bulk skipping | Carbon (a table of functions per leading byte, tail calls, SIMD over 64-byte blocks), V8, oxc | V8: 1.2 to 2.1 times by token class | benchmarks kept in the tree |
| Thin handles by value, nothing large by value | LLVM (`StringRef`, `ArrayRef`), rustc | not quantified | rustc lint `disallowed_pass_by_ref`; LLVM: review |
| No recursion | Carbon, TigerBeetle | Carbon: clang moves to a new thread near the stack limit | `misc-no-recursion` as an error (Carbon) |
| No allocation on marked paths | clang's function effects; TigerBeetle allocates everything at start | not quantified | `[[clang::nonallocating]]` with `-Wfunction-effects` |
| A benchmark gate | Carbon (a t-test on latency and exact memory bounds, on its trunk); rustc-perf, in instructions because they vary 0.1 % where wall time varies 3 % | | CI |
| Lints the compiler runs on itself | rustc: 17 `rustc::` lints, on only when it builds itself | | bootstrap passes `-Wrustc::internal` |

What it says for us:

- **The budgets are an order of magnitude below ours.**  Carbon's design
  budget is "maybe 500 instructions per line lexed and parsed" and one main
  memory access per line; it is a target, with no measured figure
  published.  vcc's C front end is at about 2,700 per token, the C++ front
  end at about 29,000.
- **Sizes are asserted everywhere, and shrinking them pays less than it
  seems.**  rustc's author on six structure-shrinking changes: "A lot of
  effort yielding just a few minor wins."  The assertion is cheap and
  stops growth; the campaign to shrink can wait (P1 now, P2 later).
- **Allocation is where the large factors are.**  rustc's biggest single
  item in that series cut heap allocations on one crate from 23.9 M to
  2.5 M within a change worth up to 42 % of instructions; Roc measured
  399,862 allocations against 46 for the same parse, 1.7 times slower.
  This is findings 2 and 3.
- **Scanner tables are a known hot spot** and every fast lexer dispatches
  on the first byte.  oxc is the caution against going further without a
  profile: it rejected a perfect hash for keywords because keyword
  matching was 1 to 2 % of its time.
- **Nobody holds speed with a lint alone.**  The mechanical guards are
  size assertions, a few bans (rustc's default hash map, Carbon's
  recursion), and a benchmark in CI.  Sorbet's author reports the other
  outcome: no continuous benchmark, and regressions that went unnoticed.
- **A compiler linting itself has a precedent**: rustc's internal lints
  are keyed off attributes placed on its own types and functions
  (`#[rustc_pass_by_value]`, `#[rustc_lint_query_instability]`), which is
  the annotation-driven shape section 6 proposes.
- **Hints are not a style.**  clang documents that `[[likely]]` has no
  effect under profile-guided optimisation, and that a profile overrides
  `cold`.  CPython's tail-call interpreter was announced at 9 to 15 % and
  corrected to 3 to 5 % once the baseline's compiler bug was found.  rustc
  measured profile-guided optimisation of itself at 10 to 20 %, more than
  any source-level item above.
- **Design-time arithmetic.**  TigerBeetle: "The best time to solve
  performance, to get the huge 1000x wins, is in the design phase, which
  is precisely when we can't measure or profile"; Dean and Ghemawat's
  hints start from a cost table (L1 hit 0.5 ns, main memory 50 ns) and say
  to count operations before coding.  Finding 1 is what that arithmetic
  catches: 61 rows, two calls a row, for every operator.

## 4. What the tools can check

Tried on the front end with clang 23.1.4:

| Check | What it reports | Here |
|---|---|---|
| `-Wlarge-by-value-copy=N` | a parameter or return value over N bytes | only for POD types: silent for a struct with a default member initializer, which is every struct of the front end (confirmed on a small case) |
| `-Wrange-loop-analysis` | a range-`for` variable that copies | only when the variable is `const T x`; `T x` is taken as intended (confirmed) |
| `-Wframe-larger-than=N` | a function's stack frame | works; needs code generation, not `-fsyntax-only` |
| `-Wpadded` | every padding byte | 228 reports; the two that matter (`token`, `entity`) are among them |
| analyzer `optin.performance.Padding` | a type whose members could be reordered to save padding | 3 types of the front end: `entity` (47 bytes, 7 optimal), `parser`, `semantic_analysis` |
| clang-tidy `performance-*` | 22 checks | only `performance-enum-size` reported (11 enumerations).  The copy checks skip trivially copyable types, which ours are; the rest key on `std::` names |
| `[[clang::nonallocating]]` with `-Wfunction-effects` | a marked function that calls one not marked and not visibly free of allocation | works on our owners (below) |

The last row is the one that fits.  Tried on `owned.h` with Apple clang 21
and clang 23.1.4: a function declared `[[clang::nonallocating]]` that has a
local `owned_array` is reported twice ("must not call non-'nonallocating'
destructor", "... function 'owned_array<int>::append'"), and so is an
`append` to an array it was handed; `operator[]` and `clear`, whose bodies
are visible and allocate nothing, pass without annotation.  The attribute
says which code is hot and the compiler checks finding 2 and 3 against it,
through calls.

Nothing sees finding 1: clang has no check for "this loop compares strings
on the per-character path".  That one is ours to write (section 6) or the
benchmark's to catch.

## 5. Rules for fast code

Each rule with what the front end does today and what could hold it.
**Check**: C the compiler alone (an assertion or a flag); L a lint rule in
vcc, with what it must know as in `RESEARCH-cxx-style.md` (S syntax tree,
N names, Y types and layout); B the benchmark gate; R review only.

### Data

| # | Rule | Today | Check |
|---|---|---|---|
| P1 | A type stored once per token, node, type or entity asserts its size next to its definition: `static_assert(sizeof(token) == 80)`.  Changing the number is a decision, visible in the diff | no such assertion | C; L (N): every element type of a translation unit's tables has one |
| P2 | No padding in such a type: members ordered by alignment | `token` 15 of 80 bytes, `entity` 47 of 216 | L (Y), with the front end's own layout; reference: the analyzer's Padding checker |
| P3 | An enumeration has the smallest underlying type that holds it | 11 do not | L (S); reference `performance-enum-size` |
| P4 | Tables and 32-bit indexes, not pointers between nodes | holds | C once indexes are distinct types (style rule 23) |
| P5 | What the common path reads per element is one array; what few elements have is a side table | `token` carries its literal's value; `entity` is one 216-byte record | R |

### Allocation

| # | Rule | Today | Check |
|---|---|---|---|
| P6 | Code on the per-token path constructs no owner and grows none.  Scratch storage is a member reused across calls, or comes from a region released as a whole | 253 local owners in 139 functions; 2.5 heap calls per token | C: `[[clang::nonallocating]]` on the per-token functions with `-Werror=function-effects`; B: heap calls per token |
| P7 | A buffer's first allocation fits what it usually holds; short strings need none | `vbuffer` starts at 4,096 bytes | B: bytes requested per token |
| P8 | What lives as long as the translation unit is allocated from its region and never freed singly | the tables are `owned_array`s, each a `realloc` chain | R |

### Dispatch and tables

| # | Rule | Today | Check |
|---|---|---|---|
| P9 | Nothing on the per-character path searches a table by comparing strings: dispatch on the first character, lengths are constants of the table | finding 1: 36 to 42 % of all instructions | L (S, N): no `strlen`, `strcmp`, `memcmp` with a non-constant length, and no loop over a whole X-macro table, in `lex/` and `cpp/` |
| P10 | A spelling is hashed once: after the lexer, names are numbers | holds (`declared_names::find` is 5.9 %) | R |
| P11 | No virtual call, function pointer or type-erased callable per token: a `switch` over a kind, or a callable passed as a template parameter | holds | L (S) |

### Copies and calls

| # | Rule | Today | Check |
|---|---|---|---|
| P12 | A value of at most 16 bytes is passed by value; a larger one by reference.  No copy of a larger record per iteration | 0 parameters, 5 returns, 16 loops | L (Y); clang's two warnings do not cover it (section 4) |
| P13 | The uncommon path is out of line: making a diagnostic is a call to a function marked cold, not code in the loop | not measured | R, B |
| P14 | A hint (`[[likely]]`, `always_inline`, `__restrict`, `[[assume]]`) comes with a measurement in its commit | none used | L (S): each use is listed |

### Measurement

| # | Rule | Today | Check |
|---|---|---|---|
| P15 | The benchmark gate covers the C++ front end: instructions per token at start-up, on headers and on source; heap calls and bytes requested per token; bytes at peak per token | `bench/vbench.py` has no C++ mode | B: recorded per commit now; a 1 % budget, as for C, once tuning starts |

## 6. A lint for speed

The style note's lint (`vpp --lint`, a rule table, clang as reference) can
carry these rules; three things are particular to them.

- **"Hot" has to be said.**  A rule about the per-token path needs to know
  which code is on it.  For allocation clang has the attribute and the
  check already (`[[clang::nonallocating]]`, section 4), and it follows
  calls.  The lint can read the same attribute for its own rules (P9, P11,
  P12), in the manner of rustc's internal lints, which are keyed off
  attributes on rustc's own items; or go by directory (`lex/`, `cpp/`), a
  table like the one `naming-lint.cpp` has.
- **The reference is a number, not clang.**  For P7, P9 and P13 no clang
  check exists.  What validates the rule is the benchmark: a rule
  that reports nothing while heap calls per token rise is a broken rule.
  Instructions retired are stable to about 0.5 % on a loaded machine
  (`bench/vbench.py`), and heap calls are exact.
- **Layout rules are dogfood for class layout.**  P2 and P12 need sizes and
  offsets of our own types, which is the layout the front end computes for
  lowering (`PLAN-cxx-sema.md` S4): the lint running on the front end
  checks that code against clang's `-fdump-record-layouts` on 200 types
  that change every week.

## 7. What can wait and what cannot

- **Can wait** (tuning; "compliance first" holds): SIMD in the lexer, the
  C lexer's techniques, struct-of-arrays for tokens, reordering `entity`,
  narrowing enumerations, the benchmark's budget.
- **Costs more every week**: P6 and P7.  They are habits, 139 functions
  have them, and every new subclause copies its neighbours.  Following them
  in new code costs nothing: a scratch buffer on the object that lives
  across calls in place of a local owner.
- **Worth doing now for its own sake**: finding 1.  It is one function, a
  third or more of every run of every C++ harness (`cxx-run.py`,
  `cxx-sema.py`, `cxx-pp.py`, the dogfood run over the tree), and of every
  C++ compile until someone looks.  This note does not change it: the plan
  says tests first, so it is a decision (section 8).
- **Cheap to start recording**: P15.  A number per commit, no gate, so
  that "tuned later" has a starting line and nothing gets silently worse
  by a factor.

Order (the user, 2026-11-18): the semantic integration into code
generation comes first; the rules of both notes are taken up once most of
the test suite compiles.

## 8. Decisions needed

1. Change `scan_preprocessing_op_or_punc` now (finding 1), outside the
   "compliance first" order?  Recommended: yes; it is small, and it makes
   every C++ test run faster.
2. Adopt P6 and P7 as how new front-end code is written, before any lint
   exists?  Recommended: yes, and mark the lexer's and preprocessor's
   per-token functions `[[clang::nonallocating]]` as each is cleaned, so
   that the compiler keeps it so.
3. Add a C++ mode to `bench/vbench.py` that records and does not gate
   (P15)?  Recommended: yes.
4. Size assertions (P1) on `preprocessing_token`, `token`, `syntax_node`,
   `type` and `entity` now?  They cost a line each and make growth visible.
5. Do the per-TU tables move from `owned_array` to regions (P8), as the C
   front end has them?  It decides what "an owner" means in the style
   note's rule 5, so it is better settled before that rule gets a lint.

## Sources

Fetched 2026-11-18.

- Carbon: C. Carruth, "Modernizing Compiler Design for Carbon Toolchain",
  CppNow 2023 slides, <https://chandlerc.blog/slides/2023-cppnow-compiler/>;
  `toolchain/docs/idioms.md`, `toolchain/docs/parse.md`,
  `toolchain/docs/check/README.md`, `toolchain/sem_ir/inst.h`,
  `toolchain/parse/tree.h`, `toolchain/lex/lex.cpp`,
  `toolchain/lex/tokenized_buffer.h`, `toolchain/benchmarking/`,
  `docs/project/cpp_style_guide.md`, `.clang-tidy` in
  `carbon-language/carbon-lang`.
- Zig: <https://github.com/ziglang/zig/pull/7920> (AST memory layout),
  <https://github.com/ziglang/zig/pull/8266> (ZIR), `lib/std/zig/Ast.zig`,
  `lib/std/zig/Zir.zig`.
- rustc: `compiler/rustc_lint/src/internal.rs`,
  `compiler/rustc_lint/src/disallowed_pass_by_ref.rs`,
  `src/bootstrap/src/core/builder/cargo.rs`, `compiler/rustc_ast/src/ast.rs`
  and `token.rs` (size assertions), `compiler/rustc_index_macros`;
  <https://rustc-dev-guide.rust-lang.org/memory.html>; N. Nethercote, "How
  to speed up the Rust compiler", posts of September 2020, February, April,
  July and October 2022, March 2023, March 2024 and March 2025,
  <https://nnethercote.github.io/>.
- LLVM: <https://llvm.org/docs/CodingStandards.html>,
  <https://llvm.org/docs/ProgrammersManual.html>;
  `clang/include/clang/AST/Stmt.h`, `DeclBase.h`, `Expr.h` (size
  assertions).
- clang: <https://clang.llvm.org/docs/DiagnosticsReference.html>,
  <https://clang.llvm.org/docs/AttributeReference.html>,
  <https://clang.llvm.org/docs/analyzer/checkers.html>;
  `clang/lib/Sema/SemaDecl.cpp` (`-Wlarge-by-value-copy`),
  `clang/lib/Sema/SemaStmt.cpp` (`-Wrange-loop-construct`); clang-tidy's
  `performance-*` pages under
  <https://clang.llvm.org/extra/clang-tidy/checks/list.html>.
- Cranelift: `cranelift/entity/src/lib.rs`, `packed_option.rs`, `list.rs`
  in `bytecodealliance/wasmtime`.
- V8: <https://v8.dev/blog/scanner>.  oxc:
  <https://oxc.rs/docs/learn/performance.html>.  Sorbet:
  <https://blog.nelhage.com/post/why-sorbet-is-fast/>.
- A. Sampson, "Flattening ASTs",
  <https://www.cs.cornell.edu/~asampson/blog/flattening.html>.  Roc's
  parser: <https://tweedegolf.nl/en/blog/88/data-oriented-design/>.
- CPython's tail-calling interpreter:
  <https://blog.nelhage.com/post/cpython-tail-call/>,
  <https://fidget-spinner.github.io/posts/apology-tail-call.html>.
- J. Dean and S. Ghemawat, "Performance Hints",
  <https://abseil.io/fast/hints.html>; Abseil's performance tips 62 and
  83, <https://abseil.io/fast/>.
- TigerBeetle, `docs/TIGER_STYLE.md`.  mold, `docs/design.md` at v2.40.4.
- A. Fog, "Optimizing software in C++", 2026 edition,
  <https://www.agner.org/optimize/optimizing_cpp.pdf>, sections 7.2, 7.15,
  7.20 and 7.22.

Not found at a primary source, and left out: the before-and-after figures
of A. Kelley's talk on data-oriented design, and W. Bright's account of
DMD's allocator.

Measurements: macOS on an M1 Pro; clang, clang-query and clang-tidy 23.1.4
(Homebrew), Apple clang 21 for the build; vcc at `4b465243`.  Instructions
from `/usr/bin/time -l`, the best of three runs; heap calls from a library
interposed on `malloc`, `calloc`, `realloc` and `free`; the profile from
`sample` at 1 ms; construct counts from `clang-query` on the front end's
unity translation unit.
