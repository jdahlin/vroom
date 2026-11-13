# Quality review: C front end: declaration replay, snapshot, builtins (2026-11-13, main at 2e6d3e73)

## 1. Summary

The area is three files with three different problems.  `driver/ccsnap.c` (553
lines, on by default) is small and well explained, but it restores a `struct
cc` by "copy everything, then put back a hand-written list of 40 fields", and
that list is wrong today: a parse error after a restored prefix crashes a
multi-threaded process (Q-cc-cache-01, reproduced).  `cc/replay.c` (3479
lines, opt-in `VCC_DR=1`) describes `sym`, `tag`, `field`, `ctype`, `attrs`
and `node` a second time as field schemas; about 30 fields, most added on
2026-11-10/11, are in no schema, the checker `VCC_DRCHECK` reads the same
schemas and so cannot see it, and both caches give wrong diagnostics for it
(Q-cc-cache-02, reproduced).  `cc/builtin.c` is a table plus eleven unrelated jobs, and what
vcc knows about one builtin is spread over six places.

The three things that matter most:
1. One description of each front-end record (fields, which are references,
   which are locations), used by the struct, the snapshot and the replay, with
   a static assert that it covers the struct.  A C++ front end adds fields
   and node kinds; today each one must be remembered in up to eight places.
2. A decision on `cc/replay.c`: 2.5 % of the tree for 0.2-1.4 % of cycles,
   off by default (RESEARCH-preprocessor.md: "stays opt-in until it saves
   time").
3. One builtin descriptor table shared by `cc/` and the code generators.

## 2. Inventory

| File | Lines | What it actually does | Better name | Header comment |
|---|---|---|---|---|
| `cc/replay.c` | 3479 | Per header-cache hit at file scope: records what parsing the region read (older names, deep) and created, publishes that as an entry, and in a later TU validates the entry against that TU's state and copies the records in instead of parsing.  Also the checker (`drcheck`), 8 hash tables, statistics | `cc/declreplay.c` (or out of main, section 6) | good; stale in "step 2", omits `VCC_DR_GAIN` |
| `driver/ccsnap.c` | 553 | Snapshot of the whole parser state after a TU's leading top-level headers, in a trie keyed by the sequence of header-cache entries; restore is a page copy plus one pass over location fields | `cc/prefixsnap.c` (it is `cc` state, called only from `cc/`) | good |
| `driver/ccsnap.h` | 44 | `struct dcst` and the inline boundary test | with the file | label only |
| `cc/builtin.c` | 2425 | The builtin name table; signature letters; name directory; `__builtin_object_size` analysis (453-667); constant folding of builtins (671-865); the call argument-list parser `args_list`; typing of `__atomic_*`/`__sync_*`; generic bit counts incl. `_BitInt` loops; vector builtins; x86 SSE builtins lowered to lane code; fabs/copysign and fast-math rewrites; `os_log` format layout (1888-2117); the dispatcher `builtin_call` | split: `builtin_table.c`, `builtin_call.c`, `builtin_fold.c` (with object size), `builtin_atomic.c` (joins `atomic.c`), `builtin_x86.c`, `oslog.c` | label only |
| `cc/cc.h`, `cc/ccint.h`, `cc/ccimpl.h` | 338, 387, 776 | read as context: the records the caches copy | (other reviews) | good |

**What the module is for (draft opening comment).**  The front end keeps
three layers of reuse between TUs of one process.  (1) pp's header cache
serves an `#include`'s tokens.  (2) The prefix snapshot (`ccsnap.c`): the
parser state is a function of the token stream, so a TU whose stream starts
with the same header-cache entries (same pointers, same `__COUNTER__`) takes
a copy of the state another TU had at that boundary.  (3) Declaration replay
(`replay.c`): one later header hit is not parsed if an entry recorded from
another TU validates against this TU's bindings.

Invariants a newcomer must know:
- A header-cache entry pointer is an identity: entries are immutable and
  never freed (`pp/pp.h:316-322, 359`, `vccd/vccd.c:131-136`), so neither cache
  invalidates anything.  Both only grow, up to `VCC_DC_MAX_MB` (256) and
  `VCC_DR_MAX_MB` (512); nothing is evicted.
- Both act only at a file-scope declaration boundary with exactly one token
  of lookahead (`c->ntk == 1`), in this order (`cc/func.c:995-1002`):
  `dr_close`, `dc_boundary`, `drcov_boundary`, `dr_boundary`.
- A snapshot is taken only with no error and no diagnostic so far
  (`ccsnap.c:519-522`); state the snapshot does not hold kills the chain
  (`dc_kill`: `#pragma weak`, any error).
- Published snapshots and entries are immutable and shared lock-free (CAS on
  list heads); only counters, `drent.hits` and the lazily built relocation
  lists (`drent.rst` state machine) change after publication.
- Everything in a `struct cc` that is not an index must be classified for
  the snapshot (section 3); every field of a record must be in replay's
  schema.  Neither is checked.

## 3. Passes and data flow (L4)

**The two caches (area questions).**

| | `ccsnap.c` (prefix snapshot, default on) | `replay.c` (declaration replay, `VCC_DR=1`) |
|---|---|---|
| Cached | the used part of 21 arrays of `cc` (`abase`, 76-101) and the `cc` struct itself, in 16 KB pages shared with the nearest ancestor's snapshot when equal | per region: reads (`drread`), the reached older records W (6 kinds), the type log, own records in A's indices, writes to W, relocation lists |
| Key | trie path of (entry pointer, `__COUNTER__`) under a root per `opt_sig` = FNV-1a over every byte of `cc_opts` before `outfd`, plus `emit` (379-387) | (entry pointer, `__COUNTER__`, `#pragma pack` state, a hand-picked `opt_sig`, 1358-1375); up to 8 variants per key, then structural validation of W |
| Invalidation | none; `dc_kill` per TU | none; `never_tab` stops re-recording a key that failed twice |
| Threads | lock-free: `roots`, `kids`, `snap` by CAS; per-thread `img_buf` (never unmapped) | `dr_tab[65536]` chains by CAS; `tc_tab` reads lock-free, inserts under one mutex `tc_mu`; `never_tab` atomics |
| In step with the structs by | whole-array `memcpy`, then three hand lists: `LOCS` (144-182), `code_ptrs` (185-202), and the keep list in `snap_restore` (346-365) | six schema functions `*_fd` (93-245), `SYM_REFS`/`FIELD_REFS` (148-150), `REL_LOC` lines in `apply` (2819-2862), `lz_tr`/`lz_ok`/`lz_eq` (1690-1724), the `MARK` block for list element kinds (2102-2175) |
| Enforced by | nothing | `schema_check` (3324-3375): only that a schema field is at most 8 bytes and that `SYM_REFS`/`FIELD_REFS` equal the non-value fields of `sym_fd`/`field_fd`, and only when `VCC_DR` is set.  `VCC_DRCHECK` compares through the same schemas |

**What a programmer must remember today.**
- New field in `sym`, `tag`, `field`, `ctype`, `attrs`: replay's `*_fd`
  with the right kind; if a reference, `SYM_REFS`/`FIELD_REFS`; if a
  location, a `REL_LOC` line in `apply` and a line in ccsnap's `LOCS`; if a
  heap pointer, `code_ptrs` and replay's `heap_piece` handling; for `ctype`
  also `ty_hash`/`ty_eq` (`cc/type.c:44-58`).
- New node kind or operand: `node_kinds` (replay.c:198-230) and, if it owns
  a list, the `MARK` switch (2121-2136).
- New array in `cc`: `A_*`, `abase`, the keep list; in replay a start count
  in `drrec` and `drent`, `rec_open`, the copy in `rec_close`, `apply`,
  `drcheck`'s `CNT`, the byte count (2267-2273), two free lists (2304-2313,
  3415-3425).
