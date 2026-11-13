# Design: renaming struct fields with vcc's own front end

Status: design, 2026-11-13.  None of it is implemented yet.

The owner's decision (`PLAN-quality.md`, track D) is this: struct fields are
renamed with a mode of vcc's own front end that reports every use of a given
field with its location, and `src/tools/rename-table.py` applies the edits.
No clang tooling is used.

Why: `rename-table.py` replaces whole words, so it cannot rename a field
named `n`, `len`, `cur` or `err` without also rewriting every local, every
other struct's field and every comment that uses the same word.  The
assembler's rename (PR #166) applied 82 of its 135 field rows and left 53
(`o`, `dir`, `cur`, `prev`, `err`, `off`, `p`, `n`, ...; see
`doc/quality/WORK-as-obj.md`).  The pp, diag, cc, vir, codegen and ld tables
have the same problem.

There are three pieces, each its own PR (the step list is at the end):

1. a front-end mode, `VCC_FIELD_USES`, that prints one line per use of the
   selected fields, with the bytes to edit (`src/frontend/c/field-uses.c`);
2. a driver, `src/tools/field-uses.py`, that runs the mode over every
   translation unit of vcc in several configurations and merges the reports;
3. `rename-table.py --fields`, which checks the rows, applies the edits by
   byte offset and lists what is left for a human.

## 1. The front-end mode

### 1.1 How it is switched on

It is a test aid, not an option: the environment variable
`VCC_FIELD_USES=FILE`.  `job_compile` (`src/driver/job.c`) reads it with
`ppd_getenv(env, "VCC_FIELD_USES")`, so the value comes from the job's
environment, as the decisions table's "Environment switches" rule requires
for test aids.  When the registry of test aids (`WORK-driver-server.md`
step 3) lands, the name goes into it.  Unless the variable is set, the mode
changes nothing a user sees.

`job_compile` refuses the job with one error line, before the front end
runs, when:

- the job is not `-fsyntax-only` (`!job->syntax || job->emit || job->layout`;
  `-S` also sets `syntax`).  The error is
  `vcc: error: VCC_FIELD_USES needs -fsyntax-only`;
- `VCC_EAGER`, `VCC_NODC` or `VPP_NOHC` is missing from the process
  environment.  The error is
  `vcc: error: VCC_FIELD_USES needs VCC_EAGER=1 VCC_NODC=1 VPP_NOHC=1`.
  Each of the three hides tokens from the parser:
  - a lazy body (`lazy_ok`, `frontend/c/lazy-body.c`) of a header's unused
    static function is never parsed;
  - a declaration-cache hit (`driver/ccsnap.c`) restores declarations
    without parsing them;
  - header-cache tokens (`frontend/pp/header-cache-serve.c`) point into the
    shared cache region, not into the file text, so their spelling position
    is unknown.

  All three are read once per process with `getenv`, so the mode cannot
  switch them off for one job; requiring them makes sure no token is hidden.
  Under vccd they are normally not set, so the job is refused there.  The
  mode runs with `vpp`, one process per TU.

Otherwise `job_compile` sets a new member of `cc_opts`,
`const char *field_uses` (the path of the selection file).  It goes after
`outfd`, so that it is not part of the declaration-cache key (the members
before `outfd` are).  `cc_run` passes it to `cc_field_uses_begin`.

`vpp --batch` is not supported: its jobs write to `/dev/null`
(`bin/vpp/vpp.c`), and a batch shares the caches the mode needs off.

**Open question for the owner:** should this become a command-line developer
option such as `--vcc-layout-checks`, for example `--vcc-field-uses=FILE`?
Once the decisions' option table lands, an option is visible in the table
and in `--help`, so that is not decided here.

### 1.2 What selects fields

The selection file has one entry per line, and `#` starts a comment:

```
OWNER.FIELD [NEW]
OWNER.*
```

- `OWNER` is the owner name defined below.
- `FIELD` is the field's current name.
- `NEW` is the name the field will get.  It is optional and only used for
  the macro check in 1.5.
- `OWNER.*` selects every field of the owner.

A line is split at its last `.`, because owner names can contain dots.

The owner of a field is named by the first of these rules that applies:

1. **Tagged record:** the record's tag.  `struct as_context` gives
   `as_context`.  Struct and union tags share one namespace in C, so the
   keyword is not part of the name.
