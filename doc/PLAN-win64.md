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

### WIN1 (2026-11-13): front end done

- **Target.** `PP_WIN64` (`--target=x86_64-w64-mingw32`, `x86_64-w64-windows-gnu`,
  `x86_64-pc-windows-gnu`; `*-windows-msvc` is an error).  `PP_IS_X86_64(t)`,
  `X86(c)` and `CC_X86_64(c)` mean the CPU (builtins, target attributes, x87,
  feature macros, the x86 code generator); `WIN64(c)` (codegen), `CC_WIN64(c)`,
  `CC_LLP64(c)`, `CC_LONG32(c)` the OS and data model.  The driver sets
  `target_x64` and `target_win64`; no PIE and no DWARF CFI by default.
- **Clang's data** from `src/tools/gen-win64data.sh` (clang 23.1.6, the
  llvm-mingw headers as query candidates): `frontend/pp/ppdata-win64.inc`
  (predefines, `-dD` order, `__has_*`) and `frontend/c/attrnames-win64.inc`.
  The driver's deltas for Windows: `__PIC__ 2` always (no `-fpic`/`-fpie`
  macros), no `_REENTRANT` (`-pthread`), no `__GCC_HAVE_DWARF2_CFI_ASM`, no
  `-fshort-wchar` change, strict modes drop `WIN32 WIN64 WINNT`, C23's `%b`
  formats with `ll`, `-municode` as clang's driver `-DUNICODE` (first on the
  command line).  `-march`/`-m` feature macros as for x86-64 Linux.
- **LLP64.** `long` 4 bytes; `size_t`/`ptrdiff_t` (`c->t_size`, `c->t_ptrdiff`)
  `unsigned long long`/`long long`; `wchar_t` `unsigned short` (cc's
  `short_wchar`); `__builtin_va_list` `char *`; integer literal types and
  `mode(DI/word/pointer)` with a 32-bit `long` (`CC_I64`, `CC_INTPTR`).  Shared
  code that took `long` as the pointer-sized type now takes `c->t_ptrdiff` /
  `c->t_size` (pointer arithmetic indices, array subscripts, VLA bounds,
  library signatures `z`/`Y`).  The code generator's own `prim(TY_ULONG)` as a
  pointer-sized type is for WIN2/WIN3.
