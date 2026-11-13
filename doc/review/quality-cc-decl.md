# Quality review: C front end, declarations, statements, flow (2026-11-13, main at 2e6d3e73)

Read in full: `frontend/c/{decl,declspec,declarator,attr,init,stmt,func,flow,lazy,tu,mangle,fmt}.c` and
`frontend/c/{cc,ccint,ccimpl}.h` (11 363 lines).  Other files only where a duplicate's other side is.
Nothing was built; `build/src/vpp` (built today) was run on two scratch files to confirm two findings.

## 1. Summary

The parsing code is in good order: one pass, clang-exact, and most of the twelve files hold the
subject their name says.  What is not sharp is everything that happens *after* a body is parsed:

1. **Reachability is written three times** (`fl_stmt` in flow.c, `stmt_flow` in func.c, the `live` bit
   of `uv_stmt` in flow.c), with their predicates written twice.  The copies have drifted: seven rules
   differ, one gives a wrong warning today (enum-covered switch, verified, TODO.md:1660).
2. **The AST forgets what the parser knew.**  A switch's case list, its default label and each
   condition's constant value are computed while parsing, dropped, and recomputed by walking the body
   in 4 to 6 places (and again in the emitters).
3. **func.c and flow.c are named for a quarter of what they hold.**  func.c has six jobs, flow.c is
   1030 lines of `-Wuninitialized` under a header that says "can a body fall off its end".

Besides these: 24 hand-written recursive walkers over the AST in these files, five functions of
330-455 lines, a per-attribute cost of eight edit sites, and about a dozen small helpers written two
to six times.  For C++ the main obstacle is `struct cc`: parser state, semantic tables and analysis
scratch in one struct that `codegen/` also sees.

## 2. Inventory

| File | Lines | What it actually does | Better name | Header comment |
|---|---|---|---|---|
| `decl.c` | 1252 | `declaration()` (one declaration, all its declarators, initializer dispatch), `declare()` (redeclaration, linkage, composite type), `#pragma weak`, K&R parameters, implicit function declarations, **and** the library-builtin tables with clang's signature-string decoder (489-803) | keep; move 489-803 to `libfunc.c`, 113-280 to `pragma_weak.c` | label only |
| `declspec.c` | 1045 | `decl_specs()`, tag lookup, typeof, **and** `_Static_assert` with its failure explanation and the record-layout note (119-331) | keep; `static_assert.c` for 119-331 | label only |
| `declarator.c` | 517 | declarator syntax into the `dop` stack, parameter lists, `apply_declarator`, `type_name`, `vector_attribute` (a type operation) | keep | label only |
| `attr.c` | 735 | attribute name table, `attribute_list_`, `merge_attrs`, unknown-attribute typo correction, x86 `target("...")` parsing, asm labels | keep | label only |
| `init.c` | 967 | initializer lists (`ilist` builder, designators, brace elision) to `N_INIT`; `static_init_ok` (constant check, mirror of `codegen/data.c`); `expr_loc` | keep | label only |
| `stmt.c` | 1351 | statements, asm statements, switch checks, **and** the unused-value/unused-result/return-stack-address diagnostics (30-407) | keep; `unused.c` for 30-239 | label only |
| `func.c` | 1027 | `function_def`, `translation_unit`, `-finstrument-functions`, second reachability walker, return-path explanation, object-size provenance, post-body constant folding | split, see Q-ccdecl-04 | stale ("-pg": none here; omits four of its jobs) |
| `flow.c` | 1403 | `body_falls_off` (1-370), `-Wuninitialized` dataflow and its reports (372-1403) | `flow.c` + `uninit.c` | stale (describes lines 1-370) |
| `lazy.c` | 211 | header function bodies kept as tokens, parsed at the end if named | `lazybody.c` | good (with ccint.h:94-99) |
| `tu.c` | 344 | per-TU table set-up and release, `cc_run`, `VCC_DRCOV` statistics | `ccrun.c` (`codegen/tu.c` is a work queue: same name, other job) | label, partial |
| `mangle.c` | 179 | Itanium mangling of `overloadable` functions | keep | good |
| `fmt.c` | 831 | `-Wformat` for printf (Darwin, arm64 Linux tables), typedef sugar recovery (`expr_tdef`) | `format_check.c` | label only |
| `cc.h` | 338 | public: types, records, syms, nodes, `cc_opts` | | good |
| `ccint.h` | 387 | "internals shared with codegen/": `struct cc` whole, builtin ids, `cval` | see Q-ccdecl-17 | label; field comments displaced (162-168) |
| `ccimpl.h` | 776 | cc-internal prototypes, inline token helpers | | good opening; section labels stale (Q-ccdecl-15) |

Draft of the module's opening comment (what a newcomer must know, none of it is written in one place):

