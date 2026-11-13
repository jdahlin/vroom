# Quality review: VIR builder, vectorizer, both lowerings (2026-11-13, main at 2e6d3e73)

## 1. Summary

The area works and is commented where the algorithms are hard (Braun, the far
loads, the vectorizer's legality rules).  Its structure is what one long
branch leaves behind: `emit/vir/build.c` is fifteen jobs in one file, the
optimizer runs on a private form that has no header, printer or verifier of
its own, and the two lowerings were written apart.  The three things that
matter most:

1. **The optimizer's form is undeclared.**  Every pass of `-O2` runs on the
   builder's pre-IR, not on VIR; SPEC-vir.md 14 and PLAN-vir.md "Files"
   describe passes over VIR in `vir/` that do not exist.  No pass can be run
   or tested alone.  This is the decision everything else waits on (6.1).
2. **Analyses are recomputed, not kept.**  One function costs about 20 sweeps
   over its instructions in the builder (about 40 with the second promotion
   round), then 3 dominator and 4 predecessor computations on VIR over a CFG
   that no longer changes, then arm64's record clean-ups with a liveness of
   their own (section 3).
3. **Two lowerings, one skeleton written twice**, and the arm64 one (2505
   lines of arm64 code) sits in the generic directory.  They share placement
   (`vir/layout.c`) and three proofs (`vir/facts.c`); the block driver, the
   allocator's edits, edge moves, the switch rule and all fold legality are
   per target.

REVIEW-vir-lowering.md's findings are not repeated; 3.4 says which are open.
A correction to the brief: `vw_emit` is at `build.c:3895` (not 1895);
`vw_rpo` is at 1861, the dominator tree at 3074 as stated.

## 2. Inventory

| File | Lines | What it actually does | Better name | Header |
|---|---|---|---|---|
| `emit/vir/build.c` | 3987 | (a) the pre-IR and its construction 72-184; (b) Braun variables 186-362; (c) the walker's `LO()` interface: conversions, names, slots and store table, integer, FP, builtins, atomics, varargs, locals, calls 364-1735; (d) begin/finish 1737-1803; (e) CFG: successors, threading, RPO, predecessors 1805-1932; (f) trivial and dead parameters 1934-2028; (g) slot promotion 2030-2385; (h) rewrite rules 2387-2752; (i) loads across blocks, with a loop finder 2754-2927; (j) value numbering 2929-3072; (k) dominators 3074-3125; (l) address un-sharing 3127-3168; (m) the simplify driver and DCE 3170-3273; (n) if-conversion 3275-3519; (o) branch joining 3521-3722; (p) edge splitting 3724-3790; (q) emission into VIR 3792-3891; (r) the pipeline `vw_emit` 3895-3987 | split, section 3.5 | good, but stale in two places (Q-16) |
| `emit/vir/vec.inc` | 1569 | loop vectorizer on the pre-IR (analysis 37-492, construction 494-1399) and loop-constant hoisting `vv_pin` 1401-1476, a different pass | `vectorize.c`, `loopconst.c` | good |
| `emit/vir/a64.c` | 2505 | allocated VIR to arm64 records: AAPCS64/Apple argument assignment and allocator constraints 137-312, moves and edits 314-615, compares and ccmp chains 617-824, memory 826-1013, tail-call legality 1015-1135, calls and switch 1137-1343, address folds 1345-1510, NEON 1588-1775, `vl_ins` 1777-2115, block/layout/body 2117-2277, `vw_a64` 2346-2505 | `arch/a64/emit/vir*.c` | good |
| `arch/x64/emit/vir.c` | 1098 | the same for x86-64 (SysV one-piece scalars, SSE2 as text) | fine; split as a64's | label only; formatted unlike the rest of the tree (no spaces around operators from line 58 on) |
| `emit/vir/try.c` | 147 | `VCC_VIR` parsing, the `VCC_VIR_LIST` filter, and the driver of one function: build, verify, print, lower | `function.c` or `driver.c` | good |
| `emit/vir/walk.c` | 17 | instantiates the shared walker a second time (`vw_wk_*`) | `walker-instance.c` | good |
| `emit/vir/vw.h` | 80 | the builder's state `vwst`, with x86-64's lowering buffers in it (71-75) | `preir.h` | label only |

What the module is for (draft opening comment).  At `-O2` a function is
walked by the shared AST walker compiled against a virtual target (`vw_`).
Each `LO()` call appends to a **pre-IR**: VIR's instruction words (`vwp`
embeds `vir_ins`) in per-block linked lists, with a union-find table `fwd`
that replaces a value by another without touching its users.  Invariants a
newcomer must know: value 0 is "none"; a constant has `blk == VW_NOBLK` and
is in no list; a removed instruction stays in its list and is skipped by
`vw_find(V, q) != q`; a store of a promoted slot stays too (`vw_promdead`);
a block parameter's operand `k` belongs to predecessor `V->pr[pred + k]`, so
whoever edits a predecessor list edits every parameter's operand list in the
same way; `V->order` is the only list of live blocks and is compacted by the
pass that clears `reach`; from `V->sealing` on every new value needs a `fwd`
entry (`vw_put`, 87-90); anything the builder cannot do `longjmp`s out
(`vw_fail`) and the direct emitter compiles the function.  After the passes
the function is copied into VIR (`vw_emit_ins`), which is append-only and
dense; `vir_canon`, the allocator and a per-target lowering follow.

