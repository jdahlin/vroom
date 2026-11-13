# Quality review: shared code generation (2026-11-13, main at 2e6d3e73)

Area: `codegen/*.[ch]`, `codegen/walk/`, `codegen/opt/`, `codegen/rec/`, `codegen/flow/` (12.1 k
lines, 49 files; `walk/prefix.h` and `unprefix.h` are generated, only their heads
read).  Paths are relative to `src/`.  Counts are grep counts unless a line
is cited.

## 1. Summary

The area has three layers on paper (walker, lowering, record passes).  `opt/`,
`rec/` and `flow/` are small and well commented.  Three things matter most:
1. **The shared layer leans on arm64's files**: reference claiming, chunk output,
   the purity predicate, the prepass entry and the range folder live in
   `arch/a64/codegen/`, and `emitimpl.h` is arm64's header with the shared types
   in it (Q-CGS-01, 02).  A C++ front end's code generation inherits that.
2. **AST facts are recomputed by private walkers**: constant truth by four
   evaluators in a chain, again at each level; purity by eight predicates;
   "contains a label" twice; a round of `emit_tu1` rebuilds every table
   (Q-CGS-03 to 06, 18).
3. **The lowering interface is 76 functions, documented only in `proto.h`, and
   tripled in three days**; the walker is two files (3731 and 1650 lines) with
   the same sequences written up to nine times (Q-CGS-07, 09, 12, 13, 27).

## 2. Inventory

| File | Lines | What it does | Better name | Header |
|---|---|---|---|---|
| `emit.h` | 12 | `emit_tu` prototype | | stale ("arm64 ... no IR") |
| `name.h` | 31 | `EM_IDENT_IS`: compare an interned name with a literal | `identeq.h` | none |
| `emitimpl.h` | 1050 | Emitter and thread state, values, lvalues, object items, the plans' types, type predicates, and about 190 lines of arm64 prototypes | split (Q-CGS-02) | stale (opening comment) |
| `tu.c` | 809 | Diagnostics, work queue, thread contexts, round reuse, roots, header/tail text, DWARF unit, output writing, replay driver, rounds | split (Q-CGS-10) | label only |
| `prepass.c` | 737 | Per-function fact walk; also VIR's inlining sizes, two capability checks, `sym_align`, `inline_object_size` | split (Q-CGS-11) | label only |
| `data.c` | 707 | Static initializers to bytes and relocations; object and string output for every target and both backends | | label only |
| `obj.c` | 245 | Item stream writer (`it_*`) and replay of items into the ELF object (`r_*`) | `items.c` | label only |
| `type.c` | 154 | `em_trunc_to`, symbol and reference names, runtime helper names | `symname.c` | stale (says type predicates) |
| `walk/walk.h` | 133 | Walker state `wkst`, `wkarg` | | good |
| `walk/proto.h` | 174 | Walker prototypes and the lowering interface | `lower.h` (as PLAN-x86.md calls it) | good |
| `walk/expr.c` | 3731 | Lvalues, bit-fields, conversions, arithmetic, VLAs, vectors, assignment, atomics, calls, builtins, FP classes, constant evaluation, conditions | 10 files (Q-CGS-27) | label only |
| `walk/stmt.c` | 1650 | Walker state and slots, initializers, declarations, scope prescan, switch, statements, inlining, the function | 5 files | label only |
| `walk/sdivconst.c` `udivconst.c` `sdivpow2.c` | 50, 57, 31 | A division recipe written with `LO(bin)` | | none |
| `walk/switch-values.c` | 46 | A value-table switch written with `LO()` | | label only |
| `walk/prefix.h` `unprefix.h` | 144, 141 | Generated renames for the second instantiation | | good |
| `opt/cmpbounds.c` | 102 | `x < MIN`-style comparisons that are constant; `em_bound_constant` | | good |
| `opt/cmpzero.c` | 34 | Comparisons equal to `x == 0` (wasm only) | | label only |
| `opt/control.c` | 107 | Constant truth of a condition; `strcmp` of two literals | `consttruth.c` | good |
| `opt/divpow2.c` `sdivpow2.c` `sdivconst.c` `udivconst.c` | 23, 23, 62, 67 | Division plans | one `div.c` | good |
| `opt/mulconst.c` `mulshift.c` `xor-not.c` `identity.c` `double-unary.c` `repeated.c` | 15, 18, 10, 29, 22, 36 | One integer identity each | one `intfold.c` | label only |
| `opt/locals.c` | 50 | Which locals may live in registers, and the best N | `reglocals.c` | label only |
| `opt/sibling-values.c` | 263 | Value numbering of two operands and of a block's last statements | `valuereuse.c` | label only |
| `opt/switch.c` | 118 | Switch strategy, case sorting, the bit-set plan | | label only |
| `opt/switch-values.c` | 218 | Value-table plan, and the table's storage and output | | label only |
| `opt/tail.c` | 101 | Sibling-call eligibility | | good |
| `opt/unusedinit.c` | 95 | Never-read integer locals | | label only |
| `rec/rec.h` | 59 | x86-64's record type, op description flags | | good |
| `rec/slots.h` `slots.c` `slot-values.c` | 77, 232, 60 | Exact stack cells and per-cell store/load facts | | good |
| `rec/copies.c` `fallthrough.c` `unreachable.c` | 56, 48, 36 | One record clean-up each, over opaque records | | good |
| `flow/flow.h` `graph.c` `maskflow.c` | 44, 69, 94 | CSR block graph, backward bit-mask liveness | | good |
| `flow/values.h` `values.c` | 32, 47 | A 32-entry value-number table (used only by `opt/sibling-values.c`) | move to `opt/` | good |