- Everything is an index into per-TU arrays that are reserved once and never move; index 0 is "none".
  String fields of `attrs` hold index + 1.
- A node is typed and converted when it is made.  Statements are the node kinds from `N_BLOCK` up
  (flow.c:903 relies on `op < N_BLOCK` meaning expression).
- One function's nodes, syms and labels are contiguous ranges (`func.c:875-876`); the linear scans in
  func.c depend on that.  A lazy body is parsed after the TU, so its range comes last, not in source order.
- All checks over a finished body run in `function_def`, on the parsing thread, before `emit_tu`.
  After an error in the body (`clean == false`) no flow check runs.
- `fatal` and failed `expect` longjmp to the innermost `recov` (one per block); `NF_ERR` nodes and
  `SF_INVALID` syms silence follow-on errors; `delayed_bug` turns "went quiet without an error" into an ICE.
- Process-wide tables (`kwclass`, builtins, `attr_dir`) are filled once under `pthread_once` in
  `cc_run`; `kwmap` is per thread, set per TU.

## 3. Passes and data flow (L4)

**While parsing a body** (each construct once): `if` evaluates its condition for `c->dead`
(stmt.c:1063); each switch runs `switch_enum_warn`, the constant-condition check and
`switch_duplicates` over `c->cases` (stmt.c:1203-1222); each expression statement goes through
`unused_cmp`/`unused_result`/`set_only`/`unused_value` (stmt.c:1334-1343, the walker `uv_warn`);
each `return` through `return_stack_addr` (stmt.c:1277); `-Wuninitialized` candidates are noted in
`c->uv[]`, `c->uv_any` (decl.c:1227-1232; stmt.c:967, 982, 1302).

**After the body**, `function_def` (func.c:872-985), in this order:

| # | Call | Walker | Runs when | Computes |
|---|---|---|---|---|
| W1 | func.c:939 `body_falls_off` | `fl_stmt` flow.c:210, up to 16 passes (flow.c:357) | body clean, and non-void non-`main` or `noreturn` | end reachable 0/1/2; reached labels; "a return / a noreturn call is live" |
| W2 | func.c:944 `return_type_warn` | `stmt_flow` func.c:341 to a fixpoint (725-728), then `rp_walk` func.c:531, which calls `stmt_flow` again per subtree through `end_of` (func.c:483; "quadratic in the nesting depth", func.c:450) | W1 said 1 or 2 | the same reachability, then one path to the end as numbered steps |
| W3 | func.c:948 `uninit_check` | `uv_stmt`/`uv_ex` flow.c:895/651, up to 16 passes (flow.c:1349), each loop to its own fixpoint, up to 9 walks of its body (flow.c:847-890) | `uv_any & 14` | per tracked variable: maybe-uninitialized / maybe-initialized at every point, and liveness (`ust.live`) |
| W4 | func.c:975 `instrument` | `instrument_returns` func.c:44 | `-finstrument-functions` | rewrites every `return` |
| W5 | func.c:977 `fold_local_builtins` | linear scans over the function's nodes: 832-838 always at `-O1`+, then 842-847 (syms), 848-854, 855-868 with `local_constant` (773) per candidate | `-O1`+, body clean | folds `__builtin_constant_p`, fast-math `pow`/`log2` of never-reassigned locals |
| W6 | func.c:979 `object_size_locals` | linear scans 746-751, 752-758 (syms), 759-767 | `SF_BOS_QUERY` | `SF_BOS_INIT` on never-reassigned local pointers |

A typical non-void function at `-O0` gets W1 only; at `-O2` W1 and one linear scan.  The cost per walk in
instructions was not measured here (quality-passes.md).

**Facts computed more than once** (confirmed by reading both sides):

| Fact | Where |
|---|---|
| can the end of a statement be reached | `fl_stmt` flow.c:210; `stmt_flow` func.c:341; `uv_stmt`'s `st->live` flow.c:895 |
| a condition is constant true/false | `fl_const` flow.c:24; `cond_value` func.c:191; `uv_const` flow.c:467 (calls `fl_const`); `cc_eval` at parse time, stmt.c:1063, result dropped |
| an expression never returns | `fl_noreturn` + `fl_has_noreturn` flow.c:75, 100; `expr_noreturn` func.c:151 |
| the switch has a default label | parser `c->sw_default` stmt.c:980, dropped at 1224; `fl_has_default` flow.c:168; `has_default` func.c:270 |
| the switch's case values | parser `c->cases` stmt.c:962, dropped at 1223; `fl_case_match` flow.c:139; `case_values` func.c:233 |
| a constant switch's value and whether a case matches | stmt.c:1208-1212; flow.c:280-285; flow.c:1086-1090 |
| labels reached | `flw.lab` flow.c:352; `flow.lab` func.c:722; `uwk.lab` flow.c:1337 |
| locals never assigned after their initializer | func.c:752-767; func.c:842-854 |
| the function has a label, goto or asm | func.c:746-751; func.c:832-838; `uv_any & 8` (stmt.c:968, 983, 1303) |

