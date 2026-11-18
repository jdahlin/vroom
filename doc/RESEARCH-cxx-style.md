# Research: a C++ style guide for vcc, and the lint that enforces it

Status: research note, 2026-11-18.  No code depends on it.  The rules in
force are `STYLE.md` (files, names, comments) and the Conventions of
`PLAN-cxx-spec.md`; this note measures the code against candidate rules,
looks at how other projects enforce theirs, and proposes a rule set and a
lint written on vcc's own C++ front end, run on vcc's own sources in CI.
Section 8 lists the decisions it needs.  Step 1 of section 7 (the flags) was
done the same day; sections 1 to 6 describe the tree before it.

## 1. The question

The tree holds two kinds of code (`src/`, without `test/`, `runtime/` and the
WebAssembly parts):

| | Files | What it is | What constrains it |
|---|---|---|---|
| C dialect | 413 `.cpp` | C that also compiles as C++20; `VCC_LANG=c` builds it as C11 (`make self`) | `STYLE.md`; the common subset of C11 and C++20 |
| C++ | 140 `.cpp` and 6 public headers, 63,841 lines (`src/frontend/cplusplus`, `src/include/frontend/cplusplus`) | C++20 without exceptions, RTTI or the standard library | `PLAN-cxx-spec.md`, Conventions |

What checks a rule today:

| Mechanism | Rule | Gate |
|---|---|---|
| `clang-format` (QEMU-style, `src/.clang-format`) | layout | pre-commit; CI on changed files |
| hook `no-raw-allocation` (a regular expression) | C++: no `malloc`/`free` family outside `owned.h` | pre-commit; CI on all files |
| hook `no-conditional-operator` (a small tokenizer in Python) | C++: no `?:` | pre-commit only |
| `bench/cxx-spec-refs.py` | C++: citations, quoted grammar, extension tags | CI |
| `vpp --lint` (`src/frontend/c/naming-lint.cpp`) | C dialect: module prefix of file-scope names | its tests only; not run over the tree |
| `bench/quality.py --check` | duplication, private copies of `common/`, threads, state | not in CI |
| `make vcc-cxx-check` | C dialect compiles as both languages, `-Wpedantic -Werror` | not in CI |
| `make VCC_UNITY=` | every file compiles alone | not in CI |
| `-Wall -Wextra -Wimplicit-fallthrough` | compiler warnings | not errors: 432 in a build, all in the C dialect, none in the C++ front end |
| `-fno-exceptions` | no exceptions | the build |

So the C++ code has a written set of conventions, three of them checked,
and nothing that says what C++ a new file may use.  The questions:

1. Which rules should C++ code in this tree follow, beyond the ones written
   down?
2. Which of them can a machine check, and with how much knowledge of the
   program?
3. Who checks: clang-tidy, scripts, or a lint in vcc itself?  vcc has a C++
   parser and a growing semantic analysis; running them over vcc's own
   sources on every CI run tests the front end on 64,000 lines of real C++
   it does not otherwise see.

Drift noticed while reading, to settle whatever else is decided:
`STYLE.md` says "tabs, kernel brace style" and `.clang-format` formats with
four spaces; `STYLE.md` allows no `abort()` and `owned.h` calls it three
times when memory runs out; `PLAN-cxx-spec.md` says CI runs the
conditional-operator hook and it does not; include guards in the front end
follow two schemes (`VCC_CPLUSPLUS_*` in 8 headers, `VCC_FRONTEND_CPLUSPLUS_*`
in 4).

## 2. What other projects require, and how they enforce it

Read from each project's guide and from the lint configuration checked into
its repository (URLs under Sources).

