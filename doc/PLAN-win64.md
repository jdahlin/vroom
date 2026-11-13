# Windows x64 as a target: MinGW-w64 (`x86_64-w64-mingw32`)

**Goal:** `vcc --target=x86_64-w64-mingw32 -c x.c` produces a COFF object that
links against the mingw-w64 CRT (UCRT) and runs with the same result as
clang's, under Wine here and natively on a Windows runner in CI.  `-S`
produces the AT&T text clang's assembler accepts for that target.  The end
state is a toolchain with nothing from LLVM at run time: vcc compiles,
assembles and links (`vlink` writes the PE image); only the sysroot (headers,
CRT, import libraries) comes from mingw-w64.

**Why:** Windows is the third OS for the x86-64 backend, and the first with a
different data model (LLP64), calling convention and object format on a CPU
we already generate code for.  It settles what in `arch/x64/` is per CPU and
what is per OS, the same way x86-64 Linux did for arm64.

**Not in this plan:** the MSVC flavour (`x86_64-pc-windows-msvc`: `clang-cl`
driver, Microsoft's headers with SAL and `__declspec` beyond what MinGW uses,
CodeView/PDB), 32-bit x86, arm64 Windows, and C++.  `__try`/`__except` is not
supported by clang for MinGW either.

**No clang or LLVM in the product.**  Clang, lld, llvm-mingw's compiler and
the LLVM object tools appear only in the test harnesses, as the oracle vcc is
compared with.  What a user runs is our own code end to end: `vcc` compiles,
`vas` assembles, `vlink` links, `var` archives.  That includes the run-time
support a compiled program links against: no compiler-rt and no libgcc.  The
helpers generated code calls (`___chkstk_ms`, the `__int128` division and
shift helpers, the `long double`/`__float128` conversions) come from vcc's own
runtime library (step WIN7R).  The only third-party input is the mingw-w64
sysroot: Windows headers, the CRT startup objects and the import libraries
(the role glibc has on Linux).  The harness takes it from an llvm-mingw
release because that packages it conveniently; nothing else in that tree is
used on vcc's side.

**Rules:** as PLAN-x86.md.  Clang is the reference
(`clang --target=x86_64-w64-mingw32`, the same clang 23 as the other tables);
data taken from clang once lives in generated files.  The output for arm64,
x86-64 Linux, Darwin and wasm must not change: every commit is checked with
`bench/vcc-same.py` against a base built before the step.

## Starting point (2026-11-13)

Measured on this host (x86-64 Linux):

- **Present:** clang 23.1.6 with the MinGW target built in (`-S` and `-c`
  work), `ld.lld` (its MinGW driver, `-m i386pep`), `lld-link`,
  `llvm-dlltool`, `llvm-readobj --unwind`, `llvm-objdump`, `llvm-mc`; Wine
  11.0.
- **Verified end to end:** a COFF object from clang 23, an import library from
  `llvm-dlltool`, linked by `ld.lld -m i386pep` and run under Wine 11 exits
  with the expected code.  So the reference chain needs nothing but a sysroot.
- **Missing:** the mingw-w64 sysroot.  `#include <stdio.h>` fails for the
  target: no headers, CRT startup objects, import libraries or compiler-rt
  builtins.  See "What validation needs" below.
- **vcc:** the target enum is `PP_LINUX_AARCH64, PP_DARWIN, PP_WASM64,
  PP_WASM32, PP_LINUX_X86_64` (`src/include/frontend/pp/pp.h:99`); `X86(c)` in
  `src/codegen/emitimpl.h:113` means "x86-64 Linux"; the driver sets
  `job->target_x64` from the triple (`src/driver/cmdline.c:275`).  No file
  knows COFF, PE, LLP64 or the Microsoft calling convention.  x86-64's code
  generator (`src/arch/x64/codegen/`, SysV in `abi.c`), VIR lowering
  (`src/arch/x64/vir/`, `calling-convention.c`), assembler
  (`src/arch/x64/as/`), fixups and linker relocations exist and are reused.