**The two reachability walkers do not agree.**  Differences found by reading:

| Rule | `fl_stmt` (decides) | `stmt_flow` (explains) |
|---|---|---|
| switch whose cases name every enumerator | falls through | no edge past it (`enum_covered`, func.c:299) |
| constant switch condition | prunes cases (flow.c:280-305) | not pruned |
| computed goto | reaches every address-taken label (flow.c:313) | "unsure", gives up (func.c:367) |
| noreturn call inside an expression | ends the path only as the whole statement (flow.c:226); elsewhere a flag | ends it in arguments, both `?:` arms, conditions, `for` init/step, initializers (func.c:151-187, 382, 395, 414) |
| statement expression | only as the whole statement (flow.c:224) | anywhere (func.c:182) |
| constant condition | a `volatile` const variable is not constant; looks through `CK_BOOL/INT/NOP` casts only (flow.c:60-64) | looks through any cast, `volatile` not checked (func.c:203, 222) |
| declaration's initializer | ignored (flow.c:233) | checked (func.c:381) |

The first row is visible: `enum e {A,B}; int f(enum e x){ switch (x) { case A: return 1; case B: return 2; } }`
warns `-Wreturn-type` in vcc, not in clang (run today; known as TODO.md:1660).  The fix exists, in
the copy that only explains.

**Could one flow walk produce all of them?**  Yes, and the walker exists: `uv_stmt` already carries
liveness through returns, noreturn calls, constant conditions, switches, loops and label joins.  With
no tracked variables its final `st.live` is `body_falls_off`'s answer.  It would need to carry:

- the state it has: `{live, u, i}`, join states for break, continue, switch entry and each label;
- two flags W1 has and it lacks: a live `return` was seen, a live noreturn call was seen;
- per switch: default / covered / constant facts, read from the node (Q-ccdecl-03), not walked;
- per condition: its tri-state, computed once (today `cc_eval` runs per condition per pass, 16 passes
  at most; only `uv_const` filters with `uv_maycon`, flow.c:438);
- on request, one byte per statement of the function ("entry reached, end reached, break reached"),
  indexed by node - node0.  `rp_walk` then reads bits instead of re-walking subtrees, and TODO.md:785-789
  (drop runtime-behaviour warnings in unreachable code; today `rt_flush` runs before any flow walk,
  func.c:930) gets the "reached statements" it asks for.

The linear scans W5/W6 are a different kind of pass (flow-insensitive); they share one "single
definition locals" bitset with each other, not with the flow walk.

**No visitor: 24 hand-written recursive walkers** in these files (21 `switch (x->op)`, 3 if-chains):

| Over statements (9) | For |
|---|---|
| `instrument_returns` func.c:44 | rewrite returns |
| `case_values` func.c:233, `has_default` func.c:270, `fl_case_match` flow.c:139, `fl_has_default` flow.c:168 | a switch body's own labels (four walkers, one traversal shape) |
| `stmt_flow` func.c:341, `fl_stmt` flow.c:210 | reachability |
| `rp_walk` func.c:531 | path explanation |
| `uv_stmt` flow.c:895 | uninitialized dataflow |

| Over expressions (15) | For |
|---|---|
| `fl_const` flow.c:24, `cond_value` func.c:191, `uv_maycon` flow.c:438 | constant condition (and "may be one") |
| `fl_noreturn` flow.c:75, `fl_has_noreturn` flow.c:100, `expr_noreturn` func.c:151 | noreturn |
| `uv_ex` flow.c:651, `uv_haslogic` flow.c:473 | uninitialized dataflow |
| `uv_warn` stmt.c:76, `unused_result` stmt.c:36 | unused value / result |
| `return_stack_addr` stmt.c:321 | address of a local returned |
| `array_copy_effects` init.c:750 | side effects (one more "is pure" predicate) |
| `static_init_ok` init.c:891 | constant initializer (mirror of `codegen/data.c:42`) |
| `local_constant` func.c:773 | second constant evaluator |
| `expr_tdef_raw` fmt.c:279 | typedef sugar |

Plus 20 loops that strip casts before looking at a node (45 in `frontend/c/`), in three variants.

