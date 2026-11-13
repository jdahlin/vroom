# Making the first hour of C feel supported

Research and design proposal, 2026-11-11. This is a proposal, not a claim that the
commands or messages proposed below already exist. Scope: Linux first, x86-64 followed
by AArch64 under QEMU; macOS later. Research branch: `research/compiler-onboarding`.
Local audit baseline: `6bb8e38` (the isolated worktree's starting commit).

## Recommendation

Treat onboarding as a sequence of successful repairs: install, run a tiny program,
break it deliberately, understand the diagnostic, make a justified change, run again.
C can support this without changing its grammar, ABI, libraries, or build systems.
The compiler should explain the evidence it has and distinguish a language violation,
a suspicious but legal operation, a missing build input, and a runtime problem.

vcc already implements much of the display infrastructure associated with modern
compilers. The highest-value next step is a small, tested first-program guide and
semantic help for common errors, using the existing records and renderers. Another
rich renderer, a mandatory project manager, or a new C dialect would miss that need.

Success is not a larger number of hints. A new user should be able to answer:

1. What part of my program or command is wrong?
2. What did the tool expect, and where did that expectation come from?
3. Which change is justified, and which alternatives depend on my intent?
4. Did compilation, linking, or running fail? What command should I try next?

## Evidence and limits

The language comparisons below use official documentation and compiler source,
retrieved on the research date. These are documented workflows, not timed usability
experiments or a ranking of whole language ecosystems. Rust, Zig, Elm, and Go were
not installed or benchmarked for this research. Version-specific output can change;
use the documented matching release rather than copying a development example.
Links to moving source branches establish the design inspected here, not a frozen
release guarantee.

The vcc audit reads code, diagnostic snapshots, explanatory pages, and the existing
[diagnostic plan](../PLAN-diag.md). Historical green counts in that plan are historical
evidence, not fresh validation of the concurrent compiler work. This task runs no
compiler builds and does not disturb the Csmith workers.

## What the modern language journeys actually offer

| Ecosystem | First working program | First-failure support | Transfer to C; qualification |
|---|---|---|---|
| Rust | `rustup`, version check, `cargo new`, `cargo run`; `cargo check` offers a shorter edit/check loop. | Labelled source spans, related causes, error codes and expanded explanations; suggestions carry applicability in JSON. | A single documented path and a recoverable error can teach a concept. Cargo's package ownership does not transfer directly to arbitrary C/make projects. Native linker prerequisites still exist. |
| Zig | A self-contained compiler archive and explicit PATH setup; `zig init`, `zig build run`, `zig build test`. | The versioned language reference includes failing examples alongside the rule; notes can connect a failed coercion to its cause. | Document a known-good version and complete small commands. Zig's different language rules and lazy analysis do not establish what a C compiler may reject. |
| Elm | The guide allows online learning before local installation; locally `elm init`, `elm reactor`, then `elm make`. | Context-specific explanations identify the role of an expression; the guide promises consistent messages across browser, reactor, and command line. | Explain “argument 2” or “returned value,” and keep representations consistent. Elm's application shape and managed browser execution remove difficulties that remain real in C. |
| Go | Install Go, create a directory and `go.mod`, write `hello.go`, run `go run .`; the tutorial then introduces a dependency. | The compiler stays concise; gopls adds continuous parsing/type errors and analyzer findings, with quick fixes. | Combine a dependable terminal path with optional editor feedback. Distinguish compiler evidence from analyzer heuristics. Editor features are not all provided by the Go compiler. |

Rust's installation guide explicitly calls out the native linker and platform tools;
“one installer” does not mean the entire native environment is automatic. Its Cargo
chapter provides a common project loop. These are complementary lessons: reduce
choices while naming prerequisites honestly. [Rust installation](https://doc.rust-lang.org/book/ch01-01-installation.html),
[Cargo first project](https://doc.rust-lang.org/book/ch01-03-hello-cargo.html).

Rust's diagnostic design separates the main fact, labelled evidence, children, and
suggestions. Its JSON includes machine-readable spans and replacement applicability;
`cargo fix` only sees the configuration it checks, so automatic repairs are not proof
that every target is correct. Adopt that separation and limitation in vcc.
[Rust diagnostic design](https://rustc-dev-guide.rust-lang.org/diagnostics.html),
[JSON contract](https://doc.rust-lang.org/rustc/json.html),
[cargo fix](https://doc.rust-lang.org/cargo/commands/cargo-fix.html).

Zig's official start page recommends different choices for stable users and development
contributors, and directs learners to the documentation for their compiler version.
Its generated project includes a test command. vcc should likewise publish a tested
installation/toolchain matrix and keep tutorial commands aligned with it.
[Zig getting started](https://ziglang.org/learn/getting-started/),
[versioned language reference](https://ziglang.org/documentation/).

Elm is particularly relevant because its compiler records why a type was expected:
call argument, branch, annotation, record access, and so on. That information makes a
message explain a relationship rather than merely list two types. The compiler also
has a JSON report path. Its accessible first-run workflow is documented independently
of the compiler's prose. [Elm local workflow](https://guide.elm-lang.org/install/elm.html),
[expectation context in compiler source](https://github.com/elm/compiler/blob/master/compiler/src/Reporting/Error/Type.hs),
[make report selection](https://github.com/elm/compiler/blob/master/terminal/src/Make.hs).

Go's tutorial assumes some programming experience and separately explains the terminal,
module, source file, and execution steps. gopls documents distinct compilation and
analysis sources and distinguishes fixes suitable for fix-all. vcc should preserve
those distinctions instead of attributing editor behavior to the compiler.
[Go first program](https://go.dev/doc/tutorial/getting-started),
[gopls diagnostics and fixes](https://go.dev/gopls/features/diagnostics).

The common opportunity is continuity: the installation instructions, example program,
compiler, explanation, and editor agree about what happened. Good prose on an isolated
error cannot compensate for an undocumented sysroot or an executable the user cannot
find.

## Existing vcc capabilities: build on these

| Capability found in this checkout | Evidence | Remaining onboarding question |
|---|---|---|
| Structured records: ID, severity, warning flag, labels, children, help, edits and applicability | [`pp.h`](../src/include/frontend/pp/pp.h), `vdiag`, `VDA_*` | Do common beginner errors populate these fields with the cause, not just the location? |
| Default vcc source display, clang-compatible display, narrated display; ASCII/Unicode choices | [`diagtext.c`](../src/frontend/diag/diagtext.c), driver flag parser | Are all three understandable without color, and does help remain useful when read aloud? |
| JSON records, SARIF and standalone HTML; text reconstruction from JSON | [`diagout.c`](../src/frontend/diag/diagout.c), [`diag-render.py`](../bench/diag-render.py) | Do installation, driver and linker failures have comparable structured evidence? Do not assume frontend coverage proves this. |
| Parser recovery and invalid nodes/declarations to suppress dependent errors | [`frontend/c/diag.c`](../src/frontend/c/diag.c), diagnostic plan D2 | Does one beginner mistake still expose the next independent mistake? Fewer diagnostics alone can mean premature termination. |
| Known library-header suggestions, typo suggestions, conversion help and format checks | Existing diagnostic tests; plan D5 | Can the hint distinguish an absent declaration from an unavailable header/sysroot? |
| `--fix`, parseable edits and unified patches; only machine-applicable edits are written by `--fix` | Driver parser and `diagout.c:fix_collect`/`fix_write` | Make the preview/write distinction explicit. A patch can contain speculative suggestions; it is not equivalent to safe automatic repair. |
| Twelve explain pages, compiled into the executable, each with wrong/fixed examples | [`doc/diag/README.md`](../doc/diag/README.md), [`vcc-explain.py`](../bench/vcc-explain.py) | IDs exist beyond these pages. Teach discovery and grow pages where they add conceptual value. |
| Per-TU diagnostic store and build-level deduplication | `server/dstore.c`, plan D7 | A diagnostic query socket is not a complete language server. Unsaved-buffer handling, versioned edits, cancellation and compile context still need a contract. |
| Snapshots, mutation tests, fuzzing, catalog and fix round trips | `bench/vcc-diag.py`, `vcc-mutate.py`, `vcc-diag-catalog.py`, `src/fuzz/fuzz-diag.c` | Add task outcomes and novice review; snapshots establish consistency rather than comprehension. |

Two concrete audit findings deserve follow-up:

- This checkout has engineering plans and individual explanations but no root README
  or cohesive first-program guide. The shim can auto-start its daemon; a learner
  should not need to understand that to compile, and its failure must identify a
  tool setup problem rather than imply a C source error.
- `fix_write` writes a replacement made from the compiler's source snapshot and then
  renames it over the path. It does not compare current content to that snapshot in
  the inspected implementation. An editor changing the file during compilation can
  therefore lose edits. A content/version check should precede any promised editor
  quick-fix workflow. This is a concrete source observation, not a reproduced race.

The older [diagnostics research](../RESEARCH-diagnostics.md) contains a “default text
stays byte-identical” constraint; the newer plan and renderer intentionally improve
vcc's default text while retaining the parseable leading location/severity line.
Use the current plan and code as the design baseline.

## Ten beginner repair stories

The “before” column below describes the insufficient information a learner may be
left with; it is **not** a claimed transcript from vcc or another compiler. Every
“proposed help” block is **new design wording**, even when its underlying detection
already exists. Preserve the established leading diagnostic line and attach help
rather than silently changing severity. Code fragments assume their ordinary enclosing
function and required declarations unless shown otherwise.

### 1. A missing semicolon: syntax, not the next line's fault

```c
int count = 3
return count;
```

Before: an expected-token error can leave the user wondering why `return` is wrong.
Current evidence: `tests/diag/missing_semi_decl.c.stderr` already points to the end of
the declaration and offers `;`; retain that behavior.

Proposed help:

```text
help: finish the declaration of 'count' with ';'
```

After: `int count = 3;`. Show the insertion at the preceding line, retain the next
statement, and only auto-apply when the existing conservative insertion rule holds.
An unmatched parenthesis must not receive the same unconditional repair.

### 2. A library function without its header: declaration, not implementation

```c
int main(void) { puts("Hello"); }
```

Before: “undeclared function” does not explain how standard functions become visible.
Existing header hints and `ext_implicit_function_decl_c99` explanation are a foundation.

Proposed help:

```text
help: include <stdio.h> before calling 'puts'; it declares the function
note: including a header supplies declarations, not a copy of the library's code
```

After: add `#include <stdio.h>`. Do not suggest a guessed prototype or a warning
suppression. If the header is already included but hidden by conditions, investigate
that context instead of duplicating an include. Test hosted and freestanding modes.

### 3. A header exists in the tutorial but not in the installation

```c
#include <stdio.h>
int main(void) { return 0; }
```

Before: “file not found” can make a correct program appear misspelled.

Proposed help, only when target/include-path evidence supports it:

```text
help: this target needs C library development headers
note: target: aarch64-linux-gnu
note: the selected sysroot has no usable <stdio.h>
help: check the sysroot and include search paths for this target
```

After: correct the toolchain/sysroot configuration, leaving the source unchanged.
Do not infer a particular package manager from a target triple. Distinguish a missing
user header (`"project.h"`) from missing standard headers; show searched paths on
request. Installation preflight should catch this before the first lesson.

### 4. A misspelled name: offer a candidate without inventing intent

```c
int count = 3;
return coutn;
```

Before: an unknown identifier names the symptom but makes the learner hunt.

Proposed help:

```text
help: a visible variable is named 'count'
note: 'count' was declared here
```

After, if intended: `return count;`. Keep typo replacement speculative. Two similarly
ranked names must not produce an automatic choice; names hidden in a different scope
are not interchangeable. Recovered uses should not cause arithmetic/type cascades.

### 5. Passing a value where a function needs a writable address

```c
void read_count(int *out);
int count;
read_count(count);
```

Before: “integer to pointer conversion” can encourage a cast that hides the problem.

Proposed help:

```text
note: parameter 'out' expects 'int *', an address of an int
help: if the function should write into 'count', pass '&count'
```

After, if intended: `read_count(&count);`. Label the parameter declaration and the
argument. Never auto-apply an address operation solely because types then match;
the API's intended use still matters. A `scanf("%d", count)` lesson should additionally
explain its format contract and check the function's return value.

### 6. The output format disagrees with the argument

```c
long count = 42;
printf("%d\n", count);
```

Before: listing two types may not teach that a variadic call follows the format.
Current snapshots already underline both the format and argument and suggest `%ld`.

Proposed help:

```text
help: '%ld' prints a long; '%d' requires an int
note: printf uses the format to read its arguments; a mismatched type can cause undefined behavior
```

After: `printf("%ld\n", count);`. Do not recommend a narrowing cast as the universal
fix. For `size_t`, prefer `%zu`; fixed-width integer typedefs need portable treatment
such as the `<inttypes.h>` macros. Verify both Linux targets and target-specific types.

### 7. Assignment in a condition is legal and sometimes intended

```c
if (count = 0) { puts("empty"); }
```

Before: a bare warning may imply that `=` is forbidden here.

Proposed help:

```text
note: '=' changes 'count'; the assigned value is used as the condition
help: use '==' to compare, or parenthesize the assignment if it is intentional
```

After, for comparison: `if (count == 0)`. Show alternatives without auto-selecting one;
`--fix` must leave this case untouched. Existing explanation already follows this
principle. Preserve the controlled `-Wparentheses` warning rather than make legal C
an unconditional error.

### 8. A variable has a value only on one path

```c
int result;
if (ready) result = 7;
return result;
```

Before: a warning at the final use can obscure why initialization was missed.

Proposed help:

```text
note: when 'ready' is false, this return reads 'result' without assigning it
help: assign a result on every path before returning
```

After: add an appropriate `else`, return directly in each branch, or initialize with a
meaningful value. Do not auto-insert zero: it may convert a visible defect into wrong
results. Existing sometimes-uninitialized tests already carry path/use notes; audit
clarity and false positives before adding analysis. The compiler need not solve all
possible runtime paths to provide a useful warning on this example.

### 9. A declaration compiled, but the definition was not linked

```c
/* main.c */
int answer(void);
int main(void) { return answer() != 42; }
/* answer.c */
int answer(void) { return 42; }
```

Command: `vcc main.c -o hello`.
Before: an undefined-symbol failure can send the learner back to adding headers.

Proposed linker/driver help, when reference attribution is available:

```text
error: undefined reference to 'answer'
note: referenced by main in main.c
help: link the source or object that defines 'answer'
note: a declaration permits a call; the final executable also needs a definition
```

After: `vcc main.c answer.c -o hello`. Do not guess a missing filename from a symbol.
Test compilation with `-c` separately: producing an object with an unresolved external
reference is normal. Do not report the missing definition as a frontend error.

### 10. Successful compilation does not mean a target program can run here

Command on x86-64: build for AArch64, then execute `./hello` directly.
Before: a shell “Exec format error” can appear to be a compiler crash.

Proposed documentation/troubleshooting guidance (not a fabricated compiler error):

```text
This executable targets AArch64 Linux; this host runs x86-64 Linux.
Run it on an AArch64 system, or use QEMU with the matching target runtime.
For the documented cross-toolchain layout:
qemu-aarch64 -L /usr/aarch64-linux-gnu ./hello
```

After: run through the verified QEMU/sysroot pair, or compile for the native host.
The shown path is an example for the documented test environment, not a universal
installation rule. A missing dynamic loader is a separate configuration failure.
Do not promise that vcc intercepts shell execution errors or detects every runtime
bug. A program can compile, link and run successfully while still containing bugs.

## A coherent first-hour path

Publish a Linux guide with a tested distribution/toolchain matrix and the actual
supported installation method. Do not advertise an unverified package-manager
command or imply that a compiler binary includes libc headers and startup objects.
For each supported setup, verify version reporting and compile/link/run prerequisites
before the lesson. The native example should be plain C:

```c
#include <stdio.h>
int main(void) {
    puts("Hello, C!");
    return 0;
}
```

Then show the *verified* public invocation, output filename and execution command.
Teach `-fsyntax-only`, `-c`, and final linking in that order, with one sentence on each
artifact. Teach one deliberate semicolon error and one format error; explain the
location, source, help, warning flag and optional ID. Introduce `--explain` only after
an error that benefits from it. Offer narrated output and no-color output beside the
normal display. Keep compiler-internal setup and server implementation out of the
happy path.

Retain make. A tiny optional sample Makefile can show the same commands as the manual
lesson, without requiring a project generator or dependency service. Cross-compiling
is a second lesson with an explicit target, sysroot and QEMU command. Do not change
native defaults merely to make the cross-target lesson shorter.

A new “beginner mode” is not required for the first milestone. Default diagnostics
should be understandable to experienced and new users alike. Longer conceptual
teaching belongs in optional explanations; experts retain concise output, warning
controls and their current build commands. If a later teaching profile enables
additional warnings, publish its exact expansion and keep it distinct from `-std`.

## Prioritized roadmap and acceptance evidence

| Priority | Work | Acceptance evidence |
|---|---|---|
| P0 | First-program guide plus installation/toolchain preflight checklist | From each supported clean Linux setup: native Hello output, deliberate failure and repair, `-c` then link, followed by ARM/QEMU. Record exact versions/commands and distinguish missing packages from invalid C. |
| P0 | First five repair stories using existing records | Dedicated snapshots verify primary location, cause label, actionable help, warning severity/flag and no speculative automatic edit. At least one unrelated later error remains discoverable. |
| P0 | Safe edits against current source content | Concurrent-file-change regression proves `--fix` refuses stale edits without changing the newer file; successful nonoverlapping fixes remain atomic and repeatable. Verify system headers, macros, symlinks and permissions explicitly. |
| P1 | Role-aware conversion and format help; extend explain pages | Expected and actual types plus parameter/return provenance survive text, narrated, JSON and SARIF. Wrong/fixed examples compile under the stated target and mode; explanations distinguish consequences from certainty. |
| P1 | Driver/linker/setup failures receive useful attribution | Missing input, unreadable file, missing standard header, bad target/sysroot, missing object definition and missing entry point have separate tests. `-c` accepts legitimate unresolved references. No made-up package or library fix. |
| P1 | Small first-hour acceptance corpus | Ten stories above include positive neighbors, ambiguous alternatives, macro variants and multi-error cases. Run native x86 first, then ARM/QEMU; no change to existing accepted C or warning controls. |
| P2 | Document editor integration and compile context | A supported adapter maps stable IDs, related spans and edits; handles unsaved document versions and stale responses. Target, defines, include paths and C standard match the actual build. |
| P2 | Evidence-based usability review | Observe 5–8 first-time C users performing repairs without hints; record first successful run, time to justified repair, mistaken edits, help discovery and confidence. Treat this as formative evidence, not population-wide statistical proof. |

C editors need the actual compilation context: target, language mode, include paths,
macros, working directory. Clangd's official design documents why missing context
creates misleading diagnostics. Start with a documented compilation database workflow;
a future vcc adapter should consume the same real commands rather than guess them.
[Clangd compile commands](https://clangd.llvm.org/design/compile-commands).

For every new diagnostic scenario, define the correct repair outcome before taking a
snapshot. Compile the fixed example and inspect its behavior where meaningful. For
ambiguous warnings, prove that the compiler does not silently choose an intent.
Apply only machine-safe fixes, rerun, and verify that unrelated source was preserved.
A lower error count with missing independent errors is a failure, not improved recovery.

## Proposed first milestone: the first error is repairable

Make a small, reviewable change covering stories 1–5, the first-program guide and a
content check before source rewriting. Reuse the current renderer and APIs. Add help
only where the existing message lacks the next justified action; retain a snapshot
unchanged when it already teaches the point. No new compiler mode, package manager,
LSP implementation, language rule or optimization is required.

Deliverables:

- `doc/getting-started-linux.md` with verified native and ARM/QEMU steps and a short
  compiler/linker/runtime troubleshooting table.
- A compact `tests/diag/onboarding_*` corpus with positive neighbors and deliberate
  ambiguity, linked from the guide and diagnostic catalog.
- Focused explanation additions where they teach declaration vs definition, value vs
  address, or source error vs toolchain configuration.
- A stale-source fix regression and documented preview/apply behavior.

Done means those commands work from the stated starting setup; every deliberately
broken example produces its intended diagnostic; the justified repair compiles; the
independent-error and ambiguous-fix tests pass; and existing parity, diagnostic and
performance gates pass. A source-only research document does not satisfy that milestone.

## Compatibility and performance constraints

Keep accepted C, ABI and standard selection unchanged. A suspicious legal operation
remains governed by its warning group and existing `-Werror` controls. Preserve the
parseable location/severity line and machine-readable IDs; additional prose belongs
in labels/help/notes, not a second unstructured output stream. Preserve make and
existing command-line contracts. Documentation may recommend explicit warning flags,
but must not conceal a language change behind a friendly preset.

Construct explanation strings, diagrams and full diagnostic records on the error
path. Use existing token locations, declaration links and interned types for common
help. Avoid an always-on whole-program analysis or symbol search for every valid
expression. Store long explanations in the generated existing table. Measure required
metadata changes rather than declaring them free: the repository's 1% instruction
budget applies, and successful builds must pass the established performance gate.
Also measure error-rich files for bounded recovery and output behavior. Do not run
heavy concurrent benchmarks while Csmith or other agents are loading the shared host.

The product promise should be precise: vcc helps users understand and repair what it
can establish, explains uncertainty when intent matters, and clearly directs them
when the next problem belongs to the build environment or the running program.
