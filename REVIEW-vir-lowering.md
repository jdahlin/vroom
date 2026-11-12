# Review: VIR builder, format and arm64 lowering (2026-11-12)

Scope: everything VIR that PR 116 brings to main except the register allocator
(`vir/alloc.c`, `allocchk.c`), which was not reviewed.  Four independent
read-throughs, each with differential tests against clang and the direct
emitter:

1. the arm64 record layer (`arch/a64/emit/`: `ins.h`, `opt.c`, `func.c`,
   `print.c`, `obj.c`), the diff of the lowering branch;
2. the builder changes and the lowering (`emit/vir/build.c` `vw_ifconv`,
   `emit/vir/a64.c`) against `SPEC-vir.md`;
3. the VIR format: `SPEC-vir.md` against `vir/` (table, printer, parser,
   verifier, canon, dominators);
4. the builder as a whole (`emit/vir/build.c`).

Status words: **fixed** (commit named, with a test that fails without it),
**open** (reproduced, not fixed), **latent** (traced in the code, no failing
input), **decision** (needs a design call).  Measurements are sqlite3.c at
`-O2`, speedtest1 `--size 100 --memdb`, paired runs on an M1 Pro.

## 1. Summary

- Six wrong-code bugs were found and fixed, each with a regression test
  (1.1).  Two more are open: a crash and a limit that wraps silently (1.2).
- One of the fixed bugs had flattered every sqlite number since the first
  lowering item: a compare dropped at a loop header miscompiled
  `sqlite3_str_vappendf` without changing the verification hash.  Corrected
  figures: VIR is **+7.9 %** cycles against LLVM's backend on the same
  inlining (virpol), from +14.5 % at the start; about +10 % against clang -O2
  when last measured (before that fix).
- The remaining runtime gap is mostly register allocation and frames, not
  instruction selection (section 4).
- Compile time through VIR is 4x the direct path on sqlite and has several
  quadratic paths on adversarial input (section 3).
- The specification and the code disagree in several places that matter for
  the next steps (section 5); three design points need a decision (section 6).

### 1.1 Fixed

| # | Bug | Where | Commit | Test |
|---|---|---|---|---|
| F1 | A later mask or sign extension dropped after `add x, x, w, sxtw/uxtw`: `exts` read the extended-operand form as a plain 64-bit add | `opt.c` `xresult` | a51b4106 | `vir_ext_add_facts.c` |
| F2 | A compare dropped at a label that a later branch jumps back to | `opt.c` `flags` | a51b4106 | `vir_flags_backedge.c` |
| F3 | A read of x16/x17 moved past a record whose expansion rebuilds them (large immediate or offset) | `opt.c` `fdefof`/`fwriter`/`fkept` | a51b4106 | `vir_scratch_fold.c` |
| F4 | Volatile through VIR: locals forwarded and merged, dead volatile stores dropped, bit-fields and initializers unmarked | lowering, builder, walker | 8dceb271 | `bench/vcc-volatile.py`, `tests/volatile/access.c` |
| F5 | The store table forwarded a local's value past a pointer store or call when its address is taken later in the text but earlier in execution (a loop): `x = i; *p = 100; s += x; p = &x;` gave 6 for 300 | `build.c` `vw_st_clear` | c93460e7 | `vir_store_late_addr.c` |
| F6 | A select on a 64-bit condition compared only the low word | `a64.c` `vl_cond` | 9bd65d4f | `vir_branch_bits.c`, `vir_select.c` |
| F7 | The low-half sweep was quadratic (4000 chained `if`s 0.57 s, now 0.02 s) | `a64.c` | b096c081 | timing |

Also fixed in passing: `V->why` was never reset (one failure in `vw_emit` made
every later function of the TU fail); `e->novtag` stayed raised when the
builder bailed out of a volatile access (every later load and store of the TU
became volatile); `fwriter`'s backward scan is bounded.  `returns_twice` and
`setjmp_sites`, known failures through VIR, were F2: they pass now.

