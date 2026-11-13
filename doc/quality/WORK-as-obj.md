# Work order: assembler and object model

The pass over `src/as/`, `src/obj/` and the writers, after the shared library
(`WORK-common.md`).  Evidence: `doc/review/quality-as-obj.md` (finding ids
`Q-asobj-NN`; its paths `ccs/vcc/X` are `src/X` now).  Names follow
`doc/STYLE.md`.

**Owns** `src/as/` (not `arch/*/as/` until step 5), `src/platform/darwin/astext.c`,
the `as_SRC` list in `src/Makefile`.  Later steps own `src/obj/`,
`src/platform/linux/elf.c` and the per-CPU `as/` and `obj/` directories.

## What done looks like (whole pass)

- One job per file, each with an opening comment; no file over about 1500
  lines without a stated reason.
- Every file-scope name has its module prefix (`as_`, `obj_`, `elf_`, `a64_`,
  `x64_`); no private copies of `lib/` facilities.
- The target is data: one table per CPU, not `machine == EM_X86_64` tests.
- Output byte for byte as before until a step says otherwise.

## Steps

Each step is one PR.  Proof for each: `make -C src -j 3 vcc` unity and
`VCC_UNITY= OUT=../build/src-parts`, `bench/vas-obj.py`, `bench/vas-enc.py
$PWD/src/test/vas`, `bench/vcc-ctests.py --suite vcc`, `bench/vcc-cfi.py`,
`bench/vcc-dwarf.py`.

### 1. Split `as/as.c` (moves only) -- this PR

Q-asobj-01.  Line numbers are those of `as.c` before the split (4081 lines).
Functions keep their names; the ones several files share lose `static`
(`VPRIV`) and get a prototype in `asimpl.h`, grouped by defining file.

| File | From `as.c` | Job |
|---|---|---|
| `as/as.c` | 15-42, 454-599, 2150-2169, 2173-2610, 3645-3720 | messages, frames and the statement scanner, labels, `assign`, directive dispatch, `statement`, lifecycle (`as_new` .. `as_end`, `as_run`) |
| `as/sym.c` | 44-113, 1352-1431, 3552-3643 | symbols, temporaries, numeric labels, `.set` variables, `.globl`/`.type`/`.size`, register aliases, `.symver` |
| `as/expr.c` | 115-452 | expressions: numbers, operators, folding, deferral |
| `as/section.c` | 601-774 | `.section`, standard sections, name parsing, subsections |
| `as/data.c` | 776-1350 | values, strings, floats, alignment, `.comm`, `.space`/`.fill`/`.org`, `.incbin`, `.reloc`, `.inst` |
| `as/macro.c` | 1501-2029 | `.macro`, arguments, templates, `.rept`/`.irp`, `.exitm` |
| `as/cond.c` | 2031-2148 | conditionals |
| `as/cfi.c` | 2612-2772, 2798-2927 | `.cfi_*` and `.eh_frame` |
| `as/relax.c` | 2785-2797, 2929-3234 | variable items: `.prefalign`, deferred alignment, LEB of differences, `.skip`, x86-64 jumps; frag offsets |
| `as/dwline.c` | 3236-3550 | `.file N`, `.loc`, `.debug_line` |
| `platform/darwin/astext.c` | 3722-4081 | Apple dialect to GNU dialect (`as_darwin_text`); uses nothing of `As`; listed in `as_SRC` |
| `as/asimpl.h` | 658, 1174, 2775-2783 | `VI_*`, `Buf8` and `uleb`/`sleb` as inline functions (until step 4), the shared prototypes |

Not cut as the review drew it:

- `assign` (1431-1499, `=` and `.set`) stays in `as.c`: it is called only
  from `statement`, and as an exported name it collides with `assign` in
  `frontend/c/expr.c` in the file-by-file link.