- New non-index field in `cc`: one of heap pointer (`code_cc`), this TU's
  own (keep list), or a `dc_kill` at the place that sets it.
- New code that writes an older tag: `DR_TAGWRITE` (5 sites today).
- New parse option: before `outfd` in `cc_opts` (ccsnap), and by hand in
  replay's `opt_sig`.

**Walks.**  Snapshot take: every array copied to `img_buf`, pointer fields
coded, one walk of lazy tokens, lazy functions, syms, nodes with spans,
labels, tags with fields for locations, then each page compared with the
ancestor's and copied again if different (two copies of new data).  Restore:
page copy, the same location walk, pointer decode.  Replay, recording: a
hook per first lookup of an older name copies the reachable older records
(`w_sym` + `w_drain`, a worklist); a hook per interned or created type.
`rec_close` then walks W five times (diff per kind), W tags again for the
content key, the type log, own nodes, syms and tags for list marks, and
strings.  Replay, hit: `validate` walks the reads twice (cheap test, then
unification with a worklist) and W once; `apply` copies each own array,
then `rel_apply` walks its relocations, then a third pass adds the location
delta (`REL_LOC`, 2819-2862): locations could be one more delta class in
`rel_apply`.  `rel_build` (first valid hit) walks all own records again.

**`rec_close` (1764-2323, 560 lines) holds 13 jobs:** dispatch to `drcheck`;
the own-failure and gain gates; diff of W against the state after the
region, four kinds (1805-1863); the `VCC_DRCHECK` scan for writes outside W
(1864-1877); the entry header and six frozen hash maps (1885-1915); the
content key of each W tag, with three macros defined in the function
(1917-2023); copies of W; validation of the type log (2042-2080); copies of
own records with heap pieces; list element kinds (2101-2181); the check
that written fields map (2193-2234); bindings, toplevel and lazy bodies;
byte accounting, budget, variant limit, publication, and a 12-line free
list for the unpublished case.  Each is a function with `(cc *, drrec *,
drent *)`; the four per-kind loops become one loop over a kind table.