## 3. Passes and data flow (L4)

### 3.1 `vw_emit` in execution order (`build.c`)

Sweeps: V = over all pre-IR values, I = over the instructions of all live
blocks, P = over their parameters, B = over blocks only.

| # | Pass | Call / definition | Sweeps | Reads | Changes | Needs |
|---|---|---|---|---|---|---|
| 1 | thread | 3902 / 1839 | B | terminators, emptiness (`head`, `phead`, `def`) | successor operands | nothing |
| 2 | rpo | 3903 / 1861 | B (DFS) | successors | `V->order`, `reach` | 1 |
| 3 | preds | 3904 / 1899 | 3 B | successors | `V->pr`, `pred`, `npred` | order |
| 4 | seal | 3906-3916 / `vw_phiops` 309 | V + P | placeholders, the (block, variable) hash | operands, new parameters, conversions appended to predecessors | preds, order |
| 5 | promote | 3917 / 2126 | 3-4 V + 2 I | uses of slot addresses, loads, stores, memsets | loads forwarded or turned into `ext`, stores marked dead, new variables and parameters | preds, order, `fwd` |
| 6 | trivial | 3918 / 1935 | P per round, to a fixed point | parameter operands | `fwd` | preds |
| 7 | deadparams | 3920 / 1980 | V + I + P | all operands | `fwd`; uses `V->map` as mark bits | order |
| 8 | simplify | 3921 / 3170 | 6 I + dominators | everything | rewrites in place, `fwd`, constants, inserted `ptradd` copies, DCE | order, preds; computes and frees use counts, dominators, RPO index (twice), positions, writers, loop ranges |
| 9 | promote again | 3924 | 3 V at least | as 5 | as 5 | 8 (it exposes `i2p(p2i $slot)`) |
| 10 | 6, 7, 8 again | 3925-3928 | P + 3 + 6 I + dominators | | | only if 9 promoted |
| 11 | condmerge | 3930 / 3563 | I + B per round | use counts (its own), `brif` pairs | moves compares, edits `pr` and operand lists, clears `reach`, compacts order; then 6 | preds, order |
| 12 | ifconv | 3933 / 3419 | B (+ `vw_armreuse`, up to 64 x 16 per arm) | diamonds and triangles | hoists arm code, adds `select`, edits `pr`/operands/order; then 6 | preds, order |
| 13 | vectorize, `vv_pin` | 3936 / `vec.inc:1490` | B + per innermost loop | loops of straight blocks | 5 new blocks per loop, `pr`, order; pinned constants | preds, order; before 14 |
| 14 | split | 3937 / 3725 | B | edges into blocks with parameters | edge blocks, `pr`, order | last CFG change |
| 15 | dead-slot scan | 3939-3944 | I | loads' slot tags | `sl.used` | after every pass |
| 16 | emission | 3946-3983 / 3818 | 2 I | all live instructions | the VIR function | order = final block ids |
| 17 | `vir_canon` | 3986 | (in `vir/`) | | renumbered copy | |

Counted by reading: about 20 sweeps over every instruction or value in the
cheapest case (one `vw_trivial` round, nothing promoted in step 9), about 40
when step 10 runs, plus about ten loops over blocks.  The survey's "15-20"
holds as a lower bound.

### 3.2 What is recomputed

| Fact | Computed at | Times per function |
|---|---|---|
| block -> RPO index | `vw_doms` 3086-3089 (freed 3124), `vw_far_init` 2850-2853, again in step 10, and `fin` 3950 | 3-5; it is the inverse of `V->order` and has no field |
| dominators | `vw_doms` 3075 per `vw_simplify`; the CFG does not change between 3921 and 3928.  Then on VIR: `vir_dom` twice in `vir_canon` (`vir/vir.c:593`, `:738`), once in `vir_alloc` (`vir/alloc.c:1962`) | 3-5; x86-64 adds one per second allocation (`arch/x64/emit/vir.c:1042`) |
| predecessors | `vw_preds` 1899; `vir_cfg` inside each `vir_dom`, in `vir_loops_build` (`vir/loops.c:49`) and in `vir_alloc` | 5 |
| use counts | `vw_simplify` 3192-3209 (twice with step 10), `vw_condmerge` 3571-3582, the vectorizer's `nuse`, then `vir_uses` in each lowering (`a64.c:2443`, x64 `vir.c:1030`) | 3-5 |
| loops | `vw_far_init` 2873-2897 (ranges of retreating edges, partial overlaps merged); `vir_loops_build` (`vir/loops.c:39-103`) is the same algorithm on VIR; the vectorizer finds its loops a third way (`vec.inc:1502-1528`) | 3 forms |
| tail-call legality | `vl_notail` (a fixed point over all instructions) at `a64.c:2395` and again at `a64.c:2442`, same inputs | 2 |

