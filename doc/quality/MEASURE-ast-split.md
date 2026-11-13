# MEASURE-ast-split: the 24 AST fields code generation reads, and what embedding them costs

Open decision (PLAN-quality.md): when `struct cc` is split into AST and parser,
check the 24 fields code generation reads against the hot paths; the split must
cost nothing.  Finding Q-BND-01 of `doc/review/quality-boundaries.md` proposes an
`ast.h` with a `struct ast` embedded in `struct cc`.  Measured on main at
6d603396 (macOS arm64 host, Apple clang `-O3` as the Makefile builds it).

## 1. The 24 fields, verified against today's source

Method: every `c->F` in `src/codegen/`, `src/arch/{a64,x64,wasm}/codegen/` (all
code generation names its `cc *` `c`: 510 parameters, no other name), for each
field of `struct cc` (`src/include/frontend/c/ccint.h:119`); plus the macros that
hide a read: `N(i)` = `c->nodes[i]` (832 uses), `T(c,t)`/`KIND(c,t)` = `c->ty[..]`
(243 + 173 uses).  The review's list is right: 22 table fields plus the two
pointers `o` (options) and `p` (preprocessor, for diagnostics) are the 24 that
are read; no other field is read, and the n-counters `nty nparams ntag nlist nipool
nfpool nattr nasm` are not (they belong in the AST anyway: they are the tables'
lengths, and the parser writes them).

| Field | Reads (uses / files) | Class | Hottest reader |
|---|---|---|---|
| `nodes` (and `N()`) | 832 via macro + 8 direct | per node | every walker/emitter function; `arch/a64/codegen/expr.c:gen_inner` |
| `ty` (and `T()`, `KIND()`) | 416 via macros + 4 | per node | `arch/a64/codegen/expr.c:gen_inner`, `codegen/walk/expr.c:wk_gen_inner` |
| `syms` | 276 / 38 | per node (symbol operands) | `codegen/walk/stmt.c:wk_stmt1` (10), `codegen/prepass.c:walk` (9), `arch/wasm/codegen/wcall.c:wcall` |
| `lists` | 153 / 24 | per node (call args, init lists) | `arch/wasm/codegen/wcall.c:wcall` (10), `codegen/prepass.c:walk` (8), `codegen/walk/expr.c:wk_call` (8) |
| `strs` | 58 / 21 | per node (string literals) | `arch/x64/codegen/asm.c:x64_asm` (8), `codegen/walk/expr.c:wk_call` (5) |
| `ipool` | 47 / 16 | per node (wide integer constants), per switch case | `arch/a64/codegen/stmt.c:gen_switch` (8), `codegen/walk/expr.c:wk_gen_inner` |
| `fpool` | 6 / 3 | per node (FP constants) | `codegen/walk/expr.c:wk_gen_inner`, `arch/a64/codegen/expr.c:gen_inner` |
| `tags` | 19 / 11 | per node (record member access, init) | `codegen/data.c:data_init`, `arch/x64/codegen/abi.c:x64_classify0`, `codegen/walk/expr.c:wk_lval` |
| `params` | 6 / 4 | per call | `codegen/prepass.c:note_fwrite`, `codegen/opt/tail.c:em_tail_candidate` |
| `labels` | 7 / 6 | per label statement | `codegen/walk/stmt.c:wk_stmt1`, `codegen/prepass.c:walk` |
| `asms` | 7 / 6 | per inline-asm statement | `arch/a64/codegen/asm.c:gen_asm`, `codegen/prepass.c:walk` |
| `t_size`, `t_valist` | 5 + 9 | per node, rare (VLA, `va_*`) | `codegen/walk/expr.c:wk_vla_fix`, `wk_builtin`; `prepass.c:scalar_valist` |
| `attr` | 1 | per symbol (`sym_attrs`) | `codegen/type.c:sym_attrs` |
| `o` | 32 / 15 | per function (a pointer to `cc_opts`: `c->o->pic` ...) | `arch/a64/codegen/func.c:sym_header`, `codegen/prepass.c:{sve,bitint}_codegen_ok`, `walk/expr.c:wk_lval` (`pic`, `tlsmodel`) |
| `nnode`, `nlabel`, `nsym` | 17 + 17 + 62 | once per TU (sizing the walker state), `nsym` also loops over all symbols | `codegen/walk/stmt.c:wk_state` (and `wk_ecx_free`), `codegen/tu.c:emit_tu1` (23 uses of `nsym`) |
| `nstr` | 6 | once per TU | `codegen/tu.c:emit_tu1`, `replay` |
| `toplevel`, `ntoplevel`, `tlasm`, `ntlasm` | 3 + 3 + 1 + 2 | once per TU | `codegen/tu.c:emit_tu1`; `arch/wasm/codegen/wasm.c:wasm_tail` |
| `p` | 9 / 3 | only on a diagnostic | `codegen/tu.c:ediag`, `eerror`, `codegen/prepass.c:{sve,bitint}_codegen_ok` |