**What the module is for (draft opening comment).**  `emit_tu` turns one TU's
finished AST into assembly text or an object.  The AST is read-only.  Work is
items: a function, an object or a string, claimed once (`claimed`, `strdone`)
when emitted code first refers to it; roots come from the top level.  Each
worker thread owns an `ecx` and appends chunks to its own buffer; chunks carry
a key (`sym << 1`, objects `| 1`, strings `1 << 62 | i`) and are sorted at the
end, so output does not depend on thread order.  With `emit == 2` a chunk is a
stream of items (`IT_*`) replayed in order into the assembler's object, byte
for byte what the text would assemble to.  Prepass facts of a function are
written by the one thread that wins `walked[s]` and are read-only afterwards;
facts of globals are installed before the workers start.  `efatal` longjmps to
`e->bail` and drops the function.  No reference is claimed while `!e->reach`
or `e->dry`.  Every walker name starts with `wk_`, and after adding a walker
function `src/tools/gen-walkprefix.py` must regenerate `prefix.h`.

## 3. Passes and data flow (L4)

### Per TU (`tu.c`)
`emit_tu` (775) runs `emit_tu1` up to 8 times.  Only arm64 asks for another
round: `need_fold` (`arch/a64/codegen/call.c:89-91`) is the one place that sets
`mf`/`newmf`; x86-64, wasm and VIR always finish in one.

The separate jobs in `tu.c`: (1) diagnostics `ediag`/`eerror`/`efatal` 19-70;
(2) queue and worker 74-79, 208-233; (3) thread context regions by index
`r[0..15]` 96-154; (4) round reuse `reuse`, `keep_make`, `keep_free` 157-184,
342-395; (5) target dispatch `do_item` 186-206; (6) roots `is_root` 242-272;
(7) the object replay driver 287-339; (8) `emit_tu1` 400-767, itself: table
set-up, knobs, global facts, VIR's inlining tables, roots, threads, sort,
header text (CET note, `.file` table), tail text (`.weak`, aliases, DWARF
unit, wasm tail), assembling or replaying or `writev` with partial-write
handling, Darwin text conversion, statistics, release; (9) the rounds 775-809.

What a round recomputes, with what it could keep:

| Recomputed each round | Where | Depends on `mf`? | Could keep |
|---|---|---|---|
| All `E` tables reserved and zero; every function's prepass | 417-437, `ensure_walked` | no (`prepass.c` never reads `mf`) | yes; only `claimed`, `strdone`, `extref`, queue and chunks are per round |
| Facts of every global | 459-463 | no | yes |
| `fn_once` (all nodes) and `fn_isize` (walks every function, Tarjan) | 465-468 | no | yes |
| Thread create/join, `calloc` of contexts, 16 regions each | 491-525 | no | yes |
| Header and tail text, built before `again` is known | 533-639, 644 | no | build in the last round only |
| Objects and strings | `keep_make` 362: skipped | no | yes, their chunks have logs too |
| Kept function bytes copied twice | 387, then `reuse` 181 | | one copy |
| `reuse` runs the prepass again for each `RL_WALK` | 176 | | not needed once facts are kept |

The prepass has one side effect that blocks plain reuse: it enqueues `__used`
static locals (`prepass.c:626-629`), which is why `RL_WALK` exists.

### Per function: the prepass (`prepass.c:448-706`)
One walk computes: `NA_LABEL` per node, `NA_ALLOCA` on the body; per local
`SA_USED`, `SA_WRITTEN`, `SA_AESC`, `SA_CWRITE`/`SA_CMULTI` with `cwval`,
`FF_*`, `luse`, `lnoreg`; per function `SA_MUSTFOLD`, `SA_NOINL`, `fnsize`,
`lspan`; per label `lref`, `lcdepth`; for VIR `inl_read`, call edges, the
setjmp bit.  It runs lazily, the first time a function is emitted or inlined
(`ensure_walked` 396-420, a CAS, the loser spins).  With VIR on it runs up
front for every defined function (`fn_isize` 324-329), so the laziness the
comment at 386-393 argues for is gone at `-O2`.

Later code that computes the same or a neighbouring fact again:

