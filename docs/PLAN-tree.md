# Source tree reorganisation

**Goal:** a source tree whose top-level directories say what each part is,
modelled on PostgreSQL's (`src/`, `src/include/`, `src/common/`, `src/port/`,
`src/bin/`, `src/test/`, `src/tools/`, `contrib/`, `doc/`), with a place for
more front ends: C++ first, then Objective-C and Objective-C++.

**Why now:** a C++ front end needs a named boundary between the language and
code generation.  Today that boundary is `cc/ccint.h`, included by relative
path from `emit/emitimpl.h`; the diagnostics engine lives inside `pp/`; host
code and portable data structures share `lib/`.  Moving files is cheapest
before a second front end exists.

**Rules:**
- Moves only.  No step changes behaviour or output: every step is checked
  with `bench/vcc-same.py` against a base built before the step (outputs byte
  for byte) and `bench/vbench.py gate`.
- Each step is one commit (or a short series) using `git mv`, so `git log
  --follow` and merges across renames keep working.  Path edits made by
  script are committed apart from hand edits.
- `make vcc VCC_UNITY=` (every file compiles alone) and the premerge set
  (`env/linux/run.sh ci/premerge.sh arm64`, wasm and x86-64 jobs) are green
  after every step.
- `arch/<cpu>/<component>/` and `platform/<os>/` keep their shape: one
  directory per target, as LLVM's `lib/Target/`.  The unity build
  (`obj/unity/MOD.c` from `MOD_SRC`) and `VPRIV` stay as they are.
- The split of `cc/` into language-neutral AST code and the C parser is **not**
  part of this plan; it is the first step of the C++ plan (see Open questions).

## Starting point (2026-11-13, main at 579245f6)

- `ccs/` holds four unrelated things: `vcc/` (the toolchain, ~160 k lines),
  `phase1/` (clang-based phase 1 tools), `fuzz/` and `wasm/` (the
  compiler-explorer build).
- 223 `#include "../…"` lines (`../lib/`, `../../../emit/`, `../cc/ccint.h`,
  `../arch/a64/ld/`); module boundaries exist only by convention.
- `pp/diag.c`, `diagout.c`, `diagtext.c`, `diagids.inc` are the diagnostics
  engine for every component (driver and vccd use it too).
- `lib/` mixes portable data structures (`arena.h`, `vec.h`, `intern.c`,
  `f128.h`, `bigint.h`, `ckd.h`) with host code (`mapfile.h`, `vperf.h`,
  `vstack.h`, `hostcrc.h`, `x86native*`, `x86features*`).
- Program `main`s are spread over component directories: `lex/lextest.c`,
  `as/vasm.c`, `vir/virtest.c`, `vir/viralloc.c`, `driver/vpp.c`,
  `vccd/vccd.c`, `vccd/shim.c`, `ld/ld.c` (also linked into vccd).
- `vccd/` holds the server and kbuild helpers that run inside it (`ar.c`,
  `rsh.c`, `fixdep.c`).
- Tests are in `ccs/vcc/tests/` (21 suites), fuzzing in `ccs/fuzz/`.
- Nine generators sit next to their output (`cc/gen-*.{py,sh}`,
  `pp/gen-*.{py,sh}`, `vccd/gen-warnings.sh`, `arch/a64/gen-sysreg.sh`,
  `fuzz/gen-corpus.py`), more in `bench/gen-*.py`.
- `bench/` has 147 entries, mostly correctness harnesses, a few benchmarks.
- References to move along: `ccs/` in 106 tracked files, `build/ccs` in 48,
  `bench/` in 136 (AGENTS.md, TODO.md, docs, harnesses, `ci/`,
  `.github/workflows/{arm64,wasm}.yml`, `env/linux/run.sh`).

## Target tree

