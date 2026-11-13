# Quality review: passes and traversals, the pipeline of one TU (2026-11-13, main at 2e6d3e73)

Paths are relative to `src/`.  Measured with the tools in `build/src/` (built 13:13 the same day),
one `vpp --batch -j 1` process per mode, `VPP_NOHC=1 VCC_NODC=1`, one warm pass first.  "M" is million
instructions retired (`include/port/vperf.h`); "% of TU" from a 6 s `sample` of the same batch is share of
samples under `ppd_run1` (about 4 800 samples, so 0.02 % a sample; statics inlined into their caller
are counted with the caller).  Scripts and raw outputs:
`/private/tmp/claude-501/-Users-johandahlin-dev-vroom--claude-worktrees-clang-o2-coverage-a3f66f/78302153-21e1-434d-8eb1-e4002f260157/scratchpad/passes/`.

## 1. Summary

Every stage calls its passes by hand; there is no pass list as data, and three phase counters
(`PPD_PH_CC`, which contains `PPD_PH_EMIT`, and `PPD_PH_OBJ`) are all that can be timed by name.
Where the time goes depends on the input: on kernel TUs the preprocessor is 53 % and all code
generation 13 %; on SQLite (one code-heavy TU) code generation is 58 % and the record clean-ups
alone (`opt_function`) 24 % of the compile.  Through VIR the same TU costs 6.6 times the direct
emitter's code generation.  The three things that matter most:

1. **The record clean-ups are the most expensive pass in the compiler and cannot be seen into.**
   About 16 sweeps in `opt_function`, 25-29 over the records per function in all, and they run in
   full a second time after VIR's allocator (10.5 % of a VIR compile).  No counter separates them.
2. **VIR's analyses are computed up to five times per function** (CFG 5, dominators 3 on VIR plus
   1-2 on the pre-IR, all with the same answer), and SPEC-vir.md 22.6 budgets 7 sweeps where the
   code does about 20 on the pre-IR, 4 in `vir_canon`, and the arm64 record sweeps after that.
3. **The front end answers "is this reachable / is this condition constant" four times** (parser,
   `fl_stmt`, `uv_stmt`, `stmt_flow`), then every emitter asks again at every level of `&&`/`||`.
   The flow walks are 0.9 % of a kernel TU but 6.8 % of SQLite.

## 2. Inventory (the stage drivers, read in full unless noted)

| File | Lines | What it does in the pipeline | Better name | Header |
|---|---|---|---|---|
| `driver/ppdriver.c` | 1968 | `ppd_run1`: argv to options and predefines (lines 903-1757, read 480-590 and 1560-1968 only), then `pp_open`, `cc_run`, `write_obj`; owns the phase counters | `driver/job.c` | good, but says "-E job" only: stale |
| `bin/vpp/vpp.c` | 271 | batch runner; prints `cc_run/emit/obj` instructions (`--stats`) | | good |
| `frontend/c/tu.c` | 344 | `cc_run`: tables, `translation_unit`, `lazy_closure`, end-of-TU symbol sweeps, `emit_tu` | `frontend/c/run.c` | label only |
| `frontend/c/lazy.c` | 211 | capture a header inline's body as tokens, parse it when referenced | | label only |
| `frontend/c/func.c` | 1027 | `function_def` (the post-body sequence), a second reachability walker for the -Wreturn-type path note, instrumentation, two linear post-body folds, `translation_unit` | split: `flowpath.c`, `instrument.c` | label only |
| `frontend/c/flow.c` | 1403 | `body_falls_off` (first 370 lines read; reachability) and `uninit_check` (entry read, body skimmed) | `frontend/c/flow-reach.c`, `frontend/c/flow-uninit.c` | says "falls off" only; holds two analyses |
| `codegen/tu.c` | 809 | `emit_tu`: rounds, `emit_tu1`: roots, work queue, stitch, object or text out | `codegen/unit.c` | label only |
| `codegen/prepass.c` | 737 | per-function AST walk (facts for the emitters), two whole-TU scans, VIR's call graph and inlining sizes | split: `facts.c`, `inlsize.c` | label; good comment at :386 |
| `codegen/obj.c` | 245 | the item stream replayed into the assembler and `ElfObj` | `codegen/items.c` | label only |
| `arch/a64/codegen/func.c` | 998 | `emit_function` (retries), parameters, `finish_function`: clean-ups, frame, tables, relaxation, text or items | split frame/finish from the walk's entry | label only |
| `arch/a64/codegen/opt.c` | 1906 | `opt_function`: liveness and 8 clean-up passes; `opt_regs`, `opt_noframe` | one pass per file (Q-passes-13) | good for 4 of the passes |
| `arch/a64/codegen/obj.c` | 704 | `a64_replay`: records to words, branch relaxation, fixups (read 436-704) | `encode.c` | label only |
| `codegen/walk/stmt.c` | 1650 | `wk_function` and statements of the shared walker (read 430-560, 845-882, 1536-1650) | | label only |
| `arch/x64/codegen/lower.c` | 1037 | `LO()` operations as records; `x64_finish` (read 560-1037) | | label only |
| `arch/x64/codegen/dead.c`, `obj.c` | 183, 169 | dead results via the shared liveness; records to mnemonic strings for the assembler | | label |
| `codegen/vir/try.c` | 147 | `vw_try`: build, verify, lower, or fall back | | good |
| `codegen/vir/build.c` | 3987 | the builder and `vw_emit`'s pass sequence (read 1-48, 1739-1978, 3060-3290, 3893-3987) | split per pass (in quality-vir-build's area) | good |
| `codegen/vir/a64.c` | 2505 | `vw_a64`: loops, allocation, lowering, then `finish_function` (read 2268-2505) | | good |
| `vir/vir.c` | 895 | `vir_cfg`, `vir_dom`, `vir_uses`, `vir_canon` (read 390-745) | | good |
| `vir/alloc.c` | 2156 | `vir_alloc` (read 1930-2156) | | good |