**Lazy bodies** (lazy.c).  `lazy_ok` (29): a `static` function defined outside the main file, not
`used`/constructor/alias, no pragma pack open.  `lazy_capture` (85) copies the tokens from `{` to `}`
into `lzt`/`lzs`, or, on a header-cache hit, only remembers the entry's range (`pp_lazy_skip`); the
sym becomes `SF_DEFINED | SF_LAZY`.  After the TU `lazy_closure` (180) parses every body whose sym
is `SF_REF/USED/ADDR_TAKEN` (set by name lookup while parsing), repeating until no body names a new
one.  `lazy_parse` (137) switches the token source (`lz_on`), hides later file-scope syms (`lz_wm`,
sym.c:226-250), clears the recovery point (an error in a lazy body ends the TU) and calls the same
`function_def`.  Consequences:
- W1-W6 run unchanged, but only for bodies that are named: their cost follows the used code.
- A body nobody names is never parsed: its errors and warnings are not reported.  Verified: an
  undeclared identifier in an unused header function compiles clean (clang: error); TODO.md:1459, 1632.
- Diagnostics of lazy bodies come after all others, not in source order (verified with two warnings).
- The emitter has its own reachability (`claimed[]`, `arch/a64/codegen/bind.c:36`); it must be a subset
  of the front end's name-based closure, checked by an internal error at bind.c:38.  The emitter's
  rounds (`codegen/tu.c:794`, up to 8) only re-walk bodies that are already ASTs; nothing is parsed late.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-ccdecl-01 | L4 | redundant walk | `fl_stmt` flow.c:210-338; `stmt_flow` func.c:341-446; `uv_stmt` flow.c:895-1197; structs `flw` flow.c:10 / `flow` func.c:114; `fl_loop_body` flow.c:199 = `loop_body` func.c:329; `fl_reach_label` flow.c:129 = `goto_label` func.c:126 | every flow rule in three places; seven rules differ (section 3), one wrong warning today | one walker in flow.c (the `uv_stmt` state), `body_falls_off` = that walker with no variables; `rp_walk` reads recorded bits | L | medium: warning text and order | `vcc-diag.py`, `vcc-verify.py`, `vcc-mutate.py`, `vcc-sema.py` warn 879/879, `vbench.py gate` |
