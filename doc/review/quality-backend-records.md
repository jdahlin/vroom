# Quality review: records after the walk (2026-11-13, main at 2e6d3e73)

Area: `arch/a64/codegen/{opt,func,print,obj,darwin,tail,unreachable,insn}.c` with
`ins.h` and `a64impl.h`; `arch/x64/codegen/` without `vir.c`; `codegen/rec/`;
`codegen/flow/`.  11.6 k lines, all read in full.  Paths are relative to
`src/`.  Builds on `REVIEW-vir-lowering.md` section 2 (record fields);
nothing from it is repeated, its open recommendations are referred to as R2.1
and R2.2.

## 1. Summary

The area works and its passes are individually well commented, but it holds
two back ends that were meant to share one layer and share only four small
kernels.  Three things matter most:

1. **Two record structs with the same bytes.**  `ins` (`ins.h:81`) and `rec`
   (`rec/rec.h:16`) have identical layout; only the field names differ
   (`rd rn rm ra` / `r[0..3]`).  Because they are distinct C types the shared
   kernels are type-erased (stride, offsets, `memcpy`, callbacks), and every
   pass above them (liveness, dead code, extensions, frame decision, sibling
   calls) is written once per target.
2. **Instruction knowledge is written per consumer, not per target.**  arm64
   classifies its opcodes in seven switches and tables, x86-64 in seven; the
   multi-instruction expansions of arm64 exist in the printer, the encoder
   and a whitelist in the optimizer; x86-64 maps record to mnemonic and
   operands twice.  A new opcode is added in up to nine places with four
   different defaults for "unknown".
3. **`opt.c` and `func.c` are the two files nobody can find a pass in**, and
   `finish_function` walks the records about 26 times at `-O1` and 6 times
   at `-O0`, three of the latter looking for records only VIR produces.

## 2. Inventory

| File | Lines | What it does | Better name | Header |
|---|---|---|---|---|
| `arch/a64/codegen/ins.h` | 90 | arm64 opcodes, the record `ins`, field overloads | `rec.h` of arm64 once merged | label; per-op comments good |
| `arch/a64/codegen/a64impl.h` | 182 | register printing, record constructors (`put op3 opi mem`), FP move encodings, `a64_big*` prototypes | `build.h` | label |
| `arch/a64/codegen/insn.c` | 40 | `cond_name[]`, `logimm_ok` | fold into `a64.h` (`a64_logimm`) | stale ("byte buffers, register names": neither is here) |
| `arch/a64/codegen/opt.c` | 1906 | op roles and effects; liveness; copies; slot forwarding; extensions; dead results; frame-register query; operand folding, constants, multiply, byte loads; repeated compares; the driver | split, see 3.3 | good for the first pass, label for the file |
| `arch/a64/codegen/func.c` | 998 | symbol header and ctor entry for all targets; frame layout; prologue and epilogue (three times); jump-table and tail expansion; tbz relaxation; text output loop; the AST-side `emit_function1` | `symhdr.c` (to `codegen/`), `frame.c`, `expand.c`, `function.c` | label |
| `arch/a64/codegen/print.c` | 509 | record to GNU as text | ok | label |
| `arch/a64/codegen/obj.c` | 704 | record to machine words (`enc`), fixups, branch relaxation, replay of `IT_CODE` items | `encode.c` + `replay.c` | label |
| `arch/a64/codegen/darwin.c` | 175 | line rewrite of the finished `-S` text into Apple's dialect | `platform/darwin/astext.c` | good |
| `arch/a64/codegen/tail.c` | 57 | sibling-call return, terminal marker lookup | ok | good |
| `arch/a64/codegen/unreachable.c` | 50 | arm64's flow table and debug adapter for `em_rec_unreachable` | a column of the op table | label |
| `arch/x64/codegen/x64impl.h` | 164 | registers, opcodes, `xst`, prototypes | ok | good, but names `twoaddr.c` (does not exist) |
| `arch/x64/codegen/lower.c` | 1037 | record constructors, temporaries, integer ops, compares, **the pass pipeline `x64_finish`**, TLS, atomics, bit builtins, alloca, SSE4.2 strings | `lower.c` + `finish.c` + `builtin.c` + `atomic.c` | label |
| `arch/x64/codegen/abi.c` | 864 | SysV classification, calls, parameters, results, varargs | ok | label |
| `arch/x64/codegen/asm.c` | 827 | inline asm: constraints, operand placement, template, x87 stack | ok | label; operand comment good |
| `arch/x64/codegen/wide.c` | 821 | `__int128` (1-480) and `_BitInt` limb arithmetic (482-821) | `int128.c`, `bitint.c` | good for the first half |
| `arch/x64/codegen/print.c` | 815 | **two-address pass with copy forwarding and the frame decision** (1-65), then prologue, record switch, epilogue, and the choice text/object item per record | `twoaddr.c` + `print.c` | label |
| `arch/x64/codegen/fp.c` | 697 | SSE scalars, x87 long double, `__float128` through libgcc, conversions | ok | good |
| `arch/x64/codegen/effects.c` | 270 | op table (`opdesc`), `x64_effects`, flow table | ok | good |
| `arch/x64/codegen/dead.c` | 183 | blocks, liveness through `codegen/flow`, dead results, compaction | ok | good |
| `arch/x64/codegen/obj.c` | 169 | whitelist of encodable records; record to mnemonic string and `XOp`s for the assembler | `asmops.c` | good |
| `arch/x64/codegen/copies.c` | 95 | adapter for `em_rec_copy_forward` | ok | good |
| `extensions.c` 34, `frame.c` 20, `locals.c` 75, `mulconst.c` 13, `mulhi.c` 35, `setjmp.c` 63, `switch-bitset.c` 24, `switch-table.c` 34, `tail.c` 59 | | one recipe each | `extensions.c` is one predicate, `x64_last_u32` | good |
| `codegen/rec/rec.h` | 59 | `rec`, `rmask`, flow kinds, `opdesc`, `receffects` | ok | good |
| `codegen/rec/copies.c` 56, `fallthrough.c` 48, `unreachable.c` 36 | | three kernels over opaque records | ok | good |
| `codegen/rec/slots.[ch]` 232+77, `slot-values.c` 60 | | exact frame cells, value availability per cell | `cells.c`, `cell-values.c` | good |
| `codegen/flow/flow.h` 44, `graph.c` 69, `maskflow.c` 94 | | CSR block graph; backward bit-mask liveness with a work budget | `maskflow.c` -> `liveness.c` | good / label / label |
| `codegen/flow/values.[ch]` 47+32 | | 32-entry direct-mapped table of expression identities (user: `codegen/opt/sibling-values.c`) | `valuetable.c` | good |