Per node: 8 table pointers plus `nodes`/`ty`; per function: `o`; per TU: the
counters and the file-scope lists; on error only: `p`.  `nodes`, `ty`, `syms`,
`lists` are the ones whose address is computed on every AST visit.  Four
corrections to the review: `nasm`/`nlist` are not read (the review counted them
in `struct ast` for the arrays they size, which is right for the split); `ty` is
read directly 4 times but 416 times through `T()`/`KIND()`; `c->o` is a pointer
(`cc_opts` is a separate object), so `c->ast.o->F` is two loads exactly as
`c->o->F` is today; `p` likewise.

## 2. What embedding changes in the code generated

### Where the fields are today

`offsetof(struct cc, F)`, today (`sizeof(struct cc)` = 117 688 bytes):

| Offset | Fields |
|---|---|
| 0, 8 | `p`, `o` |
| 976-1336 | `ty` 976, `params` 1008, `tags` 1056, `syms` 1192, `nsym` 1200, `nodes` 1208, `nnode` 1232, `lists` 1240, `ipool` 1256, `fpool` 1272, `strs` 1288, `nstr` 1296, `labels` 1304, `nlabel` 1312, `attr` 1320, `asms` 1336 |
| 117 440-117 536 | `t_size` 117440, `t_valist` 117460, `ntoplevel` 117516, `toplevel` 117520, `tlasm` 117528, `ntlasm` 117536 |

The big gap comes from the parser's arrays between them: `heap` at 1384,
`odir/tdir/ldir` at 1424, `uint32_t smark[4096]` at 99 740 (16 KB).

### The experiment

A local branch `tmp-ast-embed` (never pushed): the 24 fields and the table
counters moved, in their order, into an anonymous `struct { ... };` that is the
first member of `struct cc`.  An anonymous member has the layout of the named
member struct `struct ast ast;` in the same place and `c->syms` becomes the same
address computation as `c->ast.syms`; using it needed no access rewrite and so
could not hide a second load.  New offsets: `p` 0, `o` 8, `ty` 16, `params` 32,
`tags` 48, `syms` 64, `nsym` 72, `nodes` 80, `nnode` 88, `lists` 96, `ipool` 112,
`fpool` 128, `strs` 144, `nstr` 152, `labels` 160, `nlabel` 168, `attr` 176, `asms`
192, `t_size` 204, `t_valist` 208, `ntoplevel` 212, `toplevel` 216, `tlasm` 224,
`ntlasm` 232; `sizeof` unchanged.

Built twice (`make -C src -j 3 vcc OUT=build/base` and `OUT=build/ast`, unity)
and compared `objdump -d` of `obj/unity/codegen.o` function by function
(whole-object: 2952 functions, 272 788 instructions before, 272 735 after).

### Result

- Of 2 952 functions (same set before and after), 2 633 are instruction for
  instruction identical, 288 differ only in an immediate (a `[x,#disp]` or a
  jump-table `adr`), 3 (`wk_function`, `wk_cond`, `vw_wk_cond`) have the same
  length but another register choice or an `ldp` pair, and 28 have another length
  (27 shorter, 1 longer).  `gen_switch` (arm64 switch lowering, 2 140
  instructions): 22 instructions differ, all displacements.
- The two hot functions the review points at:
  - `arch/a64/codegen/expr.c:gen_inner` (4 186 -> 4 185 instructions,
    the per-node expression emitter).  Besides the jump-table `adr`, one
    hunk: `ldr x8,[x19,#8]` (`c->o`) and `ldr x9,[x19,#0x3d0]` (`c->ty`)
    became one `ldp x8,x9,[x19,#8]`, because `o` and `ty` are now adjacent.
  - `codegen/prepass.c:walk` (880 -> 877, the prepass over every node):
    the loads of `c->ty` and `c->nodes` are unchanged in
    form; the three removed instructions are
    `add x8,x21,#0x1c,lsl #12` / `mov w23,#0xcad4; movk w23,#1,lsl #16`, the
    address arithmetic that reached `t_valist` at offset 117 460.
  - `codegen/walk/expr.c:wk_gen_inner` (the shared walker, VIR's copy
    `vw_wk_gen_inner` too): displacements only, plus one `ldp` pair
    (`o`,`ty`) and a basic-block reshuffle (+1 instruction in the VIR copy,
    the only function that grew; 27 shrank, net -53 over the object).