What a newcomer must know: (a) the parser builds one TU-wide node array and never frees it; a
function's nodes are the index range `[object_node0, c->nnode)`, which two passes scan linearly;
(b) code generation is lazy and demand-driven: roots are queued, a function is generated when
something references it, its prepass runs the first time it is emitted or inlined; (c) inlining
is at AST level: the callee's body is walked again at every call site (kernel sample: 1 106
functions emitted, 8 984 inlined copies); (d) a function can be generated several times (retries,
rounds, VIR then direct), so every per-function fact must be either per attempt or immutable.

## 3. Passes and data flow

### 3.1 The pipeline, direct arm64 emitter (the kernel build path; kernel command lines carry `-O2`)

"O1+" rows are skipped at `-O0`.  Counts are per function unless "TU".

| # | Pass | Entry | Walks | Times | Produces | Needs |
|---|---|---|---|---|---|---|
| 1 | lex: classify, tokens, intern | `frontend/lex/lex.c:55`, `:488`, `intern_list` at `:1505` | file bytes; 64-bit masks; identifier list | 2 sweeps + 1 over identifiers, once per file per process (token cache) | token array | bytes |
| 2 | preprocess (pull) | `pp_next` `frontend/pp/setup.c:237` | token arrays, macro bodies | 1 per token delivered | `ptok` stream | 1 |
| 3 | parse + sema, one pass | `translation_unit` `frontend/c/func.c:989` from `frontend/c/tu.c:313` | `ptok` stream | 1 | types, syms, nodes; constant folds (`cc_eval`) at each `if`, `&&`, `?:` (`frontend/c/stmt.c:1063`, `frontend/c/exprop.c:931`, `:994`) | 2 |
| 3a | lazy capture | `lazy_capture` `frontend/c/lazy.c:85` | the body's tokens, pulled through pp | 1 per static header function; 130 308 kept, 4 628 parsed in 9 kernel TUs | token copy (`lzt`) | 2 |
| 3b | lazy parse | `lazy_closure` `frontend/c/lazy.c:180` | the copied tokens | fixpoint over the list until nothing new is referenced | nodes | 3 |
| 4 | falls off the end | `body_falls_off` `frontend/c/flow.c:344` (`frontend/c/func.c:939`) | statement tree | 1, up to 16 with gotos; non-void or noreturn functions only | 0/1/2 | 3 |
| 4b | path note | `return_type_warn` `frontend/c/func.c:708` | statement tree, `stmt_flow` + `rp_walk` (quadratic in depth) | only when 4 warns | diagnostic labels | 4 |
| 5 | uninitialized uses | `uninit_check` `frontend/c/flow.c:1322` (`frontend/c/func.c:948`) | statement and expression tree | 1, up to 16; when the parser saw a candidate (`uv_any & 14`) | diagnostics | 3 |
| 6 | instrument | `frontend/c/func.c:97` | statement tree | 1 with `-finstrument-functions` | rewritten returns | 3 |
| 7 | fold local builtins (O1+) | `frontend/c/func.c:827` | the function's node range, linearly | 1 scan; 3 more if it holds `__builtin_constant_p` or a fast-math call | nodes overwritten | 3 |
| 8 | object-size locals | `frontend/c/func.c:744` | node range and sym range, linearly | 3 scans; `SF_BOS_QUERY` functions only | `SF_BOS_INIT` | 3 |
| 9 | end of TU | `frontend/c/tu.c:318-328` | all syms | 3 sweeps per TU | diagnostics | 3 |
| 10 | SVE / _BitInt gate | `codegen/prepass.c:24`, `:65` | all syms, all nodes | 1 per TU, arm64 always | error or nothing | 3 |
| 11 | emit setup | `emit_tu1` `codegen/tu.c:400` | all syms (`:459`), top-level list | per round | queue, per-sym tables | 3 |
| 12 | prepass | `ensure_walked` `codegen/prepass.c:396`, `walk` `:448` | AST of one function | 1 per function per round, on first emission or inlining | `sflags`, `ffacts`, `luse`, `lspan`, `lnoreg`, `lref`, `lcdepth`, `fnsize`, `nflags` (`NA_LABEL`, `NA_ALLOCA`) | 3 |
| 13 | choose register locals (O1+) | `choose_lregs` `arch/a64/codegen/func.c:768` | sym range `lspan` | 1 per attempt | `lrsym` | 12 |
| 14 | emit walk | `emit_function1` `func.c:790`, `gen_stmt` `:969` | AST of the function and of every inlined callee, per call site | 1; up to 3 (`func.c:986-996`); operands of `__builtin_constant_p` once more, discarded (`dry`, `builtin.c:80`) | records `e->in` | 12, 13 |
| 15 | unreachable records (O1+) | `a64_unreachable` `unreachable.c:16` | records | 1 | fewer records | 14 |
| 16 | jump to next (O1+) | `drop_fallthrough` `func.c:249` | records | 1 | | 14 |
| 17 | record clean-ups (O1+) | `opt_function` `opt.c:1854` | records | liveness 4 sweeps + block fixpoint (`:248`); spill pairs 1; `copies` 2; `slots` 3; `exts` 1-3; `copies` 2; `dead` 1; compaction 1 | fewer records | 14 |
| 18 | frame facts (O1+) | `opt_regs` `opt.c:1020` | records | 1, recomputing every record's roles, reads, writes | used registers, flags | 17 |
| 19 | finish | `finish_function` `func.c:474` | records | `expand_tables` 0-2, prologue-marker scan 1 (`:553`), tail scan 1 (`:563`), `tail_expand` 1, frame-slot rewrite 1 (`:571`), `tbz_relax` 1-3, raw scan + copy (`finish_direct` `:152`) | an item in `e->out` (or text: one printing sweep `:625`) | 17, 18 |
| 20 | rounds | `emit_tu` `codegen/tu.c:794` | everything from 11 | 1; again (up to 8) while inlining marks change; unchanged functions are copied (`reuse` `:157`) but 12 runs again for all | chunks | |
| 21 | stitch and encode | `replay` `codegen/tu.c:287`, `a64_replay` `arch/a64/codegen/obj.c:514` | chunks; records | 1 encode sweep, label patches, fixups (single thread) | `ElfObj` frags | 19 |
| 22 | object layout and write | `write_obj` `driver/ppdriver.c:562` | sections | 1 | file | 21 |