**Builtins.**  Described by one table `builtins[]` (17-267: 405 names for
149 of the 159 ids; name, id, a flags byte, a signature in vcc's own letters), found
through a page directory by identifier id (`bi_dir`), dispatched by
`switch (b->id)` in `builtin_call` (2119-2425, 306 lines) and folded by
`switch (x->aux)` in `eval_builtin` (671-865).  Each code generator has its
own `switch (x->aux)`: `gen_builtin` (`arch/a64/emit/builtin.c:885`, 117
ids), `wk_builtin` (`emit/walk/expr.c:2848`, 98), `wbuiltin`
(`arch/wasm/emit/wcall.c:1049`, 96); 88 ids are in all three, 34 never reach
a generator (lowered or folded in `cc/`).  An id a target lacks is a compile
error at run time with a number ("unsupported builtin (%d)").
`cc/libbuiltins.inc` (366 names, generated from clang) is a second table in
`cc/decl.c:491` in clang's signature encoding, for `__builtin_X` that is
only a call to `X`; `libfuncs.inc` (354) gives headers for diagnostics.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-cc-cache-01 | L8 | thread (bug) | `ccsnap.c:344-365` keeps no `c->rec`; `cc/diag.c:149` sets it to a stack frame; `cc/func.c:993`; `cc/diag.c:73` | a TU with a syntax error after a restored prefix kills `vpp --batch -j 4` (6 of 6 runs); the same code runs in vccd | keep `rec` (and `tyb`); then Q-cc-cache-04 | S | low | reproducer below into `vcc-stress.py`, batch and `--vccd`; TSan |
| Q-cc-cache-02 | L1 | duplicate (bug) | schemas `replay.c:93-245`, `LOCS` `ccsnap.c:144-182`; 30 fields in neither (below) | wrong note location by default; wrong deprecated message under `VCC_DR=1`; checker blind | one field table per record in `cc/cc.h`, static assert on coverage | M | medium | new test: diagnostics equal with and without each cache; `vcc-same.py` |
| Q-cc-cache-03 | L10 | dead (decision) | `replay.c` whole; hooks in `cc/sym.c:235`, `type.c:72,111,224`, `token.c:403,439,448`, `record.c:50,242`, `decl.c:324,347`, `declspec.c:926`, `func.c:996-1001` | 3479 lines and about 15 hook sites that every new record field and tag write must respect, for 0.2-1.4 % cycles | section 6, decision 1 | S (remove) / L (keep) | low | `vbench.py gate`; premerge without the five `VCC_DR` harnesses |
| Q-cc-cache-04 | L8 | global state | `ccsnap.c:329-372`; `docs/PLAN-fas2.md:529` (the same class of bug: `cases`, `span`) | every new field of `cc` is silently taken from another TU unless someone adds it to the keep list | split `struct cc`: `cc_tu` (pp, opts, jmp_bufs, tokens, array bases, scratch, caches) and `cc_state` (what a snapshot holds); the snapshot copies `cc_state` only | M | medium | `vcc-same.py`, `vcc-cbatch.py` under TSan and ASan |
| Q-cc-cache-05 | L1 | duplicate | `ccsnap.c:379-387` against `replay.c:1358-1375` | replay's key omits 10 parse options that ccsnap's includes (`strict_std`, `conv_warn`, `wasm_sjlj`, `vcc_kernel`, `lse`, `no_fp_contract`, `no_jump_tables`, `omit_frame_pointer`, `sibling_calls`, `dump_layouts`); TODO.md:435 fixed one side only; five premerge harnesses exist to catch one forgotten option each | `cc_opts_sig()` in `cc/`, one definition | S | low | `bench/vcc-cachekey.py`, the five harnesses |
| Q-cc-cache-06 | L1 | pattern | per-builtin facts in: `ccint.h:293-337` (enum), `builtin.c:17` (table), `builtin.c:2119` (typing), `builtin.c:671` (folding), three generator switches, `arch/a64/emit/builtin.c:31-46` (side effects by id range), `cc/func.c:176` = `cc/flow.c:86` (noreturn), `cc/expr.c:693,703,721` (fake table rows for keywords) | adding a builtin touches 5-8 places; nothing says which target lacks which id | `cc/builtins.def`: X(id, flags, arity) rows generating the enum, names and a flags table (pure, noreturn, writes memory, front-end only); a premerge script lists ids per generator switch | M | low | `vcc-same.py`; `vcc-builtins.py` |
| Q-cc-cache-07 | L4 | long function | `replay.c:1764-2323` | 13 jobs, macros defined mid-function, a 12-line manual free list | split as in section 3; `drent_free()` | M | low | `VCC_DRCHECK` on PG and the kernel |
| Q-cc-cache-08 | L1 | near-duplicate | `tr` 1224-1324, `rel_one` 1400-1513, `pair` 2470-2537, `w_refs` 806-823, macros `PUT`/`PUTR` 1941-1972 | the rule "what does kind K mean for value v" is written five times; comments say "(as rel_one)" | one classifier `ref_class(e, kind, v) -> {own delta, W position, builtin, null, bad}`; the five callers act on its result | M | medium | `VCC_DRCHECK` |
| Q-cc-cache-09 | L1 | duplicate | four per-kind loops in five places: 1805-1860, 1864-1876, 2193-2234, 2902-2940, 3133-3177; also 1138-1145, 738-789 | 20 copies; they have drifted: `drcheck` checks sym, tag and type writes but not attr writes or `XF_F` | a table `wkind[W_N] = { base, element size, schema }` and one loop each | S | low | scan: runs 3132-3177, 2902-2936, 2193-2230, 1865-1875 gone |
| Q-cc-cache-10 | L2 | pattern | `replay.c`: `dr_tab` 380, `never_tab` 417, `tc_tab` 575, `rs_add` 627, `wmap` 659-727 (three copies of one probe loop), `rt_insert` 955, `rf_insert` 1005, `wk` 1898-1914 + `wfind` 1178; `ccsnap.c`: `root_get` 389 = `child` 407 | eight tables in one file; `rt_insert`/`rf_insert` repeat the rehash loops of `cc/type.c:82-92` and `237-249` | `lib/`: a u64 -> u32 open-addressing map (set, frozen form) and a CAS-prepend list; `type.c` exports its table insert | M | low | `vir-test`-style unit test for the map; `vcc-same.py` |
| Q-cc-cache-11 | L1 | duplicate | `sigtype` `builtin.c:327-356`, `lb_type` `cc/decl.c:496-552`, `wlib_type` `arch/wasm/emit/wcall.c:30`; `cc_lib_sig(name, &clang_enc)` `builtin.c:309` | two signature encodings, three decoders; 103 names are in both `builtins[]` and `libbuiltins.inc`; `decl.c:716-726` repeats `lb_functype` (560) | clang's encoding everywhere; `builtins[]` keeps only what `libbuiltins.inc` cannot say (id, kind) | M | low | `vcc-builtins.py`, `tests/vcc/lib_builtins.c`, `vcc-same.py` |
| Q-cc-cache-12 | L6 | pattern | range tests and subtraction on enum order: `builtin.c:1036,1105,1167`, `cc/atomic.c:189-220`, `arch/a64/emit/builtin.c:38-42,807,820,991-1014`, `emit/walk/expr.c:2363,2483,2517`, `arch/wasm/emit/wcall.c:937,966` | inserting an enumerator in the wrong place miscompiles atomics silently; only two comments in `ccint.h:315,323` say so | `_Static_assert`s next to the enum (first/last of each range), or class bits in the flags table of Q-cc-cache-06 | S | low | compiles |
| Q-cc-cache-13 | L1 | pattern | "anonymous local holding a value": `builtin.c:1188-1194, 1469-1472, 1492-1499, 1572-1574, 1680-1682, 1727-1734, 1821-1823`, `cc/atomic.c:62-72`, `cc/complex.c:414-419, 495-500`; "call the library function of row b": `builtin.c:1541-1553, 1584-1591, 2412-2421` | 10 + 3 copies of 4-8 lines; `at_save` (`cc/atomic.c:59-69`) and `bitcount_local` (`builtin.c:1186-1195`) are one function under two names | `tmp_var(c, type, loc)` and `tmp_decl` next to `cplx_reuse` in one file; `lib_call(c, b, args, rt, loc)` | S | low | `vcc-same.py` |
| Q-cc-cache-14 | L5 | misnamed | `elementwise_abs_effects` `builtin.c:1598-1642`, used at 762, 783, 927, 1659, 1802, 1851; twin `array_copy_effects` `cc/init.c:750-790`; `has_side_effects` `cc/conv.c:163`; `side_effects` `arch/a64/emit/builtin.c:14` | a general "evaluating this is observable" predicate named after one builtin, and four answers to the same question | one `expr_effects(c, n, flags)` in `cc/` (flags: loads of volatile count, fold short circuits); the emitter asks the same one | M | medium (answers differ today) | `vcc-ctests`, `vcc-volatile.py` |
| Q-cc-cache-15 | L5 | mixed file | `builtin.c`, jobs in section 2 | 2425 lines; the object-size analysis and the os_log encoder are found only by scrolling | split as in section 2 (moves only) | S | low | `vcc-same.py`, file-by-file build |
| Q-cc-cache-16 | L6 | pattern | name-string dispatch after the id: `x86_simd_call` 1452-1512, `x86_pcmpstr_call` 1431-1445, `atomic_lock_free_call` 1843-1845, `atomic_call` 1105, `cc_builtin_plain` 289-304, `builtin_call` 2329, 2400; `sig` used as flags (`"s."`, `"vc"`, `"si"`) 1057-1082 | one id for 8 x86 builtins, re-split by `strcmp`; the "signature" of `__atomic_*` rows is not a signature | a small `variant` byte in the table row; `_chk` -> plain as a column | S | low | `vcc-ctests`, `vcc-x86.py` |
| Q-cc-cache-17 | L7 | misplaced | `cc/ccimpl.h:35` includes `../driver/ccsnap.h`; `ccsnap.c:32` includes `../cc/ccint.h`; built in `PP_SRC` (`ccs/Makefile:87`) | one of the four module cycles (`cc` <-> `driver`); the file that copies `struct cc` is not in `cc/` | move to `cc/` (public `dc_*` stay) | S | low | builds, `vcc-same.py` |
| Q-cc-cache-18 | L10 | dead | `cc/ccimpl.h:81` flags bit 0 "constexpr": all 405 rows carry it, nothing reads it; `BI_CPU_SUPPORTS` `ccint.h:310`: no user; `builtin.c:400-421` a `done` flag under `pthread_once` (`cc/tu.c:214-218`); `replay.c:2137` `(void)s`, `2898` `(void)ty0`, `pair_w` 2350; `ccsnap.c:321-324` both branches `return s` | noise that reads as meaning | delete | S | low | compiles |
| Q-cc-cache-19 | L9 | error policy | `replay.c`: 21 `malloc`/`calloc`/`realloc` unchecked (`VGROW` 39-45, `xdup` 54, `grow` 1089, `ensure` 2333); three `fprintf` + `abort` (2798, 2806, 3372); `ccsnap.c`: `dalloc` aborts, `calloc` at 398, 418, 447 unchecked | no policy; a failed allocation is a wild write | the tree's one `xalloc` family (quality-lib) | S | low | ASan with an allocation limit |
| Q-cc-cache-20 | L6 | pattern | `replay.c` leaves `M`, `K` (3445-3446), `ADD`, `VEC`, `VGROW`, `VPUSH`, `FD`, `FDR`, `REL_LOC` defined; static names `apply`, `pair`, `tr`, `count`, `grow`, `ensure`, `validate`, `rd`, `wr`; `enum { S_OPEN, ... }` 413 beside the sym kinds `S_VAR`, `S_TAG` | in the unity TU these reach `stmt.c`, `sym.c`, `token.c`, `tu.c`, `type.c`; no clash today, by luck | prefix rule per file; `#undef` at the end until then | S | low | both builds |
| Q-cc-cache-21 | L8 | global state | `ccsnap.c:222-227` and `replay.c:2279` only add; entries live as long as the process; `ccsnap.c:523-527` a snapshot that loses the CAS is neither freed nor uncounted; `img_buf` 251 never unmapped | inferred: a long-lived vccd fills both caps with entries of headers that no longer exist, then stops caching | decision 6 | M | medium | measurement in vccd over several builds |
| Q-cc-cache-22 | L2 | pattern | `replay.c:543-553` `ticks()` reads `cntvct_el0`; `3469` prints "24 MHz ticks" | a seventh private clock, zero on x86-64, the frequency assumed | `lib/vperf.h` | S | low | `--stats` output |
| Q-cc-cache-23 | L10 | stale doc | `cc/ccimpl.h:143` files `dr_*` under "tu.c"; `builtin.c:1340` "lowered in emit (builtin.c)"; `cc/cc.h:210` `N_BLOCK` says nothing of `a`/`aux` that `replay.c:223` relies on; `builtin.c:659-667` = `679-691` (the object-size end rule twice) | misleads | fix with the moves | S | low | none needed |

**Q-cc-cache-01.**  `translation_unit` arms a recovery point on its own
stack before the boundary loop, so `c->rec` points into the recording
thread's stack when the snapshot is taken; `snap_restore` copies the whole
struct and does not put `rec` back.  Reproducer (run with the existing
`build/ccs/vpp`, nothing built): a header of 400 struct and prototype
declarations; 12 files `#include "h.h"` plus one function; 6 files `#include
"h.h"` then `int x = ;`; `vpp --batch jobs -j 4`.  Result: exit 139 or 134 in
6 of 6 runs, at most 6 lines of diagnostics; with `VCC_NODC=1` or `-j 1`:
exit 1 and all 72 lines.  The cause is from reading, not from a fix.
`c->tyb` (malloc, freed per TU, `cc/tu.c:341`) is saved only because a
snapshot requires no diagnostics so far; that is an invariant nobody wrote
down.

**Q-cc-cache-02.**  Not in replay's schemas: `sym.nref`, `tdef`, `defloc`;
`tag.ualign`, `depr`; `field.align`, `invalid`, `tdef`, `depr`; `ctype.ext`
(which `ty_eq` compares); 18 fields of `attrs` (`x86_features` ... `tlsmodel`,
among them the string indices `deprmsg`, `errmsg` and the locations `depr`,
`tlsloc`); `N_CAST`'s `b` (a sym).  Own records are copied whole and only
schema references are relocated, so these keep the recording TU's indices.
Not in ccsnap's `LOCS`: `sym.defloc` and every location in `attrs` (the
macro never visits `attrs`).  Those dated with `git log -S` (`defloc`,
`tdef`, `nref`, `ualign`, `deprmsg`, `tlsloc`, `ext`) were added on
2026-11-10/11, after the schemas (2026-11-09).  Two runs on the existing
binary:
- default build, five TUs of different length including one header that
  declares `old_fn` deprecated and calls it: after a restore the note
  "marked deprecated here" is at `h.h:402`, `401`, `400` instead of `403`.