**What the module is for (draft opening comment).**  A function leaves the
AST walk as a flat array of 16-byte machine records with physical registers.
This layer (1) removes what the walk could not avoid (code after a jump,
jumps to the next record, copies, reloads of slots, repeated extensions, dead
results), (2) decides the frame from what is left, (3) expands pseudo
records (tables, tails, far branches) and (4) writes text or object items.
Invariants a newcomer must know:

- Records keep their index between passes; a pass deletes by setting
  `op = 0xff` and one compaction at the end moves them.  `-g` rows
  (`e->dl[].k`) are record indices, so every compaction calls `dbg_remap`.
- arm64 liveness (`W.la`) is computed once and only ever enlarged by a
  rewrite (`opt.c:21`); the per-record masks `W.rr/ww/ro` must be refreshed
  by whoever edits a record.
- A record the tables do not know reads and writes everything; a label
  that asm or `&&l` may reach (`LREF_EXT`) starts with nothing known.
- arm64 records at `FRAME` carry the slot allocation's id in `rm/ra`
  (and `cond/sh`), cleared at `func.c:581`; no pass may run after that.
- x16/x17 (arm64) and r11/xmm15 (x86-64) are written by expansions that do
  not say so (R2.2); only x0-x15, x19-x28 carry facts.
- x86-64 records are three-address until `x64_twoaddr`; effects are
  "logical records before or after" that step (`effects.c:148`).

## 3. Passes and data flow (L4)

### 3.1 arm64: `finish_function` (`arch/a64/codegen/func.c:474`)

| # | Pass (file:line of the call) | Reads | Changes | Analysis needed | Sweeps over all records |
|---|---|---|---|---|---|
| 1 | `a64_unreachable` func.c:495 -> rec/unreachable.c:7 | op, flow table unreachable.c:23 | drops records after `b/ret/tail` up to a label | none | 1 (compacts) |
| 2 | `drop_fallthrough` func.c:496 -> rec/fallthrough.c:6 | op, label | drops `b L` in front of `L:` | none | 1 (compacts) |
| 3 | `opt_function` func.c:498 -> opt.c:1854 | | | | |
| 3a | `liveness` opt.c:1873 (:248) | roles, lrefc | fills `W` | builds blocks and successors itself | 4 (:265 :277 :309 :334) + block fixpoint |
| 3b | spill pairs opt.c:1876 | `SPILL_REC`, `W.la` | deletes store+reload of a dead temporary | liveness | 1 |
| 3c | `copies` opt.c:1886 (:1828) | `W.la rr ww ro` | forward (shared kernel) and backward copy propagation, dead movs | liveness | 2 (:1837 :1849) |
| 3d | `slots` opt.c:1887 (:483) | slot tags, `W.blk bs be` | load -> mov, dead stores | blocks; cells (`em_slot_*`) | 3 (:492 :518 :532) |
| 3e | `exts` opt.c:1888 (:850) | `W.bs be s0 s1 rr ww` | extension -> mov/deleted | blocks; own forward fixpoint | 2 (:857 :928) + one visit per block and round |
| 3f | `copies` again opt.c:1889 | | removes the movs 3d/3e made | liveness | 2 |
| 3g | `dead` opt.c:1890 (:964) | `W.out rr ww` | deletes pure dead results | liveness | 1 |
| 3h | `fold` opt.c:1892 (:1698), VIR only | `W.*` | folds shifts, extensions, sums, constants into readers; byte loads | liveness, per-reader backward scans (bounded 32) | 3 (:1700 :1704 :1707) |
| 3i | `flags` opt.c:1893 (:1733), VIR only | records | deletes repeated compares | own label table; **recomputes roles/reads/writes** (:1805-1807) | 3 (:1751 :1756 :1762) |
| 3j | compaction opt.c:1897 | | | | 1 |
| 4 | `opt_regs` func.c:506 -> opt.c:1020 | records | nothing; registers used, `OPF_*` | **recomputes roles/reads/writes** (:1040-1042) | 1 |
| 5 | `expand_tables` func.c:518 (:273) | `I_JT` | 5 records + table words | none | 0 without tables, else 2 + per `-g` row |
| 6 | prologue marker func.c:553, `opt_noframe` :556 | `NOP_PROLOG` (VIR only) | | recomputes roles for the prefix (opt.c:1080) | 1 |
| 7 | clear `I_TAIL.sh` func.c:562-565 (VIR only) | | | | 1 |
| 8 | `tail_expand` func.c:566 (:398), VIR only | `I_TAIL` cond 2-4 | epilogue copies | | 1 (:404) |
| 9 | frame-slot rewrite func.c:571 | `rn == FRAME` | offsets, clears tags | frame layout | 1 |
| 10 | `tbz_relax` func.c:599 (:334) | `I_TBZ` | inverted test over `b` | sizes: `a64_words` = a full `enc` per record (obj.c:333) | 1, +2 when a tbz exists |
| 11a | text: func.c:625 -> `print_ins` | | | | 1 |
| 11b | object: `finish_direct` func.c:600 (:152) | | copies records into an `IT_CODE` item; later `a64_replay` obj.c:514 encodes | | 1 (:167) + copy, then 1-2 in replay |
| 12 | Darwin `-S`: `darwin_text` codegen/tu.c:692 -> darwin.c:111 | the whole TU's text | rewrites every line | | text, not records |