| Project | Rules that bear on a restricted C++ | How they are enforced | Where it runs |
|---|---|---|---|
| LLVM | no exceptions, no RTTI; no static constructors; named casts (functional casts between integers allowed); `struct` only when all members are public; `static` for file-local functions, unnamed namespaces only round class declarations; `auto` "if and only if it makes the code more readable" | root `.clang-tidy`: `llvm-*` (16 checks of its own), `misc-*` minus six, `readability-identifier-naming`.  `clang/` turns naming off again: "too many violations" | advisory.  The only CI lint job covers changes to clang-tidy itself, on changed lines |
| GCC (C converted to C++) | must build with `-fno-rtti -fno-exceptions`; C-style casts prohibited; one-argument constructors `explicit`; conversion operators avoided; `[&]` for lambdas that do not outlive the function; never `abort` | no clang-tidy.  `#pragma GCC poison` in `system.h` on `malloc`, `calloc`, `realloc`, `strdup`, on removed macros, and on the include guards of headers a front end must not include; `abort` is a macro for `fancy_abort`; `-Werror` in the bootstrap; own format attributes so `-Wformat` checks its diagnostics.  `check_GNU_style.py` is 12 layout patterns | every compile |
| GDB (C converted to C++) | GCC's rules, with exceptions allowed | `gdbsupport/poison.h`: `memset`, `memcpy`, `memmove` and `free` are deleted templates for types that are not trivially copyable or destructible; `-Werror` with `-Wshadow=local -Wsuggest-override -Wdeprecated-copy` and others; pre-commit hooks for include guards and GNU layout | every compile; pre-commit |
| Ladybird | no C-style casts; `class` for types with methods; one-argument constructors `explicit`; names follow the specification; comments quote the specification's steps verbatim with their numbers, `AD-HOC:` and `NB:` for what is the project's own | `.clang-tidy` with eight whole groups minus 21 checks, each exclusion with a reason, but CI does not run it.  CI runs about 20 scripts (`check_style.py`: licence header, `#pragma once`, comment tags; include restrictions per component; a count ratchet on `JS::Realm` mentions) and four clang plugins whose diagnostics are errors (reference captures in lambdas marked escaping, GC cells, allocator, math functions) | plugins in the compile; scripts in pre-commit and CI |
| Chromium | Google's guide; every language and library feature is listed as allowed, banned or to be decided, and a new standard's features start banned | `PRESUBMIT.py`: about 160 `BanRule`s, each a pattern with an explanation, a severity and excluded paths.  A clang plugin (`find-bad-constructs`: out-of-line constructors, `override`, `auto` deducing a raw pointer, ...) and a `raw_ptr` plugin in every build.  `-Wunsafe-buffer-usage` as an error with opt-out by path file, pragma and `UNSAFE_BUFFERS()` with a `// SAFETY:` comment.  `.clang-tidy`: 43 named checks | plugins and warnings in every build; bans at upload; clang-tidy as review comments on touched lines; `NOLINT` "actively discouraged" |
| WebKit | smart pointers for every data member; no raw pointer held across a non-trivial call; `NOESCAPE` on lambdas used synchronously; strongly typed identifiers instead of `uint64_t` | no clang-tidy.  20 checkers in clang's static analyzer (`webkit.*`, `alpha.webkit.*`); `check-webkit-style`, a cpplint descendant with about 170 categories, among them bans of `memcpy`, `strcmp`, `printf` by name | a per-PR queue that fails on findings not listed in checked-in expectation files |
| Firefox | types annotated with where they may live (`MOZ_STACK_CLASS`, `MOZ_RAII`), what may be done with them (`MOZ_NON_MEMMOVABLE`, `MOZ_NON_AUTOABLE`) and what a converting constructor is (`MOZ_IMPLICIT`) | a clang plugin with 48 checks that read those attributes; clang-tidy with about 20 checks, and a new one is added only after every existing finding is fixed | static-analysis builds and the review bot |
| Google guide | "We do not use C++ exceptions"; avoid RTTI; no `(int)x`; output parameters as references; `[&]` only when the lambda's lifetime is obviously shorter; statics trivially destructible | cpplint: 71 categories, patterns on lines, no syntax tree | where a project wires it |
| Orthodox C++, Dear ImGui, bx | C-like C++: no exceptions, no RTTI, C headers and not their `<c...>` wrappers, no streams, nothing from the library that allocates; a standard's features five years after it | none prescribed | |
| JSF AV C++ | no exceptions (rule 208); no allocation after initialization, placement `new` only in memory-management routines (206); named casts (185); no `goto` (189); no pointer arithmetic outside containers (215) | commercial tools | |

What the table says:

- **The rules agree; the mechanisms differ.**  No exceptions, no RTTI, named
  casts, `explicit` constructors, no static constructors and "`[&]` only if
  the lambda stays in its scope" appear in nearly every guide, and the
  front end follows all but the casts already.
- **Nobody gates on clang-tidy for the whole tree.**  LLVM, Ladybird,
  Chromium and Firefox each have a `.clang-tidy`; in all four it advises,
  mostly on changed lines.  What gates is what runs in the compile: flags,
  poisoned identifiers, deleted overloads, plugins.
- **Rules about a project's own types are always the project's own code.**
  Chromium, Firefox, Ladybird and WebKit each wrote plugins or analyzer
  checkers for their owners, their smart pointers, their GC; a general tool
  cannot know them.  The rules of section 5 that have no clang-tidy
  equivalent are of that kind.
- **The cheapest enforcement is the language.**  GCC poisons `malloc`;
  GDB deletes `memset` for types that must not be cleared; WebKit asks for
  distinct identifier types in place of integers.  Rules 5 and 23 can be
  enforced that way with no lint at all.
- **A feature list beats a feature ban.**  Chromium's allowed, banned,
  undecided list means a new construct is a decision somebody made.  Rule 8
  is the same idea.
