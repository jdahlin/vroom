# Quality review: arm64's AST walker against the shared walker (2026-11-13, main at 2e6d3e73)

Paths relative to `src/`.  a64 = `arch/a64/codegen/`, wk = `codegen/walk/`.  Read in full: a64 `expr stmt call
arith builtin lval value cond fp abi bind label asm` and the six recipe files, `a64impl.h`, `ins.h`; wk `walk.h
proto.h expr.c stmt.c` and its four recipe files.  Nothing was built or run except `bench/quality.py` and grep.

## 1. Summary

arm64 has a complete AST walker of its own (9.5 k lines) next to the shared one (5.9 k lines) that x86-64 and
the VIR builder use.  Of 36 concepts compared, about a quarter have the same structure with renamed calls
(five small helpers are verbatim), a third are the same algorithm written differently, a quarter are
genuinely different, and three exist on one side only.  The scan's 5.6 k tokens (runs of 40+) are the
visible tenth; the rest is the same ladder of decisions written twice.  Three things matter most:

1. **The copies have already drifted** (Q-WA64-04): VLA re-declaration, inline-asm label numbering, bit-field
   access unit, `memcpy` thresholds, error-attribute calls, and features only one side has (stack protector,
   auto-zero, `__builtin_setjmp` on wk; NEON vector operators, known values across labels on a64).
2. **About 800 lines of target-independent code live in `arch/a64/codegen/`** and are called from wk, x64, wasm
   and VIR (`note_ref`, `side_effects`, `cond_fold`, `ael_slot`, `dbg_row`, `sym_needs_got`, ...).  Moving
   them is free of risk and is step one whatever is decided about the walkers (Q-WA64-02).
3. **arm64 cannot move onto `LO()` as it is** without changing its output: values in register pairs, floats as
   bits in general registers, results joined in registers, flags, bit-field instructions, lazy extension and
   the retry on register pressure have no place in the interface (section 4, "Interface gaps").  The move is a
   change of generated code, to be decided by a measured pilot, not a refactoring provable by `vcc-same.py`.

## 2. Inventory

