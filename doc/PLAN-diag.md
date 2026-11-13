# World-class diagnostics for vcc

**Goal:** vcc's error messages should be better than clang's for all C (not just the
kernel), with the best of rustc, Elm, Zig, Swift and GCC 15/16.  Clang is the floor, not
the answer key: we do not need to be byte-identical, and where we can do better we do.
Background and sources: `RESEARCH-diagnostics.md`.

**Rules:**
- **What tools parse is fixed:** the first line `file:line:col: level: text [-Wflag]`,
  clang's `-W` names and groups (`-Wno-x`, `-Werror=x` in build systems), exit status,
  the `-fno-color-diagnostics`/`-fdiagnostics-*` flags clang has.  Text, snippet,
  notes, fix-its and follow-on errors we may do better than clang, even in the default
  mode.
- **Clang as the measuring point:** the same error must be found at the same place (or a
  better one, with a justification); every deviation in text is a deliberate choice that
  shows in a snapshot.
- No cost without an error: `bench/vbench.py gate` (1 %) on every step.  Records are
  built only on the error path.
- Data from clang is fetched once and lives in generated files (like `ppdata*.inc`).
- Each step has a validation line; a step is done only when it is green.

## Starting point (2026-11-09)

| | |
|---|---|
| clang 23 diagnostic ids in total | ~7 300 (3 916 err, 1 770 warn, 1 120 note, 461 ext/compat) |
| of which C (heuristic, `bench/clangdiag.py stats`) | 2 985 (1 468 err, 876 warn, 245 ext, 385 note); ~15 % is C++ leaking in, so ~2 500 |
| vcc | 295 diagnostic calls, 245 distinct formats, 7 `-W` flags |
| coverage (`bench/vcc-diagcov.py`) | 105 clang diagnostics (70 err, 11 warn, 5 ext, 16 note in C); 47 vcc formats say less than clang's (the type or the detail is missing); 78 exist only in vcc (limits, "unsupported", internal errors) |
| `test/diag/` | 66 cases, exactly equal to clang (become snapshots of vcc's own text, D1.2) |
| error recovery | every `expect()` miss and 46 `fatal()` end the TU |
| output | `frontend/diag/diag.c` formats with printf and writes straight to the fd; no record is kept |

## Test strategy (applies to all steps)

Ten layers; each step below says which ones it touches.

1. **Clang as the measuring point** — `vcc-diag.py`: for each case the same exit status
   and the same set of (file, line, column, level, flag) in the first lines as clang;
   the text is not compared.  Deliberate deviations (better location, fewer follow-on
   errors) are listed in the case with a justification
   (`/* diag-differs: ... */`).
2. **Coverage** — `bench/vcc-diagcov.py` (D0.1): clang's C-relevant ids against vcc's;
   lists what is missing, per area, and which ones we have that clang never gives on
   the same case.
3. **Mutation corpus** — `bench/vcc-mutate.py` (D0.2): take valid TUs (`test/vcc`,
   PostgreSQL, the kernel), inject an error (remove a token, swap `;`/`,`, rename an
   identifier, change a type, remove `}`), run vcc and clang.  Measures per mutant:
   - no crash/timeout, exit 1, at least one error;
   - **first error** at the same place as clang's (line, column ±token);
   - **follow-on errors**: number of errors beyond the first, vcc against clang — the
     goal is fewer;
   - an error that clang finds but vcc does not (and vice versa), per mutation type.
   Fixed seed, ~2 000 mutants, < 30 s with `-j 6`.  The numbers are entered into the
   plan per step.
4. **Snapshots** — `test/diag/*.c` with `*.stderr`, `*.json`, `*.sarif` per renderer,
   `--bless` to rewrite; normalized paths.  Inline expectations
   (`// expect-error: REGEX`, `@+N`, rustc/Swift style) for cases where only the content
   matters — the same format as T4 `reject` in TODO.md, so the two are merged.
5. **Round trip** — (a) a Python renderer in `bench/` that builds text output from JSON
   must give exactly the same text as vcc (GCC's `sarif-replay` idea: the record
   contains everything); (b) fix-its: apply, compare with `*.fixed`, recompile without
   that error.
6. **No false positives** — `vcc-sema.py warn` 879/879 on the kernel, the PostgreSQL
   build (`bench/pg/`) with no new warnings against clang; later SQLite and Lua as
   third/fourth projects so that the warnings do not become kernel-shaped.
7. **Performance** — `vbench.py gate`; plus an error-rich case (10 000 errors in one TU
   with `-ferror-limit=0`) that must not become quadratic.
8. **Fuzz** — new target `fuzz-diag`: random spans, labels and fix-its over random
   source text (tabs, UTF-8, CRLF, long lines) through all renderers: no crash, no
   out-of-bounds reads, widths right.  `fuzz-pp` with `FUZZ_PP_PRINT=1` covers the pp
   errors.
9. **Sanitizers** — the diag, mutation and snapshot suites also with
   `--bin-dir build/src-asan`.
10. **Human review** — `bench/vcc-diag-catalog.py`: an HTML page with every case in
    clang's and vcc's text and JSON side by side (Elm's error-message-catalog).
    Changed messages are reviewed there before commit; vcc must win or tie on every
    row.

## Steps

### D0. Measurement and corpus (nothing visible changes)

- **D0.1 clang's table as data.**  *Done 2026-11-09.*  `bench/clangdiag.py fetch` fetches
  `Diagnostic*.td` for Homebrew clang's version into `work/clang-td/23.1.4/` (17 files,
  1.2 MB); `bench/clangdiag.py` parses them (name, class, default level, `-W` group,
  component, category, template, language bucket) in 0.5 s.  `bench/vcc-diagcov.py`
  (1.2 s) matches vcc's formats against the templates in both directions and gives
  coverage, vcc-only and "says less than clang"; `--missing [REGEX]` lists what is
  missing per `-W` group, `-v` everything.  The numbers are in Starting point.  The
  language bucket is a heuristic (name token, category, group, first alternative in
  the message): spot checks show ~15 % C++ in the C bucket.  The text matching is a
  starting point; once the diagnostics have ids (D1.3) the coverage reads them.
  `diagids.inc` for vcc itself is generated in D1.1.
  *Validated:* 7 297 diagnostics (diagtool: 7 298), 830 groups, 20 substitutions.
- **D0.2 mutation corpus** per layer 3.  *Done 2026-11-09.*  `bench/vcc-mutate.py`:
  TUs that both clang and vcc accept (gcc-torture, tcc-tests2, `test/vcc` with
  vcc-ctests' flags, and kernel TUs), one error injected into the main file by eleven
  mutators (`del-semi`, `del-close`, `del-open`, `del-token`, `dup-token`,
  `semi-comma`, `typo-ident`, `typo-keyword`, `change-type`, `insert`, `operator`),
  `-fsyntax-only -ferror-limit=0` with both.  Clang's answers are cached, so a rerun
  runs only vcc (22 s for 1 970 mutants with `-j 6`; cold 112 s).  `show CATEGORY
  [MUTATOR]` shows the cases with both compilers' errors; everything in
  `build/src/mutate/last.jsonl`.
  **Baseline** (seed 1, 1 970 mutants from 1 800 torture and 145 kernel TUs):

  | | |
  |---|---|
  | crashes / timeouts | 0 / 0 |
  | missed (clang rejects, vcc accepts) | 34 |
  | false (clang accepts, vcc rejects) | 20 |
  | both accept (the mutation became valid C) | 304 |
  | both reject | 1 612 |
  | first error at clang's place / same line / elsewhere | 82.6 % / 8.2 % / 9.2 % |
  | follow-on errors compared with clang: fewer / equal / more | 21.3 % / 60.2 % / 18.5 % |
  | errors in total | vcc 2 715, clang 3 505 |

  What the numbers say (grouped per message):
  - **First error elsewhere** (148): almost always a `,` instead of `;` at the end of a
    line.  Clang says "expected ';' at end of declaration / after expression / after
    return statement" on the line (92 cases); vcc says "expected expression" or
    "expected identifier" on the next line.  Clang's heuristic: a `,` followed by a
    line break and a token that cannot continue the expression.  Also `unsigned double`
    (7) is not diagnosed.
  - **Fewer errors** (343) is mostly that vcc stops after the first parse error where
    clang recovers and finds more (D2.2): "expected ')'", undeclared names,
    "conflicting types", "function definition is not allowed here".
  - **More errors** (298): follow-on errors after a misspelled keyword (`cas 61:`,
    `struc S {`, `cha str[]`): clang suggests the keyword ("did you mean 'struct'?")
    and goes on; vcc gives "expected ';' after expression" and undeclared names
    afterwards.
  - **Missed** (34): parameter list without types in a declaration (8), `main` with
    wrong parameter types (4), array element of incomplete type (4), call/definition
    with incomplete return type (7), return type that does not match (2).  In TODO.md.
  - **False** (20): `-Wno-error=int-conversion` / `incompatible-pointer-types` do not
    control vcc (TODO.md "Warnings as errors not controllable"), `-Wreturn-mismatch`
    does not belong to the `return-type` group, and a missing `;` before `}` in a
    struct is a warning in clang ("expected ';' at end of declaration list"), an error
    in vcc.
  *Validated:* three runs in a row give identical numbers (after a fix: cached clang
  answers came as lists, vcc's as tuples).
  **After `,` for `;` (2026-11-09):** clang's two heuristics: a `,` before something
  that cannot start an expression (`{`, `}`, `if`, `for`, `while`, `else`, `goto`, a
  declaration specifier) ends the expression, and `expect_semi` takes `,` or `:` as a
  mistyped `;` (fix-it, parsing goes on); in a declaration a `,` last on the line
  before something that cannot start a declarator is a `;` ("expected ';' at end of
  declaration").  First error at clang's place 82.6 → 88.7 %, elsewhere 9.2 → 3.5 %;
  `semi-comma` 154 of 155 at clang's place (for the last one vcc points at the `,`
  itself, clang at the follow-on error).  More follow-on errors than clang 18.5 →
  17.4 %.  Test: `test/diag/comma_semi.c`.
  **After misspelled keywords (2026-11-09):** the typo correction offers keywords like
  clang (Sema's CTypeSpecs where a type can stand, the statement keywords at the start
  of a statement; `break`/`continue`/`case`/`default` only where they are allowed,
  `bool` only in C23) and rejects, like clang, a correction when the name is shorter
  than three times the distance (`in` does not become `int`, `i` not `u`).  A chosen
  keyword is parsed *as* the keyword (the token id is rewritten): `stati int x;`,
  `struc S s;`, `retur x;`, `(doubl)x`.  Also clang's ParseImplicitInt: after
  specifiers without a type (`static in f(void)`, `const unsigne long *p`) an unknown
  name not followed by `[ ( ) ; , = { :`/`asm` is an unknown type name, and an
  identifier list without types outside a definition (`void exit(in);`) is clang's
  error.  `typo-keyword`: first error at clang's place 145 → 163 of 165, more
  follow-on errors 74 → 7, missed 7 → 1; in total first error at clang's place 88.7 →
  89.7 %, more follow-on errors 17.4 → 13.3 %, missed 34 → 27.  Left in the category:
  cascades on error nodes (`in y[8];` gives "subscripted value is not an array" on
  `y`), `as volatile (...)`; the missed one is a K&R definition against an earlier
  prototype (TODO.md).
  Test: `test/diag/keyword_typo.c` (with `.expect`: vcc shows a fix-it even for
  `(doubl)x`, where clang loses it), `test/diag/kr_decl.c`.  `vcc-diag.py` now takes
  `X.c.expect` where vcc deliberately says more than clang: vcc's text must match the
  file and the diagnostic lines (`file:line:col: level:`) must match clang's.
  **After error nodes (2026-11-09):** clang's invalid expressions and declarations.  An
  expression that has already got its error becomes an error node (`err_node`,
  `NF_ERR`: int 0, lvalue), and the operators (binary, assignment, `?:`, unary, cast,
  index, member, call) give an error node without a new error when an operand is one;
  postfix operators after an error node are only parsed (`postfix_err`).  A declaration
  whose type was wrong (unknown type name, `void` variable, incomplete type, a `void`
  field or parameter) is marked `SF_INVALID` / `field.invalid`, and every use becomes
  an error node.  At the same time clang's first errors that were missing:
  `'void' cannot be signed or unsigned`, `'long struct' is invalid`, `'short double' is
  invalid`, `cannot combine with previous 'X' declaration specifier` (with the earlier
  word; `char int` is now an error as in clang), `'void' must be the first and only
  parameter`, `argument may not have 'void' type`, incomplete parameter and result type
  in a definition.  First error at clang's place 89.7 → 91.0 % (elsewhere 3.5 → 2.4 %),
  more follow-on errors 13.3 → 11.2 %, missed 27 → 23; `change-type` first at clang's
  place 54 → 81.  What remains among the follow-on errors is mostly the parser's
  recovery ("expected X", D2.2).  Performance: `decl_specs` runs for every
  declaration, member and parameter (~100 000 per kernel TU), so the checks sit on the
  faulty token in cold functions without pointers to the loop's variables (`&sgn`
  forced them to memory: +0.4 %); the parameters' type is read once (`KIND`).  fe
  +0.27 % against the previous step; the branch is at +0.91 % against the merge base,
  close to the budget of 1 %.  Test: `test/diag/error_nodes.c`,
  `test/diag/spec_combine.c`.

### D1. Structured diagnostics (the foundation)

- **D1.0 quick wins.**  *Done 2026-11-09.*  The 47 vcc formats that said less than
  clang's: now with the type, the field, expected/given number of arguments, the index
  and the bound, and clang's ranges (`~~~`) and "forward declaration" notes where clang
  has them.  In addition:
  - `-Wexcess-initializers` is a warning as in clang (was an error for the first
    element and silent for the rest), once per list;
  - `_Generic` points at the controlling expression; `'int[2]'` without a space as in
    clang;
  - follow-on errors gone after an invalid unary operand and an invalid vector/scalar
    conversion;
  - the attribute name in "expected string literal as argument of 'section'
    attribute", without a trailing "expected ')'";
  - `invalid operand number 3 in inline asm string (2 operands, 0 labels)`: more than
    clang.
  Left in the list (13): false hits and deliberate choices (vcc's `impossible
  constraint` and `unknown asm operand name` name the operand, clang's do not).
  Findings that belong to D2 or are bugs of their own are in TODO.md ("Designated
  initializers found by PLAN-diag D1.0", "Invalid code accepted").
  *Validated:* `vcc-diag.py` 71/71 (5 new cases: `detail_types`, `detail_fields`,
  `detail_builtins`, `excess_init`, `attr_string`); `vcc-sema.py` batch/run 878/879,
  warn 879/879; `vcc-ctests.py` 1740/1740; `vcc-stress.py` 22/22; `vcc-driver.py`
  41/41; `vbench.py gate` ok (fe +0.29 %, cc +0.24 %).  Coverage: 105 → 136 clang
  diagnostics, "says less" 47 → 13.
- **D1.1 the record.**  `vdiag`: id (clang's name, e.g.
  `err_typecheck_convert_incompatible`; our own with a `vcc_` prefix), level, flag,
  primary span + label, secondary spans + labels, children (note/help), suggestions
  (span, text, *applicability*: machine / maybe / placeholder), macro and include
  context.  Allocated in an arena per TU.  Built with `d = diag_begin(c, ID, loc);
  diag_label(d, ...); diag_fix(d, ...); diag_emit(d);`  `report()`, `cc_error()`,
  `note()` and `pp_diagx()` become thin wrappers, so the ~200 call sites can be moved
  over one at a time.  `diagids.inc` is generated from the `.td` files (only the C
  bucket and the ids vcc uses), like `ppdata*.inc`.
  *Validate:* layers 1 (66/66), 6, 7.
  *Done 2026-11-09.*  `vdiag` in `include/frontend/pp/pp.h`: id, format, flag, message, primary location
  and label, spans with labels, fix-its with applicability
  (`VDA_MACHINE/MAYBE/PLACEHOLDER`), the macro's token, children (notes) and `track`
  (for D1.3).  Built in the TU's `perm` arena with
  `pp_diag_begin/label/fix/note/emit`; `pp_diag_records()` gives the TU's records in
  order.  A note reported after its diagnostic becomes its child; "expanded from
  macro" is placed directly after the diagnostic it belongs to.  The text output reads
  only the record (`render_text`), so JSON/SARIF (D3) get the same content.  `report`,
  `note`, `cc_warnx`, `pp_warnx`, `pp_diagx` and `pp_vdiag` are thin wrappers; the flag
  is in the record, not in the text.  The conversion errors that clang has as warnings
  that are errors by default (`-Wint-conversion`, `-Wincompatible-pointer-types`) and
  `#error`/`#warning` set id and flag themselves.  `diagids.inc`
  (`src/tools/gen-diagids.py`, checked in): every format vcc reports gets clang's name when
  the text matches (like `vcc-diagcov.py`, also `?:` formats) and otherwise `vcc_` +
  the format's words; the record looks the format up (binary search, only on the error
  path).  270 formats, 240 ids, 72 `vcc_*`.  `--check` says whether the table is
  out of date.
  Layer 7 found a quadratic path that existed before the record: every undeclared name
  searched for a typo correction over all symbols (40 000 errors: 7.3 s).  Clang's
  `-fspell-checking-limit` (50 attempts per TU, failed ones count;
  `-fno-spell-checking`) introduced, the same number of corrections as clang; now
  0.19 s.  `vcc-stress.py` has the case (`errors-40k`, budget 3 s).
  *Validated:* `vcc-diag.py` 78/78; `vcc-sema.py` batch 878/879, warn 879/879;
  `vcc-ctests.py` 1740/1740; `vcc-stress.py` 23/23; `vcc-driver.py` 41/41;
  `vccd-test.py` 19/19; PostgreSQL with vcc: 1 504 files, `meson test` 81 ok, 0
  failures; `vbench.py gate` ok (fe +0.94 %).
- **D1.2 the text renderer.**  Today's `vdiagx_`/`snippet` are moved to a renderer that
  takes a record; at first the same look as clang.  `test/diag/` gets vcc's own
  expected text (`*.stderr`, `--bless`) and `vcc-diag.py` switches to layer 1's
  comparison, so that the text can then get better.
  *Validate:* layers 1, 4 + 5a (the JSON round trip proves that the record is
  enough).
  *Done 2026-11-09 (layer 5a with D3.1).*  `render_text` takes a record (D1.1).
  `vcc-diag.py` now compares two things: vcc's own text against `X.c.stderr` (layer 4,
  `--bless` rewrites), and (file, line, column, level, `-W` flag) for each diagnostic
  line plus exit status against clang (layer 1).  A deliberate difference in layer 1
  is declared in the case with `/* diag-differs: ... */`.  `.expect` became `.stderr`.
  The summary also counts cases whose text is still identical to clang's (77 of 78),
  so it shows when the text starts to get better.
  *Validated:* 78/78; a changed snapshot gives FAIL with a diff.
- **D1.3 ids at all call sites** and `-fdiagnostics-show-id` (id after the flag) and
  `VCC_TRACK_DIAG=1` (vcc's own source location per diagnostic, like rustc's
  `-Z track-diagnostics`).
  *Validate:* layer 2: every vcc diagnostic has an id that is in `diagids.inc` or is
  `vcc_*`.
  *Done 2026-11-09.*  The id sits in the call site's format (the record looks it up,
  D1.1); the formats shared by several clang diagnostics (the conversion errors with a
  `%s` prefix, `#error`) set the id themselves, and the `#define` errors got one
  format each.  `gen-diagids.py` now also matches pp's `error()`/`warn()` and has a
  list of pinned ids where the text matching goes wrong
  (`err_typecheck_convert_incompatible`, `fatal_too_many_errors`, pp's `missing ')'
  after`): 303 formats, 268 ids, 75 `vcc_*`.  `-fdiagnostics-show-id` writes ` [id]`
  after the flag.  `VCC_TRACK_DIAG=1` writes ` {vcc file.c:line function}` (with `-g`,
  e.g. the ASan build) or ` {vcc function+offset}`: the report functions save their
  return address in the record, `atos` translates only when it is printed.  Two first
  attempts (macros that passed `__FILE_NAME__:__LINE__` as arguments, or stored it in
  a thread-local at the call) cost pp +0.6 % and +1.6 % respectively (a TLV access is
  hoisted into hot functions on Darwin).
  *Validated:* layer 2 in `vcc-diag.py` (every diagnostic line with
  `-fdiagnostics-show-id` has an id) 78/78; the mutation corpus runs vcc with ids: 81
  different ids over 3 158 errors, 0 without id; `gen-diagids.py --check`;
  `vbench.py gate` ok (pp +0.07 %, fe +0.89 %).

### D2. Error recovery without cascades

- **D2.1 error type and guarantee.**  An error type/error declaration that all checks
  silently accept (rustc's `TyKind::Error`); it can only be created with a token that
  can only be obtained by an error having been emitted (`ErrorGuaranteed`).  A
  `diag_delayed_bug()` that becomes an internal error if the TU ends without errors.

  *Status (done):* the error nodes (`err_node`, `NF_ERR`) and invalid declarations
  (`SF_INVALID`) are the error type; every place that goes silent because an error has
  already been emitted calls
  `delayed_bug(c, loc, what)`, and `delayed_bug_check` at the end of the TU (before
  layout and code generation) gives "internal error: ... (vcc stopped short here,
  expecting an error)" (id `vcc_internal_delayed_bug`) if no error came, instead of
  miscompiling.  `VCC_TEST_DELAYED_BUG` exercises the path.  The mutation corpus: 0
  triggered.
- **D2.2 `expect()` becomes non-fatal.**  Synchronization on `;`, `}` and the start of
  the next declaration; missing `;` after `struct`; unbalanced braces; the 46 `fatal()`
  are gone through one by one.  The goal is **fewer follow-on errors than clang**, with
  the same first error.

  *Status (step 1 done):* a parse error (`fatal`, missing closer) jumps to the nearest
  recovery point: one `_setjmp` per block, struct body, for header and TU (never per
  statement; macOS `setjmp` makes a system call).  There counters and scopes are
  restored, and the rest of the statement is skipped like GCC's
  `skip_to_end_of_block_or_statement` (balanced, `;` in parentheses is skipped, the
  initializers' open `{` are closed first, an `else` after a skipped if goes on with
  the else branch; a `}` directly after `(` or an operator token closes nothing and is
  skipped).  A missing `)`, `]` or `}` is clang's BalancedDelimiterTracker: the error
  (once per place) and "to match this", then up to the closer but not past `;`, and
  on.  After an invalid expression vcc is silent like clang (the call becomes invalid,
  no argument count check; the statement is skipped to `;`).  Clang's own forms:
  "extraneous closing brace ('}')", "extraneous ')' before ';'", "expected identifier
  or '('", "expected function body after function declarator", "expected ';' after top
  level declarator" (the rest of the declaration is skipped if no new one begins),
  "expected member name or ';' after declaration specifiers", "type name requires a
  specifier or qualifier" (`struct { a, b; }`), "cannot combine with previous 'struct'
  declaration specifier" (`int struct S x`), a missing `;` before `}` in a struct is a
  warning, the for header's "expected ';'" skips to the next `;` or `)`.  Found on the
  way: `skip_balanced` skipped only parentheses (an initializer of incomplete type ate
  only its `{`).  The mutation corpus (against 76c0909): first error at clang's place
  91.0 → 91.7 %, more follow-on errors than clang 11.2 → 6.7 %, same 68.0 → 79.4 %,
  fewer 20.8 → 13.9 %; errors in total 2323 → 3006 (clang 3518); spurious 20 → 17; no
  crashes.  `test/diag/recovery.c`: twelve independent errors, exactly clang's output.
  Performance: fe +0.62 % against the merge base (the keyword correction's
  ParseImplicitInt test moved to the loop's exit: −0.22 %).

  *Status (step 2, goal reached):* more follow-on errors than clang 6.7 → 4.8 %, same
  79.4 → 82.3 %, first error at clang's place 91.7 → 93.2 % (elsewhere 3.2 → 1.9 %).
  What got us there: a declaration that fails after its name declares it anyway,
  invalid (a placeholder that a later declaration silently replaces); a parameter list
  without `)` leaves the function declared but invalid, and calls of an invalid
  declaration are silent; `'void' must be the first and only parameter` only when the
  list is parsed (as in clang); invalid specifiers (`signed double`, `void long`,
  `'long struct' is invalid` after a tag) make the declaration invalid, and a bit-field
  or a cast of invalid type says nothing more; too many/few arguments abort the call
  (the arguments are not converted), as does an assignment to a non-lvalue (clang's
  "non-object type 'T' is not assignable" for functions); the comma spreads error
  nodes; an invalid `return` operand is silently skipped to `;`; a missing `;` after a
  prototype before a new declaration is "after top level declarator", not "expected
  function body"; a macro call that reaches the end of the file is removed with its
  name, as in clang (`clang -E` likewise).  `test/diag/recovery_decl.c` new.
  Performance: fe +0.89 % against the merge base (the preprocessor's error path in a
  cold function: without it six lines cost +0.27 % pp).
  Remaining: `f(a; )` (clang's "unexpected ';' before ')'"), interrupted attributes,
  clang's parenthesis sugar in type names (`double (*())`), "expected ';' after struct"
  when a type specifier follows a definition, the 46 `fatal()` one by one.
- **D2.3 duplicates** (`void v;`, compound literal in parentheses, type suggestions
  that cascade: TODO.md "Error recovery").
  *Validate:* layer 3: no crashes, first error at clang's place in ≥ 95 %, follow-on
  errors no more than clang's in ≥ 95 % of the mutants and fewer in total; new
  `test/diag/` cases with several independent errors; T5 stress green; layer 9.

  *Status (done, goals reached):* first error at clang's place 93.2 → 95.3 % (same line
  4.1 %, elsewhere 0.6 %), follow-on errors no more than clang's in 95.2 % (more
  4.8 %), errors in total 3153 against clang's 3518, no crashes.  What got us there,
  all clang's rules: ExpectAndConsume (a missing separator at the end of the previous
  token, with the insertion as a fix-it), BalancedDelimiterTracker for the parentheses
  pairs (sizeof, cast, `_Generic`, typeof, `_Atomic(...)`, `_Alignas`, `static_assert`,
  `switch`: at the token, with "to match this"), built-in functions' `)` at the token
  without a note, "expected '(' after 'if'" (while, for, switch, do/while as 'while')
  at the token, the EOF location (the file's last line break, otherwise the end),
  "expected ';' after struct/union/enum" (isValidAfterTypeSpecifier after a body;
  "struct S" last on the line before a type specifier, in the cold "cannot combine"
  path) with an inserted `;` so that the rest is parsed as in clang, a nameless
  declarator "(int x)" gets the error after `(`, a name in a type name is "expected
  ')'".  The duplicates (`void v;`, compound literal in parentheses, corrected names)
  were already as clang's: `test/diag/dup_followons.c` keeps them there; a
  parenthesized expression's range includes the parentheses (clang's ParenExpr,
  computed first in `rng()`).  New cases: `tag_semi.c`, `cond_paren.c`,
  `paren_close.c`, `dup_followons.c` (82/82, 80 text-identical with clang).  Layer 9:
  diag 82/82, mutations and stress under ASan (`vcc-diag.py --bin-dir`); the ASan
  build gets a 64 MB main stack (its frames are 2-3 times larger, like vccd's 16 MB
  threads), and the constant evaluation checks the stack under ASan.  Bug on the way:
  `in_tyname` is restored at recovery.  Performance: `next()` copies the previous
  token's loc/kind/flags with a 64-bit move (−0.35 % fe), `need_lvalue` and
  `runtime_warn` compute the range (`pp_tok_end`) only when they report (−0.8 % fe);
  the branch as a whole now fe −0.01 %, cc +0.03 % against the merge base (was
  +0.99 %).

### D3. Machine-readable output

- **D3.1 JSON lines** (`-fdiagnostics-format=json`): one record per line, with
  `rendered` (the exact text, so tools never re-render).
- **D3.2 SARIF 2.1** (`-fdiagnostics-format=sarif`,
  `-fdiagnostics-add-output=sarif:file=`): secondary locations, labels, the include
  chain as `locationRelationship`, fix-its as `fixes`, argv and cwd.  Text on stderr
  and SARIF at the same time.
- **D3.3 OSC 8 links** on `[-Wflag]` and id to the documentation (D6) when color is
  on.
  *Validate:* layer 4 (snapshots), 5a; SARIF is validated against the official schema
  (vendored in `bench/`); `vcc-driver.py` for the new flags.
  *Done 2026-11-09.*  `frontend/diag/diagout.c`.  **JSON** (`-fdiagnostics-format=json`): one
  record per line when it is complete (at the next record or the end of the TU), with
  id, level, flag, `werror`, message, location, label, spans, fix-its with
  applicability, include stack, children and `rendered` (the text with its include
  stack).  A location has what the text shows (file, line, byte column) and what is
  needed to recreate it (physical path, byte offset), so `bench/diag-render.py` builds
  vcc's text from the records and the source, without `rendered`.  **SARIF**
  (`-fdiagnostics-format=sarif` instead of text, `-fdiagnostics-add-output=
  sarif:file=F` in addition): rules, results with `ruleIndex`, columns in code points,
  secondary spans and notes as `relatedLocations`, the include chain with
  `isIncludedBy`, fix-its as `fixes`, argv and working directory (`%SRCROOT%`).
  **OSC 8**: with color `-Wflag` links to clang's DiagnosticsReference and the id (with
  `-fdiagnostics-show-id`) to `$VCC_DIAG_DOCS/ID.md` (D6).  vccd takes the diagnostic
  flags (`caps.c`).
  The schema: the network is not used unless the user says so, so
  `bench/sarif-check.py` does the structure check (what SARIF readers rely on: rule
  index, regions, unique location ids, relation targets, fix-its) and validates
  against the official schema (a subset validator) when it has been vendored with
  `bench/sarif-check.py fetch`.
  *Validated:* `vcc-diag.py` 78/78 with layer 5a and SARIF check for every case and
  JSON/SARIF snapshots for four cases (`/* diag-formats */`); `vcc-mutate.py
  --formats`: 1970/1970 mutants where JSON builds exactly the same text and SARIF is
  well-formed; include, macro note, tabs and UTF-8 tried separately; `vcc-driver.py`
  47/47 (six new rows for the flags); `vccd-test.py` 19/19; `vbench.py gate` ok (cc
  +0.99 %: the budget is almost used up).

### D4. Better text output (default)

This becomes the default look, not a mode of its own; the first line keeps the
format.  `-fdiagnostics-style=clang` remains as long as it is cheap, for those who
want to compare.

- Labeled multi-spans in source order (rustc/ariadne), multi-line spans, tabs and wide
  characters right.
- **Nesting** instead of flat notes (GCC 16): macro chains, include stack, "previous
  declaration", parameters that do not match.
- **Contrast color** per side in comparisons (GCC 15) and **type diff**: only the part
  that differs (a `const`, a `*`, a struct tag, a field in a function pointer type)
  is marked.
- **Role names** (Elm): "argument 2 of 'memcpy'", "the return value of 'f'".
- `help:` lines with an action; character set `unicode|ascii`; readable mode (miette's
  "narratable") under `NO_COLOR` without a tty.
  *Validate:* layer 1 (same places as clang), 4, 8, 10.

**Status (2026-11-09): done.**  `-fdiagnostics-style=vcc` (default) | `clang` | `narrated`,
`-fdiagnostics-text-art-charset=ascii|unicode|emoji|none` (unknown value: error, as in clang).
Every main line is clang's in all styles (vcc-driver checks that they are identical).
The vcc style (`frontend/diag/diagtext.c`): several lines per code excerpt, labels (`^` the place, `~`
span, `-` a folded note's place), the rightmost label on the line, the others on `|`
lines below; a note on a line that is already shown (within 5 lines, same file, without
fix-its/help) is folded into the excerpt and keeps its main line; one line in between is
shown, larger gaps become `...`; `= help:` lines (`pp_diag_help`); wide characters/emoji
count as two columns in both styles.
Type diff in color (vcc style only): only the parts of two quoted types that differ are
marked.  Role names: conversion errors on arguments get the label "argument N of 'f'",
return "the returned value".  `help:` on internal errors (delayed bug).  Output is
deferred to the next top-level record/`pp_diag_finish`/`pp_close` so that notes can be
folded in.  JSON's "rendered" follows the chosen style, JSON/SARIF carry `help`.
Deviation from the plan: narrated is a flag, not automatic under `NO_COLOR` (NO_COLOR
only means no colors; screen reader users choose).
Validation: vcc-diag 82/82 (also ASan), 80 cases text-identical with clang in the clang
style, JSON recreates both the vcc and clang styles (`bench/diag-render.py --style`),
diag-formats cases have `.narrated` snapshots; mutate `--formats` 1970/1970; layer 8:
`fuzz-diag` (random records over random text through all styles/outputs, ASan) 85 k runs
without findings, `test/fuzz/regress/diag`; layer 10: `bench/vcc-diag-catalog.py` (HTML:
source, clang, vcc, narrated, JSON).
Performance: the gate fe +0.03 %, cc +0.08 %.

### D5. Content (one substep per area, in value-per-effort order)

Each substep: new cases in `test/diag/` (snapshots, and clang as the measuring point),
layer 6 green.

1. **Missing warnings** from TODO.md (`-Wunused-variable`, `-Wuninitialized`,
   `-Wformat`, `-Wconstant-conversion`, ...) — prioritized with layer 2's table by how
   often clang gives them on PostgreSQL and the kernel.
   *Status (2026-11-09):* the kernel and PostgreSQL are warning-free under clang, so the
   order follows clang's default warnings (without `-Wall`).  **`-Wreturn-type` done:** a
   flow analysis over the AST like clang's CFG (constant conditions pruned, also `x || 1`
   and const variables with a constant initializer; noreturn calls,
   `__builtin_unreachable/trap`; a label is reached only by a reached goto, iterated to a
   fixed point; asm goto; an enum switch that covers all enumerators; indirect goto: no
   answer), with `help:`.  The kernel builds with `-Werror=return-type`: `vcc-sema.py warn`
   879/879 (no false ones; `scoped_guard`/`scoped_cond_guard` required goto tracking and
   tryEvaluateBool folding), PostgreSQL 0 warnings.
   **`-Wconstant-conversion` done (2026-11-10):** integer constants that do not fit in a
   narrower target type at initialization (also members in initializer lists, which were
   not checked at all before), assignment, arguments and return, with clang's ranges
   (unsigned w bits: -2^(w-1)..2^w-1; signed from unsigned source up to 2^w-1; a byte
   within braces -128..255), each `?:` branch separately, location as clang's
   getExprLoc, "expanded from macro" note and `help:` with the type's range.  Bit-fields:
   `-Wbitfield-constant-conversion` and `-Wsingle-bit-bitfield-constant-conversion` (also
   after an explicit cast, at `=` for assignment).  Clang's DiagRuntimeBehavior is
   imitated: nothing in sizeof/typeof or constant expressions (designators, array sizes),
   in functions nothing in dead `?:` branches and `if` branches (at file level clang
   warns there too).  `vcc-sema.py warn` 879/879 (base64.c's byte tables and ilog2 in
   designators required the rules above), PostgreSQL 0, the gate ok (fe +0.40 % against
   the merge base).  Remaining: code after return/break and `while (0)` (clang's CFG
   reachability, TODO.md), typedef names in the message (`'s8' (aka 'signed char')`), the
   floating-point cases (`-Wliteral-conversion`, `-Wimplicit-const-int-float-conversion`).
   **`-Wconstant-logical-operand` done (2026-11-10):** clang's rule (right operand a
   constant ≠ 0, 1; left an integer that is not bool; no enumerator; the operator not from
   a macro), the notes "use '&'" with fix-it and (for `&&`) "remove constant" with a
   removal.  The renderers (vcc, clang, diag-render.py) now mark a fix-it's replaced
   ranges with `~` without blanks at the edges and write no empty line for a pure
   removal, as clang does: the test case is text-identical with clang (82 of 88).
   `vcc-sema.py warn` 879/879, PostgreSQL 0 warnings, the gate ok.
   **`-Wtautological-*` done (2026-11-10):** self-comparison (`-Wtautological-compare`:
   the same variable, member chain or index after implicit conversions, not floating
   point, not from a macro), array/`&object`/function against null
   (`-Wtautological-pointer-compare`, with clang's two fix-it notes for a function) and a
   constant outside the other operand's own type
   (`-Wtautological-constant-out-of-range-compare`, the constant in the comparison's type,
   so `u == -1` is silent; the limit values belong to the disabled
   `-Wtautological-type-limit-compare`).  The renderers follow clang's highlightRange also
   for insertions: the caret is extended to an insertion's column only when no blank is
   adjacent.  The test case tautological.c; clang reports out-of-range warnings in
   reverse order within a full expression (after the expression), vcc in source order.
   `vcc-sema.py warn` 879/879, PostgreSQL 0, the gate ok (fe +0.54 % against the merge
   base; the constant comparison is tried only when an operand is narrower).
   **`-Wformat` done (2026-11-10):** frontend/c/format.c.  `format(printf|scanf, i, j)` (also
   `__printf__`, `gnu_printf`) and the C library's functions according to clang's
   Builtins.inc (not under `-fno-builtin`, which the kernel uses).  The format string is
   checked when it is a literal (also a const variable with a literal as initializer, both
   branches of a `?:` with unknown condition): types against clang's matchesType (sign and
   integer promotion are forgiven, `long` against `long long` is not), fix-it to the
   specification that fits, too few/too many arguments (clang's UncoveredArg: an argument
   is only extra if no branch reads it), `*` for width and precision, positional
   arguments, invalid conversions, undefined flags, `-Wformat-security`.  The literal's
   bytes are mapped back to their source columns by decoding the tokens again (escapes,
   concatenation); if that fails everything lands on the literal's location.  The test
   case format.c is text-identical with clang.  Remaining: typedef names
   (`'size_t' (aka 'unsigned long')`, fix-it `%zu`: vcc has no typedef names in types,
   gives `%lu`), Windows `I64`, checking calls through function pointers with a format
   attribute.
   **Correction:** the earlier lines "PostgreSQL 0 warnings" were read from the wrong log
   (the compiler's output ends up in `work/build-pg-vcc.build.log`).  In the right log
   `-Wtautological-constant-out-of-range-compare` (d9e360e) gave two false warnings (an
   `int` against an unsigned comparison is not a range), now fixed; `bench/pg/build.sh`
   now prints vcc's warnings that clang does not give: 0.  The kernel 879/879, the gate
   ok (fe +0.66 %).
   `-Wunused-*` (done): `-Wunused-variable` and `-Wunused-but-set-variable` when a block
   is closed (in declaration order, as clang's ActOnPopScope; a variable counts references
   minus discarded `=`, `op=`, `++`, `--` directly on the name, as Sema's
   RefsMinusAssignments: `(void)(x = 1)` and the last statement in `({ })` count as a
   read, `volatile` only for `=`), `-Wunused-local-typedef`, and at the end of the TU
   `-Wunused-function`, `-Wunused-const-variable` and `-Wunneeded-internal-declaration`
   (only mentioned in `sizeof`/`typeof` or from its own body).  The attributes `unused`,
   `used`, `cleanup`, `constructor`, `alias` target silence; `inline` and variables in
   headers are not warned about.  An unparsed lazy body may mention what was declared
   before it: then nothing is said (missing warnings, no false ones).  Nothing after an
   error in the function (clang: in the scope).  The test case unused.c is text-identical
   with clang (`diag-flags:` in the test file gives flags to both compilers).  The kernel
   879/879, PostgreSQL 0 warnings that clang does not give, the gate ok (fe +0.77 %; this
   change +0.10 %).  Remaining: unused variants for parameters.
   `#pragma clang/GCC diagnostic` and `_Pragma` (done): push, pop, ignored, warning,
   error, fatal, groups and `-Weverything`, for the preprocessor's and the front end's
   warnings.  The state applies per location as clang's DiagStateMap: each pragma is a
   transition in its file, a file inherits the state where it was entered (saved in fent,
   so also at a header cache replay), and a state that a header changes without a pop
   applies in the one that included it from the `#include` onwards.  A warning given at a
   scope's end follows the pragma before its location.  As clang: `warning` gives a
   warning also under `-Werror` and `-Werror=flag`, `-w` wins over everything.  A header
   with a diagnostic pragma is not recorded in the header cache.  Matrix against clang
   (`-Wall`, `-Werror`, `-Werror=format`, `-Wno-error=`, `-w`) equal; the test case
   pragma_diagnostic.c text-identical.  The kernel's parity 873/873 (batch) and 911/912
   (modpost), vccd-test, PostgreSQL 0 warnings that clang does not give, the gate ok (pp
   +0.12 %, fe +0.81 %).  fuzz-pp found a use-after-free in `raw` (token.c:149) that
   already exists in 96a2f5f: the source from the previous run, probably via the shared
   file cache (frontend/pp/file.c) when the same path gets new contents.
2. **Macros**: expanded text with the caret in it, frames from system headers folded,
   multi-level "expanded from" (TODO.md "Diagnostic details").
   **Status (2026-11-10): done.**  A diagnostic at a token from a macro expansion gets the
   caret at the call (the object macro's name, not `)`) and "expanded from macro" notes
   in several levels, as clang's: cc passes the diagnosed token's spelling (`c->dms`,
   `EXT_WARN_MS`; `bad_operands` the operator), and frontend/diag/diag.c follows it through the
   bodies (`xchain`: a direct hit, otherwise nested calls; the last occurrence first).
   Bodies are canonical copies without a file, so a lazy index over `#define` locations
   (`mdef_body`) finds the definition whose token matches.  Each frame gets a span (a
   function macro's up to and including `)`), the operands' spans are moved to the
   parameter's use in the body (`arg_spans`), and the innermost note gets `help:` with the
   expansion's text (`#`, `##`, variadic).  `-fmacro-backtrace-limit=N` (default 6,
   clang's "skipping N expansions" in the middle); the vcc text writes only the header line
   for middle frames in a chain of ≥ 3 (diag-render.py likewise, JSON unchanged).
   Consequences: `-isystem` without `-I` was not counted as system (`nsys` −1 now means
   none), `##` keeps `PF_MACRO`, the `__COUNTER__` warning gets a span.
   The kernel (kernel/fork.o, `-pedantic`): 1 584 warnings, 1 536 with clang's header, 1 031
   chains identical with clang's; the rest is mostly names built with `##` (`CONCATENATE`), which a
   search by spelling does not follow, and duplicates (`;`) where the wrong occurrence is
   chosen (TODO.md "Diagnostic details").  Clang's own errors we leave alone: a token
   directly after `\` + line break gets the location of the `\`.  New cases:
   `macro_chain.c` (with a system header), `macro_paste.c`.  *Validated:* `vcc-diag.py`
   101/101, `gen-diagids.py --check` (`vcc-diagcov.py` knows `EXT_WARN_MS`), `vcc-sema.py
   warn` 879/879, batch and run 878/879, `vcc-ctests.py` 1740/1740, stress 23 + 24,
   `vcc-mutate.py --formats` 1970/1970, `vcc-diff.py` batch 873/873 and cmp 911/912,
   `vccd-test.py` 19, `vcc-driver.py` 56/56, file-by-file build, fuzz-regress;
   PostgreSQL with vcc: 0 warnings that clang does not give, `meson test` 81 ok.  The
   gate against the merge base: pp +0.24 %, fe −0.42 %, cc −0.38 %.
3. **`static_assert`**: the operands evaluated (`48 == 56`), and for `sizeof` a layout
   note with where the padding came from.
   **Status (2026-11-09): done.**  The false term in an `&&` chain is reported (clang's
   findFailedBooleanCondition), an integer literal gives only "static assertion failed:
   msg", "expression evaluates to" is omitted when both operands are literals (as
   clang).  New: for `sizeof(struct X)`/`sizeof(T)` in the condition a note at the
   record, "'struct hdr' is 24 bytes (alignment 8)", with labels "7 bytes of padding
   after this" / "... at the end" and `help: ordering its members by decreasing
   alignment makes it 16 bytes` (`test/diag/static_assert_layout.c`).
4. **Include tips**: `memcpy` → `<string.h>` as a fix-it, from a generated libc/POSIX
   table.
   **Status (2026-11-09): done.**  `frontend/c/libfuncs.inc` (generated by `gen-libbuiltins.py`
   from clang's Builtins.inc: 354 library functions with header) — a call to an
   undeclared one is declared with the library's type as clang does, with clang's text
   ("call to undeclared library function 'memcpy' with type ...", in C89 the warning
   "implicitly declaring library function") and the note "include the header
   <string.h> ...".  `frontend/c/libnames.inc` (by hand, 173 names: types, macros, POSIX
   functions that clang lacks) gives `= help: 'size_t' is declared in <stddef.h>`.
   Both get `#include <h>` as a fix-it after the file's last `#include`
   (`pp_include_point`), in the vcc style drawn as a patch line `8 + #include
   <stddef.h>`.  "size_t n = 1;" / "FILE *fp;" in a block is parsed as a declaration of
   invalid type: no follow-on errors on `n` (clang reports every use).  The clang
   style now follows clang's LastLoc rule (no excerpt again at the same place).
   Mutants: errors in total 3153 → 3105 (clang 3519), first error at clang's place
   unchanged 95.3 %.
5. **Standard version**: C23 keywords, `-std=c11` against C23 features, GNU extensions
   under `-pedantic`.
   **Status (2026-11-10): done (except the macro location, see D5.2).**  The reference
   is Homebrew clang 23 (`vcc-diff.py`'s CL), not Apple's clang 21 in PATH: there
   `-Wincompatible-pointer-types` is only a warning.  A C23 keyword before C23 (`bool`,
   `true`, `nullptr`, `constexpr`, `static_assert`, `typeof` under strict `-std=c11`,
   `alignas`, `thread_local`, ...) gives clang's error with a `help:` line about what
   was spelled earlier (`_Bool`/`<stdbool.h>`, `NULL`, `_Static_assert`, `__typeof__`);
   strict `-std=cNN` does not have `typeof`/`asm` as keywords (clang).  `-pedantic`,
   `-pedantic-errors` (`[-Werror,-Wflag]` as clang) and `-Werror=GROUP`/
   `-Wno-error=GROUP` for groups.  Clang's ext_* warnings, off by default: GNU statement
   expression (and `-from-macro-expansion`), `?:` without the middle, empty struct/union
   and without named members, zero-length array, case ranges (`-Wc2y-extensions`),
   `&&label` and `goto *`; the check costs one cached test per flag (`EXT_WARN`).  C23
   `auto` deduces the type (like `__auto_type`, also `static auto`; `auto int` is the
   storage class), without an initializer clang's error; implicit int has C23's text.
   New default warnings that were missing altogether:
   `-Wincompatible-pointer-types-discards-qualifiers` (the first level and "in nested
   pointer types") and `-Wpointer-sign` (also clang's text for plain `char`), the type as
   clang shows it (the array before decay); `-Wint-conversion` gets clang's "; remove
   &", "; dereference with *" and "; take the address with &" with a fix-it when that
   gives exactly the target type.  "illegal storage class on file-scoped
   variable/function" was missing (the declaration becomes invalid), a `_Generic` with
   no match and a `static_assert` on an invalid condition say nothing more, and the note
   for a nameless parameter is placed where the name would stand.  The most common of
   clang's `-pedantic` warnings in the kernel (`WFLAGS=-pedantic HIST=20 vcc-sema.py
   warn`) now exist too, with clang's locations: `typeof`/`asm` "extension used"
   (`-Wlanguage-extension-token`, 6.35 M in the kernel), extra `;` outside functions and
   in struct/union (`-Wextra-semi`), binary literals, `{}` and enumerators outside `int`
   (`-Wc23-extensions`), `__COUNTER__` (`-Wc2y-extensions`), named variadic macros
   (`-Wvariadic-macros`), forward reference to `enum`, `sizeof`/`_Alignof` on void and
   function type (`-Wpointer-arith`), arithmetic on `void *` (`-Wgnu-pointer-arith`) and
   `return f();` in a void function.  PostgreSQL found an error from D5.8: in an
   `overloadable` set (tgmath.h's `__tg_promote(...)` is `unavailable`) only the member
   the call selects is now checked; the note for `unavailable` is at the declaration's
   name and for `deprecated` at the attribute, as clang.  Test cases c23_words.c,
   c23_auto.c, pedantic.c, pedantic2.c, ptr_quals.c, int_conv_hint.c (text-identical
   with clang except vcc's own `help:`), attributes.c (overloading, fields).
   *Performance:* `ext_on` is inline: one test as long as no flag in `ext_flags` (or the
   group) is mentioned on the command line and no `#pragma ... warning/error` has been
   seen (pp counts them, `nden`; the kernel's `__diag_ignore` is "ignored" and does not
   turn off the fast path).  pp's hot warnings (`__COUNTER__`, each `#define` of a
   variadic macro) have the same cache (`pp_wc_maybe`); without it the `__COUNTER__`
   check cost pp +8 % (the kernel's `__UNIQUE_ID`).  A profile (xctrace) also showed
   that `lazy_capture` copied each token's spelling with `memcpy` via a Darwin stub:
   short spellings (≤ 3 bytes) are now copied directly.  The gate against the merge
   base: pp +0.23 %, fe −0.46 %, cc −0.41 % (was fe +1.66 % before the copying; against
   the previous commit fe +0.67 % before it).  *Validated:* `vcc-diag.py` 99/99,
   `gen-diagids.py --check`, `vcc-sema.py warn` 879/879 (no warnings that clang does not
   give), batch and run 878/879, `vcc-ctests.py` 1740/1740, stress 23 + 24 (vccd),
   `vcc-mutate.py --formats` 1970/1970, `vcc-diff.py` batch 873/873 and cmp 911/912
   (modpost.c), `vccd-test.py` 19, `vcc-abi.py`, fuzz-regress, file-by-file build;
   PostgreSQL with vcc: 1 504 files, 0 warnings that clang does not give, `meson test`
   81 ok.  `vcc-sema.py warn` got `WFLAGS=` (extra flags to both) and `HIST=N` (clang's
   diagnostics that vcc lacks, per flag).  Remaining: `_BitInt` (TODO.md C23), clang's
   warning for digit separators before C23 (the lexer), `-fdiagnostics-parseable-fixits`
   (D6), a statement expression from a function macro is reported at `)` instead of the
   macro name (pp gives the expansion's token the location of `)`; D5.2).
6. **Portability**: Linux against Apple (`long double`, variadic arguments, `char`
   signedness).
   **Status (2026-11-10): done.**  vcc has no typedef sugar in the types (cc.h), so the
   declaration saves the typedef it was written with (`sym.tdef`, `field.tdef`, from
   `dspec.tds`; only when the type is exactly that one), and `expr_tdef`/`tyname_sugar`
   (layout.c) give clang's "'int64_t' (aka 'long')" for variables, parameters, members
   and calls.  -Wformat uses it, with clang's `%zu`/`%td`/`%jd` fix-its for `size_t`,
   `ptrdiff_t`, `intmax_t`, and `help:` for `[u]int{,_least,_fast}64_t` ("'long' on Linux
   and 'long long' on Apple: PRId64 from <inttypes.h> fits both").  A format argument's
   location is the start of the expression (the member's base, the call's function), as
   clang.  The range help for plain `char` says that the signedness is the target's
   (unsigned on Arm/RISC-V Linux, signed on Apple and x86).  `-Wconstant-conversion`
   took plain `char` as -128..255 on both targets: now the target's signedness (Apple:
   `char c = 200` warns as in clang), and a hex/octal/binary literal that fits as
   unsigned does not warn (clang: `0xfe` in a `signed char`, `(0xffff)` in a `short`;
   it was wrong on Linux too).  New `-Wliteral-range` (clang's text and limits for
   float, double, binary128 and Apple's `long double`), with a help when the value would
   have fit in Linux's 128 bits; a failed `_Static_assert` that mentions `long double`
   says the size on both targets.  Variadic arguments: `va_list` is a struct on AAPCS64
   Linux and `char *` on Apple, so assignment and copying work on both; nothing to say.
   New cases: `portability.c`, `portability_apple.c` (`--target=arm64-apple-macos`).
   Remaining: sugar for pointers (`'uint64_t *'`) and explicit conversions
   (`(int64_t)x`).  *Validated:* `vcc-diag.py` 103/103, `gen-diagids.py --check`,
   `vcc-sema.py warn` 879/879, batch, run and layout 878/879, `vcc-ctests.py` 1740/1740,
   stress 23 + 24, `vcc-mutate.py --formats` 1970/1970, `vcc-diff.py` batch 873/873,
   `vccd-test.py` 19, `vcc-driver.py` 56/56, `vcc-abi.py` 7, file-by-file build,
   fuzz-regress; PostgreSQL with vcc: 0 warnings that clang does not give (before the
   literal rule 7, all `0x..` in `char`), `meson test` 81 ok.  The gate: pp +0.24 %, fe
   −0.18 %, cc −0.17 %.
7. **UB in constant folding**: shift ≥ width, signed overflow, with a line about why.
   **Status (2026-11-09): done except static initializers.**  `-Winteger-overflow`
   (clang's location and text, the value with digit groups `-2'147'483'648`) for signed
   `+ - *` and unary `-` of constants, cheaply gated (`small_const`), with `help:` about
   the type's range; shifts get `help: 'int' is 32 bits wide ...`.  `vcc-sema.py warn`
   879/879 (no false ones on the kernel).  Remaining (TODO.md "Invalid code accepted"):
   `-fsyntax-only` lacks "initializer element is not a compile-time constant" (the
   check is done in codegen/data.c).
8. **Attributes**: `format`, `nonnull`, `warn_unused_result`, `deprecated("...")`.
   **Status (2026-11-10): done.**  frontend/c/attrcheck.c.  `nonnull` (without index: every
   pointer parameter; with index; on a parameter) gives `-Wnonnull` for a null argument
   (clang's CheckNonNullExpr: a constant that is false as a condition), at clang's
   location (the call's `)` for the function's plain `nonnull`, otherwise the argument).
   `warn_unused_result` and `nodiscard` (with message) give `-Wunused-result` for a
   discarded call.  `deprecated` and `unavailable` (with message) on functions,
   variables, typedefs, tags, enumerators and members give `-Wdeprecated-declarations`
   and an error respectively, with the note "has been explicitly marked deprecated
   here"; nothing inside a function that is itself deprecated.  The warnings that clang
   gives via DiagRuntimeBehavior (constant-conversion, division-by-zero, shift-count,
   tautological-constant-out-of-range, array-bounds, nonnull) are now queued to the end
   of the function as in clang (`rt_emit`), so the order against immediate warnings is
   right.  The ranges follow clang: the conversion's context (the name, `=`, the callee,
   `return`) is marked, overflow marks the operands.  The rare part of the attributes is
   in a side table (`attrx`, `attrs.ext` in the unused `flags` slot) so that `attrs`
   stays at 64 bytes, and is part of the declaration cache (its location is moved like
   the others).  The test case attributes.c is text-identical with clang;
   const_conversion/const_ub rewritten (new ranges, as clang).  The kernel 879/879,
   PostgreSQL 0 warnings that clang does not give, vccd-test, the gate ok (fe +0.96 %
   against the merge base; this change +0.18 %: the parameters' nonnull is searched only
   when there is any, the callee's location is computed once per call).

### D6. Fix-its, `--fix` and `--explain`

- **Swift's rule**: a fix-it on the error itself only if it is the only obvious one;
  others as notes, safest first.  Applicability on each suggestion.
- `-fdiagnostics-generate-patch` (all fix-its as a unified diff) and `vcc --fix` that
  applies only machine-safe ones.
- `vcc --explain ID`: `doc/diag/ID.md` with a wrong and a fixed example,
  compiled into a generated table (no files at run time).  Code only when the
  explanation adds something (rustc's rule).
  *Validate:* layer 5b; every doc example is tested in CI: the wrong one gives exactly
  that id, the fixed one nothing; `--fix` on PostgreSQL's warnings gives a build that
  passes `meson test`.

**Status (2026-11-10): done.**  Each fix-it has an applicability (`pp_fixit.app`:
`machine`, `maybe`, `placeholder`; in JSON and SARIF).  Machine-safe: `.` ↔ `->`, `,`/`:`
instead of `;`, removal of an extra `;` (`EXT_WARN_DEL`) and of an ignored format flag,
`%zu`/`%td`/`%jd`, and a missing `;` when the next token starts a new line (and is not
`{`); guesses (`maybe`): `==`, parentheses around assignment, `&`/`*`, spelling
suggestions, "expected X", a `;` in the middle of a line, an extra `)`/`]`.  A fix-it whose
range lies entirely inside a macro name is dropped (the change belongs to the macro's
definition).  `-fdiagnostics-parseable-fixits` writes clang's
`fix-it:"file":{l:c-l:c}:"text"` (in all three styles); on `test/diag` the lines differ
from clang's only where the suggestions deliberately do (include suggestions, keyword
spelling, clang's incorrect `sizeof` suggestions).  `-fdiagnostics-generate-patch` writes
all suggestions (the record's own, otherwise its first note's) as a unified diff, `vcc
--fix` writes the machine-safe ones to the files (a temporary file that is renamed, the
permissions are kept, never system headers) and says "applied N fix-its".  `vcc --explain
ID` (also the `[ID]` form from `-fdiagnostics-show-id`; without ID: the list) writes
`doc/diag/ID.md`, compiled in via `src/tools/gen-explain.py` → `driver/explain.inc`;
12 pages for the most common diagnostics.  vccd runs `--explain`/`--fix` itself with the
client's fds (`vcc` via the shim gives the same file as `vpp --fix`).  *Validated:* layer
5b in `vcc-diag.py` (`/* diag-fix */`: `--fix` is repeated until nothing more is applied,
at most 4 times, compared with `X.c.fixed`, and clang gives fewer errors afterwards),
`vcc-explain.py` 12/12 (the wrong example gives the id, the fixed one neither the id nor
an error), `vcc-mutate.py --fix`: applied to 292 of 1644 rejected mutants, the original
restored in 268, no error left in 280, more errors than before 0.  PostgreSQL has no
warnings to fix (0 under clang), so the mutation corpus validates `--fix` instead of
`meson test`.  Otherwise green: `vcc-diag.py` 105/105, sema warn 879/879, batch/run
878/879, ctests 1740/1740, stress 23 + 24, `--formats` 1970/1970, `vcc-diff.py` batch
873/873, `vccd-test.py` 19, `vcc-driver.py` 56/56, file-by-file build, fuzz-regress,
PostgreSQL 0 warnings and `meson test` 81 ok.  The gate: a call first in `ppd_run` cost
pp +3.7 % (the registers in the token loop), so `--explain` sits in a wrapper around it
(`ppd_run1`): pp +0.26 % against the merge base.

### D7. The whole build in vccd

- The same warning from a header is written once with "(also in N other TUs)"
  (`-fdiagnostics-dedup`, opt-in; the default is clang's behavior).
- Records are kept per TU, so a rerun replaces exactly that TU's set; summary at the end
  of the build (per flag, per file).
- The same records out over vccd's socket for an LSP/editor.
  *Validate:* `vccd-test.py` cases (dedup, replacement, socket); the kernel build with
  `-Wall`: number of duplicates printed before/after in the plan; layer 7 on the whole
  build.

**Status (2026-11-10): done.**  First a gap: vccd wrote a successful TU's diagnostics to
nothing (fd −1, or its log with `VCC_LOG`), so vcc's warnings were never seen under vccd.
Now they go to a temporary file per thread (`cc_own`) and to the client's stderr when the
TU succeeds (a TU that vcc fails on goes as before to clang, which reports); colors as for
the client's terminal (`ppd_env.errtty`).  `server/dstore.c`: pp gives each record at the
end of the TU to a `pp_dsink` (`pp_drec`: level, flag, file, the JSON line without
`rendered`), and the store keeps them per source file, so a recompilation replaces exactly
that TU's set (a TU that goes to clang removes its own).  `vcc --vccd-diagnostics` (summary
per flag and per file) and `=json` (each TU's records as JSON lines with `"tu"`) ask over
the socket; a query does not belong to any build (it does not end the one it asks about).
JSON is kept for at most 500 records per TU and 64 MB in total, beyond that the records are
counted and the TU gets a line `{"tu":..,"omitted":N}`.  Dedup: with `VCC_DIAG_DEDUP=1` in
the client's environment (an environment variable instead of `-fdiagnostics-dedup`: clang,
which gets the TUs that vcc fails on, does not know the flag) a warning in an included file
is left out if another TU in the same build (make's process group) has already written it
(key: file, line, column, flag, text), and the line "N warnings generated." is followed by
"M header warnings left out: printed by another TU of this build."; the summary gives the
build's count.  vccd cannot know the end of the build (make says nothing), so the summary
is given on request instead of at the end.  *Measured (the kernel, allnoconfig + fragment,
`-j 6`):* `W=1` gives 15 warnings (`-Wunused-but-set-variable`, all in .c files), identical
with clang's on the same command lines; `W=2` 38, no duplicates from headers (vcc still
lacks `-Wshadow`, `-Wsign-compare`); `KCFLAGS=-pedantic`: 1 232 485 warning lines and a
1.44 GB log without dedup, 284 030 written and 947 298 left out (−77 %) and 311 MB with.
vccd in that build: CPU 94.5 s (D6) → 93.5 s, peak RSS 1 132 → 1 238 MB (the store; before
the limits 4.2 GB).  *Validated:* `vccd-test.py` +3 cases (the warning reaches the client;
dedup once per build, a new build starts over; records per TU are replaced, summary,
`omitted`, malformed query), `vccd-cc.py` under TSan without reports (it found three shared
buffers that vccd's threads wrote to at the same time: the type names in
`tyname`/`tyname_sugar` and in -Wformat, now per TU and per check respectively; a
thread-local buffer cost fe +0.8 % on macOS, `tlv_get_addr` per call), `vcc-diag.py`
105/105, sema warn 879/879 and batch 878/879, ctests 1740/1740, stress `--vccd` 24,
`--formats` 1970/1970, file-by-file build, PostgreSQL through vccd (`build.sh vccd`, where
the warnings now reach the build) 0 warnings that clang does not give and `meson test` 81
ok.  The gate against the merge base: pp +0.25 %, fe −0.29 %, cc −0.23 %.

### D8. Later

Flow paths for `-Wuninitialized`, returned stack address and missing `return` (numbered
steps, the GCC analyzer); memory diagrams for out-of-bounds array indexing; HTML output.

Substeps (2026-11-10), in value-per-effort order:
1. **`-Wreturn-stack-address`** (clang's default warning, missing altogether): `&local`, a
   local array that decays, `&parameter`, `&a[i]`, through `?:` and casts; clang's text,
   plus a note at the declaration and `help:` about the lifetime.  *Validate:* diag cases,
   sema warn 879/879.
2. **The path for `-Wreturn-type`**: numbered steps from the function's start to `}`
   (which condition is false, which `case` breaks), from the existing flow analysis.
3. **Memory diagram for `-Warray-bounds`**: the array's elements and the index as text art
   (GCC's `-fdiagnostics-text-art`), in the vcc style.
4. **HTML output**: `-fdiagnostics-add-output=html:file=F`, the records with source
   excerpts as a standalone page.
5. **`-Wuninitialized` and `-Wsometimes-uninitialized`** (`-Wall`): a definite-assignment
   analysis over the statement tree for scalar locals whose address is never taken;
   clang's texts and notes, and the path as numbered steps.  *Validate:* clang's cases in
   diag tests, the kernel (`-Wall` on every command line) and PostgreSQL without false
   ones.

Status D8.1 (2026-11-10): `-Wreturn-stack-address` (local, parameter, compound literal,
through `?:`, casts, `&a[i]`, decay; register variables are skipped) with a label at the
declaration and `help:` about the lifetime; `&register` is an error.  On the way
`-Wunused-value` and `-Wunused-comparison` (TODO "Missing warnings"): clang's
`isUnusedResultAWarning` (comparisons with the `=`/`|=` note, `(void *)x`, `pure`/`const`
calls, the comma's left operand, the `(x = …, 0)` idiom, volatile, statement expressions),
deferred to the end of the function and silent in dead code; clang's columns (the member
name, `]`, the operator).  "From a macro body" is the token flag `PF_MACRO` for
function-like macros (vcc gives the body's token the call's location) and a location on a
macro name for object-like ones (their token gets no flag: setting it in pp's hot path
cost pp +1.6 %); an expression after an error in the same statement does not warn (the
error paths' placeholder `0`).  Character literals got the wrong location (the next
token's).  *Validated:* the diag cases `return_stack.c` and `unused_value.c` (identical with
clang), sema warn 879/879, kernel parity 873/873.

Status D8.2 (2026-11-10): "… in all control paths" shows a path to `}` as numbered labels:
the condition that is false or true, `switch` without a matching `case`, which `case` it
jumps to, `break` that leaves a loop or `switch`, the loop condition that ends the loop.  A
second walk over the statement tree (`rp_walk` in `func.c`) with the existing `stmt_flow`
for the subtrees; runs only when the warning is given, linear in a block's length (20 000
`case` 0.13 s).  Paths through `goto` are not given.  *Validated:* the diag case
`return_path.c`, `return_type.c` with the new steps.

Status D8.3 (2026-11-10): `-Warray-bounds` draws the array's cells (`[0] [1] … [n-1]`, at
most four), the index's cell outside them with `...` in between, the array's size (`'a': 4
x 'int', 16 bytes`) and which bytes the index reaches ("8 bytes past the end", "just past
the end", "before the start"); ASCII or Unicode according to
`-fdiagnostics-text-art-charset`.  The picture is a new part of the record (`art`): the vcc
style shows it under the source lines, JSON and SARIF (`vcc/art`) carry it, `diag-render.py`
recreates it; the clang and narrated styles do not show it.  Clang's note "array 'a'
declared here" is now given for members too (at the start of the declaration).
*Validated:* the diag case `bounds_art.c`, `runtime_warnings.c` with the pictures, JSON
recreates the text.

Status D8.4 (2026-11-10): `-fdiagnostics-add-output=html:file=F` writes a standalone page
(light and dark theme, no external resources): a summary and each record as the vcc style
shows it, with source lines, labels, notes, `help:` and pictures, the color codes as
`<span>` and the flags' OSC 8 links as `<a>`.  Built in `pp_diag_finish` from the record
list, like SARIF.  *Validated:* `vcc-mutate.py --formats` checks each mutant's page:
well-formed, one section per record and the same text as the text output.

Status D8.5 (2026-11-10): `-Wuninitialized` and `-Wsometimes-uninitialized` done here
(`frontend/c/uninit.c`), with clang's rules: scalar local variables (integer, enum, floating
point, pointer) that never have their address taken, one warning per variable, after
`-Wreturn-type`.  One state per variable (uninitialized, maybe, initialized) along the
statement tree; each edge that leaves a variable uninitialized is marked (`if`, `&&`, `||`,
`?:`, a loop's exit, `case`, `default`), and the reason is given only when every path from
the edge reaches the use.  Loops are run twice (first silently), a backward `goto` or `asm
goto` silences the function.  Clang's notes and fix-its (` = 0`, ` = 0.0`, ` = '\0'`,
` = NULL`, remove the condition) are given; the vcc style draws the edge and the use in the
same excerpt.  `(void)x`, `va_start`/`va_copy`/`va_end` and compound literals are read as
clang does (three false alarms on the kernel fixed).  The branch
`claude/review-remaining-errors-acd5c5` has its own `frontend/c/flow.c` for the same warnings and
overlaps D8.1/D8.2: a merge picks one of them.  *Validated:* the diag cases `uninit.c` (37
cases, same headers and fix-its as clang), `uninit_flow.c` and `uninit_pragma.c`;
`vcc-sema.py warn` 879/879; PostgreSQL without warnings that clang does not give.
*Measured:* the analysis runs on every function with tracked local variables, also the main
files' inline functions: the gate fe +0.95 %, cc +0.86 % (budget 1 %).  Two
`pp_diag_wlevel` per function alone cost 3.2 %; the flags' answers are now saved once per
TU (per location only when `#pragma clang diagnostic` exists).  A loop's silent first
iteration is run only when the loop writes a variable that is uninitialized at the entry
(`defs`).

## Order and milestones

| milestone | steps | visible to the user |
|---|---|---|
| M1 | D0, D1 | the type/detail in 47 messages (D1.0); measurable: coverage and mutation baseline |
| M2 | D2 | fewer aborted TUs, several errors per run |
| M3 | D3, D4 | JSON/SARIF, new default text |
| M4 | D5.1–D5.4, D6 | warnings, macro view, fix-its, `--explain` |
| M5 | D5.5–D5.8, D7 | the rest of the content, vccd dedup |

D1 and D2 belong together (the error type needs the record); D0.2 first so that D2 has a
baseline.

## Status

- [x] D0.1 — 2026-11-09
- [x] D0.2 — 2026-11-09
- [x] D1.0 — 2026-11-09
- [x] D1.1 – D1.3 — 2026-11-09
- [x] D2.1 – D2.3 — 2026-11-09
- [x] D3.1 – D3.3 — 2026-11-09 (the official SARIF schema not vendored)
- [x] D4 — 2026-11-09
- [x] D5.1 – D5.8 — 2026-11-10 (D5.7 except static initializers)
- [x] D6 — 2026-11-10
- [x] D7 — 2026-11-10
- [x] D8.1 – D8.4 — 2026-11-10
- [x] D8.5 — 2026-11-10 (the branch `claude/review-remaining-errors-acd5c5` has a version of its own)
- [x] Merge with main — 2026-11-10: where both branches had a version of their own, the
  one that matched clang best per warning won (measured against clang on test/diag and
  gcc-torture/tcc/vcc): main's `flow.c` (-Wuninitialized, -Wreturn-type), `fmt.c`
  (-Wformat), unused-value/-variable, deprecated/nonnull/warn_unused_result,
  conversion warnings and integer-overflow; the branch's engine, recovery, include
  tips, pedantic/C23, tautological comparisons, bounds picture and the return path's
  steps were kept.  The branch's unavailable, deprecated members, printf without a format
  attribute, "; remove &"/"; take the address with &", -Wnonnull at the end of the
  function and pragma-controlled -Wuninitialized were put on main's code.  Remaining:
  TODO.md "Merged diagnostics".  Merged against main: errors 307 against 221 of clang's
  313, format 106 against 81, no extra warnings where main has any; gate against main: cc
  −1.3 %.
  The second merge (main's 16 new commits): main's `fmt_attr` (checks the prototype as
  clang does) replaced the branch's; main's multi-line snippets (clang's maybeAddRange, at
  most 16 lines) were moved to the branch's `vdiag`, but only for ranges without a label
  (the branch's labels are not clang ranges), and the same logic is now in
  `bench/diag-render.py`.  `sometimes_uninit_do2.c` and `uninit_jump.c` got `diag-flags`
  (without them neither compiler warned); 182/182, 162 text-identical with clang.