- Nothing becomes a second load: every `c->F` stays `ldr Rd,[c,#disp]`
  (`ty nodes syms lists ...` are already pointers to arrays, `o`/`p` already
  pointers to other objects).  A second load would appear only if
  `struct ast` were a separate allocation reached through `c->ast` as a pointer;
  the proposal says embedded, and this measures embedded.

### Displacement-encoding boundaries

- arm64 `ldr` with an unsigned scaled 12-bit offset reaches 32 760 bytes
  for an 8-byte load and 16 380 for a 4-byte one; `ldp x,x` reaches 504.  The
  18 fields at 976-1336 are inside the 12-bit range now and after.  The six at
  117 440-117 536 (`t_size t_valist toplevel ntoplevel tlasm ntlasm`) are outside
  today, which costs the `add x,x,#0x1c,lsl#12` or `mov`+`movk` above; embedding
  first brings them into range, so this boundary is crossed in the cheaper
  direction.  After embedding, everything is under 240, so `ldp` can pair
  adjacent fields (`o`+`ty`, `syms`+`nsym`-style pairs when the compiler wants
  them): that is what removed the one instruction in `gen_inner`.
- The inverse risk: if `struct ast` were placed after `smark[4096]` (offset
  above 32 760) every hot read would need the extra `add`.  An AST member must
  precede the parser's big arrays, and the first 504 bytes are the place for
  the per-node tables.
- x86-64 (`c->F` in `arch/x64/codegen/`, the x86-64 emitter's disp): disp8 reaches
  -128..127 and everything beyond costs disp32 (3 bytes more per access).
  Today all 22 table fields are disp32 (offset >= 976, and `p`,`o` at 0/8).
  Embedded first, `ty`, `params`, `tags`, `syms`, `nsym`, `nodes`, `nnode`,
  `lists`, `ipool` (16-112) become disp8; `fpool` (128) and the rest stay disp32.
  That is a code-size gain, not a cost.  Not measured: no x86-64 host here; this
  is read off the encoding rule, and `bench/vcc-x86.py` is the gate for it.

### Instructions retired

`bench/vbench.py gate` (instructions, base `build/base` against `build/ast`):
section 4.

## 3. Conclusion

1. The split costs nothing in the generated code: with the AST member
   embedded (not pointed to) and placed first in `struct cc`, the emitter's reads
   are the same `ldr` with another displacement; the object has 53 fewer
   instructions of 272 788, and the hot functions change by an `ldp` and by the
   removal of the large-offset arithmetic for `t_size`/`t_valist`/`toplevel`.
2. Constraints: embed it (a pointer is a second load per access); put it before
   the parser's arrays (`odir`, `smark`: anything beyond 32 760 bytes loses the
   scaled `ldr` and 504 loses `ldp`); keep the per-node tables `ty nodes syms
   lists strs ipool` together at the start, and `o` next to `ty`.  The six rarely read
   fields (`t_size t_valist toplevel ... ntlasm`) gain from moving into the AST.
3. The split PR must prove: both builds (unity and `VCC_UNITY=`);
   `bench/vcc-same.py --base` (identical output); `objdump` of `codegen.o` before and
   after showing only displacement and `ldp` changes (the comparison script of this
   measurement, `offsetof` table in the PR); `bench/vbench.py gate` within the 1 %
   budget; `bench/vcc-x86.py` for the disp8 change; `static_assert(offsetof(struct cc, ast) == 0)`
   and `sizeof(struct ast) < 504` so the placement cannot drift.

## 4. Instruction counts

`nice -n 10 python3 bench/vbench.py gate --base build/base --bin build/ast`
(base = main at 6d603396, new = the same plus the throwaway embedding; budget 1 %).
Load: 1-minute average 5 when the wait ended, but a load of 26 at the start of the
run (other agents), 19 at its end; the instruction counts are insensitive to
that, the cycle and time columns are not and should be disregarded.

| Phase | Instructions, new against base | Noise |
|---|---|---|
| lex | -0.03 % | |
| pp | +0.07 % | |
| fe (pp + parse) | -0.13 % | 0.16 % |
| cc (-c; 6 759.5 M) | -0.00 % | 0.48 % |
| emit (709.2 M -> 709.0 M) | -0.02 % | 0.31 % |
| as | -0.00 % | 0.04 % |
| ld | +0.19 % (394.8 M -> 395.6 M) | 0.60 % |

`gate: ok`.  The emitter retires 0.2 M fewer instructions of 709 M (the `ldp`
pairs and the removed large-offset arithmetic); `ld` does not read `struct cc`
(its +0.19 % is measurement noise: no source of `ld/` changed).  The cycle
differences (-0.4 % to -1.3 %) are inside what a load of 20 makes of them and are
not claimed.