What clang does for this target (from `-S` and `-dM -E` here; these are the
facts the steps implement):

| Area | clang, `x86_64-w64-mingw32` |
|---|---|
| Data model | LLP64: `long` 4 bytes, pointers 8, `size_t` = `unsigned long long`, `wchar_t` and `wint_t` = `unsigned short`; `long double` is x87, 16 bytes; `__int128` exists |
| Predefines | `_WIN32 _WIN64 WIN32 WIN64 WINNT __MINGW32__ __MINGW64__ __SEH__`, `__declspec(a)` = `__attribute__((a))`, `__cdecl` = `__attribute__((__cdecl__))`, `__GNUC__ 4` |
| Arguments | rcx, rdx, r8, r9 or xmm0-3 by position; 32 bytes of shadow space; records of 1, 2, 4 or 8 bytes in a register, all others by reference to a caller's copy; `__int128` and `long double` by reference |
| Returns | rax or xmm0; `__int128` in xmm0; `long double` and larger records through a hidden pointer in rcx (returned in rax) |
| Varargs | `va_list` is `char *`; register arguments are spilled to the shadow space; FP arguments are passed in both the XMM and the integer register |
| Callee-saved | rbx, rbp, rdi, rsi, r12-r15, xmm6-xmm15; no red zone |
| Large frames | `___chkstk_ms` for a frame of a page or more (`movl $N, %eax; callq ___chkstk_ms; subq %rax, %rsp`) |
| Unwind | `.seh_proc/.seh_pushreg/.seh_stackalloc/.seh_setframe/.seh_savexmm/.seh_endprologue/.seh_endproc` -> `.pdata` + `.xdata` |
| Symbols | `.def NAME; .scl 2; .type 32; .endef` per function; no `.type`/`.size`; `@feat.00`; COMDAT with `.section NAME,"flags",discard,SYM` |
| Extern data | through a `.refptr.NAME` COMDAT pointer (MinGW auto-import); `dllimport` through `__imp_NAME` |
| TLS | native: `_tls_index`, `%gs:88`, `NAME@SECREL32`, data in `.tls$` |
| Bit-fields | MS layout by default (`-mms-bitfields`); to be confirmed per case by `vcc-bflayout.py` |

## What validation needs

Already here: clang 23 (the oracle), lld (the reference linker), the LLVM
object tools, Wine 11.  To add:

1. **A mingw-w64 sysroot** (the only missing piece locally).  Use a pinned
   **llvm-mingw** release (UCRT, x86-64 Linux host), fetched to
   `work/llvm-mingw` by `bench/fetch-testsuites.sh llvm-mingw` with its
   SHA-256.  It has the headers, CRT, import libraries, compiler-rt builtins
   and libunwind in one tree and needs no GCC.  Only its `x86_64-w64-mingw32/`
   sysroot and `lib/clang/*/lib/windows` are used; the compiler stays the
   host's clang 23 (`--sysroot`, `-rtlib=compiler-rt`, `-fuse-ld=lld`), so the
   oracle is the same clang as for every other table.  Ubuntu's
   `mingw-w64-x86-64-dev` is the fallback: it is MSVCRT-based and needs
   libgcc from `gcc-mingw-w64`.
2. **A Wine prefix per build tree** (`build/.../wine`, `WINEPREFIX`,
   `WINEDEBUG=-all`, no display), created once by the harness, with a
   persistent `wineserver -p` during a run so that 2000 short programs do not
   each pay for server start.
3. **Real Windows**, in CI only: a `windows-latest` job that runs the
   executables cross-compiled by the Linux job (see WIN9).  Wine's loader and
   unwinder are more forgiving than Windows' (guard pages, epilogue
   recognition, image checks), so Wine alone does not prove the unwind tables
   or stack probes.