| Q-ccdecl-02 | L1 | near-duplicate | `fl_const` flow.c:24-72 = `cond_value` func.c:191-230; `fl_has_default` flow.c:168-195 = `has_default` func.c:270-296; `fl_noreturn`/`fl_has_noreturn` flow.c:75-127 ~ `expr_noreturn` func.c:151-187; `fl_case_match` flow.c:139 ~ `case_values` func.c:233 | four predicate pairs to keep in step (two already differ) | first step of 01: func.c calls flow.c's; decide each differing rule against clang | S | low | as 01 |
| Q-ccdecl-03 | L4 | redundant walk | parser's `c->cases`, `c->sw_default` (stmt.c:958-962, 980) dropped at stmt.c:1223-1224; re-derived at flow.c:139, 168, 280-285, 1086-1090 and func.c:233, 270, 299; `c->switch_cases` (ccint.h:218) is only saved and restored (diag.c:146, 249) | each consumer walks the switch body again, per pass; the emitters collect cases once more (inferred: `codegen/walk/stmt.c:509-736`) | `N_SWITCH.c` = list of its case/default nodes, `aux` = default / enum-covered / constant bits (both free today) | M | AST shape: three emitters, `replay.c:210` | `vcc-same.py`, `vcc-ctests.py`, diag suites |
| Q-ccdecl-04 | L5 | mixed file | func.c: instrumentation 4-109, reachability 111-446, path text 448-737, object-size 739-768, folding 770-870, `function_def` 872-985, `translation_unit` 989-1027; flow.c: 1-370 reachability, 372-1403 uninitialized | `-Wreturn-type` is decided in flow.c and explained in func.c; nobody looks for a constant folder in func.c | `flow.c` (walker), `uninit.c`, `retpath.c`, `instrument.c`, `bodyfold.c`; `translation_unit` next to `cc_run` | M (moves) | low | unity and file-by-file builds, `vcc-same.py` |
| Q-ccdecl-05 | L1 | near-duplicate | `object_size_locals` func.c:746-767 vs `fold_local_builtins` func.c:832-854 (clone scan: 68, 48, 47 tokens); `local_constant` func.c:773-825 builds scratch nodes and rolls `nnode`/`nipool`/`nfpool` back (861-864) | the same three scans twice; a second evaluator beside `cc_eval`; `-O` folding of `pow`/`log2` lives in the parser | one "single-definition locals" bitset; decide where `-O` folding belongs (decision 3) | M | medium: `-O1`+ output | `vcc-same.py`, `bench/zlib`, `bench/sqlite` |
| Q-ccdecl-06 | L4 | long function | `format_check` fmt.c:377-831 (455), `decl_specs` declspec.c:647-1045 (399), `attribute_list_` attr.c:320-674 (355), `declaration` decl.c:922-1252 (331), `statement` stmt.c:1024-1351 (328), `uv_stmt` flow.c:895-1197 (303) | see below: each holds 5 to 9 jobs, with blocks repeated inside | split along the jobs listed below | M each | low to medium | diag suites, `vcc-sema.py`, `vbench.py gate` (hot paths: `decl_specs`, `statement`) |
| Q-ccdecl-07 | L2 | pattern | one attribute = enum attr.c:6-13, name attr.c:15-30, case attr.c:397-639, field ccint.h:47-77, `merge_attrs` attr.c:681-720, `attrs_empty` + layout assert ccimpl.h:766-774 / attr.c:676-679, `SF_*` copy decl.c:83-92, lazy lists lazy.c:42, 203 | eight edit sites per attribute; `( const_expr )` parsed 5 times (attr.c:399, 505, 549, 582, 618), `( ident )` 3 times (470-497) | one table: name, argument shape, field, sym flag; drives parse, merge, apply | M | low | `vcc-diag.py`, `vcc-verify.py`, `vcc-same.py` |
| Q-ccdecl-08 | L7 | global state | `struct cc` ccint.h:119-290: token queue and recovery (135-153), analysis scratch `uv*`, `cnd`, `cv_*` (154-168), semantic tables (170-203), function context (216-236), pragma state (247-255); all of it is what `codegen/` includes | C++ cannot reuse the tables without the C parser's state; any analysis adds fields to the struct every module sees | split: AST and tables (public to emit), sema context, C parser state, per-analysis scratch local to its file | L | wide, mechanical | unity and file-by-file builds; `vcc-same.py` |
| Q-ccdecl-09 | L1 | near-duplicate | `static_init_ok` init.c:891-967 "the walk of the emitter's data_init without writing" (init.c:885-889), `codegen/data.c:42`; its field loop also at `arch/wasm/codegen/wstmt.c:47-54`, `arch/a64/codegen/stmt.c:85-88` (clone scan, 70 and 56 tokens) | a new initializer form must be added to the check and to the writer, or valid code is rejected | one iterator over an `N_INIT` (offset, type, leaf), used by both | M | medium | `vcc-ctests.py`, `vcc-same.py`, `test/diag` |
| Q-ccdecl-10 | L1 | pattern | decl_specs, declarator, attributes, `apply_declarator`, `ndops = first`, vector/mode: decl.c:862-875, 932-984; declarator.c:84-116, 500-514; record.c:118.  `mode()` applied at decl.c:294-299, declspec.c:1033-1038, declarator.c:98-101; `vector_size` at decl.c:300, declarator.c:113-115, 510-513 | five copies of "one declarator's type"; an attribute that changes the type must be applied in three | `declared_type(c, ds, &d)` in declarator.c | M | diagnostic order | `vcc-sema.py layout`, diag suites |
| Q-ccdecl-11 | L1 | duplicate | init.c: braced sub-list 406-415 = 564-573 = 821-828; `{ "str" }` 394-402 = 798-806; elision test 423-424 = 579-580; continuation test 456-460, 640, 673; excess warning 344-353 vs 617-625 | 5 sequences written 2-3 times in one file | local helpers | S | low | `vcc-ctests.py`, `test/diag/excess_init*` |
| Q-ccdecl-12 | L1 | duplicate | balanced skip: decl.c:5-18 = init.c:291-308 (exact), attr.c:67-85; sorted-name search: decl.c:586-606, 758-769, attr.c:137-155, 185-196, 200-212; signature loop decl.c:560-579 = 717-728 (exact); Levenshtein attr.c:217-241 and sym.c:253; cv-qualifier keyword sets declspec.c:7-11, 535-536, 543-545, 724-732, declarator.c:195-200, 320-325 | 3 + 5 + 2 + 2 + 6 copies | `skip_balanced` only; `lib/` sorted-name lookup; `cvr_bits(kw)` in token.c | S | low | unity build, diag suites |
| Q-ccdecl-13 | L5 | misplaced | library tables and clang signature decoder decl.c:489-803; `#pragma weak` decl.c:113-280; static-assert explanation and `layout_note` declspec.c:119-331; unused-value family stmt.c:30-239; `complit_text` stmt.c:243-318; `expr_loc` init.c:876; `vector_attribute` declarator.c:9; x86 `target_attr` attr.c:270-317, the same feature loop as `driver/ppdriver.c:675-695` | 1100 lines sit in a file named for something else; x86 option parsing in generic frontend/c/ and twice | homes as in section 2; `target_attr` to `lib/x86features` | M (moves) | low | builds, `vcc-same.py` |
| Q-ccdecl-14 | L4 | pattern | source text parsed again because the AST has no span: `uv_caserange` flow.c:527 (parser had it: `vloc` stmt.c:930), `int_literal` declspec.c:220, `sizeof_layouts` declspec.c:180, `complit_text` stmt.c:243, `fmt_loc2` fmt.c:99 (a second string-literal escape scanner) | five small text scanners, each with its own idea of a token | keep the span or flag on the node; one "byte of a literal to location" in lit.c | M | low | diag suites |
| Q-ccdecl-15 | L4 | redundant walk | fmt.c:581 `fmt_span` for every conversion: `fmt_loc` + `fmt_loc2`, each `pp_src` and a scan from the literal's start (fmt.c:99-155), before any warning is known | quadratic in the format string on every checked printf call (cost inferred, not measured) | map once per string, or only in `fmt_report` | S | low | `test/diag` format cases, `vbench.py gate` |
| Q-ccdecl-16 | L6 | stale doc | ccimpl.h section labels wrong for 16 prototypes: "conv.c" lists `deprecated_use`, `tag_deprecate`, `new_attrs` (decl.c), `static_init_ok`, `expr_loc` (init.c), `unused_result` (stmt.c); "diag.c" lists `unused_value`, `unused_cmp`, `set_only` (stmt.c); "fmt.c" lists `asm_label` (attr.c), `c23_word_help` (sym.c); "stmt.c" lists `lib_builtin_disabled` (decl.c); "decl.c" lists `sig_func_type`, `fold_strncmp_zero` (builtin.c), `char_sign` (conv.c), `semi_app` (token.c).  Comments detached from their function: decl.c:488, 911; declspec.c:460; declarator.c:275; stmt.c:30; fmt.c:272; ccimpl.h:750-753; ccint.h:162-168 | the header misdirects; six comments describe the wrong function | regenerate the sections from the definitions | S | none | file-by-file build |
| Q-ccdecl-17 | L10 | dead | no reader: `cc.id_main`, `id_int128_t`, `id_uint128_t`, `id_valist`, `func_name_sym`, `fn_labels`, `has_depr_tag`, `switch_cases` (ccint.h:216-263); `uwk.skr` flow.c:418; `stopped` fmt.c:385; fmt.c:151 `k == i ? 0 : 0`; attr.c:392-393 (`A_NONE` is 0), attr.c:394 `tid` = `aid`; init.c:367-368 (a loop that only breaks); decl.c:474-476 (empty `if`); flow.c:1182-1184 (`cs`); `main` by `memcmp` at func.c:936 | 8 dead fields in the struct every module includes; dead branches read as rules | delete | S | none | builds; `ccsnap.c` if it copies the struct by layout (not verified) |
| Q-ccdecl-18 | L1 | pattern | stmt.c: `if (std != 89) scope_open/close` 12 times (839-1228); duplicate-case check twice with the same text (425-463, 947-957); label record set up twice (13-18, 790-796); "value statement of a statement expression" 4 times (44-48, 153-158, 721-726, 814-822; again `arch/a64/codegen/stmt.c:249`); attribute-then-declaration prologue 3 times (736-748, 853-862, 1308-1315); `at_asm` stmt.c:645 vs attr.c:725 | small, but `statement` is long because of them | helpers | S | low | diag suites |
| Q-ccdecl-19 | L2 | pattern | realloc doubling decl.c:124-127, stmt.c:958-961, tu.c:228-231; "small buffer or calloc" flow.c:352, 1337, func.c:508, 722; string record from bytes decl.c:131-141, 684-691, mangle.c:159-164; `vintern_copy("x\0...16", n)` 12 times tu.c:96-150 beside `vintern_cstr` (tu.c:163); the 22 regions listed by index twice, tu.c:32-53 and 181-202 | private growth code; a region added in `cc_init` and forgotten in `cc_free` leaks state into the next TU | `include/common/vec.h`; `cc_str_new`; one region table | S | low | ASan build, `vcc-sema.py batch` |
| Q-ccdecl-20 | L10 | dead | `VCC_DRCOV`: tu.c:212-292, `cc.drb` fields, hook at func.c:999; referenced only by `doc/RESEARCH-preprocessor.md` | 80 lines and a per-declaration test for a measurement whose decision is taken (replay.c exists) | remove, or move beside replay.c (quality-cc-cache decides) | S | none | builds |
| Q-ccdecl-21 | L9 | error policy | diagnostics built around `diag.c` in 18 places (attr.c:281, 306, 353; decl.c:833; declspec.c:132, 265; flow.c:1237-1397; stmt.c:193, 403); `error_limit(c), c->errors++, c->ndiag++` by hand at decl.c:828-831 and declspec.c:263-264 | error counting in three places | for quality-diag.md: a `cc_diag_begin` that counts | S | low | `vcc-diag.py` |
| Q-ccdecl-22 | L8 | global state | lazy statistics, three atomic adds, written three times lazy.c:22-24, 97-99, 122-123; `getenv` per TU tu.c:303; `lz_off` racy-by-design lazy.c:34-37 while `init_tables` (tu.c:214) is the once-only place | small | read knobs in `init_tables` | S | none | `vccd-test.py` |