F1-F3 came from the lowering branch.  F4 was partly older (volatile locals
were already dropped at the branch start).  F5 predates the lowering work.

### 1.2 Open bugs

- **O1 (crash).** `vw_readv` and `vw_phiops` recurse once per block when a
  variable defined at the top is read after about 50 000 sequential `if`s that
  do not touch it: SIGSEGV on the stack guard.  The direct emitter handles
  400 000.  `vw_entry` has the same recursion.  Fix: a worklist, or bail on
  depth.
- **O2 (silent corruption on a limit).** `vir_blk.nparam` is 16 bits and wraps:
  a function with 70 000 locals updated in a loop builds "successfully" with a
  block of 70 000 parameters counted as 4 464.  Today lowering rejects it by
  accident (a stray `bparam`); with the verifier on it reports 70 000 unrelated
  failures.  Fix: fail in `vir_ins_add` at 65 535 and report a limit in the
  parser (spec section 18 states the limit).
- **O3 (wasm, not VIR).** `(long long)(int)0x80ff0102` gives
  `i64.const 2164195586` for wasm64.  Found by `vir_byte_assembly`, which is
  `vcc-xfail-wasm`; TODO.md has the entry.

## 2. Architecture

### 2.1 Record fields with several meanings (`ins.h`)

The lowering added meanings to existing fields rather than opcodes:
`cond >= 8` on `I_ADD/SUB/ADDS/SUBS` (extended register operand), `cond 2` on
`I_JT` (a w index), `cond 4` on `I_TAIL` (a return expanded later),
`CALL_ARGS` in a call's `cond`/`sh`, `LD_VOLATILE` in `I_LD.ra` (which at
`FRAME` is the slot tag instead).  Every reader of those fields was checked.
One was wrong (F1); the rest handle or safely ignore the new meaning, and the
direct emitter never produces a record the new code misreads.  The pattern
stays fragile: a new pass must know all of them, and nothing checks it.
**Recommendation:** an accessor per overloaded field (`ins_ext(i)`,
`call_args(i)`) so a grep finds every reader, and an assert in `roles_sw`'s
default arm for opcodes it does not know.

### 2.2 Implicit x16/x17

Records that expand (an immediate or offset that does not encode, a table
dispatch) write x17/x16 without saying so in `W.ww`.  The older passes are
safe because they only carry x0-x15 and x19-x28; the fold pass was not (F3)
and now refuses to follow a value in x16/x17 across anything but a plain
one-instruction record (`fplain`).  That is a whitelist to keep in step with
the encoder by hand.  **Recommendation:** have `writes()` report the scratch
register for records that may expand; then every pass is covered by
construction.

### 2.3 The same pattern in two or three layers

| Pattern | Builder (`build.c`) | Lowering (`a64.c`) | Records (`opt.c`) |
|---|---|---|---|
| Redundant extension | `vw_simp1` EXT/TRUNC | `lo32`, `VO_EXT` as a copy | `exts`/`xredundant` |
| Addressing and operand folds | | `vl_pfold`, `vl_cinfo`/`vl_cfold` | `fold1` |
| Constants as immediates | | `vl_fixed` | `fconst` |
| Multiply by a constant | | `vl_mulk` (must mirror by hand) | `fmulk` |
| Compare/flag reuse | | icmp fuse, `vl_tfold` | `flags` |
| Lone-`br` threading | `vw_thread` | `vl_chases` | record passes |

Each layer sees a little more than the one above, which is why they grew, but
a rule changed in one place is not changed in the others (`vl_mulk` and
`fmulk` are the explicit case).  **Recommendation:** decide per pattern which
layer owns it.  The record fold is the natural owner of anything that depends
on final registers; the builder of anything target-independent.

### 2.4 If-conversion is in the builder's pre-IR  (**decision**)