- **Old findings are ratcheted, not fixed first** (WebKit's expectation
  files, Ladybird's counts), or the check is not turned on until they are
  gone (Firefox).  `clang/` shows the third outcome: a naming rule that the
  code does not follow is switched off.
- **Suppressions are scarce and explained**: Chromium's `// SAFETY:`
  comment, Ladybird's reason per excluded check.

Three facts about the tools themselves: clang-tidy has declared checks in
`.clang-tidy` as `clang-query` matchers since 22.1.0, behind
`--experimental-custom-checks`, with only `match` and `let` in a query and
no fix-its; since 22 it reports in all non-system headers by default; and
clang 23.1.0 adds `-Wlifetime-safety`, a flow analysis inside one function
that reads `[[clang::lifetimebound]]` and `[[gsl::Owner]]` on a project's
own types.  cppcheck's free MISRA support is for C; clang-tidy has no MISRA
module.

## 3. What the C++ front end does today

A rule that describes what the code already does costs nothing to adopt.
Counted with `clang-query` 23.1.4 on the front end's unity translation unit
(`lower/` is compiled apart and not in these numbers), in the front end's
own files only; `auto` and naming counted separately as noted.

| Construct | Uses | Reading |
|---|---|---|
| `throw`, `dynamic_cast`, `typeid` | 0 | the subset holds |
| `std::` in code, C++ library headers | 0 | C headers only, as `<stdio.h>` |
| `virtual` member functions | 0 | dispatch is a `switch` over a kind |
| `friend`, unions, bit-fields, `typedef` | 0 | aliases are `using` (14) |
| `goto` | 0 | (the C dialect: 196 in 39 files, its clean-up idiom) |
| conditional operator | 0 | the hook works |
| `new` expressions | 3 | placement `new` in `owned.h`; no `delete` expression |
| objects with static constructors or destructors | 0 | `sema.h`: "nothing global" |
| mutable namespace-scope objects | 0 | (the 8 clang-tidy reports are in C headers it includes) |
| constructors callable with one argument, not `explicit` | 0 | |
| `enum class` / unscoped `enum` | 48 / 4 | unscoped ones are flag sets (`cv_qualifier`) |
| classes / structs / class templates / function templates | 82 / 131 / 2 / 8 | templates are owners and parse helpers |
| member function definitions / free functions | 2,094 / 227 | 742 member functions are `const` |
| free functions that are `static` / unnamed namespaces | 66 / 57 | two ways to say "this file only" |
| `static_cast` / `reinterpret_cast` / `const_cast` | 520 / 10 / 1 | |
| C-style casts / functional casts | 238 / 22 | two ways to cast |
| parameters of non-const reference type / pointer type | 576 / 380 | no stated rule for output parameters |
| parameters with a default argument | 87 | |
| lambdas | 60 | 42 of the 43 named ones capture `[&]` |
| `auto` as written in `.cpp` files | 51 | 43 lambdas, 7 range-`for`, 1 other |
| range-`for` / `switch` / `do` | 154 / 100 / 2 | |
| reference data members | 130 | contexts held by reference |
| `[[nodiscard]]` in the public headers | 0 | 860 `return expected(...)`-style failure returns |
| names not `lower_case` (clang-tidy's naming check set to `lower_case`) | 256 | 160 are the standard's terms that are keywords, with a trailing underscore (`inline_`, `int_`); the rest are X-macro arguments and template parameters `T`, `U` |

The front end is consistent where a convention was written down (names,
allocation, `?:`, no library) and of two minds where none was: casts,
internal linkage, output parameters, `class` or `struct`.

## 4. What clang-tidy says about it

clang-tidy 23.1.4 has 604 checks (131 of them the static analyzer's).  All
general groups (`bugprone`, `readability`, `modernize`, `cppcoreguidelines`,
`misc`, `performance`, `google`, `cert`, `llvm`, `fuchsia`, `portability`,
`concurrency`, `clang-analyzer`) and 30 warning flags outside `-Wall -Wextra`
were run on every file, alone, with the build's flags.

| | C++ front end | C dialect |
|---|---|---|
| translation units | 140 | 413 |
| time, 5 jobs | 44 s (154 CPU s) | not timed |
| distinct findings (file, line, column, check) | 37,794 | 308,142 |
| checks that reported | 121 | 183 |
| files that did not compile alone | 2 (`ext/gnu/attribute.cpp`, `ext/gnu/library-builtins.cpp`) | 6 (`platform/win/pe-*.cpp`, parts of one TU by design) |

### 4.1 The C++ front end

**Most of it is not our style.**  The 15 largest checks are 86 % of the
findings, and each either contradicts a choice already made or needs the
standard library to fix:

| Findings | Check | Why not |
|---|---|---|
| 4,677 | `readability-braces-around-statements` | the formatter's style has no braces round one statement |
| 4,019 | `readability-identifier-length` | `STYLE.md` allows short locals in short scopes; the check sees only the length |
| 2,743 | `modernize-use-trailing-return-type` | not our style |
| 2,348 | `readability-uppercase-literal-suffix` | 2,288 in two generated `.inc` tables |
| 2,069 + 1,122 + 1,014 | `cppcoreguidelines-pro-bounds-*`, `-Wunsafe-buffer-usage` | the fixes are `std::span`, `std::array`, `gsl::at` |
| 2,018 | `misc-const-correctness` | `const` on every local that is not written again |
| 1,937 | `misc-include-cleaner` | contradicts one internal header per module |
| 1,836 | `modernize-use-designated-initializers` | tables |
| 1,562 | `readability-implicit-bool-conversion` | `if (p)`, `if (!n)` are the house idiom |
| 796 | `fuchsia-default-arguments-calls` | default arguments are used |
| 772 | `readability-magic-numbers` | |

Also not for us: `misc-no-recursion` (107: a recursive-descent parser),
`modernize-avoid-c-arrays` (149), `readability-use-std-min-max` (16).

**Clean already**, so a gate costs nothing: `-Wglobal-constructors`,
`-Wexit-time-destructors`, `-Wnon-virtual-dtor`, `-Wcast-qual`,
`-Wcast-align`, `-Wzero-as-null-pointer-constant`, `-Wextra-semi`,
`-Wunused-macros`, `-Wmissing-prototypes`, `-Wundef`, `-Wvla`,
`-Wformat=2`, `-Wdangling`, `google-explicit-constructor`,
`bugprone-use-after-move`, `cppcoreguidelines-avoid-goto`.  The front end
also compiles today with `-fno-rtti -nostdinc++` and with
`-Werror=global-constructors -Werror=exit-time-destructors`; the Makefile
passes none of them.

**Few findings, worth fixing and then gating:**

| Findings (files) | Check |
|---|---|
| 35 (16) + 22 | `-Wshadow`, and constructor parameters that shadow a field |
| 30 (10) | `-Wsign-conversion` |
| 28 classes | `cppcoreguidelines-special-member-functions`: a destructor and an implicit copy (scope guards such as `diagnostics_suppressed` can be copied); 80 more reports are "copy deleted, move not declared", which is correct as written |
| 19 (9) | `cppcoreguidelines-pro-type-member-init` |
| 15 (4) | `readability-inconsistent-declaration-parameter-name` |
| 15 (12) | `bugprone-branch-clone` |
| 12 | the static analyzer (10 are reads past a string literal that the 16-byte padding contract allows) |
| 7 (7) | `readability-make-member-function-const` |
| 3 (2) | `-Wshorten-64-to-32` |
| 2 (1) | `bugprone-too-small-loop-variable` |

**Two of these are defects**, both fixed on 2026-11-18.
`cpp/cpp.replace.cpp:373` and `:448` loop with a `uint16_t` counter up to a
`size_t` count, and a parameter's index was kept in 16 bits: a macro with
more than 32,767 parameters left the later ones unreplaced, and one with
65,536 or more ended `vpp -x c++` with SIGBUS, where clang handles 140,000
(`bench/cxx-run.py` now generates such a program).  `ext/clang/embed.cpp:320`
read `size` unset when `embedded_size` failed.  Two defects in 37,794
findings is the argument for a short curated list and against "enable a
group and suppress".

**Where the code is of two minds**, so a rule needs a decision first: 354
C-style casts in 43 files (`google-readability-casting`); `static` against
unnamed namespaces (176 and 55 reports from two checks that want opposite
things); 329 public data members in classes with member functions;
124 `-Wswitch-enum`; 146 locals declared without a value.

### 4.2 The C dialect

The largest C++-specific findings ask for what C does not have: 10,793
C-style casts, 1,809 `NULL`s, 1,273 `typedef`s, 3,139 `static` functions
"for an unnamed namespace".  While that code must compile as C11 a C++
style guide has nothing to say about it; its rules are `STYLE.md` and the
common subset.  The checks that would still mean something there find
defects, not style, and are a separate piece of work: 164
`bugprone-suspicious-realloc-usage`, 305
`bugprone-implicit-widening-of-multiplication-result`, 322 assignments in
conditions, 466 unchecked results of library calls (`cert-err33-c`).

## 5. Candidate rules

One row per rule: what it says, what the front end does today, and what a
checker must know.  **Needs**: F a compiler flag is enough; T tokens and
comments; S the syntax tree; N declared names and their kinds; Y types of
expressions; D data flow.  **Reference** is the clang flag, clang-tidy check
or matcher that reports the same thing, for section 6.4.

### Subset: what C++ code here does not use

| # | Rule | Today | Needs | Reference |
|---|---|---|---|---|
| 1 | No exceptions: `throw`, `try`, `catch` | 0 | F | `-fno-exceptions` (in the build) |
| 2 | No RTTI: `dynamic_cast`, `typeid` | 0 | F | `-fno-rtti` (missing) |
| 3 | No C++ standard library: no library header, no `std::`; C headers as `<name.h>`.  Language features that need library support go with it (`std::initializer_list`, `<=>` with `<compare>`, coroutines) | 0 | F | `-nostdinc++` (missing) |
| 4 | No object with a static constructor or destructor; no mutable namespace-scope object | 0 | F, N | `-Wglobal-constructors`, `-Wexit-time-destructors`; `cppcoreguidelines-avoid-non-const-global-variables` |
| 5 | Memory belongs to an owner: no `new` or `delete` expression and no allocation function outside `owned.h` | 0 outside | S | `cxxNewExpr()`, `cxxDeleteExpr()`, `cppcoreguidelines-no-malloc` |
| 6 | No conditional operator | 0 | S | `conditionalOperator()` |
| 7 | No `goto` | 0 | T | `cppcoreguidelines-avoid-goto` |
| 8 | Not used so far; the first use is a decision, not a habit: `virtual`, `friend`, multiple inheritance, unions, bit-fields, `typedef`, `const_cast`, using-directives | 0 (`const_cast` 1) | T, S | `fuchsia-multiple-inheritance`, `modernize-use-using`, `google-build-using-namespace`, `cppcoreguidelines-pro-type-const-cast`; a matcher for the rest |

### Declarations

| # | Rule | Today | Needs | Reference |
|---|---|---|---|---|
| 9 | `enum class` with its underlying type; an unscoped `enum` only for a set of flags | 48 / 4 | S | `cppcoreguidelines-use-enum-class` |
| 10 | A constructor callable with one argument, and a conversion function, is `explicit` | 0 violations | S | `google-explicit-constructor` |
| 11 | A class with a destructor deletes copying (and defines moving if it moves) | 28 classes copy implicitly | S | `cppcoreguidelines-special-member-functions` with `AllowMissingMoveFunctionsWhenCopyIsDeleted` |
| 12 | `struct`: data, all public, no invariant.  `class`: private data behind member functions | 329 public members in classes | S | `misc-non-private-member-variables-in-classes` |
| 13 | One way to give a name internal linkage | 66 `static`, 57 unnamed namespaces | S | `llvm-prefer-static-over-anonymous-namespace` or `misc-use-anonymous-namespace` |
| 14 | `auto` for a lambda and in a range-`for`, a type's name elsewhere | 50 of 51 | S | none (`modernize-use-auto` is the opposite rule) |
| 15 | A lambda that captures by reference does not outlive its scope | not known | D | none |
| 16 | Templates for owners, containers and parse helpers; no computation in types | 10 | S (count) | none |
| 17 | Macros: include guards and X-macro tables; code is a `constexpr` or `inline` function | 36 `#define` | T | `cppcoreguidelines-macro-usage` |
| 18 | One convention for output parameters | 576 references, 380 pointers | S | `google-runtime-references` was removed with Google's rule |

### Expressions and statements

| # | Rule | Today | Needs | Reference |
|---|---|---|---|---|
| 19 | Named casts; no C-style or functional cast | 260 against 531 | S | `-Wold-style-cast`; it also fires in the C headers the front end includes |
| 20 | No implicit conversion that loses bits or changes sign | 33, and the defect of 4.1 | Y | `-Wshorten-64-to-32`, `-Wsign-conversion`, `bugprone-too-small-loop-variable` |
| 21 | A name does not hide another of an enclosing scope | 57 | N | `-Wshadow` |
| 22 | A function that reports failure through its result is `[[nodiscard]]` | 0 attributes | S (by return type) | `-Wunused-result` then checks the calls |
| 23 | Indexes of different tables do not mix: `token_index`, `node_index`, `type_index` are all `uint32_t` today | unchecked | Y, with alias names kept | none; distinct types would make it a compile error |

### Names, files, comments, errors

| # | Rule | Today | Needs | Reference |
|---|---|---|---|---|
| 24 | `lower_case` for everything; macros and X-macro arguments `UPPER_CASE`; template parameters `CamelCase`; a term of the standard that is a keyword takes a trailing underscore | holds | N | `readability-identifier-naming` |
| 25 | Full words (`STYLE.md`'s list of abbreviations); a short local only in a short scope | not known | N, a word list, scope length | none: `readability-identifier-length` counts characters |
| 26 | A file is one subclause, named by its stable name; it opens with a comment; a rule cites `[label]/N` and quotes the sentence | checked | T | `bench/cxx-spec-refs.py` |
| 27 | Include guard `VCC_<PATH>_H` | two schemes | T | `llvm-header-guard` (another scheme) |
| 28 | Every file compiles alone | 2 do not | build | `make VCC_UNITY=` |
| 29 | A file over 1,500 lines or a function over 300 says in a comment why it is one thing (`STYLE.md`) | not known | S | none |
| 30 | One failure path: no `abort()`, `exit()` or `fprintf(stderr, ...)` outside it | `abort()` 3 times in `owned.h` | S, N | `callExpr(callee(functionDecl(hasAnyName("::abort", "::exit"))))` |

Of the 30 rules, 4 need only flags that the code already satisfies, 17 need
no more than tokens or the syntax tree, 4 need declared names, 2 need types,
1 needs data flow, and 2 are checked by a harness or the build already.
Eight have no clang-tidy check that says the same thing (14, 15, 16, 23,
25, 26, 29, and 19 where it must stop at a directory's edge): the rules
most specific to this tree are the ones a general tool does not have.

## 6. A lint in vcc

### 6.1 What the front end can do on its own sources today

`vpp -x c++ -fsyntax-only -fcxx-dump=...` on every source file, alone, with
the build's flags (macOS, 5 jobs):

| | C++ front end (140 files) | C dialect (413 files) |
|---|---|---|
| tokens, headers included | 4.4 M | 19.5 M |
| files without a syntax error | 140 | 398 |
| nodes kept as unparsed tokens | 9,716 | 53,502 |
| parse time | 25 CPU s | 128 CPU s |
| declarations analysed | 66,379 | 415,353 |
| well-formed | 38,756 (58 %) | 173,532 (42 %) |
| ill-formed | 0 | 1 |
| unsupported (no rules yet) | 20,743 | 212,494 |
| analysis did not finish in 60 s | 0 files | 11 files |

- **The parser reads all of it.**  The 15 files with an error share one
  line, `os/base.h:338` of the macOS SDK (`typedef void (^os_block_t)(void)`,
  Apple's blocks); none is in our code.  The unparsed nodes of the front
  end's translation units come from 50 declarations: 37 in
  `src/headers/stdatomic.h` (`_Atomic`, which is C), 6 in C headers of
  `common/`, `lex/` and `pp/`, 5 in the SDK, and 2 in our C++, in the two
  files that do not compile alone.  Every rule of section 5 marked T or S
  can be checked on the whole tree now.
- **Analysis classifies 58 % of the front end's declarations.**  In the
  front end's own files 3,586 distinct constructs lack rules, and three
  causes are 88 % of them: `[basic.lookup.unqual]` on a
  `decl-specifier-seq` (2,256), `[basic.lookup.qual]` on a
  `nested-name-specifier` (544), `[class.mem]` member-declarators (367).
  Rules marked N or Y wait for those; the work list is the same one
  `PLAN-cxx-sema.md` already has, now ordered by what our own code needs.
- **Running it found things already**: 11 files of the C dialect on which
  analysis takes more than a minute (`ld/ld.cpp`, `arch/a64/as/enc.cpp`,
  `frontend/c/declaration.cpp`, ...: something is worse than linear), the
  macro-parameter crash of 4.1, and two files that do not compile alone.
- **Cost**: 0.28 CPU s per file to parse, 153 CPU s for the tree, 31 s of
  wall time at 5 jobs, since every process lexes the same headers again; a
  batch mode as C has (`vpp --batch`) would remove most of it.

### 6.2 What makes a lint usable (prior art)

- **Few rules, each one decided.**  Section 4: a group enabled wholesale is
  86 % noise here.  Go's `vet` admits a check on three tests: it finds real
  mistakes (correctness), in code people write (frequency), with almost no
  false alarm (precision).  Google's Tricorder holds each check to under
  10 % of findings that readers mark "not useful", and found that findings
  shown at review time on changed lines get fixed while a list of old
  findings does not (Sadowski et al., CACM 2018).
- **Levels and groups.**  Clippy sorts lints into `correctness` (an error),
  `style`, `complexity`, `perf` (warnings) and `restriction` (off; a project
  turns single ones on to ban a feature it has decided against).  Rules 1-8
  are a restriction group.
- **A suppression names the rule, gives a reason, and is checked.**  Rust's
  `#[expect(lint, reason = "...")]` warns when the lint no longer fires.
  The tree has the same habit for tests: an `xfail` that passes is an XPASS
  error.
- **Adoption by ratchet.**  A rule the tree does not satisfy yet gates on a
  baseline that may only shrink, as `bench/quality.py --check` does with
  `quality-baseline.json`.
- **Every rule has a page**: what it reports, why, a wrong example and the
  fixed one.  `vcc --explain ID` and `bench/vcc-explain.py` do this for
  diagnostics already.

### 6.3 Shape

- **Where**: `src/frontend/cplusplus/lint/`, one rule per file, and a table
  `lint/rules.inc` in the manner of `ext/extensions.inc` and
  `CPLUSPLUS_DUMPS`: `LINT_RULE(identifier, name, group, phase, default,
  reference)`.
- **How it runs**: `vpp -x c++ -fsyntax-only --lint FILE`, the option the C
  naming check has.  A rule names the deepest phase it needs, as a dump
  does (`dump/dump.h`: lexing, preprocessing, parsing, analysis), and runs
  when that phase is done, so a token rule does not depend on analysis
  being complete.
- **What it reports on**: the file named on the command line and the tree's
  own headers under the rule's directories; never a system header.  A token
  that a macro produced is reported at the invocation
  (`token_stream::location`), and only if the macro is ours.
- **Which files a rule covers is data**: a table of directories per rule, as
  `naming-lint.cpp` has for prefixes.  This is what a compiler flag cannot
  do: rule 19 holds in `frontend/cplusplus` and must stay silent in
  `include/common/*.h`, which is C.
- **No guessing.**  A rule that needs types skips a declaration analysis
  marked `unsupported` and counts it; the harness prints checked and not
  checked per rule, in the manner of `sema.h` ("A construct whose rules are
  not written yet is not guessed at").
- **Diagnostics**: `[-Wvcc-lint-NAME]`, an id `vcc_lint_*`, a page in
  `doc/diag/`, a fix-it where the fix is not a choice (`vcc --fix` exists).
- **Suppression**: a comment on the line or the declaration with the rule's
  name and a reason; one that suppresses nothing is an error.
- **Gate**: a step in `ci/premerge.sh` over the whole tree with `-j`; a
  local pre-commit hook on the changed files when a built `vpp` is there.
  The two hooks of today (rules 5 and 6) become rules and go away.

### 6.4 Validation: clang is the reference

As everywhere else in the tree, a rule is right when clang agrees.  Each row
of `rules.inc` names its reference; a harness (`bench/cxx-lint.py`) runs
both and compares the sets of `file:line:column`:

- on `src/test/lint/RULE-*.cpp`, small cases with snapshots of the
  expected output, as `test/diag`;
- on the tree itself, where the two must agree on every finding, and on
  zero;
- on mutants: a violation injected into a clean file must be reported, as
  `bench/vcc-mutate.py` does for errors.

clang-tidy needs no plugin for the reference side.  Since 22.1.0 a
`.clang-tidy` file can declare checks as `clang-query` matchers (section
2); in 23.1.4 it works behind `--experimental-custom-checks`.  Tried here
on the front end:

```yaml
CustomChecks:
  - Name: no-conditional-operator
    Query: 'match conditionalOperator().bind("op")'
    Diagnostic:
      - BindName: op
        Message: 'the conditional operator is not used in the C++ front end'
        Level: Warning
```

It reported 0 conditional operators and, with the matcher of rule 30, the
3 `abort()` calls in `owned.h`: what the hook and `grep` say.  The same
file with `readability-identifier-naming` set to `lower_case` is the
reference for rule 24.  clang-tidy is then a test oracle that CI may run
where clang 23 is installed, and never the gate.

### 6.5 What the run is worth as a test of the front end

Besides the rules, each CI run parses and analyses 553 files that change
every day.  Three numbers become gates of their own: files of ours with a
syntax error (0), nodes of ours left unparsed (0), and declarations of ours
by result, where `unsupported` may only fall.  The third turns "C++ support
complete enough to compile vcc" into a count that is visible per commit.

### 6.6 What stays with clang and the sanitizers

Rules that need data flow: use after move of an owner, a reference into an
`owned_array` kept across an `append` that reallocates it (the likeliest
memory error of this design; `append` returns such a reference), reads of
unset variables.  ASan sees the second at run time since owners allocate
with `realloc`.  A lint for them wants flow analysis the front end does not
have.  The cheap part is clang's: `[[clang::lifetimebound]]` on the
accessors of `owned_array` with `-Wdangling`, and clang 23's
`-Wlifetime-safety` once the owners carry `[[gsl::Owner]]`; neither was
tried here.

## 7. Order of work

Decided 2026-11-18: finishing the semantic integration into code generation
comes first.  Steps 2 to 5 below, and the features of a wider C++ in the
front end, are taken up once most of the test suite compiles.  Rules for
fast code are in `RESEARCH-cxx-performance.md`.

1. **Flags, no code.**  Done 2026-11-18: `CPLUSPLUS_FLAGS` in
   `src/Makefile` (`-fno-rtti -nostdinc++ -Wglobal-constructors
   -Wexit-time-destructors -Werror`) for the front end's objects (rules
   1-4); CI runs the conditional-operator hook on all files; the two files
   include `sema.h`, and `make cplusplus-check` compiles each of the 140
   files alone with those flags, a step of `ci/premerge.sh arm64` (rule 28).
   Clean with Apple clang and Debian's clang 23.1.6, also with the
   sanitizers' flags.  Still open: rule 5 can move from the hook into the compiler as GCC has it:
   `#pragma GCC poison malloc calloc realloc free strdup strndup` after the
   front end's internal header has included `owned.h`.  Tried: the front
   end's unity translation unit compiles with it once `common/softfloat.h`,
   a C header whose inline functions allocate, is included before the
   pragma.
2. **Decide** the open rules (section 8) and write them into `STYLE.md` as
   its C++ part.
3. **The lint's frame and the syntax rules**: the table, `--lint` for C++,
   suppression, baseline, `bench/cxx-lint.py` with clang as reference.
   First rules 5 and 6, since the hooks give a second oracle; then 7-14,
   16, 17, 19, 22, 27, 29.
4. **Names** (21, 24, 25, 30) as lookup covers our code.
5. **Types** (20, 23) when `[basic.lookup.unqual]` and `[class.mem]` are
   closed for our own sources.

## 8. Decisions needed

1. **Casts** (rule 19): named casts only?  260 to rewrite.  Recommended:
   yes.
2. **Internal linkage** (13): `static` for functions and an unnamed
   namespace only round types (LLVM's rule), or unnamed namespaces
   throughout?  Recommended: LLVM's rule; it reads the same in the C
   dialect.
3. **Output parameters** (18): pointer (the call site shows `&x`) or
   reference?  The code does both.
4. **`virtual`** (8): keep it out, or allow it with `override` and a
   non-public or virtual destructor?  Nothing uses it.
5. **Index types** (23): make `token_index`, `node_index`, `type_index`
   distinct types, so the compiler checks, or keep aliases and lint?
   Recommended: distinct types; a lint rule is the weaker tool here.
6. **`[[nodiscard]]`** (22) on every function that returns failure?
   Recommended: yes.
7. **`auto`** (14) as the code has it: lambdas and range-`for` only?
8. **Out of memory in `owned.h`** (30): `abort()` or `vout_of_memory`?
9. **Findings are errors from the first day**, with a baseline for what
   exists, or warnings until the tree is clean?  Recommended: errors with a
   baseline.
10. **The C dialect**: when does `VCC_LANG=c` end?  Until then the C++
    rules stop at `frontend/cplusplus`; after it, 413 files and 10,793
    casts meet rule 19, which is a migration to plan, not a lint to turn on.
11. **clang-tidy in CI** as the lint's oracle (needs `clang-tidy-23` in the
    container), or only locally?

## Sources

Style guides and their enforcement, fetched 2026-11-18:

- LLVM: <https://llvm.org/docs/CodingStandards.html>; `.clang-tidy`,
  `clang/.clang-tidy`, `clang-tools-extra/clang-tidy/llvm/LLVMTidyModule.cpp`,
  `.github/workflows/pr-code-lint.yml` in `llvm/llvm-project`.
- GCC: <https://gcc.gnu.org/codingconventions.html>; `gcc/system.h`,
  `gcc/configure.ac`, `contrib/check_GNU_style_lib.py`.
- GDB: `gdbsupport/poison.h`, `gdbsupport/warning.m4`,
  `.pre-commit-config.yaml` in binutils-gdb.  The wiki pages on its C++
  conventions could not be fetched.
- Ladybird: `Documentation/CodingStyle.md`, `.clang-tidy`,
  `Meta/lint-ci.sh`, `Meta/Linters/`, `Meta/ClangPlugins/` in
  `LadybirdBrowser/ladybird`.
- Chromium: `styleguide/c++/c++.md`, `styleguide/c++/c++-features.md`,
  `PRESUBMIT.py`, `docs/clang_tidy.md`, `docs/unsafe_buffers.md`,
  `tools/clang/plugins/`, `base/memory/raw_ptr.md`, `.clang-tidy`.
- WebKit: <https://webkit.org/code-style-guidelines/>;
  <https://github.com/WebKit/WebKit/wiki/Safer-CPP-Guidelines>;
  <https://clang.llvm.org/docs/analyzer/checkers.html>;
  `Tools/Scripts/webkitpy/style/checkers/cpp.py`.
- Firefox: `build/clang-plugin/Checks.inc`, `mfbt/Attributes.h`,
  `tools/clang-tidy/config.yaml` in `mozilla-firefox/firefox`.
- Google: <https://google.github.io/styleguide/cppguide.html>; cpplint.
- Orthodox C++: <https://bkaradzic.github.io/posts/orthodoxc++/>; Dear
  ImGui `docs/CONTRIBUTING.md` and `docs/FAQ.md`; bx `README.md`; EASTL's
  rationale, N2271.
- JSF AV C++ coding standards, 2005:
  <https://stroustrup.com/JSF-AV-rules.pdf>.

Tools:

- clang-tidy: <https://clang.llvm.org/extra/clang-tidy/>, its
  `QueryBasedCustomChecks.html` and
  `checks/readability/identifier-naming.html`; release notes of 21.1.0,
  22.1.0 and 23.1.0 at <https://releases.llvm.org/>.
- clang: <https://clang.llvm.org/docs/LifetimeSafety.html>,
  <https://clang.llvm.org/docs/SafeBuffers.html>.
- Sadowski, Aftandilian, Eagle, Miller-Cushon, Jaspan, "Lessons from
  Building Static Analysis Tools at Google", CACM 61(4), 2018.
- Go: `cmd/vet/README`, the criteria for a check.
- Clippy: its lint groups; Rust's `#[expect]` attribute (RFC 2383).

Measurements: clang-tidy, clang-query and clang 23.1.4 (Homebrew) and vcc
at `20b1d026`, on macOS; every source file alone with the build's flags
(`-x c++ -std=gnu++20 -fno-exceptions -D_DARWIN_C_SOURCE -Isrc/include`).