2. **Untagged record with a typedef:** its first file-scope typedef name
   (`tag.typedef_name`, set in `frontend/c/declaration.c`).
   `typedef struct { ... } fd;` gives `fd`.
3. **Anonymous member**, as in `struct S { union { int a; long b; }; };`:
   the owner of the record that contains it, applied recursively.
   `find_member` (`frontend/c/expression.c`) finds `a` through `S`, so `a`
   is reported as `S.a`.
4. **Untagged record without a typedef that is the type of a named member**,
   as in `struct S { struct { int x; } pos; };`: the containing owner, a dot,
   and the member name.  The owner is `S.pos`, so `x` is reported as
   `S.pos.x`.
5. **Anything else** (an untagged record used only for a variable, a cast or
   a parameter): `(anonymous at PATH:LINE:COL)`, the location of the
   record's `{`.  It can be selected with that spelling, but the tree has no
   such record that a table renames.

Rules 3 and 4 need to know which record contains a given record, and
`struct tag` has no such link.  The mode builds the link once, at the end of
the TU: it scans the fields of every complete record and, for each field
whose type is a record without tag or typedef, notes the containing record
(the first container found wins).  The links go into an array indexed by
tag, owned by the mode.  Building it at the end also means the typedef of
rule 2, which comes after the record's body, is already known.

One owner name can belong to two different records: a `struct Block` local
to one `.c` file and another in a different file, or a block-scope tag.
Each report line therefore carries the owner's definition (`DEF`, the
location of the tag's `{` as `PATH:LINE`).  The script refuses a row whose
owner has more than one `DEF` in the merged report until the row names one
(`OWNER@PATH`, see 4.1).

### 1.3 Where field names occur, and the hooks

The mode records a use at the point where the parser resolves a name to a
field.  It reads the identifier token itself there, because the token is
gone afterwards.

| Syntax | Where the parser resolves it | Hook |
|---|---|---|
| declaration: `int n;`, `int *p, n:3;` | `struct_body` (`frontend/c/record-parse.c`), in the member loop after `declarator()` | When the mode is on, `declarator()` (`frontend/c/declarator.c`) copies the name token into the mode's state.  `struct_body` then records a `decl` for the record being defined and field index `nf`. |
| `x.f`, `p->f` | postfix loop of `frontend/c/expression.c` (`case T_PERIOD`, `case T_ARROW`), then `member()` or `member_slow()` | Before `next(c)`, copy the name token.  After the case's `member()` call(s), record a `member` from the result node if it is `N_MEMBER`.  The node's `b` is the tag and `c` the field index, both of the innermost record on an anonymous path. |
| designators: `.f =`, nested `.a.b =`, `[i].f`, ranges `[0 ... 3].f`, GNU `f:` | `il_designation_path` (`frontend/c/initializer.c`), at `find_member` (the name is `tok(c)`).  Nested designators recurse into the same function. | Record a `designator` from `l->ty`'s tag, `path` and depth `d`. |
| `offsetof(T, a.b[i].c)`, `__builtin_offsetof` | `offsetof_expr` (`frontend/c/builtin-call.c`), at `find_member` for each `.` component | Record an `offsetof` from `ti`, `path` and `d`. |

A path through anonymous members (`d > 1`) is followed to its last step,
which is the named field in the innermost record.  Rule 3 then names that
field's owner.

The parser parses all of the following, so the hooks see uses inside them:
`sizeof`, `typeof`/`__typeof__`, `_Alignof`, `_Static_assert`, `_Generic`
associations, `__builtin_types_compatible_p` and `if (0)` blocks.

The hooks do not see:

- **Text the preprocessor skips:** `#if 0`, a branch not taken in this
  configuration (`#ifdef __linux__` on macOS, `VCC_WASM`, `_WIN32`,
  `DEBUG`), and files no configuration compiles.  Running several
  configurations (section 3) and the leftover report (4.5) cover these.
- **`counted_by(f)`:** `attribute_list` (`frontend/c/attribute.c`) keeps
  only the identifier id.  vcc's sources do not use the attribute
  (`include/common/attributes.h` says so); the leftover report would list
  one if it appeared.
- **Synthesized member accesses:** `member()` is also called with a name the
  user did not write at that place (`__builtin_dynamic_object_size` and
  `__builtin_counted_by_ref` in `builtin-call.c` read the `counted_by`
  field).  These are not uses.  Since the hooks are at the parse sites and
  not in `member()`, they are not reported.