`vw_ifconv` hand-edits predecessor lists, parameter operand arrays, block
order and instruction lists of the pre-IR.  Its invariants were traced and
hold, and differential tests pass.  But it cannot be tested by `virtest`,
other targets cannot share it, it runs after the only simplifier (so
`x = 0; if (a < b) x = 1;` becomes `cmp; mov; mov; csel`, not `cset`, and
`select c, 1, 1` survives), it visits outer diamonds before inner ones, and it
has no profitability test.  Speculated instructions keep their flags; that is
only correct because the builder never sets `nsw`/`nuw`/`exact`.
**Recommendation:** make it a pass over finished VIR (spec section 14), with
`tests/vir` cases, run before a simplification step.

### 2.5 The low-half analysis  (**decision**)

`lo32` (a value only read through its low 32 bits) is a target-independent
demanded-bits analysis living in the arm64 lowering.  It also reads the C
return type (`L.ret32`) and word 1 of a call's signature group, neither of
which the IR's text form can express (5.1).  Checked against both ABIs: only
width-4 extensions become copies, so char/short/`_Bool` arguments keep their
caller-side extension on Darwin; variadics and tail calls are right.
**Recommendation:** resolve spec open issue O1 the other way (let an INT piece
of size <= 4 and a 32-bit result be `i32` in the IR); then most of `lo32`
disappears and the rest belongs in `vir/` next to `vir_uses`.

### 2.6 The format (`vir/`)

