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

As landed (T1–T5; the differences from the first draft are under Status):

```
src/                        was ccs/vcc/
  Makefile                  was ccs/Makefile: make -C src
  include/                  the headers another module includes, at their module's path;
                            -Isrc/include, included as "common/arena.h", "frontend/pp/pp.h"
    common/ port/ frontend/{lex,pp,c}/ codegen/ driver/ as/ obj/ ld/ vir/ utils/
    arch/{a64,x64,wasm}/    a64.h, reloc.h, x86features.h, wasmas.h
    protocol.h              vccd's wire protocol (shim, server, contrib/phase1)
  headers/                  was include/: vcc's own <arm_neon.h>, <stdatomic.h>, ... (VCC_INCDIR)
  common/                   was lib/: intern.c, ldhash.c (their headers in include/common/)
  port/                     darwin/pmc.c, wasm/{mman.c,compat/} (x86native, mapfile, vperf,
                            vstack, hostcrc are header-only, in include/port/)
  frontend/
    lex/                    shared by every language
    pp/                     shared
    diag/                   out of pp/: engine, text styles, JSON/SARIF/HTML, diagids.inc
                            (still in the pp module: it uses ppimpl.h)
    c/                      was cc/: parser + sema
    cxx/  objc/             later
  codegen/                  was emit/: tu data type prepass, walk/ rec/ flow/ opt/ vir/
  vir/                      the IR
  as/  obj/  ld/            unchanged
  arch/<cpu>/{as,codegen,obj,ld}   (emit/ -> codegen/)
  platform/<os>/            target OS: linux (ELF), darwin (Mach-O, ldmacho), wasm
  driver/                   compile job, ccsnap, explain
  server/                   vccd.c (with its main), dstore, caps, warnings.inc
  utils/                    ar.c rsh.c fixdep.c: kbuild's helpers run inside vccd
  bin/<prog>/               vcc (shim.c: vcc vas vld var vsh), vpp, vasm, lextest, virtest, viralloc
  test/                     was tests/: the suites keep their names (vcc/, diag/, vir/, abi/,
                            gcc-torture/, tcc-tests2/, ...); fuzz/ (was ccs/fuzz)
  tools/                    every gen-* script; retarget-paths.py
contrib/
  phase1/                   was ccs/phase1
  wasm/                     was ccs/wasm (explorer page, build.sh)
doc/                        was docs/; vcc/docs/diag -> doc/diag
bench/                      unchanged (T6 dropped)
build/src/                  was build/ccs/ (and build/linux/src, build/src-asan, -tsan)
```