- **Vector swizzles** (`v.xyzw`): these are not fields, and the postfix case
  takes `swizzle()` before `member()`.
- **Field names in strings, comments and stringified macro arguments**
  (`#f`): the parser does not resolve these tokens to fields.

A TU with errors stops the mode: `cc_field_uses_finish` prints only the `tu`
line, with `errors`, and the job fails as it does today.  Uses found after an
error recovery would be guesses.

### 1.4 Spelling: the bytes to edit

Every recorded use keeps a copy of the name's `ptok` (`s`, `len`, `loc`,
`flags`).  `ptok.s` points at the spelling:

- for a token read from a file, it points into that file's text;
- for a token of a macro body, it points into the text of the file holding
  the `#define` (`vfill` in `frontend/pp/ppimpl.h`; a body's `src` is that
  file's text, see `macro.c`).

In both cases the pointer lies inside a `vlex_file`'s `src`.  So the
spelling position comes from the pointer, whatever the expansion did with
`loc`.  The function that finds it:

```
/* the file and byte offset where token t is spelled: PP_SPELLING_FILE if
   that is where it is read from (loc), PP_SPELLING_MACRO if it is spelled
   elsewhere (a #define body), PP_SPELLING_NONE if it is spelled in no file
   (pasted, or from the header cache), PP_SPELLING_DIRTY if it contains a
   line splice */
int pp_token_spelling_find(const pp *p, const ptok *t, const vlex_file **file, uint32_t *offset);
```

It lives in `src/frontend/pp/source-query.c` and is declared in
`include/frontend/pp/pp.h`.  It scans `p->fe[]` for the `vlex_file` whose
`[src, src + size)` holds `t->s`, caching the last hit (uses cluster by
file).  It then compares that position with the file and offset of `t->loc`
(`loc_fent`).  Only the mode calls it.

The table gives each case and what it means for the rename (the `WHERE`
column of the report):

| Token | `loc` | `s` | `WHERE` | The edit |
|---|---|---|---|---|
| read from the file | the token | the token | `file` | at that position |
| macro argument substituted without `#` or `##` | its own (a pre-expanded argument keeps its tokens' `loc`) | the argument in the file | `file` | at the argument |
| macro body token, function-like (`subst_range`) or object-like (`C_VTOK`, `next-token.c`) | the invocation's name; `PF_MACRO` is set only for function-like macros | the `#define` body | `macro` | in the `#define` body.  This changes every expansion, so the script's conflict check (4.3) decides. |
| argument that is an operand of `##` | the invocation (`expand.c`: "unexpanded argument tokens take the expansion's location") | the argument in the file | `file` or `macro` | possible, since the bytes are the name.  But if `##` joined the token with a non-empty neighbour, the result is a different identifier and not this field. |
| result of `##` (`paste()`) | the left operand's | the TU arena | `none` | none: "cannot rename automatically here", a human edits the macro |
| identifier containing a backslash-newline (`TF_DIRTY`) | | raw text | `dirty` | none: by hand |

Using `s` rather than `loc` also handles a case that `loc` gets wrong: an
object-like macro named like the field (`#define n (s->n)`).  The body token
`n` gets the invocation's `loc`, whose bytes also spell `n`, and it has no
`PF_MACRO`.  Its `s`, however, points into the `#define`, so it is reported
as `macro` and the invocation is not edited.

### 1.5 Output

The mode writes to the job's output (`co.outfd`, which is stdout for
`vpp -fsyntax-only`), with one `write` at the end of the TU.  The writer is
`cc_field_uses_finish`, called from `cc_run` after `unused_typedefs` and
before `cc_free`.

The format is tab-separated, one record per line, with no header.  Every
path is the `vlex_file.path` as vcc opened it: relative to the job's working
directory, or absolute.

```
tu	MAIN	ok|errors
use	ROLE	OWNER	FIELD	DEF	WHERE	PATH	OFFSET	LINE	COL	LEN	EXPANSION
other	ROLE	OWNER	FIELD	DEF	WHERE	PATH	OFFSET	LINE	COL	LEN	EXPANSION
members	OWNER	DEF	NAME,NAME,...
macro	NAME
```

- **`use`:** a use of a selected field.
  - `ROLE` is `decl`, `member`, `designator` or `offsetof`.
  - `DEF` is the `PATH:LINE` of the owner's definition.
  - `PATH OFFSET LINE COL LEN` is the spelling position (1.4).  `OFFSET` is
    a byte offset counted from 0; `LINE` and `COL` count from 1, and the
    column is in bytes.  For `none` and `dirty`, these columns give the
    expansion's position instead, and `OFFSET` is `-1`.
  - `EXPANSION` is `PATH:LINE:COL` of `loc` when `WHERE` is not `file`, and
    `-` otherwise.
- **`other`:** a member use or declaration whose field name is a selected
  name but whose owner is not selected.  The script needs these to detect a
  spelling position that serves two owners (4.3).
- **`members`:** for each selected owner, every name `find_member` finds
  from it, used by the collision check.  That is the record's fields plus
  those of its anonymous members, recursively.  For an owner named by rule 4,
  it is the members of that untagged record.
- **`macro`:** a selected field name or a selection's `NEW` that is a
  defined macro at the end of the TU (`pp_defined`).

Records are printed in the order they were recorded, and duplicates are kept.
For example, a use in a header's `static inline` function is at the same
position in every TU; the script merges these.

### 1.6 State and files

The mode is one new file, `src/frontend/c/field-uses.c`, in `cc_SRC`, with
an opening comment.  Its one job is the mode.  Its functions are `VPRIV` and
declared in `ccimpl.h`:

```
void cc_field_uses_begin(cc *c, const char *selection_path);   /* reads the file, sets c->field_uses */
void cc_field_uses_name_keep(cc *c);                           /* copies *tok(c) as the pending name */
void cc_field_uses_record(cc *c, int role, uint32_t tag, uint32_t field_index);
void cc_field_uses_path_record(cc *c, int role, uint32_t tag, const uint32_t *path, int depth);
void cc_field_uses_finish(cc *c);                              /* owners, filter, print, free */
```

`struct cc` gets one pointer, `struct cc_field_uses *field_uses`, which is
NULL when the mode is off.  The state holds:

- the selection: the field-name ids as a small hash set, plus the entries;
- the pending name token;
- the recorded uses (tag, field index, role, `ptok`);
- at the end, the container array of 1.2.

While parsing, the mode keeps every use whose field-name id is in the
selection; any name counts when there is an `OWNER.*` entry.  It cannot test
the owner yet, because the owner names are only known at the end of the TU.

## 2. Cost when off

The mode uses hooks in the parser (each `if (unlikely(c->field_uses))`), not
a pass over the AST after parsing.  A later pass could not work without
making the normal path pay for it:

- `N_MEMBER` keeps the operator's location (`mk(c, N_MEMBER, ..., loc)`,
  with the `loc` of `.` or `->`), not the name token, and it has no
  `ptok.s`.  Keeping them would mean a wider node for every member access.