Sweeps over the records of one function at `-O2`: 25-29 (rows 15-19 and 21), about 16 of them in
`opt_function`.  At `-O0`: 9-11.

### 3.2 x86-64 through the shared walker (rows 1-12 as above)

| # | Pass | Entry | Walks | Times | Produces |
|---|---|---|---|---|---|
| 13x | register locals | `x64_locals_begin` `arch/x64/codegen/locals.c:28` | sym range | 1 | homes |
| 14x | VLA / cleanup scan | `wk_vla_scan` `codegen/walk/stmt.c:438` (`:1639`) | statement tree | 1 | zeroed sp slots, cleanup chain |
| 15x | emit walk | `wk_function` `codegen/walk/stmt.c:1542`, `wk_stmt` `:1641` | AST, inlined callees per call site | 1 (no retry) | `rec` records |
| 16x | unreachable, jump to next (O1+) | `x64_finish` `lower.c:641`, `:645` | records | 1 + 1 | |
| 17x | dead results (O1+) | `x64_dead` `dead.c:179` | records | blocks 1, transfer 1, fixpoint, removal 1, compaction 1; only after a discarded scalar | |
| 18x | two-address form, copies forwarded | `x64_twoaddr` `print.c:23`, `:38` | records | 1 | |
| 19x | print or items | `x64_print` `print.c:196`, `:312` | records | 1-2 | text, or `IT_X64` records |
| 21x | encode | `x64_replay` `arch/x64/codegen/obj.c:81` | records | 1: each record becomes a mnemonic string and operands for `as_x64_insn` | frags |

### 3.3 `-O2` through VIR (`VCC_VIR=bl`; rows 1-12 as above)

| # | Pass | Entry | Walks | Times | Produces |
|---|---|---|---|---|---|
| V0 | call counts, inlining sizes | `fn_once` `codegen/prepass.c:242`, `fn_isize` `:309` | all nodes of the TU; then the prepass of every defined function, used or not; Tarjan over the call graph | 1 per round | `once`, `isz` |
| V1 | VIR walk | `vw_try` `codegen/vir/try.c:91`, `vw_wk_function` (the walker of 3.2 under `vw_`) | AST as 14x-15x | 1 | pre-IR `V->p`, with local CSE and SSA placeholders |
| V2 | pre-IR passes | `vw_emit` `codegen/vir/build.c:3895` | pre-IR | `vw_thread`, `vw_rpo`, `vw_preds` (2 loops), seal, `vw_promote`, `vw_trivial` (fixpoint), `vw_deadparams`, `vw_simplify` (7 sweeps: constants count, uses, `vw_doms`, far table, CSE over the dominator tree, `vw_ownaddr`, dead), optionally promote-trivial-deadparams-simplify again (`:3924`), `vw_condmerge`, `vw_ifconv`, `vw_vectorize`, `vw_split`, slot-use sweep (`:3939`), emission (`:3953`): about 20 | VIR |
| V3 | canonical form | `vir_canon` `vir/vir.c:583` | VIR | `vir_dom` (`:593`), constants sweep, renumbering sweep, `vir_dom` again (`:738`) | canonical VIR |
| V4 | verify (`VCC_VIR=v` only) | `vir_verify` `try.c:120` | VIR | 1, with its own `vir_dom` (`verify.c:1073`) | |
| V5 | lowering set-up | `vw_a64` `codegen/vir/a64.c:2347` | VIR | `vl_vcross` (a liveness of vector values, `:2281`), `vir_loops_build` (`vir_cfg` again, `loops.c:49`), i2p/p2i operand rewrite (`:2369`), `vl_notail` (twice: `:2395`, `:2442`), sibling-call candidates (`:2398`) | loops, marks |
| V6 | allocation | `vir_alloc` `vir/alloc.c:1946` | VIR | `vir_cfg` then `vir_dom` (`:1962`; `vir_dom` builds the CFG itself), own successor table (`:1965`), `webs`, `sweep5` (both again in the linear fallback), `webs_loops`, `save_point`, sweep 6 | registers, edits, moves |
| V7 | lowering | `vir_layout_chases`, `vir_uses` (`a64.c:2443`), `vir_low32`, `vl_body` (`:2494`) | VIR | 3 analyses + order + 1 emission sweep | arm64 records |
| V8 | rows 15-19 again | `finish_function` (`a64.c:2501`) | records | all of row 17, plus `fold` 3 sweeps and `flags` 4 (`opt.c:1891-1894`) | item |
| V9 | fallback | `emit_function` (`codegen/tu.c:201`) | AST again, rows 13-19 | whenever V1-V7 failed | item |