- `VCC_NODC=1 VCC_DR=1 VCC_DR_GAIN=0`, two different first headers, then the
  same header: the replaying TUs print "'old_fn' is deprecated: sec_one" (a
  section name from the other header) instead of "use new_fn instead".
  With `VCC_DRCHECK=1`: "4 regions compared, 0 mismatches".
Fix: `SYM_FIELDS(X)` and so on in `cc/cc.h`, each row (type, name, kind);
the struct, replay's schema, ccsnap's location and pointer lists and a
`_Static_assert` that the rows fill the struct all come from it.  A table
`node_operands[N_NKINDS]` replaces `node_kinds` and gives the generic child
walk that review B wants.  The checker should compare whole records after
relocation, not schema fields.

**Q-cc-cache-06.**  Detail for the plan: 159 ids; 405 names; 34 ids are
front-end only and should not be in the enum the generators switch on (or
be marked so); `builtin_call` should index a table of handlers by id class
instead of a 306-line switch whose `default` falls into 90 lines of special
cases (2328-2424).  `__has_builtin` reaches the table through a function
pointer set by a constructor (`builtin.c:393-396`), a hidden pp -> cc edge.

**Duplicate scan, triaged** (60+ tokens, 83 runs in the three files).  Real
copies that want one helper: Q-cc-cache-09 (about 30 runs), 08 (1241-1319
against 1412-1503), 10 (type.c against 960-1023, 633-675), 13 (builtin.c
1469-1735 and atomic.c, complex.c), the `*N$` parse in `oslog_layout`
(1963-1980, twice).  Inherent table shape: `abase` (`ccsnap.c:79-97`), the
`xdup` block (`replay.c:2024-2032`), the schema functions (93-195), the
`atomic_call` cases (1110-1138).  `LOCS` (`ccsnap.c:153-175`) is a table that
Q-cc-cache-02 generates.  FNV in `replay.c`: one FNV-1a (`tc_intern` 581),
three FNV-prime loops in `opt_sig` (1366-1372), and three golden-ratio mixes
(392, 419, 625); `ccsnap.c` has one more FNV-1a (382).

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| "replay" | `pp/` (`C_REPLAY`, "replayed region"), `cc/replay.c`, `emit/tu.c:287`, `emit/obj.c` | pp: tokens of a cache entry delivered again; cc: declarations copied from an entry; emit: per-function item logs applied in output order | pp: **serve** (an entry is recorded and served; `C_SERVE`); cc: keeps **replay** (`declreplay.c`); emit: **commit** (`tu_commit_items`) |
| "declaration cache", `dc_`, "decl cache" in statistics | `ccsnap.c`, `ccint.h:269` | the prefix snapshot; RESEARCH-preprocessor.md calls it "prefix snapshots", and layer 3 also caches declarations | **prefix snapshot**, prefix `psnap_` |
| `die` | `ccsnap.c:469` | marks this TU's chain dead; not a fatal error | `chain_end` |
| `fd`, `FD`, `FDR` | `replay.c:89-91` | field descriptor | `fdesc` |
| `rdz`, `wrz`, `rd`, `wr`, `drent.rd`, `drread` | `replay.c:248-280, 325` | read/write n bytes; read/write a field; the list of name reads | `get_le`, `put_le`, `field_get`, `field_put`, `reads` |
| `tk`, `tk_set`, `tk_val`..`tk_w` | `replay.c:529, 608, 542` | tag content keys; tick counters; `c->tk` is the token lookahead | `tagkey`, `ticks_*` |
| `rt`, `rf`, `rt_insert` | `replay.c:511-514` | the region's type and function-type tables; `rt_defer`/`rt_flush` in `ccimpl.h:198` are run-time diagnostics | `regty`, `regfn` |
| `r0` | `drrec` 1121 against `drent` 1891 | params start in one struct, strings start in the other | spell the counts: `sym0`, `tag0`, `type0`, `node0`, `list0`, `ipool0`, `fpool0`, `str0`, `attr0`, `param0` |
| `drop` | `replay.c:296` | one type-log operation | `tyop` |
| `W`, `w_*`, `wm_*`, `wk`, `ws`..`wl`, `xs`..`xy`, `un`, `trx`, `tr` | `replay.c` | the older records a region reached ("world"); its map; writes; unifier; translator | `reach_*` for W; `write_*`; `unify_*`; `xlate_*` |
| `RO_DS`..`RO_DA`, `RA_*` | `replay.c:308-310` | relocation op "add the delta of array S"; relocated array | `REL_ADD_SYM`...; `RELARR_*` |
| `S_OPEN`..`S_N`, `F_*` | `replay.c:413, 466` | statistics and failure bits; `S_` is the sym-kind prefix | `DRS_*`, `DRF_*` |
| `A_TY`..`A_N`, `abase`, `coder`, `P` | `ccsnap.c` | array ids, array base and size, pointer/location coder | `ARR_*`, `arr_span` |
| `bitcount_local`, `bitcount_statement` | `builtin.c:1186-1200`, used by `os_log_call` 2088-2112 | make a local with an initializer; append an expression statement | `tmp_decl`, `stmt_expr` |
| `sig` | `builtin.c:82` | a signature, or flags for atomics | Q-cc-cache-16 |
| `builtin.c` | `cc/`, `pp/`, `arch/a64/emit/` | three files of one name with three jobs | `cc/builtin_*.c`, `pp/ppmacros.c`, `arch/a64/emit/builtin_gen.c` |

