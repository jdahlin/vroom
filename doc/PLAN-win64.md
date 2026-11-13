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

### WIN0-WIN10 (2026-11-13): planned

Nothing implemented.  The reference chain was checked by hand (clang 23 ->
COFF -> `ld.lld -m i386pep` -> Wine 11 exit code); the sysroot is not
installed.
