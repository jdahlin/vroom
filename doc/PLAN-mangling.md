# PLAN-mangling: restructuring the Itanium mangler

Status: proposal (2026-11-27), steps M0-M7.  No step started.

The C++ front end's Itanium mangler (`src/frontend/cplusplus/abi/itanium/
mangling.cpp`) grew one construct at a time while the front end learned to
compile libc++, nlohmann/json and Stockfish.  It works, and it cites the ABI,
but it is now the largest file of the front end and the hardest to change
safely.  This plan restructures it without changing a single symbol.

## 1. Where it stands (origin/main 34062df8d)

- **One file, one class.**  5448 lines, one `itanium_mangler` with 76
  member functions and ~20 fields of per-mangling state (`encoding_template`,
  `written_first`/`written_count`, `pack_element`, `operand_dependent`,
  `alias_value_refused`, `alias_value_trial`, `template_params_written`,
  `dependent_value_depth`, `mangling_part`, ...).  The longest functions:
  `dependent_expression_of` 690 lines, `type` 369, `operator_name` 178,
  `constraint_expression` 163, `dependent_value_expr` 159.
- **Deciding and writing are one step.**  Every function both decides which
  production applies and appends its bytes to the output `vbuffer`.  When a
  decision turns out wrong the bytes are taken back: 10 places rewind
  `out->n` (`out->n--` to drop an `E` and write `Q...E` instead,
  `out->n = before` after a failed trial), and the substitution candidates
  have to be rewound with them, by hand.  `alias_value_trial` mangles a
  whole argument speculatively and throws it away.
- **The encoding letters are spelled many times.**  The operator codes are
  written in seven places: `binary_operator_code`,
  `constraint_expression` (`aa`, `oo`, `nt`), `dependent_fold`, the unary
  table in `dependent_expression_of`, `dependent_value_expr`'s table keyed
  by numbers, `operator_name`'s two switches and `structure_expression`'s
  binary and unary tables.  `"aa"` alone appears six times.  The literal
  forms (`L`, `Li`, `LDnE`, `L_Z`), `sr`, `cv`, `E` (58 times) are written
  inline wherever they are needed.
- **Four sources of expressions, four encoders.**  An `<expression>` is
  mangled from
  1. a requires-clause's syntax (`constraint_expression`,
     `constraint_template_args`, `compound_requirement`, ...),
  2. the written syntax of a dependent type with its bindings
     (`dependent_expression_of`, `dependent_fold`, `operand_type`, ...),
  3. an `entity_kind::dependent_value` entity whose `value` is a bare
     number chosen in `template/template-argument.cpp` (8 `aa`, 9 `oo`,
     10-19 the relational operators, 11 `st`, 12 `Li`, 13 `clsr`, 14, 20
     `L_Z`, 21, 4 and 6: `dependent_value_expr`), and
  4. sema's `expression_structure` nodes (`structure_expression`).
  Each walks its own representation and writes its own bytes.
- **Mangling outside the mangler.**  Guard variables are made in
  `lower/static-storage.cpp` by splicing `"_ZGV"` onto a symbol minus its
  `_Z`; thunks are `vbuffer_format_append(..., "_ZTv%lld_n%llu_")` and
  `"_ZThn%llu_"` in `lower/virtual-bases.cpp`; the `__cxxabiv1` type_info
  vtables are nine literal symbols in `lower/typeinfo.cpp`.
- **Names drift from the grammar.**  Some functions are named for the
  nonterminal they produce (`nested_name`, `prefix`, `source_name`,
  `bare_function_type`, `seq_id`), others for where their input comes from
  (`dependent_value_expr`, `structure_expression`, `written_template_args`,
  `unqualified_name_untagged`, `std_of_char`, `member_qualifiers`,
  `qualified`, `variant`).
- **Citations are good but uneven.**  130 `[abi:...]` citations and 41
  `[impl.clang]`, checked by `bench/cxx-spec-refs.py` against
  `docs/itanium-abi-index.tsv` and the quoted productions against
  `docs/itanium-abi-grammar.tsv`.  `[abi:expressions]` (31 citations) covers
  four different encoders; several branches inside the long functions say
  which production they are only in prose, or not at all.

## 2. Goals

1. Files by ABI section, each a few hundred lines, with one internal header.
2. Two layers: a *builder* that decides which production and alternative
   applies to an entity, a type or an expression (everything that looks at
   the semantic analysis, including clang's choices), and an *encoder* that
   turns the chosen productions into bytes and owns compression.  The
   builder never writes a byte; the encoder never looks at the analysis.  One pass and no
   intermediate tree: nothing allocated that is not allocated today (3.2).
3. Each encoding letter written once, in a table that quotes its
   production; a check keeps it that way.
4. Function, type and enumerator names are the grammar's nonterminals;
   the code that decides between alternatives names the alternative.
5. Each function begins with the productions it produces (as today) and
   each branch is tagged with the alternative it takes, inline.
6. No change to any symbol, any refusal or its reason, and no more than
   cxx-fe-bench's +0.5% budget on the front end's instructions.

Out of scope: new mangling coverage, Microsoft mangling, the C front end's
`overloadable` mangler (`frontend/c/mangle.cpp`, 207 lines; it could later
use the encoder, see M7), and unifying sema's four expression
representations (the plan makes them share one builder interface, which is
what makes that unification possible later).

## 3. The target design

### 3.1 Files

`src/frontend/cplusplus/abi/itanium/mangling/` (the Makefile's
`cplusplus_SRC` wildcard gains `abi/*/*/*.cpp`), one file per ABI section,
named by its label in `docs/itanium-abi-index.tsv`:

| file | ABI sections | today's functions |
|---|---|---|
| `mangling-internal.h` | | the builder and encoder classes |
| `productions.inc` | the grammar's terminals | every letter (3.3) |
| `operators.inc` | `mangling-operator` | `<operator-name>`: code, arity, token, ABI name |
| `builtin-types.inc` | `mangling-builtin` | `<builtin-type>` per `fundamental_type` |
| `symbols.cpp` | `mangling-structure`, `mangling-special*` | `mangled_name`, `encoding`, the public `itanium_*_name` entry points; TV TT TI TS TC GR, and GV, Th/Tv moved in from `lower/` |
| `names.cpp` | `mangle.name`, `mangle.abi-tag`, `mangling-special-ctor-dtor`, `mangling-operator` | `name`, `nested_name`, `prefix`, `unscoped_name`, `unqualified_name`, `source_name`, `ctor_dtor_name`, `operator_name`, ABI tags |
| `scopes.cpp` | `mangling-scope`, `closure-types`, `mangling.anonymous` | `local_name`, `discriminator`, `closure_type_name`, unnamed types, `function_scope` |
| `types.cpp` | `mangling-type`, `mangle.qualified-type`, `mangle.function-type`, `mangle.array-type`, `mangle.pointer-to-member-type`, `mangle.decltype`, `mangling.named` | `type` split by alternative, `builtin_type`, `qualified_type`, `function_type`, `bare_function_type`, `array_type`, `class_enum_type` |
| `template-arguments.cpp` | `mangle.template-param`, `mangle.template-arg` | `template_args`, `template_arg`, `template_param`, `template_template_arg`, the parameter types (`Tn`) |
| `expressions.cpp` | `expressions`, `mangling.literal`, `mangling.declaration-reference`, `mangle.function-param` | the shared builder of `<expression>`, `<expr-primary>`, `<unresolved-name>` that the four sources call |
| `expressions-written.cpp` | | source 2, today's `dependent_expression_of` and helpers, split by expression form |
| `expressions-constraint.cpp` | | source 1, requires-clauses |
| `expressions-dependent-value.cpp` | | source 3 |
| `expressions-structure.cpp` | | source 4 |
| `encoder.cpp` | `mangling-compression`, `mangle.number`, `mangle.seq-id` | the encoder: bytes, `<substitution>`, `<seq-id>`, `<number>`, the `St`/`Sa`/`Ss`... abbreviations |

The helpers `enclosing_function`, `next_enclosing_function`,
`visible_local_class` (used by sema too) go to `scopes.cpp`.

### 3.2 Builder and encoder, without a tree

Itanium compression is defined left to right: a component is a candidate
once it has been written, and a later one refers back to it.  So the
encoding can be produced in one pass, as it is today, and the separation is
an interface, not an intermediate form: no node tree, no allocation beyond
the output buffer and the candidate array that exist today.

- The *builder* decides; the *encoder* writes.  Each builder function for a
  production first *resolves* (a pure function of the analysis returning a
  small value on the stack: which alternative, its operands, the
  substitution key) and then *emits* by calling the encoder with that
  value.  Nothing is written before the alternative is known.
- The *encoder* (`itanium_encoder`, one method per production or
  terminal group: `nested_name_begin(cv, ref)`, `nested_name_end()`,
  `source_name(id)`, `builtin_type(t)`, `template_param(position)`,
  `operator_code(op)`, `substitution_or_candidate(key)`, ...) owns the
  output buffer, the candidate array, `<seq-id>`, `<number>` and the `St`,
  `Sa`, `Ss`... abbreviations.  It reads only the tables (3.3), never the
  analysis.

The ten rewinds of today (origin/main) and what replaces each:

| where | today | instead |
|---|---|---|
| `constraint_template_args`, `class_enum_type` (2) | write `N`, try `substitution(template)`, rewind if it is no candidate | look the candidate up first (`encoder.find_candidate(key)`, pure), then write |
| `specialization_encoding`, `constraint_template_args` (2) | `out->n--`: take back the `E` that `name()` wrote, to put arguments or `Q...E` before it | the caller resolves `<template-prefix> <template-args>` and asks for the nested-name to stay open; `E` is written once, at the end |
| `dependent_expression_of`: `cv`/`tl` + `type_operand` (3) | write `cv` or `tl`, try the operand, rewind if it is not a type | resolve the operand's type first (`operand_type` already does it, without writing), then write |
| `dependent_expression_of`: `sr` qualifier (1) | write `sr` and source names, classify the last qualifier while writing, rewind bytes and candidates | classify the qualifier levels first (pure), then write |
| `parameter_type`: the alias-value trial (2) | copy the whole candidate array to an `owned_array`, mangle the type, on one refusal mangle it again with `alias_value_trial`, rewind, restore the copy | a pure query "does this type name an alias template's specialization in a value-dependent argument" decides the mode before writing; if that query cannot be written, `encoder.mark()`/`rewind(mark)` (a byte count and a candidate count: candidates are only appended, so no copy) |

Three of today's rewinds do not restore the candidates with the bytes
(`cv`, `tl`, `cv ... _E`); that is harmless only while `type_operand` fails
before it registers a candidate.  Resolving first removes the question.

`mark`/`rewind` stays in the encoder for a case that cannot be decided
first, but a use of it needs a comment saying why; the goal is none.

### 3.3 One table of terminals

`productions.inc` is an X-macro list of every alternative the mangler
produces, each line its production, alternative name, the terminals and
the citation:

```c
/* [abi:mangle.name] <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E */
P(nested_name, prefix_name, "N", "E")
/* [abi:mangling-special-vtables] <special-name> ::= TV <type> */
P(special_name, virtual_table, "TV", "")
```

`operators.inc` holds `<operator-name>`'s table (code, arity, the
`preprocessing_op_or_punc` it is written with, the ABI's description), read
by `operator_name` and by all four expression sources; the `value` numbers
of `dependent_value` (3 above) become that table's operator enumerators,
set in `template/template-argument.cpp`.  `builtin-types.inc` maps
`fundamental_type` to `<builtin-type>`.

A check in `bench/cxx-spec-refs.py`: every production of
`productions.inc` and `operators.inc` is one of its section's productions
in `docs/itanium-abi-grammar.tsv`, and no `.cpp` under `mangling/` other
than `encoder.cpp` contains a string literal that is a terminal from the
tables (so a letter cannot come back inline).

### 3.4 What substitution compares

Today a candidate is a `type_index` (one record per type), an entity (a
namespace or template prefix), a member function type with its class, or a
pack expansion.  The builder passes that identity to the encoder as a
`candidate_key` (one of those four kinds, a value, not an allocation), so
compression is unchanged by construction: the encoder compares keys, not
bytes.  Comparing the encoded
bytes would be simpler and is what the demangler sees, but it differs from
today (and from clang) for dependent types that are spelled alike and are
not the same type; that is a decision for later, not for this plan.

### 3.5 Naming and comments

- A builder function is named for its nonterminal (`unresolved_name`,
  `base_unresolved_name`, `cv_qualifiers`, `ref_qualifier`,
  `qualified_type`, `template_prefix`); one that only decides between
  alternatives for one input source is `<nonterminal>_of_<source>`
  (`expression_of_constraint`, `expression_of_structure`).
- Renames: `dependent_value_expr` -> `expression_of_dependent_value`,
  `structure_expression` -> `expression_of_structure`,
  `dependent_expression_of` -> `expression_of_written`,
  `unqualified_name_untagged` -> `unqualified_name` (and `abi_tags` for the
  tag part), `member_qualifiers` -> `cv_qualifiers` + `ref_qualifier`,
  `qualified` -> `qualified_type`, `std_abbreviation`/`std_of_char` ->
  the encoder's `<substitution>` abbreviations, `variant` ->
  `ctor_dtor_kind`, `written_template_args` ->
  `template_args_of_written`, `unresolved_qualifier` ->
  `unresolved_qualifier_level`.
- Each function's comment quotes its productions (checked as today); each
  branch that picks an alternative starts with `/* <alternative> */`,
  quoting that right-hand side, so a reader finds `sr <unresolved-type>
  <base-unresolved-name>` at the code that builds it.  ABI prose stays in
  our own words, clang's choices stay `[impl.clang]`.

## 4. Steps

Every step keeps every symbol and refusal identical (M0's gate) and
passes `cxx-fe-bench.py --against` within +0.5%; no step adds an
allocation.  One PR per step.

### M0: the identity gate
`bench/cxx-mangle-same.py --save F | --against F`: `vpp -fcxx-dump=symbols`
over `src/test/cxx/run`, `src/test/cxx/sema` inputs, clang's mangling tests
(when `work/testsuites` is there) and the json/Stockfish TUs that compile:
every symbol and every refusal with its reason, byte for byte.  Also the
existing gates: `cxx-mangle.py` (passing list), `cxx-run.py -O 0123`,
`cxx-sema.py --check`, `cxx-c-suites.py`, `vcc-abi.py` (cxx/).
Status: done (2026-11-27).  `bench/cxx-mangle-same.py`: 1618 inputs (1467
cxx/run, 57 clang mangling tests, 71 json, 23 Stockfish), ~2.5 min at -j 12;
two runs of one build agree.  The mangler is ~7% of `-fcxx-dump=symbols` on
Stockfish's search.cpp (perf record), most of it `closure_type_name`'s scan
of the entity table; `cxx-fe-bench.py` (-fsyntax-only) does not reach it,
so instructions of `-fcxx-dump=symbols` on four json/Stockfish TUs are the
speed measure.

### M1: one table per kind of terminal
`operators.inc` and `builtin-types.inc`; the operator tables and both
`operator_name` switches read `operators.inc`; `dependent_value`'s numbers
become operator enumerators (sema side in `template/template-argument.cpp`
and the readers).  No file split yet; the smallest diff that removes the
duplication.
Status: done (2026-11-27).  `abi/itanium/operators.inc` (49 rows: code,
arity, token) is read by `operator_name` and every expression site through
`operator_written`; `prefix_operator` keeps prefix ++/-- (the ABI's pp_,
mm_) out of a unary expression.  `builtin-types.inc` is `builtin_type`'s,
and the integer-literal, bool and nullptr literals take their type's
letter from it; one keyword table (`simple_type_keyword_alone`) replaces
two.  `dependent_value_form` in sema.h names the codes, with
`dependent_value_operators` shared by sema and the mangler.  Identity gate:
no difference (it now ignores the line of a refusal's vcc source site).
Speed: +0.02% instructions.

### M2: the file split
Mechanical: move functions into the files of 3.1, unchanged, with
`mangling-internal.h` holding the class.  Every file compiles alone
(`VCC_UNITY=`, `cplusplus-check`).  Move GV, Th/Tv and the `__cxxabiv1`
names from `lower/` into `symbols.cpp` behind `itanium_*_name` functions.
Status: not started.

### M3: names and comments
The renames of 3.5, the per-branch alternative tags, and the long
functions split along their alternatives (`type` into one function per
`<type>` alternative, `expression_of_written` by expression form,
`operator_name` into the table lookup).  `cxx-spec-refs.py` coverage
reported before and after.
Status: not started.

### M4: the encoder interface
All bytes go through `itanium_encoder` (3.2), reading `productions.inc`; it
owns `out`, the candidates, `<seq-id>`, `<number>` and the abbreviations.
The rewinds still exist at first, as `encoder.mark()`/`rewind(mark)`, which
rewind the candidates too (fixing the three sites that do not).  The literal
check of 3.3 turns on here.
Status: not started.

### M5: resolve before emit
Each rewind of 3.2's table replaced by resolving first; the alias-value
trial by a pure query, which also removes `alias_value_trial`,
`alias_value_refused` and the copy of the candidate array.  Done when
`mark`/`rewind` has no caller, or each remaining one says why.
Status: not started.

### M6: one expression builder
The four sources call one set of `<expression>` builders
(`binary_expression(op, a, b)`, `unary_expression`, `cast_expression`,
`expr_primary_integer`, `unresolved_name`, ...) in `expressions.cpp`; each
source file keeps only the reading of its representation.  The per-mangling
state that belongs to one source (`written_first`, `pack_element`,
`dependent_value_depth`, ...) moves into that source's walker object.
Status: not started.

### M7 (optional): the C mangler
`frontend/c/mangle.cpp` uses the encoder for its types and substitutions.
Status: not started.

## 5. Decisions needed

1. **Directory or prefix.**  `abi/itanium/mangling/` (one Makefile wildcard
   change) or `abi/itanium/mangling-*.cpp`.  Recommended: the directory.
2. **Order of M1 and M2.**  M1 first keeps the diff of the split purely
   mechanical; M2 first makes M1's diff smaller per file.  Recommended: M1
   first.

A node tree built per symbol and encoded afterwards was considered and
left out: compression is left to right, so a tree buys nothing the
resolve-then-emit split does not, and costs an array and a second walk.