Prefix conventions and where they break: `dc_` is public ccsnap, `dr_` is
public replay, but `cc/tu.c:224` names the DRCOV counters `dr_tok`,
`dr_hits`, ... and its functions `drcov_*`; inside `replay.c` only the hooks
carry `dr_`, the other 80 functions have two-letter or no prefixes;
`ccsnap.c` and `replay.c` both use `st_*` for counters and both define an
`opt_sig`.  `builtin.c` has no prefix at all (`arity`, `sigtype`,
`classify_type`, `object_*`, `oslog_*`).

## 6. Decisions needed

1. **Declaration replay (layer 3) before C++.**  Options: (a) keep in main
   and generate its schemas from the field table of Q-cc-cache-02, with
   findings 07-10 done (L, and every C++ record must be replayable or mark
   the region failed); (b) move to a branch with RESEARCH-preprocessor.md
   as the record, and remove the hooks and five premerge harnesses (S); (c)
   leave as is.  Measured (RESEARCH-preprocessor.md, 2026-11-09): 5-7 %
   fewer instructions, 0.2-1.4 % fewer cycles on top of snapshots, 500 MB
   of entries.  Recommendation: (b); return to it after the field table
   exists and only if a cycle measurement justifies it.  (c) is the one
   option that should not survive: its schemas are already wrong.