| Fact | In the prepass | Computed again by |
|---|---|---|
| contains a label or case | `NA_LABEL` 703 | `wk_branch_entry` `walk/stmt.c:520-571` (full walk per call; `wk_ternary` uses both, `walk/expr.c:1498` and `1519`); `wk_stmt_entries` 849 calls it at each level; wasm `wlabels` `arch/wasm/codegen/reachability.c:5` (inferred from its name) |
| has side effects | not computed | `side_effects` `arch/a64/codegen/builtin.c:14`, a walk per call: 10 calls in `walk/`, 14 arm64, 7 wasm |
| pure small integer expression | not computed | `em_bound_pure` `opt/cmpbounds.c:8`, `em_unused_pure` `opt/unusedinit.c:12`, `sv_constant` `opt/switch-values.c:24`, `em_sibling_value` `opt/sibling-values.c:14`, `em_control_int` `opt/control.c:45`, `wk_isconst` `walk/expr.c:2292`, `wk_ival` `walk/expr.c:3508` |
| condition is constant | not computed | `em_const_truth`, `wk_condfold`, `wk_ival`, `cond_fold` (arm64's), `wk_isconst` + `wk_gen`, `cc_eval`, `wk_constant_p` (Q-CGS-03) |
| cleanups in scope at a label | `lcdepth` 673 (a depth) | `wk_vla_scan` `walk/stmt.c:438-515` (a tree: `cn`, `lcn`), a second prescan per function and per inlined copy (1483, 1639) |
| callee is tiny (no asm, no over-aligned local) | not computed | `wk_tiny_ok` `walk/stmt.c:1274` per call site; arm64's `tiny_ok` `call.c:418` is the same code (226 tokens) |
| cases of a switch | not computed | `wk_cases` 575 per switch; then `em_switch_values_plan` and `em_switch_bitset_plan` read the body's statement list again; the case array is copied three times (711-725, 742-749, 756) |

### The lowering interface
`LO(name)` pastes a prefix (`x64_`, or `vw_` for VIR; `proto.h:12-19`).  A
target implements 76 functions: 75 declared in `proto.h:71-173` (one,
`undecl`, only under `WK_PREFIX`) and `LO(ecx_free)`, declared in
`walk/stmt.c:48`.

| Group | Functions |
|---|---|
| registers (7) | `treg tfree mov tkeep nfree nffree toreg` |
| memory (5) | `load store laddr copy zero` |
| integer (8) | `bin un ext cmp cmpbr brz umulhi smulhi` |
| switch (2) | `switch_bitset switch_table` |
| wide and `_BitInt` (12) | `ldmem i128mem wconst wbitint wbin wun wcmp wcvt bigconst bigbin bigcmp bigcvt` |
| float (9) | `ftreg fconst fbin fneg fsqrt root4 fcmp cvt fbits` |
| builtins (13) | `trap asm barrier setjmp longjmp frame bitop alloca tlsaddr atomic spget spset pcmpstr` |
| control (5) | `dbgrow label jump jumpr named` |
| calls (8) | `classify call params tail_return ret retrec va_start va_arg` |
| function (7) | `begin locals_begin local undecl slot finish ecx_free` |

Outside `LO()` a new target also edits: `do_item` (`tu.c:195-205`),
`ecx_free` (151-153), a state pointer in `ecx` (`emitimpl.h:400-403`), its
`*_switch_values` and `*_replay` prototypes (542-544, 745-746), and the
directive ternaries in `data.c:340, 370, 699`, `tu.c:536-541, 623-633`,
`obj.c:118`.

It is not one coherent interface.  It had 24 functions when the walker was
added (commit 00a2670a, 2026-11-10) and 76 three days later.  Signs of growth
by case: register ownership is stated per function in comments ("consumes
both", "the operand registers stay the caller's", "r freed", "l's register is
reused") and `LO(tkeep)` (19 uses) exists to undo a consume; `pcmpstr` and
`root4` are x86 builtins; `local`, `locals_begin`, `undecl` and `slot` exist
for VIR; `switch_bitset`'s x86 body is `x64_bin`/`x64_cmpbr`/`x64_brz` only
(`arch/x64/codegen/switch-bitset.c:8-23`), so it is walker code under a target
name.  The walker also asks who its target is without `LO()`: `X86(c)`
(`walk/expr.c:48, 2327`, `walk/stmt.c:1310`), `DARWIN(c)`
(`walk/switch-values.c:8`), `e->vtry` (`walk/expr.c:1000`,
`walk/stmt.c:1336-1371`), `#ifdef WK_PREFIX` (`walk/stmt.c:419, 1331, 1364,
1370`), and seven messages start with "x86-64:".

Documentation: only the comments in `proto.h`.  `doc/PLAN-x86.md:144-158`
describes `codegen/walk/lower.h` with "some thirty operations" of other names
(`iconst`, `lea`, `setcc`, `branch`, `prolog`); that file never existed.

### Plans and their emission (`codegen/opt/` against the walkers)
`codegen/opt/` decides on the AST and returns a small struct or a rewritten
operator; it emits nothing (one exception, Q-CGS-17).  Each of the three AST
walkers calls the plan and has its own file that emits it:

| Plan (`codegen/opt/`) | arm64 | shared walker (x86-64, VIR) | wasm |
|---|---|---|---|
| `udivconst.c` | `arch/a64/codegen/udivconst.c` | `walk/udivconst.c` | `arch/wasm/codegen/udivconst.c` (a subset) |
| `sdivconst.c` | `a64/.../sdivconst.c` | `walk/sdivconst.c` | not used |
| `sdivpow2.c` | `a64/.../sdivpow2.c` | `walk/sdivpow2.c` | `wasm/.../sdivpow2.c` |
| `switch-values.c` | `a64/.../switch-values.c` | `walk/switch-values.c` | `wasm/.../switch-values.c` |
| `switch.c` (bit set) | `a64/.../switch-bitset.c` | `LO(switch_bitset)`: `x64/.../switch-bitset.c` | `wasm/.../switch-bitset.c` |
| `mulconst.c`, `tail.c` | `a64/.../mulconst.c`, `tail.c` | `x64/.../mulconst.c`, `tail.c` | none |

The walker's four files are already target-neutral: they use only `LO(bin)`,
`LO(mov)`, `LO(cmpbr)`, `LO(umulhi)`.  arm64's files hand-pick instructions
(`UMULH`, `MSUB`, `UBFM`) for the same recipe steps; they disappear when
arm64 implements `LO()`, not before.  wasm is a stack machine and shares only
the plan.  So: the emission side can be shared through `LO()` for every
register target, and today is for two of three; the per-target `switch_bitset`
can move into the walker now.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-CGS-01 | L7 | misplaced | `note_ref`/`claim_ref` `arch/a64/codegen/bind.c:4,11`; `out_chunk`, `sym_header`, `ctor_entry` `func.c:6,16,118`; `walk_fn`, `ael_slot`, `direct_callee` `call.c:342,369,645`; `is_local_var` `bind.c:185`; `side_effects`, `strip_casts` `builtin.c:14,6`; `ptr_to_vla` `lval.c:69`; `dbg_where` `stmt.c:1020`; `cond_fold` `cond.c:184`; `em_is_cmp` `arith.c:92`.  `walk/walk.h:17-19` says so | The shared layer cannot be read or built without arm64's files; every new client depends on them | `codegen/ref.c` (claims, chunks, symbol header), `codegen/astq.c` (purity, locals, casts), `codegen/range.c` (`cond_fold`) | M | low: moves | `vcc-same.py`, both builds |
| Q-CGS-02 | L7 | mixed file | `emitimpl.h:688-691, 732-736, 748-925` arm64 prototypes (about 190 lines, sectioned by a file name without its directory); `:118`, `:1048` arm64 includes; `tempregs` `tu.c:17`, `tidx` `emitimpl.h:696-704`; about 40 `ecx` fields only arm64 reads (section 5); `ecx_init` `tu.c:103-123` reserves all 16 regions for every target | Nobody can tell shared state from arm64 state; x86-64 and wasm carry arm64's context | Core header (emitter, context core, `val`, `lval`, refs, items); prototypes to `a64impl.h`; arm64 fields into an `a64st` beside `wkst`/`xst`/`wst` | S header, L fields | low / medium | `vcc-same.py`, `vbench.py gate` |
| Q-CGS-03 | L4 | redundant walk | `wk_condfold` `walk/expr.c:3598-3612` runs `em_const_truth`, `side_effects`, `wk_ival`, `cond_fold` in turn; `wk_stmt1` asks it for an `if` (`walk/stmt.c:996`) and `wk_cond` asks again for the same node (`expr.c:3619`), then once per operand of `&&`, `\|\|`, `!`, casts (3634-3704); `wk_ternary` `expr.c:1497-1529` adds `wk_isconst` + `wk_gen` twice, `cond_fold`, `cc_eval` | The four evaluators run twice or more over a non-constant `if` condition before code is made; cost not measured here | One `em_truth(e, n)` with the four evaluators fused, result passed down instead of asked again | M | medium: evaluator order is load-bearing | `vcc-same.py`; count from `quality-passes.md` |
| Q-CGS-04 | L4 | pattern | The eight predicates in the table of section 3 (side effects, pure integer expression) | Eight switches over the same node kinds with different accepted sets, kept in step by hand; none cached | One `em_expr_class` computed in the prepass walk into `nflags` (6 bits free), or one parametrised predicate | M | medium: the sets differ on purpose | `vcc-same.py` |
| Q-CGS-05 | L4 | redundant walk | `wk_branch_entry` `walk/stmt.c:520-571` against `NA_LABEL` `prepass.c:703` | A second definition of "can be entered" beside the prepass bit | Add the two extra causes (VLA cast, VLA or cleanup declaration) as a second prepass bit | S | low | `vcc-same.py` |
| Q-CGS-06 | L4 | redundant walk | Table "what a round recomputes" above | Each extra round repeats the whole prepass, thread start and tail text | Keep `E`'s fact tables, threads and contexts across rounds; text in the last round | M | medium | `vcc-same.py`, kernel build; rounds per TU first (not measured) |
| Q-CGS-07 | L6 | stale doc | Section 3 "The lowering interface" | A target author has 76 prototypes and no rules; the plan describes another interface | `walk/lower.h` with the ownership rule stated once; target questions as `LO()` predicates; x86 builtins behind one `LO(builtin)` | M | low | `vcc-x86.py`, `vir-test.py` |
| Q-CGS-08 | L1 | duplicate | Known `a[K]` constants kept twice: arm64's `kval`/`kvalid` through `ael_slot` (`walk/expr.c:1780-1788, 3361-3367, 3527-3529`, reset `walk/stmt.c:10, 1564`) and the walker's `wkst.ael` (`walk/expr.c:1693-1704, 3368-3369`, `walk.h:110-113`) | Two trackers updated and invalidated by different rules for one fact | Keep `wkst.ael`; drop the arm64 one from the walker | S | medium | `vcc-same.py`, `vcc-x86.py` |
| Q-CGS-09 | L1 | near-duplicate | Inside `walk/`: store a value by kind (`wk_putmem` or register store) at 9 sites: `expr.c:910-911, 951-959, 1355-1363, 2541-2548, 2819-2826, 3481-3489`, `stmt.c:1149-1157, 1420-1428, 1463-1471`; compound operator `expr.c:1765-1776 = 1878-1889` (exact); comparison `1270-1281 = 1416-1424`; CAS retry tail `1897-1908 = 2507-2515`; park while short `1252-1265 = 1749-1758`; sign-extend pair 8 times (528, 537, 570, 629, 894, 1677, 2810, 3107); bool-vector lane lvalue 6 times (932, 1292, 1347, 2930, 2939, 2944); `novtag` bracket 7 times | The walker is 8.8 % redundant by the scan, the highest of any directory over 5 k lines | `wk_store_any`, `wk_compound`, `wk_cas_retry`, `wk_sext`, `wk_lane_lval`, `wk_volatile` | M | low | `vcc-same.py` |
| Q-CGS-10 | L4 | long function | `emit_tu1` `tu.c:400-767` (367 lines) and the nine jobs of `tu.c` | The scheduler, the output format and the rounds cannot be changed apart | `codegen/pool.c` (queue, worker, contexts), `codegen/tuout.c` (header, tail, DWARF unit, write), replay driver into `obj.c`, `tu.c` keeps the rounds | M | low | `vcc-same.py` |
| Q-CGS-11 | L4 | mixed file | `prepass.c`: `fn_once`, `inl_constarg`, `inl_cand`, `fn_isize` 242-384 (VIR's inlining sizes); `sve_codegen_ok`, `bitint_codegen_ok` 24-85 (two scans of every symbol and node per TU); `sym_align` 7; `inline_object_size` 718 | A third of the file is not the prepass | `codegen/inlsize.c`; capability checks to a flag the front end sets when it makes the type | S | low | `vcc-same.py` |
| Q-CGS-12 | L4 | long function | `wk_builtin` `walk/expr.c:2848-3268` (421 lines); below | | Split by family; one FP-class implementation | M | low | `vcc-same.py` |
| Q-CGS-13 | L4 | long function | `wk_stmt1` `walk/stmt.c:921-1250` (330), `wk_switch` 644-809 (166); below | | `wk_block`, `wk_if`, `wk_return`; one case array | S | low | `vcc-same.py` |
| Q-CGS-14 | L1 | duplicate | `em_trunc_to` `type.c:4-25` = `trunc_to` `frontend/c/const.c:4-24` (91 tokens exact); 96 calls of the emit copy | Two definitions of how an integer of a type wraps | Export one from `frontend/c/` | S | low | `vcc-same.py` |
| Q-CGS-15 | L2 | pattern | "plain local integer" written out at `opt/sibling-values.c:30-32, 131-133, 139-141, 153-155, 178-181, 207-210`, `opt/repeated.c:24-26`, `opt/locals.c:12-15`, `opt/unusedinit.c:72-75`, `opt/switch-values.c:71-72`; "integer of 4 or 8 bytes, not `_BitInt`" in 12 plans; `opt && !noopt` 18 times here, 51 in all emitters; operator list `+ - * & \| ^` 4 times (`sibling-values.c:45, 89, 148, 201`) | A rule about locals changes in ten places | `em_plain_local`, `em_word_int`, `em_opt_on` in one `opt/optimpl.h` | S | low | `vcc-same.py` |
| Q-CGS-16 | L1 | near-duplicate | `emit_object` `data.c:393-614`: seven exits; `.zero`-or-bytes block 455-461, 490-496, 518-524, 594-606; Darwin descriptor text 512-535 = 552-571; both arms equal at 595-603; directive names by ternary `data.c:340, 370, 699`, `tu.c:623-633` | A new target or section kind edits every exit | One `obj_body` + `obj_tail`; a per-target directive table | S | low | `vcc-same.py`, `vcc-darwin-s.py` |
| Q-CGS-17 | L5 | misplaced | `opt/switch-values.c:156-218` stores tables on `ecx` (malloc'd list) and writes sections, in the directory whose rule is "plan only"; `flow/values.[ch]` has one user, `opt/sibling-values.c`; `em_rec_*` prototypes in `emitimpl.h:655-687`, not `rec/rec.h`; the bit-set plan is in `opt/switch.c` | The directory names stop predicting content | Table output to `data.c`; `values.*` to `opt/`; prototypes to `rec.h` | S | low | `vcc-same.py` |
| Q-CGS-18 | L4 | redundant walk | `wk_vla_scan`, `wk_tiny_ok`, `wk_cases` (table in section 3) | Three more partial walks of a body per function, call site and switch | Cleanup tree and "tiny" bits from the prepass walk | M | medium | `vcc-same.py` |
| Q-CGS-19 | L2 | pattern | `wk_state` `walk/stmt.c:26-43` and `wk_ecx_free` 60-77: 18 sizes written twice; three allocation idioms in the area: `vmreg_get` 32, `reserve` + `munmap` 49/42, `malloc`/`calloc`/`realloc` 31 (`fn_isize` 314-320 has eleven) | A size changed on one side unmaps the wrong length | A table of per-sym, per-label, per-node arrays; one region set as `tu.c` uses | S | low | ASan suite |
| Q-CGS-20 | L6 | stale doc | `emit.h:4` and `emitimpl.h`'s opening comment describe an arm64-only emitter without IR; comments on the wrong line: `:194` (`cwval` carries `walked`'s, `:179` has none), `:215` (`nerrcall` carries `print_ns`'s), `:960` (`inl_cand` carries `fn_once`'s); `proto.h:35` names `codegen/walk/func.c`; `PLAN-x86.md:127-135, 144, 296` list files that do not exist | The first thing a reader meets is wrong | Rewrite with section 2's draft | S | none | review |
| Q-CGS-21 | L9 | error policy | `wk_call` returns without freeing a malloc'd `args` at `walk/expr.c:2185, 2189, 2213, 2224`; `2213` has two equal arms; `wk_weak0` `expr.c:71-79` and `ref_weak` `emitimpl.h:1022-1028` answer "may this address be 0" by different rules (inferred: no test written) | A leak above 64 arguments; two answers to one question | `VAUTOFREE`; one predicate | S | low | `vcc-ctests.py`, a test for a weak tentative object |
| Q-CGS-22 | L10 | dead | `wk_userlabel` `walk/stmt.c:151` never called; `wk_incdec` unused locals `expr.c:1925-1931`; `em_select_locals`' callback is `em_local_gpr_type` at both call sites (`arch/a64/codegen/func.c:776`, `arch/x64/codegen/locals.c:38`); `em_cmpzero`, `em_const_strcmp` have only wasm as outside caller, and the walker folds `strcmp` itself (`expr.c:2174-2187`) | | Remove; call `em_const_strcmp` from `wk_call` | S | none | `vcc-same.py` |
| Q-CGS-23 | L1 | pattern | `FN_*` `emitimpl.h:409-417` and `fixed_names` `type.c:29-41` are parallel lists; setjmp names at `prepass.c:548-549`, `opt/tail.c:34-35`, `arch/x64/codegen/vir.c:983-985`; inline-definition rule `tu.c:254-259` = `inline_only` `emitimpl.h:104-111` | Lists drift | One X-macro; `em_is_setjmp`; `is_root` calls `inline_only` | S | none | `vcc-same.py` |
| Q-CGS-24 | L8 | global state | `getenv` per round: `VCC_LREGS`, `VCC_EMIT_STATS` (twice, `tu.c:445, 731`), `VCC_NOOPT`, `VCC_OBJ_TEXT`, `VCC_EMIT_REPEAT` | Under vccd the server's environment decides, read per TU | Into `cc_opts`, read once by the driver | S | none | `vccd-test.py` |
| Q-CGS-25 | L3 | thread | Threads created and joined per round (`tu.c:519-525`), `th[64]`; spin wait `prepass.c:418-419`; one lock per chunk (`arch/a64/codegen/func.c:10-13`) and per enqueue (`tu.c:76`) | A private pool; see `quality-scheduling.md` | The shared scheduler | - | - | - |
| Q-CGS-26 | L1 | pattern | `rec/` passes take records as bytes in three shapes: a layout struct (`fallthrough.c:6`), loose stride and offset (`unreachable.c:7`), a callback struct (`copies.c:7`), because `ins` and `rec` are two 16-byte types (`rec/rec.h:5-10`) | Untyped offsets where one record type would give fields | One record type when arm64 moves; until then one layout struct | M | medium | `quality-backend-records.md` |
| Q-CGS-27 | L5 | mixed file | `walk/expr.c` holds eleven topics in 3731 lines; `PLAN-x86.md:127-135` planned ten files | Nothing can be found by file name | `lval.c bitfield.c conv.c arith.c vla.c assign.c atomic.c call.c builtin.c fpclass.c cond.c`; regenerate `prefix.h` | S | low: moves | `vcc-same.py`, `gen-walkprefix.py --check` |

**Q-CGS-12, the jobs inside `wk_builtin`.**  (1) pass to the CPU: sqrt, trap,
setjmp, frame address, bit operations, `pcmpstr`; (2) constants:
`constant_p`, object size, the `cc_eval` default; (3) pass-through of the
first argument; (4) vector shuffle and convert, lane by lane (2916-2955); (5)
atomics, by `wk_atomic`; (6) `alloca` with the VLA save slots; (7) `clrsb` and
bit reverse built from `LO(bin)`; (8) the overflow builtins by three
strategies, 65 lines in place (3051-3115) beside `wk_overflow_wide`; (9) FP
classification for three representations, 83 lines in place (3118-3200),
while `wk_fpclass`/`wk_fptest`/`wk_fpattern` (2558-2684) classify the same
values another way; (10) sign bit and ordered comparisons; (11) `va_start`,
`va_end`, `va_copy`.  FP format widths are chosen by ternary chains at
`expr.c:2722, 3190-3199` and `data.c:209`: one table of (exponent, mantissa
bits) per type serves all three.

**Q-CGS-13, the jobs inside `wk_stmt1`.**  Reachability gate and debug row;
block (VLA stack restore, value-reuse substitution, cleanups: 936-974, with
the "is a VLA variable" test written at 943-944, 955-956 and `451-452`);
expression statement; `if` (994-1045: three ways to find a constant, then
`wk_cond`); the three loops; case and label placement; goto, break,
continue; `return` in three forms (inlined copy, record, scalar: 1130-1221);
asm (empty-asm barrier test); computed goto.  `wk_switch` has five strategies
in sequence and pushes the loop record by hand twice (675-677, 797-799) above
`wk_loop_push` (813).

## 5. Names (L5, L6)

The fields asked about, and where they are read:

| Name | Where | What it is | Read by | Proposal |
|---|---|---|---|---|
| `E->mf` | `emitimpl.h:213` | per sym: a static function in which an `error()` call survived folding; inlined everywhere from the next round on | arm64 only | `must_inline` |
| `E->K` | `:216` | the previous round's kept function chunks and logs | arm64 only (rounds) | `prev_round` |
| `E->newmf` | `:214` | functions marked this round: asks for another round | arm64 only | `marked_now` |
| `cwval` | `:194` | per local with `SA_CWRITE`: the one constant every assignment stores | arm64, walker | `const_write_val` |
| `lcdepth` | `:199` | per user label: cleanup variables and VLAs in scope at its definition | arm64 only | `label_cleanup_depth` |
| `luse` | `:189` | per local: uses weighted 8 per loop level, saturating | `opt/locals.c` (arm64, x86-64) | `use_weight` |
| `lnoreg` | `:190` | per local: must be in memory (address taken, asm memory operand, brace initializer) | arm64, walker, VIR | `needs_memory` |
| `lspan` | `:191` | per function: lowest and highest local sym | `opt/locals.c` | `local_range` |
| `e->vlow` | `:405` | the records are VIR's arm64 lowering | arm64, VIR | `from_vir` |
| `e->vtry`, `vfail` | `:404, 406` | the VIR walker runs: errors are recorded and the function falls back | walker, VIR, `tu.c` | `vir_attempt`, `vir_fail_reason` |
| `ndl`, `dl` | `:324-325` | `-g` line rows, each at a record index | arm64, x86-64 | `dbgrows` |
| `nlr`, `lrsym`, `lrreg` | `:375-377` | the locals given callee-saved registers | arm64 only | into `a64st` |
| `resv` | `:372` | temporaries never handed out (held by register locals, x28 when realigned) | arm64 only | `reserved_temps` |
| `live` | `:371` | temporaries in use | arm64 only | into `a64st` |
| `dry` | `:381` | depth of `dry()`: code made and thrown away, no references claimed | arm64 10, wasm 1, walker 1 | `discard_depth` |
| `novtag` | `:321` | inside a volatile access: records stay untagged | arm64, walker, VIR | `volatile_depth` |

arm64-only but in the shared `ecx` (no reference in `walk/`, `arch/x64`,
`arch/wasm`): `in nin capin sla sstk b lm caselbl lrefc nlabel lkvi kvs
inst_next foff fmax falign vsz cl_top outmax vararg vaslot ngr_named
nstk_named nvr_named vrslot nsrn live resv inlfree spill nlr lrsym lrreg
usedcs loops nloop clean nclean f inline_depth cases jtl jt`.  Shared by all:
`E c out data rel raw reach top curloc bail rlog weakref calls`.

Other identifiers:

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `E` / `e`, `emitter` / `ecx` | everywhere | one round's TU state / one thread's context, told apart by letter case; `ecx` is also an x86 register | `emround`, `emthread` (STYLE.md decides) |
| `walk`, `walk_fn`, `ensure_walked`, `walked`, `RL_WALK` | `prepass.c`, `call.c:342` | the prepass, next to `codegen/walk/`, the code generator | `prepass_fn`, `prepassed` |
| `reuse` | `tu.c:157`; `e->reuse_from` | a kept chunk of the last round; a value-reuse substitution | `round_reuse`; `subst_from` |
| `item` | `tu.c:186`; `emitimpl.h:443` | a queue entry (sym, or string with the top bit); an object stream record | `job`; `item` |
| `slot` | `slot()`, `wk_slot`, `ael_slot`, `em_slot_*`, `recipe.slots` | frame slot, known-value index, stack cell of a record pass, table entries | `em_cell_*` for the record pass; `kv_index` |
| `inst` | `bind.inst`, `wkst.inst`; `ins`, `nins` | inline instance; instruction | `copy_id` |
| `opt` | `codegen/opt/`, `arch/a64/codegen/opt.c`, `o->opt`, `noopt` | AST plans, a record optimizer, the `-O` level | `codegen/plan/` |
| `keep` | `struct keep`, `keep_cl`, `LO(tkeep)`, `EM_UNUSED_KEEP` | four unrelated things | `struct round_keep`, `LO(reuse_reg)` |
| `control.c` | `opt/` | constant truth of conditions | `consttruth.c` |
| `sv_*`, `svproof` | `opt/switch-values.c` | statics without the `em_` prefix | `em_sv_*` |
| `wk_assign` / `wk_assign_`, `wk_ulabel` / `wk_userlabel` | `walk/expr.c:1687, 1708`, `walk/stmt.c:139, 151` | wrapper and body told apart by an underscore | `wk_assign_track`, drop the second |

Prefixes: `em_` for `opt/`, `rec/`, `flow/` and some of `emitimpl.h`; `wk_`
for the walker; `it_`/`r_` for items and replay; the rest of the shared code
(`reserve now enqueue reuse do_item worker is_root replay prim tsize`) has
none.  Breaks: `em_is_cmp` is defined in `arch/a64/codegen/arith.c:92`;
`a64_switch_values`, `wk_switch_values`, `wswitch_values` are declared in the
shared header (`emitimpl.h:542-544`); `opt/sibling-values.c`,
`opt/switch-values.c` and `flow/values.*` are indented with one space, the
rest with tabs.

## 6. Decisions needed

1. **Move arm64-hosted shared services now, or with the arm64 migration?**
   (Q-CGS-01, 02.)  Recommendation: now, as moves only; it is what makes
   "arm64 onto the walker" a visible diff later.
2. **Rounds.**  Keep the TU-level retry and make it cheap (Q-CGS-06), or drop
   it when arm64 inlines through the walker?  Needs one number first: TUs
   with more than one round in a kernel build and their share of emit time.
3. **Purity and constant truth: one prepass classification or the bounded
   local proofs?**  The plans were written "without another walk" on purpose.
   Decide from `quality-passes.md`'s instruction counts for `side_effects`,
   `wk_condfold` and `wk_branch_entry`.  Recommendation: fuse the truth chain
   (Q-CGS-03) regardless; classify in the prepass only if the counts show it.
4. **The lowering interface.**  Freeze and document it as `walk/lower.h` with
   one ownership rule, or first shrink it (x86 builtins behind one hook,
   `switch_bitset` into the walker, target questions as predicates)?
   Recommendation: document first, shrink before arm64 implements it.
5. **`codegen/opt/` granularity.**  19 files for 1393 lines.  One recipe per file
   as today, or five by topic (`div`, `intfold`, `switch`, `valuereuse`,
   `reglocals`) with a shared `optimpl.h`?  Recommendation: by topic.
6. **Split `walk/expr.c` before or after the unifications?**  It changes 4.2 k
   redundant tokens' addresses for every open branch.  Recommendation: in the
   walker's rename window, before arm64 work starts.
7. **One record type** (`ins` = `rec`) before generic record passes grow
   further (Q-CGS-26); with `quality-backend-records.md`.

## 7. Not covered

- No build, no run, no measurement: every cost above is a count of walks or
  copies, not instructions.  How often a TU takes a second round is unknown.
- `walk/prefix.h` and `unprefix.h` (generated): heads only.  `codegen/vir/` is
  another review; I read only `vir/walk.c`'s head for the instantiation.
- arm64's, wasm's and x86-64's files were read only where the shared code
  calls into them (the definitions cited in Q-CGS-01, three recipe files,
  `need_fold`); the concept comparison is `quality-walker-a64.md` and
  `quality-walker-wasm.md`.
- `wk_weak0` against `ref_weak` (Q-CGS-21) and the `wlabels` equivalence are
  inferred from the source, not tested.
- The clone scan was run with `--only codegen/` (159 runs, 124 outside
  `codegen/vir/`); runs under 60 tokens were not triaged.  The 32/64-bit pairs in
  `opt/udivconst.c` and `opt/sdivconst.c` are deliberate (no host 128-bit
  division for 32 bits) and are not findings.