4. **Unwind tests that depend on `.pdata`** (new, `src/test/win64/`):
   a stack walk with `RtlCaptureStackBackTrace`/`RtlVirtualUnwind` through
   vcc frames of every prologue shape; `longjmp` across vcc frames (UCRT's
   `longjmp` unwinds with `RtlUnwindEx`); a C++ `throw` in a clang++ object
   that crosses vcc's C frames; a fault inside a vcc function caught by a
   vectored handler that unwinds out.
5. Nothing else: no KVM, no Windows machine here, no Visual Studio.

## Steps

One PR, one commit (or a few) per step; every step leaves all existing
harnesses green.  If the PR grows too large to review, WIN7 (vlink) and WIN8
(projects) are the natural places to cut.

| # | Deliverable | Criterion |
|---|---|---|
| WIN0 | **Reference chain and harness skeleton.** llvm-mingw fetch; `bench/hostenv.py` finds the sysroot and Wine; `bench/vcc-win64.py` (from `vcc-x86.py`): every test built by clang for the target, linked by lld, run under Wine, results cached in `build/.../win64tests/ref`; the test runtime (`src/test/rt`) builds for the target | clang's side of the three suites (gcc torture, tcc tests2, `src/test/vcc`) runs under Wine; tests that cannot work on Windows (signals, `fork`, LP64 assumptions) are listed with a reason |
| WIN1 | **Target and front end.** `PP_WIN64` (triples `x86_64-w64-mingw32`, `x86_64-w64-windows-gnu`, `x86_64-pc-windows-gnu`); `CC_LLP64(c)`; predefines and `__has_*` columns from clang (`gen-ppdata.sh`, `src/driver/predef.c`); `wchar_t`/`wint_t`/`size_t`/`intptr_t` types; integer literal typing with a 32-bit `long`; the calling-convention keywords as attributes (`__cdecl`, `__stdcall`, `__fastcall`: accepted and ignored on x64); `dllimport`/`dllexport`; `ms_abi`/`sysv_abi`; `__builtin_ms_va_list`; `ms_struct`/`gcc_struct` and MS bit-field layout; search paths from `--sysroot` | `vpp -dM -E` = clang's byte for byte; `vcc-diff.py`-style parity on the sysroot's headers (`windows.h`, `stdio.h`, every CRT header); `vcc-sema.py layout`, `vcc-bflayout.py` and `vcc-ldconst.py` for the target checked by clang; `vcc-driver.py` cases |
| WIN2 | **Calling convention**, direct emitter (`arch/x64/codegen/abi.c` split into `abi-sysv.c` and `abi-win64.c` behind one interface) and VIR (`arch/x64/vir/calling-convention.c`): argument and return classes of the table above, shadow space, varargs both ways, the callee-saved set (GPR and xmm6-15), no red zone, `ms_abi`/`sysv_abi` per function on both OSes | `bench/vcc-abi.py --target win64` and `vcc-abi-generated.py`: clang/vcc x caller/callee, under Wine; `ms_abi` functions on x86-64 Linux run natively against clang |
| WIN3 | **Frames, probes and `-S`.** Prologue in the form the Windows unwinder requires (pushes, one allocation, optional frame pointer, XMM saves) with the `.seh_*` directives; epilogues only in the forms the unwinder recognises (`add`/`lea rsp`, pops, `ret` or tail `jmp`), which constrains shrink-wrapping and tail calls; `___chkstk_ms` for large frames, `alloca` and VLAs; `.def/.scl/.type/.endef`, `@feat.00`, section names and flags (`.rdata`, `.tls$`, `.ctors`), COMDAT, `.comm` with log2 alignment, weak externals; `.refptr.` stubs, `__imp_`; native TLS | `vcc-win64.py`: vcc `-S`, assembled by clang, linked by lld, run under Wine: the three suites match clang's outputs; the unwind tests of point 4 pass under Wine |
| WIN4 | **Assembler for COFF.** `vas`/inline asm: the `.seh_*` family, `.def/.scl/.type/.endef`, `.section` with COFF flags and COMDAT selection, `.secrel32`, `.secidx`, `.rva`/`@IMGREL`, `@SECREL32`, `.linkonce`, `.weak` as weak external | `bench/vas-obj.py` cases for the target; `vas-corpus.py` on `clang -S` of the test C files for the target |
| WIN5 | **COFF objects.** `src/platform/win/coff.c` beside `elf.c` (header, section table, symbol table with aux records, string table, relocations `ADDR64 ADDR32 ADDR32NB REL32 REL32_1..5 SECREL SECTION`, COMDAT, `.bss` sizes, alignment in section flags) and `.pdata`/`.xdata` built from the SEH directives as LLVM MC does; `-c` directly from the records | `vcc-win64.py --objcmp`: `vpp -c` byte-identical to clang assembling vcc's `-S`; `llvm-readobj --unwind` equal; the suites run with `--obj`; `bench/vlink-bad.py`-style corrupt COFF input fails cleanly |
| WIN6 | **Debug info and the rest of the flags.** DWARF in COFF sections (`.debug_*` with section-relative relocations) for `-g`; `-ffunction-sections`/`-fdata-sections`; `-fPIC`/`-fPIE` accepted (everything is position independent already); `-municode`, `-mwindows`, `-mconsole`, `-mthreads` in the driver | `vcc-dwarf.py` for the target (`dwarfdump --verify`, `.text` unchanged); Wine's `winedbg --command bt` shows vcc's frames with lines; `vcc-driver.py` |
| WIN7 | **vlink writes PE.** `src/platform/win/pe-linker.c` (as `macho-linker.c`: chosen when the first input is COFF or by `-m i386pep`): COFF and archive input, import libraries (short import records and GNU-style `.idata$N` objects), `.idata` synthesis, section merge by `$` suffix, COMDAT and weak-external resolution, `--gc-sections`, base relocations, sorted `.pdata`, the TLS directory, MinGW pseudo-relocations, `.ctors`/`.dtors`, `__ImageBase` and the other linker symbols, deterministic header (timestamp, checksum), `--subsystem`, `--entry`, `-shared` with an export table and `--out-implib` | `vcc-win64.py --vlink`: the loaded image identical to lld's for every test (as `VCC_LDCMP` does for ELF) and it runs under Wine; `vlink-opts.py` cases vs `ld.lld -m i386pep`; corrupt inputs fail cleanly; a DLL built by vlink loads into a clang-built program |
| WIN7R | **vcc's own runtime library** for the target (`src/runtime/`, built by vcc into `libvccrt.a`, linked by the driver in place of compiler-rt/libgcc): `___chkstk_ms`, the `__int128` helpers (`__udivti3 __divti3 __umodti3 __modti3 __multi3`, shifts), the integer/FP conversions the code generator calls, whatever `long double` and `__float128` operations are not inline | every helper tested against compiler-rt's results on generated inputs (differential, under Wine); the three suites and WIN8's projects link with `-nodefaultlibs` + `libvccrt.a` + the CRT only: `llvm-nm` of each vcc-built executable's link shows no object from compiler-rt, libgcc or libunwind |
| WIN8 | **Real projects.** zlib, SQLite and FFmpeg (C fallbacks, then with NASM) cross-compiled by vcc, their own test suites run under Wine, against clang's cross build (`bench/zlib/run.py`, `bench/sqlite/run.py`, `bench/ffmpeg/run.py` with `--target win64`); `vccd` serves the cross builds (`CC="vcc --target=..."` in a meson/CMake cross file) | the same test sets pass as with clang; no vcc-only failures; zlib's output bytes equal clang's build |
| WIN9 | **CI.** `.github/workflows/win64.yml` (reusable, called from `premerge.yml`, added to `gate`'s `needs`) with two jobs: **`wine`** on `ubuntu-24.04` in the Debian container (`ci/setup-debian.sh win64` adds Wine and the pinned llvm-mingw; `ci/premerge.sh win64` runs WIN1-WIN7's harnesses and builds zlib and SQLite with vcc), which uploads the vcc-built executables with a manifest of expected output and exit code per test; **`native`** on `windows-latest`, which downloads that artifact and runs every executable on real Windows with a small Python runner, including the unwind tests | both jobs green and in the gate; the cache keys include the llvm-mingw and Wine versions (`toolfp`) |
| WIN10 | **vcc on Windows** (after the PR; listed so the design leaves room): `src/port/` for Windows (file mapping with `VirtualAlloc` reservations, threads, named pipes for `vccd`, no `fork`), vcc built by vcc for the target, and the `native` CI job compiling the suites on the runner | `vcc.exe` built by vcc compiles the suites on `windows-latest`; `-S` output byte-identical to the Linux-hosted vcc (as `vcc-self.py` for wasm) |

