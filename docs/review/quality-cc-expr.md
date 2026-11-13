# Quality review: C front end, expressions, types, constants (2026-11-13, main at 2e6d3e73)

Files, relative to `ccs/vcc/`: `cc/{expr,exprop,conv,const,bitcast,lit,type,layout,record,complex,atomic,bigint,bigfloat,sym,token}.c`
and `cc/{cc,ccint,ccimpl}.h`: 11 368 lines, all read in full.  Nothing was built or run except
`bench/quality.py` and a 5-line program, outside the tree, printing struct sizes and enum counts.

## 1. Summary

The type, conversion, layout and constant code is in good order where it is plain C11 rules:
`type.c`, `lay_record` and `eval` use no token and can be called from anywhere.  What stands in
the way of a second front end is not these functions but what they hang on:

1. **One struct for everything.**  `struct cc` (`ccint.h:119-290`, 190 fields) is the token
   window, the parser's context, the diagnostics side channels and the AST tables at once; `emit/`
   and `arch/` read about 24 of the fields.  Every type, conversion and evaluation function takes
   it, and several read parser context through it (Q-ccexpr-01, -18).
2. **Constants are evaluated on demand, by two evaluators, with no memory.**  `eval` (494 lines,
   six jobs) is called from about 110 places in `cc/` and 50 in `emit/` and `arch/`; the parser asks
   it up to five times per binary operator; `bitcast.c` holds a second folder for vector lanes
   (Q-ccexpr-02, -03).
3. **Lowering in the front end is written by hand four times.**  Complex arithmetic, wide atomics,
   `_BitInt`/float casts and some builtins are rewritten into AST while parsing, each file with its
   own temporaries, statement expressions and CAS loop (Q-ccexpr-04).  On the way the node fields
   took on meanings `cc.h` does not list, two of which overlap (Q-ccexpr-05).

About half of `conv.c` and a third of `exprop.c` are warnings, not conversions or parsing
(Q-ccexpr-06).  `bigint.c` and `bigfloat.c` are written in another dialect (Q-ccexpr-10).

## 2. Inventory

| File | Lines | What it actually does | Better name | Header |
|---|---|---|---|---|
| `cc.h` | 338 | public format: type kinds, `ctype`, `field`, `tag`, `sym`, node kinds and `node`, cast kinds, `cc_opts`, `cc_run` | `ast.h` + `ccopts.h` | good; node comments incomplete (Q-05) |
| `ccint.h` | 387 | `struct cc` (all per-TU state), `attrs`, `casm`, lazy-body records, the `BI_*` enum, `cval`, the `cc_*` functions emit calls | split: Q-01 | label only; "AST side tables" understates it |
| `ccimpl.h` | 776 | prototypes across cc's files, token inlines (`next`, `accept`, `expect*`), `decay`/`convert`/`postfix` fast paths, `dspec`/`dop`/`declr`, recovery | - | good on top; file sections stale (Q-12) |
| `type.c` | 571 | type interning (two hash tables, pointer cache), constructors, predicates, promotion, usual conversions, compatibility, composite | - | label |
| `layout.c` | 521 | record layout (175 lines); the type printer `tyname` every diagnostic uses (240); the `dump_layouts` self-check output (100) | `layout.c` + `typename.c` + `layoutcheck.c` | label, says two of three |
| `record.c` | 405 | parses struct/union and enum bodies, checks members, calls `lay_record`; picks enum types | - | label |
| `const.c` | 1046 | `trunc_to`; address constants; C23 constant-object reads and constant-initializer grammar; `eval`; null pointer constants | - | label |
| `bitcast.c` | 969 | `__builtin_bit_cast` parse; object-representation reader over initializers; a second constant folder for vector lanes; `vector_constant` | `objrep.c` | good |
| `conv.c` | 1079 | `cast_node`, decay, promotion, `conv_kind`, `convert*`, `rvalue`, lvalue checks (about 480 lines); conversion diagnostics, `-Wconstant-conversion`, `-Wliteral-conversion`, `-Warray-bounds` with its text art (about 600) | `conv.c` + `convwarn.c` + `bounds.c` | label |
| `expr.c` | 1440 | semantic actions `binop`, `assign`, `conditional`, `member`, `deref`, `addr_of`; vector swizzles; primary and postfix parsing; `call` with overloads, argument checks and library-call rewrites | - | label |
| `exprop.c` | 1158 | parsers for unary, cast, binary, `?:`, assignment, comma; `sizeof`; `explicit_cast`; 400 lines of tautology/overflow/shift warnings | `exprparse.c` + `exprwarn.c` | label |
| `lit.c` | 587 | number, character and string literals to nodes | - | label |
| `complex.c` | 507 | `_Complex` as a two-field struct, arithmetic lowered to AST; also the general helpers `cplx_reuse`, `with_pre`, `zero_of`, `neg`, `scalar_cond`, `bad_operands` | helpers to `synth.c` | label |
| `atomic.c` | 258 | `_Atomic` objects of sizes other than 1/2/4/8: libatomic calls and CAS loops as AST | `atomicwide.c` | good |
| `bigint.c` | 185 | `_BitInt` over 128 bits: literal, eager folding, conversions to float, exactness, printing | - | good; body unreadable (Q-10) |
| `bigfloat.c` | 91 | run-time `_BitInt` <-> floating casts built as AST (no big floats) | `bitintcast.c` | good |
| `sym.c` | 600 | scopes and binding; unused-variable/static/typedef warnings; typo correction; node constructors, source ranges, the list builder `vec` | `scope.c` + `typo.c` + `node.c` | label |
| `token.c` | 450 | keyword tables; token window; `expect` failures and `;` recovery; `#pragma pack/weak/visibility/ms_struct` | `token.c` + `pragma.c` | label |