### 3.4 Facts computed more than once

| Fact | Where it is computed | Once and stored? Owner |
|---|---|---|
| Statement reachable / end reached | parser: `c->dead` counter from `cc_eval` at `if`, `&&`, `?:` (`frontend/c/stmt.c:1063-1076`, `frontend/c/exprop.c:931-940`, `:994-1007`); `fl_stmt` `frontend/c/flow.c:210`; `uv_stmt` carries it as `ust.live` (`frontend/c/flow.c:380`, `:895`); `stmt_flow` `frontend/c/func.c:341` and `end_of`/`rp_walk` `:483`, `:531`; emitters: `e->reach` with `NA_LABEL` (`arch/a64/codegen/stmt.c:1081`, `codegen/walk/stmt.c:930`); records: `em_rec_unreachable` `codegen/rec/unreachable.c:7`; VIR: `vw_rpo` `build.c:1861`, `vir_canon` drops blocks without an idom (`vir.c:597-616`) | The front-end ones: yes, one flow walk (Q-passes-04), owner `frontend/c/flow`.  The emitter's depends on its own folding and inlining, so it stays, as do the record and VIR ones |
| Has side effects / is pure | `has_side_effects` `frontend/c/conv.c:163` (any builtin is an effect, a volatile read is not); `side_effects` `arch/a64/codegen/builtin.c:14` (the opposite on both; used by `codegen/prepass.c:734`, 10 sites in `codegen/walk/`, 7 in `arch/wasm/codegen/`); `cplx_pure` `frontend/c/complex.c:41`; `array_copy_effects` `frontend/c/init.c:750`; `elementwise_abs_effects` `frontend/c/builtin.c:1598`; `em_bound_pure` `codegen/opt/cmpbounds.c:8`; `em_unused_pure` `codegen/opt/unusedinit.c:12`; `em_value_pure_decl` `codegen/opt/sibling-values.c:170`; `same_pure` `arch/a64/codegen/cond.c:17`; `uv_maycon` `frontend/c/flow.c:438`; `wk_branch_entry` (below) | Partly: two node bits set bottom-up when the parser makes the node, "may have an effect (clang's HasSideEffects)" and "may read volatile", answer `has_side_effects` and `side_effects` in O(1); owner `frontend/c/` (node flags).  The bounded "cheap and repeatable" predicates (`cplx_pure`, `em_bound_pure`, `em_unused_pure`, `same_pure`) are four whitelists of the same shape with different operator sets: one function with a mask argument |
| Constant truth of a condition | parser `cc_eval` (above; result dropped); `fl_const` `frontend/c/flow.c:24`, per fixpoint iteration, no pre-filter; `uv_const` `frontend/c/flow.c:467` with the pre-filter `uv_maycon` ("cc_eval is not free"); `cond_value` `frontend/c/func.c:191` (differs from `fl_const`: strips every cast, no volatile test); emitters `em_const_truth` `codegen/opt/control.c:101` then `cond_fold` `arch/a64/codegen/cond.c:184`, called from `gen_cond` `cond.c:233`, `wk_condfold` `codegen/walk/expr.c:3598`, `wexpr.c:1561`, at every level of `&&`/`||`/`!` recursion and in every attempt, round and inlined copy | The front-end value: yes, 2 bits on the condition node from the parser's `cc_eval`; owner `frontend/c/stmt.c`.  The emitter's: the node bits answer the common case first; what depends on inlined constants stays dynamic, but evaluated once per condition, not per level |
| Is a compile-time constant | `a64_constant_p` `arch/a64/codegen/builtin.c:847` (with a discarded code generation, `dry`); `wk_constant_p` `codegen/walk/expr.c:2837`, `wk_isconst` `:2292`; `wconstant_p` `arch/wasm/codegen/wcall.c:355`; `local_constant` `frontend/c/func.c:773`; `em_bound_constant` `cmpbounds.c:40`; `sv_constant` `codegen/opt/switch-values.c:24` | The `cc_eval` half: with the bit above.  The "after inlining" half is the emitter's by nature; one policy function for the three targets (walker reviews) |
| Contains a label / case | prepass `NA_LABEL` on the node (`codegen/prepass.c:703`); `wk_branch_entry` `codegen/walk/stmt.c:520`, called again at every level by `wk_stmt_entries` `:849` and at `walk/expr.c:1498`; linear scans `frontend/c/func.c:746-751` and `:832-837`; a switch's own labels: `has_default` `frontend/c/func.c:270` = `fl_has_default` `frontend/c/flow.c:168` (same body), `case_values` `func.c:233`, `fl_case_match` `flow.c:139`, per switch per iteration; `init_has_labeladdr` `prepass.c:423`; `tiny_ok` `arch/a64/codegen/call.c:418` (contains asm) | Yes: the parser knows when it makes a label, case, goto or asm; a per-function flag set (`has_label`, `has_goto`, `has_asm`, `has_vla`, `has_cleanup`) on the function's sym and a `default`/case summary on the `N_SWITCH` node; owner `frontend/c/stmt.c`.  `NA_LABEL` stays the per-node answer and `wk_branch_entry` reads it |
| Liveness | machine registers over arm64 records: `liveness` `arch/a64/codegen/opt.c:248` (own blocks, own fixpoint), again per block in `dead` `:964`; the shared `em_flow_liveness` `codegen/flow/maskflow.c:35` used by x86-64 (`arch/x64/codegen/dead.c:143`) and wasm (`arch/wasm/codegen/flow.c:224`) but not arm64.  VIR values: `sweep5`/`livein` `vir/alloc.c:424`, `:1248`; the checker's own `vir/allocchk.c` (meant to be independent); `vl_vcross` `codegen/vir/a64.c:2281` | Two implementations for records where one exists; arm64's also keeps per-record masks that `opt_regs`, `opt_noframe` and `flags` recompute (`opt.c:1040`, `:1080`, `:1805`).  One per function, owner `codegen/flow`.  `vl_vcross` asks the allocator's liveness one question before the allocator runs |
| Dominators, RPO, predecessors | pre-IR: `vw_rpo` `build.c:1861`, `vw_preds` `:1899`, `vw_doms` `:3075` (per `vw_simplify`, so 1-2); VIR: `rpo_order` `vir.c:431`, `vir_cfg` `:397`, `vir_dom` `:472`, both Cooper-Harvey-Kennedy.  Per function: `vir_dom` 3 times (`vir.c:593`, `:738`, `alloc.c:1962`), `vir_cfg` 5 times (3 inside `vir_dom`, `loops.c:49`, `alloc.c:1962`), plus a third successor table `alloc.c:1965-1981` | Yes: `vw_emit` emits in reverse postorder, so `vir_canon`'s second `vir_dom` and the allocator's recompute what the first one stored in `f->blk`; owner `vir/vir.c`, with a "CFG valid" stamp on `vir_fn` |
| Use counts | per local, weighted: prepass `luse` (`prepass.c:99`); per function: `fn_once` over all nodes (`prepass.c:242`) next to the parser's `SF_USED`/`SF_REF`; pre-IR `S.cnt` (`build.c:3192-3209`, per `vw_simplify`); VIR `vir_uses` (`vir.c:544`, called at `a64.c:2443` and `arch/x64/codegen/vir.c:1030`) after the allocator derived its own | `fn_once`: the parser can count references and calls when it makes `N_VAR`/`N_CALL` (owner `frontend/c/`).  `vir_uses`: computed once before allocation and shared (owner `vir/`) |