| File | Lines | What it does | Better name | Header |
|---|---|---|---|---|
| `a64impl.h` | 182 | record constructors (`put op3 opi mem ...`), `reg` (prints a register name), `last_cset`, fmov encodings; the float-representation comment | `a64rec.h` | good, but the FP comment (155-162) is stale: says `_Float16` and 128-bit long double are unsupported; `fp.c` does both |
| `ins.h` | 90 | the `ins` record, `I_*`, `C_*`, pseudo registers | ok | good |
| `value.c` | 387 | temporaries (`treg`: retry by longjmp), frame slots and slot tags, `toreg`, sized load/store with adrp+lo12 folding, `copy_mem`/`zero_mem`; `sym_needs_got` (clang's GOT policy, not arm64) | ok | label |
| `bind.c` | 191 | reference log and work queue (`note_ref`, `claim_ref`: target independent), per-symbol bindings, `is_local_var` | `ref.c` (shared) + `bind.c` | label |
| `label.c` | 159 | labels, reachability, known-value join over edges, branch records | ok | label ("emitting records": those are in `a64impl.h`) |
| `abi.c` | 380 | AAPCS64/Apple classification, small and bool vector returns, spill around calls, `call_fixed` | ok | label |
| `lval.c` | 683 | lvalues from nodes, pointer + index, bit-fields, `_Atomic` load/store, typed `load`/`store` | split `bitfield.c` | label |
| `arith.c` | 853 | `extend`, constant folding, compares, truth tests, casts, integer operators, 128-bit pairs, and 300 lines of `_BitInt` limb code (`a64_big*`, 552-853) | split `bigint.c` | label |
| `cond.c` | 299 | 227 lines of target-independent condition folding (`same_pure`, `irange`, `cond_fold`), then `gen_cond` | `codegen/opt/condfold.c` + `cond.c` | label |
| `fp.c` | 498 | float/double as bits in GPRs through v16/v17, long double through libcalls, host folding | ok | label |
| `expr.c` | 1345 | `gen` and its cases; vector lanes (284-439); `_Atomic` compound assignment | split `vector.c` | label |
| `stmt.c` | 1249 | initializers, cleanups, blocks, switch, return, VLAs, declarations, `-g` line rows (992-1051, shared), statements | split `switch.c`, `vla.c`; `dbg_*` to `codegen/` | label |
| `call.c` | 854 | calls, known-value tracking (349-410), inlining (418-642), `memcpy` inline, `gen_call` | split `inline.c` | label |
| `builtin.c` | 1479 | `side_effects` (shared), `dry`, overflow builtins, va_list, atomics, `gen_builtin` (595 lines) | split by family (Q-WA64-08) | label |
| `asm.c` | 710 | `kbits` (known bits of an AST expression, 146 lines, shared), `gen_asm` (473 lines) | ok | label |
| `sdivconst.c udivconst.c sdivpow2.c mulconst.c switch-values.c switch-bitset.c` | 17-61 | one recipe each from `codegen/opt/`, in arm64 records | ok | good (`sdivpow2.c`: none before the include; `switch-values.c` is indented with one space) |

**What the module is (draft opening comment).**  arm64's code generator walks the typed AST once per function
and appends `ins` records; `func.c` then cleans up, lays out and prints or encodes them.  Invariants:
- A value is a `val`: constant, symbol address, one temporary (`V_REG`) or a pair (`V_REG2`: 128-bit integers
  and binary128 long double).  Floats and doubles are their bit patterns in a general register.  Aggregates,
  vectors and `_BitInt` over 128 bits are the address of their bytes.
- Whoever holds a `val` owns its temporaries and must `val_free` it; an operation returns its result in its
  left operand's register.  Every statement leaves `e->live` as it found it (`VCC_LEAKCHK`).
- A narrowing cast does not extend: the high bits of a register are undefined above the value's width until a
  consumer needs them (`arith.c:296,345`, `udivconst.c:23`).  (wk keeps registers extended, `walk/expr.c:828`.)
- `e->reach` is false after a jump; every record constructor is then a no-op, and a label that nothing refers
  to does not make code reachable again.  Dead branches are walked but emit nothing.
- Frame slots are bump-allocated and released by resetting `e->foff` at the end of the statement
  (`stmt.c:1248`), compound literals excepted (`cl_top`).
- Out of temporaries, `treg` longjmps and `emit_function` generates the function again, up to three times
  (`value.c:15`, `func.c:986-998`).  Code that can ask first (`a64_udivconst_ok`) does.
- x16 is scratch inside one helper, never across a call to another; x17 is the backend's for large offsets
  (`value.c:359`) and is used only where no frame access comes between (`stmt.c:547,583`, `call.c:704`).

## 3. Passes and data flow, and the concept matrix

One walk per attempt: `emit_function1` -> `gen_stmt` -> `gen`/`gen_lval`/`gen_cond`, records appended in
order.  Repeated or extra walks: (1) the retry: a whole-function walk again without register variables, and a
third time with spilling (`func.c:990-994`); (2) `dry()` (`builtin.c:80`): `__builtin_constant_p` generates
its operand, inlined calls included, and throws the records away; (3) small AST sub-walks per node:
`side_effects` (`builtin.c:14`), `cond_fold` (`cond.c:184`), `collect_cases` (`stmt.c:312`), `tiny_ok`
(`call.c:418`), `kbits` (`asm.c:40`), plus the `em_*` probes at the top of `gen_binary` (`expr.c:447-451`:
`cond_fold`, `em_cmpbounds`, `em_repeated_integer` on every binary node).  The shared walker adds
`wk_branch_entry` (`walk/stmt.c:520`), `wk_isconst` (`walk/expr.c:2292`), `wk_ival` (`walk/expr.c:3508`) and
`wk_vla_scan` (`walk/stmt.c:438`, once per function and per inlined copy).  "Is this expression a constant
here" is answered by six mechanisms: `cc_eval`, `em_const_truth`, `cond_fold`, `wk_isconst`+`wk_ival`,
`dry()`, `kbits` (Q-WA64-05).

Verdicts: **V** verbatim, **R** same structure with renamed calls, **A** same algorithm written differently,
**D** genuinely different (why), **1** on one side only.

| Concept | arm64 | shared walker | Verdict |
|---|---|---|---|
| Integer binary operators | `expr.c:441 gen_binary`, `arith.c:325 arith` | `expr.c:1373 wk_binary`, `976 wk_arith` | first 25 lines R (scan 107 tokens); the ladder (commute, identity, xor-not, divide recipes, pointer +-, pointer difference) A; a64 picks immediates, logical immediates and shifted operands itself, wk leaves that to `LO(bin)` |
| Constant folding | `arith.c:23 fold_int_bin`, `92 em_is_cmp` | `expr.c:86 wk_fold`, `127 wk_fold_cmp`, `122 wk_is_cmp` | `em_is_cmp` V; fold A (two signatures: operand and result type + ok flag, against one type + bool) |
| Comparisons | `arith.c:98 emit_cmp`, `116 emit_cmp2`, `expr.c:554-591` | `expr.c:1270 wk_cmpv`, `1411-1425`, `LO(cmp)`, `LO(cmpbr)` | D: a64 returns a condition code and keeps the flags; `cset` is undone by popping the last record (`a64impl.h:117 last_cset`, `cond.c:280`) |
| Conditions, `&&` `\|\|` `!` | `cond.c:229 gen_cond`, `arith.c:142 test_zero`, `expr.c:243 gen_logical` | `expr.c:3615 wk_cond`, `1457 wk_condval`, `3598 wk_condfold` | skeleton R; folding A (a64 generates and looks at the `val`; wk evaluates the AST first and tracks `cjump`); the 0/1 value D (register join against a frame slot) |
| `?:` | `expr.c:160 gen_cond_expr`, `123 gen_cond_spill` | `expr.c:1485 wk_ternary` | D: a64 joins in a fixed register (`move_to`), a slot only on the third try; wk always stores and reloads |
| Assignment, compound | `expr.c:863 gen_assign`, `713 assign_op` | `expr.c:1687 wk_assign`, `1708 wk_assign_`, `1644 wk_optype` | A (a64 reads the promoted type from the node, wk recomputes it) |
| `++` `--` | `expr.c:957 incdec_op`, `1051 gen_incdec` | `expr.c:1809 wk_incval`, `1919 wk_incdec` | R (scan 70); a64 has one branch per representation |
| Bit-field load and store | `lval.c:351-445`, `488-513`, `630-664` | `expr.c:433-695` | `bf_piece` = `wk_bf_piece` V; split loops R; ordinary fields D (ubfx/sbfx and bfi, 2-3 records, against shifts and masks through `LO(bin)`, 6+ operations; the access unit differs too) |
| Lvalues, addressing | `lval.c:76 gen_lval`, `bind.c:132 local_lval`, `value.c:198-353` | `expr.c:269 wk_lval`, `143 wk_fromval`, `173 wk_local` | member, string, deref, bit-lane index R (scan 110); locals D (`B_CONST`, `B_REG`, `B_LREG`, `L_GLOBREG` against `LO(local)`); addressing D (adrp+lo12 folded into the load `value.c:274`, shifted index `lval.c:46`) |
| Struct copy, zeroing | `value.c:355 copy_mem`, `373 zero_mem`, `stmt.c:6 zero_lval`, `call.c:699 copy_pairs` | `LO(copy)`, `LO(zero)`, `expr.c:1607 wk_putmem` | the interface fits; a64's bodies are the two `LO` operations |
| Initializers | `stmt.c:32-171` | `stmt.c:167-325` | `is_zero_init` = `wk_zero_init` V; `init_elems`/`wk_elems` R (scan 318 tokens); string bytes as immediates on a64, copied on wk |
| Casts, conversions | `arith.c:242 cast`, `expr.c:1168-1271` | `expr.c:795 wk_conv`, `835 wk_cast` | `CK_BITS/SPLAT/UNION/VBITS/DECAY` R (the bit-field byte loop near verbatim, scan 268); integer conversion A with a different invariant (lazy against kept extension) |
| Floating point | `fp.c` (all) | `LO(fbin fneg fcmp cvt fconst fbits fsqrt)`, `expr.c:2686 wk_fpconst` | D: bits in GPRs through v16/v17, long double as a pair through libcalls, host folding in `fp.c:183-191`; wk assumes float registers (`LO(ftreg)`) and long double by address |
| Calls, arguments | `call.c:764 gen_call`, `96 do_call`, `23 park`, `abi.c:220 classify` | `expr.c:2134 wk_call`, `2040 wk_argslot`, `LO(classify)`, `LO(call)` | D: wk stores every argument in a slot and hands slots over; a64 keeps register arguments in temporaries and moves them (`call.c:136-144`), parks under pressure only (`call.c:810`) |
| Returns | `stmt.c:603 gen_return` | `stmt.c:1130-1221`, `LO(ret)`, `LO(retrec)` | A; the interface fits |
| `va_start`, `va_arg` | `builtin.c:403`, `435`, `966-978` | `expr.c:2323`, `3230-3250`, `3436-3445`, `LO(va_start)`, `LO(va_arg)` | the interface fits; a64's code is the body of the two `LO` operations |
| alloca, VLAs | `builtin.c:1090-1117`, `stmt.c:740-886` | `expr.c:2979-2995`, `1097-1246`, `stmt.c:438`, `936-973` | `vla_fix` R (scan 94 exact); declaration A; scope exit D (a cleanup-stack entry on a64; block-end restore, a `vla[]` stack and zeroed saved sp on wk) |
| Switch | `stmt.c:312-601`, `switch-bitset.c`, `switch-values.c` | `stmt.c:575-809`, `switch-values.c`, `LO(switch_bitset)`, `LO(switch_table)` | case collection R (scan 168); tree A (a64 reuses the flags for equality, `stmt.c:365`); strategy already shared (`codegen/opt/switch.c`); value table R (scan 83); jump table in records on a64 (`I_JT`) |
| Division, multiplication by constants | `arith.c:361-378`, `sdivconst.c`, `udivconst.c`, `sdivpow2.c`, `mulconst.c` | `expr.c:1039-1054`, `walk/sdivconst.c`, `udivconst.c`, `sdivpow2.c` | A: recipes shared in `codegen/opt/`, adapters written twice; the four `_ok` predicates are one function |
| Loops, break, continue | `stmt.c:1103-1238` | `stmt.c:994-1129` | R (scan 142); a64 adds the frame mark (`keep_cl`) |
| Labels, goto, reachability | `label.c`, `stmt.c:1194-1218` | `stmt.c:8 wk_label`, `822 wk_goto`, `849-903` | D: a64 joins known values over forward edges and decides reachability by reference count, with three label kinds; wk forgets everything at a label and needs `wk_branch_entry`/`wk_stmt_entries` to keep labelled code in dead branches |
| Declarations, cleanups | `stmt.c:909 gen_decl`, `894 dead_decl`, `175-223` | `stmt.c:357 wk_decl`, `330 wk_cleanups` | A (a64 inlines the cleanup function; goto uses prepass depths against a scope tree) |
| Statement expressions | `stmt.c:238 gen_block(want)` | `expr.c:2260 wk_stmtexpr` | A; a64 copies an aggregate value out of the block's slots and handles labels before the value; wk refuses a value that is neither a register nor a constant when cleanups run (`expr.c:2280-2283`) |
| Inlining | `call.c:418-642` | `stmt.c:1274-1531` | `tiny_ok` = `wk_tiny_ok` V (scan 226, the longest run); policy A (depth 48 against 16; `mf[]` refold and `SA_MUSTFOLD` only a64; VIR sizes only wk); a64 keeps parameters and the result in temporaries |
| Known constants | `B_CONST`, `kv_track`/`ael_slot` (`call.c:349-398`), `kv_edge`/`kv_join` (`label.c:23-77`), `dry` | `kgen`/`kval`, `W->ael` (`expr.c:211-255`), `pk`, `wk_isconst`, `wk_ival` | D in design; wk also calls a64's `ael_slot` and writes `e->kval` (`expr.c:1780-1788`, `3361-3369`): two tables for one fact |
| `_Atomic` objects | `lval.c:255-346`, `expr.c:775 atomic_rmw` | `expr.c:720-751`, `1619-1637`, `1836 wk_atomic_rmw` | A |
| `__atomic_*`, `__sync_*` | `builtin.c:550-843` | `expr.c:2355-2534`, `LO(atomic)` | argument handling A; primitives D (LSE `ld<op>al` or LL/SC per operation; wk has six operations and CAS loops for the rest) |
| Other builtins | `builtin.c:885-1479` | `expr.c:2848-3268` | shuffle/convert R (scan 67); expect, assume, object_size, lock_free R; bit operations A (a64 folds constants and has every instruction, wk `LO(bitop)` and generic code); FP class A (wk's `wk_fpattern` is the better form); overflow A; rotates, ARM hints, `arm_rsr`, `flt_rounds`, prefetch 1 (a64); `fpclassify`, `floor`->`floorf` 1 (wk) |
| Inline asm | `asm.c:238 gen_asm` | `LO(asm)` = `arch/x64/codegen/asm.c:214 x64_asm` | template scanner near verbatim (scan 265+105); operand phases same structure; register model D |
| Complex | `expr.c:1303-1320` | `expr.c:3472-3493` | R |
| Vectors | `expr.c:284-439`, `871-895`, `builtin.c:893-929`, `abi.c:90-173` | `expr.c:1284-1371`, `1726-1735`, `2916-2955` | lane loops R (scan 186); NEON fast path (`gen_vbin`, raw text) and small/bool vector returns 1 (a64) |
| `_BitInt`, 128-bit | `arith.c:443 arith2`, `552-853 a64_big*`, `expr.c:689`, `1078` | `LO(big*)`, `LO(w*)`, `expr.c:1666`, `3291` | `gen`/`wk_gen` wrapper R (scan 72); `a64_big*` already has `LO`'s signatures (`a64impl.h:177-182` = `proto.h:106-109`); 128-bit D (pair against by address) |
| Volatile | `lval.c:450-459`, `537-544` | `expr.c:698-708`, `730-790`, `stmt.c:304-322` | R (both raise `e->novtag`) |
| setjmp | none (a library call; `BI_SETJMP` would hit `builtin.c:1476`) | `expr.c:2884-2898`, `LO(setjmp)`, `LO(longjmp)` | 1 (wk) |
| Tail calls | `stmt.c:610` -> `a64_tail_return` (`tail.c`, not read) | `stmt.c:1131 LO(tail_return)` | the interface fits; policy shared (`codegen/opt/tail.c`) |
| Stack protector, auto-zero | none (inferred: no `sprot`/`autozero` in a64) | `stmt.c:405-418`, `1581`, `expr.c:180` | 1 (wk) |

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-WA64-01 | L1 | duplicate | section 3: 36 concepts, most of them V, R or A | every language feature and every fix is written twice (three times with wasm); the C++ front end will add nodes to both | arm64 onto the shared walker, after the interface grows (below); or a documented exception with the shared policy extracted | L | high: it is the kernel path | not `vcc-same.py` (output changes); `vcc-ctests`, kernel boot, PostgreSQL, `vbench.py gate` with a new budget |
| Q-WA64-02 | L7 | misplaced | `bind.c:4-59,185`; `builtin.c:6-72`; `cond.c:1-227`; `stmt.c:992-1051`; `asm.c:40-185`; `call.c:5-18,342-410,418-444,645-665`; `value.c:219-251`; `lval.c:6-21,69-74`; `arith.c:23-95`; users: wk (`note_ref` x9, `side_effects` x10, `ptr_to_vla` x7, `is_local_var` x7, `walk_fn` x4, `cond_fold` x3, `ael_slot` x3), x64 (`sym_needs_got` x6, `dbg_remap` x5), wasm (`side_effects` x7) | the shared walker cannot be read or built without arm64's directory; `walk.h:17-19` says so itself | `codegen/ref.c`, `codegen/opt/condfold.c`, `codegen/opt/effects.c`, `codegen/dbgrow.c`, `codegen/opt/kbits.c`, `codegen/opt/known.c` | S (moves only) | none | `vcc-same.py --base`, unity and `VCC_UNITY=` builds |
| Q-WA64-03 | L1 | duplicate | `stmt.c:32` = wk `stmt.c:167`; `call.c:418` = wk `stmt.c:1274`; `arith.c:92` = wk `expr.c:122`; `lval.c:351` = wk `expr.c:457`; `stmt.c:312` / wk `stmt.c:575`; `expr.c:7` / wk `expr.c:3273`; strcmp of literals three times: `call.c:668`, wk `expr.c:2174-2187`, `codegen/opt/control.c:24`; `_chk` simplification `call.c:755` / wk `expr.c:2161-2172`; TLS model `lval.c:106-116` / wk `expr.c:307-318`; shuffle/convert lanes `builtin.c:893-929` / wk `expr.c:2916-2955`; `a64_udivconst_ok` = `a64_sdivconst_ok` = `wk_*_ok` (four bodies) | a second copy to keep in step, each 5-40 lines | one `em_` function each in `codegen/opt/` | S each | low | `vcc-same.py --base` (both targets) |
| Q-WA64-04 | L1 | near-duplicate | list below | the two walkers disagree on behaviour; each item is a bug on one side or a decision nobody wrote down | decide each; a test in `test/vcc/` per item | M | medium | the new tests on arm64, x86-64 and VIR |
| Q-WA64-05 | L4 | redundant walk | six constant predicates (section 3); known values: a64 `B_CONST` + `kval` + `ael`; wk `kgen/kval` + `W->ael` + `pk` and a64's `ael_slot` (`walk/expr.c:1693-1704` and `1780-1788` both record the same assignment) | one fact in two tables in the shared walker; a new folding rule must be taught to several predicates | one known-value table with one join rule, in `codegen/`; one "constant here" query on top of it | M | medium (the kernel's folds) | `vcc-same.py`, kernel build (no surviving `error()` call) |
| Q-WA64-06 | L4 | redundant walk | `value.c:15-16`, `52-53`; `func.c:986-998`; workarounds `udivconst.c:9`, `sdivconst.c:4`, `switch-values.c:8` | a function that runs out of temporaries is walked two or three times; every new recipe needs a predicate so that it does not provoke the retry | count retries first (a `vperf` counter); then either ask before allocating (wk's `wk_short`, `walk/expr.c:1249`) or keep the retry and say so in the interface | S to count | low | the counter over a kernel build |
| Q-WA64-07 | L1 | pattern | "Interface gaps" below | without them arm64 on `LO()` generates more records and slower code | grow `proto.h` | M | medium | per operation: x86-64 and VIR outputs unchanged (`vcc-same.py`) |
| Q-WA64-08 | L4 | long function | `builtin.c:885-1479` | 47 case groups in one function; the binary128 "sticky low word" sequence is written three times (`1339-1347`, `1376-1382`, `1411-1418`) | one function per family, table-dispatched; see below | S | low | `vcc-same.py` |
| Q-WA64-09 | L1 | near-duplicate | `asm.c:500-555` = `arch/x64/codegen/asm.c:601-664`; `asm.c:262-267` / x64 `288-295` | the template grammar exists twice and already differs (list below) | `codegen/asmtmpl.c`: the scanner calls back for an operand (index, modifier) and a label | S | low | `vcc-same.py`, the 19 `asm*` tests in `test/vcc` |
| Q-WA64-10 | L6 | misnamed | 236 file-scope functions in the files read, 24 with `a64_`; 18 unprefixed `static inline` in `a64impl.h` are visible in every emit file | `mem`, `mov`, `reg`, `put`, `word`, `slot`, `load`, `store`, `cast` read as generic and collide in meaning with `wk_mem`, `wk_put`, `LO(load)`, `LO(store)` (section 5) | prefix rule in section 5, by script | M (about 1900 call sites) | low, but touches every open arm64 branch | `vcc-same.py`; object files identical |
| Q-WA64-11 | L4 | mixed file | section 2: `stmt.c` (8 jobs), `call.c` (4), `builtin.c` (7), `arith.c` (`_BitInt`), `expr.c` (vectors), `lval.c` (bit-fields, atomics) | nobody finds the switch lowering in `stmt.c` or inlining in `call.c` | the splits of section 2, `git mv`-style moves only | S | none | unity and file-by-file builds |
| Q-WA64-12 | L1 | pattern | byte-chunk loop six times: `value.c:361`, `378`; `stmt.c:24`, `124`; `call.c:710`, `744`; "address of a new slot" over ten times: `expr.c:1247`, `1262`, `1317`; `stmt.c:287`, `672`; `call.c:258-290` (x5); already a helper for `_BitInt` only (`arith.c:558 a64_bigslot`); records filled by hand where `opi` exists: `stmt.c:395-399`, `853-856`, `builtin.c:1100-1103` | repetition; the magic `cond = 0xee` ("add the outgoing area", read at `func.c:574`) is written in two places without a name | `a64_chunks`, `a64_slot_addr`, an `I_ADDI` flag constant in `ins.h` | S | low | `vcc-same.py` |
| Q-WA64-13 | L10 | stale doc | comments above the wrong function: `cond.c:6` (describes `gen_cond`, sits on `strip_shift0`), `cond.c:168`, `builtin.c:74` (on `dry`: "without emitting anything"), `asm.c:30` (clobber comment on `kbits`), `stmt.c:990-991`, `call.c:71-74`; `lval.c:534` ("unless keep; returns v": no such parameter, returns void); `a64impl.h:155-162`; `emitimpl.h:748-774` lists `put`..`symop` under "label.c" and `jump`/`bcond` under "builtin.c: atomics" | a reader trusts the comment | fix in place | S | none | review |
| Q-WA64-14 | L10 | dead | unused: `atomic_target`'s `a` (`expr.c:758`), `popcount`'s `w` (`builtin.c:113`), `lbase`'s `size` (`value.c:198`), `call_fixed`'s `a2` (`abi.c:361`), `yt` (`expr.c:379,416`), `k` (`expr.c:212-218`); no-ops: `expr.c:741` (`? pt : pt`), `stmt.c:594`; `binding()` (`bind.c:63`) is `&e->b[s]`; `cond.c:280-282` repeats `last_cset` by hand; `stmt.c:130` writes x16 as `R_X17 - 1` | noise | delete | S | none | `vcc-same.py` |
| Q-WA64-15 | L9 | error policy | `a64impl.h:37-40` and `stmt.c:414` abort; `stmt.c:793` is a hard limit of 64 VLA sizes per function (wk has none); `asm.c:257` 64 operands; `getenv("VCC_LEAKCHK")` on two paths (`value.c:22`, `stmt.c:1066`); wk messages say "x86-64:" in code VIR runs for arm64 (`walk/stmt.c:127,1248`, `walk/expr.c:24,3267`) | limits and wording differ per copy | `efatal` everywhere; limits in one table | S | low | `vcc-stress.py` |

**Q-WA64-04, where the copies disagree** (found by reading; none was run, so each is "inferred"):
1. A VLA declaration reached again by `goto`: wk frees the previous instance (`walk/expr.c:1188-1199`); a64
   allocates again (`stmt.c:828-886`).
2. Inline-asm label operands by number: x64 counts the `+` outputs (`nops + nplus + k`, x64 `asm.c:637`), a64
   does not (`asm.c:535`, `548-553`); a64 has no `{a|b}` dialect alternatives and no constraint alternatives.
3. `%=`: `top * 1024 + asmk`, at most 1024 per function (`asm.c:510-515`); x64 `top << 32 | serial`.
4. Bit-field access unit: bytes covering the field (`lval.c:491`) against the declared type's aligned unit
   (`walk/expr.c:437`); it shows on volatile fields.
5. `memcpy`/`memset` inline: up to 128 bytes, ldp/stp, any byte pattern (`call.c:718-753`); up to 64 bytes,
   zero only (`walk/expr.c:2091`).
6. A surviving `error`-attribute call: reported once and compilation goes on (`call.c:164-171`); fatal on the
   first (`walk/expr.c:2205`).
7. `main` falling off its end: any function named `main` (`func.c:971`); file scope, not static
   (`walk/stmt.c:1535`).
8. Statement expression with cleanups whose value is a symbol address: handled (`stmt.c:303-309`); refused
   (`walk/expr.c:2280-2283`).
9. Inlining at `-O0`: a64 inlines small `inline` functions (`call.c:464-478`, the kernel needs it); wk only
   `always_inline` (`walk/stmt.c:1327`).
10. TODO.md:221 (unary minus on `__int128` vector lanes) is a bug recorded for a64 only; `expr.c:383` now
    looks fixed, the entry may be stale.

**Interface gaps: what arm64 needs that `proto.h` cannot say today.**
1. Register pairs.  The walker has no `V_REG2`: `wk_free` frees one register (`walk/expr.c:62`), and a 128-bit
   value is by address or unsupported (`wk_scalar`, `expr.c:27`).  arm64 either moves `__int128` and long
   double to memory (more code, changed output) or the walker learns pairs.
2. A result in a chosen register.  `?:`, `&&`/`||` as values, inlined results and parameters go through frame
   slots in wk; arm64 needs `LO(join)` (move a `val` into a given temporary) or accepts the slots.
3. Flags.  `LO(cmp)` gives 0/1 and `LO(cmpbr)` branches; nothing lets a second branch reuse a compare
   (`stmt.c:365`) or invert a pending `cset` (`expr.c:577-580`, `666`).  Could stay a peephole inside a64's
   `LO(brz)`, as `test_zero` does today.
4. Bit-field extract and insert (`LO(bfx)`, `LO(bfi)`), a scaled index (`LO(index)(p, i, scale)`), storing
   zero without a register (`store_n(ZR)`), a store of a constant: all spelled as `LO(bin)` chains today.
5. Label kinds and edges.  `LO(label)` has one kind; a64 needs loop head, forced (jump back, address taken)
   and plain, and it joins known values on every edge (`kv_edge` in `jump`, `bcond`, `cbz`, `tbz`).  The join
   must move up into the walker, or the walker keeps forgetting at labels and the kernel's folds are rechecked.
6. Arguments.  `wkarg` is SysV-shaped (`cls[2]` of INT/SSE, `x87`, `up`); AAPCS64 needs up to four FP
   registers (HFA), by-reference copies, x8, bool-lane vectors, Apple's stack packing, and a "call begins"
   point for the register counters (`e->nsrn`, `call.c:122`).  Passing values, not only slots, is needed to
   keep `call.c:136-144`.
7. The retry.  `wk_function` treats any longjmp as a fatal error (`walk/stmt.c:1575`); a64's `treg` uses 2 for
   "again".
8. `dry()`: a mark/rewind of the target's state (`builtin.c:80-108` saves 13 fields) for
   `__builtin_constant_p`; wk answers from the AST (`wk_constant_p`, `walk/expr.c:2837`), which sees less.
9. Policy that must be reconciled first, because the kernel build depends on it: inlining at `-O0`, `mf[]`
   refold (`call.c:81-93`), `SA_MUSTFOLD`, the dropped `error()` call.
10. Floats: workable if `LO(ftreg)` may return a general register on arm64; not checked against every use of
    `wk_freg`.

**What arm64 does better at `-O0` and a move must not lose** (the 2-second kernel path): no AST pre-walks for
dead code (reachability is one flag test per record); bump-and-reset slots against a linear search of up to
1024 free slots plus an `LO(slot)` call per temporary (`walk/stmt.c:85-108`); constants and addresses as
arguments are materialised straight into x0-x7 (`call.c:54-69`), not stored and reloaded; bit-fields in 2-3
records; global loads in 2 records (`value.c:274-279`); `?:` and `&&` without a store and a load; known
values that survive forward labels; never-written locals bound to their constant at any `-O`
(`stmt.c:936-946`).  None of this was measured here.

**Q-WA64-08, the jobs inside `gen_builtin`:** vector shuffle/convert (893-929); `constant_p` (930); values
passed through (943-958); va_list (959-978); traps and ARM hint words (979-1009, 1034-1037); rotates
(1010-1033); frame and return address (1038-1064); object size (1065-1076); overflow and carry (1077-1080,
bodies at 140-401); prefetch, alloca (1081-1117); atomics dispatch and fences (1118-1140); system register,
rounding mode (1141-1166); bit counting with constant folding (1167-1263); FP comparisons (1264-1328); FP
classification (1329-1468); the front end's folds (1469-1477).  Proposed files: `builtin-bits.c`,
`builtin-fp.c`, `builtin-atomic.c` (with 550-843), `builtin-overflow.c`, `builtin-va.c`.

**Q-WA64-09, the jobs inside `gen_asm`:** clobber set and temporaries saved or reserved (261-282); output
operands (284-326); input operands with tied inputs, known-bits immediates, memory fallbacks (328-439);
output registers (441-461); fixed registers (463-468); asm-goto trampolines (470-471); vector operands loaded
(473-490); template expansion (492-600); the read/write masks for `opt.c` (601-637); vector and fixed outputs
back (638-660); stores on the fall-through path and once per trampoline (665-692); frees and label edges
(693-709).  Shared with `x64_asm`: the scanner (escape handling, `%[name]`, `%N`, label lookup: 370 tokens
near verbatim), the clobber loop, the operand-phase order.  Not shared: register naming, constraint letters,
x87, flag outputs.

**An incremental order, least risk first** (each provable byte for byte unless said):
1. Q-WA64-02: move the target-independent functions out of `arch/a64/codegen/`.
2. Q-WA64-03: the pure helpers, one at a time.  `fold_int_bin`/`wk_fold` last of these: add a differential
   unit test over every operator, type and edge value before merging the two signatures.
3. Q-WA64-09: the asm template scanner.
4. Q-WA64-11 and -14: splits and dead code (moves only).
5. Q-WA64-05: one known-value table, first inside the shared walker (`W->ael` against `ael_slot`).
6. The pilot of decision 1: arm64 implements `LO(label, jump, brz, cmpbr, load, store, treg, tfree, toreg)`
   and the shared walker runs loops, `if`, `goto` and `break` for it behind an environment knob.  Output will
   differ (label numbering, known values at labels); the proof is the test wall, record counts and the gate.
7. Recipes (`a64_*divconst`, `a64_sdivpow2`, `a64_switch_*`) as `LO` operations, then expressions; calls and
   inline asm last.

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `mem` | `a64impl.h:129` | appends a load/store record | `a64_ldst`; "mem" also means "held by address" (`wk_mem`) |
| `put` | `a64impl.h:33` | appends an empty record | `a64_rec`; `wk_put` stores to an lvalue |
| `reg` | `a64impl.h:6` | prints a register name into a `tbuf` | `a64_regname` |
| `word` | `a64impl.h:163` | appends an encoded instruction | `a64_word` |
| `mov`, `movi`, `op3`, `op3s`, `op4`, `opi`, `opbf`, `cset`, `symop` | `a64impl.h` | record constructors | `a64_` + name |
| `slot`, `treg`, `tfree`, `toreg`, `toreg2`, `val_free` | `value.c` | frame slot, temporaries | `a64_slot` ...; "slot" also names a known-value index (`ael_slot`) and a field in six structs |
| `load`, `store`, `load_n`, `store_n` | `lval.c`, `value.c` | typed and sized access; `store` consumes its `val` | `a64_load` ...; `LO(store)` takes a register and keeps it: same word, other contract |
| `cast`, `arith`, `arith2`, `extend`, `tobool`, `test_zero` | `arith.c` | value operations | `a64_`; `arith2` -> `a64_arith128` |
| `gen`, `gen_inner`, `gen_stmt`, `gen_block`, `gen_lval`, `gen_cond`, `gen_cond_expr` | | the walk | `a64_expr`, `a64_stmt`, `a64_lvalue`, `a64_branch` (for `gen_cond`), `a64_ternary` (for `gen_cond_expr`: today one word apart from the branch) |
| `dry` | `builtin.c:80` | generate and discard | `a64_probe_const` |
| `fpk` | `fp.c:7` | 1 for double, 0 for float, fatal otherwise | `a64_fp_is_double` |
| `X` | `emitimpl.h:1037` | "t is a 64-bit operation" | `a64_is64` |
| `park`, `pair`, `classify`, `hfa`, `jump`, `bcond`, `cbz`, `tbz`, `popcount`, `binding`, `kbits`, `vraw` | | | `a64_` or `em_` by where they end up |
| `va_arg_`, `va_start_` | `builtin.c:403,435` | underscore dodges the macro | `a64_va_arg`, `a64_va_start` |
| `val_`, `rb_` | `stmt.c:64`, `expr.c:293` | locals dodging a type and a parameter name | `init`, `raw` |
| `E->mf`, `nlr`/`lrsym`/`lrreg`, `inlfree`, `vsz`, `sla`/`sstk`, `kv`, `ael` | `emitimpl.h:312-408` | must-fold set, register locals, ... | spelled-out fields; a glossary in STYLE.md |

Prefix rule proposed: every file-scope name in `arch/<cpu>/emit/` starts with the CPU (`a64_`, `x64_`,
`wasm_`); shared emitter code is `em_`; the walker is `wk_`; no unprefixed function in a header that other
directories include.  It breaks today in 212 of 236 functions here.

Words with two meanings: **slot** (frame slot; known-value index); **mem** (a record; held by address);
**put**; **cond** (`ins.cond` carries a condition, a shift kind, an extension, a sign-extension flag, the raw
kind, a TLS model and the 0xee marker); **gen_cond** (branch) against **gen_cond_expr** (`?:`).  One meaning
with several words: a known constant is `B_CONST`, `kv`, `kval`, `ael`, `kgen`, `pk`, `cst`; a local in a
callee-saved register is `lreg`, `B_LREG`, `L_VREG`, `nlr`; saving a value to the frame is `park`, `spill`,
`keep` (the opposite), `cond_spill_value`.  The FP scratch registers are `FV0`/`FV1` in `fp.c:5` and the
literal 16 in `builtin.c:936-940`.

## 6. Decisions needed

1. **Does arm64 move onto the shared walker?**  Options: (a) full move after the interface grows (gaps 1-9);
   (b) share policy only (steps 1-5 of the order), keep two walkers, document the exception; (c) a pilot on
   loops and `if` first, then decide.  Recommendation: (c) after steps 1-5, which are worth doing in any case
   and remove the cheap third of the duplication.  The pilot must report instructions per kernel TU and
   records per function at `-O0`, and the count of retries.
2. **Which representation wins where they differ**: 128-bit and long double as register pairs or by address;
   floats as bits in GPRs or in float registers; lazy or kept extension.  Each decides how much arm64 output
   changes.  Recommendation: measure on the pilot; do not adopt x86-64's choices by default.
3. **Known values at labels**: a64's join over forward edges, or wk's "forget at every label".  The kernel's
   `error()` folds depend on it.  Recommendation: the join, in shared code.
4. **`dry()` or an AST predicate** for `__builtin_constant_p`.  Recommendation: keep `dry()` semantics until
   a kernel build shows the predicate folds the same calls.
5. **Each item of Q-WA64-04**: bug or intended; clang is the reference.
6. **Rename window for `arch/a64/codegen/`** (about 1900 call sites, VIR's `codegen/vir/a64.c` included).

## 7. Not covered

- Nothing was built, run or measured: every behaviour difference is inferred from the source; the cost of
  `LO()` and of the retry is unmeasured.
- `func.c`, `opt.c`, `print.c`, `obj.c`, `darwin.c`, `tail.c`, `unreachable.c`, `insn.c` (another review);
  `func.c:764-843` and `960-998` were read for the retry only.  Function entry and parameters (the a64 side of
  `wk_function`) are therefore not in the matrix.
- `arch/x64/codegen/asm.c` was read at 214-352 and 586-710 only; the other x86-64 lowering files not at all, so
  "the interface fits" means the walker's side of `LO()`, not x64's implementation of it.
- `codegen/emitimpl.h` was read for the types and prototypes; `codegen/opt/*.c`, `codegen/prepass.c` not read.
- wasm's walker (the third copy) is in `quality-walker-wasm.md`.
- Whether `BI_SETJMP` can reach arm64's `gen_builtin`, and whether TODO.md:221 is stale, were not checked.