After emission the CFG is final: only `vir_canon`'s first `vir_dom` is
needed, the other three predecessor and two dominator computations repeat it.

### 3.3 Walks that could be one

- 1-3: threading and predecessor counting belong in the RPO depth-first walk.
- 6-7: with a trivial parameter removed when its operands are known (REVIEW
  section 3, still open) the fixed point at four call sites (3918, 3925, 3931,
  3934) and `vw_deadparams` become one pass.
- In 8: the constant count (3182), the use count (3192) and the position and
  writer sweep (2854) read the same lists in the same order before the
  dominator walk: one sweep.  `vw_ownaddr` (3139) and the dead scan (3247)
  after it: one sweep, which can also set `sl.used` (step 15).
- 5: the escape scan (2149) and the field scan (2170) are one loop; the
  second call (3924) repeats both in full when simplification freed nothing.
- 11: reuse 8's use counts instead of counting again.

### 3.4 After the builder

`vw_try` (`try.c:91`): build; optional verify and print; then `vw_a64` or
`vw_x64`.  arm64 (`a64.c:2347`): loops, an operand rewrite of the VIR (i2p/p2i
bypassed, 2369-2378), `vl_vcross` (vector liveness, only with vectors and
calls), `vl_notail`, sibling candidates, `vir_alloc`, `vir_layout_chases`,
`vl_notail` again, `vir_uses`, `vir_low32`, one emitting pass (`vl_body`),
then `finish_function`.  x86-64 (`vir.c:988`): three scans of support
(1003-1022), `vir_uses`, loops, `vir_alloc` (twice when r10 is needed as
scratch), layout, one emitting pass, `x64_finish`.