**What the module is for (draft opening comment).**  `cc/` turns the preprocessor's token stream
into typed AST tables for one TU.  Parsing and semantic analysis are one pass: a parse function
builds its node through a semantic action (`binop`, `assign`, `convert`, `member`...), which checks
operands, inserts implicit conversions as `N_CAST` nodes and reports clang's diagnostics.  Nothing
is folded at construction except `sizeof`, enumerators, float-to-int casts of constants and
`_BitInt` arithmetic; every other constant is computed when asked for by `eval`, also later from
emit threads, which is why `eval` and all it calls must not append to any table.  Invariants a
newcomer needs:

- **Nodes.**  A node id is a `uint32_t` index into `c->nodes` (24 bytes: `op`, `flags`, `aux`,
  `ty`, `loc`, `a`, `b`, `c`); 0 is "none".  Lists of ids are runs in `c->lists`; integer values
  are in `c->ipool` (`i128`), floating ones in `c->fpool` (a double, plus two slots of binary128
  bits for a 16-byte type), strings in `c->strs`.  Per node, `c->span` holds first and last token
  and `c->paren` the outer parentheses.  An implicit conversion is an `N_CAST` with no span; an
  error stand-in is an int 0 with `NF_ERR`.  All tables are address ranges reserved per TU from a
  pool (`tu.c:31-53`, 22 regions), never moved, so pointers stay valid across `mk`; only the bump
  heap checks its end (`tu.c:15`).  After parsing, nodes are read-only for emit threads.
- **Types.**  A `qty` is `index << 3 | qualifiers`.  The basic types have index == kind
  (`tu.c:69-80`), so `prim(k)` is `QT(k, 0)`.  Derived types are interned: one open-addressing
  table over the whole `ctype` (`type.c:44-101`), one for function types keyed by result and the
  parameter run in `c->params` (`type.c:186-268`), and a direct cache `ptrc[qty]` for pointers.
  Struct, union and enum types are not interned: `ty_new` makes one per tag and completion writes
  its size, alignment and (enum) base in place.  A typedef is not a type; `aligned` typedefs and
  `_Atomic(T)` are interned variants (`canon` points at the original; `n` holds T), so compare
  `canon()` indices, never raw ones.  `_Complex T` is a struct flagged `RF_COMPLEX`, one per
  element kind.  An array's length is `n | aux << 32`.
- **Symbols.**  One `sym` (80 bytes) per declaration, not per entity; redeclarations chain through
  `next`.  Scopes are two-level directories from identifier id to the innermost binding (`odir`
  ordinary, `tdir` tags, `ldir` labels) with the shadowed binding in `sym.shadow` and an undo
  stack per open scope (`sym.c:147-164`).  A tag has a `sym` of kind `S_TAG` whose `val` is the
  tag index.  Syms are never freed; the end-of-TU warnings scan all of them.

## 3. Passes and data flow (L4)

One pass builds the tree; these walks run inside it, on the subtree just built:

| Walk | Where | Computes | How often |
|---|---|---|---|
| `eval` | `const.c:522` | value, ICE flag, address constant | per `&&`/`||` left operand (`exprop.c:931`), per right operand of `/ % << >>` (`:526`) and of `&& ||` (`:850`), per comparison with a narrow operand (`:838`, `:840`, again `:780`), per signed `+ - *` on both operands (`:878`), per `?:` condition (`:994`), per conversion in an initialization, argument, return or assignment (`conv.c:604`), per pointer conversion (`is_npc`), per subscript decay (`conv.c:943`) |
| `promotion_bitfield` | `conv.c:73` | bit-field under comma/assign/pre-inc chains | every `rvalue` of an int-or-wider operand (`conv.c:1031`) |
| strip implicit casts | 17 open-coded loops (Q-11) | the operand as written | each warning check |
| `same_ref` | `exprop.c:599` | same object on both sides | each comparison not from a macro |
| `cplx_pure` | `complex.c:41` | may be evaluated twice | each lowered operand |
| `has_side_effects`, `objsize_unknown`, `constexpr_init_ok`, `bc_grammar`, `bc_check`, `bc_read` | `conv.c:163`, `:541`, `const.c:359`, `bitcast.c:144`, `:636`, `:358` | see section 4b | on demand |

After the body: `unused_vars` per closed scope (`sym.c:35`).  After the TU: two loops over every
sym in `unused_statics` (`sym.c:74`, `:81`), one in `unused_typedefs` (`:132`), `layout_checks` on
request.  At emit time the same `eval` runs again from about 50 call sites.