- **Attributes.** `ms_abi`/`sysv_abi` on the function type (`ctype.ext`:
  `CC_FNABI_MS`/`CC_FNABI_SYSV` for the convention that is not the target's
  own; part of type compatibility; inherited by a redeclaration without one,
  clang's error for one added later; `cc_func_abi(c, t)`).  `dllimport`/
  `dllexport` in `attrs.dll` (`CC_DLLIMPORT`/`CC_DLLEXPORT`): a dllimport
  variable without initializer is an extern declaration, clang's errors for
  dllimport data definitions, non-inline function definitions and static.
  `cdecl stdcall fastcall thiscall vectorcall regcall pascal` accepted and
  ignored with clang's warnings (Windows: only `pascal` warns).  Where clang
  does not know them for the target (dllimport on Linux, ms_abi on arm64):
  "unknown attribute" as before.  `__builtin_ms_va_list` (`char *`, every
  x86-64 OS; on Linux declared where first named, so other TUs keep their
  symbol numbers) and `__builtin_ms_va_start/end/copy` (the `BI_VA_*` nodes;
  `cc_valist_is_ms(c, type)` tells the kinds apart); `__builtin_ia32_sfence/
  lfence/mfence` as a full barrier (MinGW-w64's `intrin-impl.h`).
- **Bit-fields.** clang's `isMsStruct`: `ms_struct`, `#pragma ms_struct on` or
  `-mms-bitfields` (Windows' default, `-mno-ms-bitfields`), unless
  `gcc_struct` (`RF_GCC_STRUCT`); both on one record is clang's error.
- **Search paths** as clang's MinGW toolchain: the resource headers, then
  `SYSROOT/x86_64-w64-mingw32/include`, `.../usr/include` if it exists,
  `SYSROOT/include` (`--sysroot`).
- **Shared fixes found on these headers:** lazy bodies under `#pragma pack`
  (MinGW-w64's headers are all inside one: the bodies keep the pack value of
  their definition, so unused intrinsics are not parsed); a typedef name of an
  unnamed record as a member declares nothing without -fms-extensions (clang);
  layout claims skip the x86 header builtins clang does not let be named
  (`_mm_sfence`, `__rdtsc`, ...); the pp fixes of the previous commit
  (`#pragma message` text, spliced `-dM` strings, `-dD` undefs, include-guard
  knowledge for `-H`/markers, `#pragma GCC system_header` markers).
- **Verified** (this host, clang 23.1.6, llvm-mingw 20261104):
  `-dM -E` byte-identical for 29 flag sets x 3 triples (87/87);
  `bench/vcc-driver.py` 76 new rows (`-E` and `-dM`) all pass (229/304
  overall, the 75 others are the arm64 tables' clang 23.1.4 version macros,
  as before); `bench/vcc-diff.py --win64` (982 of the 1547 top-level headers
  clang accepts + 8 flag variants = 990 TUs): cmp, text, textc, dm, deps, hdr
  990/990, lines 989/990, dd 988/990; `bench/vcc-sema.py --win64` run
  990/990, batch 990/990, layout 990/990 (21.1 M claims checked by clang);
  `vcc-bflayout.py` x86_64-w64-mingw32 (MS layout) and `-mno-ms-bitfields`
  6467 claims each, x86-64 Linux with `-mms-bitfields` 6449, 0 rejected (also
  seeds 2-4 at -n 5000); `vcc-ldconst.py --target x86_64-w64-mingw32` 6000/6000
  (from `-S` text: no COFF yet), `--target x86_64-linux-gnu` 6000/6000.
  Existing targets: `vcc-same.py` 23914/23914 identical (new modes x0 x2 xc
  xe for x86-64 Linux), `vcc-diag.py` 229/229, `vcc-x86.py --suite vcc`
  320/320, `vcc-stress.py` 36/36, the file-by-file build compiles (its link
  fails on `vir_failure_table`, as before this step); ASan: the sema run on
  135 header TUs and the attribute probes clean.
- **Not done / deviations:** `-fms-extensions` on Windows (clang then makes
  `__declspec` a keyword: its predefines differ); MS intrinsics as builtins
  (`_InterlockedExchange` etc. are only builtins under -fms-extensions); the
  rest of the x86 builtins the intrinsic headers use (`__builtin_ia32_*`,
  `__builtin_elementwise_*`), as on x86-64 Linux: an intrinsic that is called
  fails; `vectorcall`/`regcall` are not separate conventions; `-dD` order of
  `__FAST_MATH__` and of `-march`'s CPU macros, and the marker clang prints
  for `_Pragma` in a macro argument (`ompx.h`), differ as on x86-64 Linux;
  clang prints `ms_abi` pointer types as `void ((*))(void) __attribute__((ms_abi))`,
  vcc without the inner parentheses; `#pragma message(MACRO)` is printed as
  written (clang expands it); `-nostdlibinc`/`-nobuiltininc` are not handled.

### WIN2 (2026-11-14): calling convention done, both code generators

- **One interface, two classifiers.** The walker sets `e->msabi` from
  `cc_func_abi` right before every `LO(classify/call/params/va_*)` (`em_abi`
  in `walk/walk.h`; argument evaluation may make other calls in between), so
  `ms_abi` on Linux and `sysv_abi` on Windows choose per call and per
  function.  `arch/x64/codegen/abi.c` stays System V's and dispatches to
  `abi-win64.c` (`x64w_*`); the result tail is shared (`x64_call_park`,
  `x64_call_value`, `x64_call_xmms`).
- **Microsoft's rules, each checked against clang -S:** positions in
  rcx/rdx/r8/r9 or xmm0-3, the rest at `16 + 8p`, 32 bytes of shadow space at
  every call; records of 1/2/4/8 bytes as integers, all others (empty ones,
  `long double`, `__int128`, `__float128`, `_BitInt(65..)`, vectors) by
  reference to the caller's copy, which the callee uses in place
  (`LO(byref)`: the walker treats the slot as holding the address, as for a
  VLA); `__int128` and vectors returned in xmm0 (a short vector stored and
  loaded no wider than it is; one-lane vectors as their element, as LLVM),
  32/64-byte vectors split in 16-byte halves by reference, each its own
  position, returned in xmm0.. (LLVM without AVX); other records, `long
  double`, `__float128` through the hidden pointer in rcx; `ms_abi` off
  Windows returns `long double` in st(0), and `sysv_abi` on Windows returns it
  through the hidden pointer (clang).  Varargs: a float in both registers; the
  callee homes rcx..r9; `va_list` a `char *`, by-reference types read as a
  pointer; `__builtin_ms_va_list` on Linux.  The runtime helpers (`__divti3`,
  `__addtf3`, `__fixdfti`, ...) through the same convention
  (`x64w_helper`); `__bf16` to `float` inline (MinGW's compiler-rt takes
  `__extendbfsf2`'s argument in an integer register).  A declared `alloca` is
  the builtin on Windows; `main` calls `__main` first.
- **Callee-saved** rbx rbp rdi rsi r12-r15 xmm6-xmm15: print.c saves what the
  body writes (WIN3); a call to a Microsoft callee is marked (`rec.sh`) so
  rsi/rdi/xmm6-15 are not counted as clobbered; the direct emitter's pushes of
  live temporaries leave rsi/rdi out for such calls.  Tail calls only between
  functions of the same convention, not through `__imp_`/`.refptr.`.
- **VIR** (`arch/x64/vir/`): `vx_arg` by position on Windows, shadow space,
  a variadic call's floats copied to the integer registers, calls clobber
  rax rcx rdx r8-r11 xmm0-5, rsi/rdi and xmm6-13 allocatable as callee-saved,
  always a frame pointer; `main`'s `__main` call built in VIR; a convention
  other than the target's falls back (`x64_calling_convention`).  Lowered over
  the suites: 17375 of 18820 functions (x86-64 Linux: 17362 of 18834), the
  rest the same reasons as on Linux (records, variadic definitions, asm).
- **Front end fixes found here:** `ms_abi`/`sysv_abi` on a parameter's
  function pointer (`int (OTHER *f)(...)`) reach the function type
  (`declarator.c`); `ms_struct` (MinGW's default) aligns a builtin scalar
  field to its size whatever a typedef says, not enums (clang's
  `IsMsStruct` rule; `record-layout.c`).
- **Verified:** `bench/vcc-abi.py --target win64` 28 pass, 3 xfail (packed
  boolean vectors, TODO.md), 4 skipped (clang's own build fails or LP64),
  the same with `--vir`; `--target x86_64` 35/35 (the new `msabi_calls`:
  ms_abi functions natively against clang, sysv_abi ones under Wine);
  `bench/vcc-abi-generated.py --target x86_64-w64-mingw32 --count 100`:
  gcc 92 pass, 8 unsupported (decimal float), llvm 100/100 (before the
  `ms_struct` fix gcc's case 36, an under-aligned enum typedef, failed).

### WIN3 (2026-11-14): frames and -S done

- **The Windows frame** (print.c, for Windows and for `ms_abi` functions):
  `push rbp`; one `sub` for a save area of the callee-saved registers the body
  writes (scanned from the final records, asm texts' register names and asm
  clobbers, calls by their callee's convention); `movaps`/`movq` saves with
  `.seh_savexmm`/`.seh_savereg`; `lea S(%rsp), %rbp` with `.seh_setframe %rbp,
  S` (S <= 224 < 240); `.seh_endprologue`; then in the body the realignment
  (r12) and the locals (`___chkstk_ms` for a page or more).  Locals sit below
  the area (the printer rebases negative rbp offsets; asm texts carry a marker
  for it).  Epilogue: the restores, then `leaq 0(%rbp), %rsp; popq %rbp`
  between `.seh_startepilogue`/`.seh_endepilogue`, `retq` or the tail `jmp`;
  `nop` after a call right before it.  Leaf functions without saves keep no
  frame and no unwind data.  `__builtin_frame_address(0)` is the establisher
  frame (rbp less the area, as clang), which MinGW's unwinding setjmp needs.
  alloca/VLA probe with `___chkstk_ms` (`X_PROBE`).  Jump tables in `.rdata`
  after the function (code only in `.text`).
- **COFF text:** `.def/.scl 2|3/.type 32/.endef` per function, no
  `.type/.size/.note.GNU-stack`; `.data .bss .rdata,"dr" .tls$,"dw"`, a named
  section `"xr"/"dr"/"dw"`; -ffunction-/-fdata-sections as `one_only`
  COMDATs; `.ctors/.dtors` (`.ctors.NNNNN` for a priority); extern data
  through `.refptr.NAME` (discard COMDATs in the tail, exactly for
  non-hidden extern variables not defined here and weak undefined symbols),
  dllimport through `__imp_NAME` (`callq *__imp_f(%rip)`); native TLS
  (`_tls_index`, `%gs:88`, `NAME@SECREL32`); no PIC/GOT; -g: `"dr"` DWARF
  sections and `.secrel32` (enough for clang's assembler; WIN6).
- **Verified:** `bench/vcc-win64.py -j 6 --seh` 2012/2016 (gcc 1612/1612,
  tcc 105/106, vcc 291/294, win64 4/4), and the same with `--vir`; the SEH
  checker passes on every object but one naked function.  Remaining:
  `tcc/95_bitfields_ms` (MS layout of a packed record after `:0`, TODO.md),
  `vcc/float128` and `vcc/asm_flag_out` (our tests assume a 64-bit `long`:
  an out-of-range conversion; `btq` on a 32-bit register), `vcc/dead_results`
  under `--seh` (`naked` is not implemented on x86-64 at all: the asm's
  `retq` sits inside a frame; the function is never called).  Wine timeouts
  under load (`gcc/20000217-1`, `20000113-1`) pass alone.  `src/test/win64`:
  `unwind_walk` (RtlVirtualUnwind and RtlCaptureStackBackTrace through no
  saves, GPR and XMM saves, pages, alloca, realigned, variadic frames),
  `unwind_longjmp` (UCRT longjmp with a frame through vcc frames that clobber
  every callee-saved register), `unwind_veh` (a vectored handler unwinds out
  of faults in leaf, saving, paged and alloca frames and resumes the caller),
  `conventions`; all pass with both code generators, clang's build too.
  Existing targets: `vcc-same.py` identical in every mode (and x2/xc with
  `VCC_VIR=l`) except `-E` line markers naming vcc's headers by the base
  tree's path; `vcc-x86.py` 2123/2128 as the base (5 `inline` failures
  there too); `vcc-diag.py` 229/229; `vcc-bflayout.py` 0 rejected on every
  target; `quality.py --check` 22/22.
- **Not done:** `-c` for the target (WIN5: obj.c rebases frame offsets but the
  COFF writer is not here); stack protector uses `__stack_chk_guard`
  directly (untested); `naked`; packed boolean vectors at Windows calls;
  `vectorcall`.  Runtime helper calls vcc emits (for WIN7R): `___chkstk_ms`,
  `__main`, `__udivti3 __divti3 __umodti3 __modti3 __ashlti3 __lshrti3
  __ashrti3`, `__floattisf __floattidf __floatuntisf __floatuntidf __fixsfti
  __fixdfti __fixunssfti __fixunsdfti __floattixf __floatuntixf __fixxfti
  __fixunsxfti`, the `__float128` set (`__addtf3 __subtf3 __multf3 __divtf3
  __eqtf2 __netf2 __lttf2 __letf2 __gttf2 __getf2 __unordtf2 __extenddftf2
  __extendsftf2 __extendxftf2 __trunctfdf2 __trunctfsf2 __trunctfxf2
  __floatsitf __floatditf __floatunsitf __floatunditf __floattitf
  __floatuntitf __fixtfsi __fixtfdi __fixunstfsi __fixunstfdi __fixtfti
  __fixunstfti`), the half/bfloat16 set (`__extendhfsf2 __truncsfhf2
  __truncdfhf2 __truncxfhf2 __trunctfhf2 __truncsfbf2 __truncdfbf2
  __truncxfbf2 __trunctfbf2`), and from the C library `memcpy memset memmove
  strlen floorf ceilf truncf roundf rintf nearbyintf fabsf`
  (`__stack_chk_guard`/`__stack_chk_fail` under -fstack-protector).

### WIN4-WIN10: planned