- `offsetof` folds to an integer (`offsetof_expr` returns `mk_int`), and
  designators fold into the initializer list, so no node is left to visit.
- Field declarations are not AST nodes, and the tag's `field.loc` has no
  spelling pointer.
- `sizeof` and `typeof` operands and folded expressions are not all kept.

The branches are:

- two in the `.`/`->` postfix cases (keep the name, then record);
- one in `declarator()`'s identifier case;
- one in `struct_body`'s member loop;
- one each in `il_designation_path` and `offsetof_expr`;
- two in `cc_run`.

All of them test one pointer in `struct cc` that stays NULL for the whole
TU, so they are always predicted correctly, and the recording functions are
out of line (`noinline, cold`).  The most frequent branch is the one in
`declarator()`, which runs for every declarator of the TU.  PR 1 measures
the cost with `bench/vbench.py gate` (instruction counts, budget 1 %).  The
expected cost is one load and one branch per declarator and per member
access, which should be lost in the noise.

## 3. Running it over the tree

### 3.1 What vpp parses today

Measured on main at ddbb6413, macOS arm64.  The compile commands of
`make -C src vcc` were taken from `make -n -B`; in each, the compiler was
replaced by `build/src/vpp -fsyntax-only`, and `-c`, `-o` and `-MMD -MP` were
dropped.