The repeated fact is "is this subtree constant, and what is it".  Nothing remembers the answer:
`mk_bin` (`conv.c:1073`) builds `N_BINARY` unfolded, so `1 + 2 + 3` is re-walked by each asker, and
a failing `eval` of `a + b + c + ...` descends the whole left spine before it meets the first
variable.  Per operator that is linear in the chain's length, so a chain is quadratic (inferred
from the code; not measured, see section 7).  `_BitInt` operands take the other policy: folded
when built (`big_fold_node`, `exprop.c:171`, `:190`, `:959`, `conv.c:32`).

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-ccexpr-01 | L7 | global state | `ccint.h:119-290`: tokens, parser context, diagnostics state and tables in one struct; `ccimpl.h:34-35` includes `emit/emit.h` and `driver/ccsnap.h` | emit sees all parser state; a C++ parser cannot own a tree without being a `struct cc` | `struct ast` (tables, pools, builtin types) embedded first in `cc`; type/const/layout functions take the `ast` | L | medium: hot field offsets move | `vcc-same.py`, `vbench.py gate` |
| Q-ccexpr-02 | L1 | near-duplicate | `bitcast.c:752-927` (`bc_lane`: unary and binary folding), `:718-750` (casts), `:713` (`bc_q`) against `const.c:683-707`, `:739-924`, `:623-665`, `:115` (`get_q`) | two folders; they already differ (shift past the width: clamp at `const.c:899`, fail at `bitcast.c:884`; division overflow checked only at `bitcast.c:890`) | value-level `cv_cast`, `cv_unary`, `cv_binary` in `const.c`, called by both | M | medium | `vcc-ctests.py --suite vcc`, `vcc-ldconst.py`, `vcc-same.py` |
| Q-ccexpr-03 | L4 | long function, redundant walk | `const.c:522-1015`; callers: 39 `cc_eval` + about 70 `eval` in `cc/`, 50 in `emit/`+`arch/`; `exprop.c:931`, `:850`, `:526`, `:838`, `:878` | 494 lines with six jobs; each asker re-walks; quadratic chains (inferred) | split by job (4c); then decide folding policy (decision 2) | M, then L | medium: clang's warnings depend on node shape | `vcc-diag.py`, `vcc-mutate.py`, `vbench.py gate`; a measurement is missing |
| Q-ccexpr-04 | L1 | pattern | temp local: `complex.c:84-88`, `:414-418`, `:485-489`, `:495-499`, `atomic.c:62-66`, `:76-80`, `bigfloat.c:14-15`, 9 more in `builtin.c`; `&x` node: `atomic.c:13`, `bigfloat.c:21`, `complex.c:410`, `:480`, `exprop.c:409`; finish a statement expression: `atomic.c:47-55`, `bigfloat.c:24-27`, `complex.c:450-453`, `builtin.c:2113-2115`; library call: `atomic.c:20-35`, `complex.c:297-313`; CAS retry loop: `atomic.c:154-163` = `:207-215` (197 tokens), `:179-184`, `complex.c:439-448`; `memory_order_seq_cst` as `5` 15 times | four private AST builders (`at_*`, `bf*`, inline, `builtin.c`'s); `builtin.c:1188-1200` repeats `atomic.c:39-72` | `cc/synth.c`: `mk_temp`, `mk_addr`, `mk_stmtexpr`, `mk_libcall`, `mk_cas_loop`, `MO_SEQ_CST` | M | low; node creation order must stay | `vcc-same.py`, `vcc-ctests.py --cpu cortex-a53` |
| Q-ccexpr-05 | L6 | misnamed | 4d; `N_COMMA.aux`: `builtin.c:1697` sets bit `NC_FP_NONNEG` = 1, `complex.c:505` stores `N_PREINC..N_POSTDEC` (14..17), read at `stmt.c:108` and `emit/prepass.c:194` | 15 and 17 have bit 0 set: a lowered complex `--` reads as "non-negative FP" if such a comma ever reaches `prepass.c:194` (none found: the types differ; latent) | named accessors per node kind; a flag of its own for "lowered from operator" | M | low | `vcc-same.py` |
| Q-ccexpr-06 | L5 | mixed file | `conv.c:185-716`, `:761-1009`; `exprop.c:519-912`, `:1046-1067`; `sym.c:24-144`, `:253-473`, `:482-600`; `layout.c:198-441`, `:177-196`, `:443-521`; `token.c:286-450`; `expr.c:1034-1246` | `tyname` is in `layout.c`, node constructors in `sym.c`, `-Warray-bounds` art in `conv.c`: nobody finds them by name | moves only: `convwarn.c`, `bounds.c`, `exprwarn.c`, `typename.c`, `layoutcheck.c`, `node.c`, `typo.c`, `pragma.c` | S each | low | unity and file-by-file builds, `vcc-same.py` |
| Q-ccexpr-07 | L1 | duplicate | `const.c:4-24` = `emit/type.c:4-25` (`em_trunc_to`); `const.c:26-34` = `arch/a64/emit/lval.c:13-21` (`elem_size`); `has_side_effects` `conv.c:163` against `side_effects` `arch/a64/emit/builtin.c:14` | two copies to keep in step; the two side-effect predicates answer differently (4b) | export `cc_trunc_to`, `cc_elem_size` in `ccint.h`; predicates: decision 7 | S | low | `vcc-same.py` |
| Q-ccexpr-08 | L1 | near-duplicate | subobject to initializer: `const.c:195-300`, `bitcast.c:83-131`, `:487-521`; range triple scan: `const.c:290-294`, `bitcast.c:120-124`, `:490-494`; truth of a `cval`: `const.c:413-414`, `:420`, `:934`, `:945`, `:956`, `bitcast.c:188-190`, `:199-201`, `:795-797`, `complex.c:78`, `builtin.c:1617`; cycle guard on `const_eval_path`: `const.c:162-169`, `:335-340`, `:390-397`, `:563-570` | the copies disagree on `CV_BIGINT` and on addresses (true in `const.c:934`, false in `bitcast.c:190`) | `init_member`, `init_elem`, `cv_truth`, a `CONST_EVAL_ENTER` helper in `const.c` | M | low | `vcc-ctests.py`, `vcc-same.py` |
| Q-ccexpr-09 | L1 | pattern | fraction bits, exponent bits, bias per floating kind as nested `?:`: `bigfloat.c:33-35`, `bigint.c:55-56`, `:106-107`, `const.c:501-502`, `bitcast.c:313-320`, `:562-563`, `builtin.c:730`, `:790`; x87's 80 valid bits: `bitcast.c:336-342`, `:549`, `:628`, `:676` | a new format (or a typo in one copy) is seven edits | `fltfmt[TY_*]` beside `basic[]` in `type.c` | S | low | `vcc-ldconst.py`, `vcc-f80.sh` |
| Q-ccexpr-10 | L6 | misnamed | `bigint.c`, `bigfloat.c`: one-space indent, many statements per line (`bigint.c:162`, `:183` are 423 and 350 characters), helpers `bfi bfu bfr bfo bft bfs bfl bfa`; magnitude prelude three times (`bigint.c:41-46` = `:92-96` = `:116-119`) | cannot be reviewed line by line; `big_fold_node` is 40 lines and several screens of code, so the size ratchet misses it | reformat the two files (276 lines), `big_magnitude()`, names as Q-04 | S | low | `vcc-same.py`; decision 5 |
| Q-ccexpr-11 | L1 | pattern | "implicit = `N_CAST` without span" open-coded 17 times: `exprop.c:591`, `:601`, `:603`, `:627`, `:629`, `:1049`, `conv.c:270`, `:494`, `:584`, `:628`, `expr.c:1081`, `sym.c:499`, `:513`, `stmt.c` x2, `declspec.c` x2; three spellings (with and without `&& N(n)->a`); the helper `no_implicit` is static (`exprop.c:589`) | the invariant is written nowhere; `setspan` gives any spanless node a span, so `__real__ x` (`exprop.c:266-271`, then `:303`) can turn a promotion into an "explicit" cast (inferred) | `strip_implicit()` inline in `ccimpl.h`; state the rule at `setspan` | S | low | `vcc-diag.py`, `vcc-same.py` |
| Q-ccexpr-12 | L10 | stale doc | `ccimpl.h` sections name the wrong file: under `conv.c` nine functions of `const.c`, `init.c`, `decl.c`, `stmt.c` (`:382-390`); under `type.c` the `big_*` (`:215-224`); under `complex.c` the `atomic_*` (`:418-422`); under `sym.c` `delayed_bug`, `fwd_note` (`diag.c`); `cc_eval` declared at `ccint.h:358` and `ccimpl.h:437`; orphan comments `ccimpl.h:750-751`, `type.c:195`, `conv.c:338`, `:537`, `token.c:152`; three accessor sets `CT/CKIND/CN` (`ccint.h:354-356`, `CN` unused), `T/KIND/N` (`ccimpl.h:55-56`, `:66`), again `emitimpl.h:89`, `:115-116`; `cc_eval`, `cc_is_signed` defined in `builtin.c:450-451` | the header is the module's map and points the wrong way; `eval` and `cc_eval` are one function under two names inside `cc/` | regenerate the sections from the definitions (a script, as `gen-walkprefix.py`); one accessor set in `ccint.h` | S | low | both builds |
| Q-ccexpr-13 | L9 | error policy | `malloc`/`calloc`/`realloc` beside the arenas: `lit.c:399`, `:425`, `bigint.c:13`, `:18`, `:25`, `:41`, `:92`, `:116`, `:120`, `:152`, `:162`, `:172`, `bitcast.c:251`, `record.c:12-14`, `layout.c:427`, `:445`; out of memory answered four ways: `fatal` (`bigint.c:13`), `return false` (`bigint.c:42`, `bitcast.c:252`), `fputs` + `abort` (`record.c:16`), bare `abort` (`layout.c:428`); no end check on nodes, syms, lists, types (`sym.c:196`, `:486`, `:549`, `type.c:73`, `:105`) | a failed allocation in a constant read silently makes an expression "not constant"; table overflow is a fault, not a message | scratch arena from `lib/arena.h`; one `cc_oom`; one checked bump for the tables or a stated limit | M | low | `vcc-stress.py`, ASan suite |
| Q-ccexpr-14 | L4 | long function | `expr.c:1034-1246` `call`: arguments, overloads, arity messages, conversions, `-Wnonnull`, `-Wformat`, then four library rewrites; "global, not static, no asm name, not disabled" three times (`:1169-1172` = `:1194-1196` exact, `:1182-1185`); "callee is a function sym" five times (`:1089`, `:1129`, `:1144`, `:1165`, `:1182`); libm folded with the host's libm (`:1219-1233`) | 213 lines; the rewrites are hidden in the parser; emit has its own `direct_callee` (`emit/prepass.c:541`) | `callee_sym()`, `lib_call_rewrite()` in `builtin.c` | S | low | `vcc-same.py` |
| Q-ccexpr-15 | L1 | near-duplicate | `++`/`--`: `exprop.c:94-117` against `expr.c:1413-1435`; compound literal: `exprop.c:216-224` (no `body`, no `sc`) against `:431-443`; `N_MEMBER` built at `expr.c:459-467`, `:497-503`, `complex.c:106-110`; subscript checks inline in the postfix loop `expr.c:1289-1350`; unary checks inline in `exprop.c:143-191`; the four-context message switch `conv.c:200-215`, `:277-289`, `:360-372` (arm `:204-206` is dead: `:195` returns first) | no single action for subscript, unary or `++`: what a C++ front end hooks operator overloading on | `incdec()`, `subscript()`, `unary_op_node()`, `mk_complit()`, `mk_member()`, `conv_phrase()` | M | low | `vcc-diag.py`, `vcc-same.py` |
| Q-ccexpr-16 | L10 | dead | `expr.c:533` (`&& 0`), `:1359`+`:1366` (`nloc`), `const.c:871`+`:918` (`bits`), `bigint.c:152`+`:182` (`scratch`), `token.c:350-352` (`if (...) return; return;`), `lit.c:581-582` (assigned twice), `exprop.c:566`+`:572` (parameter `v`), `:645`; `abort()` after a `noreturn` call `token.c:170`, `expr.c:633`; `unsigned_of()` on a type the branch knows is unsigned, 14 times (`const.c:885-886`, `:904`, `:910-913`; inferred equivalent to `trunc_to(c, x, ot)`) | noise; each `unsigned_of` on a `_BitInt` goes through `ty_intern` from an emit thread (a lookup that hits, inferred) | delete | S | low | `vcc-same.py` |
| Q-ccexpr-17 | L2 | duplicate | `lit.c:156-170` = `pp/ifexpr.c:23-37` (digits, 116 tokens), `lit.c:291-300` = `pp/ifexpr.c:79-88` (escapes), `lit.c:28-32` = `pp/ifexpr.c:60-64`; UTF-8 decode `lit.c:455-467` = `:537-548` (127 tokens); rehash `type.c:79-94` = `:234-251`, again `replay.c:960-971`, `:1010-1023`; `ty_eq` = `replay.c:1715-1720`; u32 hash set `bitcast.c:233-273`; `dslot` `sym.c:6-15` = `pp/hcrec.c:9-16`; FNV-1a inline `token.c:354-356`; the list builder `vec` (`ccimpl.h:70-74`, `sym.c:533-557`) beside `lib/vec.h` | literal rules live in pp and cc; four private hash tables in this area | `lib/` literal helpers (digits, escape, UTF-8); one u32 open-addressing set; see `quality-lib.md` | M | low | `vcc-diff.py cmp`, `vcc-same.py` |
| Q-ccexpr-18 | L8 | global state | conversion context in `c->cv_what/cv_loc/cv_fn/cv_arg/cv_callee`, saved and restored by hand (`conv.c:591-596`, `:721-733`, `expr.c:1144-1148`); diagnostics side channels `c->dms`, `pp_diag_next_id` (`conv.c:300-309`, `:662-672`), `pp_diag_pc` (`expr.c:736`); `kwmap == kwmap_c23` decides a string's element type (`lit.c:497`); `lay_record` reads the `#pragma pack` stack (`layout.c:161`); replay hooks inside `ty_intern`, `ty_new`, `ty_func`, `lookup` (`type.c:71`, `:110`, `:223`, `sym.c:234`) | sema's answer depends on fields set somewhere up the call chain; a missed restore mislabels the next diagnostic | a `convctx` argument; `pack` as an argument of `lay_record`; `c->o->std` in `lit.c` | M | low | `vcc-diag.py`, `vcc-bflayout.py` |

### 4a. Parsing, checking, folding: where the line is, and what a C++ front end can reuse

*Parse functions* (consume tokens): in `expr.c` `generic_sel`, `primary_kw`, `primary_unbound`,
`stmt_expr`, `paren_expr`, `primary_`, `call`, `postfix_ops`; in `exprop.c` everything from
`sizeof_operand` to `const_expr` except the warnings; `bitcast_expr`; `lit.c`; `record.c`.
*Semantic actions* (nodes in, node out, no token consumed): `binop`, `assign`, `conditional`,
`member`, `deref`, `addr_of`, `explicit_cast`, `sizeof_type`, `swizzle`, all of `conv.c`,
`complex.c`, `atomic.c`, `bigfloat.c`.  *Folding* happens in neither: it is `eval`, on demand.
The line is clean for binary operators, assignment, `?:`, member access and conversions.  It is
missing for subscripts, unary operators, `++`/`--` and calls, whose checks sit inside the parse
loops (Q-15, Q-14), and for struct members: `struct_body` parses and records a field in one 200-line
function with no "add this field" entry (`record.c:35-233`).

Reusable as they are, called with a `cc`: `type.c` (no token, no context; `compatible` calls
`eval` for VLA bounds, `type.c:518`), `lay_record` (C rules only: no bases, no vptr, but it keeps
data size apart from size, `layout.c:13`), `eval` and `const.c`, `conv_kind`, `rvalue`, `decay`.
What ties them to the C parser:

- the struct itself (Q-01); `N()` and `ISERR()` name a variable `c` (`ccimpl.h:66-67`);
- parser context read by sema: `c->dead`, `c->unevaluated`, `c->cexpr`, `c->fn`, `c->defer_rt`,
  `c->constexpr_init`, `c->cv_*` (24 uses in `conv.c`), `c->dms`;
- token state read by actions: `c->prevloc` as "the member name" or "the `)`" (`expr.c:88`,
  `:433`, `:454`, `:471`, `:496`, `:1008`); `primary_sym` looks at the next token to decide "used"
  and "assigned" (`expr.c:662-671`);
- C-only rules with no seam: `conv_check` (C's assignment constraints), `compatible`/`composite`,
  one lvalue bit (`NF_LVALUE`), `eval` with no call frames and aggregates only as initializer nodes;
- no room in the formats: `ctype.flags` is 8 bits, all taken (`cc.h:56-65`); `node.flags` 8 bits,
  all taken (`cc.h:245-247`); a `qty` has 3 qualifier bits, all taken; `sym.flags` has 4 of 32
  free.  References, value categories and `noexcept` need bits that do not exist (decision 3).

### 4b. Recursive predicates over expressions

| Predicate | Where | Fact | Same fact elsewhere |
|---|---|---|---|
| `has_side_effects` | `conv.c:163` | may change state: any call, assignment, `++`, statement expression, any builtin, `va_arg`; ignores volatile reads and initializer lists (5 callers, all in `init.c`, `builtin.c`, `declarator.c`) | `side_effects` `arch/a64/emit/builtin.c:14` (27 call sites): adds volatile and atomic reads, looks inside builtins, statements; `array_copy_effects` `init.c:750` and `elementwise_abs_effects` `builtin.c:1598`: add volatile reads, pure callees, short-circuit by `eval` |
| `cplx_pure` | `complex.c:41` | twice equals once, and cheap: depth 8, no volatile, no call, builtin only if constant | the negation of the above with a budget; `em_unused_pure` `emit/opt/unusedinit.c:12`, `em_bound_pure` `emit/opt/cmpbounds.c:8`, `same_pure` `arch/a64/emit/cond.c:17` |
| `eval`, `eval_addr`, `const_int`, `is_npc` | `const.c:522`, `:43`, `:1038`, `:1018` | is constant (value, ICE, address) | `bc_lane` `bitcast.c:752` (Q-02); `local_constant` `func.c:773`, `sv_constant` `emit/opt/switch-values.c:24`, `wk_constant_p`/`a64_constant_p`/`wconstant_p` (not read) |
| `constexpr_init_ok` | `const.c:359` | C23 constant-initializer grammar | `bc_grammar` `bitcast.c:144` (same recursion with bit_cast rules, falls back to it at `:218`); `static_init_ok` `init.c` (not read) |
| `constexpr_type_ok`, `bc_type` | `const.c:445`, `bitcast.c:38` | type recursion over arrays and members | same shape, different rule |
| `constexpr_subobject`, `bc_initializer` | `const.c:195`, `bitcast.c:83` | initializer of `x.m` / `x[i]` | each other (Q-08); `cc_init_subobject` `const.c:304` for emit |
| `objsize_unknown` | `conv.c:541` | depends on an object size vcc does not know | - |
| `same_ref` | `exprop.c:599` | both name one object | `same_pure` (arm64) for another purpose |
| `promotion_bitfield`, `bitfield_of` | `conv.c:73`, `:60` | value is a bit-field | - |
| `mark_addr_taken`, `addr_of`'s register walk | `expr.c:531`, `:556` | base variable of a member chain | each other |
| `is_flt_lit` | `const.c:100` | a floating literal under no-op casts | - |
| lvalue | `NF_LVALUE`, set at construction | - | not recomputed anywhere: good |

Three of these compute "has effects" with three answers.  One predicate with a mask (calls,
writes, volatile reads, unevaluated builtins) would serve all six callers' families; whether the
differences are wanted is decision 7.

### 4c. `eval`: one function, six jobs

`const.c:522-1015`, 494 lines, plus `eval_builtin` in `builtin.c`.  Jobs, by arm: (1) the ICE
flag, threaded through every arm; (2) integer folding with C wrap rules (`:867-924`); (3) floating
folding at two precisions, double and binary128/x87 (`:817-866`, casts `:623-665`); (4) address
constants for static initializers: symbol, string or label plus offset, label differences, pointer
comparisons (`:750-816`, `eval_addr` `:43-98`); this is what `emit/data.c` wants; (5) reads of
constant objects (`:556-577`, `:986-989`, through `constexpr_value` and `bitcast_value`); (6)
clang's library folds (`strlen` `:717-738`, string elements `:990-1008`).  The `N_CAST` arm is 105
lines, `N_BINARY` 187.  Split along the jobs: `eval_cast`, `eval_binary_addr`, `cv_binary_flt`,
`cv_binary_int`, `eval_strlit_elem`; the `cv_*` ones take values, not nodes, and replace
`bc_lane`'s copies (Q-02).  `eval` itself reports nothing and appends nothing: keep that as the
stated contract (today it is three comments: `const.c:133-135`, `:176-178`, `bigint.c:1-2`).

### 4d. Node fields with several meanings

| Field | Holds | Names that would say it |
|---|---|---|
| `a` | operand node; pool index (`N_NUM`, `N_FNUM`); offset in `lists` (`N_BIGNUM`); string index (`N_STR`); sym (`N_VAR`, `N_DECL`); label (`N_LABELADDR`, `N_LABEL`, `N_GOTO`); asm record (`N_ASM`); active union member (`N_INIT`, aux 2: `const.c:261`, `bitcast.c:104`, not in `cc.h:203`) | `n_operand`, `n_pool`, `n_sym`, `n_label`, `n_active_member` |
| `b` | second operand; tag (`N_MEMBER`, `N_DESIG`); list offset (`N_CALL`, `N_BUILTIN`, `N_BLOCK`, `N_INIT`); word count (`N_BIGNUM`); a `qty` (`N_SIZEOF_VLA`, `exprop.c:19`); sym (`N_COMPLIT`; `N_CAST`: the typedef, `exprop.c:454`); pool index (`N_CASE` hi) | `n_rhs`, `n_tag`, `n_list`, `n_vla_type`, `n_object_sym`, `n_cast_typedef` |
| `c` | third operand (`N_COND`, statements); field index (`N_MEMBER`); count (`N_CALL`, `N_BUILTIN`, `N_BLOCK`, `N_INIT`); a `qty`: the computation type of `N_ASSIGN` (`expr.c:314`) and the vector type of `N_INDEX` (`expr.c:149`, `:1346`), neither in `cc.h`; a folded FP node (`N_CAST`, `conv.c:39`); list of [step, body] (`N_FOR`) | `n_field`, `n_count`, `n_compute_type`, `n_vector_type`, `n_folded` |
| `aux` | operator token (`N_UNARY`, `N_BINARY`, `N_ASSIGN`); `CK_*`; `BI_*`; `NUM_*` bits; initializer kind 0/1/2; designator kind; 1 = bool-vector lane (`N_INDEX`, `expr.c:1347`); 1 = capture (`N_SIZEOF_VLA`, `N_DECL`); `N_COMMA`: `NC_FP_NONNEG` or an inc/dec node kind (Q-05); `N_STMTEXPR`: `N_ASSIGN` or an inc/dec kind meaning "lowered, do not warn" (`atomic.c:53`, `complex.c:453`) | `n_op`, `n_cast_kind`, `n_builtin`, `n_num_flags`, `n_init_kind`, `n_lowered_from` |

Also: `sym.val` is an enumerator's value and an `S_TAG` sym's tag index (`layout.c:382`);
`cval.i` is the integer and an address's byte offset; `cval.str` and `.label` are index + 1 while
`.sym` is the index; `ctype.n` is precision, length, parameter count, size node or atomic's base;
`field.off` is in bits where every other size is bytes.

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `N`, `T`, `KIND` | `ccimpl.h:55-66` | node/type accessors, `N` with a hidden `c` | one set in `ccint.h`, `c` explicit |
| `mk`, `mk2`, `mk_bin`, `mkstmt` | `sym.c:484`, `:524`, `conv.c:1073`, `stmt.c` | node constructors in three files | `node_new*` in `node.c` |
| `rng`, `rng_np` | `sym.c:497-522` | a node's source range, with/without parentheses | `node_range`, `node_range_bare` |
| `vec`, `vinit`, `vpush`, `vfreeze` | `ccimpl.h:70`, `sym.c:533-557` | builder of a node-id list | `idlist_*`; "vec" also means GNU vectors (`vec_scalar_ok`, `vec_mask_type`) |
| `cplx_reuse`, `with_pre`, `neg`, `zero_of`, `scalar_cond` | `complex.c:67-180` | evaluate-once temp; sequence a statement before a value; `-x`; a zero of a type | `once()`, `seq()`, `mk_neg`, `mk_zero`; out of `complex.c` (used by `expr.c:337`, `:1206`) |
| `promote` / `promoted` | `type.c:411`, `conv.c:87` | promotion of a type / of a node, bit-fields included | `promote_type`, `promoted_type_of` |
| `canon`, `complete`, `rank`, `prim`, `composite`, `qualify` | `type.c` | type functions with no prefix beside `ty_*` constructors | `ty_canon`, `ty_complete`, ... |
| `cc_error`, `cc_warnx`, `cc_errorx`, `cc_warn_begin` | `ccimpl.h:165-205` | internal (VPRIV) diagnostics with the exported prefix | `cc_` only for what `ccint.h` exports |
| `conv_warn` | `conv.c:353` and `cc_opts.conv_warn` (`cc.h:296`) | a function and an option bit set | option: `conv_downgrade` |
| `dead`, `cexpr`, `cnd`, `bcp`, `sie`, `ibr`, `dms`, `dfx` | `ccint.h:128-239` | "in an unevaluated branch", "in a constant expression", ... | spelled out, grouped in a `sema` context (Q-01) |
| `at` | `dspec.at`, `declr.at` (attributes); `at_*` in `atomic.c`; `at_attribute` (position) | three meanings | `attrs`; `atw_*`; keep the third |
| `big_` | `bigint.c`, `bigfloat.c` | `_BitInt` over 128 bits; `bigfloat` has no big floats | `bitint_`; file `bitintcast.c` |
| `bfi` ... `bfa` | `bigfloat.c:7-18` | builder shorthands | Q-04's names |
| `S_TAG` | `ccimpl.h:64` | a sym kind defined outside the `S_*` enum (`cc.h:121`) | into the enum |
| `plb`, `ple`, `psyms`, `tdef`, `ualign`, `f` | `cc.h:108-173` | parameter-list begin/end, typedef used, unadjusted alignment, fields | spelled out |
| `lit_esz(kind, target)` | `lit.c:358` | the second parameter is `short_wchar` | rename |
| `i128s_`, `idlen_`, `idstr_`, `IDF` | `conv.c:782`, `ccimpl.h:49` | formatting helpers with trailing underscores | `fmt_i128`, `id_len`, `id_str` |

Prefixes that hold: `ty_` (constructors), `lay_`, `bc_`, `cplx_`, `swz_`, `ovl_`, `lz_`, `dr_`,
`uv_`, `rt_`.  Where they break: type predicates and rules have none; `const.c` has none
(`eval`, `trunc_to`, `is_npc`) beside `constexpr_*`; expression actions have none (`binop`,
`assign`, `member`, `deref`, `call`).  Words with two meanings: "vec", "at", "big", "const"
(constant evaluation, the qualifier, `-Wconstant-conversion` in `const_conv`), "base" (pointee
type, radix, scratch index, member base), "tag" (record, `S_TAG` sym, `tagdecl`), "pre"
(statement to run first, pre-increment).

## 6. Decisions needed

1. **An AST context apart from the parser** (Q-01).  Options: (a) `struct ast` embedded in `cc`,
   functions migrate file by file; (b) leave it and give C++ its own `cc` variant; (c) a full
   three-way split (ast, sema context, parser).  Recommendation: (a) now, since it is mechanical
   and emit then includes `ast.h` only; (c) when the C++ parser starts.
2. **Folding policy** (Q-03).  Lazy `eval` everywhere (today), or fold integer and floating
   constants when the node is built, as `_BitInt` already does.  Folding at build changes node
   shapes that clang-parity warnings read (`NUM_RADIX`, spans, enumerators), so first measure:
   instructions spent in `eval` per kernel TU and on a long chain (`vperf`).  Recommendation:
   split `eval` regardless; fold only if the measurement shows more than 1 %.
3. **Bits.**  `node.flags`, `ctype.flags` and the qualifier bits are full.  Options: take 8 bits
   from `node.aux` (every value stored there today fits in 8: `T_NKINDS` is 63, `BI_COUNT` 159,
   `CK_*` and `NUM_*` under 32), grow `node` to 32 bytes (measure), or side tables.  Needed before C++ and before
   Q-05's "lowered" flag and Q-11's "implicit" flag.
4. **Where front-end lowering lives** (Q-04): stay as AST rewriting in `cc/` (three back ends and
   C++ get it for nothing; warnings must then see through it, as `stmt.c:108` does), or move to
   the shared walker or VIR.  Recommendation: stay, behind one builder module.
5. **Reformat `bigint.c` and `bigfloat.c`** (Q-10), an exception to "no mass reformat".
   Recommendation: yes, 276 lines, no open branch is likely to touch them (to be checked).
6. **Records for C++**: extend `lay_record` with bases and vptr, or a second builder calling
   `lay_field`.  Recommendation: second builder; first give `struct_body` an "add field" action.
7. **Side-effect predicates** (4b): one parametric predicate, or the family documented with its
   differences.  Needs a table of which caller relies on which difference; `quality-walker-a64.md`
   and `quality-codegen-shared.md` hold the other half.

## 7. Not covered

- The rest of `cc/` (other reviewers), including `eval_builtin`, `static_init_ok` and the nine
  temporaries in `builtin.c`, which I located by grep and the clone scan only.
- The other side of cross-module duplicates (`pp/ifexpr.c`, `replay.c`, `emit/type.c`,
  `arch/a64/emit/builtin.c`) was read at the cited lines, not in full.
- No measurement: the quadratic behaviour in section 3 and the cost of repeated `eval` are read
  from the code.  The claim in Q-16 that `unsigned_of` is redundant, and in Q-11 about
  `__real__`, are inferred, not tested.
- The count of `struct cc` fields used outside `cc/` (24 by emit and arch, 67 with `driver/`) is
  from a regex over `c->NAME`, approximate.
- `bench/quality.py --clones 999 --min-tokens 40 --only cc/`: 378 runs under `cc/`; I triaged
  those touching my files (about 130).  Real second copies are in Q-02, -04, -07, -08, -10, -14,
  -15, -17; the predicate runs against `arch/wasm/emit/reachability.c` and `wexpr.c` are the same
  switch shape over different facts, not copies; `type.c:15-26` and `layout.c:366-373` are tables.
- `lib/f128.h`, `lib/bigint.h` and `emit/emit.h` were not read.
