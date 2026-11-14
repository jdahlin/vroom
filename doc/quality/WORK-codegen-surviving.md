# Work order: the code generation that survives VIR

The module pass for the parts of code generation that VIR-only code
generation keeps, written to be carried out by a cheaper model and scripts.
`doc/quality/VIR-MIGRATION.md` ("What this does to the quality plan") is
binding: arm64's AST walker, x86-64's direct lowering and their record
optimizers are deleted at the end of the migration, so they get no pass;
what lowers VIR's output stays and gets one.  Evidence:
`doc/review/quality-backend-records.md` (`Q-BR-NN`),
`quality-codegen-shared.md` (`Q-CGS-NN`), `quality-walker-a64.md`
(`Q-WA64-NN`).  Names follow `doc/STYLE.md`.

The reviews' line numbers are those of main before the tree moved: find
code **by function name**; line numbers here are hints from main at
d602dfa2.

**Starts when** now: nothing it needs is open.  `WORK-common.md` step 2
rewrites every file of `src/` by script; whichever of the two lands second
merges `origin/main` (a merge commit) and reruns
`src/tools/rename-table.py` with `doc/quality/rename-common.tsv` on the
files it added.  It runs beside `WORK-vir.md` all the way: the two share no
file (see "Owns").

**Owns** (no other order edits these; this order edits nothing else):

- `src/codegen/emitimpl.h` and `src/codegen/tu.c` (this order, not
  WORK-vir, owns both; WORK-vir keeps its state in the builder's own struct);
- the new files of steps 1, 3 and 9 under `src/codegen/` (listed there);
- `src/codegen/rec/rec.h`, `rec/fallthrough.c`, `rec/unreachable.c`, and in
  `rec/copies.c`, `rec/slots.c`, `rec/slot-values.c` only the lines step 4
  (record type) and the adoption section change;
- arm64, `src/arch/a64/codegen/`: `a64impl.h`, `ins.h`, `insn.c`, `abi.c`,
  `asm.c`, `func.c`, `obj.c`, `print.c`, `darwin.c`, `unreachable.c`; in
  `opt.c` only `opt_regs`, `opt_noframe` and the per-opcode switches step 5
  replaces;
- x86-64, `src/arch/x64/codegen/`: `x64impl.h`, `print.c`, `obj.c`,
  `effects.c`, `asm.c`; in `abi.c` the classification functions; in
  `lower.c` the record constructors and `x64_finish`;
- **cut only**, in arm64's walker files `bind.c`, `builtin.c`, `call.c`,
  `cond.c`, `stmt.c`, `lval.c`, `arith.c`, `value.c`, `label.c`: step 1
  removes the functions it lists from them, and nothing else in those
  files changes;
- `bench/vcc-rec-effects.py`, and in `AGENTS.md` only the bullets that name
  the files above.

Not owned, and not edited: `src/codegen/vir/`, `src/vir/`,
`src/arch/a64/vir/`, `src/arch/x64/vir/`, `src/arch/x64/codegen/vir.c`
(WORK-vir); `src/codegen/walk/`, `codegen/opt/`, `codegen/flow/`,
`prepass.c`, `data.c`, `obj.c`, `type.c`, `src/arch/wasm/`; `src/common/`,
`src/port/` (WORK-common); the front end, `as/`, `obj/`, `ld/`, `driver/`,
the server.  `src/Makefile` needs no edit: every new file lands under a
wildcard of `codegen_SRC` (`codegen/*.c`, `arch/a64/codegen/*.c`,
`arch/x64/codegen/*.c`).

**Interface frozen for WORK-vir.**  VIR's two lowerings call `finish_function`,
`x64_finish`, `x64_begin`, the record constructors of `a64impl.h`
(`put`, `mov`, `movi`, `op3`, `op3s`, `op4`, `opbf`, `opi`, `mem`, `word`,
`cset`, `f_in`, `f_out`, `q_in_hi`) and `x64impl.h` (`x64_put`, `x64_op3`,
`x64_mov`, `x64_mem`, `x64_movi`, `x64_label`, `x64_jump`, `x64_csused`,
`x64_argreg`, `x64_effects`, `x64_last_u32`, `x64_nosimd`), `note_ref`,
`logimm_ok`, `canon_sym`, `sym_attrs`, `sym_needs_got`, `abi_align`,
`em_tail_caller_ok`, `em_tail_target_ok`, `tsize`, and arm64's `slot`,
`slot_tag`, `addr_of_ref`, `lbase`, `log2sz`, `ref_align`, `load_n`,
`store_n`, `newlabel`, `kv_edge`, `deflabel_forced`, `jump`, `bcond`,
`cbz`, `tbz`.  This order neither renames them nor changes their
signatures; their renames wait for a rename window after VIR-MIGRATION
step 5.  Every header this order splits keeps being included from where its
declarations were (`emitimpl.h` includes the new headers), so no includer
outside this order changes.  A record opcode WORK-vir needs is a one-row
PR to the op table of step 5 or 7, made by this order on request.

## What done looks like

- Nothing outside `src/arch/a64/` lives in it: the ~800 target-independent
  lines are in `src/codegen/`, and x86-64 and wasm include no arm64 file
  for them.
- `emitimpl.h` declares what is shared; arm64's prototypes are in arm64's
  headers.
- One record type: `ins` is `rec`, 16 bytes, and the record passes read it
  without byte offsets.
- One op table per CPU (operands read and written, flags, branch kind,
  text, encoding), read by the passes, the printer and the encoder; adding
  an opcode without a row does not compile.
- Each expanding arm64 record has one plan that printer, encoder and sizer
  read; the prologue and epilogue are made once.
- ABI classification is a function of the type that VIR calls; the
  inline-asm template has one scanner for both CPUs.
- The tail of `finish_function` scans only what can be there, carries its
  frame in a struct, and fails through one function.
- Files of one job each, with opening comments; private names follow
  STYLE.md.

## Steps

Each step is one PR.  Proof for every step unless it says otherwise (all
steps but 10 keep the output identical):

```
nice -n 10 make -C src -j 3 vcc OUT=$PWD/build/u                     # unity
nice -n 10 make -C src -j 3 vcc VCC_UNITY= OUT=$PWD/build/p          # file by file: each file compiles alone
bench/vbench.py mkbase REV                                           # base from the branch point
bench/vcc-same.py --base DIR -j 3                   (VCC_BIN=$PWD/build/u)
                                                    # its c2/s2/d2 modes are VIR (-O2), c1 the direct emitter: both go through the same tail
bench/vcc-ctests.py -j 3 --suite vcc --no-cache --bin-dir build/u
python3 bench/vcc-x86.py -j 3 --suite vcc           # x86-64 (CI's x86-64 job); and CI's wasm job
```

`vcc-same.py`'s known 8 differences (mode `e`, the base tree's path in line
markers) are not differences.  `bench/vbench.py gate` (within 1 %) needs a
quiet machine: the PR lists it under "Proof not run" and the integrator
runs it.