Order: WIN0 first (nothing can be judged without the reference side); WIN1
and WIN2 are independent of each other; WIN3 gives running programs with
clang as the assembler and lld as the linker, which is where the suites start
finding bugs; WIN4-WIN5 remove clang's assembler; WIN7 removes lld; WIN9 can
start as soon as WIN3 runs (its `ci/premerge.sh win64` grows with the steps).

## Design notes

- **OS against CPU.** `X86(c)` stays "the x86-64 CPU"; a new `WIN64(c)` (and
  `ELF(c)` where a Linux assumption is really the object format) picks the OS
  branch.  What is shared by Darwin and Windows on x86-64 later moves with
  it.  The directory is `src/platform/win/`.
- **One ABI interface, two classifiers.** The walker and VIR ask the same
  questions (where does argument N go, how is this type returned, what is
  callee-saved, how much outgoing space); `sysv_abi`/`ms_abi` on a function
  type select the classifier per call, so both must be compiled in for every
  x86-64 OS.
- **Unwinding is by decoding.** Windows unwind version 1 finds epilogues by
  disassembling at the fault address.  The record optimizer and VIR's
  shrink-wrapping must not put anything but the canonical instructions
  between the stack restore and the `ret`; `arch/x64/codegen/tail.c` and the
  frame code get a Windows mode, and a checker in `vcc-win64.py` decodes
  every epilogue of every test object.
