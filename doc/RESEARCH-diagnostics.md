# Diagnostics: state of the art and design for vcc

Research of 2026-11-09. Sources: three web research passes (rustc/Elm, Zig/Go/Swift,
GCC 13-16/clang/renderers/TypeScript/Python) and a local count of clang's diagnostic table
(Homebrew clang 23.1.4, `diagtool`). Points not checked against a primary source are marked
*(unverified)*.  vcc is a general C compiler: the kernel is one workload, PostgreSQL another.

## 1. Size of the problem

Clang 23 has about **7 300 diagnostic ids** (names extracted from `diagtool`'s tables):

| prefix | count | | area (by name keyword, rough) | count |
|---|---|---|---|---|
| `err_` | 3 916 | | C++ (templates, classes, constexpr, modules, ...) | ~2 300 |
| `warn_` | 1 770 | | OpenMP / OpenACC | ~1 140 |
| `note_` | 1 120 | | driver, frontend, PCH, serialization | ~600 |
| `ext_` + `compat_` | 461 | | Objective-C | ~440 |
| `remark_`, `fatal_` | 30 | | OpenCL/CUDA/HLSL/SYCL | ~260 |
| | | | target-specific (NEON, SVE, x86, RISC-V, ...) | ~105 |
| | | | **remainder (mostly C, some C++ left in)** | **~3 150** |

2 236 warnings have a `-W` flag, 56 have none.  Estimate for what can fire on C: **1 500-2 000**
(~1 000 errors, ~600 warnings, ~300 notes).  vcc today: ~200-400 distinct messages, 7 `-W` flags.
An exact C coverage table needs clang's `Diagnostic*Kinds.td` (message text + language);
not downloaded yet.

For scale: rustc has ~520 error codes (highest E0806), each with a long explanation.

## 2. Where each one really shines

### rustc: the structure
- **Labelled multi-spans.** One primary span (what is wrong) and secondary spans (why:
  "expected due to this"), drawn in source order so the error reads as a story.
  `Diag` = message + `MultiSpan` + children (note/help) + suggestions.
- **Suggestions with `Applicability`** (`MachineApplicable`, `MaybeIncorrect`,
  `HasPlaceholders`, `Unspecified`).  Only machine-applicable ones are applied by
  `cargo fix`/IDEs; shown as `+`/`~` diffs.  Style rule: suggestions are not questions.
- **`ErrorGuaranteed` + error type: no cascades.**  A zero-size token only the emitter can
  create; recovery nodes (the error type) need one, every check accepts the error type
  silently.  `delayed_bug`: "an error must have been reported", else ICE.
- **Error codes + `rustc --explain`**: Markdown page per code, wrong and fixed example,
  examples compiled as doctests.  Rule: a code only if the explanation adds something.
- **`--error-format=json`**: spans with `is_primary`, label, replacement, applicability,
  macro `expansion` chain, nested children, and `rendered` (the exact text, so tools never
  re-render).
- **Contributor tooling**: `-Z track-diagnostics` (compiler source location of each
  diagnostic), `-Z treat-err-as-bug=N`, a dropped un-emitted `Diag` is an ICE.
- **UI tests**: inline `//~ ERROR`, `.stderr` snapshots with `--bless`, `run-rustfix`
  applies fixes, compares to `.fixed` and checks it compiles.
- **Lesson from Fluent**: translation infra removed in 2026 (compiler-team MCP #967).
  Keep typed diagnostic definitions, skip i18n.

### Elm: the wording
- Header naming the error kind and file; plain-language summary *above* the snippet;
  code shown exactly as written; the more specific hints below, so you stop reading when
  you have enough.
- **Mismatch names the role**: "the 1st argument to `add` is not what I expect", then found
  vs. expected types stacked, large records reduced to the differing fields.
  Implementation: every type constraint records why it was created (argument N of f,
  if-branch, annotation) at negligible cost.
- **Syntax errors**: say what construct was being parsed, show a valid example of it,
  propose the concrete next token.
- **Error message catalog** (`elm/error-message-catalog`): a corpus of programs that
  trigger each message, used to review and regress wording.
- Not for us: the first-person chatty voice on the first line (tools parse
  `file:line:col: error:`); the column-0 declaration rule.

### Zig: the "why is this compiled"
- Child notes belong to their error and are sorted with it.
- **Reference traces** ("referenced by: foo a.zig:10:5, main ..."), `-freference-trace=N`.
- Comptime call stacks: "called from here" per frame.
- `zig ast-check`: per-file, type-free checks (unused locals, never-mutated `var`,
  unreachable code), cached, run by ZLS on every keystroke.
- Errors stored per analysed unit, so an incremental re-check replaces exactly that unit's
  errors; deterministic sort by file/line.  Terse: lowercase, no period, one fact per line.
- Run time: error return traces (where an error was created and how it travelled).
- Not for us: lazy analysis (C checks every definition).

### Go: terseness and the fix framework
- gc: `file:line:col: msg`, sorted, stops at 10 ("too many errors").
- **Error codes as documentation** (`internal/types/errors`): each code's doc comment says
  when it fires, with an example; gopls links it via LSP `codeDescription`.
- **Analysis framework**: analyzers with dependencies and cross-package "facts",
  diagnostics carry `SuggestedFixes` (text edits); the same analyzers run in vet, gopls and
  `-fix`.  gopls quick fixes: fill missing returns, `:=` to `=`, create undeclared name.

### Swift: discipline
- **Style guide** (`doc/Diagnostics.md`): one phrase, no period; state the rule, not the
  failure; mention the plausible fix even without a fix-it.
- **Fix-it rule**: a fix-it on the error only if it is the one obvious, almost certainly
  right fix; alternatives go on notes, safest first, each saying what it does.
- **Diagnostic groups**: `[#GroupName]` suffix with a link to a Markdown page
  (`userdocs/diagnostics/`), `-Werror <group>`; `-debug-diagnostic-names`.
- `-verify` with fix-it checks (`{{3-9=replacement}}`, `{{none}}`) and child-note checks.
- Diagnostics inside macro expansions nested under the outer file where they occur.

### GCC 13-16: output plumbing and pictures
- **Several sinks at once**: `-fdiagnostics-add-output=sarif` (text on stderr and a
  `.sarif` file).  JSON format deprecated in 15, removed in 16, for SARIF.
- **Rich SARIF**: secondary locations, labelled ranges, the include chain as
  `locationRelationship`, fix-its, argv/cwd.  `sarif-replay` renders SARIF back to text:
  a test oracle (text == render(SARIF)).
- **Nested diagnostics** (default in 16): indented bullets instead of 30 flat notes.
- **Contrast colouring** (15): each side of a comparison (operand types, expected/actual)
  gets its own colour, used for the type in the message, its range and its label.
- **Analyzer paths**: numbered events joined by control-flow lines; out-of-bounds
  **memory diagrams** (buffer extent vs. access range); `-fdiagnostics-text-art-charset`.
  GCC 16: experimental HTML output with step-by-step state.
- **C front-end details**: missing `&`/`*` fix-its; known-header `#include` hints;
  C23 transition notes (`bool` is a keyword in C23); argument-count errors "expected 0,
  have 1" pointing at the function-pointer typedef; OSC 8 links on option names;
  `-fdiagnostics-generate-patch` (all fix-its as a unified diff).
- libgdiagnostics: the renderer as a C library.

### clang: the baseline we match
Good text (carets, ranges, fix-its, typo correction, macro notes, include stack,
`static_assert` operand evaluation "evaluates to '12 == 13'").  Weak: flat note lists, no
contrast, SARIF still "unstable" (add-output, nested SARIF and stable ids are open PRs in
2025-26), no paths or pictures, no explanations.

### Renderers and others
- **ariadne**: many labels per snippet with crossing-free connectors, colour per label,
  multi-line spans, tabs and wide characters right.
- **miette**: code + docs URL per diagnostic, `help:` line, related diagnostics grouped,
  and a **narratable** (screen-reader) handler chosen under `NO_COLOR`/non-tty.
- **TypeScript**: elaboration chains ("A is not assignable to B" > "types of property
  'x' are incompatible" > leaf); `relatedInformation` ("'x' is declared here"), which is
  LSP's `DiagnosticRelatedInformation`.
- **Python 3.11-3.14**: `~~~^^^` under the exact failing subexpression; "did you forget to
  import 'sys'?" from a frozen stdlib list; keyword typos ("did you mean 'while'?"); stray
  quote hints.

### Evidence that it matters
Rust survey 2021 (9 354 answers): error messages the most praised area, 90 %.  Barik et
al. 2017 (eye tracking): 13-25 % of task time spent reading errors, and reading difficulty
predicts task performance.  Becker et al. 2019: 50 years of studies; effects of "enhanced"
messages are mixed, so measure, do not assume.

## 3. What vcc takes (ranked by value per effort)

Constraint: the default text stays byte-identical to clang (`vcc-diag.py`); everything
new is opt-in or rides on colour/extra sinks, as GCC does.

1. **Structured diagnostics.**  A record per diagnostic: id, severity, primary span +
   label, secondary spans + labels, children, suggestions with applicability, macro and
   include context.  Renderers: `clang` (today's text), `rich`, SARIF 2.1, JSON-lines
   with `rendered`.  Zero cost without errors.  (rustc, GCC, all)
2. **No cascades.**  An error type/declaration that every check accepts silently, plus a
   guarantee that an error was emitted; recovery at `;`, `}` and declaration starts.
   (rustc; TODO.md "Error recovery")
3. **Stable ids with docs.**  The `-W` flag name or a code, `vcc --explain X` from a
   generated table, examples compiled in CI, OSC 8 links.  (rustc, Go, Swift, miette)
4. **Labelled spans and role naming.**  "argument 2 of 'memcpy'", "return value of 'f'",
   "previous declaration here" as a secondary label in the same snippet.  (rustc, Elm, TS)
5. **Type diffs and contrast colouring.**  Show only the differing part of two types
   (a `const`, one `*`, a struct tag, one field of a function-pointer type), one colour
   per side.  (GCC 15, Elm, ariadne)
6. **Macro expansions as nesting.**  The expanded text with the caret in it, collapsible
   frames, macros from system headers folded by default.  (Swift, Zig comptime stacks,
   GCC nesting; vcc's pp has the token history)
7. **Fix-its with the Swift rule and applicability.**  Missing `&`/`*`, `#include` for
   known libc/POSIX names, C23 keyword notes, `-fdiagnostics-generate-patch`, `vcc --fix`
   applying machine-applicable ones only.  (GCC, Python, rustfix, Swift)
8. **Build-wide view in vccd.**  A header warning printed once with "(also in N TUs)";
   per-TU storage so a re-check replaces that TU's set; the same records to an LSP.
   Works under make, ninja or anything using the shim.  (Zig, gopls)
9. **Tests as rustc does them.**  Inline annotations, `.stderr` snapshots per renderer
   with a bless mode, a fix round-trip (apply, compare, recompile), text == render(SARIF),
   an Elm-style catalog that triggers every message.  (rustc, Swift, GCC, Elm)
10. **Later**: flow paths for `-Wuninitialized`/stack address/missing return and
    out-of-bounds memory diagrams (GCC analyzer); a narratable mode (miette).

Not taken: translation/i18n, chatty first-person first lines, lazy analysis,
mandatory unused-variable errors (only as an opt-in strict mode).

## Sources
- rustc-dev-guide: diagnostics, error-guaranteed, diagnostic-structs, test/ui
- blog.rust-lang.org/2016/08/10/Shape-of-errors-to-come; doc.rust-lang.org/error_codes;
  github.com/rust-lang/compiler-team/issues/967; Rust surveys 2021 and 2025
- elm-lang.org/news/compiler-errors-for-humans, /compilers-as-assistants, /the-syntax-cliff;
  github.com/elm/error-message-catalog
- ziglang.org/documentation/master/#Error-Return-Traces; reference-trace rework *(unverified
  details)*
- pkg.go.dev/internal/types/errors; pkg.go.dev/golang.org/x/tools/go/analysis;
  gopls doc/features/diagnostics.md
- github.com/swiftlang/swift/blob/main/doc/Diagnostics.md; swift-syntax PR #2214
- developers.redhat.com: GCC 14 static analysis (2024-04-03), 6 usability improvements in
  GCC 15 (2025-04-10), GCC 16 error messages and SARIF (2026-04-28); gcc.gnu.org changes
- github.com/zesterer/ariadne; github.com/zkat/miette; PEP 657; Python 3.12/3.14 what's new
- Barik et al. 2017; Becker et al. 2019 (via neverworkintheory.org)