Order and parallelism: 1 → 2 → 3 (3a, 3b, 3c in parallel) → then 4, 7, 8,
9 in parallel (disjoint files) → 5 after 4 → 6 after 5 → 10 after 6 →
11 last.  Steps 8 and 9 come first among the later steps if WORK-vir is
waiting on them (its 3.2 needs step 8, its 3.1 needs step 9).

### 1. Move the target-independent code out of arm64 (moves only)

Cut each function, unchanged, into the new file; its prototype stays in
`emitimpl.h` (it already is there) so no caller changes.  A function that
was `static` and is now called across files becomes `VPRIV`.  Every new
file starts with `#include "emitimpl.h"`.

| From (`src/arch/a64/codegen/`) | Functions | To (`src/codegen/`) | Why | Finding |
|---|---|---|---|---|
| `bind.c` | `note_ref`, `claim_ref` | `references.c` | used by the walker, x86-64, data, VIR, wasm | Q-CGS-01, Q-WA64-02 |
| `call.c` | `walk_fn` | `references.c` | called by `tu.c`, `walk/stmt.c`, wasm | Q-CGS-01 |
| `bind.c` | `binding`, `is_local_var` | `local-variables.c` | walker, `opt/`, prepass, `tu.c`, x86-64, wasm | Q-WA64-02 |
| `func.c` | `out_chunk`, `sym_header`, `ctor_entry` (and the lock `out_chunk` takes) | `function-output.c` | x86-64 `lower.c` and wasm call them; `sym_header` tests `X86(c)` | Q-BR-11, Q-CGS-01 |
| `stmt.c` | `dbg_file`, `dbg_where`, `dbg_row`, `dbg_remap` | `debug-rows.c` | x86-64's `lower.c`, `print.c`, `dead.c` call `dbg_remap`; VIR's lowering will call `dbg_row` (WORK-vir 3.0) | Q-BR-11 |
| `call.c` | `ael_slot` (not `kv_track`, not `const_var`: arm64 only) | `known-values.c` | called only by `walk/expr.c` | Q-CGS-08, Q-WA64-02 |
| `builtin.c` | `strip_casts`, `side_effects` | `expression-effects.c` | walker x10, wasm x7, x86-64 `locals.c`, prepass, `data.c` | Q-WA64-02 |
| `lval.c` | `ptr_to_vla` | `expression-queries.c` | `walk/expr.c` x7 | Q-WA64-02 |
| `call.c` | `direct_callee` | `expression-queries.c` | `prepass.c` | Q-CGS-01 |
| `cond.c` | `strip_shift0`, `same_pure`, `cmp_mask`, `trange`, `irange`, `irange_cmp`, `cmp_var_range`, `ffact`, `cond_fold` (not `gen_cond`) | `condition-fold.c` | `walk/expr.c` x3 | Q-WA64-02 |
| `arith.c` | `em_is_cmp` (not `fold_int_bin`) | `condition-fold.c` | the walker's copy is `walk/expr.c`'s, Q-WA64-03 decides later | Q-WA64-02 |
| `value.c` | `sym_needs_got` | `symbol-access.c` | x86-64 x6 (`lower.c`, `obj.c`, `print.c`, `vir.c`) | Q-BR-11 |
| `asm.c` | `imm_fits`, `kbits` | `known-bits.c` | target-independent bit facts; their only user is `gen_asm` today | Q-WA64-02 |

