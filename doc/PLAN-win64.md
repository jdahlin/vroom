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

### WIN4, WIN5 (2026-11-13): the standalone assembler done (integrated 2026-11-14; `vpp -c`: below)

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

### WIN7 (2026-11-13): vlink writes PE, byte-identical to lld for clang's objects (integrated 2026-11-14)

`src/platform/win/pe-linker.c` (one TU like `ld/ld.c`, #including
`pe-options.c`, `pe-input.c`, `pe-resolve.c`, `pe-export.c`,
`pe-layout.c`, `pe-write.c`; model in `pe-linker-internal.h`, COFF/PE
constants in `pe-definitions.h`, to be shared with WIN5's `coff.c`):
`ld_main` hands a link to it when the first input is a COFF x86-64 object
or the line has `-m i386pep`.  It takes the GNU ld command line that clang
passes to `ld.lld` for MinGW and reproduces lld-link's MinGW mode:

- **input**: COFF objects (COMDAT with every selection and lld's MinGW
  merging of any/largest and any/same-size, associative sections and the
  implicit `.pdata$F`/`.xdata$F` association, weak externals, COMMON with
  `-aligncomm`, `.drectve` exports/includes/alternate names, long section
  names, relocation-count overflow), `ar` archives with the GNU or the
  Microsoft symbol table, short import records and GNU dlltool-style
  `.idata$N` objects, `.def` files; files read in lld's FIFO order (an
  archive member is queued by the reference that needs it), a file named
  twice read once, `crtend.o` moved last;
- **resolution**: entry point (`mainCRTStartup`/`WinMainCRTStartup`/
  `DllMainCRTStartup`), `__ImageBase`, the `__guard_*` absolutes
  (`__guard_flags` = 0x100 when an object has `@feat.00` bit 11),
  `__CTOR_LIST__`/`__DTOR_LIST__`, `__RUNTIME_PSEUDO_RELOC_LIST__`,
  alternate names, `_load_config_used`, MinGW auto-import (`__imp_` loads,
  `.refptr.` replacement, pseudo relocations), `__imp_` of local symbols,
  undefined symbols reported as lld does, `--gc-sections` (COMDAT and
  associative aware), MinGW auto-export for DLLs;
