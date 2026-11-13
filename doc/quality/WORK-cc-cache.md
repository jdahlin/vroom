# Work order: C front end, builtins and library functions

The third of three orders for `src/frontend/c/` (with `WORK-cc-expr.md` and
`WORK-cc-decl.md`).  The evidence is `doc/review/quality-cc-cache.md`
(finding ids `Q-cc-cache-NN`), with `doc/review/quality-boundaries.md`
(`Q-BND-NN`) and `doc/review/quality-lib.md` (`Q-lib-NN`).  Names follow
`doc/STYLE.md`; the "Decisions" table of `doc/PLAN-quality.md` is binding.

The review covers three files; this order can act on one of them.
`frontend/c/replay.c` is frozen until its owner decides on
`doc/quality/MEASURE-replay.md`'s recommendation (remove it), and
`driver/ccsnap.c` is the driver order's.  What is left is the builtin
table and its handlers, and, since one order should hold both of vcc's
builtin signature encodings, the library-function tables.

Line numbers are today's (main at d602dfa2; the review's numbers still hold
for these files).  If a line has moved, find the same text.

**Starts when** WORK-common steps 1 to 3 are on main.  Step 1 may run at
once; step 2 waits for WORK-cc-decl step 2 (the renames run one at a time);
step 5 waits for WORK-cc-decl step 1 (it creates `library-function.c`).

**Owns** (5 files, and every file they are split into)
- `src/frontend/c/builtin.c`.
- `src/frontend/c/replay.c`: owned so that it has an owner, and **frozen**:
  no step of this order changes it.
- The library tables `src/frontend/c/libbuiltins.inc`, `libfuncs.inc`,
  `libnames.inc`, and `src/frontend/c/library-function.c` from the merge of
  WORK-cc-decl step 1 (which cuts it out of `decl.c` 488-851, the only file
  that includes the three tables).

The shared internal header `src/frontend/c/ccimpl.h` and `ccint.h` are
WORK-cc-expr's.  This order edits in `ccimpl.h` only, by hand, the
`builtin` declarations (`ccimpl.h:76-87`), which only its files use, plus
prototypes its step 1 needs, in the sections of its files; anything else
there, and the `BI_*` enum in `ccint.h:293-337`, is a request to that
order or the AST split order.

It edits other modules and the other cc orders' files only by script, in
step 2, to follow a rename.

## Rules shared by the three cc orders

As in `WORK-cc-expr.md` ("Rules shared by the three cc orders"):
`replay.c` is frozen and the rename rows it would need go to
`doc/quality/rename-cc-deferred.tsv` (none of `builtin.c`'s function names
occur in `replay.c` today; the types `builtin` and `T` do); the
`struct cc` / `ast.h` split is not in these orders; the rename steps run
one at a time and a conflicting scripted PR is regenerated on current
main, never rebased; every step builds with `make VCC_UNITY=`.

## What done looks like

- `builtin.c` is ten files, one job each, each with an opening comment that
  says where a new builtin's other facts live (review section 3: enum,
  table, typing, folding, the three code-generator switches).
- Every function is `cc_builtin_` + noun + verb, or `cc_library_` for the
  library tables.
- `builtin_call` indexes a handler table by id class instead of a 306-line
  switch; no dispatch by name string after the id.