| Configuration | TUs | Fail |
|---|---|---|
| file by file (`VCC_UNITY=`), host flags | 337 | 0 |
| unity (`obj/unity/*.c` and the files outside modules) | 52 | 0 |
| file by file, `-mcpu=apple-m1` replaced by `--target=x86_64-apple-darwin` (the `__x86_64__` paths, with the macOS SDK) | 337 | 0 |
| file by file with `VCC_EAGER=1 VCC_NODC=1 VPP_NOHC=1` | 337 | 0 |
| the same with `-fsanitize=address,undefined` / `-fsanitize=thread` | 337 + 337 | 0 |
| `VCC_WASM=1`: the 14 extra TUs (`arch/wasm/`, `ld/wasmld.c`, `platform/wasm/wasm.c`) | 14 | 0 |
| `bin/vcc/shim.c -DVSH` (the vsh shim, built without `-c`) | 1 | 0 |
| the sources of `make fuzz` (link commands with several `.c` files each) | 50 | 0 |

The file-by-file run takes 4.9 s at `-P 4` on this machine, which was loaded
at the time; the unity run takes 0.9 s.

Two things fail, so they are limits the design has to work around:

- **Globbing `src/**/*.c` is wrong.**  The 56 `src/ld/*.c` files other than
  `ld/ld.c` are pieces that `ld/ld.c` `#include`s.  They fail on their own,
  with clang too (`unknown type name 'size_t'`).  The driver takes its TU
  list from the Makefile, never from a glob.
- **Linux code needs a Linux host.**  On macOS, `--target=aarch64-linux-gnu`
  and `--target=x86_64-linux-gnu` stop at the first system header
  (`'stdint.h' file not found`: there is no Linux sysroot).  So `__linux__`
  and `_GNU_SOURCE` code is parsed only by the Linux arm64 configuration,
  which runs in the container (`env/linux/run.sh`).  x86-64 Linux has no
  local host; premerge's x86-64 job compiles that code, and a use the report
  missed there is a compile error (section 5).

Nothing in vcc's sources has to change before PR 1.

### 3.2 `src/tools/field-uses.py`

```
src/tools/field-uses.py SELECTION [--config NAME]... [-j N] [-o REPORT.tsv]
src/tools/field-uses.py SELECTION --commands FILE [-o REPORT.tsv]   # tests: one "CWD<TAB>COMMAND" per line
```

1. **Build vpp** with `make -C src -j N vcc` (the host's default `OUT`:
   `build/src` or `build/linux/src`).
2. **Collect the TU command lines** for each configuration from
   `make -C src -n -B TARGET VARS OUT=TMP CCACHE=`:
   - every line with `-c X.c`;
   - the compile-and-link lines whose inputs are `.c` files (the shims,
     `make fuzz`), with one vpp run per `.c` file.

   The unity sources are generated for real first, with
   `make -C src OUT=TMP TMP/obj/unity/MOD.c ...` for the names the dry run
   mentions.  Each line is split with `shlex.split`, then:
   - kept: `-D`, `-U`, `-I`, `-isystem`, `-include`, `-std=`, `-f*` (except
     `-fsanitize=fuzzer`), `-m*`, `--target=`;
   - dropped: the compiler, `-c`, `-o X`, `-MMD`, `-MP`, `-MF X`, `-W*`,
     `-g`, `-O*`, link inputs and link flags;
   - added: `-fsyntax-only`.
3. **Configurations.**  The default is every one the host can run:

   | Name | Host | Commands |
   |---|---|---|
   | `files` | any | `make -n -B vcc VCC_UNITY=` |
   | `unity` | any | `make -n -B vcc` |
   | `x86` | macOS | `files`, with `-mcpu=*` replaced by `--target=x86_64-apple-darwin` |
   | `asan`, `tsan` | any | `files` plus `-fsanitize=address,undefined` or `-fsanitize=thread` (for the `__has_feature` tests in `constant-eval.c` and `vccd-internal.h`) |
   | `wasm` | any | `make -n -B vcc VCC_WASM=1 VCC_UNITY=`, only the TUs that `files` does not have |
   | `fuzz` | any | `make -n -B fuzz` |

   On Linux, `files` and the others use the Linux host: aarch64 in the
   container, x86-64 natively.
4. **Run each TU** in `src/`, with
   `VCC_FIELD_USES=SELECTION VCC_EAGER=1 VCC_NODC=1 VPP_NOHC=1`, `-j N` at a
   time, under `nice -n 10`.  A TU that exits non-zero or prints
   `tu ... errors` fails the whole run, and its command is printed.
