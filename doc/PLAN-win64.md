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

### WIN7 (2026-11-13): vlink writes PE, byte-identical to lld for clang's objects

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

### WIN7R (2026-11-14): vcc's runtime library; programs link without compiler-rt

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

### WIN1-WIN6, WIN8-WIN10 (2026-11-13): planned

Nothing implemented.  The reference chain was checked by hand (clang 23 ->
COFF -> `ld.lld -m i386pep` -> Wine 11 exit code); the sysroot is not
installed.