- **LLP64 in the front end.** Every place that assumes
  `sizeof(long) == sizeof(void *)` outside `CC_ILP32` has to be found: grep
  for the `long` type used as `size_t`/`intptr_t`, then the layout and sema
  harnesses on the sysroot's headers.  This is the step most likely to touch
  shared code.
- **`long double` printing.** UCRT's `printf` takes `long double` as
  `double`; mingw-w64's own `__mingw_printf` handles the 80-bit type
  (`__USE_MINGW_ANSI_STDIO`).  The test runtime must select the same one on
  both sides.

## Risks and open questions

- **Byte-identical objects** need MC's COFF choices exactly: symbol table
  order, section-definition aux records, COMDAT association of `.pdata` and
  `.xdata` with their function sections, `.xdata` deduplication, and the
  `.debug$S` and `.rdata$zzz`-style records clang itself adds.  The
  comparison is against clang assembling vcc's text, so only what the
  assembler adds counts.
- **Wine against Windows.** A test green under Wine can fail on the runner
  (and the reverse, rarely).  The `native` job is the judge; anything found
  there gets a test in `src/test/win64/`.
- **Bit-field layout:** MS rules differ from GCC's for mixed base types,
  zero-width fields and `packed`; `ms_struct` and `gcc_struct` can be mixed
  in one TU.  `vcc-bflayout.py` generates records for both.
- **Import libraries** come in two formats and mingw-w64's are built by
  either `dlltool`; vlink must read both, and lld's handling of the GNU
  format is the reference.
- **Intrinsics:** mingw-w64's `intrin.h` and `psdk_inc/intrin-impl.h`
  implement `_BitScanForward`, `__readgsqword`, `_InterlockedExchange` and
  the like with inline asm and `__builtin_*`; `vcc-builtins.py` for the
  target shows which builtins the headers need.