- **output**: lld's partial sections (sorted by name, `$` suffix of MinGW
  COMDATs stripped, `.CRT` sorted), output sections in link.exe's order with
  `.idata .xdata .CRT .ctors .dtors .00cfg .edata` merged into `.rdata` and
  `.bss` into `.data`, synthetic `.idata` and import thunks, `.edata` and
  `--out-implib` (byte-identical to lld's), `.buildid` with the XXH3 build
  id, base relocations, sorted `.pdata`, the TLS directory and its alignment,
  the load config directory, the COFF symbol table and string table (lld's
  StringTableBuilder order), `SOURCE_DATE_EPOCH`/`--no-insert-timestamp`.

Measured:

- `bench/vcc-win64.py --vlink-ref -j 4` (clang's object of every test linked
  by lld and by vlink with clang's own link line, `--ld-path`): runs-ok
  2012/2012 and image-identical 2012/2012 (gcc 1612, tcc 106, vcc 294),
  83 s cold at `-j 4`.
- `bench/vlink-pe.py --run`: 57/57 cases against `ld.lld -m i386pep`
  (options, errors, weak, TLS, constructors, selectany, COMMON, archives,
  `--whole-archive`, DLLs with `--out-implib` and a `.def`, auto-import of
  data from a DLL, a GNU dlltool-style import library, local `__imp_`):
  same success or first error, byte-identical outputs, same run under Wine.
  A DLL built by vlink loads into a program linked by lld and the reverse.
- `bench/vlink-pe-bad.py` (ASan+UBSan): 530/530 (field-by-field and 300 random mutations) corrupt COFF objects,
  archives and import records fail cleanly.
- ELF and existing users unchanged: `bench/vlink-opts.py` 66/66,
  `bench/vlink-bad.py` 34/34, `bench/vcc-x86.py --suite vcc --vlink` 293/293
  (as before the step).  The unity and the file-by-file (`VCC_UNITY=`)
  builds compile vlink; the file-by-file vccd link fails on
  `vir_failure_table` defined in several codegen files, which predates WIN7.

Known differences from lld (none seen in the suites): the hash order of
lld's symbol table is not reproduced where lld iterates it (auto-import
loads of `__imp_` members for still-undefined symbols, the order of
`__imp_` pointers to local symbols; vlink uses the order of first
reference); undefined-symbol reports have no DWARF file:line; `-Map`
writes the section and input-section lines of lld's map, not the symbol
lines; lld 23 accepts `--major/minor-image-version` and writes 0.0, as does
vlink.  Not implemented (a clear error): `--icf=all`, `--guard-cf`,
`--wrap`, `--defsym`, `--delayload`, `--pdb`, `--output-def`, `-Xlink`,
`.rsrc` sections, bigobj, thin archives, LTO bitcode, `/merge` and
`/section` in `.drectve`; i386/ARM targets.

### WIN7 on current main (2026-11-14)

Merged with main after the linker split (#230) and the renames (#224,
#225, #228, #231): `src/Makefile` keeps main's `LD_PARTS` and adds
`platform/win/pe-linker.c` to `LD_SRC` and `VCCD_LD_OBJ`; `pe-*.c` and
`ld/link-entry.c` needed no change.  The file-by-file build's duplicate
`virbuild_failure_table` (present on main without WIN7) is fixed: the
declaration in `codegen/emitimpl.h` (`codegen-internal.h` since main's
rename) is `extern` outside `VCC_UNITY`.
Measured after the merge: unity and file-by-file builds clean;
`vlink-pe.py --run` 57/57; `vcc-win64.py --vlink-ref -j 4` runs-ok and
image-identical 2012/2012 (134 s cold); `vlink-pe-bad.py` (ASan) 530/530;
`vlink-opts.py` 66/66; `vlink-bad.py` 34/34; `vcc-x86.py --suite vcc
--vlink` 295/295.

### WIN7R (2026-11-14): vcc's runtime library; programs link without compiler-rt (integrated 2026-11-14)

`src/runtime/` (internal header `runtime-internal.h`, 12 C files and
`stack-probe.S`, about 1900 lines) builds `libvccrt.a` with
`src/runtime/build.sh` (`make -C src vccrt`; CC and AR are parameters,
today clang for the target and vcc's `var`).  Written from the helpers'
specifications, no compiler-rt or libgcc source; compiler-rt is only the
oracle in `bench/vccrt-test.py`.

Which helpers, from data (`vccrt-test.py needs`): 70.  The test objects
(clang, -O0 and -O2, the 2012 reference-ok tests) need 41 of them (36 at
-O2; -O0 adds `__modti3 __umodti3 __muldc3 __trunctfsf2 __trunctfxf2`):
`___chkstk_ms` in 85 objects; `__int128` division in `pr84748`,
`bitint_runtime`; `__float128` arithmetic, comparisons and conversions in
`float128`; x87 <-> `__int128` in `ldouble_int128`; `_Float16`/`__bf16`
narrowings; `__divsc3 __divdc3 __divxc3 __muldc3`.  The CRT needs
`___chkstk_ms` (libmingw32 pseudo-reloc, libmingwex) and `__mulsc3
__muldc3 __mulxc3 __divsc3 __divdc3 __divxc3` (libmingwex's cpow*/ctan*);
vcc's code generator names the rest (`__ashlti3 __lshrti3 __ashrti3`, the
sf/df `__int128` conversions, the remaining tf conversions, `__trunctfhf2
__trunctfbf2 __truncxfbf2 __truncxfhf2 __extendbfsf2`, `__divtc3`,
`__powisf2 __powidf2 __powixf2 __powitf2`).  `__multc3` is added (clang
calls it for `_Complex __float128` `*`).  Nothing else from the builtins
archive is reachable: no atomics, `__multi3`, `__popcount*` or `__bswap*`
(clang and vcc inline them for x86-64).

ABI, checked with `clang -S` for the target: every helper is called with
the C ABI of its C prototype (`__int128`, `long double` and `__float128`
arguments by reference, `__int128` returned in xmm0, `long double`,
`__float128` and `_Complex double` and wider returned through a hidden
pointer, `_Float16`/`__bf16` in xmm0), so the C definitions match;
`___chkstk_ms` keeps every register (rax included) and touches each page
from the caller's rsp down to rsp - rax, in order.

Measured:

- `vccrt-test.py diff`: 70/70 helpers bit-identical to compiler-rt under
  Wine (1.19 M calls, 2.7 s; `-n 300000`: 17.4 M calls, 21 s); three are
  checked against the exact result computed by the harness instead:
  `__truncxfbf2` (not in compiler-rt), `__truncxfhf2` (compiler-rt rounds
  twice, through double: `bff4cf90000000200000` gives `927c`, the correct
  result is `927d`), `__extendbfsf2` (compiler-rt's reads the `__bf16` from
  `%cx`; clang passes it in `%xmm0`).  Complex results compare NaN parts
  only as NaN (which NaN depends on the order of the hardware operations).
  Choices matched to compiler-rt where the specification is open: the
  default NaN is positive, unordered comparisons give 1 (eq/ne/lt/le) or
  -1 (gt/ge), widening keeps a signalling NaN signalling, `__trunctfxf2`
  and `__truncsfbf2` quiet a NaN only when payload bits are lost.  Out of
  range float-to-integer conversions (undefined in C) saturate and are not
  compared.  The Annex G division keeps a NaN quotient for a divisor of
  NaN and zero parts (the example's text would give zero).
- `vccrt-test.py chkstk`: registers preserved for sizes 0-250000, every
  page of the range written (GetWriteWatch: 489 pages for 2 MB), frames
  up to 950000 bytes on a 1 MiB-reservation thread.  Under Wine only the
  write-watch check detects a probe that skips pages (Wine commits thread
  stacks itself); compiler-rt's probe reads, so that check is libvccrt's
  only.  Real Windows (WIN9's native job) is still to run it.
- `vccrt-test.py native` (x86-64 Linux, SysV): the library compiled by vcc
  itself (`vpp --target=x86_64-linux-gnu`) is bit-identical to the
  clang-built one on all 70 helpers; host compiler-rt 66/69 (the three
  differences explained), libgcc 49/70 (soft-fp's negative default NaN and
  2/-2 unordered results, NaN quieting, its own complex division).
- `vcc-win64.py --vccrt-ref -j 4`: 2012/2012 tests run as the reference
  linked with libvccrt.a instead of compiler-rt, by lld and by vlink; no
  link map names compiler-rt, libgcc or libunwind (80 s).

Not done: building with vcc for the target needs WIN1-WIN5 (the target,
the Windows ABI, COFF objects and `.seh_*` in `vas` for `stack-probe.S`);
then CC="vcc --target=x86_64-w64-mingw32" is the only change.
The driver does not link it yet (WIN8: `-nodefaultlibs` + libvccrt.a).
Rounding modes other than to nearest and exception flags are not
supported (C cannot read MXCSR without an intrinsic).

- 2026-11-14: `var` archives COFF, so the library no longer needs llvm-ar:
  `build.sh`'s AR defaults to `var` (VCC_BIN, default the build directory,
  run against a private vccd), `AR=` still overrides.  `src/utils/ar.c`
  writes llvm-ar's COFF format when the first member is a COFF object or
  import record: GNU's first linker member with each name once, the second
  linker member (member offsets, 16-bit member indices, names sorted),
  long names as `NAME\0`; the symbols as llvm's object library reads
  them (external but undefined, weak externals of the alias search kind,
  common, absolute; bigobj; short import records `__imp_X` and `X`, data
  imports `__imp_X` only; identify_magic's machine list; the 32-bit string
  table position and recovery from a broken symbol or string table).
  Also `U` (real dates, uids, gids, modes), one `//` entry per long name,
  a regular archive as a plain member, and on Linux GNU (not BSD) for an
  archive whose first member is not an object, all as llvm-ar.
  ARM64EC/ARM64X members and archives mixing ARM64 with other COFF
  machines (llvm-ar adds `/<ECSYMBOLS>/`) are refused.  Verified:
  `bench/var-cmp.py coff` (new mode, no kernel tree) 230/230 cases
  byte-identical to llvm-ar 23 (115 command sequences, again with
  SYM64_THRESHOLD=0: rcs/rc/qc/crsD/D/U/S/T, r/q/d/s/m/t/p/x, libvccrt's 13
  objects, 356 COFF and 51 ELF objects from the suites, 60 generated files,
  llvm-dlltool's import library), 1500/1500 mutants (same bytes or both
  rejected), ASan/UBSan clean on all cases and 1000 mutants; the old ar.c
  against the new on the 38 ELF-only cases: the 27 Kbuild-style ones
  unchanged, the 11 others the intended fixes; `make vccrt` with var
  gives the byte-identical libvccrt.a llvm-ar gives for the same objects;
  `vcc-win64.py --vccrt-ref -j 4` 2014/2014 (lld and vlink), 0 foreign;
  `vccrt-test.py diff` 70/70.

### WIN9 (2026-11-14): CI written, run locally; first GitHub run pending

`.github/workflows/win64.yml` (reusable, called from `premerge.yml`, in
`gate`'s `needs`):

- **`wine`** (ubuntu-24.04, `container: debian:trixie`, 45 min limit):
  `ci/setup-debian.sh win64` installs Debian 13's Wine (10.0; no i386
  architecture, 64-bit loader only) and runs `bench/fetch-testsuites.sh
  llvm-mingw` (SHA-256 checked; in a plain checkout it unpacks into
  `work/llvm-mingw`, where `bench/hostenv.py` looks).  The release tarball
  (80 MB) is cached by `actions/cache` keyed on `MINGW_REL`/`MINGW_SHA256`;
  `toolfp` adds `wine --version` and the pin.  A second cache holds ccache,
  `win64tests/ref` and the pass caches.  Then `ci/premerge.sh win64`:
  `vcc-win64.py --ref-only`, `--vlink-ref`, `--vccrt-ref --export`,
  `vlink-pe.py --run`, `vccrt-test.py diff --export` and `chkstk --export`,
  `var-cmp.py coff` (moved here from the arm64 job), and `win64-pack`:
  `build/linux/win64-exes.tar.xz`, uploaded as the `win64-exes` artifact.
- **`native`** (windows-2025, 15 min limit, `needs: wine`): sparse checkout
  of the runner and the allow-list, Python 3.12, `bench/win64-native.py
  win64-exes.tar.xz --allow ci/win64-native-allow.txt`.  No compiler.

`--export DIR` (`vcc-win64.py`, any mode; `vccrt-test.py diff|chkstk`)
writes the passing programs and `DIR/*.manifest.json` (per test: exe, argv,
stdin, expected stdout, exit code, timeout; expected = clang's reference
under Wine).  A test passing from the pass cache is linked again (the link
is deterministic) and exported without running.  The exported programs
today: clang's objects linked by vlink with libvccrt.a (2013) plus the
differential and stack-probe programs of `vccrt-test.py`.
`bench/win64-native.py` runs each in an empty directory, in parallel, with
the manifest's timeout; stdout compared with `\r\n` as `\n`, exit codes
equal (a code above 255 that is not an NTSTATUS also modulo 256, as Wine
reports them); failures to `$GITHUB_STEP_SUMMARY`; allowed failures need a
reason and an allowed test that passes is an error.  `--wrapper wine` runs it
on Linux.  `vlink-pe-bad.py` runs in the asan job (arm64), beside
`vlink-bad.py`.

**Guarded** in `ci/premerge.sh` (`has FILE TEXT`; a skipped step prints
`skip NAME (why)` and a summary row), so each switches on when its feature
is merged, with no CI change:

| step | runs when | then |
|---|---|---|
| `win64-own` | `bench/vcc-win64.py` has `'--own'` | `vcc-win64.py --own --export`: the exported programs become vcc's (and `--vccrt-ref` runs without `--export`) |
| `win64-own-vir` | ... and `'--vir'` | `vcc-win64.py --own --vir` |
| `abi-win64` | `bench/vcc-abi.py` mentions `win64` | `vcc-abi.py --target win64` |
| `vas-obj-coff` | `src/test/vas-obj/{win64,coff}-*.s` exist | `vas-obj.py` |
| `zlib-win64`, `sqlite-win64` (WIN8 hook) | `bench/{zlib,sqlite}/run.py` mention `win64` | `run.py --target win64 all` |

Found on the way: `gcc/941014-2` prints a field of fresh `malloc` memory
when it happens to be set; under Wine the reference printed `17742` once and
nothing later.  It is in `WIN64_SKIP` (the `native` job would be flaky on
it); `--vlink-ref` retries a Wine timeout once, as the other modes do.

Measured (this host, 16 threads, `CI_JOBS=4`):

- `ci/premerge.sh win64` on the host (Wine 11.0), cold caches: 5 min
  (`win64-ref` 77 s, `--vlink-ref` 70 s, `--vccrt-ref` 116 s, `var-coff`
  24 s, the rest under 5 s each); warm: 2 min 53 s (`ref` 1 s, the two
  link modes 81 s each: compile and vlink again, no Wine).
- In `debian:trixie` under docker with the checkout copied (no `.git`, no
  `work/`), as CI: `ci/setup-debian.sh win64` 74 s with the llvm-mingw
  download, 65-68 s from the cached tarball; `ci/premerge.sh win64` 5 min
  22 s cold (vcc built without ccache), 2 min 36 s warm, all steps green,
  and `win64-native.py --wrapper wine` there on the packed artifact
  2015/2015 in 4 s.  Debian's Wine 10.0
  gives the same reference set as Wine 11.0: 2014 reference-ok of 2176,
  the same 162 exclusions; WIN-specific lists unchanged.
- Artifact: 2015 programs (353 MB unpacked, about 175 KB each: the static
  CRT) in a 4.4-4.9 MB `tar.xz`.
- `bench/win64-native.py --wrapper wine -j 4` on it: 2015/2015 in 3 s.
  With a wrong expected stdout, a wrong exit code, an allowed difference,
  a stale and an unknown allow entry: 2 fail, 1 allowed, 1 stale, 1
  warning, exit 1, the step summary table written.
- `vlink-pe-bad.py` (ASan build of this host): 530/530.
- `actionlint` 1.7.7 (without shellcheck) and `yaml.safe_load`: clean.

Not proven until the first GitHub Actions run: the workflow as a whole
(the `workflow_call` wiring, `actions/cache` with a glob path in the
container, the artifact hand-over, `compression-level: 0`); the runner
times on GitHub's 4-core x86-64 machine (expected: `wine` 10-12 min cold,
5-6 min warm; `native` 2-3 min with the download); **every program on real
Windows** (Server 2025): the expected results come from Wine, so a Wine
difference shows up as a `native` failure, to be fixed or added to
`ci/win64-native-allow.txt` with its reason; `chkstk`'s strict page check
on a real guard-page stack; `vlink-pe-bad.py` on the arm64 runner.

### Integration (2026-11-14): WIN1-WIN7R and WIN9 on current main

Merged into worktree-win64, in this order: main 2b7ef4d4 (the front end's
private and public renames, VIR's and the build's names, the linker split),
WIN7+WIN7R, WIN4+WIN5, then the var-for-COFF branch (main 9d322868: the
code generator's file and field renames of #241) and WIN9's CI branch.
WIN1-WIN3's code was renamed by learning main's renames from its diff (per
file, per directory, names that vanished there) and applying them to our
side and the base before a three-way merge; struct field renames came from
the compiler's errors with each struct's old and new definition aligned;
about 30 hunks were resolved by hand.  `X86(c)` is `CODEGEN_IS_X64(c)`,
`WIN64(c)` became `CODEGEN_IS_WIN64(c)`; main's asm frame operands (a 0 byte
the printer turns into rbp or r12) and the Windows frame's `X_ASMFRAME`
displacement are both read by the printer.  The COFF/PE constants are one
header, `include/platform/win/coff-format.h` (was `pe-definitions.h`; coff.c
dropped its own); the tail-merged string table of `elf.c`, `coff.c` and
`pe-write.c` is `common/string-table.h`; the object writers share
`elf_section_bytes_copy` and `ELF_EACH_FIXUP_IN_LAYOUT` (obj.h); vlink-PE's
arrays use `common/vector.h`, its hash `vhash_string`, coff.c's LEB128
`common/leb128.h` (quality.py: what is left is blessed with its reason in
the commit).

Measured after the merges (before = the Status entries above):

| harness | before | after |
|---|---|---|
| `vcc-win64.py --seh` / `--vir --seh` | 2012/2016 | 2014/2018 (the 4 known; main added 2 tests) |
| `--vasm-ref` | 2011/2012 | 2017/2018 (`debug_asm`, WIN6) |
| `--vlink-ref` | 2012/2012 | 2018/2018 image-identical |
| `--vccrt-ref` | 2014/2014 | 2018/2018, 0 foreign |
| `vcc-abi.py --target win64` | 28 pass, 3 xfail, 4 skip | the same |
| `vas-obj.py` | 77/77 | 77/77 |
| `vas-corpus.py --target x86_64-w64-mingw32` | 4494/4494 | 4514/4514 (more test files) |
| `vlink-pe.py --run` | 57/57 | 57/57 |
| `vccrt-test.py diff` | 70/70 | 70/70 (1194507 calls) |
| `var-cmp.py coff` | 230/230 | 230/230, 300/300 mutants |
| `vcc-diff.py --win64` cmp/text/textc/dm/deps/hdr, lines, dd | 990, 989, 988 | the same |
| `vcc-sema.py --win64` run/batch/layout | 990/990 | 990/990 (21.1 M claims) |
| `vcc-bflayout.py` | 0 rejected | 0 rejected (6467 claims each MinGW mode) |
| `vcc-driver.py` | 229/304 | 229/304 (the arm64 tables' version macros) |
| `vcc-same.py` vs main | identical but -E markers | 23943/23958 vs 9d322868: the 15 are -E markers naming the base tree's headers |
| `vcc-x86.py` | 2123/2128 | 2125/2130 (the 5 `inline` failures of main too) |
| `vcc-x86.py --suite vcc --objcmp` / `--vlink` | 323/323, 293/293 | 325/325, 295/295 |
| `vcc-diag.py`, `vlink-opts.py`, `quality.py --check` | 229, 66, 22/22 | 229, 66, 22/22 |
| unity and file-by-file builds | file-by-file vccd link failed | both build and link |

### WIN5 (2026-11-14): `vpp -c` for the target, the driver's link, own toolchain end to end

- **`-c`.** The code generator's object path selects COFF for the target
  (`codegen_object_target_set` in `codegen/unit-emit.c`; the driver's
  `.s`/`.S` path in `driver/job.c`).  Records are encoded directly, as for
  x86-64 ELF; the text items (`.def` records, sections and COMDAT, the
  `.seh_*` frame, jump tables in `.rdata`, data objects and strings) go to the
  TU's own assembler in place, which writes them through WIN4/WIN5's COFF
  directives and `coff_seh_*`.  Not done: emitting those through the
  `coff_*` calls without text; the ELF path has the same split.
- **The driver links.** `driver/link-mingw.c`: without `-c/-S/-E` a Windows
  command line compiles each source and links with vlink and the line clang's
  MinGW toolchain gives ld.lld (checked with `clang -###` for the default,
  `-shared`, `-static`, `-mwindows`, `-mconsole`, `-municode`, `-mthreads`,
  `-pthread`, `-s`, `-nostdlib`, `-nodefaultlibs`, `-nostartfiles`, `-L/-l`,
  `-Wl,`, `-Xlinker`), with `libvccrt.a` in place of compiler-rt and never
  libgcc or libunwind; `-v` prints it.  `libvccrt.a` is `VCC_VCCRT` or
  `vccrt/libvccrt.a` beside the program (`make -C src vccrt`).  vpp spawns
  the vlink beside it; vccd runs `ld_main` in process and takes Windows
  `-c/-S` as its own compile (the shim `vcc --target=x86_64-w64-mingw32 x.c
  -o x.exe` works against a private vccd; not in a harness yet).
- **libvccrt.a is built by vcc**: `runtime/build.sh` defaults to `vpp
  --target=x86_64-w64-mingw32` (stack-probe.S through vcc's assembler) and
  `var`, so the library needs no LLVM tool.
- **`vcc-win64.py --own`, now the default** (`--clang-as` the former one):
  vpp -c for the test and the test runtime, vcc's driver links, PATH an empty
  directory on vcc's side; `fail foreign` if the vlink line names
  compiler-rt, libgcc, libunwind, clang's `lib/` or an LLVM tool, or the
  link map has an input that is not ours, the sysroot's CRT files and archive
  members, or libvccrt.a's members.
- **Tests:** float128 and asm_flag_out and 17 of the 20 LP64-excluded
  `src/test/vcc` tests moved to `long long` (mechanically; their -S for x86-64
  and aarch64 Linux, with and without VIR, is unchanged but for format and
  assert strings); asm_clobber_all, asm_imm_knownbits and bit_cast stay
  excluded with their real reasons.

Measured: `--objcmp` 2105/2106 objects of `vpp -c` byte-identical to clang -c
and to vasm of `vpp -S` (so `llvm-readobj --unwind` is equal too; the one
left is `vcc/debug_asm`, -g, WIN6); `--obj --seh` 2014/2018 before the test
changes; `--own --seh` 2032/2034 and `--own --vir --seh` 2032/2034 after
them (left: `tcc/95_bitfields_ms`, MS layout of a packed record after `:0`,
and `vcc/dead_results`, `naked` not implemented); `--clang-as --seh` the
same 2032/2034; `vcc-win64.py --suite vcc` 312/313 (was 291/294);
`vccrt-test.py diff` 70/70 with the vcc-built library, `chkstk` ok,
`needs` 70/70; `vcc-x86.py --suite vcc` 324/324, `vcc-ctests.py --suite vcc`
331/331, `vcc-vir-x86.py` green.

CI locally (`CI_JOBS=4 ci/premerge.sh win64`, this host): every guarded
step that can run does and passes: win64-ref, win64-vlink (2034/2034
image-identical), win64-vccrt, **win64-own** and **win64-own-vir**
(2033/2033 + 1 xfail each), vlink-pe, vccrt-diff, vccrt-chkstk, var-coff,
**abi-win64**, **vas-obj-coff**, win64-pack; zlib-win64 and sqlite-win64 skip
(WIN8).  The export now holds vcc's own programs: `bench/win64-native.py
build/linux/win64-exes.tar.xz --wrapper wine` 2035/2035 (2033 vcc-built
test programs, the two vccrt-test programs).  Known failures are `XFAIL` in
vcc-win64.py with their TODO.md items (95_bitfields_ms at run, dead_results
at seh); a pass of one is an error.

What still comes from LLVM on vcc's side: no tool.  The resource headers
(`stddef.h`, `stdarg.h`, the x86 intrinsic headers) are clang's
(`VCC_RESDIR`, as for every target; the harness passes clang's include
directory with -isystem to both sides), and the sysroot is llvm-mingw's
packaging of mingw-w64.

### WIN6 (2026-11-14): debug information and the rest of the flags

What clang does for the target, checked here first: `-g` is DWARF 4 (cc1as
gets `-dwarf-version=4`, also for a `.s` without `-g`); `-gdwarf-2..5` are
honoured, and a `.file 0` in the text makes MC write version 5; no
`.debug_frame` and no `.cfi_*` (unwinding stays SEH); `.debug_*` are `"dr"`
sections referenced with `IMAGE_REL_AMD64_SECREL`; clang also adds a
CodeView `.debug$S` (S_OBJNAME, S_COMPILE3), which nothing reads for MinGW.

- **Assembler.** The line table in the version clang -c writes:
  `as_dwarf_version_set` (vasm `-gdwarf-N`, the driver's `-gdwarf-N`) else 4
  for COFF and 5 otherwise, `.file 0` making it 5.  Version 4 (and 2/3,
  which lack `max_ops_per_inst`): no file 0, MC's directory list without the
  compilation directory, names in `.debug_line`.  COFF: `.debug_line` and
  `.debug_line_str` are created after `.pdata` and `.llvm_addrsig` when the
  text did not declare them (`coff_section_late`, as MC's streamer), version
  5's string offsets are SECREL.  With `-g` (vasm `-g`, `vcc -g -c x.s`) the
  table knows the compilation directory as cc1as's does, and a source
  without `.file N` gets MC's debug information for assembler sources
  (GenDwarfForAssembly): a row per instruction (the outermost macro/`.rept`
  line, cpp markers, one-byte x86 prefixes as their own instruction), a
  DW_TAG_label per label, `.debug_info`/`.debug_abbrev`/`.debug_aranges` and
  `.debug_ranges`/`.debug_rnglists` over the sections with instructions,
  MD5 of the source for version 5, the main file named without the
  compilation directory; vasm `-dwarf-debug-producer` (vcc: "vcc").  COFF
  writer fix found here: a relocation against a temporary that is a
  section's COMDAT symbol names that symbol (MC; vcc's `-fdata-sections`
  compound literals).
- **Compiler `-g`.** The same content as for Linux (one compile unit, line
  rows per statement) in DWARF 4 for Windows: `.file 1 "dir" "main"` and the
  other files one more (clang's naming: relative to the compilation
  directory), a version 4 unit header (below 4: the line table offset as
  data4, the high pc an address); `-gdwarf-N` on every target
  (`cc_options.dbg_version`; Linux's default output is unchanged).  COFF:
  when a function is outside `.text` (`-ffunction-sections`, a section
  attribute) the unit gets DW_AT_ranges, one range per function
  (`.debug_ranges`, version 5 `.debug_rnglists`), so symbolizers find it.
  `-g` does not change `.text`; `-g1/-g2/-g3/-gline-tables-only` give the
  same tables, `-g0` none, as on Linux.
- **Linker.** vlink-PE already carried `.debug_*` as lld does (long names
  in the string table, discardable, SECREL/SECTION applied; `-s`,
  `--strip-all`, `--strip-debug`): now tested with clang's and vcc's `-g`
  objects.
- **Flags.** `-fno-asynchronous-unwind-tables` without `-funwind-tables`
  drops the `.seh_*` directives (no `.pdata`/`.xdata`) and the nop before an
  epilogue after a call, as clang; the frame and `___chkstk_ms` stay.
  `-fstack-protector*`: `__stack_chk_guard` through
  `.refptr.__stack_chk_guard` (was direct) and the driver links
  `-lssp_nonshared -lssp` from the sysroot when any such flag is given
  (clang's MinGW toolchain; never compiler-rt).  `-ffunction-sections`/
  `-fdata-sections` give clang's `.text$NAME` `one_only` sections (clang
  adds `,unique,N`); `-fPIC/-fpic/-fPIE/-fpie/-fno-pic` change nothing in
  either compiler's `-S`.  `-municode/-mwindows/-mconsole/-mthreads`: the
  link line equals clang's.
- **Tests.** `vas-obj.py` assembles every case with `-g` too, `win64-dwarf4.s`
  and `win64-dwarf5.s`; `vas-corpus.py -g [--as-g]`; `vcc-dwarf.py --target
  x86_64-w64-mingw32|x86_64-linux-gnu --gflag= --cflags=` (COFF `.text`
  sections, and vasm of `-S` = `-c`); `VCC_CFLAGS=-g vcc-win64.py`
  (`llvm-dwarfdump --verify` of every linked image, llvm-symbolizer at every
  function vcc defined gives it and a line); `vlink-pe.py` 15 new cases;
  `vcc-driver.py` 27 link rows and 31 code-generation rows; `src/test/win64`
  `stack_protector`, `municode`, `subsystem` (`-mwindows`: the optional
  header says GUI, gdi32 linked), `mthreads` (a thread key destructor runs),
  `no_unwind_tables` (RtlLookupFunctionEntry finds none); `vcc-win64.py`
  passes a test's driver link flags to both links and skips the SEH checker
  where unwind tables are off.

Debugger under Wine: winedbg 11 reads the DWARF of vcc's image (module
"Dwarf-4") and shows file:line for the CRT's frames, but names vcc's frames
only `crash+0x1491`: it names functions from DW_TAG_subprogram, which vcc
writes on no target (TODO.md DWARF).  clang+lld's image gets names
(`leaf+0x17(x=0x3)`) but no file:line for its own frames either.  So the
check is llvm-symbolizer on the image: for the same program, vcc's and
clang's builds give the same function and file:line (`crash.c:8` at the
fault, the callers' lines).

Measured (this host, `-j 3`/`-j 4`, Wine 11):

| harness | before | after |
|---|---|---|
| `vcc-win64.py --vasm-ref` | 2017/2018 (`debug_asm`) | 2039/2039 |
| `vcc-win64.py --objcmp` | 2105/2106 | 2120/2120 |
| `vcc-win64.py --seh`, `--vir --seh` | 2032/2032 + 2 xfail | 2037/2037 + 2 xfail (5 new tests) |
| `VCC_CFLAGS=-g vcc-win64.py --seh` | - | 2036/2037 + 2 xfail; the one is the oracle: clang -g -O2 of `tcc/101_cleanup` for MinGW runs over 60 s (deterministic, also alone) |
| `VCC_CFLAGS=-g vcc-win64.py --vlink-ref` | - | runs-ok and image-identical 2038/2038 (the same oracle timeout) |
| `vas-corpus.py --target x86_64-w64-mingw32 -g` | - | 4530/4530 (also `-gdwarf-5`); `--as-g` 4540/4540 |
| `vas-obj.py` | 77/77 | 81/81, `-g` 70/70 + 1 xfail (`symver.s`) |
| `vcc-dwarf.py --target x86_64-w64-mingw32` | - | -O0 1847/1847, -O2 1846/1846; `-gdwarf-2/3/5`, `-gline-tables-only`, `-ffunction-sections -fdata-sections` all pass |
| `vcc-dwarf.py` (aarch64), `--target x86_64-linux-gnu` | | 1594/1594, 1596/1596 (also `-gdwarf-3/4`) |
| `vlink-pe.py --run` | 57/57 | 72/72 |
| `vcc-driver.py` | 229/304 | 287/362 (the 75 are the arm64 tables' version macros, as before) |
| `vcc-same.py` vs the starting commit | | 23943/23958: the 15 `-E` markers naming the base tree's headers, as before |
| `vcc-x86.py --suite vcc --objcmp` | 325/325 | 327/327 |
| `quality.py --check` | 22/22 | 22/22 |

Unity and file-by-file builds compile and link.  Not done (TODO.md):
`-g` on `.S` through the driver (clang names the temporary file and follows
the preprocessor's markers into included files), `.symver` with `-g`, the
CodeView `.debug$S`, DW_TAG_subprogram and the rest of DWARF (as on Linux),
Linux's unit ranges for functions outside `.text`, a stack-protector failure
test.

### WIN8, WIN10: planned