2. **Field table as X-macros in `cc/cc.h`** (struct generated from rows) or
   hand-written structs plus a separate descriptor table checked by
   `_Static_assert(sum of sizes == sizeof)`.  The first cannot drift; the
   second keeps the structs readable.  Recommendation: the second for
   `sym`/`tag`/`field`/`ctype`/`attrs`, with the assert, and a real table
   for node operands.
3. **Split `struct cc`** into per-TU and snapshot state (Q-cc-cache-04).
   Recommendation: yes, before C++ adds parser state; it also answers what
   C++ needs from `ccint.h` (L7).
4. **Builtin table**: one `.def` shared by `cc/` and `emit/` with clang's
   signature encoding, or keep vcc's letters.  Recommendation: `.def`,
   clang's encoding, library rows generated.
5. **Names** of the three layers and the three "replay"s (section 5).
6. **Eviction** for both caches or a flush at vccd's build boundary
   (`vccd/vccd.c:131`): needs a measurement of memory over several builds.

## 7. Not covered

- Read in full: `cc/replay.c`, `cc/builtin.c`, `driver/ccsnap.c`,
  `driver/ccsnap.h`, `cc/cc.h`, `cc/ccint.h`, `cc/ccimpl.h`.  Read in
  excerpts only: `cc/type.c`, `cc/decl.c`, `cc/expr.c`, `cc/diag.c`,
  `cc/tu.c`, `cc/func.c`, `cc/init.c`, `cc/conv.c`, the three generator
  dispatchers (counted by a script over `BI_*` names, not read), pp's side
  of regions (`pp_top_events`, `pp_skip_top`, `pp_skip_hit`).
- The three runs used `build/ccs/vpp` as found (built 2026-11-13 13:13);
  nothing was built, no sanitizer build was run, vccd itself was not run.
  The cause named in Q-cc-cache-01 is from reading.
- Which of the ten options missing from replay's `opt_sig` change what a
  header region parses to: not verified.
- `sym.defloc` after a restore, `sym.tdef`/`field.tdef` after a replay:
  wrong by reading, no run.
- Memory growth of either cache in a long-lived vccd: inferred.
- Whether the four side-effect predicates disagree on a real case.
- The cost of each walk in instructions (review B).