**Q-ccdecl-06, the jobs inside each long function.**
- `decl_specs`: (1) pending attributes; (2) the keyword loop: storage class, function specifiers,
  qualifiers, `_Atomic`, `_Alignas`, `_BitInt`, base types with clash checks, width and sign, tags
  (70 lines: lookup, body, attributes, empty-struct warning), typeof; (3) implicit-int recovery
  (`goto again`); (4) base/width/sign to a type (983-1018); (5) `_Complex`, mode, atomic, qualifiers.
  The eight-line "after a named type" prologue is repeated four times (814-820, 835-841, 849-855, 863-869).
- `declaration`: static assert; specifiers with the implicit-int fallback; then per declarator: type
  and file-scope checks; the `__auto_type` path (a second copy of declare-and-initialize, 996-1043);
  K&R; `declare`; constexpr checks (three copies: 1021-1028, 1054-1066, 1175/1210); function body
  dispatch (lazy or now); the initializer with six saved and restored context fields (1142-1158);
  static-init check; tentative and incomplete types; uninit bookkeeping; the comma/semicolon tail
  three times (1007-1010, 1037-1042, 1245-1250).  `declare` sets the same sym flags in three
  branches (decl.c:397-400, 438-441, 463-470).
- `statement`: labels; `if` with dead-code tracking; three loops with the same `in_loop`/`cnd`
  bracket; `for` with its own recovery point; `switch` with three checks; `return` with conversion
  and five diagnostics; goto; asm; typo recovery; the expression statement with the unused-value rules.