```
src/                        was ccs/vcc/
  Makefile                  was ccs/Makefile
  include/vcc/              public header of each module; -Isrc/include
    common/*.h  port/*.h    (was lib/*.h)
    lex.h pp.h diag.h       ast.h (was cc/cc.h + cc/ccint.h)
    codegen.h as.h obj.h ld.h vir.h driver.h ...
  common/                   was lib/: arena buf tbuf vec hash intern ldhash f128 bigint ckd defer attr
  port/                     host-only: mapfile vstack vperf hostcrc x86native x86features,
                            darwin/pmc.c, wasm/ (mman.c, compat/ from ccs/wasm)
  frontend/
    lex/                    shared by every language
    pp/                     shared
    diag/                   out of pp/: engine, text styles, JSON/SARIF/HTML, diagids.inc
    c/                      was cc/: parser + sema
    cxx/  objc/             later
  codegen/                  was emit/: tu data type prepass, walk/ rec/ flow/ opt/ vir/
  vir/                      the IR, unchanged
  as/  obj/                 unchanged
  ld/                       vlink, wasmld, objcopy
  arch/<cpu>/{as,emit→codegen,obj,ld}
  platform/<os>/
  driver/                   command lines, compile job, ccsnap, explain
  server/                   vccd core: vccd.c dstore caps warnings
  utils/                    ar.c rsh.c fixdep.c: kbuild helpers run inside vccd
  bin/<prog>/               one main each: vcc (shim.c: vcc vas vld var vsh), vccd, vpp,
                            vasm, vlink, lextest, virtest, viralloc
  test/                     was vcc/tests/: regress/ (was vcc/), diag/, vir/, vir-alloc/, vas/,
                            vas-obj/, abi/, ...; external/ (gcc-torture, tcc-tests2, c90);
                            fuzz/ (was ccs/fuzz)
  tools/                    every gen-* script
contrib/
  phase1/                   was ccs/phase1
  wasm/                     was ccs/wasm (explorer page, build.sh)
doc/                        was docs/; vcc/docs/diag -> doc/diag
bench/                      performance only (T6)
build/src/                  was build/ccs/ (and build/src-asan, -tsan)
```

`frontend/c/` keeps its internal header `ccimpl.h`; each module keeps its
`*impl.h` next to its files.  Only headers another module includes move to
`src/include/vcc/`.

### File map

| From | To | Note |
|---|---|---|
| `ccs/vcc/*` | `src/*` | T1 |
| `ccs/Makefile` | `src/Makefile` | T1; `make -C src` |
| `ccs/phase1/`, `ccs/wasm/` | `contrib/phase1/`, `contrib/wasm/` | T1; `mman.c`, `compat/` to `port/wasm/` in T2 |
| `docs/`, `ccs/vcc/docs/diag/` | `doc/`, `doc/diag/` | T1 |
| `ccs/protocol.h` | `src/include/vcc/protocol.h` | phase 1 and vccd share it |
| `lib/` | `common/`, `port/` | T2 |
| `lex/`, `pp/` | `frontend/lex/`, `frontend/pp/` | T3 |
| `pp/diag*.c`, `pp/diagids.inc` | `frontend/diag/` | T3 |
| `cc/` | `frontend/c/` | T3 |
| `emit/`, `arch/*/emit/` | `codegen/`, `arch/*/codegen/` | T3 |
| `vccd/{vccd,dstore,caps}.c`, `warnings.inc` | `server/` | T3 |
| `vccd/{ar,rsh,fixdep}.c` | `utils/` | T3 |
| `vccd/shim.c`, `lex/lextest.c`, `as/vasm.c`, `driver/vpp.c`, `vir/virtest.c`, `vir/viralloc.c` | `bin/<prog>/` | T3; `ld_main` stays in `ld/` |
| public headers | `include/vcc/` | T4 |
| `tests/`, `ccs/fuzz/` | `test/`, `test/fuzz/` | T5 |
| `gen-*` | `tools/` | T5 |
| `bench/` harnesses | `src/test/harness/` or `test/` | T6, optional |

## Steps

