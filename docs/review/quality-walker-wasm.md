# Quality review: wasm's AST walker against the shared walker (2026-11-13, main at 2e6d3e73)

Read in full: `arch/wasm/emit/` (12 files, 7 172 lines) and `emit/walk/` (`walk.h`, `proto.h`,
`expr.c`, `stmt.c`, the three recipe files it shares a job with).  Paths are relative to `ccs/vcc/`;
in the tables `wexpr.c`, `wstmt.c`, `wcall.c`, `wasm.c`, `wimpl.h` are in `arch/wasm/emit/`.

## 1. Summary

The wasm generator is a third, complete AST walker.  Of 32 concepts compared, 6 are genuinely
different from the shared walker, and every one of them is control flow or the call sequence; 19
are the same C-level decision written again, most with only the call names changed; the rest is target code that
the shared walker already delegates through `LO()`.  The three things that matter:

1. **wasm cannot be driven through `LO()` as the interface is today** (Q-wasm-01): the walker hands
   out untyped registers, hard-codes 8-byte addresses (74 `LO(bin)(.., 8, ..)` sites, wasm32 is
   ILP32), stages arguments in frame slots and expresses all control as labels (26 `wk_newlabel`
   sites).  Moving wasm onto it changes every output byte.  What can be shared without that is the
   C-level layer: rules, initialiser walks, builtin decoding, predicates.
2. **No harness proves a wasm refactor byte for byte** (Q-wasm-02): `bench/vcc-same.py` has only
   arm64 modes.  Every sharing step below needs a wasm mode first.
3. **The function body is walked more often than needed** (Q-wasm-04, -05, -13): a goto pre-scan
   of every function including its expressions, label and termination proofs re-walked per
   statement although the prepass has the answer, and `side_effects`/`cc_eval` re-run on the same
   subtree at each nesting level.

## 2. Inventory