5. **Merge the reports:**
   - paths are made relative to the repository (`os.path.realpath`, from
     `src/`); a path outside the repository stays absolute and is never
     edited;
   - identical records (all columns except `EXPANSION`) become one, with a
     last column listing the configurations that saw it;
   - the report is written sorted by path and offset.

A whole run takes about 30 s on macOS: six configurations of 1-5 s of CPU
each at `-j 4`.

## 4. `rename-table.py --fields`

### 4.1 Field rows

Every table uses one format for field rows:

```
old<TAB>new<TAB>field<TAB>OWNER[<TAB>comment]
```

`OWNER` is the owner name from 1.2 (`as_context`, `S.pos`).  When two records
share that name, `OWNER@PATH` picks one definition.

For the tables that already exist, the script also accepts a leading
`struct ` or `union ` (it strips it).  It also looks owners up through the
table's own `type` rows (for example `As	as_context	type`).  So a field row
written before its type was renamed still finds the record, whether or not
the type row has been applied.

Converting the existing tables, in the PR that first applies each one:

- `rename-as.tsv`, `rename-lexpp.tsv`: column 3 is already `field` and
  column 4 is already `struct X`.  Only `struct Sv (ambiguous: ...)` in
  `rename-as.tsv` needs its parenthesis moved to the comment column; the
  script refuses an owner that still contains spaces after the prefix is
  stripped.
- `rename-diag.tsv`: its field rows are commented out as `unsafe`; the diag
  rename turns them into field rows.
- `rename-common.tsv`: has no field rows.

Field rows are taken out of the whole-word pass.  `--apply` without
`--fields` skips them and prints `N field rows: apply them with --fields`.
Today a field row is a word row; from now on a table is applied twice, words
then fields or the other way round (owners resolve either way).

### 4.2 Commands

```
src/tools/rename-table.py TABLE.tsv --fields                         # dry run: field-uses.py for the field rows, then checks
src/tools/rename-table.py TABLE.tsv --fields --report R.tsv [--report R2.tsv]   # use reports made earlier (e.g. a Linux run), merged
src/tools/rename-table.py TABLE.tsv --fields --apply [--allow-manual] # edit
src/tools/rename-table.py --selftest                                  # PR 2's test
```

The dry run writes the selection (`OWNER.old new` for each row) to a
temporary file and runs `field-uses.py` (unless `--report` is given).  For
each row it prints the number of uses and files, the positions grouped by
`WHERE`, and every problem from 4.3.  `--apply` refuses if there is any
problem.

### 4.3 Checks

Each of these is a problem unless it says otherwise.

1. **Stale row:** there is no `decl` record for (owner, old).  If `new` is
   in the owner's `members`, the row is reported as `already applied`, which
   is not a problem.  Otherwise it is reported as `unused`.
2. **Ambiguous owner:** the `decl` records have two `DEF`s, so the row must
   say `OWNER@PATH`.  A `DEF` outside the repository (a system struct) is
   refused.
3. **Name taken:** `new` is already in the owner's `members`, and no other
   row of the same owner renames that member away (swaps within one owner
   are allowed).  Since `members` includes the fields of anonymous members,
   this also catches a clash with an enclosing anonymous aggregate.
4. **Merge:** two rows of one owner have the same `new`.
5. **Macro:** there is a `macro` record for `old` or `new`, or a line
   matching `#\s*define\s+NEW\b` anywhere in the scope.  A macro named like
   the field changes what the token means.
6. **Keyword:** `new` is a C or GNU keyword (the list in
   `frontend/c/keywords.c`).
7. **Shared position:** a position (path, offset) has `use` records of this
   row and `other` records, from another owner or from another row with a
   different `new`.  One spelling then serves two owners: a `#define` body
   used with two structs, or a header included as a template.  The position
   is not edited and is listed for a human.
8. **Macro parameter:** for a `macro` position, the `#define` around it has
   a parameter named `new`, so after the edit the token would be that
   parameter.  The script finds the `#define` in the file text: the line
   that starts with `#define`, following backslash-newlines.