### 3.5 Cost

Counters that exist: `ppd_ph[]` (`include/driver/ppdriver.h:33`; `PPD_PH_CC` wraps `cc_run` and so includes
`PPD_PH_EMIT`, `codegen/tu.c:781-807`; `PPD_PH_OBJ` is layout and write only, encoding is in EMIT);
`vpp --batch --stats`; `VPP_TOKCOUNT` with `--vcc-tokhash` (tokens only); `VCC_EMIT_STATS`
(`codegen/tu.c:731`: wall time of setup, emit, `finish_function` labelled "printing", prepass);
`VCC_NOOPT` (clean-ups off); `VCC_EMIT_REPEAT`; `VPP_TIME`; `lextest core`.  vlink has named phases
with instruction counts (`ld/ld.c:154`); the compiler has nothing like it.

Instructions (M) per stage, by subtraction between modes:

| Stage | 9 kernel TUs, arm64 Linux `-O2` | SQLite amalgamation, arm64 Darwin `-O2` |
|---|---|---|
| lex (cold run minus warm, shared token cache) | 333 | not measured |
| pp, tokens only | 1 325 (53 % of the warm total) | 75 (8.5 %) |
| parse + sema (`-fsyntax-only` minus pp) | 832 (33 %) | 282 (32 %) |
| emit (`PPD_PH_EMIT`) | 324 (13 %) | 508 (58 %) |
| ... of which clean-ups (minus `VCC_NOOPT=1`; net, the unoptimized records cost more to encode) | 78 (3.1 % of the TU) | 209 (24 %) |
| ... emit at `-O0` | 158 | 230 |
| object layout + write | 4.5 | 3.9 |
| total, warm | 2 497 | 876 |
| emit with `VCC_VIR=b` (build VIR, then emit as before) | 594 | 1 521 |
| emit with `VCC_VIR=bl` | 681 (1 374 functions lowered, 836 fell back) | 3 335 (2 928 lowered, 12 fell back) |
| ... of which clean-ups after VIR (minus `VCC_NOOPT=1`) | 84 | 389 (10.5 % of the TU) |
| ... of which the vectorizer (minus `VCC_VIR_VEC=0`) | 0 | 14 |

Share of samples, % of TU:

| Pass | kernel, direct | SQLite, direct | SQLite, VIR |
|---|---|---|---|
| `lazy_capture` (pp pulled for skipped bodies) | 36.4 | | |
| `function_def` (parse of bodies + rows 4-8) | 10.9 | 28.8 | |
| ... `fl_stmt` (row 4) | 0.29 | 1.86 | |
| ... `uv_stmt` (row 5) | 0.63 | 4.95 | |
| prepass (row 12) | 1.37 | 5.9 | 0.9 |
| `em_select_locals` (row 13) | 1.53 | 0.36 | |
| `gen_stmt` (row 14; `inline_call` inside it 2.8 / 4.7) | 5.2 | 23.1 | |
| `finish_function` (rows 15-19) | 5.9 | 33.5 | 9.0 |
| ... `opt_function` (row 17) | 4.6 | 27.9 | 6.8 |
| `replay` (row 21) | 1.45 | 4.4 | 1.0 |
| VIR walk (V1) | | | 12.8 |
| `vw_emit` (V2 + V3): `vw_simplify` 10.2, `vw_promote` 6.8, `vir_canon` 5.2, `vw_phiops` 4.6, `vw_emit_ins` 3.5, `vw_trivial` 2.1, `vw_deadparams` 1.8, own 5.2 | | | 42.1 |
| `vir_alloc` (V6) | | | 17.9 |
| `vir_dom` in all (3 calls; `vir_cfg` 1.1 of it) | | | 2.0 |
| `vl_block` 2.9, `vir_low32` 1.9, `vir_uses` 0.8 (V7) | | | 5.6 |