Inlining.  There is no IR-level inliner.  `wk_can_inline`
(`emit/walk/stmt.c:1302`) decides per call site; `wk_inline` (`:1385`) parks
the arguments, gives the copy a new instance number (`W->inst`, which keys
the builder's locals, `build.c:1425`) and calls `wk_stmt` on the callee's AST
body.  So every copy repeats the walker's folding and all builder work, and
nothing built for a callee is reused.  Cost model (`emit/prepass.c:309`
`fn_isize`, before the workers start): `isz[s]` = AST nodes of `s` plus `isz`
of every callee it would inline, bottom-up over Tarjan's components; a call
is inlined when `isz <= WK_INLINE_VIR` (150), 4 x that with a constant
argument, 4000 for a static function named once (`fn_once`, `:242`); never
inside its own recursion group, never a caller of `setjmp`, depth at most 16.
Measured in PLAN-vir.md:1763: limit 40 to 150 took sqlite's compile from 0.37
to 0.52 s.  A function that then fails in the builder, in the allocator or in
the lowering is walked again by the direct emitter, copies included.

REVIEW-vir-lowering.md, status of its open points (checked in the code):
**fixed** since: the float-argument bail (`build.c:738-741`), per-function
clears (`vw_hgen` 193, `labhi` 1757), `vw_promote`'s field lists (2131), the
low-half analysis moved to `vir/facts.c:8` (but it still takes `ret32` and
reads the signature's size word, so 2.5 and 5.1 stand).  **Still open**: 2.1
accessors for overloaded record fields; 2.2 (`fplain` is still a whitelist,
`opt.c:1133`); 2.3 (`vl_mulk` `a64.c:109` mirrors `fmulk` by hand); 2.4;
2.6 (dumps, list encodings); 2.8's latent items (a retyped load keeps its CSE
key 2581-2591, scratch arrays leak on `vw_fail`, `vw_emit_ins` reads past a
`br` list 3851-3855, the 24-bit hash 233/244); section 3: parameters at every
join, `vw_trivial` to a fixed point, Cooper-Harvey-Kennedy on ladders
(`vw_doms` 3091), `vir_canon`'s two dominator runs; 4: no constant folding of
binary operations and no `x - x` in `vw_simp1`, no block merging after 12;
5.2 (`build.c:17-19` still says an i32's upper half is zero); 5.3-5.4.  Not
rechecked: 2.7, `LO(begin)` before the `setjmp`, the constant table, 5.5.

### 3.5 Proposed split (moves only)

`emit/vir/`: `preir.h` (types of `vw.h`, `vw_find`, the live test, the
operand and successor iterators, GROW), `ssa.c` (b), `lo-value.c`,
`lo-mem.c`, `lo-call.c` (c, d), `cfg.c` (e, k, p and the loop ranges),
`params.c` (f), `promote.c` (g), `rules.c` (h), `gvn.c` (i, j, l, m),
`ifconv.c` (n), `condmerge.c` (o), `vectorize.c`, `loopconst.c`,
`tovir.c` (q), `pipeline.c` (r, with the pass list as a table), `lower.c`
(what both lowerings share, Q-02).  `arch/a64/emit/vir-abi.c`, `vir-move.c`,
`vir-select.c`, `vir-mem.c`, `vir-call.c`, `vir-neon.c`, `vir-func.c`; the
same cut for x86-64.  All are in the `emit` unity TU already, so `static`
becomes `VPRIV` and nothing else changes.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-VIRB-01 | L4 | mixed file | `build.c` sections (a)-(r) of the inventory; `#include "vec.inc"` at 3893 | 4000 lines to scroll for any pass; 13 passes share file-scope helpers with no stated contract | the split of 3.5 | M | low (moves) | `vcc-same.py --base` with `VCC_VIR=bl`, unity and `VCC_UNITY=` builds |
| Q-VIRB-02 | L1 | near-duplicate | block driver `a64.c:2120-2171` / x64 `vir.c:956-975`; edits `a64.c:460-484` / `vir.c:202-214`; edge moves `a64.c:487-558` / `vir.c:215-240`; setup `a64.c:2424-2443, 2481-2493` / `vir.c:1056-1093`; allocation check `a64.c:2412-2423` / `vir.c:1048-1054`; dense-switch rule `a64.c:1258-1273` / `vir.c:78-97`; `vl_twice` `a64.c:1021-1032` = `vx_twice` `vir.c:978-986` = `emit/prepass.c:547-549`; `vl_grow` `a64.c:84-93` = `vx_grow` `vir.c:29-36` | a third target copies 400 lines; the copies already differ (below) | `emit/vir/lower.c`: one driver, a table of target callbacks; `vir_switch_dense()` and a case accessor in `vir/` | M | medium | `vcc-same.py` with `VCC_VIR=bl`, `vcc-x86.py`, `vcc-ctests.py`, `vir-test.py` |
| Q-VIRB-03 | L7 | misplaced | all of `emit/vir/a64.c`: AAPCS64 `141-173`, register sets `177-180`, 72 hand-encoded instruction words (e.g. `399`, `543`, `1615`, `1986`), frame set-up `2446-2470`; x64's lowering is in `arch/x64/emit/` | breaks AGENTS.md's layout rule; nobody looks for arm64 code in `emit/vir/` | `arch/a64/emit/vir*.c` | S | low (move) | `vcc-same.py` |
| Q-VIRB-04 | L4 | redundant walk | table 3.2 | 3-5 dominator runs, 5 predecessor builds, 3-5 use counts per function; `vw_simplify` + `vw_promote` lead the builder's profile (REVIEW 3) | analyses as fields of the pre-IR with two validity bits (CFG changed, values changed); on VIR: canon leaves `cfg` and `dom` valid and `vir_loops_build`/`vir_alloc` take them | M | low | `vcc-same.py`; `vbench.py gate` must go down |
| Q-VIRB-05 | L4 | redundant walk | `a64.c:2501` calls `finish_function`; `arch/a64/emit/func.c:495-498`: `a64_unreachable`, `drop_fallthrough`, `opt_function` (liveness, `copies` twice, `slots`, `exts`, `dead`, `opt.c:1873-1893`); five forks on `e->vlow` (`func.c:513, 548`, `opt.c:1036, 1867, 1891`); `word_regs` (`opt.c:1000`) decodes the raw words the lowering wrote | a second liveness and five record passes after an allocator that already placed every value; "the same finish_function" is in fact a forked one | measure first (6.3); then a short VIR pipeline: `fold`, `flags`, `dead` | M | medium | `vcc-same.py` cannot (output changes); sqlite hash, ctests, `zlib/run.py`, cycles |
| Q-VIRB-06 | L4 | redundant walk | 3.3; `while (vw_trivial(e))` at `build.c:3918, 3925, 3931, 3934` | sweeps that read the same lists back to back | fuse as listed in 3.3 | M | medium | `vcc-same.py`, `vbench.py gate` |
| Q-VIRB-07 | L1 | pattern | the live test written inline: about 20 `vw_find(V, q) != q` tests, 14 `vw_promdead`, 7 `VW_LIFETIME` in `build.c`, with different members at `3339`, `3396` (every memset), `3578` (no lifetime test), `3634`, `3966`; the one helper is `vv_skip` (`vec.inc:170`).  Operand walk: `VW_EACHOP` `2433` and hand copies at `2002-2008`, `2155-2167`, `3297`, `3826-3828`, `vec.inc:186-194`, `512-514` | a new instruction kind or flag must be added in each copy; the variants are where a pass and its neighbour disagree | `preir.h`: `vw_live()`, one operand iterator | S | low | `vcc-same.py` |
| Q-VIRB-08 | L1 | duplicate | predecessor removal `build.c:3495-3503` = `3698-3706` (197 tokens); order compaction `3510-3517` = `3713-3720`; unlinking from a block list `3666-3672` = `3677-3683`; icmp inverse and swap tables: `build.c:3586`, `vec.inc:955`, `vec.inc:1093` (both), `a64.c:622`; icmp codes as bare 0-9: `build.c:934-944`, `3539`, `vec.inc:1107-1115`, `a64.c:1939-1941`, x64 `vir.c:759, 862`; switch-case decoding `x ? cw[..+1] << 32 \| cw[..] : cw[..]` eight times in `a64.c:1262-1328` and eight in x64 `vir.c:85-90, 348-355` | CFG edits by hand in each pass (REVIEW 2.4 traced one); a code table to keep in step in five places | `cfg.c`: `vw_pred_remove`, `vw_order_compact`, `vw_unlink`; `vir.h`: an enum of the codes, `vir_cc_inv[]`, `vir_cc_swap[]`, `vir_switch_case()` | S | low | `vcc-same.py` |
| Q-VIRB-09 | L5, L6 | misnamed | section 5 | the prefixes do not say which layer a function is in | section 5's scheme | M | low (scripted) | builds; `vcc-same.py` |
| Q-VIRB-10 | L8 | global state | 8 knobs; three ways to cache one: `_Thread_local` (`try.c:23`, `vec.inc:1480`, `build.c:3614`), plain `static` written by emit threads (`a64.c:452, 2206, 2383`), `getenv` per function (`a64.c:2412`, x64 `vir.c:1048, 1081`); `VCC_VIR_ROT` read in two files; `vw_listed` takes a process-wide mutex per function even with no list (`try.c:52-77`); the vectorizer runs only with the letter `l` (`vec.inc:1497`), so `VCC_VIR=bd` prints another function than `bl` lowers | a lock and a `getenv` in the per-function path; options nobody can list | one `vir_opts` read once in `emit/tu.c` next to `VCC_NOOPT` (`tu.c:439-448`) | S | low | `vcc-same.py`; `vccd-cc.py --bin-dir build/ccs-tsan` |
| Q-VIRB-11 | L4 | redundant walk | refusals tested after the build: `-g` (`a64.c:2357`, x64 `vir.c:1000`), stack protector, no-SIMD and interrupt functions (x64 `vir.c:1000-1002`), `VCC_VIR_LIST` (`try.c:132`) | with `-g -O2` every function is walked and built, discarded, and walked again | test what does not need the VIR in `vw_try` before `vw_wk_function` (`try.c:96`) | S | low | `vcc-same.py`; instructions with `-g` |
| Q-VIRB-12 | L2 | pattern | growth: `GROW` `build.c:56`, `vl_grow`, `vx_grow`; four open-addressing tables with the same multiplier written six times (`build.c:233, 244, 1421, 2713-2717, 2735`; tables at `202`, `1444-1475`, `2711`, `2728`); about 35 `malloc`/`free` pairs per function, 22 of them in `vw_simplify` (`3177-3222`, `2836-2846`, `3078-3083`), which leak when `vw_fail` jumps out | against the "minimal allocation" goal; four hash tables to get right | a per-function arena (`lib/arena.h`) reset in `vw_begin`; `lib/vec.h`; one table helper | M | low | `vcc-same.py`; ASan suite; `vbench.py gate` |
| Q-VIRB-13 | L8 | global state | arm64's scratch is a file-scope thread-local never freed (`VLT`, `a64.c:76`); x86-64's is in the builder's state (`vw.h:71-75`), freed by `vw_ecx_free` (`build.c:1800`), which misses `xlayout` | two homes for one thing, and the free list has already drifted | the lowering's buffers in one struct owned by `lower.c`, or the arena of Q-12 | S | low | ASan with leak check on Linux |
| Q-VIRB-14 | L4 | long function | `vx_ins` 456 lines (x64 `vir.c:498`): 130 lines of fold matching (508-637), an ALU table, then one switch (661-952) with 21 `snprintf`'d SSE texts; `vl_ins` 339 lines (`a64.c:1777`): one switch of about 50 cases, entered after `vl_vec` (`1595-1773`), a second switch tried first for every instruction; `vv_loop` 407 lines (`vec.inc:974`): analysis to 1123, construction after | `vv_loop` can fail after it started building (`return false` at 1143, 1287, 1298, 1346, 1358, after `vv_blk` at 1127-1133), leaving orphan blocks and instructions; the lowerings' pending folds are 21 and 24 loose fields of state (`a64.c:47-63`, x64 `vir.c:23-26`) | `vv_analyze` / `vv_build`; per lowering: fold matcher, scalar switch, SIMD; dispatch on `vir_isvec` once | M | medium | `vcc-same.py`, `vcc-tsvc.py` |
| Q-VIRB-15 | L4 | misplaced | `vv_pin` (`vec.inc:1401-1476`) hoists loop constants; it runs for loops that were not vectorized (1552) and is switched off with the vectorizer (`VCC_VIR_VEC=0`, `-mgeneral-regs-only`, 1497) though it also hoists integer bounds and symbol addresses | a pass nobody finds; a switch that turns off two things | `loopconst.c`, its own pass and switch | S | medium (output changes where the gates differ) | TSVC, sqlite cycles |
| Q-VIRB-16 | L7 | misplaced | target policy in the generic builder: `build.c:1105` (fma: "arm64 only"), `1342, 1359, 1654` (Apple's `va_list`), `1515` (classification "for arm64", used for x86-64 too), `3132-3153` (arm64's offset ranges as "every CPU here"), `3473` and `vec.inc:1080` (`!X86`), `3531-3540` (cbz/tbz), `vec.inc:1445-1449` (arm64 immediates); `try.c:137` and `build.c:3946` name the targets | a third target edits the builder in nine places | a policy record the target fills (fma, branch joining, offset and compare-immediate tests, `va_list` model, `lower` function) | M | low | `vcc-same.py` both targets |
| Q-VIRB-17 | L6 | stale doc | SPEC-vir.md 14 (`bool vir_NAME(vir_fn *)`, order simplify, gvn, mem, simplify, dce) and PLAN-vir.md:60-65, 185-192 (`emit/vir/lower.c` calling the real CPU's `LO`; `vir/simplify.c`, `gvn.c`, `mem.c`) describe code that does not exist; `build.c:17-19` (REVIEW 5.2); AGENTS.md lists neither `try.c`, `walk.c`, `vw.h`, `arch/x64/emit/vir.c` nor `VCC_CCMP`, `VCC_VIR_CHECK`, `VCC_VIR_LIST` | the documents send a reader to the wrong files | rewrite with P2's ARCHITECTURE.md | S | none | reading |
| Q-VIRB-18 | L10 | dead | `vw_st_clear`'s parameter is ignored (`build.c:604-608`; nine callers pass `true`); `VW_NO` never used (1054); an empty `if` (881-883); `it` set and voided (1221-1232); `s->exposed ? 0 : 0` (3810); `vw_zerok` (2094) = `vw_undef` (326); `vw_fin_blk` (3816); `VCC_CCMP=all`, measured and not chosen (3609-3613) | noise; the slot hint at 3810 looks like a lost feature | delete | S | low | `vcc-same.py` |
| Q-VIRB-19 | L7 | misplaced | `a64.c:2369-2378` rewrites the VIR's operands before allocation (an IR pass in a lowering, arm64 only); both lowerings include `vw.h` to reach `V->f`, `V->lp`, `V->ao` | the lowering depends on the builder's private state; the two targets allocate different functions | a hand-off struct (function, loops, allocation); the copy-through rewrite as a `vir/` utility both use | S | medium | `vcc-same.py` arm64; x86 suites |

Q-02.  What is shared today: `vir_layout_chases/order/duplicate`
(`vir/layout.c`), `vir_low32`, `vir_mask32`, `vir_read_be`, `vir_sext_shift`
(`vir/facts.c`), `vir_uses`, the allocator and its checker.  What the copies
already disagree on: the dense-switch count (`a64.c:1270` adds
`h - a + 1 > 64 ? 64 : h - a + 1`, x64 `vir.c:91` adds `h-a>=64 ? 64 : h-a+1`:
different when a range covers the whole type), arm64 also tests `noopt`
(`1273`); x86-64 runs the block's trailing edits (`vir.c:974`), arm64 does
not; arm64 skips `VT_MULTI` operands (`2155`), x86-64 counts them (`967`);
arm64 caches `VCC_VIR_ROT`, x86-64 reads it per function.  Each may be
intended; none is stated.  Fold legality ("used once, by the next
instruction, not spilled, no allocator edit in between") is `vl_once`,
`vl_once1`, `vl_kept` (`a64.c:759-772, 1372`) and `vx_memnext`,
`vx_foldedits` (`vir.c:371-386`): one predicate in `vir/` would serve both.

Q-05.  Is the clean-up needed?  Partly, by construction: `fold` and `flags`
run only for VIR (`opt.c:1891`) and the lowering relies on them (`vl_mulk`
puts a constant "next to the madd, where it sees it", `a64.c:107`); copies
come from `trunc`/`ext`/`i2p` lowered as `mov` (`a64.c:1900, 1912`).  `slots`
and the second liveness redo what promotion and the allocator decided.  How
much each pass still removes on VIR's records is not measured anywhere.

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `vw_` | about 180 functions in `build.c` | four things: the `LO()` interface (`vw_load`), pre-IR primitives (`vw_put`, `vw_find`), passes (`vw_ifconv`), and the drivers and lowering entries (`vw_try`, `vw_a64`, `vw_x64`) | `vb_` builder interface, `pir_` pre-IR, `vpass_` passes, `vlow_` lowering driver |
| `vw_wk_` | `walk.c` | the walker's second instance | `vbwk_` (generated, `gen-walkprefix.py`) |
| `vl_`, `vl`, `VLT` | `a64.c` | arm64 lowering, its state, its thread-local buffers | `a64v_`, `a64v_state`; `VL_SLOT` (`vir/alloc.h:95`) is the allocator's and unrelated |
| `vx_`, `vx` | x64 `vir.c` | x86-64 lowering | `x64v_`; note `VX_MEM`, `VX_EXT`, `VX_LSL`, `VX_MADDL` and `vlx` are arm64's (`a64.c:1374`) |
| `VW3`, `VW2` | `a64.c:1590` | arm64 vector word encoders, with the builder's prefix | `A64V_W3` |
| `vv_`, `VK_`, `VJ_`, `VM_` | `vec.inc` | vectorizer; value kinds, object kinds, scalar modes | `vvec_`, `VVK_`, `VVOBJ_`, `VVAT_` |
| `vwp`, `vwb`, `vwst`, `vwh`, `vwhe` | `vw.h`, `build.c:2399` | pre-IR instruction, block, builder state, variable-table entry, CSE-table entry | `pir_ins`, `pir_blk`, `vb_state`, `vb_varent`, `gvn_ent` |
| `vw_emit` | `build.c:3895` | runs the pass pipeline, then writes VIR; "emit" means machine code everywhere else | `vb_finish_passes` + `pir_to_vir` |
| `vw_m`, `vw_k`, `vw_st` | `build.c:3793, 114, 49` | pre-IR value to VIR value; make a constant; get the state | `pir_to_vir_value`, `pir_const`, `vb_state_get` |
| `VLT.chs`, `hl`, `lbf`, `nf`, `c32`, `cop`, `clf` | `a64.c:76` | chase targets, rotated-loop per header, second label for a return block, no-frame calls, capacities | `chase`, `rot_loop`, `ret_label2`, `noframe`; capacities with the vector type |
| `L->soff` / `L->o->soff` | `a64.c:33` / `a64.c:598` | a slot's frame offset / the allocator's successor offsets, in one function | `slot_off`, `succ_off` |
| `V->map` | `build.c:1985`, `3948` | mark bits in `vw_deadparams`, then the pre-IR-to-VIR map | two fields |
| `var \| 0x80000000u`, `var \| 0x40000000u` | `build.c:2083`, `2314` | two more key spaces in Braun's table: entry value, "block defines it" | named key constructors |

Words with two meanings: **st**: state (`vw_st`, `vwst`), store table
(`V->st`, `vw_st_clear`), store type (`vw_stty`), stride (`build.c:1015`).
**slot**: a frame object, a spill slot, `vwp.slot` (slot + 1), a hash
position (`vw_hslot`).  **parameter**: `VO_PARAM` and block parameters, which
are also "phi" (`vw_newphi`, `vw_phiops`) and "param" (`vv_param`,
`vw_deadparams`).  **fold**: an ordered reduction (`VK_FOLD`), an instruction
folded into its user (`vl_cfold`), constant folding.  **pin**: hoisting
(`vv_pin`) and a vector address parameter (`S->pin`, `vec.inc:73`).
**chain**: `vl_chain` (ccmp), `vv_chain` (induction), `vw_cchain`.  **S**: the
simplifier's state, the vectorizer's state, and a block in `vw_condmerge`
(3587).  **lowering**: the walker's `LO()` interface and VIR-to-records.

Environment switches:

| Knob | Read at | Is | Used by a harness |
|---|---|---|---|
| `VCC_VIR` letter `l` | `try.c:26` | the product switch of the whole path until V7 | yes |
| `VCC_VIR` letters `b v d s` | `try.c:31` | debug (build only, verify, dump, report) | yes |
| `VCC_VIR_CHECK` | `a64.c:2412`, x64 `vir.c:1048` | debug: the allocation checker | yes |
| `VCC_VIR_LIST` | `try.c:54` | debug: bisecting | cleared by one |
| `VCC_VIR_VEC=0` | `vec.inc:1483` | product (`-fno-vectorize`), also stops `vv_pin` | yes |
| `VCC_VIR_VEC` `r` / `n` | same | debug report / measurement of `vv_pin` | `r` yes, `n` no |
| `VCC_VIR_ROT=0` | `a64.c:2210`, x64 `vir.c:1081` | measurement aid of the rotation work | no |
| `VCC_VIR_SHRINK=0` | `a64.c:2387` | measurement aid, arm64 only | no |
| `VCC_VL_PROBE` | `a64.c:455` | instrument: nops after allocator edits; changes code | no |
| `VCC_CCMP` | `build.c:3618` | `all`: a rejected experiment; `off`: debug; documented nowhere | no |

## 6. Decisions needed

1. **Where the optimizer lives** (extends REVIEW 6.1 from if-conversion to
   every pass).  The pre-IR exists because Braun's construction and the passes
   need what VIR does not have: insertion into any block (`vw_conv_in` appends
   to a predecessor), replacement without rewriting users (`fwd`), deletion.
   VIR is dense and append-only: blocks are ranges, values are numbered in
   block order; it is the form that is printed, parsed, verified and
   allocated.  Options: (a) declare the pre-IR as VIR's mutable form: its own
   header, the pass list as a table, a checker of the invariants in section
   2, and an importer VIR -> pre-IR so that `virtest --pass NAME` runs one
   pass on a `.vir` file; (b) make VIR editable and port the passes; (c) move
   only the CFG passes (11, 12, 14, block merging) to VIR.  Recommendation:
   (a).  It gives the testability and sharing REVIEW 2.4 asks for at the cost
   of an importer, and keeps one copy of each pass; then rewrite SPEC 14.
2. **One lowering driver** (Q-02): shared skeleton with target callbacks now;
   a shared fold-group pass over allocated VIR (which instructions a user
   absorbs) later, if a third target comes.  Recommendation: the skeleton now.
3. **arm64's record clean-ups after VIR** (Q-05): measure with `vperf` how
   many records each of `copies`, `slots`, `exts`, `dead`, `fold`, `flags`
   deletes on sqlite through VIR, and the instructions each costs; then keep
   a VIR pipeline of the passes that pay.  Not decidable by reading.
4. **Raw words or records** for FP and NEON on arm64 (72 hex words, decoded
   again by `word_regs`) and SSE as text on x86-64 (`X_RAW`): add record
   opcodes, or accept opaque instructions and stop optimizing across them.
   Belongs with quality-backend-records.md.
5. **Inlining by re-walking the AST** at each call site, against cloning a
   callee's built pre-IR.  Cloning needs a function to outlive its build (one
   live function per thread today, REVIEW 2.6).  Recommendation: keep, and
   record it in ARCHITECTURE.md as a decision with its measured cost.
6. **Knobs**: `VCC_VIR=l` and `VCC_VIR_VEC=0` become driver options or stay
   environment-only until V7; delete `VCC_CCMP`, `VCC_VL_PROBE` and
   `VCC_VIR_VEC=n`; keep `ROT`/`SHRINK` only as fields of one `vir_opts`.
7. **Target policy in the builder** (Q-16): a policy record, or per-target
   pass lists.  Recommendation: the record; the measured `!X86` exceptions
   (`build.c:3467-3479`, `vec.inc:1076-1081`) become two named fields.
8. **Prefix scheme and rename window** for section 5 (PLAN-quality decision 7).

## 7. Not covered

- Nothing was built or run; every statement about cost is a count made by
  reading, except the numbers quoted from PLAN-vir.md and REVIEW-vir-lowering.md.
- SPEC-vir.md was read in section 14 and its table of contents only;
  PLAN-vir.md in its design, pass, file and step sections and two status
  entries (inliner, compile time), not its 2500 lines of status.
- `vir/` only where a fact crosses the boundary (`vir_canon`, `vir_cfg`,
  `vir_dom`, `vir_loops_build`, `layout.h`, `facts.c`); `emit/walk/` only
  `wk_can_inline` and `wk_inline`; `arch/a64/emit/opt.c` and `func.c` only the
  drivers and the `vlow` forks.  The scan's `a64.c:1283-1295` =
  `arch/a64/emit/stmt.c:404-416` (jump-table buffers) and `a64.c:2444-2463` =
  `arch/a64/emit/func.c:801-819` (per-function reset) were confirmed as real
  copies; their home (`jt_reserve`, `fn_reset`) is for quality-walker-a64.md.
- The correctness of the vectorizer's legality rules and of the folds in both
  lowerings; whether the differences listed under Q-02 are bugs.
- Whether a failed `vv_loop` construction can leave anything reachable: the
  new blocks are not in `V->order`, but `V->npr` and instructions appended to
  no live block were not traced.