- `next_stmt` (the statement scanner's hot path, `always_inline`) is
  `VPRIV` without `inline`: `macro.c` calls it too.  Unity code is as before;
  file by file it is a call.
- `uleb`/`sleb` and `frag_bases` (the review's `lib/leb.h` and `obj/`) are
  not moved out of the module in this step: header and `relax.c`.

### 2. One fixup resolver (Q-asobj-02, 10, 11)

`obj/resolve.c`: the CPU-independent part of fixup resolution written once
(load frag and symbols, absolute fold, difference case, same-section pc-relative
patch, else relocation), with four per-CPU hooks (is pc-relative, absolute to
pc-relative type, never-in-place types, patcher) and one flag (constant target
as given or as `c - P`).  Removes the copies in `arch/a64/obj/fixup.c`,
`arch/x64/obj/fixup.c` and `platform/darwin/macho.c`.  Output may differ only
in the way the review names; otherwise byte for byte (`vas-obj.py`,
`VCC_LDCMP=1 vcc-ctests.py`, `vcc-darwin-s.py`).

### 3. Per-CPU table instead of `machine == EM_X86_64` (Q-asobj-06, D3)

One table per CPU read by the assembler and object: comment character,
instruction entry, CFI register mapping, data relocation types, nop fill,
branch relaxation.  The 20 tests in `as.c` (now spread over the new files), 3 in
`obj.c`, 1 in `elf.c`, the x86 state in `struct As` and `asimpl.h`'s
`#include "arch/a64/a64.h"` go.  Validated by `vas-obj.py`, `vcc-x86.py`.

### 4. Shared byte and LEB128 helpers (Q-asobj-03, 07)

`common/` gets `bytes.h` (little-endian stores and loads) and `leb.h` (one
encoder into a caller's bytes); `uleb`/`sleb`/`leb_len` here, the writers'
`put`/`p16`/`p32`/`p64`, the nine LEB copies and the twelve grow-an-array
blocks use them.  This belongs to the shared library's owner; this module
follows.

### 5. Names, per-CPU directories, comments

- Rename `ElfObj`/`elf_*` to `obj_*` (the format-neutral model), keep `elf_`
  for `platform/linux/elf.c` only (STYLE.md: the prefix of a platform is its
  format).
- `as_` prefix on every file-scope function and long-lived field of the
  module (`d_data` -> `as_directive_data`, `err` -> `as_error_report`, ...); rename
  table checked in beforehand, applied by script.
- `arch/a64/as` and `arch/x64/as`: `a64_as_`/`x64_as_` prefixes, x86-64's
  `strcmp` chain to one hashed table (Q-asobj-05), `enc.c` classes
  (Q-asobj-12).
- Stale headers and dead code (Q-asobj-16, 17); `as.h` opening comment is the
  module overview of the review's section 2.
- Error policy (Q-asobj-13) and the ELF/Mach-O section mapping in one table
  (Q-asobj-08) are their own PRs.

## Rename table (step 5, assembler part)

`doc/quality/rename-as.tsv`: one `old<TAB>new<TAB>kind<TAB>where` line per
identifier of `src/as/*.c`, `asimpl.h` and `as.h` (351 lines: 145 functions,
135 struct fields, 38 constants, 19 types, 5 globals, 9 files).  Rules used:
`as_noun_verb` in full words (STYLE.md); types `as_noun`, `As` is
`as_context`; constants `AS_NOUN_VALUE`; functions the arm64 and x86-64
assemblers define for it carry `a64_as_`/`x64_as_`; `as_darwin_text` is the
Mach-O dialect translator, so it takes the platform's format prefix
(`macho_`).  Not in the table: `ElfObj`/`elf_*` (the obj module's table),
short locals and the parameter name `a`, names that are already right
(`as_new`, `as_free`, `as_run`, `as_begin`, `as_end`, `VPRIV`).  A `-` as the
new name means delete (`pool_dummy` is dead, Q-asobj-16).  `Sv`'s fields `s`
and `n` need a type-aware rewrite, not a whole-word one.  Checked: no two
olds map to one new in one scope, and no new name exists in the tree
(`as_directive_data` and `as_expression_parse` appear only in STYLE.md and
this file as examples).  The fields of `struct PoolS` (arm64's literal
pools) are for the arm64 pass.