At `-O1` a direct-emitter function is walked about 26 times (2 + 17 in
`opt_function` + 7); through VIR about 32.  At `-O0`, the kernel's level,
steps 6-10 and 11 still run: 6 walks, of which 6, 7 and 8 look for records
only `codegen/vir/a64.c` emits (`a64.c:1185,1540,2263`).  These are counts read
from the code, not measured cost; review B has the instructions.

Recomputed analyses: roles/reads/writes per record four times (3a, 3i, 4, 6);
label positions twice (`W.lpos` opt.c:286, `lp` opt.c:1751); block structure
once (shared by 3c-3h through `W`); liveness once.  The frame-register
query (4) cannot use `W.rr/ww` because compaction (3j) moved the records.

### 3.2 x86-64: `x64_finish` (`arch/x64/codegen/lower.c:629`)

| # | Pass | Reads | Changes | Analysis | Sweeps |
|---|---|---|---|---|---|
| 1 | `em_rec_unreachable` lower.c:641 | flow table effects.c:181 | as arm64 | none | 1 |
| 2 | `em_rec_fallthrough` lower.c:645 | | as arm64 | none | 1 |
| 3 | `x64_dead` lower.c:648 -> dead.c:179; only when `e->discarded_scalar`, at most 256 blocks, 1024 edges, 65536 records (dead.c:6-8, :181) | `x64_effects` per record, twice (:110, :154) | deletes pure dead results | own block split (:83), `em_flow_build` + `em_flow_liveness` | 4 (:83 :106 :150 :168) |
| 4 | `x64_twoaddr` lower.c:649 -> **print.c:23** | | ties results, inserts movs; calls `x64_copy_forward_at` per mov (copies.c:76, forward scan with `x64_effects` per record) and `x64_framefree_record` per record (frame.c:11, `x64_effects` again) | none (a copy must reach an overwrite in its block) | 1 + scans |
| 5 | `x64_print` lower.c:653 -> print.c:196 | | prologue, text or `IT_X64` item per record (`x64_obj_record` obj.c:6), epilogue | | 1 |
| 6 | object: `x64_replay` obj.c:81 | items | mnemonic string + `XOp`s to `as_x64_insn` | | 1 |

`x64_effects` (effects.c:120, a table row plus about ten tests) is evaluated up
to four times per record (dead twice, copies, frame) and never cached.

### 3.3 One idea, two implementations