- **Wine in CI:** Debian 13 packages an older Wine than the 11.0 here; either
  WineHQ's repository in `ci/setup-debian.sh` or accept the difference and
  let the `native` job cover it.
- **Windows runner minutes** cost twice Linux's on the free plan; the
  `native` job only runs prebuilt executables, so it stays short.

## Status

### WIN0 (2026-11-13): done

`bench/fetch-testsuites.sh llvm-mingw` fetches llvm-mingw 20261104 (UCRT,
x86-64 Linux host; SHA-256 checked) to `work/llvm-mingw/`.
`bench/hostenv.py` has `MINGW_SYSROOT` (`VCC_MINGW_SYSROOT`), `WINE`
(`WINE`), `wine_env` and `wine_start` (a prefix per build tree,
`BUILD/wine`, created once, one `wineserver -p60`; `WINEDEBUG=-all`, no
display, no Mono/Gecko, no winedbg).  `bench/vcc-win64.py` (from
`vcc-x86.py`) builds every test with the host's clang 23 and links with
`clang --target=x86_64-w64-mingw32 --sysroot=S -resource-dir=S/lib/clang/23
-rtlib=compiler-rt --unwindlib=none -fuse-ld=lld -static` (llvm-mingw's
resource dir for the compiler-rt builtins only; the default unwindlib looks
for libgcc_s).  The test runtime: `src/test/rt/include-win64/` before
`rt/include` (`setjmp.h`: the 256-byte `_JUMP_BUFFER`, UCRT's `_setjmp` with
a null frame, so `longjmp` does not unwind; `errno.h`: `_errno()`), and
`rt/win64.c` linked into both sides: `stdin`/`stdout`/`stderr` as variables,
the printf family through `__mingw_v*printf` (80-bit `long double`, as
`__USE_MINGW_ANSI_STDIO=1`), `abort()` exits 3 quietly, an unhandled
exception prints its code and exits 255.

`--ref-only`, measured: 2172 tests (gcc 1701, tcc 143, vcc 328), 2012
reference-ok (1612, 106, 294), 160 excluded: 68 clang-rejects, 26 ref-fail,
28 tcc-specific, 2 static (return-addr, a vcc-noref) — all as for x86-64
Linux — and 36 only for this target, each in `WIN64_WHY` with its reason:
lp64 22 (20 of them our own `src/test/vcc` tests with a 64-bit `long`; they
can move to `long long`), sjlj 4 (`__builtin_setjmp`), ub-trap 3 (clang makes
`unreachable` an `int3` on Windows), ms-layout 2, libc 2, frame 1, asm 1,
abi 1.  60 s cold at `-j 6` on this host, 0.3 s from the cache.  vcc's side
is wired (`vpp -S`, `--obj`, `--objcmp`) and fails at `as` until WIN1-WIN3.

### WIN4, WIN5 (2026-11-13): the standalone assembler done; `vpp -c` waits for WIN1-WIN3