- `attribute_list_`: GNU or `[[ ]]` framing; the `[[__clang__::x]]` special case; vendor scope; the
  45-case argument switch; the unknown-attribute diagnostic with correction.
- `format_check`: find the format string (format_arg chain, `-Wformat-security`, const variable);
  **parse one conversion into `fspec`** (478-577: a parser that is not a function; the `n$` digits
  loop three times, 493-502, 518-526, 537-545); invalid conversion; positional mixing; `*` arguments;
  flag, precision and length rules; argument type against the table with the fix-it; unused arguments.
- `uv_stmt`: the dataflow transfer per statement kind, with the "sometimes" bookkeeping (pending
  ifs, do-while(0) breaks, switch arms) inlined in each case; `uwk` has about 60 fields.

**Q-ccdecl-08.**  What a C++ front end would share, and what it would not:
- *C grammar, not shared:* the keyword loop of `decl_specs`, `declarator`'s syntax, `declaration`'s
  driver, K&R and implicit int/function rules, C's redeclaration and composite-type rules in
  `declare`, designator and brace-elision syntax, `statement`'s dispatch, C89 scope rule.
- *Language-neutral, a library:* the type builders behind `apply_declarator` and the width/sign
  resolution; attributes (table, record, merge, apply); `#pragma weak`, visibility, pack;
  the library-function tables; initializer list model (`ilist`) and `static_init_ok`; switch checks;
  unused-value, return-stack-address, `-Wformat`; all of flow.c; instrumentation; static-assert
  explanation and layout notes; `mangle.c` (already Itanium, the seed of the C++ mangler); lazy
  bodies (C++ needs deferred member bodies: the same capture, with the opposite visibility rule to
  `lz_wm`); the tables and arenas of `tu.c`.