x86-64: this Mac has no x86-64 glibc headers, so only 284 of the suite's own files could be
compiled (`test/vcc`, `test/gcc-torture`), a batch that static initializers dominate.  Inside it:
`wk_function` 7.2 % of samples (`wk_stmt` 48 % of that, `x64_finish` 28 %, prepass 7 %,
`wk_vla_scan` 2.5 %), `as_x64_insn` under `replay` 22.8 %.  `-S` emit 1 903 M against `-c` 4 238 M.

What the counters cannot separate, and the counter to add: (1) the passes inside `opt_function`
(all static, inlined; `VCC_NOOPT` is all or nothing): a per-pass instruction counter keyed by pass
name; (2) rows 4-8 from parsing (inside `PPD_PH_CC`): a `PPD_PH_FLOW` phase; (3) prepass and rows
15-19 in instructions (`VCC_EMIT_STATS` has wall time only); (4) how often row 14 retries: a count
per TU next to `E.nfn`; (5) VIR: build, pre-IR passes, canon, alloc, lowering, each by name.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-passes-01 | L4 | redundant walk | `codegen/vir/a64.c:2501` -> `arch/a64/codegen/func.c:494-498` -> `opt.c:1873-1894` | After VIR allocated registers, the whole clean-up sequence runs again with its own liveness, plus `fold` and `flags` that exist only for VIR: 389 M = 10.5 % of a VIR compile of SQLite | Give VIR's records their own short finish: no `copies`/`slots`/`exts` where the allocator's moves and spills make them empty; `fold` and `flags` into the lowering, where use counts exist | L | high (changes output) | per-pass counters first (how much each pass still removes on VIR records); then `vcc-ctests.py` with `VCC_VIR=bl`, `vcc-tsvc.py`, `bench/sqlite`, `bench/zlib`; not `vcc-same.py` |
| Q-passes-02 | L4 | pattern | `opt.c:1854-1906`, `build.c:3895-3987`, `frontend/c/func.c:931-979`; no table anywhere (0 hits for `VCC_VIR_PASSES`, which SPEC-vir.md 22.6 names) | The most expensive code (24 % of SQLite) can be neither timed, disabled nor verified pass by pass; every question about fusing is a guess | Decision 1 below | M | low | `vcc-same.py`; `vbench.py gate` |
| Q-passes-03 | L4 | redundant walk | `vir.c:593`, `:738`; `alloc.c:1962`; `loops.c:49`; `alloc.c:1965-1981`; `build.c:3075` | Dominators 3 times and the CFG 5 times per function with one answer; `vir_dom` is 2.0 % of a VIR compile, so about 1.3 % to save; the spec says "computed once" (22.5) | A valid stamp on `vir_fn`; `vir_canon` keeps the first result when the order is kept (the case `vw_emit` produces); `vir_alloc` and `vir_loops_build` reuse it and its successor table | S | low | `vcc-same.py` with `VCC_VIR=bl`; `vir-test.py`; `vir-alloc.py` |
| Q-passes-04 | L4, L1 | redundant walk, near-duplicate | `frontend/c/flow.c:210` and `:895`; `frontend/c/func.c:341`; `flow.c:168-195` = `func.c:270-296`; `flow.c:24` against `func.c:191` | Reachability walked by two analyses per function, a third walker for the note that can disagree with the one that decided (other cast and volatile rules, `enum_covered` only in one); 6.8 % of SQLite, 0.9 % of a kernel TU | One flow walk producing reachability, falls-off, noreturn and the uninitialized states (`uv_stmt` already tracks `live`); the path note reads its result; `fl_const` reads a stored truth | M | medium (clang-exact diagnostics) | `vcc-diag.py`, `vcc-verify.py`, `vcc-mutate.py`, `vcc-sema.py run`; outputs unchanged |
| Q-passes-05 | L4 | redundant walk | `codegen/opt/locals.c:26-49` over `lspan` (`prepass.c:411`); callers `arch/a64/codegen/func.c:776`, `arch/x64/codegen/locals.c:38` | The candidates are found by scanning every sym between the function's lowest and highest local; a lazily parsed function has its parameters early and its locals at the end of the table, so the range is most of the TU: 1.5 % of a kernel TU, more than the prepass (inferred from the profile and the code) | The prepass appends each local it notes to a per-function list; selection reads the list | S | low | `vcc-same.py`; `vbench.py gate` (emit) |
| Q-passes-06 | L4 | redundant walk | `codegen/vir/try.c:91-147`, `codegen/tu.c:198-202` | A function VIR cannot build is walked by the VIR walker until it fails, then by the direct emitter: 834 of 2 210 kernel functions (578 for inline asm, 222 for a record argument or result) | The prepass already visits `N_ASM` and the signature is known: a "VIR cannot build" bit skips the try | S | low | `vcc-same.py` with `VCC_VIR=bl`; `VCC_VIR=bls` lists unchanged |
| Q-passes-07 | L4 | redundant walk | `arch/a64/codegen/cond.c:229-250`, `codegen/walk/expr.c:3598-3640`, `codegen/opt/control.c:45-107`, `builtin.c:847-882` | A chain of n `&&` is evaluated for constant truth at each of its n levels, each time re-running `__builtin_constant_p` operands (`cc_eval`, `side_effects`, a discarded code generation); `cond_fold` + `em_control_int` 0.4-0.9 % | Truth bits on the node from the parser; the emitter evaluates a condition once and hands the result down | M | medium | `vcc-same.py`; kernel boot (the kernel leans on these folds) |
| Q-passes-08 | L4 | redundant walk | `arch/x64/codegen/obj.c:71-79`, `:81-169` | x86-64 `-c` formats a mnemonic (`snprintf` for `cmov`, `set`, `j`) per record and the assembler looks it up by name; arm64 encodes records directly.  In the test batch the assembler takes 3 times the samples of code generation (not a representative corpus) | An encoder entry per record op, as `arch/a64/codegen/obj.c` `enc` | M | medium | `vcc-x86.py`; objects byte-identical (`vcc-same.py` on an x86 corpus) |
| Q-passes-09 | L4 | redundant walk | `codegen/prepass.c:24-61`, called per TU at `codegen/tu.c:777` | Every arm64 TU scans all syms and all nodes for SVE types that the kernel never has (inlined into `emit_tu`, whose own share is 0.6 %; inferred) | The type table knows: a flag when the first `TY_SVE` use is made | S | low | `vcc-same.py`; `test/diag` |
| Q-passes-10 | L4 | redundant walk | `codegen/walk/stmt.c:438`, `:1639`; `:520`, `:849-880` | The shared walker walks the statement tree once for VLAs and cleanups before emitting, and asks "contains a label" by recursion at every level although `nflags` holds `NA_LABEL`; 0.2-0.35 % | Prepass flags on the function; `wk_branch_entry` reads `NA_LABEL` (its extra VLA-cast rule becomes a second bit) | S | low | `vcc-same.py`, `vcc-x86.py` |
| Q-passes-11 | L4 | redundant walk | `codegen/tu.c:400-436`, `:794-801`; `prepass.c:396-420` | A second round copies unchanged functions but repeats the prepass for all of them, since its tables are per round (net/core/filter.o: 961 prepass walks again, 6.5 % of that TU's emit time); with VIR, `fn_isize` also walks every defined function, used or not (`prepass.c:324-329`) | Prepass tables live for the TU, not the round | S | low | `vcc-same.py` |
| Q-passes-12 | L7, L5 | misplaced, misnamed | `side_effects` in `arch/a64/codegen/builtin.c:14`; `cond_fold` in `arch/a64/codegen/cond.c:184` called by `codegen/walk/expr.c:3611`; `has_side_effects` `frontend/c/conv.c:163` | The shared walker and wasm depend on two arm64 files for target-independent analyses; two predicates with nearly one name answer differently on builtins and volatile reads | `codegen/opt/effects.c`, `codegen/opt/condfold.c`; rename the front end's `evaluated_for_effects` | S | low | builds (`VCC_UNITY=`), `vcc-same.py` |
| Q-passes-13 | L4 | mixed file | `opt.c` (9 passes and 2 queries), `build.c` (15 passes), `frontend/c/func.c` (5 jobs), `codegen/prepass.c` (per-function facts, 2 TU gates, call graph, Tarjan, inlining sizes) | No pass can be read, timed or replaced alone | One pass per file after the table exists (moves only) | M | low | `vcc-same.py` |
| Q-passes-14 | L4 | stale doc | SPEC-vir.md 22.6 (7 sweeps; simplify+gvn one pass without a dominator-tree walk), 22.5 ("computed once"), "0 mallocs per function" against `build.c:3078-3083`, `:3177-3221`; `vir/loops.c:2-4` | The spec describes a pipeline the code is not; nobody can tell which is intended | P2 decides the VIR pipeline; then the spec or the code moves | S | none | review |
| Q-passes-15 | L5 | misnamed | `include/driver/ppdriver.h:33`, `vpp.c:14-16`, `codegen/tu.c:739` | `PPD_PH_CC` contains EMIT; `VCC_EMIT_STATS` calls `finish_function` "printing" although it is clean-ups, frame and item output | Exclusive phases; real names | S | low | `vbench.py` reads the same columns |
| Q-passes-16 | L4 | redundant walk | `frontend/c/func.c:746-767`, `:832-854` | Two linear scans with the same body (labels, then assigned variables) for two folds | With the per-function flags of 3.4, both become one scan or none | S | low | `vcc-same.py`, `vcc-ctests.py` |

Q-passes-01.  The clean-ups were written for the direct emitter, which loads a register variable
into a fresh temporary and spills around calls (`opt.c:16-17`).  VIR's records have neither habit,
yet `finish_function` cannot tell: `e->vlow` only adds passes.  Whether `slots` or `exts` still
remove anything on VIR records is exactly what the per-pass counters must show before anything is
cut; the estimate is an upper bound.

Q-passes-04.  `uninit_check` also runs when its two warnings are off, as soon as any header in
the TU has a diagnostic pragma (`flow.c:1331-1333`): SQLite compiled with `-w` still spends 5 % in
`uv_stmt`.  A per-function test at the body's location would keep clang's behaviour.

Q-passes-05.  Checked against SQLite, where functions are defined in the main file and the span
is tight: 0.36 %.

## 5. Names

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `walk` | `codegen/prepass.c:448` | the prepass's fact collector | `facts_node` |
| `walk_fn`, `ensure_walked`, `E->walked` | `arch/a64/codegen/call.c:342`, `prepass.c:396` | "the prepass has run for this function" (and log it) | `facts_need`, `facts_ready` |
| `replay` | `codegen/tu.c:287`; also `frontend/c/replay.c`, `a64_replay`, `x64_replay` | encode the chunks into the object | `encode_unit`; "replay" stays the declaration cache's word |
| `reuse`, `keep`, `E->K`, `E->mf` | `codegen/tu.c:157`, `:342` | last round's functions; the "must fold" marks that trigger a round | `prev_round`, `E->refold` |
| `dry` | `arch/a64/codegen/builtin.c:80` | generate and discard to learn if a value is constant | `trial_gen` |
| `opt_function`, `opt_regs`, `opt_noframe` | `opt.c` | record clean-ups; used-register query; "no frame needed before record n" | `rec_cleanup`, `rec_used_regs`, `rec_frameless_prefix` |
| `copies`, `slots`, `exts`, `dead`, `fold`, `flags`, `pure`, `W` | `opt.c` | passes and their shared liveness (`W` names three structs, PLAN-quality) | `pass_copies`, ..., `rec_live` |
| `fl_`, `uv_`, `flow`, `flw`, `stmt_flow`, `fl_stmt` | `frontend/c/flow.c`, `frontend/c/func.c` | two reachability walkers with two state structs one letter apart | one `reach_` |
| `vw_emit` | `build.c:3895` | runs the pre-IR pipeline, then emits | `vw_finish_ir` and `vw_emit` |
| "sweep 5", "sweep 6" | `vir/alloc.c` | numbers from SPEC 22.6, which the code no longer follows | names: `alloc_liveness`, `alloc_assign` |

"Pass" has no single meaning: lexer passes 1 and 2, prepass, "sweeps" in VIR, "rounds" in
`emit_tu`, "phases" in the driver and linker.  STYLE.md should fix: pass (one traversal with a
name), analysis (a pass that only computes), round (a repeat of a stage), phase (a timed stage).

## 6. Decisions needed

1. **Pass table (PLAN-quality decision 4).**  Options: (a) keep the hand-called sequences and add
   counters around each call; (b) a static table per site, `{ name, function, condition, needs }`,
   run by one small loop that can skip a pass by name (`VCC_PASS_OFF=slots,exts`), count per pass
   under a switch, and call a verifier between passes; (c) a pass manager with declared analyses
   and invalidation.  What each site needs: **`opt.c`**: passes are already `void f(ecx *)` over
   shared state; the table needs a condition (`vlow`), the rule "liveness stays an
   over-approximation" written as a contract, and a record verifier that does not exist.
   **`build.c`**: passes return "changed" (then `vw_trivial` reruns) and one group repeats; the
   table needs that result, a statement of which passes keep order and predecessors valid, and a
   pre-IR check (the verifier only reads finished VIR).  **Front end**: four gated calls, no shared
   shape.  Recommendation: (b) for `opt.c` and `build.c`, with `vperf` read only when the switch
   is on (a read costs about 1 600 instructions, `include/port/vperf.h:4`); for the front end no table but
   the single flow walk of Q-passes-04.  (c) is not worth it for two sites.
2. **Where facts live.**  Node bits set by the parser (effect, volatile read, constant truth) and
   per-function flags (label, goto, asm, VLA, cleanup, SVE), against a side table from the
   prepass.  The parser already computes each of them and drops it.  Recommendation: parser, for
   what does not depend on code generation; needs free bits in `node.flags` (cc reviewers).
3. **Clean-ups after VIR (Q-passes-01).**  Keep them as a safety net, or give VIR its own finish.
   Needs the per-pass counters and a code-quality comparison; decide after measurement.
4. **Is SPEC-vir.md 22.6 still the target?**  If yes, `vw_emit`'s separate passes are debt to fuse
   (simplify+gvn without a dominator walk, no `vw_doms`); if no, the spec's budget table goes.
5. **Phase counters as a product feature.**  `--stats` today is `vpp --batch` only.  A
   `-ftime-report`-like switch on exclusive, named phases would serve this plan and users.

## 7. Not covered

- Read in excerpts only: `frontend/lex/lex.c` and `frontend/pp/` beyond `pp_next`'s entry; `ppd_run1`'s option
  parsing; `frontend/c/flow.c:480-1320` (`uv_ex`, `uv_stmt`); the bodies of `vw_promote`, `vw_condmerge`,
  `vw_ifconv`, `vw_split`, `vec.inc`; `vir/alloc.c` before line 1930; `x64_print`; wasm entirely.
  Sweep counts inside those are not given or are marked "about".
- Not measured: the passes inside `opt_function` and `vw_simplify` separately; how often the
  arm64 emitter retries; lexing per TU without the shared cache; x86-64 on real code; multi-thread
  emission (`VCC_THREADS`).  Whether `condmerge`/`ifconv`/`split` leave `V->order` and the
  predecessor lists valid was not verified.
- Sample shares are cycles on a loaded machine and statics are folded into callers; the
  instruction counts by subtraction are the firm numbers.
- Observed while measuring, for the scheduling and driver reviews: `vpp --batch` workers run on
  the default 512 KB thread stack (`bin/vpp/vpp.c:91`) while emit workers get the main thread's
  size (`codegen/tu.c:495-518`); `test/vcc/flat_declarators_labels.c` ends such a batch with SIGBUS.