| File | Lines | What it actually does | Better name | Header comment |
|---|---|---|---|---|
| `wimpl.h` | 462 | types (bindings, lvalue, ABI class, goto records, memory and flow events), the one state struct, text printers, locals, frame slots, block open/close | `wasmimpl.h` (as `a64impl.h`, `x64impl.h`) | good; says "wasm64" only, wasm32 is supported |
| `wasm.c` | 713 | state pool; C ABI classification and signatures; `.functype` header and destructor tail; parameters; the function (prologue, epilogue, sections) | `func.c` + `abi.c` | label only |
| `wexpr.c` | 2286 | conversions; lvalues; VLA sizes; loads/stores incl. bit-fields; 16-byte integers and long double (435 lines); vector lanes; operators; conditions; assignment; the expression switch | split: `conv.c`, `lvalue.c`, `mem.c`, `wide.c`, `vector.c`, `expr.c` | label only |
| `wstmt.c` | 1585 | local initialisers; inline asm; dead-code skipping; cleanups; VLA scopes; goto pre-scan and dispatch loops; setjmp frames; switch; statements | split: `init.c`, `goto.c`, `switch.c`, `sjlj.c`, `stmt.c` | label only |
| `wcall.c` | 1404 | calls; library prototypes for implicit declarations; variadic buffers; alloca; setjmp/longjmp calls; `__builtin_constant_p`; overflow, atomic and vector builtins; all other builtins | split: `call.c`, `libsig.c`, `builtin.c`, `atomic.c` | label only |
| `slots.c` | 277 | records typed load/store events while the body is printed; load forwarding and dead stores in straight code (`emit/rec/slots.c`); rewrites the body text | `cells.c` (it is `emit/rec/slots.c`'s adapter; "slot" already means a frame slot here) | good |
| `flow.c` | 256 | records block/if/branch events; dead stores across if/else joins (`emit/flow/`) | `store-dse.c` | good |
| `reachability.c` | 52 | `wlabels` (a statement holds a label), `wterm` (a statement never completes) | fine | good |
| `sdivpow2.c`, `udivconst.c` | 29, 27 | wasm's renderers of two `em_` division recipes and which recipes it accepts | fine | none (start with `#include`) |
| `switch-bitset.c`, `switch-values.c` | 44, 37 | wasm's renderers of two shared switch plans | fine | good |

What the module is for (draft of its opening comment): one C function at a time, straight from the
AST to LLVM's wasm text.  Invariants: (1) every expression leaves exactly one wasm value on the
operand stack or none; the function returns its `WT_` type.  (2) A value that does not fit a wasm
scalar (records, arrays, vectors, `__int128`, `long double`, complex) is the *address* of its bytes
(`WT_MEM`), as in the shared walker's `wk_mem`.  (3) A scalar local whose address is never taken is
a wasm local (`WB_LOC`), everything else lives in the shadow-stack frame; `W->fp` exists only if a
slot was made.  (4) Blocks are numbered by nesting (`W->depth`); a branch names its target's
number, `wbr` turns it into a relative depth.  Every `block`/`loop`/`if`/`else`/`end`/`br` goes
through `wblock`, `wend`, `wbr`, `wboundary`, because those also feed `flow.c` and `slots.c`.
(5) The body is text; `slots.c` remembers byte ranges of loads and stores in it.  (6) A compound
statement that holds a goto target is emitted as a dispatch loop over segments (`wstmt.c:311`).

## 3. Passes and data flow (L4)

Per function, in order:

| # | Walk | Where | Computes | Note |
|---|---|---|---|---|
| 1 | prepass over the AST | `walk_fn`, `arch/a64/emit/call.c:342` -> `emit/prepass.c:448` | `sflags`, `lnoreg`, `nflags` (`NA_LABEL`, `NA_ALLOCA`), label references | shared; wasm reads `lnoreg` (through `em_local_eligible`) and `sflags` only |
| 2 | goto pre-scan, statements **and expressions** | `wgoto_scan`, `wstmt.c:538`, called at `wasm.c:595` | compounds that hold labels, nested case labels, setjmp sites, statement expressions; then three `qsort`s | runs for every function, also one without a label |
| 3 | emission | `wstmt`/`wexpr` | the body text, memory and flow events | once; no retry (arm64 retries) |
| 3a | inside 3: `wlabels`, `wterm` | `reachability.c`, called at `wstmt.c:206, 217, 808, 1412, 1464` | "holds a label", "never completes" | recursive each time; `nflags & NA_LABEL` is the first, already computed |
| 3b | inside 3: `wvla_block` | `wstmt.c:275`, called at `1367`, `1302` | first VLA declaration of a compound | linear scan of every compound's statements |
| 3c | inside 3: switch body scans | `wstmt.c:1029-1069`, `em_switch_bitset_plan`, `em_switch_values_plan` | segments, nested cases, plans | the switch body is scanned 3-4 times before it is emitted |
| 3d | inside 3: `side_effects`, `cc_eval`, `wbcp_in` on a subtree | `wexpr.c:1302, 1455, 1573, 1886` | "pure", "constant" | repeated at each nesting level (Q-wasm-13) |
| 4 | sweeps over memory events | `wmem_optimize`, `slots.c:182` (2 sweeps); `wflow_optimize`, `flow.c:126` (3 sweeps) | forwarded loads, dead stores | only at -O1 and up |
| 5 | text copies | `wmem_render` (`W->b` -> `W->fn`), `wasm.c:709` (`W->fn` -> `e->out`) | the function's text | the body is copied twice |
| 6 | per TU: sort of declaration lines; for `-c` the assembler parses the text | `wasm_header`, `wasm.c:363`; `emit/tu.c:663` | `.functype` lines; the object | one line per call site is sorted, then deduplicated |

Do `flow.c` and `slots.c` recompute what `emit/prepass.c` or `emit/flow/` compute?  No.  The
analyses are the shared ones (`em_slot_*`, `em_slot_value_*`, `em_flow_build`, `em_flow_liveness`).
What is private is the event log (`wmem`, `wflow_event`), needed because wasm has no records, and
`wm_grow` (`slots.c:10` = `arch/a64/emit/opt.c:452`).  The escape fact (`wmem_escape`) is finer than
the prepass's `lnoreg` (per frame allocation, including aggregates), so it is not a second copy.
The redundant facts are 3a and 2: `wlabels` is `NA_LABEL` except that it stops at an inner switch
(`reachability.c:26`, the prepass does not: `prepass.c:636`); `wterm` is what the shared walker
keeps as `e->reach` (`walk/stmt.c:930, 1116`); the pre-scan could be skipped from a prepass flag.

### Concept matrix

Verdict: **V** verbatim; **R** same structure with renamed calls; **A** same algorithm written
differently; **D** genuinely different (what forces it); **T** target code on both sides, the walker
already calls it through `LO()`; **W**/**K** only in wasm / only in the walker.

| Concept | wasm | shared walker (`emit/walk/`) | Verdict |
|---|---|---|---|
| Integer binary operators | `wbinary` wexpr.c:1350, `warith` 1219 | `wk_binary` expr.c:1373, `wk_arith` 976 | R for the opening (`em_cmpbounds`, `em_repeated_integer`, `em_sibling_values`: wexpr.c:1356-1390 = expr.c:1380-1405) and pointer arithmetic (1399-1431 / 1429-1449); the operation is T |
| Constant folding | none of its own: `cc_eval` at `wnum` 283, `wknown` 1557, wcall.c:1364 | `wk_fold` 86, `wk_fold_cmp` 127, `V_CONST` through every function, `wk_ival` 3508, bound locals and `a[K]` (211-267) | K: wasm folds what the front end folds; the walker also folds what it has bound |
| Rules for a constant operand (identity, xor-not, mul to shift, power-of-two and reciprocal division) | `wbinrule` 1289, `wbinreduced` 1324, `sdivpow2.c`, `udivconst.c` | expr.c:989-1055, `walk/sdivpow2.c`, `udivconst.c`, `sdivconst.c` | A: the same `em_` recipes asked in a hand-written order twice (Q-wasm-03) |
| Comparisons | `wcompare` 1261, wexpr.c:1432-1445 | `wk_cmpv` 1270, expr.c:1411-1425 | R + T |
| Conditions, `&&` `\|\|` `?:` | `wtruth` 1484, `wbool` 1511, `wknown` 1557, wexpr.c:2153-2193, `wcond_expr` 1582 | `wk_cond` 3615, `wk_condfold` 3598, `wk_condval` 1457, `wk_ternary` 1485 | D: the walker compiles a condition into jumps to a label; wasm needs an i32 for `if`.  The known-condition part is A (both start with `em_const_truth`) |
| Assignment, compound assignment | `wassign` 1627 | `wk_assign_` 1708, `wk_optype` 1644 | R.  wasm takes the computation type from the node (wexpr.c:1689, set at `cc/expr.c:314`); the walker recomputes it |
| Increment, decrement | `wincdec` 1764, `w16incdec` 1097 | `wk_incdec` 1919, `wk_incval` 1809 | R |
| Bit-fields | `wbf_bytes` 440, `wload` 491-551, `wstore` 570-664, `wbf_trunc` 679 | `wk_bf_unit` 437 .. `wk_bf_store` 643 | A, and diverged (Q-wasm-12); the run-time bit index is R (wexpr.c:365-388, 493-500, 572-596 / expr.c:359-376, 609-617, 655-673) |
| Lvalues, addressing | `wlval` 296, `wlocal` 133, `wscale` 256, `wnum` 283 | `wk_lval` 269, `wk_local` 173, `wk_ptradd` 1068 | R (same cases; the member rule wexpr.c:341 = expr.c:340).  TLS, register globals: K |
| Loads, stores, struct copy | `wload` 478, `wstore` 558, `wcopy` 671 | `wk_load` 730, `wk_store` 782, `wk_putmem` 1607, `LO(load/store/copy)` | T |
| Local initialisers | `wzero_init` wstmt.c:6, `welems` 20, `winit` 98 | `wk_zero_init` stmt.c:167, `wk_elems` 189, `wk_init` 291 | V (`zero_init`, also `arch/a64/emit/stmt.c:32`); R (`elems`) |
| Casts | `wconv` 28, wexpr.c:1982-2097 | `wk_conv` 795, `wk_cast` 835 | R for the cast kinds (CK_BITS byte loop wexpr.c:2010-2025 = expr.c:879-900; CK_SPLAT; CK_UNION); the conversion is T |
| Floating point | `wfconst` wimpl.h:258, wexpr.c:1928, `wmath_insn` wcall.c:308 | `LO(fbin/fneg/fcmp/cvt)`, `wk_fpconst` 2686 | T.  Class builtins are A: float compares, f32/f64 only (wcall.c:1241-1309) against bit patterns for every format (expr.c:2537-2684, 3118-3200) |
| Calls, arguments | `wcall` wcall.c:389, `warg` 165, `wclassify` wasm.c:104, `wsig` 182 | `wk_call` 2134, `wk_argslot` 2040, `LO(classify/call)` | D: arguments on the operand stack in order and a signature string per call, against slots then registers.  R for the C-level prefix: fortified calls (wcall.c:416-435 / expr.c:2161-2172), strcmp of literals (wcall.c:410 calls `em_const_strcmp`; the walker has its own copy, expr.c:2174-2187).  Inlining, inline memcpy: K.  Library prototypes, K&R matching: W |
| va_arg, va_start | `wvaarg` wcall.c:671, 1059-1079 | expr.c:3436, 3230-3250, `LO(va_start/va_arg)` | T |
| alloca, VLA | wexpr.c:164-240, wstmt.c:857-885, `wvla_block` 275, `wvla_leave` 297, wcall.c:213, 1115 | expr.c:1097-1246, `wk_vla_scan` stmt.c:438, 936-973, expr.c:2979 | R for `vla_fix` (wexpr.c:203-240 = expr.c:1112-1145); A for storage (a linear list of size locals and an sp local per block, against per-node slots and a 24-byte record) |
| Switch | `wswitch` wstmt.c:1004, `wswitch_tree` 966, `wnested_cases` 928, two recipe files | `wk_switch` stmt.c:644, `wk_cases` 575, `wk_switch_tree` 615 | Policy shared (`emit/opt/switch.c`); the strategy order is A (wstmt.c:1190-1267 / stmt.c:694-792); the body is D: wasm cuts it into segments at its top-level case labels and reaches nested ones through the goto dispatch, the walker places a label |
| Loops | wstmt.c:1450-1524 | stmt.c:1046-1099 | D: `block`+`loop`(+`block`) and depths, against three labels |
| goto, labels | wstmt.c:311-692, `wgblock` 803, `wgtest` 784, computed goto 1541 | `wk_goto` 822, stmt.c:1108, `LO(jumpr)` 1241 | D: state locals and a `br_table` dispatch loop per compound; a statement entered by goto skips its condition |
| Dead code | `wstmts` wstmt.c:198, `reachability.c`, `notop` 1439 | `e->reach` + `NA_LABEL` stmt.c:930, `wk_branch_entry` 520, `wk_const_if` 882 | A (Q-wasm-04) |
| Statement expressions | `wstmtexpr` wexpr.c:1847, `wst_sxval` 1827, wstmt.c:1398 | `wk_stmtexpr` 2260 | A: wasm emits the whole compound (it may need a dispatch loop) and catches the value statement in a local |
| Cleanups, return, declarations | wstmt.c:224-270, `wreturn` 1308, `wdecl_stmt` 842 | stmt.c:330, 1130-1221, `wk_decl` 357 | A, A, R (the same return-a-cleanup-local rule: wstmt.c:1320-1330 / stmt.c:1173-1185) |
| Atomics | `wbi_atomic` wcall.c:816; `_Atomic` objects plain (wexpr.c:1650) | `wk_atomic` 2355, `wk_atomic_rmw` 1836 | R for decoding the builtin (wcall.c:823-838, 937-938 = expr.c:2360-2381, 2483-2484); the operations are plain loads and stores by decision (no threads) |
| `__builtin_constant_p` | `wconstant_p` wcall.c:355 | `wk_constant_p` 2837 | A; V with arm64's (`arch/a64/emit/builtin.c:858-879`): three policies (Q-wasm-07) |
| Other builtins | `wbuiltin` wcall.c:1049 | `wk_builtin` 2848 | R for expect, assume, prefetch, lock_free, fences; bit counting T; bitreverse and overflow A (wcall.c:1196-1229, 756 / expr.c:3020-3050, 3051-3115) |
| Inline asm | `wasm_asm` wstmt.c:145 (empty template only) | stmt.c:1222, `LO(asm)`, `LO(barrier)` | T |
| Complex | wexpr.c:2229 | expr.c:3472 | R |
| Vectors | `wvlanes` 1142, `wbi_vector` wcall.c:992, splat 2054 | `wk_vlanes` 1309, expr.c:2916-2955, 922-938 | R |
| `__int128`, long double | `w16*` wexpr.c:697-1131 | `LO(wconst/wbin/wun/wcmp/wcvt)` | T: `w16arith` is `LO(wbin)`.  `_BitInt`: unsupported (wexpr.c:1901) |
| volatile | `wlv.vol` wexpr.c:303 -> `wmem_poison` slots.c:152 | `e->novtag` around `LO(load/store)` | A: each tells its own optimizer |
| setjmp | wcall.c:235-301, `wsj_begin/end` wstmt.c:702-781 | expr.c:2884, `LO(setjmp/longjmp)` | D: wasm exceptions, and re-entry at the call's statement through the goto dispatch |
| Function, parameters, frame | `wasm_emit_function` wasm.c:552, `wparams` 426, `wslot` wimpl.h:344 | `wk_function` stmt.c:1542, `wk_slot` 85 | T.  The walker reuses temporary slots per statement, `wslot` never (Q-wasm-17) |

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-wasm-01 | L1, L7 | pattern | `walk/proto.h:71-173`; `LO(treg)(e)` has no type; 74 `LO(bin)(e, .., 8, ..)` in `walk/expr.c`; `wk_argslot` expr.c:2040; 26 `wk_newlabel`; `val.r` is `uint8_t` (`emit/emitimpl.h:229`) | the shared walker cannot serve a stack machine or an ILP32 target, so a third walker exists | decision 1; share the C level now (Q-03, -06..-09) | L | high | none byte for byte: output changes |
| Q-wasm-02 | L1 | pattern | `bench/vcc-same.py:30-33`: modes `c0 c1 c2 s2 e d2 ds`, all arm64 | no wasm refactor can be shown to change nothing; `vcc-wasm.py` only runs programs | add modes `-S` wasm64 and wasm32 at -O0 and -O2 | S | low | the harness itself |
| Q-wasm-03 | L1 | near-duplicate | wexpr.c:1289-1348 / `walk/expr.c:989-1055` | a new `em_` rule is wired into each walker's order by hand; wasm's order and subset already differ | `emit/opt/`: one `em_bin_plan(c, ty, op, rhs, accepts)` returning a tagged recipe; per target a renderer | M | medium | vcc-same (arm64 unaffected), new wasm modes |
| Q-wasm-04 | L4 | redundant walk | `reachability.c:5-52`, calls at wstmt.c:206, 217, 808, 1412, 1464 / `emit/prepass.c:636, 668-704`, `walk/stmt.c:930` | subtrees re-walked per dead statement and per `if`; two definitions of "holds a label" | read `nflags & NA_LABEL`; give the prepass wasm's stop at an inner switch or accept the superset | S | low | wasm modes (dead switches may differ) |
| Q-wasm-05 | L4 | redundant walk | `wasm.c:595`, wstmt.c:385-513, 538-588 | every function pays a statement and expression walk plus three sorts for goto support | a prepass flag per function (user label, nested case, setjmp call, statement expression); skip the scan without it | S | low | wasm modes |
| Q-wasm-06 | L1 | duplicate | wstmt.c:6-17 = `walk/stmt.c:167-178` = `arch/a64/emit/stmt.c:32-46`; wstmt.c:20-95 / `walk/stmt.c:189-289` | three copies of the initialiser walk to keep in step with the front end's N_INIT layout | `emit/`: `em_zero_init`; then one initialiser walk with a per-target sink (scalar, bit-field, string, zero) | S then M | low | vcc-same, wasm modes |
| Q-wasm-07 | L1 | duplicate | wcall.c:355-387 = `arch/a64/emit/builtin.c:858-879`; `walk/expr.c:2837-2846` | three answers to `__builtin_constant_p`; a kernel macro can fold on one target and not on another | `emit/opt/`: the non-emitting part once, a target hook for "bound constant" | S | medium | wasm modes; `vcc-constant-control.py` |
| Q-wasm-08 | L1 | near-duplicate | wexpr.c:1142-1208 / `walk/expr.c:1309-1371`; wcall.c:992-1047 / expr.c:2916-2955; wexpr.c:2010-2025 = expr.c:879-900; the bool-vector lane lvalue: 10 sites in wasm (wasm.c:501, wexpr.c:1163, 1164, 1184, 2065, wstmt.c:37, wcall.c:191, 1012, 1029, 1036), 9 in the walker | lane-by-lane vectors exist twice, 19 hand-written lane lvalues | `emit/`: lane iteration and `em_lane(c, vt, i)` -> offset, bit; in wasm first one `wlane()` helper | S (wasm), M (shared) | low | wasm modes |
| Q-wasm-09 | L1 | near-duplicate | fortified calls wcall.c:416-435 / expr.c:2161-2172; atomic decoding wcall.c:823-838, 937-938 / expr.c:2360-2381, 2483-2484; `vla_fix` wexpr.c:203-240 / expr.c:1112-1145; callee wcall.c:6-25 / expr.c:2154 | builtin and call conventions of the front end decoded per walker | `emit/opt/`: `em_chk_plain`, `em_atomic_decode`, `em_direct_callee`, `em_vla_sizes` | M | low | vcc-same, wasm modes |
| Q-wasm-10 | L1 | duplicate | inside wasm: 16-byte constant into a slot wexpr.c:1909-1919, 1929-1940, wcall.c:1369-1377 (`w16const` exists, 778); zero a slot wexpr.c:2043-2047, 2216-2220, wstmt.c:107-111, 878-882; truth of a scalar wexpr.c:74-79 = 1497-1501 = 1601-1606; store and keep the value 1675-1684 = 1746-1755; `wretype` wcall.c:124-147 beside `wconv` wexpr.c:82-117; wstmt.c:84-95 = 116-127; wcall.c:1080-1084 = 1085-1089; `wgdx_cmp` wstmt.c:524 = `wprio_cmp` wasm.c:296 | about 150 lines; a fix lands in one copy | helpers in the same files | S | low | wasm modes |
| Q-wasm-11 | L4 | long function, mixed file | `wexpr1` 391 lines (wexpr.c:1896), `wbuiltin` 356 (wcall.c:1049), `wswitch` 291 (wstmt.c:1004), `wcall` 279 (wcall.c:389), `wbi_atomic` 174 | nobody finds the cast kinds or the switch strategies; see the jobs below | split by job, then files as in section 2 (moves only) | M | low | wasm modes |
| Q-wasm-12 | L1 | near-duplicate | wexpr.c:440-448: a field spanning 3 bytes is read and written as 4, 5-7 as 8; `walk/expr.c:451-462, 877-878` touches only the field's bytes | inferred, not tested: a packed bit-field at the end of an object reads and rewrites bytes outside it | one container rule in `emit/` used by both | S | medium | a test in `tests/vcc` first |
| Q-wasm-13 | L4 | redundant walk | `wexpr` wexpr.c:1886 (`side_effects` per unwanted value); `wknown` 1573 (`wbcp_in`, `side_effects`, `cc_eval`) from 1588, 2155, wstmt.c:1410; `wbinrule` 1302, 1455 (`cc_eval` of each right operand, then of the left) | a chain `a && b && c ..` or nested arithmetic re-walks its left spine at every level | answer once per node: a prepass bit for "pure", and try `cc_eval` only on N_NUM/N_CAST shapes | M | low | `vbench.py`; wasm modes |
| Q-wasm-14 | L4 | pattern | body text copied `W->b` -> `W->fn` -> `e->out` (slots.c:243, wasm.c:709); `-c` re-parses it (`emit/tu.c:663`); wasm.c:698 `strstr` of the emitted text to learn what `wfname` decided at wasm.c:239; slots.c:237-277 patches by byte offset | an extra pass per TU, and decisions read back from text | PLAN-wasm W5 "generator directly to the model"; meanwhile a flag instead of `strstr` | L (S for the flag) | medium | `vcc-wasm.py --objcmp` |
| Q-wasm-15 | L4 | pattern | `wdecl` at every call (wcall.c:604), every run-time call (`wrt`, wexpr.c:727-734), every function address (wexpr.c:1957); sorted in `wasm_header` wasm.c:363-405 | one text line per call site, sorted per TU | a per-thread "declared" bit per symbol (as `claim_ref`) | S | low | wasm modes |
| Q-wasm-16 | L7 | misplaced | wasm calls `walk_fn` (`arch/a64/emit/call.c:342`), `claim_ref`, `is_local_var` (`a64/emit/bind.c:11, 185`), `out_chunk` (`a64/emit/func.c:6`), `side_effects` (`a64/emit/builtin.c:14`); `wswitch_values` is declared in `emit/emitimpl.h:544`, not in `wimpl.h`; `wimpl.h:442-443, 454` list `wsj_begin`, `wswitch_bitset` under the wrong file | the wasm generator does not build without arm64's emitter; `walk.h:17-19` says so for the walker too | move the five to `emit/` (they are target independent); fix the three prototypes | S | low | all outputs unchanged (moves) |
| Q-wasm-17 | L8 | global state | `wst` wimpl.h:64-126: about 60 fields for eight jobs; `WMAXGD` (goto records) also caps VLA sizes, VLA blocks, cleanups and setjmp sites (wasm.c:35-39); `cl` is a `wvb` whose `.sp` holds a symbol (wimpl.h:100, wstmt.c:904); a global pool with its own mutex (wasm.c:7-8) while the walker's state is per `ecx`; `wslot` never reuses (wimpl.h:344) | a field's owner is unknown; every 16-byte temporary grows the shadow-stack frame for the whole function | sub-structs per job (goto, sjlj, vla, cells, flow); statement-scoped slot reuse as `walk/stmt.c:85-108` | M | medium (frame layout changes) | wasm modes for the struct; run suites for slot reuse |
| Q-wasm-18 | L2 | pattern | `malloc` per statement: wstmt.c:809, 1105, 1197, 1237; private growing vectors wasm.c:317-320, wstmt.c:936-939, slots.c:10-19, flow.c:29-39 | four private vectors beside the reservations in `wst` | `lib/vec.h` or the state's reservations | S | low | wasm modes |
| Q-wasm-19 | L9 | error policy | 51 `efatal` in the area; 8 say "not supported yet (PLAN-wasm.md W4)" (wexpr.c:68, 1232, 1274, 1504, 1777, wstmt.c:666, 671, wcall.c:1403), W4 is done; the shared walker's 7 messages say "x86-64:" (`walk/expr.c:24, 725, 3267`, `walk/stmt.c:127, 1248, 1587, 1602`) | a user is sent to a finished plan step; the VIR instantiation already prints "x86-64" for other targets (inferred) | one `em_unsupported(e, loc, what)` that names the target | S | low | `vcc-diag.py` |
| Q-wasm-20 | L10 | dead, stale doc | `DARWIN(c)` in `switch-values.c:7`; `(void)W` wstmt.c:799; `(void)want` wcall.c:986; orphaned comments wasm.c:246, wexpr.c:1527; `wimpl.h:2` "wasm64"; PLAN-wasm.md's architecture table names `arch/wasm/emit/print.c`, `as/enc.c`, `as/parse.c`, `obj/fixup.c`, none of which exists | misleads a reader | delete; correct the table | S | none | - |
| Q-wasm-21 | L5, L6 | misnamed | section 5 | a bare `w` collides with five other meanings in the tree | prefix `wasm_`, one rename window | M | low | all outputs unchanged |

**Q-wasm-01, what `LO()` would need.**  The six D rows need the AST or new callbacks:
- *Values.*  A walker "register" would be a wasm local: `LO(treg)(e, type)` (i32/i64/f32/f64), and
  every literal `8` for an address becomes the pointer width.  Each operation then prints
  `local.get, local.get, op, local.set`; today's operand-stack code comes back only with a
  stackifier over records, and wasm has no records (the body is text).
- *Structured control*, usable by a label target too (it implements them with labels):
  `LO(block)`/`LO(loop)` returning a scope, `LO(end)(scope)`, `LO(br)(scope)`,
  `LO(br_if)(scope, val, sense)`, `LO(if)(val)`/`LO(else)`/`LO(endif)`.  They cover `if`, the three
  loops, `break`/`continue`, `?:`, `wk_condval`, the return label, an inlined call's end label and
  the walker's internal loops (CAS loops expr.c:1863, 2488; VLA zeroing 1229; fpclassify 2644).
  `wk_cond`'s jump threading (expr.c:3615) has no stack form: a condition must become
  `LO(cond)(node)` or a value.
- *What still needs the AST:* a switch body (`LO(switch_stmt)(node)`: segments, nested cases), a
  compound that holds a label, any `if`/loop/switch a goto enters (its condition is skipped:
  wstmt.c:784, 1088, 1418, 1491), setjmp sites, the value statement of a statement expression, and
  a per-function scan (`LO(scan)(body)`).  So the walker would hand whole statements to wasm
  exactly where wasm's code is longest (wstmt.c:311-838, 1004-1294: about 820 lines stay).
- *Calls:* arguments as values in order, and the callee's C function type and call node (for
  `wsig`, K&R definitions, library prototypes), not `wkarg` slots.

**Q-wasm-11, the jobs inside.**  `wexpr1`: (1) constants, incl. 16-byte ones into a slot;
(2) symbol values (function address, enumerator); (3) address-of and array decay; (4) nine cast
kinds, of which CK_BITS alone is 39 lines; (5) unary operators; (6) `&&`/`||` with known-condition
folding; (7) dispatch to assignment, `?:`, calls and builtins inside memory fences; (8) zero value;
(9) complex from parts; (10) label address; (11) `sizeof` of a VLA; (12) rvalue load.  Each of 1,
4, 6 is a function in the shared walker (`wk_fpconst`, `wk_cast`, `wk_condval`).
`wbuiltin`: (1) va_start/end/copy; (2) pass-through (expect, assume_aligned); (3) no-ops and
fences; (4) traps; (5) frame and return address; (6) alloca with the VLA save-point refresh;
(7) bit counting, ffs, clrsb; (8) bswap, bitreverse, rotates; (9) FP comparison and class
builtins, signbit; (10) dispatch to overflow, atomic, vector; (11) constant_p; (12) any builtin
`cc_eval` folds; (13) object_size.

**An incremental order** (each step byte-identical on wasm unless said): (0) Q-02, the wasm modes;
(1) Q-10 and the `wlane()` half of Q-08, inside wasm only; (2) Q-16 moves, `em_zero_init` (Q-06),
`em_direct_callee`, `em_chk_plain` (Q-09): verbatim code to `emit/`; (3) Q-05 and Q-15 (fewer
walks, same output), then Q-04 (dead switches may change: review the diff); (4) Q-03 `em_bin_plan`
and Q-07 constant_p, wasm first, then the walker, then arm64; (5) the initialiser walk and vector
lanes shared (Q-06, Q-08); (6) Q-11 splits and Q-21 renames, moves and renames only; (7) Q-17 slot
reuse and Q-14, which change output and are proved by `vcc-wasm.py` runs.

## 5. Names (L5, L6)

Prefix today: a bare `w` on every function, type and file (`wimpl.h:9-10`), enums `WT_ WB_ WL_ WM_
WF_ WA_ WG_`, state variable `W`.  Where it collides:

| Name | Where | Collides with |
|---|---|---|
| `W` (`wst *W`, 58 functions) | all of `arch/wasm/emit/` | `wkst *W` in the walker (25 functions) and a static struct named `W` in `arch/a64/emit/opt.c:215`, all three in the `emit` unity TU |
| `WA_IGNORE..WA_PAIR` | wimpl.h:396 | `WA_INT`, `WA_SSE` (`walk/walk.h:33`, used by `arch/x64/emit/abi.c`, `emit/vir/build.c`): two enums with one prefix in one TU |
| `WF_BLOCK..` (flow events), `WM_FENCE..` | wimpl.h:44, 54 | `WF_WEAK..` symbol flags (`arch/wasm/as/was.h`, `platform/wasm/wasm.c`); `WM_LIST/BR/POS` (`vir/parse.c`) |
| `w_state` | wasm.c:10 | `w_sym`, `w_ty`, `w_push` .. (`cc/replay.c`, `vir/alloc.c`) |
| `wm_grow`, `wm_event` | slots.c:10, 79 | `wm_has`, `wm_clear` (`cc/replay.c:691`) |
| `wknown` | wexpr.c:1557 | reads as a `wk_` walker function |
| `wasm_asm` (static, inline asm statement) | wstmt.c:145 | the public `wasm_` entry points and `wasm_assemble` |
| `wst_sxval` | wexpr.c:1827 | reads as a method of the state type `wst` |

Proposal: `wasm_` for functions and types of this module and `WASM_` for its enumerators, as
`x64_` and `a64_` name a CPU's code (and as `LO_PREFIX` would be if wasm ever became a lowering);
the six public entry points keep their names; the state is `wasm_fn *F` reached as `e->wasm`; the
walker's argument classes become `WK_ARG_INT/SSE`.  The assembler keeps `was_`.

Identifiers that hide their meaning:

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `wi`, `wiu`, `wii`, `wop` | wimpl.h:207-248 | print an instruction: bare, with unsigned, with signed operand, typed | `wasm_ins`, `wasm_ins_u`, `wasm_ins_i`, `wasm_typed` |
| `wpop`, `wpconst` | wimpl.h:277-278 | a pointer-width operation / constant (not "pop") | `wasm_ptr_op`, `wasm_ptr_const` |
| `wrt` | wexpr.c:722 | call a compiler-rt routine; `W->rt` is the wasm result type | `wasm_rtcall` |
| `w16` | wexpr.c:708 | 1 for `__int128`, 2 for long double, 0 | `wasm_wide_kind` with an enum |
| `wtmp`, `wfree` | wimpl.h:311, 321 | take and return a temporary *local* | `wasm_tmp_local`, `wasm_tmp_release` |
| `wfp` | wimpl.h:335 | the frame-base local, made on first use | `wasm_frame_local` |
| `wg*` (`wgd`, `wgp`, `wgdx`, `wgrec`, `wgsub`, `wgneed`, `wgseg`, `wgstate`, `wgtest`) | wstmt.c:331-838 | goto dispatch: record, point, index, .. | `wasm_goto_*`; `wgd` -> `goto_scope`, `wgp` -> `goto_point` |
| `sjr sji sjl sjv sjsp sjp sjfid` | wimpl.h:105-108 | setjmp scan cursor and resume locals | a `sjlj` sub-struct with words |
| `cl[].sp` | wimpl.h:100 | the cleanup variable's symbol | its own struct, `.sym` |
| `notop`, `sxv`, `sxt`, `vszn`, `vszl`, `vb`, `gdx`, `ngl` | wimpl.h:91-111 | see their comments | spelled out |

Words with two meanings: **slot** (frame slot `wslot`; variadic buffer place `wva_slot`; the cell
analysis in `slots.c`; `dynbit_slot`); **flow** (`arch/wasm/emit/flow.c` is one dead-store pass,
`emit/flow/` a graph library, `cc/flow.c` front-end reachability); **seg** (switch segment
`W->seg`, dispatch segment `wgd.seg`); **st** (state local `wgd.st`, scalar type `wabi.st`,
statement list wstmt.c:1015, store-op table wexpr.c:454); **pt** (promoted type wasm.c:441,
computation type wexpr.c:1689, part type wcall.c:183); **k** (a wasm type almost everywhere, but
also `wbind.k`, `wlv.k`, `wabi.k`, a block index at wstmt.c:1472).

## 6. Decisions needed

1. **wasm onto the shared walker, or a documented exception.**  (a) Exception at the control-flow
   and call level, shared C level: the order above; wasm keeps about 5 k of its 7 k lines
   (estimate), each concept gets one planner in `emit/` and one renderer per target.  (b) Onto the
   walker with structured callbacks: needs typed temporaries, a pointer-width-clean walker,
   arguments as values, the callbacks of Q-wasm-01 in x86-64's and VIR's lowerings as well, and
   records plus a stackifier to get compact code back; no byte-identity proof exists for it.
   (c) wasm from VIR at -O1 and up (a stackifier over VIR's CFG, which has dominators and loops),
   the AST generator kept for -O0.  Recommendation: (a) now, and state in ARCHITECTURE.md that
   structured control and the operand stack are the exception; decide (c) with PLAN-vir, not
   here; not (b).
2. **`NA_LABEL` for wasm** (Q-04): refine the prepass (stop at an inner switch) so output stays
   identical, or accept that a dead switch is kept.  Recommendation: refine; arm64 and x86-64 gain.
3. **Bit-field container rule** (Q-12): is touching bytes beside a packed field acceptable on
   wasm (single-threaded linear memory)?  Recommendation: no; one rule in `emit/`, test first.
4. **The prefix** (Q-21): `wasm_` as proposed, or a short one (`wm_` and `wa_` are taken).
5. **Text as the body** (Q-14): keep text until "generator to the model" is done, or introduce
   records for wasm now.  Recommendation: keep; records only if decision 1 goes to (b) or (c).

## 7. Not covered

- Not read: the lowerings behind `LO()` (`arch/x64/emit/`, `emit/vir/build.c`), `arch/a64/emit/`
  beyond the lines cited from the clone scan, `emit/opt/` beyond `control.c`, `locals.c` and the
  signatures used here, `walk/sdivconst.c`, `walk/prefix.h`, `unprefix.h`, `arch/wasm/as/`.
- Nothing was built or run.  Q-wasm-12 (bytes beside a packed bit-field) and the "x86-64" text in
  VIR's diagnostics (Q-19) are inferred from reading.  The cost of the repeated walks (Q-04, -05,
  -13) is not measured; `quality-passes.md` owns the numbers.
- `wk_optype` against the front end's computation type, and the walker's own strcmp fold next to
  `em_const_strcmp`, are leads for `quality-codegen-shared.md`, not verified here beyond the lines.
- The clone scan's 157 runs for this directory were triaged only where they crossed into
  `emit/walk`, `arch/a64/emit`, `cc/` or repeated inside wasm; the `cc/func.c`, `cc/flow.c`,
  `cc/conv.c` matches are the statement-tree recursion shape (`BLOCK/IF/WHILE/DO/FOR`), written by
  hand in `wlabels`, `wterm`, `wnested_cases`, `wgscan`, `wvla_block`, `wk_cases`, `wk_vla_scan`,
  `wk_branch_entry`, `wk_tiny_ok`: a visitor candidate for `quality-passes.md`.