- Today the two are in the same functions and reach each other through `struct cc`.

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `uv_` | flow.c (`uv_stmt`, `uv_ex`), ccint.h (`uv`, `uvc`, `uv_any`), ccimpl.h (`uv_tracked`) **and** stmt.c (`uv_warn`, `uvw`) | uninitialized variable; in stmt.c "unused value warn" | `uninit_` and `unused_` |
| `flow` / `flw` | func.c:114 / flow.c:10 | the same walker state, twice | one `flowst` (Q-ccdecl-01) |
| `fl_`, `rp_`, `il_`, `lb_`, `mg_`, `lz_`, `dr_`, `dc_` | | per-topic prefixes; consistent inside a file | list them in STYLE.md's glossary |
| `tu.c` | frontend/c/ and codegen/ | cc: run one TU; emit: work queue and threads | `ccrun.c` |
| `uv_any` bits 1, 2, 4, 8 | ccint.h:154; tested as `& 14`, `& 1`, set with 8 (func.c:947, stmt.c:967) | why the uninit walk must run | named enum |
| `skind` 0-7 | flow.c:415, 424; set at 500, 1011, 1040, 1104 | which edge a "sometimes" report names | named enum (comment lists 0-5, code uses 6 and 7) |
| `dop.kind` 0/1/2; `dop.np` | ccimpl.h:106-114 | pointer/array/function; `np` is parameter-sym count for a function, a source location for an array | enum; two fields |
| `N_BLOCK` `aux == 1`, `N_DECL` `aux == 1`, `N_INIT` `aux` 0/1/2 | stmt.c:763, 874, 1004, 1020; decl.c:1034; init.c:703-708 and every reader | flat label list; inferred bounds; struct/array/union | named constants in cc.h |
| `attrs.wur` 1/2, `attrs.pure` 1/2, `attrs.fmt` packed `1<<16 \| i<<8 \| j` | ccint.h:58-60, 72 | which spelling; pure or const; three fields in one | enums; three fields |
| `dcm dcif dcf dcn dcl dclf sif sfalse sdep dl dlf ndl swc swa nswc spend sdone` | `uwk`, flow.c:382-435 | "sometimes-uninitialized" candidates per variable | a `struct candidate` per kind |
| `c->cnd`, `c->ibr`, `c->sie`, `c->dms`, `c->dfx`, `c->bcp` | ccint.h | branch depth; open initializer braces; "static_init_ok looks through errors"; pending diagnostic text / fix-it; in `__builtin_constant_p` | spelled out |
| `T/KIND/N` and `CT/CKIND/CN` | ccimpl.h:55-66, ccint.h:354-356 | the same three macros for frontend/c/ and for codegen/ | one set |
| `declare` vs `declaration` vs `decl_specs` vs `declarator` vs `declr` vs `dspec` | decl.c, declspec.c, declarator.c | bind one name; parse one declaration; ...; the two result structs | `bind_decl` for `declare` |
| `cond_of`, `cond_value`, `fl_const`, `uv_const`, `scalar_cond` | stmt.c:507, func.c:191, flow.c:24, 467, complex.c | parse a condition; its constant value (three names) | `cond_const` once |

Word with two meanings: "flow" (flow.c the file; `struct flow` in func.c), "tu" (two files), "uv".

## 6. Decisions needed

1. **One flow walk** (Q-ccdecl-01, PLAN-quality decision 4).  Options: (a) keep two walkers, share only
   the predicates (S, removes the drift in four of seven rules); (b) one walker with the uninit state,
   recorded reach bits for the path text and for TODO.md:785.  Recommend (a) now as the proof step,
   (b) as the target.  Each of the seven differing rules needs a clang check first: which copy is right.
2. **Switch facts on the node** (Q-ccdecl-03): changes the AST the three emitters and `replay.c` read.
   Recommend yes, together with the shared walker's switch lowering, so the emitters' case
   collection goes at the same time.
3. **Does the front end optimize?**  `fold_local_builtins` (fast-math `pow`/`log2`, `-O1`+ only) and
   `object_size_locals` are optimizer facts computed in the parser module.  Options: stay in frontend/c/ as a
   named pass file; move to `codegen/prepass.c`; leave to VIR at `-O2`.  Needs: which outputs depend on
   them today (`vcc-same.py` with the pass off).
4. **`struct cc` split** (Q-ccdecl-08) before C++ starts, or `ccint.h` stays the one header.  Recommend
   the split as its own workstream after the file moves: AST and tables first (what codegen/ may see).
5. **Lazy bodies and diagnostics**: unreferenced header functions are not checked and lazy
   diagnostics are out of source order.  Options: document as vcc's rule; parse eagerly when not in
   a system header and a warning could fire (TODO.md:1632); sort diagnostics by location at the end.
6. **`VCC_DRCOV`** (Q-ccdecl-20): delete or keep as a debug knob.

## 7. Not covered

- `frontend/c/record.c`, `sym.c`, `diag.c`, `token.c`, `expr*.c`, `conv.c`, `const.c`, `builtin.c`, `replay.c`:
  not read; lines cited from them come from grep or the clone scan (other reviews own them).
- The emitter side of every cross-module duplicate (codegen/data.c, codegen/walk/stmt.c, arch/*/emit) was
  seen as excerpts only: "inferred" where marked.
- No instruction counts: how much W1-W6 and `fmt_span` cost is for quality-passes.md.
- Clang's behaviour was checked for one of the seven differing flow rules (enum-covered switch).
- Whether `driver/ccsnap.c` or `frontend/c/replay.c` depend on the layout of `struct cc` (matters for
  Q-ccdecl-08 and -17) was not verified.
- The generated tables (`attrnames*.inc`, `fmttab.inc`, `lib*.inc`) and their generators were not read.
