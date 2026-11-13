# Work order: C front end, declarations, statements, flow

The second of three orders for `src/frontend/c/` (with `WORK-cc-expr.md` and
`WORK-cc-cache.md`).  The evidence is `doc/review/quality-cc-decl.md`
(finding ids `Q-ccdecl-NN`), with `doc/review/quality-diag.md` (`Q-diag-NN`),
`doc/review/quality-boundaries.md` (`Q-BND-NN`) and
`doc/review/quality-lib.md` (`Q-lib-NN`).  Names follow `doc/STYLE.md`; the
"Decisions" table of `doc/PLAN-quality.md` is binding.

Line numbers are today's (main at d602dfa2; the review's numbers still hold
for these files).  If a line has moved, find the same text.

**Starts when** WORK-common steps 1 to 3 are on main.  Step 1 may run at
once (in parallel with the other cc orders' step 1); step 2 waits for
WORK-cc-expr step 2 (the renames run one at a time, and this table uses the
names that one gives the shared helpers); step 9 waits for WORK-diag step 4.

**Owns** (16 files, and every file they are split into)
- `src/frontend/c/`: `decl.c`, `declspec.c`, `declarator.c`, `attr.c`,
  `init.c`, `stmt.c`, `func.c`, `flow.c`, `lazy.c`, `tu.c`, `mangle.c`,
  `fmt.c`, `diag.c`, and the generated tables `attrnames.inc`,
  `attrnames-x86.inc`, `fmttab.inc` (their generators,
  `src/tools/gen-attrnames.sh`, `gen-x86data.sh`, `gen-fmttab.py`, change
  only if a table's file name changes).
- Not `decl.c` 489-851 once step 1 has cut it into `library-function.c`:
  that file, like the `lib*.inc` tables it includes, is WORK-cc-cache's
  from the merge of step 1 on.  This order cuts it so that one order edits
  `decl.c` at a time.

The shared internal header `src/frontend/c/ccimpl.h`, `cc.h` and `ccint.h`
are WORK-cc-expr's.  This order edits `ccimpl.h` only by script (step 2)
and, in step 1, to add the prototypes of functions its moves turn from
`static` into `VPRIV`, in the section of the file they came from; the
section regeneration is WORK-cc-expr step 3.  Fields of `struct cc` that
this order's steps add, remove or rename are a request to WORK-cc-expr,
listed in the PR.

It edits other modules and the other cc orders' files only by script, in
step 2, to follow a rename.

## Rules shared by the three cc orders

As in `WORK-cc-expr.md` ("Rules shared by the three cc orders"):
`replay.c` is frozen and the rename rows it would need go to
`doc/quality/rename-cc-deferred.tsv` (of this order's names today: `diag`,
`rec`, `instrument`, `lazy_replayed`, `halloc`, and the types `attrs`,
`lzfn`; recompute at the time); the `struct cc` / `ast.h` split is not in
these orders; the rename steps run one at a time and a conflicting
scripted PR is regenerated on current main, never rebased; every step
builds with `make VCC_UNITY=`.

## What done looks like

- One job per file in the table of step 1, each with an opening comment;
  `function_def` reads as the list of checks it runs, each in its own file.
- Every file-scope function of these files is `cc_` + noun + verb; the
  per-topic prefixes (`fl_`, `uv_`, `rp_`, `lz_`, `mg_`, `il_`, `lb_`, `dc_`)
  are gone; `uv_` no longer means two things.
- The two reachability walkers share their predicates, and where they
  disagreed each rule is clang's (decision 1 (a) of the review).
- One table per attribute drives parse, merge and apply.
- Front-end reporting goes through the diag module's one record API (after
  WORK-diag step 4) and counts errors in one place.
- Output byte-identical in every step except those named.

## Steps

Each step is one PR.  Proof for every step unless it says otherwise:

```
nice -n 10 make -C src -j 3 OUT=$PWD/build/w vcc                      # unity
nice -n 10 make -C src -j 3 VCC_UNITY= OUT=$PWD/build/w-parts vcc      # file by file
VCC_BIN=$PWD/build/w bench/vcc-same.py --base DIR -j 3   # DIR: bench/vbench.py mkbase <branch point>
bench/vcc-sema.py batch -j 3 --bin-dir build/w           # 878/879
bench/vcc-sema.py run -j 3 --bin-dir build/w             # 878/879 (needs work/: else say so)
bench/vcc-diag.py -j 3 --bin-dir build/w                 # snapshots unchanged
bench/vcc-ctests.py -j 3 --suite vcc                     # VCC_BIN=$PWD/build/w
```
`bench/vcc-same.py` runs with `-w`: it does not compare diagnostics, so any
step touching a check also runs `bench/vcc-diag.py` and
`bench/vcc-verify.py -j 3`.  `bench/vbench.py gate` where a step says so:
it needs a quiet machine; the PR says "Proof not run: needs a quiet
machine" and the integrator runs it.

### 1. Split (moves only)

`git mv` each file to its new name first, then cut the ranges out.  A
`static` function separated from its callers becomes `VPRIV` with a
prototype in `ccimpl.h`.

| From | To | What | Why |
|---|---|---|---|
| `decl.c` | `declaration.c` | `declaration`, `declare`, attributes onto syms, K&R parameters | name says the job |
| `decl.c` 113-280 | `pragma-weak.c` | `weak_push` ... `weak_finish` (`#pragma weak`) | Q-ccdecl-13 |
| `decl.c` 488-851 | `library-function.c` | the `libbuiltins.inc` / `libfuncs.inc` / `libnames.inc` tables, clang's signature decoder `lb_type`/`lb_functype`, `lib_builtin*`, `implicit_lib_*`, `implicit_func`, `lib_header`, `include_fix`, `lib_func` | Q-ccdecl-13; owned by WORK-cc-cache from then on (Q-cc-cache-11) |
| `declspec.c` | `declaration-specifiers.c` | `decl_specs`, tag lookup, `typeof` | |
| `declspec.c` 119-331 | `static-assert.c` | `layout_note`, `sizeof_layouts`, `int_literal`, `failed_term`, `static_assert_failed`, `static_assert_decl` | Q-ccdecl-13 |
| `declarator.c` | stays | | |
| `attr.c` | `attribute.c` | table, `attribute_list_`, merge, typo correction, asm labels | |
| `attr.c` 270-317 | `x86-target-attribute.c` | `target_attr` (`target("...")` parsing) | x86 option parsing in a generic file (Q-ccdecl-13); its twin `driver/ppdriver.c:675-695` is the driver order's, and a shared `x86-features` helper is not WORK-common's: noted under "Not in this order" |
| `init.c` | `initializer.c` | | |
| `stmt.c` | `statement.c` | statements, asm statements, switch checks | |
| `stmt.c` 30-239 | `unused-value.c` | `unused_result`, `vol`, `uv_warn`, `unused_cmp`, `set_only`, `unused_value` | Q-ccdecl-13; frees the prefix `uv_` for `uninit` |
| `stmt.c` 241-388 | `return-stack-address.c` | `complit_text`, `return_stack_addr` | Q-ccdecl-13 |
| `func.c` | `function-definition.c` | `function_def` (872-985) | Q-ccdecl-04 |
| `func.c` 4-109 | `instrument-functions.c` | `-finstrument-functions` | Q-ccdecl-04 |
| `func.c` 111-446 | `return-path.c` | `stmt_flow` and its predicates (the walker that explains) | Q-ccdecl-04; joined to the explanation, not to `reach.c`, until step 5 |
| `func.c` 448-737 | `return-path.c` | `rp_*`, `return_path`, `return_type_warn` | Q-ccdecl-04 |
| `func.c` 739-870 | `function-body-fold.c` | `object_size_locals`, `local_constant`, `fold_local_builtins` | Q-ccdecl-04; where it finally belongs is review decision 3, not this order |
| `func.c` 987-1027 | `translation-unit.c` | `translation_unit` | beside `cc_run` (Q-ccdecl-04) |
| `flow.c` 1-370 | `reach.c` | `body_falls_off` and the `fl_*` walker | STYLE glossary: "flow" is data flow over machine records; front-end reachability is "reach" |
| `flow.c` 372-1403 | `uninitialized.c` | `-Wuninitialized` dataflow and its reports | Q-ccdecl-04 |
| `tu.c` | `translation-unit.c` | `halloc`, `cc_init`, `cc_free`, `init_tables`, `cc_run` | `codegen/tu.c` is a work queue: same name, other job (review section 5) |
| `tu.c` 222-292 | `declaration-coverage.c` | `VCC_DRCOV` (`drcov_*`, `cc_drcov_stats`) | its fate is decided with `replay.c` (Q-ccdecl-20); until then it is out of the way |
| `lazy.c` | `lazy-body.c` | | |
| `mangle.c` | stays | | |
| `fmt.c` | `format-check.c` | `-Wformat` and typedef sugar | |
| `diag.c` | `report.c` | `diag`, `cc_error`, `fatal`, `report`, `note`, the flag entries | STYLE glossary: diag's verb is "report" |
| `diag.c` 71-137, 139-285 | `recovery.c` | `parse_jump`, `expect_match_fail`, `skip_close`, `delayed_bug*`, `rec_save`, `skip_nest`, `skip_stmt`, `skip_semi`, `skip_for_head`, `stray_rbrace`, `rec_restore` | parser recovery is not reporting |
| `diag.c` 570-576 | `identifier.c` | `idname`, `idlen_`, `idstr_` | not diagnostics |

`declarator.c`, `mangle.c` and the three generated tables keep their names.

Proof adds: `git diff -M --stat` shows renames and cut files only;
`bench/quality.py --clones 999` reports no new clone.

### 2. Rename (scripted)

Table `doc/quality/rename-cc-decl.tsv`, applied with
`src/tools/rename-table.py` (with WORK-cc-expr's path column for names also
defined outside `frontend/c/`).  The designer checks it against STYLE.md
before it is applied.  Rows hit by the replay rule go to
`rename-cc-deferred.tsv`.  No new name may exist already (`git grep -w`).

| Old | New |
|---|---|
| every file-scope function of the owned files | `cc_` + noun + verb, the noun from its file: `cc_declaration_`, `cc_declaration_specifiers_`, `cc_declarator_`, `cc_attribute_`, `cc_initializer_`, `cc_statement_`, `cc_function_`, `cc_return_path_`, `cc_reach_`, `cc_uninitialized_`, `cc_unused_`, `cc_static_assert_`, `cc_pragma_weak_`, `cc_lazy_body_`, `cc_mangle_`, `cc_format_check_`, `cc_recovery_`, `cc_report_`, `cc_translation_unit_` |
| `fl_*`, `flw` | `cc_reach_*`, `cc_reach_state` |
| `uv_*`, `uwk`, `ust`, `uj` (flow.c) | `cc_uninitialized_*`, `cc_uninitialized_walk`, `cc_uninitialized_state`, `cc_uninitialized_join` |
| `uv_warn`, `uvw` (stmt.c) | `cc_unused_value_walk`, `cc_unused_value_state` |
| `rp_*`, `rpath`, `rstep_cmp`, `flow` (func.c's struct) | `cc_return_path_*`, `cc_return_path`, `cc_return_path_step_compare`, `cc_return_path_reach_state` |
| `stmt_flow`, `cond_value`, `has_default`, `case_values`, `expr_noreturn`, `enum_covered` | `cc_return_path_reach`, `cc_condition_value`, `cc_switch_has_default`, `cc_switch_case_values`, `cc_expression_is_noreturn`, `cc_switch_is_enum_covered` |
| `body_falls_off`, `uninit_check`, `return_type_warn` | `cc_reach_body_falls_off`, `cc_uninitialized_check`, `cc_return_type_check` |
| `declaration`, `declare`, `decl_specs`, `declarator`, `apply_declarator`, `type_name` | `cc_declaration_parse`, `cc_declaration_bind` (review: `bind_decl`), `cc_declaration_specifiers_parse`, `cc_declarator_parse`, `cc_declarator_apply`, `cc_type_name_parse` |
| types `dspec`, `declr`, `dop` | `cc_specifiers`, `cc_declarator`, `cc_declarator_op` |
| `statement`, `compound_stmt`, `labeled_statement`, `asm_stmt`, `mkstmt`, `label_ref` | `cc_statement_parse`, `cc_compound_statement_parse`, `cc_labeled_statement_parse`, `cc_asm_statement_parse`, `cc_statement_new`, `cc_label_reference` |
| `attribute_list_`, `merge_attrs`, `new_attrs`, `apply_sym_attrs`, `init_attrs`, `asm_label`, `skip_balanced` | `cc_attribute_list_parse`, `cc_attribute_merge`, `cc_attribute_new`, `cc_attribute_apply_symbol`, `cc_attribute_table_init`, `cc_asm_label_parse`, `cc_token_skip_balanced` |
| `function_def`, `translation_unit`, `instrument`, `fold_local_builtins`, `object_size_locals` | `cc_function_definition_parse`, `cc_translation_unit_parse`, `cc_instrument_functions` (deferred: `instrument`), `cc_function_body_fold`, `cc_function_object_size_locals` |
| `lazy_ok`, `lazy_capture`, `lazy_parse`, `lazy_closure`, `lz_*` | `cc_lazy_body_eligible`, `cc_lazy_body_capture`, `cc_lazy_body_parse`, `cc_lazy_body_closure`, `cc_lazy_body_*` (`lazy_replayed` deferred) |
| `format_check`, `fmt_*`, `expr_tdef*` | `cc_format_check`, `cc_format_*`, `cc_typedef_sugar*` |
| `mg_*`, `name_overload` | `cc_mangle_*`, `cc_mangle_overload` |
| `il_*`, `ilist` | `cc_initializer_list_*`, `cc_initializer_list` |
| `pragma_weak`, `weak_*` | `cc_pragma_weak_parse`, `cc_pragma_weak_*` |
| `lb_*`, `lib_*`, `libbi*`, `libfn*`, `implicit_*` | stay: `library-function.c` is WORK-cc-cache's after step 1, and its step 2 renames them |
| `cc_error`, `fatal`, `note`, `report`, `fwd_note`, `cc_warnx`, `cc_errorx`, `cc_warn_begin`, `ext_on_slow`, `ext_warn`, `rt_defer`, `rt_emit`, `rt_flush`, `error_limit`, `delayed_bug`, `delayed_bug_check`, `punct_spell` | `cc_report_error`, `cc_report_fatal`, `cc_report_note`, `cc_report`, `cc_report_forward_note`, `cc_report_warning`, `cc_report_flag_error`, `cc_report_warning_begin`, `cc_extension_enabled_slow`, `cc_report_extension`, `cc_report_runtime_defer`, `cc_report_runtime`, `cc_report_runtime_flush`, `cc_report_error_limit`, `cc_report_delayed_bug`, `cc_report_delayed_bug_check`, `cc_punctuator_spelling` (`diag`, `rec` deferred) |
| `rec_save`, `rec_restore`, `recov`, `skip_stmt`, `skip_semi`, `skip_close`, `parse_jump`, `expect_match_fail` | `cc_recovery_save`, `cc_recovery_restore`, `cc_recovery_point`, `cc_recovery_skip_statement`, `cc_recovery_skip_semicolon`, `cc_recovery_skip_close`, `cc_recovery_jump`, `cc_recovery_expect_match_fail` |
| `idname`, `idlen_`, `idstr_` | `cc_identifier_name`, `cc_identifier_length`, `cc_identifier_string` |
| `cc_init`, `cc_free`, `init_tables` | `cc_translation_unit_init`, `cc_translation_unit_free`, `cc_tables_init` (`halloc` deferred) |
| `drcov_*`, `dr_tok` ... `dr_dc`, `drb_has` | stay: decided with `replay.c` |
| `uv_any` bits 1, 2, 4, 8; `skind` 0-7; `dop.kind` 0/1/2; `attrs.wur`, `attrs.pure` values | named enums `CC_UNINITIALIZED_*`, `CC_SOMETIMES_*`, `CC_DECLARATOR_OP_*`, `CC_UNUSED_RESULT_*`, `CC_PURE_*` (the enum goes beside the field; for fields of `cc.h`/`ccint.h` it is WORK-cc-expr's file, so the enum and the comment are in the same PR, with that order's designer as reviewer) |
| `struct cc` fields `cnd`, `ibr`, `sie`, `dms`, `dfx`, `uv`, `uvc`, `uv_any`, `sw_default`, `cases` | `branch_depth`, `open_braces`, `static_init_through_errors`, `pending_diagnostic_text`, `pending_fixit`, `uninitialized`, `uninitialized_count`, `uninitialized_reasons`, `switch_default`, `switch_cases_open` (each only if `replay.c` does not use it; check with the script) |

The `N_BLOCK`/`N_DECL`/`N_INIT` `aux` values (review section 5) are AST
shape: the split order's.

Proof adds: `git grep -w` for every applied old name returns nothing outside
`doc/` and `rename-cc-deferred.tsv`; `replay.c` unchanged.

### 3. Comments and dead code (identical output)

- An opening comment on every owned file.  `function-definition.c`'s says
  in which order the checks of a finished body run and when (the table of
  the review's section 3, W1-W6), and that no check runs after an error in
  the body.  The module-level invariants (review section 2) go into
  `cc.h`'s comment in WORK-cc-expr step 3.
- Comments detached from their function (Q-ccdecl-16): `decl.c:488`
  (belongs to `implicit_func`), `:911`, `declspec.c:460`, `declarator.c:275`,
  `stmt.c:30`, `fmt.c:272`; the stale opening comments of `func.c` ("-pg")
  and `flow.c` (describes lines 1-370 only).
- Dead code (Q-ccdecl-17): `uwk.skr` (`flow.c:418`), `stopped`
  (`fmt.c:385`), `fmt.c:151` `k == i ? 0 : 0`, `attr.c:392-393` (`A_NONE` is
  0), `attr.c:394` `tid` (= `aid`), `init.c:367-368` (a loop that only
  breaks), `decl.c:474-476` (empty `if`), `flow.c:1182-1184` (`cs`).  The
  dead `struct cc` fields are WORK-cc-expr step 3.
- `main` found by `memcmp` at `func.c:936`: compare the interned id
  (`c->id_main` is dead today; WORK-cc-expr deletes it, so intern "main"
  once in `cc_tables_init` instead).

### 4. Small duplicates (identical output)

- `skip_balanced` once (Q-ccdecl-12): `decl.c:5-18` = `init.c:291-308`
  (exact), `attr.c:67-85`.
- The signature loop `decl.c:560-579` = `:717-728` (exact): one function in
  `library-function.c`.  This is WORK-cc-cache's file after step 1, so this
  bullet is done there (its step 4) and listed here only to avoid doing it
  twice.
- `cvr_bits(kw)` for the six cv-qualifier keyword sets (`declspec.c:7-11`,
  `:535-536`, `:543-545`, `:724-732`, `declarator.c:195-200`, `:320-325`);
  it goes in `declaration-specifiers.c`, not `token.c` (WORK-cc-expr's).
- Levenshtein: `attr.c:217-241` calls `cc_edit_distance`
  (`typo-correction.c`, WORK-cc-expr).
- `initializer.c` (Q-ccdecl-11): braced sub-list `init.c:406-415` =
  `:564-573` = `:821-828`; `{ "str" }` `:394-402` = `:798-806`; elision test
  `:423-424` = `:579-580`; continuation test `:456-460`, `:640`, `:673`;
  excess-initializer warning `:344-353` against `:617-625` (keep each text).
- `statement.c` (Q-ccdecl-18): `scope_open/close` under `std != 89` twelve
  times (839-1228) into `cc_c89_scope_open/close`; the duplicate-case check
  (425-463 and 947-957, same text) once; the label record (13-18, 790-796)
  once; "value statement of a statement expression" (44-48, 153-158,
  721-726, 814-822) once (the copy in `arch/a64/codegen/stmt.c:249` is the
  code-generation order's); the attribute-then-declaration prologue
  (736-748, 853-862, 1308-1315) once; `at_asm` (`stmt.c:645`) against
  `attr.c:725`.
- `declaration-specifiers.c`: the eight-line "after a named type" prologue
  (`declspec.c:814-820`, `835-841`, `849-855`, `863-869`).
- `declaration.c`: the comma/semicolon tail (`decl.c:1007-1010`,
  `1037-1042`, `1245-1250`); the sym flags set in three branches of
  `declare` (397-400, 438-441, 463-470); the three constexpr checks
  (1021-1028, 1054-1066, 1175/1210).
- `lazy-body.c`: the three statistics adds (`lazy.c:22-24`, `97-99`,
  `122-123`) once.
- `translation-unit.c` (Q-ccdecl-19): the 22 regions listed twice
  (`tu.c:32-53`, `181-202`) become one table, so a region added to init is
  also freed; the twelve `vintern_copy("x\0...16", n)` (96-150) become
  `vintern_cstr`; string record from bytes (`decl.c:131-141`, `684-691`,
  `mangle.c:159-164`) one `cc_string_new` (the `decl.c:684-691` site is in
  `library-function.c`: call it there by script in this PR, the one edit to
  WORK-cc-cache's file).

Proof adds: `bench/vcc-verify.py -j 3`, `bench/vcc-mutate.py -j 3`,
`make -C src vcc-asan` then `bench/vcc-sema.py batch -j 3 --bin-dir
build/src-asan` (the region table).

### 5. Shared reachability predicates (changes diagnostics; Q-ccdecl-02, review decision 1 (a))

`return-path.c` calls `reach.c`'s predicates instead of its copies:
`cond_value` (`func.c:191-230`) -> `fl_const` (`flow.c:24-72`),
`has_default` (`func.c:270-296`) -> `fl_has_default` (`flow.c:168-195`),
`expr_noreturn` (`func.c:151-187`) -> `fl_noreturn`/`fl_has_noreturn`
(`flow.c:75-127`), `case_values` (`func.c:233`) -> `fl_case_match`
(`flow.c:139`), `loop_body` (`func.c:329`) = `fl_loop_body` (`flow.c:199`),
`goto_label` (`func.c:126`) = `fl_reach_label` (`flow.c:129`); one state
struct.

Before the PR: for each of the seven rules where the walkers differ (the
review's table in section 3), a test file in `src/test/diag/` and clang's
answer for it; the shared predicate implements clang's.  The enum-covered
switch is already fixed on main (PR #137: `flow.c:283` calls
`enum_covered`), so its test exists; six remain: constant switch condition,
computed goto, noreturn call inside an expression, statement expression,
`volatile` constant condition and casts, declaration initializer.  The PR
lists each rule with clang's answer and which copy was right.

Proof adds: `bench/vcc-verify.py -j 3`, `bench/vcc-mutate.py -j 3`,
`bench/vcc-sema.py warn -j 3` (879/879), `bench/vcc-diag.py --bless` only
for the snapshots this changes (each named in the PR), `bench/vbench.py
gate` (quiet machine).

### 6. Long functions by job (identical output; Q-ccdecl-06)

Each becomes a short driver over named functions in the same file, along
the jobs the review lists: `format_check` (`fmt.c:377-831`: the conversion
parser 478-577 becomes `cc_format_conversion_parse` returning an `fspec`,
and the `n$` digit loop 493-502 = 518-526 = 537-545 once), `decl_specs`
(`declspec.c:647-1045`), `attribute_list_` (`attr.c:320-674`, before step 7
reshapes it: only the framing and the unknown-attribute path are cut out
here), `declaration` (`decl.c:922-1252`: the `__auto_type` path 996-1043,
the initializer with its six saved fields 1142-1158), `statement`
(`stmt.c:1024-1351`: one function per statement keyword), `uv_stmt`
(`flow.c:895-1197`: one function per statement kind; the "sometimes"
bookkeeping in `cc_uninitialized_sometimes_*`).  No behaviour change: the
order of every check stays.

Proof adds: `bench/vcc-verify.py -j 3`, `bench/vcc-mutate.py -j 3`,
`bench/vbench.py gate` (quiet machine: `decl_specs` and `statement` are
hot).

### 7. One attribute table (identical output; Q-ccdecl-07)

A table in `attribute.c`, one row per attribute: name (from
`attrnames.inc`), argument shape (none, `( const_expr )`, `( ident )`,
`( string )`, ...), the `attrs` field it sets, the sym flag it copies
(`decl.c:83-92`), and whether lazy bodies must see it (`lazy.c:42`, `203`).
`attribute_list_`'s 45-case switch (`attr.c:397-639`), `merge_attrs`
(`attr.c:681-720`) and the `SF_*` copy are driven by it; the five
`( const_expr )` parsers (`attr.c:399`, `505`, `549`, `582`, `618`) and three
`( ident )` parsers (470-497) become one per shape.  The `attrs` struct
(`ccint.h:47-77`) and `attrs_empty` (`ccimpl.h:766-774`) stay where they are
(WORK-cc-expr's files); the PR adds a `_Static_assert` beside the table that
every field of `attrs` has a row.

Proof adds: `bench/vcc-verify.py -j 3`, `bench/vcc-mutate.py -j 3`,
`bench/vcc-sema.py layout -j 3`.

### 8. One declarator type (diagnostic order may change; Q-ccdecl-10)

`cc_declared_type(c, ds, &d)` in `declarator.c`: specifiers, declarator,
attributes, `apply_declarator`, `mode()`, `vector_size`.  It replaces the
five copies (`decl.c:862-875`, `932-984`; `declarator.c:84-116`, `500-514`;
`record.c:118`, the last by script in WORK-cc-expr's `record-parse.c`) and
the three applications of `mode()` (`decl.c:294-299`,
`declspec.c:1033-1038`, `declarator.c:98-101`) and of `vector_size`
(`decl.c:300`, `declarator.c:113-115`, `510-513`).  Where the copies
report in a different order today, keep clang's order (run clang on each
test of `src/test/diag` the PR changes; list them).

Proof adds: `bench/vcc-sema.py layout -j 3`, `bench/vcc-bflayout.py`,
`bench/vcc-verify.py -j 3`, `bench/vcc-mutate.py -j 3`.

### 9. Reporting through the diag module (identical diagnostics; Q-ccdecl-21, Q-diag-08, Q-diag-15)

After WORK-diag step 4.  The six "report a warning of a flag" entries
(`diag.c:393-412` `cc_warnx`, `416-433` `cc_errorx`, `437-457`
`cc_warn_begin`, `521-542` `ext_warn`, `546-568` `rt_defer`, and pp's
`pp_warnx` on the diag side) become wrappers of one
`cc_report_warning_begin(c, flag, on, groups, loc, fmt, ap)` that returns
the record or NULL and drops a pending fix-it (`c->dfx`) when it returns
NULL (fixes the "Bugs" item below if a test shows it).  The 18 places that
build records around `diag.c` (`attr.c:281`, `306`, `353`; `decl.c:833`;
`declspec.c:132`, `265`; `flow.c:1237-1397`; `stmt.c:193`, `403`) and the
hand-made `error_limit(c), c->errors++, c->ndiag++` (`decl.c:828-831`,
`declspec.c:263-264`) use the diag module's builder; errors are counted by
the engine (`diag_error_count`).  `c->errors` stays as a field, set from
the engine's count, because `replay.c` reads it (frozen).

Proof adds: `bench/vcc-diag.py -j 3`, `bench/vcc-mutate.py -j 3 --formats`,
`bench/vcc-verify.py -j 3`, `bench/vcc-sema.py warn -j 3`,
`bench/vcc-stress.py` and `--vccd` (error limits).

### 10. Format strings mapped once (identical output; Q-ccdecl-15)

`fmt_span` (`fmt.c:581`) maps every conversion to a location before any
warning is known: `fmt_loc` + `fmt_loc2`, each a `pp_src` and a scan from
the literal's start (`fmt.c:99-155`).  Map only when a warning is reported
(in `fmt_report`), or once per string.

Proof adds: `bench/vcc-diag.py -j 3` (the format cases),
`bench/vbench.py gate` (quiet machine; the PR gives the instruction count
on a TU with a long `printf` chain, before and after).

### 11. Single-definition locals once (identical output; Q-ccdecl-05)

`object_size_locals` (`func.c:746-767`) and `fold_local_builtins`
(`func.c:832-854`) compute the same "locals never assigned after their
initializer" and "the function has a label, goto or asm" with the same
three scans: one bitset, computed once when either needs it.
`local_constant` (`func.c:773-825`, a second constant evaluator) stays
until review decision 3 is taken.

Proof adds: `bench/zlib/run.py all` and `bench/sqlite/run.py all` (macOS;
`-O1`+ output), or "Proof not run: needs macOS".

### 12. Knobs and out of memory (failure paths only; Q-ccdecl-22, Q-ccdecl-19)

`getenv` per TU (`tu.c:303`) moves into `cc_tables_init` (once); `lz_off`
(`lazy.c:34-37`) is read there too.  `realloc` doubling (`decl.c:124-127`,
`stmt.c:958-961`), the "small buffer or calloc" sites (`flow.c:352`,
`1337`, `func.c:508`, `722`), `func.c:840`, `attr.c:53` and `tu.c:297` fail
through `vout_of_memory()` until the adoption step replaces them.

Proof adds: `bench/vccd-test.py`, `bench/vcc-stress.py --vccd`.

## Not in this order

- One reachability walker for all three uses, with recorded reach bits for
  the path text and for TODO.md:785-789 (Q-ccdecl-01, review decision 1
  (b)): after step 5, its own order with a measurement.
- Switch facts on the `N_SWITCH` node (Q-ccdecl-03): AST shape, read by
  three code generators and `replay.c`; with the AST split order and the
  shared walker's switch lowering.
- Whether the front end optimizes (`fold_local_builtins`, `local_constant`,
  `object_size_locals`; review decision 3): needs `vcc-same.py` with the
  pass off.
- The `struct cc` / `ast.h` split (Q-ccdecl-08): its own order later, on
  `MEASURE-ast-split.md` (embed `struct ast` first in `struct cc`).
- `replay.c`, `VCC_DRCOV` (Q-ccdecl-20: moved aside in step 1, deleted or
  kept with the replay decision), and the deferred rename rows.
- One `N_INIT` iterator for `static_init_ok` and the data writers
  (Q-ccdecl-09): spans `codegen/data.c`, `arch/*/codegen`.
- Source text parsed again because a node has no span (Q-ccdecl-14:
  `uv_caserange`, `int_literal`, `sizeof_layouts`, `complit_text`,
  `fmt_loc2`): needs a span or flag on the node (AST split) and one "byte
  of a literal to location" in `literal.c` (WORK-cc-expr).
- Lazy bodies' diagnostics (review decision 5): unreferenced header
  functions are not checked (TODO.md:1459, 1632) and their diagnostics come
  last; a decision, then its own PR.
- `target_attr` and `driver/ppdriver.c:675-695` as one x86 feature parser
  (Q-ccdecl-13): needs a home outside both modules; not a WORK-common
  facility.
- `library-function.c`'s signature decoder and tables: WORK-cc-cache.

## Bugs, not in this order

Each needs a test against clang and a fix in its own PR.  Checked on main
at d602dfa2.
- Fixed by PR #137: the enum-covered switch's wrong `-Wreturn-type`
  (`flow.c:283` now calls `enum_covered`).
- A pending fix-it (`c->dfx`) survives a warning that is switched off in
  four of the five entries of `diag.c` and may attach to the next
  diagnostic (Q-diag-08; inferred, not reproduced).
- The six remaining reachability rules where the two walkers differ (step
  5): any of them that gives a wrong warning against clang today is a bug
  in its own PR before step 5.

## Report

Per step: the PR, the proof commands with their results (numbers), anything
this order got wrong.  The designer checks the result against STYLE.md and
"What done looks like", not the diff line by line.

## Adoption of common/ facilities

Only after WORK-common step 5 has merged; one PR, identical output (common
proof plus `make -C src vcc-asan` and `bench/vcc-sema.py batch -j 3
--bin-dir build/src-asan`).  Old file names; find them in the step 1 files.

| File | Site | Count | Facility |
|---|---|---|---|
| `decl.c` | `realloc` doubling of the `#pragma weak` list 124-127 | 1 | `VVECTOR_APPEND` (`common/vector.h`) |
| `stmt.c` | `realloc` doubling of `c->cases` 958-961 | 1 | `VVECTOR_APPEND` |
| `tu.c` | `realloc` doubling of `c->drb` 230 (DRCOV, in `declaration-coverage.c` after step 1: decided with replay) | 1 | stays until the replay decision |
| `tu.c` | `malloc(sizeof(cc))` 297 | 1 | stays (one per job, pooled by `vmpool`); fails through `vout_of_memory` (step 12) |
| `flow.c` | "small buffer or calloc" 352, 1337 | 2 | `VVECTOR_NEED` over caller storage |
| `func.c` | "small buffer or malloc/calloc" 508, 722; `calloc` of `stable` 840 | 3 | `VVECTOR_NEED` over caller storage; 840 the TU's region |
| `attr.c` | `calloc` of an `attr_dir` page 53 (process-wide, under `pthread_once`) | 1 | stays; fails through `vout_of_memory` |
| `lazy.c` | `getenv` 1 | 1 | `cc_tables_init` (step 12), not a facility |
| `tu.c` | `getenv` 2 | 2 | as above |
| `attr.c` | sorted-name binary search `attr.c:137-155`, `185-196`, `200-212` (the two in `decl.c:586-606`, `758-769` are in `library-function.c`: WORK-cc-cache's) | 3 | one local `cc_sorted_name_find`: `common/string-index.h` is a hash, and these tables are generated sorted |
| `diag.c` | `vbuf_*` in the report entries | all | `vbuffer_*` (renamed by WORK-common's script; nothing to do unless its failure mode changed) |

No FNV, LEB128, byte load/store or file mapping in these files.