9. **Generated file:** the position is in a file whose first lines say
   `generated by` (`STYLE.md`: `/* generated by TOOL from SOURCE: do not edit
   */`).  Today that covers `ppdata*.inc`, `diagnostic-ids.inc`,
   `explain.inc`, `attrnames*.inc`, `libfuncs.inc` and others.  The
   generator must change instead.
10. **By hand:** positions whose `WHERE` is `none` or `dirty`.  These are a
    problem unless `--allow-manual` is given; then they are listed for the
    human, who edits them in the same PR.

### 4.4 Applying

All edits of one file are applied in one pass:

1. Positions are merged, so a header seen from many TUs gives one position.
2. They are sorted by offset.
3. Each is checked to hold exactly the bytes of `old`
   (`data[off:off + len] == old.encode()`).
4. They are applied from the end of the file, so the offsets stay valid.

If any position does not match, nothing is written: the report is stale and
must be made again.  This mode never edits strings, comments or prose.

### 4.5 The leftover report

After the edits (and in the dry run, as a prediction), the script lists every
whole-word occurrence of each `old` in its scope (`SCOPE` in
`rename-table.py`) that is neither an edited position nor an `other`
position.  A small C lexer in the script sorts them into:

- **`code`, member-shaped**: `.old`, `->old`, `.old =`, `old:` in an
  initializer, or the last argument of `offsetof`.  These must be checked:
  they are in text no configuration compiled (`#if 0`, `_WIN32`, `DEBUG`,
  x86-64 Linux) or in a record the mode did not see.  Each is listed with
  `path:line:col` and the line.
- **`comment` and prose** (`.md`) occurrences that are member-shaped or in
  backquotes: listed, not changed.  The author fixes the comments that refer
  to this field.
- **`string`:** listed, not changed.
- **any other `code` word** (locals, other fields): only counted;
  `--leftovers all` lists them.

## 5. Proof for a field rename PR

A field rename changes no type, layout, constant or printed text.  That
gives the following checks:

- **vcc's outputs are byte-identical.**  Run `bench/vbench.py mkbase REV`
  with REV the merge-base, then `bench/vcc-same.py --base DIR -j 4`.  Any
  difference is a bug.
- **vcc's own objects are identical**, except for text that names the field
  through `#` (assert-like macros that print their condition).  Build the
  merge-base and the head with `make -C src -j 4 vcc CCACHE= OUT=...` into
  two directories and `cmp` every `.o`.  Every difference must be explained
  by `llvm-objdump -s -j __cstring` (macOS) or `.rodata` (Linux), with
  nothing in `__text`.  This checks that the rename touched only names.
- **The unity and file-by-file builds compile**, on macOS and in premerge
  (Linux arm64 and x86-64).  A use the report missed in compiled code is a
  compile error, because the old name no longer exists in the record.  That
  is why the configurations and the leftover report only need to cover text
  that no build compiles.
- **The usual suites pass:** `bench/vcc-ctests.py -j 4`; the module's own
  harnesses (for the assembler, `bench/vas-obj.py` and
  `bench/vas-enc.py $PWD/src/test/vas`); every `src/tools/gen-*.py --check`;
  `bench/quality.py --check`.
- **The PR body accounts for the rest:** every "to check" line of the
  leftover report and what was done with it, and the by-hand positions.

## 6. Steps

### PR 1: the mode (`quality/field-uses-mode`)

Files:

- `src/frontend/c/field-uses.c` (new) and `ccimpl.h`;
- `include/frontend/c/cc.h` (`cc_opts.field_uses`);
- the hooks in `declarator.c`, `record-parse.c`, `expression.c`,
  `initializer.c`, `builtin-call.c` and `translation-unit.c`;
- `pp_token_spelling_find` in `frontend/pp/source-query.c` and `pp.h`;
- `driver/job.c`;
- `src/Makefile` (`cc_SRC`);
- the tests, `ci/premerge.sh`, and one line in `AGENTS.md`'s Build and test
  section.

Tests: each `src/test/field-uses/NAME.c` starts with
`/* select: OWNER.FIELD [NEW] */` lines, and its expected report is in
`NAME.expect`.  The harness is
`bench/vcc-field-uses.py [-j N] [--bless] [--bin-dir DIR]`, which works like
`bench/vcc-diag.py` (snapshots, `--bless`).  `ci/premerge.sh` runs it in the
arm64 and asan jobs.  The cases include at least:

