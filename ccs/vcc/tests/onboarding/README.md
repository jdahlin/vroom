# Beginner C diagnostic repair corpus

B01–B25 are an initial set of C mistakes and suspicious constructs that a learner may
need help understanding. These IDs are stable. They are not a measured ranking of the
25 most frequent errors. Research and learner-question evidence are maintained on
`research/compiler-onboarding` in `docs/compiler-onboarding.md`; that report separates
course-study evidence, public anecdotes and proposed heuristics.

Each directory contains three complete programs:

- `broken.c`: a language error, library-contract violation, or intentionally suspicious
  construct. **Do not execute these programs.** Some have undefined behavior; others
  are legal C whose likely meaning needs explanation rather than rejection.
- `fixed.c`: one justified repair. This demonstrates an intended example outcome;
  it does not imply that the compiler should automatically make the change.
- `valid.c`: a legal neighboring construct that must remain accepted. For example,
  deliberate assignment, pointer identity comparison, one-past pointer formation,
  raw character arrays, or returning a local pointer to allocated storage.

`manifest.json` is a list of 25 objects. Each contains `id`, `title`, `phase`, additional
`flags`, the three relative source paths, `expect`, and `known_gap` (a reason for both targets, or a map keyed by target). Expectations
name a source line, severity, and case-insensitive regular expression describing
useful diagnostic content. They describe the issue independently of a snapshot;
matching a generic unrelated warning does not satisfy an expectation. IDs in the
compiler's diagnostic stream are evidence to record, not invented requirements.

Default language/warning flags belong to the runner (`-std=c17 -Wall -Wextra
-Wconversion`). Per-case flags enable a targeted warning where useful. Most cases
use explicit standard-library declarations to keep the fixture independent of
headers. B04 deliberately uses `<stdio.h>` in the fixed example to exercise the
header/declaration distinction; target setup must provide it. Allocation fixtures
use the compiler's `__SIZE_TYPE__` for exact `size_t` declarations without a header.

B05 is a final-link failure. Its fixed program provides the definition in the same
file. An unresolved reference in an object compiled with `-c` is normal and must not
be turned into a frontend error. The link diagnostic may lack source coordinates;
the manifest's line identifies the source reference, not a promise that every linker
has debug information or prints that location.

`known_gap` records a concrete missing capability or an intent-dependent diagnostic
proposal. A gap is not a passing result. Cases B10/B17, for example, cannot justify
mandatory default warnings or an automatic arithmetic/whitespace edit: the suspicious
constructs can be intentional. B15/B16 need library-contract knowledge, and B25 may
need analysis beyond a frontend. Baselines must expose missing detections honestly;
never remove an expectation solely because current output is silent. When a gap is
fixed, preserve the case and remove its marker in the same reviewed change.

Expected source locations can have documented intentional differences: B01 may point
at the end of the preceding line or the next token; B08 can point at the control
statement or the misleadingly indented statement; B12 can point at the closing brace
or the function. The runner should retain this evidence and avoid silently allowing
an arbitrary location in an otherwise large source file.

Fixed and valid programs must compile under the selected target and mode. Runtime
validation, when separately authorized by a runner, applies only to the fixed/valid
programs with specified input and expected behavior; input examples are interactive
and should never be launched without a bounded input contract. Compile acceptance
alone does not establish correct handling of malformed input, EOF, allocation failure
or every possible runtime path. Valid neighbors test acceptance, not a promise of
warning-free output for every compiler version.

Initial fixture validation (2026-11-11): GCC 15.2 and Clang 23.1.6 accepted all 50
fixed/valid programs under `-std=c17 -Wall -Wextra -Wconversion -fsyntax-only`
(100 checks). All 25 manifest IDs, expectation regexes and line numbers were checked.
This does not establish that vcc detects all 25 cases, or that every expectation has
already been met on ARM. The dedicated runner reports those results.

First actual vcc x86 baseline: 10 covered cases and 15 documented diagnostic gaps
(after placing B11's primary expectation on its controlling branch; the later read
is a secondary note). The other 15 gaps are B07–B10, B13–B18, B20–B23 and B25.
All fixed/valid examples were clean for Clang and vcc in that runner invocation.
B05's reported link failure is supplied by the explicitly identified external linker
used by the runner, not evidence of vcc's own linker diagnostics. These counts are a
starting baseline, not a green completion gate or proof of pedagogical effectiveness.

## Running and interpreting the suite

```sh
LLVM=/usr/lib/llvm-23 python3 bench/vcc-onboarding.py --json build/onboarding/x86_64.json
LLVM=/usr/lib/llvm-23 python3 bench/vcc-onboarding.py --target aarch64-linux-gnu --json build/onboarding/aarch64.json
```

The normal command fails for every missing explanation (`GAP`), including documented
ones. `--baseline` instead reports documented gaps as `XFAIL`, fails new regressions,
and fails unexpected improvements (`XPASS`) until their markers are removed. It is
used by the fast premerge gate; a green baseline does not mean all 25 are covered.
`--bin-dir DIR` or `VCC_BIN=DIR` selects the tested compiler, copied for the run.

The runner checks issue-specific diagnostic content, source line, severity, stable
IDs, exact text reconstruction from JSON in both text styles, and valid SARIF. It
also requires all 50 repaired/intentional examples to compile without diagnostics
in both vcc and Clang. Link tests build corrected examples but never execute any
program. Broken programs are never executed. Five verifier regression tests guard
against matching source snippets, unrelated locations, or incomplete corpora.

Validated baseline on both Linux targets: x86 has 10 covered cases and 15 gaps;
ARM has 11 covered cases and 14 gaps. B14 format checking currently meets the
contract only on ARM, so its gap marker is target-specific. One covered case on
each target is the external-linker story. See `BASELINE.md` for captured messages.
B09 explicitly uses `-Wno-error=parentheses`: Clang 23 otherwise diagnoses this
legal chained comparison as an error by default; this corpus tests explaining its
meaning without requiring language rejection.