- Sound: the opcode table matches the spec for every opcode's operand kinds,
  type masks, flags and effects (one exception, `asm`'s result type); print,
  parse and print again is identical on a unit exercising every flag, constant
  type and slot hint; the verifier rejects the wrong units tried (type
  mismatches, use before definition, argument counts, a disallowed flag).
- Three list encodings (`br`: length is the target's `nparam`; `switch`: stride
  depends on the selector's type; `call`/`ret`/`ibr`/`asm`: counted), each
  decoded again in `vir_oit_init`, the verifier's range check and print/parse.
  The uncounted `br` list is what turns O2 into corruption.
  **Recommendation:** one `n`-prefixed list form and a list-kind byte in
  `vir_optab`.
- Compiler dumps (`VCC_VIR=bvld`) print `%??` for signatures and `@sN` for
  symbols, so a function from real code cannot be fed back to `virtest` or
  `viralloc`.  That blocks reducing a miscompile to a `.vir` test.
- Locations are written and remapped but never read.
- Much of the surface is never produced by the builder (`nsw`/`nuw`/`exact`,
  `likely`, `pure`/`const`, most function and block flags): untested end to end.
- 64-bit `ptr` is hardwired and `VC_INT` cannot be i32, so wasm32 does not fit;
  one live function per thread will force copies for an inliner over VIR.

### 2.7 Verifier holes (accepted but wrong per the spec)

`ext.8u` from i64 to i32 and no-op extensions; `likely` with `unlikely`;
`noreturn` with `pure`; an unchecked `tlsaddr` model and symbol; a nonzero
`tag`; a `VB_ADDR` block with parameters without an `ibr`; `asm` output count
against its template; `VB_LOOP` on a non-header; a nonzero word 1 on a
`VG_REG` group.  The verifier runs only under the `v` letter, once after the
build, not after each pass or in sanitizer builds as section 12 says.

### 2.8 Builder (`build.c`)

- Sound: Braun sealing (placeholders in RPO, operand order tied to the
  predecessor list, which `vw_split` rewrites in place); conversions land in
  the predecessor before its terminator; `vw_promote`'s escape analysis,
  overlap detection and memset coverage; each `vw_simp1` rule for width and
  sign; the memory-generation scheme for load CSE (extended basic blocks only,
  fresh at joins and loop headers).  112 functions with 1167 checks give the
  same results from VIR, the direct emitter and clang, apart from F5.
- Latent: a single-use load retyped in place keeps its old CSE key (correct
  today only because the walker always extends after a narrow load); scratch
  arrays leak when `vw_fail` longjmps (OOM paths); `LO(begin)` runs before the
  `setjmp`; `vw_emit_ins` reads past a `br` list if the block is not among the
  target's predecessors; the variable hash uses 24 bits.
- Coverage: sqlite builds 1910 of 1916 functions (the 6 pass a record).  vcc's
  own sources build 1649 of 2473 (67 %): record arguments or results 551,
  `__int128` 120, thread-locals 102, frame address 18, inline asm 12, stack
  pointer 9, alloca 6, switch bit sets 5.  **Every call with a `float`
  argument bails**: `vw_stty` returns F64 for an F32 stored as 8 bytes, which
  makes the intended fallback dead code (one-line fix, verified in a scratch
  copy; not applied).

## 3. Compiler performance

| sqlite3.c -O2 | Wall | Compiler instructions |
|---|---|---|
| vcc, direct path | 0.15 s | 1.22 G |
| vcc through VIR, branch start | 0.54 s | 4.45 G |
| vcc through VIR, now | 0.58 s | 4.77 G |
| clang -O2 | 9.75 s | |

A profile of the VIR path: the walker and builder about 60 % (top self-time
`vw_promote`, `vw_simplify`), lowering including allocation about 35 %.

Fixed: the low-half sweep (F7); `fwriter`'s unbounded scan; the allocator
session fixed `vl_cfold` rescanning a block's edits (80 000 statements: 1.96 s
to 0.64 s).

Open, with numbers:

| What | Input | Direct | VIR |
|---|---|---|---|
| Per-function clears at the largest size seen (`V->h`, `V->lab`, `build.c` `vw_begin`) | one 40 000-`if` function, then 20 000 one-liners | 0.07 s | 3.34 s (64x per small function); 6 % of sqlite |
| Phis created at every join for every live variable, removed later by whole-function passes | 3000 locals + 3000 `if`s | 0.01 s | 3.17 s |
| The variable hash degenerates above 2^24 entries | 4000 locals + 4000 `if`s | 0.01 s | over 130 s, 1.4 GB |
| `vw_trivial` to a fixed point (passes = loop depth) | 400 nested loops | 0.00 s | 0.94 s (cubic) |
| `vw_promote` loops over all fields of the function per access | one struct, 4000 fields | 0.03 s | over 19 s |
| `vir_dom`/`vw_doms` (Cooper-Harvey-Kennedy) on ladder CFGs | 16 000 gotos | 0.03 s | 12.2 s |
| `vir_dom` alone | `lad16000.vir` | | 1.89 s (16x for 4x the blocks) |
| The constant table is a fixed 2^24 entries with a random hash | 300 000 small functions | | +115 MB RSS per thread |
| `tbz_relax` encodes every record of any function with one tbz | | | not measured |
| `vir_canon` computes dominators twice | 300 000 small functions | | about 50 ns/instruction against a budget of 30 (spec 22.1) |

**Recommendations, in order of effect:** remove a trivial phi as soon as its
operands are known (Braun's `tryRemoveTrivialPhi`) or place phis by dominance
frontier, which also bounds the hash; clear only what a function used; per-slot
field lists in `vw_promote`; a bound or semi-NCA for dominators; a
function-sized constant table.

## 4. Runtime performance

VIR against virpol (LLVM's backend on VIR's own inlining), cycles: +14.5 % at
the branch start, **+7.9 %** [+7.1 .. +8.6] now.  The intermediate figures in
`PLAN-vir.md` and in commit messages between the first lowering item and
a51b4106 are too good by about 2.7 % (F2).

Static instructions in the sqlite binary:

| | VIR | virpol | Excess |
|---|---|---|---|
| Total | 304 k | 242 k | +62 k (26 %) |
| Register-to-register moves | 38.8 k | 21.3 k | +17.5 k |
| Loads and stores on sp/fp | 39.6 k | 21.8 k | +17.8 k |
| Unconditional branches | 17.2 k | 12.1 k | +5.0 k |
| Constant moves | 24.3 k | 20.6 k | +3.6 k |
| Returns (tail-duplicated epilogues) | 2.8 k | 1.7 k | +1.1 k |

More than half the excess is register allocation and frame handling.  The
last three lowering changes (low-half moves, multiplier constants,
if-conversion) each landed within measurement noise; further selection
patterns will too.

What the builder leaves for the lowering (missed, with examples in the
reviewers' scratch files): no constant folding of binary operations
(`add 1, 1` after a forwarded store), no `x - x`, no forwarding past a store
to a disjoint field of the same base (any store invalidates all loads), i64
`mul x, 4` for index scaling not turned into a shift, double extensions around
narrow parameters, empty and single-predecessor blocks surviving (no block
merging after if-conversion), boolean chains as phis through empty blocks, no
load availability at joins.

Not in VIR at all, from the earlier gap analysis: loop rotation, 1-byte jump
table entries, shrink-wrapped frames, FP loads without the x16 detour.

## 5. Specification against code

5.1 **Call signatures (section 9.1).**  Word 1 of a `VG_REG` group holds the C
argument size and the `VG_UNNAMED` flag; the lowering depends on both (Darwin
stack packing, variadics, the low-half rule).  The spec says word 1 is for
memory kinds only, the verifier does not check it, and print/parse drop it, so
two different signatures print the same.  The lowering also indexes groups by
argument position, which holds only while every group has one piece.

5.2 **32-bit values.**  The lowering's invariant is "an i32's register has an
unspecified upper half" (`trunc` is a copy, `ext.4u` the only thing that
clears).  The spec does not state it; open issue O1 says the builder extends,
and the header of `build.c` says the upper half is zero.  F6 was a violation
of the unwritten rule.

5.3 **Things the spec says 1.0 does not do, and the code does:** `switch` from
the builder with the lowering's own table code (15.4, X5); tail calls in the
lowering (1.3); folds beyond "a value used once, by the next instruction"
(15.3); if-conversion, absent from section 14's pass list.

5.4 **Things the spec describes that do not exist:** `VCC_VIR_VERIFY`,
`-fvir-dump=STAGE`, `VCC_VIR_PASSES`, the `too_large` reason, the `; loc`
comment; `virrun`, the reference interpreter of section 13, so passes have no
equivalence check; `vir_canon` computing loop depth and resolving aliases.

5.5 **Signed division.**  `sdiv`/`srem` of the minimum by -1 are defined (rows
13, 15).  arm64 gives that natively; x86-64's `idiv` traps, so the first
listed target needs a guard on every signed division.  Make it immediate UB
(C and LLVM agree) or state the cost.

5.6 Smaller: `asm`'s result type (7.3 against 8.6); limits in `vir.h` that
section 18 omits; `check_call` caps a call at 1 024 argument words against the
spec's 65 535 groups.

## 6. Decisions needed

1. **If-conversion as a VIR pass** (2.4): move it, or keep it in the builder
   and accept that it stays arm64-only in effect and untestable in isolation.
2. **32-bit arguments and results** (2.5, 5.1, 5.2): `i32` in signatures, or
   document the size word and the unspecified upper half and give them syntax.
3. **Where each fold lives** (2.3), before more are added.
4. **What gates a merge of VIR work**: today the sqlite hash plus the C suites.
   The hash missed F2 for the whole branch.  Candidates: the differential
   generators the reviewers wrote by hand (kept as a harness), the kernel and
   PostgreSQL through `VCC_VIR`, the verifier in every sanitizer run, and
   `virrun`.

## 7. Not covered

The register allocator and its checker.  x86-64: `bench/vcc-x86.py` could not
run on the review machine, and the walker changes for volatile (F4) are shared
with it.  `bench/vcc-volatile.py` has only run on macOS and is not in
`ci/premerge.sh`.  The Apple-ABI harness through VIR has 4 failures that
predate this work and were not looked into.  The latent items listed above
were traced, not triggered.