- `.` and `->`, and a field with the same name in two structs (one `use`,
  one `other`);
- an anonymous struct in a union in a struct (`Outer.a`), an untagged member
  type (`S.pos.x`), a typedef-only record, and a block-scope tag that shadows
  a file-scope one (two `DEF`s);
- declarations: plain, several declarators, a bit-field, a function pointer
  `int (*f)(void)`;
- designators: `.f =`, `.a.b =`, `[1].f =`, `[0 ... 2].f =`, GNU `f:`, a
  compound literal, and a designator through an anonymous member;
- `offsetof(T, a.b[2].c)` and `__builtin_offsetof`;
- `sizeof`, `typeof`, `_Static_assert`, `_Generic` and `if (0)`;
- macros: an argument (`file`); a function-like body (`macro`, with the
  `#define` position and `EXPANSION`); an object-like body; an object-like
  macro named like the field; nested macros; `##` (`none`); `#f` (nothing
  reported); a line splice inside the name (`dirty`);
- a header included twice as a template with different `#define`s (same
  position, two owners);
- `macro` records for a field name and for a `NEW` that are macros;
- `OWNER.*`, and an owner named by rule 5;
- a TU with an error (`tu ... errors`, exit 1), and the refusals of 1.1 (no
  `-fsyntax-only`; one of the three variables missing).

Proof: the unity and file-by-file builds; `vcc-ctests.py`; `vcc-same.py`
byte-identical; `vbench.py gate` within 1 % (the hooks are on parser paths);
the parity and sema runs from AGENTS.md if `work/` is usable; premerge.
`field-uses.py` only arrives in PR 2, so PR 1 also shows, by running vpp by
hand, that one `files` TU and one unity TU of the assembler print the
expected uses of `as_context.cur`.

### PR 2: the driver and the script (`quality/field-uses-rename`)

Files: `src/tools/field-uses.py` (new), `src/tools/rename-table.py`, the
fixture `src/test/field-uses/rename/`, `bench/vcc-field-uses.py` (which calls
the self-test), and `AGENTS.md`.

The fixture holds a few `.c` and `.h` files, a `commands` file for
`field-uses.py --commands`, a `table.tsv`, and the expected files `*.after`.
`rename-table.py --selftest` copies it into a temporary directory, runs the
dry run and `--apply` with `--root TMP`, and compares the result with the
`*.after` files.  `--root` is a new option that names the tree to work on
instead of the checkout.  The cases:

- a plain rename across a header and two TUs;
- a refused stale row;
- a name already taken in the owner, and one taken in an anonymous member;
- a swap of two fields;
- a shared position;
- a clash with a macro parameter;
- a pasted use (listed with `--allow-manual`);
- a byte mismatch (the fixture edited after the report), so nothing is
  written;
- the leftover report for `#if 0` code and for a comment.

Proof: the self-test passes in premerge.  `rename-table.py` on
`doc/quality/rename-common.tsv` (already applied) prints what it printed
before: apart from field rows, the word pass is unchanged.

### PR 3: the assembler's remaining fields (`quality/as-fields`)

1. Convert the field rows of `doc/quality/rename-as.tsv` (4.1): the `Sv`
   note moves to the comment column.  `pool_dummy` (row `-`, a field to
   delete) leaves the table and is deleted by hand in its own commit.
2. Run `rename-table.py doc/quality/rename-as.tsv --fields`.  The 82 applied
   rows come out as `already applied`, and the 53 that are left
   (`WORK-as-obj.md`) are checked.  Resolve every problem: a taken name gets
   a new name in the table, and a shared position is edited by hand.
3. In the Linux container, run `field-uses.py` for the same selection, and
   pass both reports with `--report`.
4. Run `--apply`.  Review `git diff --stat`, the short names (`o`, `p`, `n`,
   `e`, `v`, `k`, `t`, `s`, `a`), the leftover report and the comments.
5. Give the proof of section 5, including `vas-obj.py` and `vas-enc.py`, and
   update the status in `WORK-as-obj.md` ("Rename table: applied": the
   fields are done).

## Open questions for the owner

1. Should the `VCC_FIELD_USES` test aid be a command-line developer option
   instead (`--vcc-field-uses=FILE`, next to `--vcc-layout-checks`)?  It
   would then appear in the option table and in `--help`.

Everything else in this design is internal tooling.