| Step | What | Verification |
|---|---|---|
| T1 | `ccs/vcc` → `src`, `phase1`/`wasm` → `contrib`, `docs` → `doc`, `build/ccs` → `build/src`.  Scripted path rewrite in Makefile, bench, ci, workflows, env, AGENTS.md, TODO.md, doc. | `make -C src vcc`, unity and file-by-file; `vcc-same.py`; premerge (all jobs); `contrib/wasm/build.sh`; `vccd-kbuild.sh` + `boot-test.py` once |
| T2 | `lib/` → `common/` + `port/`; `ccs/wasm/{mman.c,compat}` → `port/wasm/` | as T1; x86-64 host build (`vcc-x86.py`) and wasm build, the two users of `port/` |
| T3 | `frontend/{lex,pp,diag,c}`, `codegen/`, `server/`, `utils/`, `bin/`; `MOD_SRC` lists and `gen-walkprefix.py` paths updated | as T1; `gen-walkprefix.py --check`, `gen-diagids.py --check`, `gen-explain.py --check`; `vcc-diag.py` (diagnostic file paths in snapshots) |
| T4 | `src/include/vcc/`, `-Isrc/include`; a script rewrites every `#include "../…"` of a public header to `#include "vcc/…"`; relative includes remain only inside a module | as T1; `git grep '#include "\.\./\.\./'` empty outside `arch/` → module internals; fuzz targets and `contrib/wasm` build |
| T5 | `tests/` → `test/`, fuzz → `test/fuzz/`, generators → `tools/` (their output paths fixed) | every harness that names a suite; `make fuzz fuzz-regress`; each generator rerun gives an identical file |
| T6 | optional: `bench/` split into correctness harnesses and performance work | `ci/premerge.sh` all jobs; AGENTS.md command list runs as written |

T1–T5 change no source text except paths.  T6 is the step that changes most
commands people type and can be dropped.

## Validation

- `bench/vbench.py mkbase REV` before each step, `bench/vcc-same.py --base DIR
  -j 6` after: test-suite outputs byte-identical.  A path inside an output
  (`__FILE__`, DWARF `DW_AT_name`, diagnostics) differs only when the input's
  path changed; those cases are listed in the commit message.
- `bench/vbench.py gate`: no instruction-count change (moves only).
- Premerge on all jobs (arm64, asan, tsan, wasm, x86-64) through
  `ci/premerge-push`.
- A `git grep` for the old paths (`ccs/`, `build/ccs`, `vcc/lib/`, `/cc/`,
  `/emit/`) is empty after each step, except history in `doc/` status sections.

## Risks and open questions

- **Open branches:** 131 remote branches and several worktrees.  Git follows
  renames in merges, but a branch that adds a file to `emit/` or `cc/` puts it
  in the old place.  Do T1–T4 in one short window, announce it, and rebase the
  live branches right after; a `tools/retarget-paths.sh` that applies the same
  rewrite to a branch's diff makes that cheap.
- **Path-sensitive output:** diagnostic snapshots under `tests/diag` and
  DWARF tests contain source paths; T3 and T5 may need `vcc-diag.py --bless`
  with the diff reviewed by hand.
- **Muscle memory and docs:** `bench/` command lines in AGENTS.md and
  thousands of lines of plan history.  Status sections in `doc/` keep their
  old paths (they are history); live instructions are rewritten.
- **`build/ccs` → `build/src`:** every harness's `--bin-dir` default and ccache
  keys change once; local ccache is cold after T1.
- **Open: `as/` + `obj/` → `mc/`?**  LLVM's name for the pair; keep them apart
  for now since `obj/` is also the linker's input model.
- **Open: where `ast.h` ends up.**  T4 publishes `cc.h` + `ccint.h` as
  `vcc/ast.h` unchanged.  The real split (types, layout, constant evaluation,
  records, mangling, builtins shared by C and C++; C's parser and sema apart)
  depends on how C++ classes and templates reach code generation, and is the
  first step of a `PLAN-cxx.md`.
- **Open: C++ lexing.**  `frontend/lex/` will need a language mode (`::`,
  `.*`, `->*`, raw strings, keywords); the SIMD lexer's tables are per mode,
  not a second lexer.  Also part of `PLAN-cxx.md`.

## Status

Not started.  Plan written 2026-11-13.
