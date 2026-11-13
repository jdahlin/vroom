# Research: does vroom's architecture carry to C++, and where does it fail?

Status: research note, 2026-11-13.  No code depends on it.  Prior art on
compile servers and header reuse for C is in `RESEARCH-preprocessor.md`
(Bothner's GCC compile server, Tromey's incremental GCC, zapcc, the kernel's
fast-headers work); this note does not repeat it.

## 1. The question

vroom's speed rests on three choices:

1. **One resident process** (vccd) that owns everything shared between TUs:
   the header cache, the prefix snapshots (`ccsnap.c`, "an in-memory chained
   PCH"), the capability table that answers kbuild's probes, and the tools
   kbuild would otherwise spawn (fixdep, ar, sh, the linker).
2. **Skipping repeated header work.**  A C header is almost only declarations,
   so the front end's state after a TU's leading headers is a function of the
   token stream, and a snapshot of it serves every TU that starts the same way.
3. **A direct AST-to-machine-code emitter at -O0**; VIR only at -O2.

The question is whether any of these is wrong for C++, and what other inputs
make the design stop paying.

## 2. What others found (mailing lists, forums)

| Source | What it says | Bearing on vroom |
|---|---|---|
| Lattner, cfe-dev 2010, "Designing a clang incremental compiler server" | a shared server caching headers as pre-lexed token streams and PCH | vccd's header cache; never built upstream |
| cfe-dev 2012, "A Clang-based service architecture" (Carruth's ClangService design) | a persistent clang server; a reply asks for `.o` output from it | became clangd, an IDE server; the compile half was never done |
| zapcc, cfe-dev 2015 (Keren) | ~200K-line clang diff, caches template instantiations and generated code in the server; "very very complex"; caching off for C | the instantiation cache is the hard part; never upstreamed |
| llvm-dev 2009, "-O0 compile time speed (was: Go)" | FastISel was the answer | -O0 needs its own path |
| TPDE, EuroLLVM 2025 | a separate -O0 back end, 10-20x faster than LLVM's; tuning gave ~18% over LLVM 18->20 | 10x needs a separate fast path, not tuning |
| GCC list 2010, Bosscher "Where does the time go?" | C front end ~17% of GCC's time | C: front-end share is modest in GCC |
| Novillo, same thread | Google C++: front end 70-78% of wall time | C++: the front end dominates, but see section 3.3 |
| Myers, same thread | C front-end speed long neglected next to C++ | room for C front ends |
| LKML 2021, Torvalds "Very slow clang kernel config" | allmodconfig config ~2.7 s with clang vs ~0.9 s with gcc: process startup in `cc-option` probes | vccd's probe answers target exactly this |
| kbuild patches 2026 (Stoakes) | 19-22% on 64 cores, 1-10% on an 8-core M2 | build-system work, not compiler work |
| Pakkanen (Meson), "compiler daemon thought experiment", 2024 | the compiler daemon argued from the build-system side | same idea as vccd |
| Cuik + Tilde backend (Arguelles), Handmade Seattle 2023 | new C11 compiler and SSA back end; "LLVM is far too slow and far too big to be fixed from the inside" | closest in spirit; unfinished, no benchmarks |
| TCCBOOT (Bellard, 2004) | compile and boot Linux 2.4.26 in <15 s on a P4 | correctness and optimisation given up; one patched kernel only |

Pattern: upstream compilers gain percents; 10x comes from a separate path or
from removing repeated work; compile servers have been proposed since 2010 and
died either of complexity (zapcc) or by turning into IDE servers (clangd).

## 3. C++: what breaks or weakens

### 3.1 Duplicated work moves past the snapshot

In C the duplicated work is header declarations, which the prefix snapshot
skips.  In C++ headers *declare* `std::vector`, `std::map`, `fmt::format`, and
each TU's own code *instantiates* them: `std::vector<std::string>` with its
members, overload resolution, SFINAE and constexpr evaluation, again in every
TU.  A prefix snapshot then gives what PCH gives, and C++ projects already use
PCH.

Going further needs an **instantiation cache across TUs**: zapcc's hard part.
The result of an instantiation depends on what is visible at its point of
instantiation (two-phase lookup, ADL, specialisations declared later).  The
ODR lets a cache key on (template, arguments) alone, as clang's modules do;
when a program breaks the ODR, which needs no diagnostic, the cache silently
gives code a separate compile would not.  This is the main correctness edge.

Mitigation in vroom's own style: a verification mode that instantiates both
ways and compares, as `vcc-sema.py layout` has clang check our claims.

### 3.2 Code generation is repeated in every TU

Inline functions and template instances are emitted as COMDAT/`linkonce_odr`
in every `.o` and all copies but one are discarded by the linker.  In C++ this
often costs more than parsing, and `-g` multiplies it (DWARF types per TU).
vccd could emit each instance once and reuse its bytes, but each `.o` must stay
complete (archives, make, the linker's model), so this saves CPU, not I/O or
link input.  It has to be designed into the server from the start.

### 3.3 The bottleneck is semantics, not tokens

Novillo's 70-78% is mostly name lookup, overload resolution, instantiation and
constexpr, not lexing.  The SIMD lexer and the preprocessor optimise a small
share.  constexpr/consteval is an interpreter in its own right (clang is
moving it to a bytecode interpreter).

### 3.4 A harder baseline

For C the baseline is plain clang.  C++ projects already use unity/jumbo
builds, PCH, ccache and remote execution; unity builds collect much of the
header-sharing win.  A C++ claim has to beat unity + PCH, not clang alone.

### 3.5 The size of clang parity

Not an architectural problem, but it decides feasibility: Itanium mangling,
vtable/VTT layout and RTTI bit for bit; the ~100 type-trait builtins
libstdc++ and libc++ use (`__is_same`, `__builtin_*`); a Sema roughly ten
times C's.  The oracle method (clang verifies our claims) still works.

### 3.6 C++20 modules

They need a dependency scan before the build (P1689) and BMI files, so "do
not replace make" means make has to learn modules.  vccd is a natural BMI
store in memory, so here the architecture is an advantage.

## 4. Where it fails even for C

- **No shared prefix.**  Per-file `-D` flags, a different first header in
  each TU, a `config.h` that differs: sibling first headers already share
  little (`RESEARCH-preprocessor.md` 6.7).  Without common prefixes or
  repeated regions the cross-TU win is gone and only single-TU speed is left.
- **Headers generated during the build.**  kbuild writes headers mid-build;
  any content cache with incomplete keys gives stale results.  `TODO.md`
  already lists "Header/snapshot cache keys incomplete".  The correctness
  edge most likely to bite on a new project.
- **One process for all jobs.**  A crash or leak in vccd takes every job down.
  The 16 GB reservation suits the kernel; a Chromium-sized build needs
  eviction.  `vccd-test.py` covers crashes, not memory pressure.
- **Distributed builds.**  Sharing is per machine.  Under remote execution
  (Bazel, distcc) each action is hermetic and elsewhere, and vccd reduces to
  a fast single-TU compiler.
- **-O2.**  Once the optimizer dominates, shared front-end work buys little;
  the lead is in cold -O0 and debug builds.
- **Amdahl at 2 s.**  make's own work, stat/exec and the serial link become
  the floor, which pushes more into the server (fixdep, rsh and the linker
  already are).

## 5. Conclusion

Nothing here invalidates the architecture: one resident process owning the
shared state, plus a fast direct emitter.  For C it is the right design.  For
C++ it is necessary but not sufficient; two layers would have to be added:

1. an instantiation cache keyed on the ODR, with a verification mode that
   catches ODR violations instead of trusting them;
2. cross-TU dedup of generated code and of DWARF.

These are where zapcc and the 2010-2012 clang server proposals stalled, and
where a fast C++ vroom would succeed or fail.

## 6. What this asks of the C++ preparation

`PLAN-tree.md` leaves the `ast.h` split and C++ lexing to a `PLAN-cxx.md`;
`PLAN-quality.md` names `struct cc` as the obstacle and has the split drafted.
Section 3 adds requirements to that split:

- **Instantiations as separable units.**  If an instantiation reads and
  writes the whole `struct cc`, it can never be cached across TUs.  Keep it a
  function of (template, arguments, the declarations it looked up), so that
  input can be recorded, hashed and checked later (3.1).
- **Snapshots of C++ state.**  `ccsnap.c` copies every per-TU array and the
  heap and stores pointers as offsets.  C++ state (lazily declared special
  members, instantiation tables, lookup caches) must follow the same rules,
  or the prefix snapshot, the one part known to carry over, is lost.
- **Codegen keyed by symbol, not by TU.**  COMDAT functions should reach the
  emitter as (mangled name, body) that vccd can emit once and reuse (3.2).
  The cc-emit cycle being one call (`cc_run` calls `emit_tu`) is the place
  to cut.
- **Measure the split first.**  Kernel TUs are pp 53 %, parse and sema 33 %,
  emit 13 % (`PLAN-quality.md`).  Before investing in the C++ lexer, measure
  the same split on a C++ project with clang's `-ftime-trace`; section 3.3
  predicts sema and instantiation dominate, not pp.
- **A C++ test project with a real baseline.**  Pick one that already has
  unity + PCH (3.4), so the result answers whether vroom wins where it
  matters.

## Sources

- Lattner, cfe-dev 2010: https://lists.llvm.org/pipermail/cfe-dev/2010-June/009493.html
- Clang service RFC, cfe-dev 2012: https://lists.llvm.org/pipermail/cfe-dev/2012-June/022028.html
- ClangService design: https://github.com/chandlerc/llvm-designs/blob/787056c6208749705d94bd2d700307f29e9c623f/ClangService.rst
- zapcc, cfe-dev 2015: https://lists.llvm.org/pipermail/cfe-dev/2015-May/043155.html
- -O0 compile time speed, llvm-dev 2009: https://lists.llvm.org/pipermail/llvm-dev/2009-November/027393.html
- TPDE, EuroLLVM 2025: https://www.llvm.org/devmtg/2025-04/slides/technical_talk/engelke_faster.pdf
- TPDE-LLVM announcement: https://discourse.llvm.org/t/tpde-llvm-10-20x-faster-llvm-o0-back-end/86664
- GCC "Where does the time go?" 2010: https://gcc.gnu.org/pipermail/gcc/2010-May/191982.html
- Novillo's reply: https://inbox.sourceware.org/gcc/AANLkTinaqVy3qL7qmtxO0PIQf5SDz8wK1HWnF0JlUVFr@mail.gmail.com
- Myers' reply: https://inbox.sourceware.org/gcc/Pine.LNX.4.64.1005201931130.2047@digraph.polyomino.org.uk
- LKML 2021, Very slow clang kernel config: https://lkml.org/lkml/2021/4/29/892
- Torvalds 2022: https://lists.openwall.net/netdev/2022/06/22/233
- kbuild patches (Phoronix): https://www.phoronix.com/news/Linux-Kbuild-Faster-v3
- Compiler daemon thought experiment: https://nibblestew.blogspot.com/2024/12/compiler-daemon-thought-experiment.html
- Cuik: https://github.com/RealNeGate/Cuik
- Tilde backend talk: https://handmadecities.com/media/seattle-2023/tb/
- TCCBOOT: https://bellard.org/tcc/tccboot.html