The review proposed `codegen/opt/` for three of them (`condfold.c`,
`effects.c`, `kbits.c`, `known.c`); `codegen/opt/` is the walker's recipe
directory, removed or rewritten in VIR-MIGRATION step 4, so they go to
`src/codegen/` instead.

Second commit, the arm64 helpers VIR's lowering calls out of files that
will be deleted with the walker (moves only; names unchanged):

| From (`src/arch/a64/codegen/`) | Functions | To (`src/arch/a64/codegen/`) | Why |
|---|---|---|---|
| `value.c` | `slot`, `slot_tag`, `addr_of_ref`, `lbase`, `log2sz`, `ref_align`, `load_n`, `store_n` | `memory-access.c` | called by `codegen/vir/a64.c`; `value.c` goes with the walker |
| `label.c` | `newlabel`, `kv_edge`, `deflabel_forced`, `jump`, `bcond`, `cbz`, `tbz` | `branches.c` | same; `jump`, `bcond`, `cbz`, `tbz` call `kv_edge` |

`label.c` and `value.c` keep everything else.  Proof adds `bench/vcc-dwarf.py
-j 3` (debug rows moved).

### 2. Split `emitimpl.h` (moves only; Q-CGS-02, Q-CGS-20, Q-BR-11)

| From (`src/codegen/emitimpl.h`) | To | Why |
|---|---|---|
| arm64 walker prototypes (the sections named `expr.c`, `stmt.c`, `call.c`, `arith.c`, `lval.c`, `value.c` (rest), `cond.c` (`gen_cond`), `fp.c`, `bind.c` (rest), `label.c` (rest), `builtin.c` (rest), `tail.c`, the recipe files) | new `src/arch/a64/codegen/walker-prototypes.h` | dies with the walker in one `git rm` |
| arm64 surviving prototypes (`func.c`, `print.c`, `obj.c`, `opt.c`, `insn.c`, `darwin.c`, `unreachable.c`, step 1's `memory-access.c`, `branches.c`) | `src/arch/a64/codegen/a64impl.h` | arm64's header |
| arm64 ABI prototypes (`abi.c`: `hfa`, `is_sret`, `classify_va`, `abi_align`, `classify`, `spill_live`, `unspill`, `call_fixed`, ...) | new `src/arch/a64/codegen/abi.h` | step 8 makes it VIR's interface |
| the `em_rec_*` inline helpers (the byte-offset views) | `src/codegen/rec/rec.h` | record helpers; step 4 removes most of them |
| the opening comment ("arm64-only emitter without IR") | rewritten: shared state of every target, the walker, VIR's builder | Q-CGS-20 |
| comments one line off (on `cwval`, `nerrcall`, `inl_cand`) | moved to their field | Q-CGS-20 |

`emitimpl.h` includes `walker-prototypes.h`, `abi.h` and (as today)
`a64impl.h` and `ins.h` at the places it included arm64 headers, so no
includer changes.  The ~40 `ecx` fields only arm64 reads (`a64st`,
Q-CGS-02's "L fields") are not moved: they die with the walker.

### 3. Split by job (moves only; three PRs, in parallel)

**3a, arm64** (Q-BR-03, Q-BR-22):

| From (`src/arch/a64/codegen/`) | Functions | To (`src/arch/a64/codegen/`) |
|---|---|---|
| `func.c` | `cs_base`, `cs_off`, `FK`, `finish_direct`, `finish_function` | `finish.c` |
| `func.c` | `a64_fallthrough_remap`, `drop_fallthrough`, `jt_of`, `expand_tables`, `tbz_relax`, `tail_expand` | `expand.c` |
| `func.c` | `choose_lregs` | `frame-registers.c` |
| `func.c` | the function text (prologue/epilogue strings, `.loc` rows, function line; the text half of `finish_direct` if it separates without edits) | `function-text.c` |
| `func.c` | `emit_function1`, `emit_function` | stays in `func.c` (walker entry, not in this order) |
| `opt.c` | `opt_regs`, `opt_noframe`, `word_regs` | `frame-registers.c` |
| `obj.c` | `enc`, `e_movi`, `e_*`, `a64_words` | `encode.c` |
| `obj.c` | `a64_replay` and the chunk loop | `commit.c` ("commit": STYLE.md glossary) |
| `darwin.c` | all | `git mv` to `darwin-text.c` (its move to `platform/darwin/` waits: Q-BR-22 also needs `as/`) |

**3b, x86-64** (Q-BR-03, Q-BR-17, Q-BR-21):

| From (`src/arch/x64/codegen/`) | Functions | To |
|---|---|---|
| `print.c` | `x64_tied`, `x64_commutes`, `x64_twoaddr` | `twoaddr.c` (the name `x64impl.h` and `lower.c` already use) |
| `lower.c` | `x64_state`, `x64_ecx_free`, `x64_put`, `x64_op3`, `x64_mov`, `x64_base`, `x64_mem`, `x64_movi`, `x64_label`, `x64_jump`, `x64_jumpr`, `x64_named` | `records.c` |
| `lower.c` | `x64_begin`, `x64_compact_remap`, `x64_finish`, `x64_dbgrow` | `finish.c` |
| `abi.c` | `x64_merge`, `x64_cls`, `x64_wide_cls`, `x64_wide_record`, `x64_check_vector_abi`, `x64_bool_lanes`, `x64_classify`, `x64_classify0`, `x64_need` | `abi-classify.c` |
| `obj.c` | `x64_obj_record`, `xo_*` | `encode.c`; `x64_replay` stays in `obj.c` renamed by `git mv` to `commit.c` |

`x64_print` is split into prologue, epilogue and the record switch as
three functions in `print.c` (Q-BR-21; a move of blocks, no logic change).
`abi.c` keeps the call sequences (direct path, not in this order).

**3c, the TU driver** (Q-CGS-10):

| From (`src/codegen/tu.c`) | What | To (`src/codegen/`) |
|---|---|---|
| `tu.c` | the queue, worker loop, `ecx_init`, `ecx` reservation and release, thread start and join | `work-queue.c` |
| `tu.c` | the unit header, tail, DWARF unit and the write of `-S` text | `unit-output.c` |
| `tu.c` | the commit driver (chunks to the assembler/object) | `unit-commit.c` (not `codegen/obj.c`, which this order does not own) |
| `tu.c` | `emit_tu1`'s rounds | stays; `emit_tu1` becomes the rounds calling the three |

The pool is moved, not redesigned: the shared scheduler (Q-CGS-25,
`quality-scheduling.md`) replaces it later.  Proof for 3a and 3c adds
`bench/vcc-dwarf.py -j 3`, `bench/vcc-cfi.py -j 3`, `bench/vas-obj.py`,
`bench/vccd-test.py`; 3b adds `bench/vcc-x86.py -j 3` (all suites).

### 4. One record type (Q-BR-01, Q-CGS-26)

`ins` (`arch/a64/codegen/ins.h`) and `rec` (`codegen/rec/rec.h`) are the
same 16 bytes.  `rec` gets the register bytes as a union so both spellings
compile:

```c
union { uint8_t r[4]; struct { uint8_t rd, rn, rm, ra; }; };
_Static_assert(sizeof(rec) == 16, "one record is 16 bytes");
```

`ins.h` replaces its struct by `typedef rec ins;` (field names unchanged,
so no arm64 file changes).  The byte-offset views go: `rec/fallthrough.c`'s
layout struct and `rec/unreachable.c`'s stride and offset take `rec *`;
`emitimpl.h`'s `em_rec_*` views (moved to `rec.h` in step 2) are deleted
where the caller now reads the field, kept where a caller outside this
order uses them (`arch/x64/codegen/copies.c`, `lower.c` keep compiling
unchanged).  `rec/copies.c`'s callback struct stays (it abstracts opcode
facts, not layout; step 5 and 7 feed it from the op tables).

### 5. arm64's op table (Q-BR-02)

New `src/arch/a64/codegen/op-table.h` (an X-macro `A64_OPS(X)`, one row per
`I_*` opcode of `ins.h`, in its order) and `op-table.c` (the table):

| Column | Meaning | Replaces |
|---|---|---|
| `name` | the `I_*` enumerator | |
| `text` | mnemonic, or `NULL` with `expand` set | `print.c`'s switch (the plain cases) |
| `roles` | per operand slot `rd rn rm ra`: none, read, write, read-write | `opt.c` `roles_sw` |
| `flags` | reads NZCV, writes NZCV | `opt.c` `sets_flags` |
| `branch` | none, unconditional, conditional, compare-and-branch, test-and-branch, indirect, return, call | `opt.c` `brkind_sw`, `unreachable.c` (its opcode list) |
| `pure` | no side effect beyond `rd` | `opt.c` `pure`, `XT` |
| `single` | always one machine instruction | `opt.c` `fplain` (with step 6) |
| `expand` | the expansion plan of step 6, or none | |
| `encode` | base encoding, or the encoder function for the form | `obj.c` (`encode.c`) switch, plain cases |

Completeness at compile time: the enum and the table come from the same
X-macro, and `_Static_assert(sizeof a64_ops / sizeof *a64_ops == I_COUNT)`.
Readers switch one by one, one commit each: `roles_sw`, `brkind_sw`,
`sets_flags`, `pure`/`XT`, `unreachable.c`, `print.c`, `encode.c`.  An
unknown opcode in printer or encoder fails (`efatal` now, `VBUG` after
WORK-common step 3; Q-BR-14), it no longer prints `// ?` or emits nothing:
this is the one behaviour change and changes no output on the suites.
`I_MOVSP` (never emitted, Q-BR-16) is deleted with its rows.  Proof adds
`bench/vcc-rec-effects.py` (extended to compare the table with the old
switches on every opcode before they are deleted), `bench/vas-obj.py`,
`bench/vcc-darwin-s.py -j 3`.

### 6. One plan per expanding record (Q-BR-04, Q-BR-06, Q-BR-18)

`src/arch/a64/codegen/expansions.c`: `a64_movi_plan`, `a64_addi_plan`,
`a64_mem_plan`, `a64_tlv_plan` (count, kind and chunks of the instructions a
record becomes), read by `print.c`, `encode.c`, `a64_words` (the sizer)
and the `writes` column (x17 from `p_addi`, R2.2's hidden writes), replacing
`p_movi`/`e_movi`, `p_addi`/the `obj.c` copy, `p_mem`/`obj.c`, the TLV
pair, and `fplain`.  The prologue and epilogue: `a64_prologue(ins *out)`
and `a64_epilogue(ins *out)` in `arch/a64/codegen/finish.c`, made once as records; the text
prints them through `print_ins`, the CFI rows come from the `NOP_CFI_*`
markers (removes the copies in `finish_direct`, the two string versions
and the CFI restore rows).  `obj.c`'s use of `logimm_ok` and the encode of
`a64_logimm` (`as/enc.c`, not owned) must agree: `encode.c` checks the
`-1` case and fails; merging the two tests is `as/`'s order.  Proof adds
`bench/vcc-cfi.py -j 3`, `bench/vcc-dwarf.py -j 3`, `bench/vas-obj.py`,
`bench/vcc-darwin-s.py -j 3`, `bench/vas-corpus.py -j 3 -O2` (if time).

### 7. x86-64's op table and forms (Q-BR-02, Q-BR-05, Q-BR-19)

`src/arch/x64/codegen/op-table.h`/`.c`: the same idea with x86-64's
columns (operands read and written, two-address tie, flags, branch kind,
text, `x64_form`: the operand shape the printer and encoder both select by).
Replaces `effects.c`'s two tables (`x64_effects`, `x64_flow_table`),
`x64_tied`, the duplicated tables `print.c`/`obj.c` (Q-BR-05's list), and
`x64_asmlabel` = `x64_lbl` and the second register-name table (Q-BR-19).
Runs in parallel with 5 and 6 (no shared file).  Proof adds
`bench/vcc-x86.py -j 3` (all suites, `--objcmp`), `bench/vas-obj.py`.

### 8. ABI classification as an interface for VIR (WORK-vir 3.2)

arm64: `abi.h` (step 2) declares the classification as functions of a type
with no records and no `ecx` (`hfa`, `is_sret`, `classify_va`,
`abi_align`, `classify`'s decision part); `abi.c` keeps the record-emitting
half (`spill_live`, `unspill`, `call_fixed`) for the walker.  Split
`classify` into `a64_classify_type` (pure, in new `abi-classify.c`) and its
caller if they are one function today.  x86-64: `abi-classify.c` (step 3b)
exposes `x64_classify` the same way.  Nothing in `codegen/vir/` changes
here: WORK-vir 3.2 starts calling it.  Proof adds `bench/vcc-abi.py`,
`bench/vcc-abi.py --host`.

### 9. One inline-asm template scanner (Q-BR-12, Q-WA64-09)

`src/codegen/asm-template.c`: one scanner of `%` escapes (operand index,
modifier, named operand, `%=`, `%%`, `%l` labels) that calls back per
operand and per label.  `arch/a64/codegen/asm.c` (the template loop and the
operand-name lookup) and `arch/x64/codegen/asm.c` (its loop) call it; the
differences the review lists between the two grammars are kept as they are,
each one a flag of the call with a comment (deciding them changes output:
a later PR with a test each).  `gen_asm` and `x64_asm` themselves are
split by phase (Q-BR-21: select alternative, place operands, template,
store outputs) in the same files.  WORK-vir 3.1 lowers `VO_ASM` through the
arm64 operand callback.  Proof adds the `asm*` tests of `test/vcc`
(`bench/vcc-ctests.py -j 3 --suite vcc asm`), `bench/vcc-x86.py -j 3`, the
kernel with `bench/vccd-kbuild.sh work/build-X -j 3` when `work/` exists
(1 686 inline-asm functions in the sample).

### 10. The tail of `finish_function` (changes behaviour on limits only)

| What | Change | Finding |
|---|---|---|
| the scans for VIR-only records (`finish_function`, `tail_expand`) | run only when `e->vlow`; `put()` keeps a bit set of opcodes seen, the presence scans (`tbz`, raw) test it | Q-BR-08 |
| `FK` (frame shape through file statics), `finish_direct`'s packed `ctor` int | an `a64_frame` struct passed down to `cs_base`, `cs_off`, `finish_direct`, `tail_expand` | Q-BR-20 |
| `rawp`/`retreads` set by each entry of `opt.c` | one cleanup context set and cleared by the entries (only in `frame-registers.c` and the entries `opt.c` keeps) | Q-BR-20 |
| "function too large" (`a64impl.h` abort, `finish.c` x2, `efatal` in `finish.c` and x86-64 `records.c`) | one `em_too_large(e)` in `codegen/function-output.c`: a diagnostic and the job fails | Q-BR-14 |
| two compactions back to back (`finish_function`, `x64_finish`) | one | Q-BR-09 |

Proof: as above (identical output), plus `bench/vcc-stress.py --vccd`
(limits fail the job, the server survives), `bench/vcc-cfi.py -j 3`;
instructions per TU should go down (`bench/vbench.py gate`, integrator).

### 11. Comments, dead code, private names

- Opening comment on every owned file (one job, what it reads, what it
  writes); fix the stale ones (Q-BR-17: `twoaddr.c` now exists, `insn.c:1`,
  `opt.c`'s "taken afresh", the orphan comments; `doc/PLAN-x86.md`'s list of
  files that do not exist, as a pointer to this order's file list).
- Dead: `em_slot_access`, `em_slot_detail`, `em_flow_graph_free` and the
  others of Q-BR-16 if still unused (`git grep -w NAME -- src` returns the
  definition only); the `if (0)` block in x86-64 `wide.c` is not owned:
  report it.
- Private renames, by script, on names whose every use is in owned files
  (`git grep -lw NAME -- src` lists owned files only; the list goes in the
  PR):

| Old (pattern) | New | Where |
|---|---|---|
| `p_NAME` (static) | `a64_print_NAME` | `arch/a64/codegen/print.c` |
| `e_NAME` (static) | `a64_encode_NAME` | `arch/a64/codegen/encode.c` |
| `xo_NAME` | `x64_encode_NAME` | `arch/x64/codegen/encode.c` |
| `cs_base`, `cs_off` | `a64_frame_save_base`, `a64_frame_save_offset` | `finish.c` |
| `jt_of` | `a64_jump_table_of` | `expand.c` |
| `dbg_file`, `dbg_where` (static) | `codegen_debug_file`, `codegen_debug_where` | `codegen/debug-rows.c` |

Names VIR or the walker call (the frozen list above, `dbg_row`,
`dbg_remap`, `out_chunk`, `side_effects`, ...) are not renamed here.
Proof: as above; `AGENTS.md`'s bullets for these files updated.

## Not in this order

- arm64's AST walker: `src/arch/a64/codegen/` `expr.c`, `stmt.c`, `call.c`,
  `arith.c`, `lval.c`, `value.c`, `cond.c`, `fp.c`, `bind.c`, `label.c`,
  `builtin.c` (beyond step 1's cut), the recipe files (`mulconst.c`,
  `sdivconst.c`, `sdivpow2.c`, `udivconst.c`, `switch-bitset.c`,
  `switch-values.c`), `tail.c`, `func.c`'s `emit_function*`, and `opt.c`'s
  passes: no renames, splits, comments or fixes (Q-WA64-01 .. -15 except
  -02 and -09; Q-BR-09's `opt.c` half, -10, -13, -15's `opt.c` sites).
  They are deleted in VIR-MIGRATION step 5.
- x86-64's direct lowering: `lower.c` (beyond step 3b), `wide.c`, `fp.c`,
  `locals.c`, `mulconst.c`, `mulhi.c`, `setjmp.c`, `switch-bitset.c`,
  `switch-table.c`, `tail.c`, `frame.c`, `extensions.c`, `dead.c`,
  `copies.c`, and `abi.c`'s call sequences (Q-BR-07, Q-BR-10, Q-BR-13).
- wasm: all of `src/arch/wasm/`.
- The shared walker `src/codegen/walk/` (Q-CGS-03 .. -09, -12, -13, -18,
  -21, -27), `codegen/opt/` (Q-CGS-15, -17), `codegen/flow/`: VIR-MIGRATION
  step 4 decides what is left of them.
- `prepass.c` (Q-CGS-11), `data.c` (Q-CGS-16), `type.c` (Q-CGS-14,
  needs the front end), `codegen/obj.c`: they survive, but no finding in
  scope; a later order.
- `getenv` per round into `cc_opts` (Q-CGS-24): needs the driver.  The
  thread pool's replacement (Q-CGS-25): the scheduler's plan.
- `darwin-text.c` to `platform/darwin/` (Q-BR-22): needs `as/`.
- Everything owned by WORK-vir, WORK-common, and the front end, `as/`,
  `obj/`, `ld/`, `driver/` and server orders.

## Adoption of common/ facilities

Only after `WORK-common.md` step 5 has merged; one PR per facility, each
proved like step 1 (identical output) plus the ASan suite
(`bench/vcc-ctests.py -j 3 --suite vcc --bin-dir build/ccs-asan`) where an
allocation changes.  Counts are from main at d602dfa2 (grep), before steps
1-3 moved code: find the sites by the old names.

| File (today) | Sites | Facility | Evidence |
|---|---|---|---|
| `codegen/tu.c` | `reserve` 11, `vmreg_get` 32, `malloc`/`calloc`/`realloc` 3 (three idioms) | `vregion` per job | Q-CGS-19, Q-lib-10 |
| `codegen/tu.c` | `now()` 6 | `vclock_seconds` | Q-lib-12 |
| `codegen/tu.c` | `_Thread_local` 6 (grow-only scratch among them) | job regions | Q-lib-19 |
| `codegen/emitimpl.h` | `abort` 1, `now` 1, `reserve` 1 | `VBUG` or `vjob_fail`; `vclock_seconds`; `vregion` | Q-lib-01, Q-lib-12 |
| `arch/a64/codegen/opt.c` (`frame-registers.c` and the switches only) | `grow_to` 5, `malloc`/`realloc` 19 (13 `realloc` in one block), `abort` 6, `_Thread_local` 9 | `VVECTOR_NEED`; `vout_of_memory`; job regions | Q-BR-15, Q-BR-14, Q-lib-02 |
| `codegen/rec/slots.c` | `em_slot_grow` 6 (= `grow_to`), `abort` 1 | `VVECTOR_NEED`, `vout_of_memory` | Q-BR-15 |
| `codegen/rec/slot-values.c` | `realloc` 1, `abort` 1 | same | Q-BR-15 |
| `arch/a64/codegen/func.c` (`finish.c` after step 3) | `abort` 4, `malloc` 1, `now` 2 | `VBUG`/`vjob_fail` (after step 10's `em_too_large`), `vclock_seconds` | Q-BR-14, Q-lib-12 |
| `arch/a64/codegen/a64impl.h` | `abort` 1 ("function too large") | step 10's `em_too_large` | Q-BR-14 |
| `arch/a64/codegen/obj.c` | `hexval` 3 (lower case only) | `vhex_digit_value` | Q-lib-14 |
| `arch/a64/codegen/obj.c` | `reserve` 1 | `vregion` | Q-lib-10 |
| `arch/x64/codegen/obj.c` | `snprintf` 4 | `vformat_unsigned`/`vformat_hex` where it formats one integer | Q-lib-14 |
| `arch/x64/codegen/abi.c` (classification only) | `malloc` 2 | `vregion` | Q-lib-10 |
| `arch/x64/codegen/lower.c` (constructors only) | `malloc` 2, `reserve` 2, unchecked `realloc` in `x64_dbgrow` | `VVECTOR_NEED`, `vregion` | Q-BR-14, Q-lib-02 |
| `arch/a64/codegen/darwin.c` | `idch` 2 | none: the three `idch` sets differ (Q-lib); left | Q-lib-14 |

## Report

Per step: the PR, the proof commands with their results (numbers), what
was not run and why, the files touched outside "Owns" (none expected; any
is a stop-and-report), and what this order got wrong.

## Status

- Step 1: done (PR "codegen: WORK-codegen-surviving step 1").  Deviations
  from the table: `back_edge` (static helper of `kv_edge`, defined in
  `label.c`) moved with `kv_edge` into `branches.c`; `ref_align` stays
  `static` in `memory-access.c` (no caller outside that file today); `kbits`,
  `imm_fits` became `VPRIV` and `ctor_entry` got a prototype, all three in
  `emitimpl.h`; `value.c` keeps a duplicate prototype of `sym_needs_got` (left,
  it is the walker's file).  Nothing was needed in `src/codegen/vir/` or
  `tu.c`.  Note: `bench/gen-walkprefix.py` is `src/tools/gen-walkprefix.py`
  now, and `vcc-ctests.py` takes the binaries from `VCC_BIN`, not `--bin-dir`.
