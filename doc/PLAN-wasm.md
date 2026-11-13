# WebAssembly as a target: new arch and new platform in vcc

**Goal:** `vcc --target=wasm64-unknown-emscripten -c x.c` produces a wasm object
that `wasm-ld` links and that runs with the same result as clang's. `-S` gives
LLVM's text syntax for wasm. The final goal is that vcc compiles itself to wasm
and that the explorer page (`contrib/wasm/`) runs that version instead of the
emscripten build.

**Why:** Today emscripten (clang) compiles vcc to wasm (`contrib/wasm/build.sh`).
With our own wasm backend clang is not needed for that, and vcc gets a second
target next to arm64. It forces the split between generic and target-specific
code that x86-64 also needs (TODO.md: "Target-OS code out of the generic
files").

**Rules:** same as phase 2. Clang is the reference
(`--target=wasm64-unknown-emscripten` and `wasm32-wasip1`), nothing from
clang/LLVM at run time. `wasm-ld` and emscripten's libc are used for linking and
testing until step W7. No step may change arm64 output: every commit is verified
against the 1 326 outputs used at the directory move (PLAN-fas2.md, "Status
directory structure").

## Starting point (2026-11-09)

- The emitter generates straight from the AST without an IR. The generic part
  `codegen/` is 2 462 lines (queue, threads, data, prepass, types), the arm64 part
  `arch/a64/codegen/` is 10 292 lines. The arm64 part also contains things that are
  not arm64-specific: constant folding during generation, the reachability flag,
  inlining of `__always_inline`, the `__builtin_constant_p` rules and the
  `need_fold` regeneration.
- The type sizes are a static LP64 table (`frontend/c/type.c:6`), `__builtin_va_list` is
  the AAPCS64 struct or Apple's `char *` (`frontend/c/tu.c:94`).
- Predefines exist for two targets (`ppdata-predef.inc`: aarch64-linux, Darwin).
- The object writers are ELF (`platform/linux/elf.c`) and Mach-O
  (`platform/darwin/macho.c`) on top of the common object model `include/obj/obj.h`
  (sections, frags, symbols, fixups).
- Emscripten 7.0.0 is available (Homebrew), with a wasm64 sysroot (libc,
  compiler-rt). Node 26 runs memory64 without flags. Clang 23 writes wasm `.s`
  and objects for both widths.

## The data model: wasm64 first

| | aarch64-linux | wasm64-emscripten | wasm32-wasip1 |
|---|---|---|---|
| `long`, pointer, `size_t` | 8 | 8 | **4** |
| `long double` | binary128 | binary128 | binary128 |
| `char` | unsigned | **signed** | **signed** |
| `va_list` | AAPCS64 struct | `void *` | `void *` |
| largest alignment | 16 | 16 | 16 |

wasm64 is LP64 like arm64, so the type table can stay. What differs is `char`
(as on Darwin) and `va_list` (`void *`, Darwin has `char *`). The explorer is
also memory64. wasm32 (ILP32) needs a type table per target and 32-bit pointers
throughout the emitter, so it comes last (W6).

## Architecture

| Part | File(s) | Contents |
|---|---|---|
| Target selection | `driver/ppdriver.c`, `frontend/c/`, `codegen/` | A `target` value (cpu, os, pointer width) instead of the `darwin` flag; `--target=wasm64-*`/`wasm32-*` |
| Predefines, `__has_*` | `frontend/pp/ppdata-predef.inc` via `gen-ppdata.sh` | One more table per target, taken from clang once |
| Layout | `frontend/c/type.c`, `frontend/c/layout.c` | `char` signed, `va_list` as `char *`; later a table per target |
| Generic code generation | `codegen/` | What is lifted out of `arch/a64/codegen/`: folding, reachability, inlining, `__builtin_constant_p` |
| Code generator | `arch/wasm/codegen/` | AST → wasm instructions: stack machine, locals, structured control flow, shadow stack |
| Text output | `arch/wasm/codegen/print.c` | `-S` in LLVM's syntax (`.functype`, `.local`, `local.get`, `block`/`loop`/`end_block`) |
| Encoder | `arch/wasm/as/enc.c` | Opcodes, LEB128 (5/10 bytes with padding where a relocation goes) |
| Assembler | `arch/wasm/as/parse.c` | wasm `.s` as `vas` reads it: `-S` → `-c` round trip, and `.s` tests against llvm-mc |
| Fixups | `arch/wasm/obj/fixup.c`, `reloc.h` | `R_WASM_FUNCTION_INDEX_LEB`, `R_WASM_MEMORY_ADDR_LEB64`/`SLEB64`/`I64`, `R_WASM_TABLE_INDEX_*`, `R_WASM_GLOBAL_INDEX_LEB`, `R_WASM_TYPE_INDEX_LEB` |
| Object writer | `platform/wasm/wasm.c` | Type, import, function, code and data sections; `linking` (symbol table, segments, init functions) and `reloc.CODE`/`reloc.DATA`. The object format is the same for emscripten and WASI; the differences between the OSes (libc, startup code, sjlj) are in the driver and the ABI code |

### The code generator

That the emitter works from the AST suits wasm well, since C's structured
statements become wasm's structured control flow directly:

- `if`/`else` → `block`/`br_if` or `if`/`else`/`end`; `while`/`for`/`do` → `block` + `loop`;
  `break`/`continue` → `br` to the right depth.
- `switch` → `br_table` when the cases are dense (like arm64's jump table),
  otherwise a comparison chain. A jump into a loop (Duff's device) is handled as
  `goto`.
- `goto`: in the common case (forward out of nesting, `goto out`/`goto err`) a
  `block` around the jump. Otherwise a stackifier for the function: basic
  blocks, dominator tree and the same algorithm as LLVM's
  `WebAssemblyCFGStackify`. Irreducible control flow becomes a `loop` with a
  `br_table` dispatcher on a label variable (LLVM's
  `FixIrreducibleControlFlow`). Indirect `goto` is not supported, and clang
  rejects it too.
- Values live on the operand stack within an expression. Variables and
  temporaries become wasm locals (unlimited number: no register allocation,
  `opt.c` is not needed). Local variables whose address is taken, arrays and
  structs are placed on a shadow stack in linear memory via the global
  `__stack_pointer`, as clang does.
- Calls: direct `call`, indirect `call_indirect` via the table with a type
  index. ABI as clang's `WebAssemblyABIInfo`: scalars in parameters; a struct
  with a single scalar field is passed as the scalar, other aggregates as a
  pointer to a copy; aggregates are returned through a hidden sret pointer;
  varargs in a buffer on the caller's shadow stack, the caller passes the
  pointer.
- `__int128` and binary128 `long double` via compiler-rt calls (`__multi3`,
  `__addtf3`, …), as clang. Atomics as plain loads and stores without shared
  memory, with `i64.atomic.*` under `-matomics`/`-pthread`.
- `setjmp`/`longjmp` as emscripten's wasm-EH variant (`__wasm_setjmp`,
  `__wasm_setjmp_test`, `__wasm_longjmp`, `try_table`). vcc needs it itself: pp
  and cc jump out with `longjmp` on errors.
- Inline asm: wasm `.s` fragments via `arch/wasm/as/parse.c`. It is rare in C
  for wasm and can come late.

## Steps

| # | Deliverable | Criterion |
|---|----------|-----------|
| W0 | **Target selection and front end.** `target` instead of `darwin`; `--target=wasm64-unknown-emscripten`; predefines from clang; `char` signed and `va_list` as `void *` for wasm | `vpp -dM -E` = `clang -dM -E` for the target; `vcc-sema.py layout` with wasm64 as target (layouts, constants, types checked by clang); arm64 output byte-identical |
| W1 | **Arch hooks in `codegen/`.** `do_item`, the head and tail in `tu.c` and the data directives in `data.c` choose per target; the wasm generator folds by itself with `cc_eval` (replanned: folding and inlining are not lifted out of `arch/a64/codegen/` until wasm needs them) | arm64 output byte-identical (`vcc-same.py`), `vbench.py check` |
| W2 | **First wasm output.** `arch/wasm/codegen/` for integer arithmetic, locals, parameters, `return`, direct calls, `if`/loops/`break`/`continue`; `-S`. The explorer gets the target "wasm64" | Output is assembled by `clang --target=wasm64-unknown-emscripten -c`, linked by emcc and run in node with the same result as clang's; new harness `bench/vcc-wasm.py` |
| W3 | **Memory and data.** Global variables, strings, initializers, pointer arithmetic, shadow stack for address-taken locals, arrays and structs, `memcpy`/`memset` for aggregates | `vcc-ctests.py --suite vcc` with `--target wasm64` in node |
| W4 | **All of C.** Structs as values (ABI above), varargs, `switch` with `br_table`, `goto` with stackifier, floating point, bit-fields, VLA/`alloca`, `__int128` and `long double` via compiler-rt, atomics, `setjmp`/`longjmp` | gcc-torture and tcc-tests2 in node via emcc: same passes as clang; `bench/vcc-abi.py --target wasm64` (clang/vcc × caller/callee) |
| W5 | **Object files.** `arch/wasm/as/enc.c`, `arch/wasm/obj/fixup.c`, `platform/wasm/wasm.c`; `-c` directly without text; `vas` reads wasm `.s` | Linked by `wasm-ld` and run: everything from W4 with `-c`; encoder against `llvm-mc` (`bench/vas-enc.py`); `wasm-objdump -x` compared against clang's object for the same `.s` |
| W6 | **vcc compiles itself to wasm64.** The NEON lexer as scalar `VLEX_SCALAR`, CRC32C in software, `src/port/wasm/mman.c` as today; emcc only links | `-S` output from the self-built `vcc.wasm` byte-identical with the emscripten build's on the test files; the explorer uses it |
| W7 | **wasm32 (ILP32).** Type table per target, pointer width 4 through the emitter and the object writer, wasi-libc | Same suites as W4 with `--target=wasm32-wasip1` |
| W8 | **Own wasm linker** in `ld/` (optional; the no-LLVM rule) | A program linked by `vlink` runs like the one linked by `wasm-ld`; times against `wasm-ld` |

The order makes every step visible: W2 gives output in the explorer and W5
runnable objects. W1 can partly run in parallel with W0.

## Validation

- **Reference:** clang 23 with the same target. `-S` is not compared line by
  line, since instruction selection differs. What is compared is behaviour
  (running), ABI (calls between clang and vcc objects) and layout
  (`vcc-sema.py layout`).
- **Running:** emcc links against emscripten's wasm64 libc and node runs. Same
  setup as `bench/vcc-ctests.py` with qemu for arm64. Cache as there: an object
  byte-identical to one that has already passed is not run again.
- **arm64 unchanged:** every commit in W0, W1 and W7 is verified against the
  1 326 outputs and `vbench.py gate`.

## Risks and open questions

- **W1 is the hard step.** Folding and inlining are intertwined with arm64's
  register handling (`B_REG`, `fnctx.retreg`). Lifting them out without changing
  a single byte of arm64 output requires small steps. The alternative is that
  the wasm generator gets its own copy of the folding. It is faster but gives
  two places to keep in step with clang's rules, so it is chosen only if W1
  drags on.
- **`goto`-heavy code:** the kernel and vcc itself mostly have `goto out`
  patterns that are handled with a surrounding `block`. The stackifier is needed
  anyway for gcc-torture.
- **setjmp via wasm-EH** must match emscripten's runtime exactly (symbols,
  tags). Tested early in W4 with a small program.
- **Performance:** wasm output should cost about the same as arm64 output per
  function (no extra passes beyond the stackifier, which only runs for functions
  with `goto`). Measured with `vbench.py` when W5 is done.

## Status

### W9 (2026-11-11): replace Emscripten, first standalone test path done

The goal is to build and run wasm without emcc/emsdk. Emscripten is today
test/runtime infrastructure, not a necessary part of vcc's wasm code generator.
Vcc already has an assembler, an object writer and `ld/wasmld.c`; W8 replaces
the linker, but the old test path's emcc still chooses libraries and generates
JS. `wasm*-unknown-emscripten` names the current target ABI, not that the
program must be built or started with Emscripten.

| Step | Deliverable | Status and verification |
| --- | --- | --- |
| W9.1 | Libc-free test path: vpp -S for code requirements, vpp -c for our own object, vlink for the module, a small Node launcher | Done for OPT1's first rule: `bench/vcc-opt-rules.py --target wasm32-unknown-emscripten --run` and the same for wasm64. O0–O3 run the same C regression. No emcc, LLVM assembler, wasm-ld or SDK is used in this wasm path. Verified with the system's `/usr/bin/node` 24.22.2 and then 25.9.0 without an SDK path. Earlier Node 22 lacked table64 and could not run this wasm64 module. |
| W9.2 | Own test runtime for an explicit selection of the vendored C suites | Planned. Reuse portable C from `test/rt/rt.c` or reviewed libc/compiler-rt sources; build with vcc. Separate the arm64 startup, UART/exit and FP asm. Add small JS imports for output/exit and a heap with memory.grow. Keep the same declarations/test contract and also verify the reference compiler's programs against the runtime. |
| W9.3 | Complete library/builtin support for the test corpus, including exception-based setjmp | Planned. Inventory the actual undefined symbols; implement or vcc-build compatible __int128/binary128/complex helpers and libc/libm sources. __wasm_setjmp/__wasm_longjmp and the __c_longjmp tag need their own ABI/EH tests. Missing functions give an explicit unsupported, not falsely passed or silently filtered tests. |
| W9.4 | Standalone explorer and self-build | Planned. Replace em-config/sysroot selection, libc header/runtime delivery, startup/argv/stdout/exit and the generated VccModule/FS/callMain/ExitStatus with our own adapter. Pack the files in the VFS; keep the worker API or migrate worker/selfrun in the same step. Test files, memory growth, diagnostics and the self-build's stage2=stage3. |
| W9.5 | GHA's mandatory wasm gate uses our own chain | In progress with the W9.1 regressions in `ci/premerge.sh`; the old full suite still needs emcc. Remove the SDK installation only when the same decided corpus and ABI tests run with W9.2–W9.4. Emscripten can then exist as an optional comparison reference, never as a mandatory build/runtime component. |

A newly written full libc is not required. Selected existing sources can be
built by vcc, with documented origin/licence and a tested wasm32/64 ABI. The
shared optimizer does not wait for this runtime work: W9.1 already gives its own
chain for the positive code requirements and the side-effect/boundary-value
tests. Installing on the host and activating emsdk is not a product deliverable.

Code inventory: `bench/vcc-wasm.py` uses emcc for `rt/wasm.c` and the final
link/JS; `rt/wasm.c` is only the assert adapter against an external libc.
`contrib/wasm/build.sh` uses Emscripten's sysroot and JS file/process glue.
`explorer/worker.js` and `selfrun.mjs` use VccModule, FS and callMain.
`src/port/wasm/mman.c` already owns the reservation policy but needs sbrk, pread,
errno and standard library functions. The wasm generator emits compiler-rt calls
and EH frames but does not yet deliver their runtime.

### Shared optimizations (2026-11-11): planned for wasm32 and wasm64

`PLAN-fas2.md`, **Shared optimizer without SSA**, OPT0–OPT8 owns the new
optimization plan. Wasm is a mandatory consumer of the shared C rules and
strategies together with x86 and arm64. Connections are in `wexpr.c`
(`wbinary`/`warith`, `wknown`, `wlocal`, `wvlanes`), `wstmt.c` (`wswitch`,
reachability) and `wcall.c:wcall`. Keep the operand stack, wasm locals,
structured result blocks and `br_table`; no physical register passes are
required. The shared inlining policy must be completed with real wasm bindings
and return/goto/cleanup remapping. Native-only tools or an x86-green audit do
not count as wasm verification. Run both pointer widths, with a scalar fallback
without the SIMD/tail-call features. Pure extractions must give the same
output; new rules get intended, tested improvements on wasm too. No rule is
implemented in this step; existing W statuses are unchanged.

Before the plan: the wasm *build* of vcc via emscripten (`contrib/wasm/`, commit
8e9f9da) and the explorer page.

### Integration (2026-11-09): branch `claude/wasm`
Everything done in one branch: W5 and W8 (`claude/wasm-w5-objects`,
`claude/wasm-w8-linker`), W7's front end (`claude/wasm-w7-wasm32`) and the rest
of W4 (`claude/wasm-compiler-explorer-5ed0bf`). Not included: the cleanup
attribute (`claude/wasm-cleanup-wip`, an unexplained run-time failure).
Conflicts only in `emitimpl.h` (`WASM()` now covers wasm32, plus `inline_only`)
and `bench/vcc-wasm.py` (`rt/wasm.c` is also linked with `--vlink`).
- `vcc-wasm.py -j 6`: 1 717/1 742 (gcc 1 588/1 605, tcc 97/104, vcc 32/33).
- arm64: `vcc-same.py` 13 165/13 181; the 16 deviations are intended
  (`ctor_priority.c`: `.init_array.N` as clang, W5; `36_array_initialisers.c`
  and `composite_array.c`: the size from an earlier declaration, a fix in the
  front end). `vcc-ctests` 1 745/1 745, `vcc-sema batch` 878/879, the file by
  file build without warnings.

### W4 (2026-11-09): almost done
Done since W2: `goto` (dispatch loops with state locals and `br_table`, jumps
into nested statements), label addresses and computed `goto`, inline asm with
an empty template, vectors (lane by lane, sret), `__int128`/`long double` via
compiler-rt (16-byte values as addresses of slots), VLA (below
`__stack_pointer`, released at block end, `break`/`continue` and backward
jumps), `alloca`, atomics as plain loads/stores (as clang without the atomics
feature), overflow builtins, dead code after jumps and known conditions, local
copies of inline-only functions (the emitter does not inline), function
addresses in data (`.int32`/`.int64`, table index), varargs slots of at least 4
bytes.
Remaining (errors, not silent errors):
- `emit` 13: the cleanup attribute (2, WIP branch), `asm goto` (2), `goto` to a
  label that is not in a compound statement (2), `case` in a nested statement
  (1), `switch` on 128 bits (1), overflow builtins on `__int128` (1),
  `_Atomic` objects (1), bit-fields over 8 bytes (1), `atomic_signal_fence` (1,
  front end), destructors (1, W5).
- `link` 6: the tests assume that optimization removes the `link_error` calls.
- `run` 6: optimization-dependent (`bcp-1`, `builtin-constant`,
  `frame-address`, `20030125-1`), `complex-6` (`long double _Complex`) and
  `24_math_library` (the library's signatures for implicitly declared).
- `bench/vcc-abi.py --target wasm64|wasm32 [--vlink]` (2026-11-10): the four
  combinations clang/vcc × caller/callee, `vpp -c`, linked by emcc and run in
  node.  New test `test/abi/aggregates` (records 0..48 bytes, one element,
  unions, bit-fields, `__int128`, `long double`, `_Complex`, varargs with
  records, callbacks in both directions) and `aligned_args`.  It found: a
  vector with one lane is returned as a scalar (as LLVM), not through a
  pointer.  wasm64 9/9, wasm32 8/8 (`bitfield_layout` requires LP64), `--vlink`
  9/9.  The same tests found four arm64 bugs (fixed): Linux rounds to an even
  register pair after clang's "unadjusted" alignment (not the record's own
  `aligned()`), Apple never rounds; Apple's `long double` (8 bytes) in an HFA;
  Apple's HFA on the stack with the element's alignment and size.

Later the same day: `setjmp`/`longjmp` through wasm-EH (`-mllvm -wasm-enable-sjlj`,
like emscripten's `-sSUPPORT_LONGJMP=wasm`: `__wasm_setjmp`/`__wasm_longjmp`,
`try`/`catch __c_longjmp`, tags in the assembler, the object and vlink;
`test/vcc/setjmp_sites.c`), destructors, `asm goto` with an empty template,
`switch` on `__int128`, wide bit-fields, `_Atomic` objects, math functions as
instructions. `vcc-wasm.py -j 6`: 1 739/1 746 in all modes (`-S`, `--objcmp`
byte for byte, `--obj --vlink`); what remains are the 6 `link_error` tests
(require optimization) and `bcp-1`.

### W6 (2026-11-10): done, explorer not switched
vcc compiles itself to wasm64: `VCC=build/src/vpp OUT=../build/wasm-self
sh contrib/wasm/build.sh` (from `src/`) makes every object with vcc's wasm
generator, assembler and object writer against emscripten's libc headers; emcc
only links (`-sSUPPORT_LONGJMP=wasm`, vcc's setjmp is wasm-EH).  What was needed:
- vcc's `<arm_neon.h>`/`<arm_acle.h>` have C versions of the inline-asm
  intrinsics for targets other than arm64 (lane by lane; CRC32/CRC32C one
  nibble at a time), tested against clang's NEON on 20 000 random inputs;
- `_Thread_local` as plain objects on wasm, as LLVM does without shared memory
  (`test/vcc/thread_local.c`);
- `__builtin_bitreverse8..64`, `__builtin_isinf`/`isfinite` in the wasm
  generator (`test/vcc/bitreverse.c`, `fp_class_builtins.c`).
`bench/vcc-self.py -j 6`: every test in the suites, preprocessed by the native
vpp for aarch64 and wasm64, is compiled `-O2 -S` by both the emcc build and the
self-built `vcc.wasm` (`contrib/wasm/selfrun.mjs`, node): 3 741/3 741 outputs
byte-identical (3 428 compiled, the rest the same diagnostics), 37 s.
`vcc.wasm` 1,26 MB against emcc's 1,61 MB, but ~7x slower (9,0 s against 1,2 s
on the 41 MB `many_probes`): no optimization, intrinsics lane by lane.
The explorer still uses the emcc build; switching is a decision (speed) and a
new publication of the page.

### W7 (2026-11-09): done
`--target=wasm32-unknown-emscripten`: predefines as clang, ILP32 in the front
end, and now all the way through: pointer width per target in the generator
(`WPT(c)`, `wrep()`: `i32` addressing, `__stack_pointer` and table indexes as
i32, `.int32` addresses in data, compiler-rt signatures with `P`), the sjlj
payload `{env, val}` at 4+4 bytes, the assembler with `R_MEMORY_ADDR_LEB` (5
bytes) for symbols in memarg, the object writer with i32 limits for memory and
table and `i32.const` offset, and `vlink` reading the width from the objects'
memory import (error on mixing).  Two fixes in the front end that wasm32 found
(ILP32 makes `1ul << 40` an over-shift): constant shifts fold as clang's C
(count ≥ width becomes width−1, a negative count shifts the other way,
`test/vcc/const_shift.c`), and `-Wno-[error=]int-conversion`,
`incompatible-pointer-types` and `incompatible-function-pointer-types` turn the
error into a warning as clang.
- `vcc-wasm.py -j 6 --wasm32`: 1 739/1 746, `--objcmp` byte for byte with
  clang's objects, `--obj --vlink` (`build/src/wasmtests32`); the same 7 remain
  as for wasm64.

### W8 (2026-11-09): own linker, in branch `claude/wasm-w8-linker` (on W5)
- `ld/wasmld.c`: `vlink` links wasm when called as `wasm-ld` or with
  `-mwasm64`. Reads relocatable wasm objects (clang's and vcc's), archives
  (SysV symbol table, members fetched lazily by strong references, `--export`
  and `--export-if-defined` as in wasm-ld) and emscripten's stub libraries
  (`#STUB`: symbols that JS provides, with dependencies that become roots).
  Symbol resolution (strong before weak, duplicates are errors), GC from the
  roots (exports, `NO_STRIP`, constructors), `__wasm_call_ctors` (init
  functions by priority), undefined weak functions as stubs that trap and null
  in the table, synthetic `__stack_pointer`, `__heap_base`, `__data_end`,
  `__stack_low/high`, `__start_/__stop_X`; memory layout with `--stack-first`,
  `-z stack-size`, `--initial-heap`, `--no-growable-memory`, `--table-base`;
  segments merged by name (`.rodata`, `.data`, `.bss` omitted), data symbols
  exported as immutable globals, relocations written into the padded fields,
  `target_features` as the union plus `--extra-features`.
- emcc uses it through `EM_LLVM_ROOT` = a directory with emscripten's LLVM
  tools where `wasm-ld` points to `vlink` (`EM_FROZEN_CACHE=1`, the cache is
  only read). `bench/vcc-wasm.py --vlink` links vcc's side this way; `--obj
  --vlink` is the whole vcc chain (vpp -c, vlink) with only emscripten's libc
  and JS left.
- Validation: `vcc-wasm.py -j 6 --vlink` and `--obj --vlink`: the same 1 590
  passes as with wasm-ld (two bugs found and fixed on the way:
  `--export-if-defined` must fetch archive members, otherwise `fflush` is
  missing and output without a final newline disappears; an undefined weak
  function must have table slot 0). Time for emcc's link of t1.c (libc,
  dlmalloc, stubs): `vlink` 5,4 ms against `wasm-ld` 29,2 ms (best of 10,
  loaded machine).
- Missing (gives an error, not a silent error): COMDAT, TLS segments in
  multithreaded mode, `--entry`/`_start`, the `name` section (debug names),
  `--growable-table` without max, shared memories, PIC/`-shared`. Output is not
  byte for byte as wasm-ld's (export order, segment division); that is not
  required.

### W5 (2026-11-09): object files, in branch `claude/wasm-w5-objects`
- `arch/wasm/as/as.c` (+ `was.h`, the interface `wasmas.h`): LLVM's wasm `.s`
  (what the generator writes: `.functype`, `.globaltype`, `.tabletype`,
  `.local`, sections, `.globl`/`.weak`/`.hidden`/`.type`/`.size`/`.p2align`,
  `x = y`, `.set`, data directives and strings; the whole MVP instruction set,
  sign-ext, `trunc_sat`, `memory.copy`/`fill`) into a model; instructions are
  encoded directly, symbol operands leave fixed LEB fields (5/10 bytes) and
  relocations. `platform/wasm/wasm.c` lays out the model as LLVM's
  WasmObjectWriter: symbols, functions, imports and types in the order the
  assembler registered the symbols (`call_indirect` signatures where the
  instruction is), padded section and subsection sizes, ELEM segments and table
  import when a function address is taken, `linking` (symbol table, segments)
  and `reloc.CODE`/`reloc.DATA`.
- `-c` for wasm64: `emit_tu` takes the text (as `VCC_OBJ_TEXT`) through
  `wasm_assemble`; the driver writes the bytes (`cc_opts.wasm_obj`). Still
  through text, not yet directly from the generator (the plan's "without text"
  remains, a performance question). `vasm --target=wasm64-…` reads wasm `.s`.
- Validation: `bench/vcc-wasm.py --objcmp` compares `vpp -c`'s object byte for
  byte with clang's object for the same `.s`: **all identical** (0 deviations
  on the suites, 1 590 tests passed linked by emcc and run in node on the
  branch's base 1856415, the same as the `-S` path). `--obj` runs the suites
  with vcc's objects.
- arm64 unchanged: `vcc-same.py` 13 167/13 167 against the build before wasm,
  `vcc-ctests` 1 743/1 743, the file by file build without warnings; the
  emscripten build (`contrib/wasm/build.sh`) includes the new files.
- Addition (in `claude/wasm-w8-linker`): constructors. The generator silently
  dropped `__attribute__((constructor))` for wasm; now `.init_array[.N]`, which
  the assembler turns into the object's init functions (linking subsection 6,
  in the sections' order as LLVM); the priority `constructor(N)` is saved in
  `attrs.prio` (the front end dropped it, also for arm64, which now writes
  `.init_array.N` as clang). The assembler also takes custom sections
  (`.custom_section.X`; `producers` and `target_features` last),
  `.no_dead_strip` (the table import clang -O0 forces), the segment flag `"S"`
  and symbols as memory offset (`i32.store x`, MEMORY_ADDR_LEB64): clang's own
  `-O0` `.s` now gives the same object as clang. Test `test/vcc/ctor_priority.c`
  (arm64 and wasm); destructors give an error for wasm (clang makes them
  `__cxa_atexit` calls).
  Names outside ASCII are written in quotes (`"привет"`, the section
  `".data.привет"`) as clang -S; LLVM's wasm assembler does not read them back
  (clang's own `.s` fails too), so `vcc-wasm.py` then builds vcc's side with
  `vpp -c` (tcc/83_utf8_in_identifiers passed).
- Remaining in W5: the generator directly to the model (without text),
  destructors, TLS, `vas` in vccd.

### W1, W2 (2026-11-09): done
- W1 replanned: instead of lifting folding and inlining out of
  `arch/a64/codegen/` (risky, see Risks) `codegen/` chooses the generator per target:
  `do_item` (functions to `wasm_emit_function`), the head (`wasm_header`:
  `.globaltype __stack_pointer`, `.tabletype`, one `.functype` per referenced
  function, sorted and unique, from each thread's list), the tail (no
  `.note.GNU-stack`) and `data.c` (one section per object, `.long`/`.quad`,
  strings in `.rodata..L.str.N`). `ecx.w` holds the wasm state (one pool).
- W2: `arch/wasm/codegen/` (`wimpl.h`, `wasm.c` functions/ABI/head, `wexpr.c`
  expressions/lvalues, `wcall.c` calls/varargs/builtins, `wstmt.c`
  statements/initializers).
  One value per C expression on the operand stack; scalar local variables
  without a taken address (the prepass's `lnoreg`) in wasm locals, the rest in a
  frame on the shadow stack (`__stack_pointer`, restored from a saved local);
  `return` as `br` out of the function's outermost block with the value in a
  local. ABI as clang: a struct with one element as the scalar, empty ones
  ignored, others as a pointer to a copy, sret; `__int128`/`long double` as two
  i64; varargs in a buffer on the caller's frame (alignment per the type, step
  rounded to 4, as `emitVoidPtrVAArg`). `main` is renamed as in clang
  (`__main_argc_argv`, or `__original_main` plus the wrapper `main(i32, i64)`
  and `__main_void`). Implicitly declared library functions get the library's
  prototype (new `cc_lib_sig`, lookup only); calls to a K&R definition in the
  same file use the definition's parameters.
  `switch` with `br_table` (dense cases) or a comparison chain, the cases at the
  top level of the body. Already included in W2: global data, strings,
  initializers, pointer arithmetic, bit-fields, structs as values, varargs,
  `alloca`, floating point.
- The explorer has the target `wasm64-unknown-emscripten` (vcc in wasm that
  gives wasm).
- New `bench/vcc-wasm.py`: the suites from `vcc-ctests.py` built by clang and by
  vcc (`-S`, assembled by clang) for wasm64, linked by emcc, run in node.
  LLVM's assembler type-checks `call_indirect` indexes against an i32 table
  (clang's own `-S` fails the same way), so a file whose only error is that is
  reassembled with `-Wa,--no-type-check`. Result: gcc 1 411/1 605, tcc 85/104,
  vcc 22/30 (all 1 518/1 739, 87,3 %). What remains (W4): `goto` (44), inline
  asm (36), vectors (36), `__int128`/`long double` (31), VLA (14), atomics and
  some builtins, cleanup, Duff's device.
- arm64 unchanged: `vcc-same.py` 13 160/13 160 against the build before,
  `vbench.py check --quick` 66/66; `vcc-ctests --suite vcc` 34/34, `vcc-sema
  batch` 878/879, the file by file build (`VCC_UNITY=`) without warnings.

### W0 (2026-11-09): done
- `--target=wasm64-…` gives `PP_WASM64` (the third column in `has_tab`,
  `pp.h`); the driver distinguishes `lnx` (aarch64 Linux) from Darwin and wasm,
  so the Linux additions (the `-fshort-wchar` table, `__ARM_NEON`, LSE) do not
  end up in wasm predefines. `gen-ppdata.sh` gives `predef_wasm64`,
  `predef_order_wasm64` and the wasm answers to `__has_*`; run with
  `EXTRA_INC="work/glibc-include <emscripten's sysroot/include>"`. The Linux and
  Darwin columns and tables are unchanged; 23 new names from emscripten's
  headers.
- Front end for wasm64, checked against clang 23: `char` signed, `wchar_t`
  `int`, `__builtin_va_list` `void *`, `long double` binary128 with alignment 8
  (emscripten), unnamed bit-fields as Apple (does not affect the record's
  alignment), GNU vectors aligned to the full size (arm64: at most 16).
- `-S`/`-c` for wasm64 gives an error pointing to W2 until the code generator
  exists.
- Verified: `vpp -dM -E` = clang for wasm64 with `-O0/-O1/-O2/-Os`, `-std=c99`,
  `-fPIC`, `-ffreestanding` (C23 deviates, as for Linux already before);
  `vcc-bflayout.py` 6 604 claims per target, 0 rejected for all three targets;
  layout claims (`--vcc-layout-checks`) for 1 880 test files with wasm64 as
  target: 83 858 claims, the 9 files clang rejects have the same rejections for
  aarch64-linux (existing, `_Atomic` compatibility etc.). arm64 unchanged: new
  `bench/vcc-same.py` 13 160/13 160 outputs identical (1 880 files × -c
  -O0/-O1/-O2, -S, -E, Darwin -c/-S) against the build before,
  `vbench.py check --quick` 66/66; `vcc-diff batch --cmp --kernel` 873/873,
  `vcc-sema batch` 878/879, `vcc-driver` 41/41, `vcc-ctests --suite vcc` 34/34.

## Reference contract for the GCC run tests

`bench/vcc-wasm.py` respects GCC's explicit, unconditional
`dg-require-effective-target label_values` before compiling: LLVM's Wasm target
cannot represent GNU label addresses that leave the function or are stored in
data. These cases are reported as `skip:unsupported-target`, not as passed
tests. That a simpler label address can be optimized away does not mean the
requirement is supported. Other target conditions, for example `indirect_jumps`,
are not covered by this rule.

Verified with LLVM 23 on `20071220-1`, `20071220-2` (label address in a static
table) and `990208-1` (label address leaves the inline function). Without the
target-condition check LLVM gives fatal backend errors with a stack dump. These
errors are still `error:oracle-compile` for tests without this explicit source
contract; diagnostic text must never reclassify a crash, timeout or tool error
as a skip. `bench/test-runtime-oracle.py` checks both paths. Unexpected `fail`
and `error` always print the suite/test name even without `-v`.

### OPT5: typed frame cell operations (in progress)

Wasm now shares the exact cell/overlap analysis (`codegen/rec/slots.c`) and the
value availability (`codegen/rec/slot-values.c`) with arm64. The separate adapter
`arch/wasm/codegen/slots.c` registers typed load/store, overlap and control events
as the body is produced. The optimizer chooses actions before the rendering; no
assembler text is read or pattern-matched. Stable allocation IDs follow direct
frame objects and their struct fields. Only selected forwardings get their own
wasm locals, which are not reused by the temporary pool.

The first part replaces exact 4/8-byte integer and floating-point loads and
removes overwritten stores within straight code segments at O1–O3. The operands
are still evaluated: a load becomes `drop; local.get`, a removed store still
consumes address and right-hand side. Overlap, volatile and atomic are excluded
via the same cell analysis; unknown memory operations, calls, asm and
structured control boundaries break availability. Objects whose address leaves
the direct access are excluded. VLA/dynamic frames and setjmp functions are not
yet optimized. Narrow cells, generally unread stores and removal of redundant
address producers remain.

Verified locally: unity and file by file build and 16 combinations in
`bench/vcc-wasm-cells.py` (wasm32/64 × O0–O3 × two source files), with both
code-shape requirements and running through our own assembler/linker and Node.
The requirements show removed loads from independent struct fields, two stores
to one, three loads to one after an asm boundary, and preserved volatile/atomic
operations. The run cases also cover right-hand side effects, union/bit-field,
control flow and addresses that leave the object. Cost measurement and CI
remain; this is partial progress, not full O2 coverage.

Cost test against `opt/slot-values` (`e6bad4c`), both host builds with
`-O3 -march=x86-64-v3`, Callgrind whole process including startup, 500
functions per test, wasm32/64 and O1/O2:

| Test | Increase in host instructions |
|---|---:|
| Pure integer code, no frame | +0,185–0,186 % |
| Pointer-load loop, no optimizable frame cell | +0,298 % |
| Struct with two fields and repeated accesses | +8,19 % |

The struct test removes three loads and one overwritten store per function (six
memory operations to two). The measurement shows the adapter cost when it
actually works; it does not meet a general cost limit of 1 % and claims no
improvement in the program's run time. Binary/source hashes, commands and
profiles are local in `work/opt-profile/wasm-cells-cost/result.json`. The old
build is rejected by the new positive code-shape requirement. The same 16
combinations also pass with ASan+UBSan. Cost review and CI are still open
delivery conditions.

Separate small cost change: at most 16 cell intervals are sorted with a bounded
insertion sort in the shared classifier; larger sets keep `qsort`. The same
positive 500-function test goes from about +8,19 % to +7,03 % whole-process host
instructions against the base. The twelve assembler outputs for the control,
loop and struct tests are byte-identical before/after the sort change. The
sanitized interval/value contracts and 16 wasm combinations pass; the interval
test also has explicit cases on both sides of the limit 16.

A short mixed C test after the sort change compiles eleven existing regression
files to wasm64 O2: deep mixed expressions, register locals, bit casts, frame
cells, integer rules, deep conditions, power-of-two divisions, atomics, memory
builtins and the new frame cell cases. Resource headers from LLVM 23 are given
explicitly. The sum for eleven separate processes is 96 456 389 to 97 034 041
host instructions (+0,599 %); individual files are at +0,115 to +1,337 %. The
`_BitInt` runtime file is already rejected by the base build and is not counted
as passed or in the sum. Commands, source/binary hashes and error text are
local in `work/opt-profile/wasm-cells-mixed-configured-cost/result.json`. This
delimited compile test does not replace the ordinary full performance gate or
prove the program's run time; CI remains.

### OPT5 integration check (2026-11-11)

On `opt/wasm-memory`, based on PR46 and the wasm step: unity and file by file
build. 6120/6120 arm64 objects/text are identical against the host kernel
e6bad4c (2040 files × O1 object/O2 object/O2 text), including the new portable
wasm_frame_cells regression. Sanitized cell/value contracts and all 16
observable code requirements (four targets × O0–O3), with a removed unread
volatile store as a negative mutation, pass. CI now also runs the observable
contract.

The whole own wasm32 suite: 149/149 pass, 30 run XFAIL and 15 reference
exceptions. `parenthesized_function` was wrongly marked XFAIL on wasm32 too;
both the f866a31 baseline and the candidate pass, so wasm32 now requires PASS
while wasm64's remaining signature bug is still run as XFAIL. The whole own
wasm64 suite: 153 pass, 31 run XFAIL, nine reference exceptions and one visible
oracle-compile error: the local clang23 crashes on isinf_sign's unsupported
`_Float16`. The same crash is reproduced with the f866a31 binary; no reference
crash is hidden or counted as PASS. Logs:
`work/opt-pr-tmp/wasm-memory-{own64,baseline-oracle,own32-final}.log`.
This is a local reference limitation, not a claim that the whole wasm64 suite
is green. New CI and a broader performance gate remain.

### OPT6: short-circuited constant control decisions (substep)

The target-shared helper `codegen/opt/control.c` is now also used in wasm: pure
combinations of `__builtin_constant_p`, `!`, `&&`/`||` and literal
`__builtin_strcmp` choose only the live branch at O1–O3. The existing
constant_p policy and O0 output are preserved; unknown or side-effecting
expressions continue through the old path. `if_call_const` and `cond_dead_and`
pass wasm32/64 and are mandatory. A new SDK-free test
`bench/vcc-constant-control.py` runs all four levels and is in CI for both wasm
modes and the native targets. Inlining, constant switch with removed segments
and WARN statement expressions remain open; the WARN test with volatile asm
`nop` also needs support for that instruction in wasm asm. See the OPT6 status
in `PLAN-fas2.md` for evidence, costs and delimitation.

### OPT5 continuation: private stores through structured joins

On `1d8e29a` (flow parent `7691f6c`) the separate wasm adapter
`arch/wasm/codegen/flow.c` uses the shared `codegen/flow` graph and the budgeted mask
solver. Typed control events are captured from the function entry during
ordinary emission; unique opening tokens distinguish branch targets even when
lexical depth is reused. No new AST walk, SSA or assembler text parsing.

Private exact 4/8-byte integer/floating-point cells get overwrite DSE across
if/else joins and nested diamonds. Pure control fences still break local
forwarding; real memory/call barriers also break DSE and may only be merged
within the same control epoch. That preserves the barrier after an empty branch
on all paths. The existing renderer keeps address/RHS effects and local value
captures. Full convergence and separate staging are required before change. All
cells are conservatively live at exit: general unread DSE is not included.
Loops, switch/dispatchers, goto, asm, SJLJ and dynamic frames abstain.
Capture, cell and work limits give a fallback; O0/noopt abstain. The graph is
built only after complete emission, exact cell classification and real repeated
private stores through a join.

114 configurations pass through our own tools and Node: 10 new flow cases
(wasm32/64 × O0–O3 code shape/running, and O2/noopt code shape), 16 existing
cell cases and 88 opt rule checks. Positive cases have exactly one final store;
volatile/atomic, reads before overwrite, exits, alias/escape, effects,
floating-point bits and excluded control shapes are checked. An isolated mutant
without the fence-epoch condition is rejected by the intended empty-else code
check on both widths; the original sources are left intact. Unity build with
`-j 1`, standalone syntax check of the changed wasm C files and independent
review are done. The quick gate now runs `bench/vcc-wasm-flow.py`; remote CI
remains.

The whole own SDK-based suites on the same revision end with status 0:

| Target | PASS | Run XFAIL | Skips |
|---|---:|---:|---|
| wasm32 | 178/178 | 36 | 16: 12 clang-reject, 1 noref, 3 ref-fail |
| wasm64 | 182/182 | 37 | 11: 8 clang-reject, 1 noref, 2 ref-fail |

Exceptions do not count as PASS. The logs are
`work/opt-pr-tmp/wasm-flow-full32.log` and `wasm-flow-full64.log`.
Focused validation, source evidence and isolated mutant logs/hashes are in
`build/linux/flow-evidence/`. After integrating the exact flow parent
`f7327bd` into `1458a90`, the same 114 configurations and standalone syntax
check pass again. 66 O0/O2-noopt pairs are byte-identical against the parent;
two further pairs are rejected by both's existing ILP32 layout assert in
`atomic_ops`. The SDK whole-suite figures above still apply to `1d8e29a`.

Matched clang 23 host builds with `-O3 -march=x86-64-v3`, exactly `f7327bd`
against `1458a90`, give the following separate Callgrind evidence. The whole
process including startup is counted, wasm32/64 O2 `-S`, serially once per
test/binary: 200-function controls with straight code, conditions and excluded
loops cost +0,314–0,552 % host Ir with byte-identical assembler and objects.
Two positive diamond tests cost +4,494–4,683 % extra host Ir; stores go from
800/1000 to 200 and ordinary `-c` objects shrink 2,886–3,325 %. Active analysis
is thus more expensive than 1 % in these concentrated positive tests.

A fixed mix of existing regression files gives in total +0,106 % host Ir on
wasm32 (10 files) and +0,095 % on wasm64 (11 files). `atomic_ops` is excluded
on wasm32 because both builds reject its LP64 layout assert; this is not PASS.
Two assembler outputs change per target. Commands, hash manifest, raw profiles,
objects and exceptions are in `build/linux/flow-evidence/cost/result.json`; the
whole table is in `PLAN-fas2.md`. Relocatable object size is not loaded code
size or program speed, and this does not replace the full performance gate or
remote CI. Program run-time gain is still unmeasured; broader control flow,
general DSE, OPT5/O2 and G5/G7 remain.