`vasm --target=x86_64-w64-mingw32 x.s -o x.o` writes the COFF object clang
-c writes, byte for byte.  Nothing from LLVM: `src/platform/win/coff.c` (the
writer: section numbers with associative COMDAT last, section-definition
records with MC's JamCRC from 0, COMDAT symbols after their section,
registration-order symbols, weak externals with `.weak.NAME.default.UNIQUE`,
`.file` records, a tail-merged string table, ADDR64 ADDR32 ADDR32NB REL32
SECREL SECTION, the >= 0xffff relocation-count record, time stamp 0),
`src/platform/win/unwind.c` (UNWIND_INFO version 1 at `.seh_endproc` or
`.seh_handlerdata`, `.pdata` at the end; `.xdata$NAME`/`.pdata$NAME`
select-any COMDAT for a `.text$NAME` COMDAT section, as MinGW has no
associative COMDAT; a pair of its own for any other section) and
`src/as/coff-directives.c` (`.def/.scl/.type/.endef`, `.section` flags
`a b d n D r s w x y i` with `discard one_only same_size same_contents
associative largest newest` and `,unique,N`, `.linkonce`, `.secrel32`,
`.secidx`, `.rva`, `.symidx`, `.weak`/`.weak_anti_dep` as weak externals,
`.file "x"`, `.seh_proc endproc pushreg stackalloc setframe savereg savexmm
pushframe endprologue startepilogue endepilogue handler handlerdata
unwindversion endfunclet unwindv2start`), plus `@IMGREL`/`@SECREL32`, `@` in
names (`@feat.00`), `.comm` with a log2 alignment and MinGW's `-aligncomm`
in `.drectve`, `.cfi_*` as MC's COFF `.eh_frame` (absolute 8-byte FDE
addresses, data alignment -4), and MC's rule that pc-relative references to
a function (`.type 32`) keep their relocation (also for jump relaxation).
ELF-only directives (`.size .hidden .local .ident .previous .subsection
.symver .weakref .rodata ...`) are errors at the same lines as MC's.

Two fixes in shared code came out of the corpus: x86 jump relaxation now
follows MC's relaxOnce (a backward jump sees the live layout, a forward one
the previous pass's for both ends), and the CFA offset of x86-64 CFI starts at
the CIE's 8 (`.cfi_adjust_cfa_offset` first was off by 8 on ELF too).

Measured:
- `bench/vas-obj.py`: 77/77 (13 new `win64-*.s` cases, 3 of them error files).
- `bench/vas-corpus.py --target x86_64-w64-mingw32` (clang -S at -O0 and
  -O2 of every C file under `src/test`): 4494/4494 identical; 854 skipped
  (clang -S fails without a sysroot: headers the test runtime lacks).
- `bench/vcc-win64.py --vasm-ref`: 2011/2012 run like clang's build under
  Wine and are clang -c's object (gcc 1612, tcc 106, vcc 293/294); the one
  left is `vcc/debug_asm` (`-g`): clang writes DWARF 4 line tables for this
  target (include directories and file names in `.debug_line`), vasm writes
  version 5 with `.debug_line_str` (WIN6).
- `bench/vas-mutate.py -n 20000` (two seeds) with the ASan/UBSan vasm: no
  crash; it found two signed overflows on absurd input, fixed.
- Unchanged elsewhere: vas-obj ELF cases, `vas-enc.py` 1181 ok,
  `vcc-x86.py --suite vcc --objcmp` 323/323, x86-64 ELF vas-corpus: the
  same 8 -O2 differences as before the change.

Not done: `.seh_unwindversion 2` and `.seh_splitchained` (chained unwind
info) are refused as unsupported (clang emits neither for MinGW C), `-g`
(above), big-object COFF (more than 65279 sections), `.secnum`/`.secoffset`,
`.cv_*` CodeView directives.

For the code generator (`vpp -c`): the driver sets `obj_set_format(o,
OBJ_COFF)` next to `elf_set_machine(o, EM_X86_64)` (codegen/tu.c), and
elf_finish/elf_emit then write COFF.  The record encoder needs no text: the
`.def` attributes are `ElfSym.coff_class`/`coff_type` (set them and
`elf_reg` the symbol where the text would have the `.def`), `.weak` is
`elf_bind(STB_WEAK)` plus `coff_weak = 3`, sections are `coff_section(o,
name, len, characteristics, comdat_sym, selection)`, `.comm` alignment is
`coff_align_common`, and the unwind information is the `coff_seh_*` calls
of obj.h, each with a temporary label the encoder defines at the
instruction boundary (`coff_seh_proc` at the entry, one call per prologue
instruction after it, `coff_seh_end_prologue`, `coff_seh_end`).

### WIN1-WIN3, WIN6-WIN10 (2026-11-13): planned

Nothing implemented.  The reference chain was checked by hand (clang 23 ->
COFF -> `ld.lld -m i386pep` -> Wine 11 exit code); the sysroot is not
installed.