- One signature encoding (clang's) and one decoder in the front end.
- Output byte-identical in every step except those named.

## Steps

Each step is one PR.  Proof for every step unless it says otherwise:

```
nice -n 10 make -C src -j 3 OUT=$PWD/build/w vcc                      # unity
nice -n 10 make -C src -j 3 VCC_UNITY= OUT=$PWD/build/w-parts vcc      # file by file
VCC_BIN=$PWD/build/w bench/vcc-same.py --base DIR -j 3   # DIR: bench/vbench.py mkbase <branch point>
bench/vcc-sema.py batch -j 3 --bin-dir build/w           # 878/879
bench/vcc-ctests.py -j 3 --suite vcc                     # VCC_BIN=$PWD/build/w
bench/vcc-builtins.py                                    # builtins unknown to vcc, __has_builtin
bench/vcc-diag.py -j 3 --bin-dir build/w
```
`bench/vbench.py gate` where a step says so: it needs a quiet machine; the
PR says "Proof not run: needs a quiet machine" and the integrator runs it.

### 1. Split `builtin.c` (moves only; Q-cc-cache-15, -23)

Each range starts at the comment above its first function.

| From `builtin.c` | To | What |
|---|---|---|
| 1-451 | `builtin-table.c` | `arity`, `builtins[]` (17-267), the name directory, `cc_builtin_name`, `cc_builtin_libkind`, `cc_chk_simplify`, `cc_builtin_plain`, `cc_lib_sig`, `sigtype`, `sig_func_type`, `builtin_type`, `builtin_known` and its constructor hook, `init_builtins`, `builtin_of` |
| 432-449 | `builtin-fold.c` | `classify_type` |
| 453-669 | `builtin-object-size.c` | `object_*`, `cc_object_size` (`__builtin_object_size` analysis) |
| 671-865 | `builtin-fold.c` | `eval_builtin` |
| 867-1027 | `builtin-call.c` | `args_list`, `unqual_array`, `mk_builtin`, `offsetof_expr` |
| 1028-1172, 1839-1887 | `builtin-atomic.c` | `atomic_call`, `atomic_lock_free_call` |
| 1173-1341, 1748-1788 | `builtin-bit-count.c` | `fixed_bitcount`, `bitcount_local`, `bitcount_statement`, `big_bitcount`, `generic_bitcount`, `stdc_rotate` |
| 1342-1404, 1419-1428, 1561-1645, 1701-1747 | `builtin-vector.c` | `vector_builtin`, `builtin_vector_lane`, `elementwise_abs_*` |
| 1429-1522 | `builtin-x86.c` | `x86_pcmpstr_call`, `x86_simd_call` |
| 1405-1418, 1523-1560, 1646-1700, 1789-1838 | `builtin-library-call.c` | `fold_strncmp_zero`, `narrow_math_call`, `fp_sign_call`, `fast_math_call` |
| 1888-2118 | `builtin-os-log.c` | `oslog_layout`, `oslog_arg`, `os_log_call` |
| 2119-2425 | `builtin-call.c` | `builtin_call` |

The wrappers `cc_eval`/`cc_is_signed` (450-451) go with `builtin-table.c`
and are deleted by WORK-cc-expr step 2.  The forward declaration of
`elementwise_abs_effects` (669) goes where its users are.  With the move,
the stale comments of Q-cc-cache-23 are fixed: `builtin.c:1340` ("lowered
in emit (builtin.c)": say `arch/*/codegen`), and `ccimpl.h:143` files the
`dr_*` prototypes under "tu.c" (`ccimpl.h:142-146`): they move under a
"replay.c" heading, which edits the header, not `replay.c`.

Proof adds: `git diff -M --stat` shows the rename and cut files only;
`bench/quality.py --clones 999` reports no new clone.

### 2. Rename (scripted)

Table `doc/quality/rename-cc-cache.tsv`, applied with
`src/tools/rename-table.py`; the designer checks it against STYLE.md first.
No new name may exist already (`git grep -w`).

| Old | New |
|---|---|
| every file-scope function of `builtin-*.c` | `cc_builtin_` + noun + verb, the noun from its file |
| `builtin_call`, `args_list`, `mk_builtin`, `offsetof_expr`, `builtin_of`, `init_builtins`, `builtin_known`, `builtin_type`, `eval_builtin` | `cc_builtin_call_build`, `cc_builtin_arguments_parse`, `cc_builtin_node_new`, `cc_builtin_offsetof_build`, `cc_builtin_lookup`, `cc_builtin_table_init`, `cc_builtin_is_known`, `cc_builtin_type`, `cc_builtin_fold` |
| `sigtype`, `sig_func_type`, `arity` | `cc_builtin_signature_type`, `cc_builtin_signature_function_type`, `cc_builtin_arity_check` |
| `object_*`, `cc_object_size` | `cc_builtin_object_size_*`, `cc_builtin_object_size` |
| `atomic_call`, `atomic_lock_free_call` | `cc_builtin_atomic_build`, `cc_builtin_atomic_lock_free_build` |
| `fixed_bitcount`, `big_bitcount`, `generic_bitcount`, `stdc_rotate` | `cc_builtin_bit_count_fixed`, `cc_builtin_bit_count_bitint`, `cc_builtin_bit_count_generic`, `cc_builtin_rotate_build` |
| `bitcount_local`, `bitcount_statement` | `cc_lowering_local_new`, `cc_lowering_statement_append` (review: `tmp_decl`, `stmt_expr`; moved to `lowering.c` in step 4) |
| `vector_builtin`, `builtin_vector_lane`, `elementwise_abs_*` | `cc_builtin_vector_build`, `cc_builtin_vector_lane`, `cc_builtin_elementwise_abs_*` (`elementwise_abs_effects` -> `cc_builtin_has_effects`: it answers a general question, Q-cc-cache-14) |
| `x86_simd_call`, `x86_pcmpstr_call` | `cc_builtin_x86_simd_build`, `cc_builtin_x86_pcmpstr_build` |
| `narrow_math_call`, `fp_sign_call`, `fast_math_call`, `fold_strncmp_zero` | `cc_builtin_narrow_math_build`, `cc_builtin_fp_sign_build`, `cc_builtin_fast_math_build`, `cc_builtin_strncmp_zero_fold` |
| `oslog_*`, `os_log_call` | `cc_builtin_os_log_*`, `cc_builtin_os_log_build` |
| `cc_lib_sig`, `cc_chk_simplify`, `cc_builtin_libkind`, `cc_builtin_plain` | `cc_builtin_library_signature`, `cc_builtin_checked_simplify`, `cc_builtin_library_kind`, `cc_builtin_unchecked` |
| in `library-function.c`: `lb_type`, `lb_functype`, `libbi_find`, `disabled_lib_find`, `lib_builtin_disabled`, `lib_builtin_known`, `lib_builtin_sig`, `lib_builtin`, `implicit_lib_type`, `implicit_lib_func`, `implicit_func`, `libfn_find`, `lib_header`, `include_fix`, `lib_func`, types `libbi`, `libfn` | `cc_library_signature_type`, `cc_library_signature_function_type`, `cc_library_builtin_find`, `cc_library_disabled_find`, `cc_library_builtin_is_disabled`, `cc_library_builtin_is_known`, `cc_library_builtin_signature`, `cc_library_builtin_declare`, `cc_library_implicit_type`, `cc_library_implicit_declare`, `cc_implicit_function_declare`, `cc_library_function_find`, `cc_library_header`, `cc_library_include_fixit`, `cc_library_function_declare`, `cc_library_builtin`, `cc_library_function` |
| `cc_disable_builtin` (public, called by the driver) | `cc_builtin_disable` |
| `bi_dir`, `DIR_BITS`, `DIR_PAGE` (builtin) | `cc_builtin_directory`, `CC_BUILTIN_DIRECTORY_BITS`, `CC_BUILTIN_DIRECTORY_PAGE` |
| type `builtin` | `cc_builtin` (deferred while `replay.c` names it: then a `typedef` is not allowed either, decision "no aliases"; keep `builtin` and list it in the deferred file) |

`BI_*` stay (the AST split order renames them with the enum).

Proof adds: `git grep -w` for every applied old name returns nothing outside
`doc/` and `rename-cc-deferred.tsv`; `replay.c` unchanged.

### 3. Dead code and table asserts (identical output; Q-cc-cache-18, -12, -23)

- Flags bit 0 "constexpr" (`ccimpl.h:81`): all 405 rows set it, nothing
  reads it; the bit goes, the `CC_LIB_*` kind moves down (check
  `cc_builtin_library_kind`'s shift).
- `init_builtins`' `done` flag (`builtin.c:400-421`): it runs under
  `pthread_once` (`tu.c:214-218`); the flag and its comment go.
- `BI_CPU_SUPPORTS` (`ccint.h:310`, no user): listed for the AST split
  order, not deleted here (WORK-cc-expr's file, enum renumbering).
- The object-size end rule written twice (`builtin.c:659-667` =
  `679-691`): `cc_builtin_fold` calls `cc_builtin_object_size`.
- `_Static_assert`s next to the table in `builtin-table.c` for each enum
  range the code subtracts on (first and last of each: `builtin.c:1036`,
  `1105`, `1167`, `atomic.c:189-220`, and the code generators' ranges
  `arch/a64/codegen/builtin.c:38-42`, `807`, `820`, `991-1014`,
  `codegen/walk/expr.c:2363`, `2483`, `2517`, `arch/wasm/codegen/wcall.c:937`,
  `966`): one assert per range, with the comment "the code at X subtracts
  on this order".

### 4. Shared helpers (identical output; Q-cc-cache-13, -16)

After WORK-cc-expr step 5 (its `lowering.c` helpers).
- "Anonymous local holding a value" (`builtin.c:1188-1194`, `1469-1472`,
  `1492-1499`, `1572-1574`, `1680-1682`, `1727-1734`, `1821-1823`) calls
  `cc_lowering_temporary`; `bitcount_local` and `bitcount_statement` move
  to `lowering.c` under their step 2 names (a hand edit to WORK-cc-expr's
  file, reviewed by its designer).
- "Call the library function of row b" (`builtin.c:1541-1553`, `1584-1591`,
  `2412-2421`) calls `cc_lowering_library_call`.
- The signature loop `decl.c:560-579` = `:717-728` (exact, now in
  `library-function.c`) once.
- A `variant` byte in the table row (Q-cc-cache-16) replaces the name
  string dispatch after the id: `x86_simd_call` (1452-1512),
  `x86_pcmpstr_call` (1431-1445), `atomic_lock_free_call` (1843-1845),
  `atomic_call` (1105), `cc_builtin_plain` (289-304, a `_chk` -> plain
  column), `builtin_call` (2329, 2400); and the atomics' "signature" used as
  flags (`"s."`, `"vc"`, `"si"`, 1057-1082) becomes flags.
- The `*N$` parse in `oslog_layout` (1963-1980, twice) once.

Proof adds: `bench/vcc-ctests.py -j 3 --cpu cortex-a53`, `bench/vcc-x86.py
-j 3` (x86-64 host: else "Proof not run: needs an x86-64 host"),
`src/test/vcc/builtin_os_log.c` in the suite.

### 5. A handler table for `builtin_call` (identical output; Q-cc-cache-06)

`builtin_call` (`builtin.c:2119-2425`, a 306-line switch whose `default`
falls into 90 lines of special cases, 2328-2424) becomes a table of
handlers indexed by id class in `builtin-call.c`; each handler is one of
the functions of step 1's files.  The table rows and the `BI_*` enum stay
separate until the AST split order generates both from a `builtins.def`.

Proof adds: `bench/vcc-ctests.py -j 3 --suite vcc` and `--cpu cortex-a53`,
`bench/vcc-wasm.py -j 3` (wasm64 builtins; "Proof not run" with the reason
if emsdk is missing), `bench/vbench.py gate` (quiet machine).

### 6. One signature encoding (identical output; Q-cc-cache-11)

After WORK-cc-decl step 1.  `builtins[]` keeps only what
`libbuiltins.inc` cannot say (id, kind, variant); its signatures are
written in clang's encoding and decoded by `library-function.c`'s decoder
(`lb_type`, `decl.c:496-552`); `sigtype` (`builtin.c:327-356`) and
`cc_lib_sig`'s `clang_enc` flag (309) go.  The 103 names in both tables
are checked to give the same type before the vcc-letter row is removed;
the PR lists any that differ (each a separate bug PR first, if clang
disagrees with vcc's row).  `wlib_type` (`arch/wasm/codegen/wcall.c:30`)
is the wasm order's to switch to the front end's decoder.

Proof adds: `bench/vcc-builtins.py`, `src/test/vcc/lib_builtins.c` in the
suite, `bench/vcc-diag.py -j 3` (implicit declaration notes),
`bench/vcc-wasm.py -j 3` or "Proof not run".

### 7. Out of memory (failure paths only)

`init_builtins`' `malloc` (407) and `calloc` (414) fail through
`vout_of_memory()`; the `malloc` + `memset` + `free` of a padded copy
becomes `vintern_cstr` (`common/intern.h:77`: no padding needed, names
under 64 bytes; the PR checks the longest name in `builtins[]`).

Proof adds: `make -C src vcc-asan` and `bench/vcc-sema.py batch -j 3
--bin-dir build/src-asan`.

## Not in this order

- **Anything in `frontend/c/replay.c`** (Q-cc-cache-02's replay half, -03,
  -07, -08, -09, -10's replay tables, -19, -20, -22): the owner decides on
  `MEASURE-replay.md` first.  If replay stays, those findings become their
  own order; if it goes, `VCC_DRCOV` (`declaration-coverage.c`, moved by
  WORK-cc-decl), the hooks in `sym.c`, `type.c`, `token.c`, `record.c`,
  `decl.c`, `declspec.c`, `func.c` and `rename-cc-deferred.tsv` follow.
  Meanwhile `replay.c`'s leaking macros (Q-cc-cache-20) are contained by
  sorting it last in `cc_SRC` (WORK-cc-expr step 1), not by `#undef`.
- `driver/ccsnap.c` and `include/driver/ccsnap.h` (Q-cc-cache-17's move to
  `frontend/c/`, Q-cc-cache-04's keep list, the `LOCS` list): the driver
  order's; the moved file would then need an owner among these three.
- One field table per record with a coverage assert (Q-cc-cache-02, review
  decision 2) and `struct cc` split into per-TU and snapshot state
  (Q-cc-cache-04, decision 3): with the AST split order (its own order
  later, on `MEASURE-ast-split.md`: embed `struct ast` first in
  `struct cc`).
- `builtins.def` generating the `BI_*` enum, names and a flags table for
  the code generators (Q-cc-cache-06, decision 4): the enum is in
  `ccint.h` and switched on by three code generators; with the AST split.
- One side-effect predicate (Q-cc-cache-14): `cc_builtin_has_effects`,
  `init.c:750`, `conv.c:163`, `arch/a64/codegen/builtin.c:14` disagree
  today; needs the table of which caller relies on which difference.
- `__has_builtin` through a function pointer set by a constructor
  (`builtin.c:393-396`, a hidden pp -> cc edge, Q-BND-14): the boundary
  between pp and cc, decided with the module graph.
- One `cc_opts_sig()` for both caches (Q-cc-cache-05): touches `ccsnap.c`
  and `replay.c`.
- Eviction of either cache in a long-lived vccd (Q-cc-cache-21, decision 6):
  needs the measurement over several builds.

## Bugs, not in this order

Each needs a test and a fix in its own PR.  Checked on main at d602dfa2.
- Fixed by PR #137 (a1be8b33): Q-cc-cache-01, a parse error after a
  restored prefix crashed `vpp --batch -j 4`.
- Open: `sym.defloc` and every location in `attrs` are not in `ccsnap.c`'s
  `LOCS` (`ccsnap.c:144-182`): after a restore the "marked deprecated
  here" note is on the wrong line (Q-cc-cache-02, reproduced by the
  review).  `ccsnap.c` is the driver order's file.
- Open: replay's schemas miss about 30 fields (Q-cc-cache-02): wrong
  "deprecated" message under `VCC_DR=1`; frozen with `replay.c`.
- Open: replay's cache key omits ten parse options that `ccsnap.c`'s
  includes (Q-cc-cache-05); and `ccsnap.c:385` hashes `cc_opts` only up to
  `outfd`, so an option after it is not keyed (Q-BND-05).
- Open: a snapshot that loses the CAS is neither freed nor uncounted
  (`ccsnap.c:523-527`, Q-cc-cache-21).

## Report

Per step: the PR, the proof commands with their results (numbers), anything
this order got wrong.  The designer checks the result against STYLE.md and
"What done looks like", not the diff line by line.

## Adoption of common/ facilities

Only after WORK-common step 5 has merged; one PR, identical output (common
proof plus `bench/vcc-builtins.py`).  Old file names and lines; find them
in the step 1 files.  `replay.c`'s sites (21 allocations, `VGROW`/`VPUSH`,
eight hash tables, two FNV-1a, a private clock, quality-lib Q-lib-01/-04)
wait with the file.

| File | Site | Count | Facility |
|---|---|---|---|
| `builtin.c` | `malloc` of a padded name 407 + `free` | 1 | `vintern_cstr` (step 7), not a facility |
| `builtin.c` | `calloc` of a directory page 414 (`bi_dir`) and its twin `attr.c:53` (`attr_dir`, WORK-cc-decl's) | 1 | stays: a page directory by identifier id, not a vector or a string table |
| `builtin.c` | hex digit value 744-745 | 1 | `vhex_digit_value` (`common/format.h`) |
| `builtin.c` | linear `strcmp` scan of `builtins[]` in `builtin_known` 385-391 | 1 | `vstring_index_find` (`common/string-index.h`), built once in `cc_builtin_table_init` |
| `decl.c` (in `library-function.c`) | sorted-name binary search 586-606, 758-769 | 2 | the local `cc_sorted_name_find` of WORK-cc-decl's adoption (`attribute.c`'s three): `string-index.h` is a hash, these tables are generated sorted; whichever adoption PR merges second calls the first's helper |

No LEB128, byte load/store, buffers or file mapping in these files.

## Status

- Step 1 done (PR "cc: builtins and snapshot, one job per file"), stacked on
  WORK-cc-decl step 1.  `replay.c` and `driver/ccsnap.c` are untouched
  (replay.c is frozen pending the removal decision; ccsnap.c is the driver
  order's), so the part of the review about the snapshot waits.
- The order's line numbers are function lines; each range was taken from the
  comment block above the function, as the order says.  The forward
  declaration of `elementwise_abs_effects` (669) is gone: its users are in
  four files, so the function is `VPRIV` with a prototype in `ccimpl.h`
  (as are `arity`, `sigtype`, `builtin_type`, `atomic_call`,
  `atomic_lock_free_call`, `generic_bitcount`, `bitcount_local`,
  `bitcount_statement`, `stdc_rotate`, `elementwise_abs_call`,
  `builtin_vector_lane`, `x86_pcmpstr_call`, `x86_simd_call`, `os_log_call`
  and `object_pointer_bound`; the `object_bound` typedef moved there too).
- `builtins[]` stays static in `builtin-table.c`; its two other users
  (`builtin-vector.c`, `builtin-library-call.c`, and `builtin-call.c`) reach
  it through the new `builtin_all()`.  The block of `ccimpl.h` where the
  `builtin` type is declared holds these prototypes.
- Stale comments fixed: "lowered ... in emit (builtin.c)" now says
  `arch/<cpu>/codegen`; the `dr_*` prototypes in `ccimpl.h` sit under a
  "replay.c" heading.
- Proof not run: `bench/vbench.py gate` and `bench/quality.py --clones 999`.