| Idea | arm64 | x86-64 | Shared today |
|---|---|---|---|
| Unreachable code | table unreachable.c:23-44 | table effects.c:183-268 | kernel `em_rec_unreachable` |
| Jump to next | func.c:249 | lower.c:645 | kernel `em_rec_fallthrough` |
| Block split + liveness | opt.c:248-344 (arrays, round-robin) | dead.c:83-144 + flow/maskflow.c (CSR, worklist, budget) | nothing (wasm's `flow.c` also uses `em_flow_*`; not read) |
| Dead results | opt.c:940-986 | dead.c:44-48, :150-164 | nothing |
| Copy forward | opt.c:346-384, :1828 | copies.c | kernel `em_rec_copy_forward`, two modes |
| Copy backward | opt.c:387 | none | |
| Slot forwarding, dead stores | opt.c:483 | none (`x64_slot` is an empty stub, locals.c:56) | kernel `em_slot_*` |
| Known extensions | opt.c:596-935 (global) and `last_cset` a64impl.h:117 | extensions.c:6 (previous record only) | nothing |
| Leaf frame | opt.c:1020 + func.c:506-515 | frame.c + print.c:32-41 | nothing |
| Compaction with `-g` remap | opt.c:1895-1905 | dead.c:167-176 | built into the two kernels only |
| Sibling call | tail.c:6-57 | tail.c:5-59 | `em_tail_candidate` only |

### 3.4 `opt.c`: its state, and the split

`W` (opt.c:205): per-thread scratch of the liveness pass.  `la` live-after
per record; `rr`, `ww` registers read and written per record; `ro` the
record's roles; `blk` its block; `bs`, `be` a block's first and last record;
`s0`, `s1` a block's fall-through and branch successor; `in out use def` the
block sets; `lpos` label -> block + 1.  `RT` (:104) opcode -> roles, `BT`
opcode -> branch kind, `XT` opcode -> "may be a redundant extension", `RBT`
register number -> its bit (0 above 63), all filled by a constructor (:111).
`rawp`, `retreads` (:39-40): the inline-asm text and the registers a return
reads, set by each entry point.  `SL` (:445): slot cells, record -> cell,
cell values.  `XF` (:616) with `xf`: per block and register, `z` = the value
fits in its low z bits, `s` = it is the sign extension of its low s bits;
`xz()`, `xs()` read them, `XU` = unknown.  `fold1` (:1221): one fold at
reader k; `fclass` classifies a definition as shift or extension; `fdefof`
finds the single definition a reader consumes; `fwriter` the last writer;
`fkept` "what the definition read is unchanged up to the reader"; `fdone`
the bookkeeping; `fplain` "this record is one instruction, so it cannot
clobber x16/x17"; `fconst`, `fmulk`, `fbytes`/`fbswap` constants, multiply by
constant, byte-assembled loads.

Proposed files (moves only; the same names on x86-64 where it has the pass):

| New file | From opt.c | Entry point today -> proposed |
|---|---|---|
| `effects.c` | 25-202, 940-956, 1000-1018, 1133-1151, 1717-1721 | `roles reads writes brkind pure fplain sets_flags word_regs` -> `a64_rec_roles`, `_reads`, `_writes`, `_branch`, `_pure`, `_single`, `_sets_flags`, `a64_word_regs` |
| `liveness.c` | 204-344 | `W`, `liveness` -> `a64_live L`, `a64_liveness`; fields `live_after reads writes roles block_of first last succ_next succ_jump label_block` |
| `copies.c` | 346-422, 1827-1852, 1875-1884 | `copies`, `backward` -> `a64_copies`, `a64_copy_backward`, `a64_dead_spills` |
| `slots.c` | 425-594 | `slots` -> `a64_slot_forward` |
| `extensions.c` | 596-935 | `exts` -> `a64_extensions`; `xf` -> `a64_extfacts` with `zbits[32]`, `sbits[32]` instead of one array and `XS()` |
| `dead.c` | 958-986 | `dead` -> `a64_dead_results` |
| `frameregs.c` | 988-1089 | `opt_regs`, `opt_noframe` -> `a64_used_regs`, `a64_runs_frameless` (they are not optimizations) |
| `fold.c`, `fold-bytes.c` | 1091-1539, 1698-1713; 1541-1696 | `fold` -> `a64_fold_operands`, `a64_fold_byteload` |
| `compares.c` | 1715-1825 | `flags` -> `a64_repeated_compares` |
| `cleanup.c` | 1854-1906 | `opt_function` -> `a64_cleanup`: the pass list and nothing else |

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-BR-01 | L1 L7 | near-duplicate | `ins` arch/a64/codegen/ins.h:81-90 = `rec` codegen/rec/rec.h:16-25 byte for byte; erasure in rec/fallthrough.c:9-21, rec/unreachable.c:7-18, rec/copies.c:15-29, emitimpl.h:656-686 | every shared pass needs stride/offset plumbing and a per-target adapter; nothing above the four kernels can be shared | one struct, `union { uint8_t r[4]; struct { uint8_t rd, rn, rm, ra; }; }`; kernels take `rec *` | M | low: no layout change | `vcc-same.py`, `vbench.py gate`, `bench/vcc-rec-*.py` |
| Q-BR-02 | L1 | pattern | arm64: opt.c:44 `roles_sw`, :134 `brkind_sw`, :120 `XT`, :940 `pure`, :1133 `fplain`, :1717 `sets_flags`, unreachable.c:23-44, obj.c:85, print.c:109; x86-64: effects.c:22 and :183, print.c:7 `x64_tied`, :13, copies.c:15, obj.c:9-26, extensions.c:20-28 | a new opcode is added in up to nine places; the defaults differ (read-all, entry barrier, `// ?`, zero words) | one op table per target, `opdesc` with the missing columns, the enum and mnemonic generated from it | M | medium: table must reproduce operand-dependent roles (`OP_VAR`) | `vcc-same.py`, `bench/vcc-rec-effects.py` |
| Q-BR-03 | L4 | mixed file | opt.c: ten passes, 1906 lines (3.4); func.c: five jobs; x64 print.c:1-65 is a pass, lower.c:629 the pipeline | nobody finds a pass; the pass order is readable in neither target | the split of 3.4; `x64_twoaddr` to `twoaddr.c`, `x64_finish` to `finish.c` | M | none (moves) | `vcc-same.py`, unity and file-by-file builds |
| Q-BR-04 | L1 | near-duplicate | arm64 expansions twice: `p_movi` print.c:14-62 = `e_movi` obj.c:14-60; `p_addi` print.c:134-176 = obj.c:104-134; `p_mem` print.c:72-107 = obj.c:183-199; TLV print.c:485-504 = obj.c:296-327; a third copy as sizes (`a64_words` obj.c:333 runs `enc`) and a fourth as the whitelist `fplain` opt.c:1133-1151 | `-S` and `-c` can differ silently; R2.2's hidden x17 writes come from here | one plan per expanding record (`a64_movi_plan` ...: count, kind, chunks) that printer, encoder, sizer and `writes()` read | M | low | `vcc-same.py` on `-S` and objects, `bench/vas-obj.py`, `bench/vcc-darwin-s.py` |
| Q-BR-05 | L1 | near-duplicate | x86-64 record -> instruction twice: print.c:325-745 and obj.c:98-165; same tables at print.c:77 = obj.c:85, print.c:200-203 = obj.c:86-91, print.c:376-377 = obj.c:118-119 = print.c:650-651, `X_MOVI` forms print.c:341-366 = obj.c:107-113; a third list obj.c:9-26 says which records the second switch knows | two switches and a whitelist to keep in step; the object path builds mnemonics with `snprintf` (obj.c:139,141,147,161) for a lookup by name | `x64_form(rec) -> mnemonic id + XOp[3]`; the printer formats the `XOp`s, replay passes them on | M | medium | `bench/vcc-x86.py`, `vcc-same.py` |
| Q-BR-06 | L1 | duplicate | arm64 prologue/epilogue three times: records func.c:175-210, strings func.c:626-657 and :711-738, records again func.c:446-463; CFI restore rows func.c:696-700 = :727-731 | a frame shape (`sleaf`, `nomov`, realigned) is added in three places | `a64_prologue(ins *out)`, `a64_epilogue(ins *out)`; text prints them with `print_ins`, CFI rows from the `NOP_CFI_*` markers that already exist | M | medium (`-g`, CFI text) | `vcc-same.py`, `vcc-cfi.py`, `vcc-dwarf.py` |
| Q-BR-07 | L1 | duplicate | "save live temporaries, align, call, restore": abi.c:399-411 + :538-550 = fp.c:252-264 + :330-342 = wide.c:78-90 + :112-124 = lower.c:703-715 + :726-738 (the scan's four exact runs) | four call sequences; a change to the save rule (xmm width, alignment) in four files | `x64_call_save`/`x64_call_restore` beside `x64_save` (lower.c:269); then `x64_tfcall`, `x64_lcall` and TLS-GD as callers of one `x64_libcall` | S | low | `bench/vcc-x86.py`, `vcc-same.py` |
| Q-BR-08 | L4 | redundant walk | func.c:553-561, :562-565 and `tail_expand` :404-407 scan every function for VIR-only records; func.c:339-342 (tbz), :167-168 (raw) are presence scans | at `-O0` three of the six walks can find nothing outside VIR and two more only test for a rare opcode | gate on `e->vlow`; `put()` keeps a bit set of opcodes seen | S | none | `vcc-same.py`; `vbench.py gate` should go down |
| Q-BR-09 | L4 | redundant walk | 3.1: roles/reads/writes recomputed opt.c:1805-1807, :1040-1042, :1080-1085; `copies` twice (:1886, :1889); two compactions back to back on both targets (func.c:495-496, lower.c:641-646) and a third (opt.c:1897, dead.c:167); label positions twice (opt.c:286, :1751) | about 26 walks per function at `-O1` | fuse unreachable + fallthrough into one sweep; compute the frame registers before compaction from `W.rr/ww`; `flags` reads `W` | M | medium (pass order is semantic) | `vcc-same.py`, `vbench.py gate`; ranking needs review B |
| Q-BR-10 | L1 L4 | near-duplicate | block split + liveness opt.c:248-344 vs dead.c:83-144 + flow/maskflow.c:35-85; dead sweep opt.c:964-986 vs dead.c:150-164; compaction opt.c:1895-1905 = dead.c:167-176 | two solvers; x86-64's is capped and gated (dead.c:6-8, :181), so most x86-64 functions get no dead-code pass at all | `codegen/rec/blocks.c` (split by op table), one solver, `em_rec_compact` | M-L | medium | `bench/vcc-rec-dead.py`, `bench/vcc-flow.py`, `vcc-same.py`, gate |
| Q-BR-11 | L7 | misplaced | in arm64's func.c but for all targets: `out_chunk` :6, `sym_header` :16 (tests `X86(c)` :67, :104), `ctor_entry` :118 (:128); callers lower.c:652,672,679, arch/wasm/codegen/wasm.c:712.  `dbg_row` arch/a64/codegen/stmt.c:1031-1044 = `x64_dbgrow` lower.c:573-586; `.loc` rows func.c:664-667 = :706-709 = x64 print.c:302-305 = :762-765; function line func.c:608-615 = print.c:215-221; `sym_needs_got` in arch/a64/codegen/value.c (x64impl.h:158); emitimpl.h:118 includes arm64's `ins.h` for every target | x86-64 and wasm depend on an arm64 file; four copies of the `-g` row rule | `codegen/funcout.c`: symbol header, ctor entry, chunk, debug rows and their printing | S | none (moves) + low | `vcc-same.py`, `vcc-dwarf.py` |
| Q-BR-12 | L1 | near-duplicate | inline-asm template: arch/x64/codegen/asm.c:600-664 vs arch/a64/codegen/asm.c:499-560 (scan: 265 + 105 tokens); operand-name lookup a third time asm.c:428-433 | two parsers of `%` escapes; `%=` already differs (x64 asm.c:619 `top << 32 \| serial`; arm64 `top * 1024 + asmk++`, limit 1024) | `codegen/asmtmpl.c`: the scanner calls back `(modifier, operand index)` and `(label k)`; per target only the modifier set and operand printing | S-M | low | `vcc-ctests.py`, kernel build (arm64), `vcc-x86.py`, `vcc-same.py` |
| Q-BR-13 | L1 | near-duplicate | arch/a64/codegen/tail.c:6-47 vs arch/x64/codegen/tail.c:5-48; `a64_tail_ref` :50-57 vs `x64_tail_ref` :50-59 | the park-then-assign rule twice | into `codegen/opt/tail.c` with three hooks (store to slot, load argument register, marker) | S | low | `vcc-ctests.py --suite inline`, `vcc-same.py` |
| Q-BR-14 | L9 | error policy | "function too large": abort a64impl.h:37-40, func.c:357-360, :409-412; `efatal` func.c:287, lower.c:52.  Unknown opcode: print.c:505-507 prints `// ?`, obj.c:329 emits nothing, x64 print.c:744 `efatal`.  OOM: `abort` opt.c:236,242,459,868,1746, rec/slots.c:13, unchecked lower.c:583 | a limit kills the process (vccd: inferred) or yields wrong code without a diagnostic | one `em_too_large(e)`; `efatal` in both defaults (R2.1 asked for `roles_sw`'s) | S | none | `bench/vcc-stress.py --vccd` |
| Q-BR-15 | L2 | pattern | `grow_to` opt.c:452-461 = `em_slot_grow` rec/slots.c:6-15 (exact); 13 `realloc`s opt.c:222-234; opt.c:864-869, :1741-1747; rec/slot-values.c:11-16; flow/maskflow.c:4-33; flow/graph.c:20-31; `malloc` per function func.c:343; two `calloc`s per call expression abi.c:370-371 | seven private growth idioms, each with its own failure rule; heap traffic per function and per call | one per-thread scratch arena reset per function (`include/common/arena.h`) | M | low | `vbench.py gate`, ASan suite |
| Q-BR-16 | L10 | dead | `I_MOVSP` is never emitted (ins.h:47; listed opt.c:97, unreachable.c:43, obj.c:85); wide.c:212-232 is `if (0)`; `em_slot_access`/`em_slot_detail` (rec/slots.c:77,111) and `em_flow_graph_free`/`em_flow_live_free` are called only by `bench/vcc-rec-slots.py` and `bench/vcc-flow.py`; `x64_slot` locals.c:56-59 is empty; `x64_x87` declared twice x64impl.h:153,162; `x64_effects` marked `unused` x64impl.h:151 with three users | readers must work out what is live; the slot header documents two modes, the product uses one | delete; keep test-only API behind the test's own define | S | none | builds, `vcc-rec-slots.py` |
| Q-BR-17 | L10 | stale doc | `twoaddr.c` named in x64impl.h:11,103 and lower.c:5; insn.c:1; opt.c:961-962 says reads and writes are "taken afresh", :983 uses the cached ones; orphan comment opt.c:424; the comment for `opt_regs` (opt.c:988-993) sits above `word_regs`; doc/PLAN-x86.md:79,193-214 lists `rec/live.c`, `rec/copy.c`, `rec/dead.c`, `rec/branch.c`, `peep.c`, `twoaddr.c`, `func.c`, none of which exist; AGENTS.md does not list `bench/vcc-rec-*.py`, `vcc-flow*.py` (ci/premerge.sh:74-89) | the documents describe a layer that was built differently | fix with the split (Q-BR-03) | S | none | reading |
| Q-BR-18 | L1 | duplicate | `logimm_ok` insn.c:9-40 and `a64_logimm` arch/a64/as/enc.c:102: two tests of "logical immediate"; obj.c:27 decides with one, obj.c:43 encodes with the other and does not check for -1 | if they ever disagree the encoder writes garbage | `logimm_ok(v, x)` = `a64_logimm(v, x) >= 0` | S | none | `vcc-same.py`, `bench/vas-enc.py` |
| Q-BR-19 | L1 | duplicate | `x64_asmlabel` asm.c:117-123 = `x64_lbl` print.c:92-98; register names asm.c:90-94 vs print.c:69-76; `DELETED` 0xff three times (opt.c:31, copies.c:9, dead.c:11); `I_MRS` lookup obj.c:543-549 = :682-688; debug adapters unreachable.c:4-14 = func.c:237-247 = lower.c:617-627; `rmask` typedef opt.c:25 and rec.h:29 | small, but each is a second place to edit | one definition each; the adapters go with Q-BR-01 | S | none | `vcc-same.py` |
| Q-BR-20 | L8 | global state | `FK` func.c:139-143 carries the frame shape from `finish_function` to `cs_base`/`cs_off`, `finish_direct`, `tail_expand`; `rawp`/`retreads` opt.c:39-40 must be set by each entry (:1026, :1067, :1859); `finish_direct`'s `ctor` packs constructor, destructor, x28 base and leaf in one int (func.c:150, :601) | hidden parameters; an entry point that forgets `rawp` reads the previous function's asm | `a64_frame` struct passed down; a cleanup context for `rawp`/`retreads` | S | low | `vcc-same.py` |
| Q-BR-21 | L4 | long function | `x64_print` 620 (print.c:196), `x64_asm` 614 (asm.c:214), `print_ins` 312, `finish_function` 287, `enc` 252 (obj.c:79), `x64_call` 241 (abi.c:327), `fold1` 203, `emit_function1` 195, `a64_replay` 191, `x64_cvt` 187 (fp.c:511), `x64_wbin` 160 | `x64_print` is prologue + interrupt frames + record switch + epilogue + item selection in one body | prologue, epilogue and the record switch as three functions; `x64_asm` by phase (select alternative, place operands, template, store outputs) | M | low | `vcc-x86.py`, `vcc-same.py` |
| Q-BR-22 | L7 L4 | misplaced | darwin.c is target-OS code in an arch directory; it is the inverse of `as_darwin_text` (as/as.c:3979, include/as/as.h:35-39), which names it; `defined()` darwin.c:31-42 rescans the whole text for each `.weak` line (:134); the TU text is copied once more to be rewritten (codegen/tu.c:683-694) | two rewriters to keep inverse by hand; quadratic in weak symbols | `platform/darwin/`; a round-trip check `as_darwin_text(darwin_text(t))`; see decision 5 | S | low | `bench/vcc-darwin-s.py` |

Notes on the findings.

**Q-BR-01, what stops arm64's passes from running on the shared records.**
Not the struct.  (a) Roles: arm64 needs "this field is written" for `rn`
(writeback) and `ra` (`ldp`), opt.c:34; `opdesc.regs` has `OD_DEF0` only
(rec.h:37).  (b) `sz` means 0/1 = w/x or log2 bytes on arm64 (ins.h:82) and
bytes on x86-64 (rec.h:17).  (c) arm64 hides slot tags in `rm/ra/cond/sh`
of `FRAME` records (emitimpl.h:307-308), fields a shared pass would read as
operands.  (d) arm64 caches roles and masks per record; x86-64 decodes on
demand, so the copy kernel has two modes (emitimpl.h:671-686).  (e) Flags:
arm64's passes keep NZCV out of the masks (`pure` excludes flag setters,
`flags` is its own pass); x86-64 uses bit 63 (dead.c:9).  (f) The field
overloads of R2.1: still 13 raw readers of `cond >= 8`, `CALL_ARGS`,
`LD_VOLATILE` in five files, no accessor.  Only (a) and (d) need a decision;
(b), (c), (e) stay in the target's op table.

**Q-BR-05.**  `x64_print` also decides the output form: per record it asks
`x64_obj_record` and then either appends the record to an `IT_X64` item or
prints text into an `IT_TEXT` item (print.c:307-323).  So for `-c` a function
is a mix of record items and text snippets that the assembler parses, and
prologue and epilogue are always text.  "Printer" hides that.

**Q-BR-10.**  The x86-64 cap is deliberate ("bounded") but it means the two
targets do not have the same pass set at `-O1`, which PLAN-x86.md promised.
The liveness kernel is also used by wasm; unifying on it means measuring it
on arm64 first (decision 3).

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `W`, `la rr ww ro blk bs be s0 s1` | opt.c:205 | liveness scratch (3.4) | `a64_live` and the field names of 3.4; `W` is also the walker state (`wkst *W`, arch/x64/codegen/tail.c:8) |
| `RT BT XT RBT` | opt.c:104-108 | opcode tables | columns of the op table |
| `SL`, `XF`, `FK` | opt.c:445, :616; func.c:139 | slot scratch, extension scratch, frame kind | `a64_slotwork`, `a64_extwork`, `a64_frame` |
| `xz xs XU XS zbits sbits` | opt.c:612-628 | known zero/sign width | `ext_zero_bits`, `ext_sign_bits`, `EXT_UNKNOWN` |
| `fold1 fclass fdefof fwriter fkept fdone fplain fgpr FSCR` | opt.c:1101-1221 | 3.4 | `fold_reader`, `fold_classify_def`, `sole_def`, `last_writer`, `inputs_unchanged`, `fold_commit`, `a64_rec_single`, `SCRATCH_REGS` |
| `m`, `grow`, `pure`, `dead`, `flags`, `fold`, `slots`, `copies`, `exts`, `backward`, `reads`, `writes`, `enc`, `reg`, `put`, `mem`, `word`, `defined`, `starts`, `reloc`/`relocs`, `section` | opt.c, obj.c, a64impl.h, darwin.c | file-static, unprefixed, in one unity TU with `cc`-facing emit code | `a64_` (and `darwin_`) prefix; `reloc` rewrites, `relocs` tests |
| `opt_function`, `opt_regs`, `opt_noframe` | opt.c | driver; two frame queries | `a64_cleanup`, `a64_used_regs`, `a64_runs_frameless` |
| `finish_direct`, `direct()` | func.c:152, emitimpl.h:1014 | "items for the object writer" | `finish_items`, `to_object()`; "direct emitter" elsewhere means "not VIR" |
| `a64_replay`, `x64_replay` | obj.c | items -> object | `a64_encode_items`; `replay` also names `frontend/c/replay.c` |
| `x64_obj_record` | arch/x64/codegen/obj.c:6 | "can this record skip the text path" | `x64_encodable` |
| `xopnd`/`XO_*` vs `XOp`/`XK_*`/`xo_*` | asm.c:11-23; obj.c:40-71 | inline-asm operand vs assembler operand | `asmopnd`/`AO_*` as arm64 has (arch/a64/codegen/asm.c) |
| `wk_x87val`, `interrupt_saved`, `tfarg`, `TQ..TW` | asm.c:141, print.c:194, fp.c:234-235 | x86-64 statics outside the `x64_`/`X_` rule of x64impl.h:12 | prefix |
| `BR_*` vs `RB_*`; `EM_FLOW_*` | opt.c:42; rec.h:38; rec.h:34 | branch kinds twice; `EM_FLOW_*` are reachability kinds, declared in `rec.h`, unrelated to `codegen/flow/` | one enum `REC_BR_*`; `EM_REACH_*` |
| `p_lbl` / `x64_lbl` | print.c:6; x64 print.c:92 | `.LBB<f>_<l>`: hex on arm64 (parsed back at obj.c:610-626), decimal on x86-64 | one `em_label_name` |

Words with two meanings in the area: **opt** (`codegen/opt/` AST recipes,
`arch/a64/codegen/opt.c` record clean-ups, `o->opt` the level); **values**
(`flow/values.c` expression identities, `rec/slot-values.c` cell contents,
`opt/sibling-values.c`, `switch-values.c`); **slot** (frame slot, `em_slot_*`
cells inside an allocation); **flow** (`codegen/flow/` data flow, `EM_FLOW_*`
reachability, `frontend/c/flow.c`); **print** (x86-64's also schedules object items);
**obj.c** (arm64: an encoder; x86-64: a translator to assembler calls;
`codegen/obj.c`: the item replayer).

## 6. Decisions needed

1. **One record type.**  (a) Alias the fields in one struct with an
   anonymous union and type the kernels, no source churn; (b) rename about
   500 field uses in `arch/a64/codegen/` and `codegen/vir/a64.c` to `r[i]`.
   Recommendation: (a) now; (b) never, the names are better.
2. **One op table per target**, generated enum and mnemonics, against today's
   switches plus a completeness check.  Recommendation: the table; it is
   what PLAN-x86.md specified and what R2.1 needs (an unknown opcode becomes
   a compile error, not a default).
3. **One liveness.**  arm64's array solver for both, or `em_flow_liveness`
   for both (wasm uses it already).  Needs review B's instruction counts for
   3a on sqlite at `-O1`; recommendation: shared block split first, solver
   by measurement.
4. **Does x86-64 get arm64's record passes at `-O1`** (slots, extensions,
   unbounded dead code), or is VIR the only optimizing path there and the
   direct x86-64 path stays as it is?  This decides whether Q-BR-10 is worth
   L.  Related: `fold` and `flags` run only for VIR (opt.c:1891); they belong
   with VIR's arm64 lowering in the file split, and R2.3's ownership table
   should be settled first.
5. **Expansion of arm64 pseudo records.**  (a) Plan helpers shared by
   printer, encoder, sizer and `writes()` (Q-BR-04); (b) an expansion pass
   that makes every record one instruction before output, which also ends
   R2.2 but adds a walk on the `-O0` path.  Recommendation: (a).
6. **Darwin `-S`.**  Keep the text rewrite (only `-S`, never the build's
   path) and move it to `platform/darwin/`, or print the dialect directly.
   Recommendation: keep and move; add the round-trip check.
7. **Limits and unknown opcodes** (Q-BR-14): `efatal` with the function's
   location everywhere.  Needs only a yes; it changes behaviour on inputs
   that abort today.

## 7. Not covered

- `arch/x64/codegen/vir.c` (excluded); `arch/a64/codegen/asm.c` outside lines
  440-600; `codegen/vir/a64.c` was only grepped for the records it emits.
- wasm's users of `em_slot_*` and `em_flow_*` (`arch/wasm/codegen/slots.c`,
  `flow.c`) and `codegen/opt/sibling-values.c` were not read: the claims about
  them are from grep.
- `as_x64_insn` and `as_darwin_text`: other areas; the cost of the mnemonic
  lookup in Q-BR-05 is inferred.
- No measurement: the walk counts of section 3 are read from the code.
  Whether a walk matters is review B's question.
- The contract harnesses `bench/vcc-rec-*.py`, `bench/vcc-flow*.py` were
  found by grep and in `ci/premerge.sh`, not read or run.
- That an `abort()` in the emitter takes vccd down (Q-BR-14) is inferred.
- The clone scan was run for the area
  (`bench/quality.py --clones 999 --only arch/a64/codegen/ arch/x64/codegen/ codegen/rec/ codegen/flow/`);
  its runs inside the area are in Q-BR-06, 07, 12, 15, 19; the runs against
  `codegen/walk/` belong to the walker reviews.  The scan's
  `fp.c:330-343 = abi.c:538-551` and `wide.c:110-124 = abi.c:536-550` are
  Q-BR-07: real copies, with two more in `lower.c`.
