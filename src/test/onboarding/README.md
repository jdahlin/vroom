# Beginner C diagnostic repair tests

There is **one `.test` file per mistake**. Open
[01_missing_semicolon.test](01_missing_semicolon.test) to see the complete format.
Each file contains its settings, three C examples, and the actual expected messages.
There are no companion C files, per-target message files, or combined manifest.

The 25 cases are practical teaching targets, not a measured frequency ranking.
The learner research is in [compiler-onboarding.md](../../../../doc/compiler-onboarding.md).

## Format

A short TOML header holds the ID, title, semantic message contract, optional warning
flags, and any documented diagnostic gap. Named sections follow:

```text
=== broken.c ===
... the original source ...
=== fixed.c ===
... the minimal repair, preserving its layout ...
=== valid.c ===
... an intentional, valid use of a similar construct ...
=== stderr ===
... exact default diagnostic output ...
=== narrated ===
... exact narrated diagnostic output ...
```

Section headers occupy their own line. Everything after a header up to the next
header is literal content, including blank lines and caret-padding spaces. The
runner extracts the C sections into a temporary directory; source line numbers in
messages refer to those sections, starting at line 1. Broken programs are never run.

Common messages are written once. A section such as
`=== stderr.aarch64-linux-gnu ===` overrides the common message only for that target.
The link story uses `stderr` for the explicitly identified external-linker output;
only its generated temporary-directory path is normalized. It does not claim to
validate vcc's own linker diagnostics.

Empty diagnostic sections mean that the compiler is currently silent. They do not
satisfy the semantic contract or count as coverage. A `known_gap` reason documents
missing capability or an intent-dependent warning proposal; it may apply to both
targets or only one. A diagnostic improvement requires reviewing the new text and
removing the appropriate gap marker.

## Run

```sh
LLVM=/usr/lib/llvm-23 python3 bench/vcc-onboarding.py --baseline
LLVM=/usr/lib/llvm-23 python3 bench/vcc-onboarding.py --baseline --target aarch64-linux-gnu
LLVM=/usr/lib/llvm-23 python3 bench/vcc-onboarding.py --baseline B01
python3 bench/test-onboarding.py
```

`--bin-dir DIR` or `VCC_BIN=DIR` selects the compiler; the runner copies it for a
stable run. Python 3.11 or later provides the standard-library TOML parser. Clang and
the appropriate Linux headers/libraries provide reference checking. No source corpus
from `work/` is needed.

Exact text comparison covers wording, source excerpts, carets, notes, help, fix-it
display, and summaries. Additional checks cover semantic content, location, severity,
stable IDs, JSON/text reconstruction, and SARIF. All repaired and intentional C
examples must compile without diagnostics with both Clang and vcc. Compile acceptance
does not prove runtime behavior for every input or allocation failure.

Ordinary runs fail on diagnostic gaps. `--baseline`, used in CI, keeps documented
gaps as XFAIL, fails regressions, and rejects unexpected improvements (XPASS) until
reviewed. Initial coverage: x86 10 cases/15 gaps; ARM 11/14. One covered case on each
target is supplied by the external linker. B14's format diagnostic currently exists
only on ARM. B09 uses `-Wno-error=parentheses` to test explaining the legal chained
comparison without requiring language rejection.

## Update an expected message

```sh
LLVM=/usr/lib/llvm-23 python3 bench/vcc-onboarding.py --baseline --bless B01
LLVM=/usr/lib/llvm-23 python3 bench/vcc-onboarding.py --baseline --bless --target aarch64-linux-gnu B01
git diff -- src/test/onboarding/01_missing_semicolon.test
```

Review the diff. Tests and CI never regenerate expectations. Bless target runs
sequentially because they update the same file; equal target messages are shared
again automatically. `--json PATH` is optional generated execution evidence, never
an authoritative expected-message file.

B24's repair chooses static storage when a shared persistent object is intended.
It is not a recommendation to make every escaping local static or an automatic fix.