Each module keeps its `*impl.h` next to its files.  A relative include is
left only inside a module (`arch/<cpu>/<component>/` belongs to the
component's module), for three internal headers another directory of the
same module uses (`ppimpl.h` by `frontend/diag/`, `objimpl.h` by
`platform/linux/`, `was.h` by `platform/wasm/`) and for generated data
(`ppdata-*.inc`, `warnings.inc`, the linker's `arch/*/ld/*.inc`).

### File map

| From | To | Note |
|---|---|---|
| `ccs/vcc/*` | `src/*` | T1 |
| `ccs/Makefile` | `src/Makefile` | T1; `make -C src` |
| `ccs/phase1/`, `ccs/wasm/` | `contrib/phase1/`, `contrib/wasm/` | T1; `mman.c`, `compat/` to `port/wasm/` in T2 |
| `docs/`, `ccs/vcc/docs/diag/` | `doc/`, `doc/diag/` | T1 |
| `ccs/protocol.h` | `src/include/protocol.h` | phase 1 and vccd share it |
| `ccs/vcc/include/` | `src/headers/` | T1: frees `src/include/` |
| `lib/` | `common/`, `port/` | T2; `x86features.h` to `arch/x64/` (target data), `platform/darwin/pmc.c` to `port/darwin/` |
| `lex/`, `pp/` | `frontend/lex/`, `frontend/pp/` | T3 |
| `pp/diag*.c`, `pp/diagids.inc` | `frontend/diag/` | T3 |
| `cc/` | `frontend/c/` | T3 |
| `emit/`, `arch/*/emit/` | `codegen/`, `arch/*/codegen/` | T3 |
| `vccd/{vccd,dstore,caps}.c`, `warnings.inc` | `server/` | T3 |
| `vccd/{ar,rsh,fixdep}.c` | `utils/` | T3 |
| `vccd/shim.c`, `lex/lextest.c`, `as/vasm.c`, `driver/vpp.c`, `vir/virtest.c`, `vir/viralloc.c` | `bin/<prog>/` | T3; vccd's and vlink's mains stay in `server/vccd.c` and `ld/ld.c` |
| public headers | `include/<module path>/` | T4 |
| `tests/`, `ccs/fuzz/` | `test/`, `test/fuzz/` | T5 |
| `gen-*` | `tools/` | T5; `test/fuzz/gen-corpus.py` stays with the fuzzers |
| `bench/` harnesses | stay | T6 dropped |

## Steps

| Step | What | Verification |
|---|---|---|
| T1 | `ccs/vcc` → `src`, `phase1`/`wasm` → `contrib`, `docs` → `doc`, `build/ccs` → `build/src`.  Scripted path rewrite in Makefile, bench, ci, workflows, env, AGENTS.md, TODO.md, doc. | `make -C src vcc`, unity and file-by-file; `vcc-same.py`; premerge (all jobs); `contrib/wasm/build.sh`; `vccd-kbuild.sh` + `boot-test.py` once |
| T2 | `lib/` → `common/` + `port/`; `ccs/wasm/{mman.c,compat}` → `port/wasm/` | as T1; x86-64 host build (`vcc-x86.py`) and wasm build, the two users of `port/` |
| T3 | `frontend/{lex,pp,diag,c}`, `codegen/`, `server/`, `utils/`, `bin/`; `MOD_SRC` lists and `gen-walkprefix.py` paths updated | as T1; `gen-walkprefix.py --check`, `gen-diagids.py --check`, `gen-explain.py --check`; `vcc-diag.py` (diagnostic file paths in snapshots) |
| T4 | `src/include/`, `-Isrc/include`; a script rewrites every `#include "../…"` of a public header to its module path (`#include "common/arena.h"`); relative includes remain only inside a module | as T1; `git grep '#include "\.\./\.\./'` empty outside `arch/` → module internals; fuzz targets and `contrib/wasm` build |
| T5 | `tests/` → `test/`, fuzz → `test/fuzz/`, generators → `tools/` (their output paths fixed) | every harness that names a suite; `make fuzz fuzz-regress`; each generator rerun gives an identical file |
| T6 | dropped: `bench/` split into correctness harnesses and performance work | `ci/premerge.sh` all jobs; AGENTS.md command list runs as written |

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
  live branches right after; `src/tools/retarget-paths.py text` applies the
  same reference rewrite to a branch's files (see Status).
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
- **Open: where `ast.h` ends up.**  T4 published `cc.h` and `ccint.h` as
  `include/frontend/c/{cc,ccint}.h`, unchanged and not merged.  The real split (types, layout, constant evaluation,
  records, mangling, builtins shared by C and C++; C's parser and sema apart)
  depends on how C++ classes and templates reach code generation, and is the
  first step of a `PLAN-cxx.md`.
- **Open: C++ lexing.**  `frontend/lex/` will need a language mode (`::`,
  `.*`, `->*`, raw strings, keywords); the SIMD lexer's tables are per mode,
  not a second lexer.  Also part of `PLAN-cxx.md`.

## Status

### T1–T5 done, T6 dropped (2026-11-13)

Five commits on `claude/source-tree-reorganization-77f8a3`, from 579245f6.
Each moves files with `git mv` (history follows) and rewrites paths only;
`src/tools/retarget-paths.py` holds the moves as data (`move STEP` did them).

How each step was checked:
- `make -C src vcc` both in unity mode and file by file (`VCC_UNITY=`).
- `bench/vcc-same.py --base` against tools built from 579245f6: 15196 of
  15204 outputs identical.  The other 8 are `-E` line markers that name vcc's
  own headers (`ccs/vcc/include` → `src/headers`), checked by hand.
- The fast harnesses: vcc-ctests 2128/2128, vir-test, vir-exposure,
  vcc-diag 226/226, vas-obj, vlink-opts, vcc-driver, gen-walkprefix and
  gen-explain `--check`, vcc-explain, vir-alloc, volatile, stress, pic, vsh,
  bflayout, diagcov (same output as main's), vccd-test 26/26, and the
  snippet harnesses (vcc-flow, vcc-rec-*, sdivconst, udivconst, switch-plan,
  unused-init, emit-names, mulhi).
- After T5, the 43 macOS-runnable steps of `ci/premerge.sh arm64`, each in
  this tree and in 579245f6's: the same result for every step.
- vcc-wasm (wasm64 suites, 1972/1974; 2 oracle crashes in clang).
- `bench/vbench.py gate --base` (mkbase 579245f6): instructions within
  ±0.11 % in every mode.

After merging main (54383659, main's English plans and x86 host dispatch):
- Both builds and the fast harnesses as above.
- `bench/vccd-kbuild.sh work/build-tree -j 6`: 925 TUs compiled by vcc, 0
  failed, 0 fell back to clang, 12.7 s.  `boot-test.py`: Debian 13
  reaches `running` with 0 failed units.
- Premerge on the PR's CI (run 43418993945): arm64, asan, tsan, wasm and
  gate all pass.  tsan and wasm run on x86-64 ubuntu-24.04, so this also
  covers the x86-64 host build.
- The local `env/linux/run.sh` image is older than `ci/setup-debian.sh`: it
  has no compiler-rt sanitizer runtimes and no `qemu-aarch64`.  22 arm64
  steps fail there: 12 link against the missing `libclang_rt.asan`, and 10
  find no qemu binary.  Every other step passes.

Already broken on main and unchanged here:
- `make fuzz`: `fuzz-pp.c` uses `pp_opts.nsys`, and `fuzz-lex` has a
  duplicate symbol.
- `contrib/wasm/build.sh`: its file lists lack `codegen/walk`, `vir` and x64,
  so the link has undefined symbols.  Each file still compiles.
- `gen-diagids.py --check` is stale: its output is the same as main's.
- `vcc-x86features.py` fails its AVX-512 assertion on this host.

Differences from the draft above:
- `ccs/vcc/include` (vcc's own headers) became `src/headers/`, so that
  `src/include/` holds only module headers.
- No `vcc/` level under `src/include/`: the names are the module paths, as in
  PostgreSQL (`"common/arena.h"`, `"frontend/pp/pp.h"`).  `protocol.h` sits at
  `src/include/protocol.h`.
- `cc.h` and `ccint.h` were not merged into an `ast.h`; that is for the C++
  plan.
- `x86features.h` is target data, so it went to `arch/x64/` (now
  `include/arch/x64/`) rather than `port/`.  Apple's `pmc.c`/`pmc.h` went to
  `port/darwin/`: `platform/` is for target OSes.
- `frontend/diag/` is still compiled in the pp module and uses `ppimpl.h`.
  Giving it an API of its own is a follow-up.
- vccd's and vlink's mains stay where their code is (`server/vccd.c`,
  `ld/ld.c`); `bin/` holds only the small mains.
- The test suites kept their names (no `regress/` or `external/`), so
  `--suite vcc` and the suite lists in the harnesses are unchanged.
  `test/fuzz/gen-corpus.py` stays with the fuzzers.
- The generators in `bench/` (gen-diagids, gen-explain, gen-walkprefix,
  gen-memory-cells, gen-x86features, gen-x86native) moved to `src/tools/` too.
- `gen-x86data.sh` had been writing to `../cc/attrnames-x86.inc` since T3;
  it now writes `../frontend/c/`.
- vcc-ctests, vcc-wasm and vcc-x86 now key their cached clang references by
  the test's path as well.  `tcc-tests2/129_scopes` prints `__FILE__`, so a
  reference cached under the old path failed.
- `bench/vbench.py mkbase` builds an older revision from `ccs/` or `src/`,
  whichever it has.

**T6 dropped.**  The `bench/` scripts load each other by file path:
`vcc-diff.py` from 15 scripts, `vcc-ctests.py` from the wasm, x86 and
torture harnesses, and `hostenv.py` from 72.  The performance tools
(`vbench.py`) import the correctness ones, and pg/sqlite/zlib/ffmpeg both
test and time.  A split would cut through those imports and change about 100
command lines in AGENTS.md, TODO.md and CI for little gain.

**Retargeting a branch** started before T1: `git merge` or `git rebase`
follows the renames of files that exist on both sides.  Then
`src/tools/retarget-paths.py text FILE...` rewrites path references in the
branch's own files.  A file the branch added under an old directory has to be
moved by hand to its new place, and its `#include`s of moved headers become
`"common/…"` and friends.  Building with `make -C src vcc VCC_UNITY=` shows
any include that was missed.
