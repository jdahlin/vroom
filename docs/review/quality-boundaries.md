# Quality review: module boundaries and dependency direction, L7 (2026-11-13, main at 2e6d3e73)

## 1. Summary

The modules have narrow function interfaces and wide header interfaces.  Only 257
functions in the tree are global (3957 are static or VPRIV), every one but one has
a prototype in a header, and only five declarations of another file's symbol are
written by hand in a `.c` file.  What is not sharp is what the headers carry and
which way they point:

1. **`cc/ccint.h` is the AST and the parser's whole state in one struct.**  Code
   generation reads 24 of `struct cc`'s 182 fields and calls 11 functions; the other
   158 fields come along into 100 code generation files, the declaration cache
   and (through `ccsnap.h`) the server.  The split is section 4, Q-BND-01.
2. **Three of the four include cycles come from one decision each**: `cc_run`
   calls `emit_tu` (so the front end includes the code generator and the code
   generator writes the driver's timers); the declaration cache is front-end
   code filed under `driver/`; a generated table the driver reads sits in
   `vccd/`.  The fourth (`arch/a64` and `obj`) is two convenience includes.
3. **"Shared" headers are arm64's.**  `emit/emitimpl.h` includes arm64's records
   and inline emitters, 143 of its 231 prototypes are for functions in
   `arch/a64/emit/`, and 21 functions that shared code, x86-64 and wasm call live
   in arm64's files.  `as/asimpl.h` includes `a64.h`; `obj/obj.h` includes both
   CPUs' relocation tables.

A layer table with 11 violations today (section 3.3) and a check for it in
`bench/quality.py` would keep the direction once it is fixed.

## 2. Inventory

Lines | what it is | exported (types / functions / macros+enumerators) and how much
another module uses | header comment.  Counts by `scratchpad/boundaries/hdruse.py`
(identifier of the header seen as a token in a file of another module; names
defined twice in the tree, such as `SHT_*`, count as used).

| Header | Lines | What it actually is | Exports, and what others use | Comment |
|---|---|---|---|---|
| `lex/lex.h` | 113 | token kinds, token, file record, token cache | 2 types, 12 functions, 68 enumerators; all used but `vlex_load_uncached` (fuzz only), `vlex_get_hook` (fuzz). `vlex_file` has a `pp` and a `fixdep` field for two upper modules (`:64-65`) | good |
| `pp/pp.h` | 377 | three interfaces: the preprocessor (22 functions), the diagnostics engine and source locations (45, all defined in `pp/diag*.c`), hooks for the front end's caches (`pp_topev`, `pp_hitreg`, `pp_lazy`) | 14 types, 67 functions; 59 used; never used outside their file: `pp_vdiagx pp_diag_records pp_diag_level pp_warnings pp_src_rest pp_loc_macro pp_macro_defined` | good for pp, none for the diag part |
| `pp/dircache.h` | 11 | directory existence cache | 2 functions | good |
| `cc/cc.h` | 338 | AST record types (`ctype field tag sym node`), their enumerators, `cc_opts`, `cc_run` | 8 types, 5 functions, 181 enumerators (137 used outside) | good; `:273` lists 3 of 5 targets |
| `cc/ccint.h` | 387 | `struct cc` (182 fields), side-table records, builtin ids, constant values, 12 functions | see Q-BND-01 | label; says "cc/ and emit/", also `driver/ccsnap` |
| `emit/emit.h` | 12 | `emit_tu` | 1 function | stale: "arm64 ... (no IR)" |
| `emit/emitimpl.h` | 1050 | the code generators' shared state (`emitter`, `ecx`), arm64's value model and 143 arm64 prototypes, the `em_` recipes, object-item records, type predicates | internal to the emit TU; pulls 26 headers, 4082 lines, into each of 100 files | stale: "arm64 (AAPCS64, Linux ELF) ... No IR, no SSA" (`:5-7`) |
| `emit/walk/walk.h`, `proto.h` | 133, 174 | walker state; 47 `wk_` prototypes and the lowering interface, 76 `LO()` operations (all 76 called by the walker; a 77th, `LO(ecx_free)`, is declared in `emit/walk/stmt.c:48`) | internal; the one boundary in emit/ that is declared as an interface | good; `walk.h:9` "today x64_" (VIR is the second) |
| `emit/rec/rec.h`, `rec/slots.h`, `flow/*.h`, `name.h` | 59, 77, 76, 31 | target-neutral record, op description, slot cells, flow graph, identifier compare | internal; depend on nothing of a CPU | good |
| `as/as.h` | 46 | assembler API on an `ElfObj` | 1 type, 15 functions; `as_free`, `as_stats`, `as_trace` unused outside as/ | stale: "for AArch64" |
| `as/asimpl.h` | 219 | `struct As`, scanner helpers, a private hash (`hsv`), includes `a64.h` (`:7`) | internal | label |
| `obj/obj.h` | 251 | object model + ELF constants + job escape (`vjob_*`) + Mach-O entry points | 7 types, 36 functions, 12 inline, 44 enumerators; unused outside: `elf_free elf_find_section elf_sym_frag_off` | stale: "ELF64 ... for AArch64" (also x86-64, Mach-O) |
| `obj/objimpl.h` | 54 | shared by `obj/`, `arch/*/obj/`, `platform/linux/elf.c` | internal | label |
| `vir/vir.h` | 353 | format, builder, analyses, and the text form's world (units, parser, printer, verifier tables) | 14 types, 40 functions, 221 enumerators; outside vir/ 5 types, 19 functions, 124 enumerators are used (Q-BND-20) | good |
| `vir/alloc.h`, `layout.h`, `synth.h` | 156, 15, 16 | allocator interface; block layout; test target | `synth.h` used only by `viralloc` | good |
| `ld/ld.h` | 28 | four `main`-like entry points | all used | stale: "ELF linker for the arm64 kernel build" |
| `ld/ldelf.h` | 73 | ELF structs and constants for readers | ld only; 61 of 84 enumerators repeat `obj.h` and `arch/a64/reloc.h` (Q-BND-09) | stale: names `ccs/vcc/elf.h`, which does not exist |
| `driver/ppdriver.h` | 42 | one job (`ppd_run`), job thread-locals, phase timers | `ppd_compare`, `ppd_bytes_obj`, `ppd_tok_skip`, `ppd_ph` unused outside driver/ | none |
| `driver/ccsnap.h` | 44 | declaration cache hooks called by the parser | 7 functions, `struct dcst` | label |
| `vccd/{ar,caps,dstore,fixdep,rsh}.h` | 12-43 | vccd's own files | none used outside vccd/; `caps.h` exports `cc_option`, `cc_predefined`, `ld_option`, `as_source_probe` | good (`rsh.h`), label |
| `arch/a64/a64.h` | 160 | two things: the assembler's operand model (`A64Op`, `A64Fix`, `a64_asm`: used only by `arch/a64/as/`) and 24 inline encoders (used only by `arch/a64/emit/obj.c` and `enc.c`) | includes `obj/obj.h` for `ElfVal` (`:17`) | good |
| `arch/a64/reloc.h`, `arch/x64/reloc.h` | 47, 16 | relocation numbers | used by as, emit, ld, obj, platform | label |
| `arch/x64/as/operand.h`, `x64as.h` | 21, 82 | x86-64 operand `XOp`, `as_x64_insn`; encoder internals | `operand.h` is included by `arch/x64/emit/obj.c:4` | good |
| `arch/wasm/as/wasmas.h`, `was.h` | 13, 130 | `wasm_assemble`; the module model shared with `platform/wasm/wasm.c` | | good |
| `ccs/protocol.h` | 71 | shim and server wire format | | stale names (`vroom-cc`, `vroomd`) |
| `lib/*.h` | | include only each other (`arena.h`, `buf.h`, `mapfile.h` -> `attr.h`; `defer.h` -> `mapfile.h`; `tbuf.h` -> `arena.h`; `intern.h` -> `hostcrc.h`; `x86native.h` -> `x86features.h`); no header of `lib/` includes a module | | |

What a newcomer must know (draft of an opening comment for the tree): a module is
a directory plus `arch/<cpu>/<module>/` and, for `obj`, `platform/linux/`; six
modules (`cc pp emit as obj vir`) compile as one translation unit, so `VPRIV` is
`static` there and a shared function has no linker symbol; a public header is
`MOD.h`, the module's own is `MODimpl.h`; `cc/ccint.h` is the exception that is
both.  Data shared by the files of a unity module is `extern` (Q-BND-15).

## 3. The dependency graph (deliverables 1 and 4)

### 3.1 As it is

Module of a file as `bench/quality.py` has it (`arch/<cpu>/<comp>/` belongs to
`<comp>`).  118 include lines cross a module (116 + the two of `ccs/protocol.h`).

| Module | Includes (number of include lines) |
|---|---|
| `lib` | nothing |
| `lex` | lib 3, platform/darwin 1 (`lextest.c:31`, a program) |
| `pp` | lex 1, lib 8 |
| `cc` | pp 1, lib 7, **emit 1**, **driver 1** |
| `vir` | lib 3 |
| `obj` | lib 2, **arch/a64 1**, **arch/x64 1** |
| `arch/a64` (`a64.h`) | **obj 1** |
| `as` | obj 2, lib 3, arch/a64 3 |
| `emit` | cc 2, as 3, obj 1, vir 5, lib 13, arch/a64 1, **driver 1** |
| `platform/linux`, `/darwin`, `/wasm` | obj 1; obj 1, ld 1, lib 1; as 1 |
| `ld` | lib 10, arch/x64 1 |
| `driver` | pp 4, cc 3, as 1, obj 1, lib 8, **vccd 1** |
| `vccd` | driver 2, pp 3, lex 2, ld 1, lib 9, protocol 2 |

Includes of another module's internal header: `emit/emitimpl.h:66`,
`driver/ccsnap.h:4`, `driver/ccsnap.c:32` -> `cc/ccint.h`; `platform/linux/elf.c:14`
-> `obj/objimpl.h` (same module in fact); `arch/x64/emit/obj.c:4` ->
`arch/x64/as/operand.h`; `platform/wasm/wasm.c:12` -> `arch/wasm/as/was.h`;
`emit/tu.c:3` -> `arch/wasm/as/wasmas.h`.

### 3.2 The four mutual pairs

| Pair | Include lines | Calls | Real or accident |
|---|---|---|---|
| cc <-> emit | `cc/ccimpl.h:34` -> `emit/emit.h`; `emit/emit.h:7` -> `cc/cc.h`, `emit/emitimpl.h:66` -> `cc/ccint.h` | cc -> emit: one, `cc/tu.c:334` (`cc_run` calls `emit_tu`).  emit -> cc: 11 functions, `cc_eval` at 50 sites in 14 files | Real in calls, by choice of who sequences: the front end calls the code generator.  Remove by having the driver call it (Q-BND-02) |
| cc <-> driver | `cc/ccimpl.h:35` -> `driver/ccsnap.h`; `driver/ccsnap.h:4`, `ccsnap.c:32` -> `cc/ccint.h`; `driver/ppdriver.c:37` -> `cc/cc.h` | cc -> ccsnap: `dc_begin/end/boundary/materialize/kill/stale` at 7 sites (`cc/tu.c:310,339`, `func.c:998`, `token.c:61-62`, `diag.c:265`, `decl.c:222`).  ccsnap -> cc: `cc_fetch`, `dr_restored`, and 21 arrays of `struct cc` by name (`ccsnap.c:38-39`) | Accident of where a file sits: `ccsnap.c` is front-end code in `driver/` (Q-BND-03) |
| driver <-> vccd | `driver/ppdriver.c:1352` -> `vccd/warnings.inc`; `vccd/vccd.c:83,88` -> `driver/ccsnap.h`, `ppdriver.h` | driver calls nothing in vccd | Accident: a generated table (`vccd/gen-warnings.sh`) with two readers (`vccd/caps.c:92` too) lives with one of them (Q-BND-10) |
| arch/a64 <-> obj | `arch/a64/a64.h:17` -> `obj/obj.h`; `obj/obj.h:54` -> `arch/a64/reloc.h` (and `:55` x64's) | none either way: `obj/obj.c` uses no `R_*` name; `a64.h` needs only the type `ElfVal` | Accident: `obj.h` includes the tables for its callers; `a64.h` holds the assembler's operand type next to the encoders (Q-BND-08) |

### 3.3 The layering the tree should have

A module includes only modules of a lower layer (and `arch/<cpu>/<m>/` is in `<m>`'s
layer and may include `<m>`'s internal header).  Names as in PLAN-tree.md.

| # | Layer | Today's files | May include |
|---|---|---|---|
| 0 | `port` | `lib/{mapfile,vperf,vstack,hostcrc,x86native}.h`, `platform/darwin/pmc` | libc |
| 1 | `common` | the rest of `lib/`; add: `VPRIV`, the job escape, the target enum, `i128` | 0 |
| 2 | `arch` data | `arch/a64/reloc.h`, `arch/x64/reloc.h`, `x86features.h`, the encoder half of `a64.h`, an `elf.h` of format constants | 0-1 |
| 3 | `frontend/lex` | `lex/` | 0-1 |
| 4 | `frontend/diag` + `frontend/pp` | `pp/` (one layer until diag has an interface: section 6, decision 3) | 0-3 |
| 5 | `ast` (language-neutral) | part A of `cc.h` + `ccint.h` (Q-BND-01) | 0-4 |
| 6 | `frontend/c` (later `cxx`, `objc`) | `cc/`, `driver/ccsnap.*` | 0-5 |
| 7 | `vir` | `vir/` | 0-1 |
| 8 | `obj` | `obj/`, `arch/*/obj/`, `platform/{linux/elf.c,darwin/macho.c}` | 0-2 |
| 9 | `as` | `as/`, `arch/*/as/`, `platform/wasm/wasm.c` | 0-2, 8 |
| 10 | `codegen` | `emit/`, `arch/*/emit/` | 0-2, 4 (diag only), 5, 7-9; not 6 |
| 11 | `ld` | `ld/`, `arch/*/ld/`, `platform/darwin/ldmacho.c` | 0-2 |
| 12 | `utils` | `vccd/{ar,rsh,fixdep}.c` | 0-3 |
| 13 | `driver` | `driver/ppdriver.c` | 0-10 |
| 14 | `server` | `vccd/{vccd,dstore,caps}.c` | 0-13 |
| 15 | `bin` | `vpp.c`, `shim.c`, `vasm.c`, `lextest.c`, `virtest.c`, `viralloc.c` | all |

Violations today:

| # | Where | What |
|---|---|---|
| V1 | `cc/ccimpl.h:34` | front end includes codegen |
| V2 | `cc/ccimpl.h:35` | front end includes driver (`ccsnap.h`) |
| V3 | `emit/emitimpl.h:70`, `emit/tu.c:773` | codegen includes driver, and writes its counters (`tu.c:806-807`) |
| V4 | `emit/emitimpl.h:66` | codegen includes the C parser's state (layer 6), not only the AST |
| V5 | `emit/emitimpl.h:118`, `:1048` | shared codegen includes one CPU's `ins.h`, `a64impl.h` |
| V6 | `as/asimpl.h:7` | shared assembler includes one CPU's `a64.h` (the x86-64 assembler gets it) |
| V7 | `arch/a64/a64.h:17` | arch data includes `obj` |
| V8 | `driver/ppdriver.c:1352` | driver includes the server's `warnings.inc` |
| V9 | `lib/intern.c:197` | common names `lex`'s `vlex_stat` (under `VLEX_STATS`) |
| V10 | `cc/cc.h:31`, `cc.h:284`, `ccint.h:377-385` | the AST header includes a CPU's feature table; `cc_opts` holds five `x86_features` |
| V11 | `pp/pp.h:99` | the target enum (`PP_LINUX_AARCH64` ...) is the preprocessor's; cc names it on 48 lines in 15 files, emit through `DARWIN()`, `WASM()`, `X86()`; `vir.h:108` (`VTG_*`) and `obj.h:143` (`EM_*`) have their own |

Not violations but inverted through data or a hook: `lex/lex.h:64-65` (fields for
pp and vccd in `vlex_file`); `pp/pp.h:104` (`pp_builtin_known`, Q-BND-14).

**The check.**  `bench/quality.py` already computes `module_edges`.  Add a table
in the script (or `bench/quality-layers.json`): `layers`: an ordered list of
`{name, dirs}` as above; `allow`: extra pairs (`codegen -> pp` restricted to the
diag header by file name); `internal`: the regexp of internal headers and, per
internal header, the directories that may include it (`emitimpl.h`: `emit/**`,
`arch/*/emit/`); `baseline`: today's violations as `file:line -> header`.  The
check fails for an include line that goes to a higher layer, to another module's
internal header, or for a `mutual_modules` entry, unless it is in the baseline;
the baseline only shrinks.  It needs no build and fits `--check`.  Hand-written
declarations (section 4, Q-BND-11) want a second rule: no `extern` and no
prototype of a non-static function in a `.c` file (4 `extern` lines, one of them
`environ`, and 9 prototypes today).

### 3.4 Passes and data flow (L4)

Not this lens.  One observation that is: the order of calls across modules is
`driver -> cc_run -> (pp_next ...) -> emit_tu -> as/obj`, nested, not sequential:
the driver cannot time or skip code generation, so `emit_tu` adds to
`ppd_ns_emit` and `ppd_ph` itself (V3).

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-BND-01 | L7 | misplaced | `cc/ccint.h:119-290`; `emit/emitimpl.h:66` | A C++ front end has no AST to produce: the AST is 24 fields inside the C parser's state | split into `ast.h` and the C parser's part (lists below) | M | low: moves of declarations | `vcc-same.py`, both builds, `gen-walkprefix.py --check` |
| Q-BND-02 | L7 | misplaced | `cc/tu.c:333-334`, `cc/ccimpl.h:34`, `cc/cc.h:320-322`, `emit/tu.c:773,806-807` | cycle cc<->emit; V3; `cc_opts` carries `ElfObj`, `As`, `vbuf` through the front end | driver calls `cc_parse`, `emit_tu`, `cc_close` | S | low | `vcc-same.py`, `vbench.py gate` |
| Q-BND-03 | L7 | misplaced | `driver/ccsnap.c:1-39`, `cc/token.c:110`, `vccd/vccd.c:83,1759` | cycle cc<->driver; a wrapper; the server compiles with the parser's state for one `dc_stats` call | `cc/declcache.c` in the cc TU; `dc_stats` declared in `cc.h` | S | low | `vcc-sema.py batch`, `vccd-test.py` |
| Q-BND-04 | L7 | mixed file | `emit/emitimpl.h:118,173-408,689-937,1048`; call sites below | x86-64 and wasm cannot be read or built without arm64's emitter; the header is 1050 lines | `codegen.h` (state, refs, queue, recipes) + `arch/a64/emit/a64impl.h`; the helpers move to `emit/` | L | medium: with the walker work | `vcc-same.py`, `vcc-x86.py`, `vcc-wasm.py` |
| Q-BND-05 | L7 | misplaced | `cc/cc.h:272,299-304`; `driver/ccsnap.c:381-387`; `cc/replay.c:1358-1366`; `cc/builtin.c:1525`, `cc/func.c:829` | two cache keys from one record, by different rules; `opt` is read by the parser and is in neither | `cc_opts` as `parse` + `codegen` structs; one key function | S | medium: a possible stale snapshot | a two-TU test at -O0 then -O2 under vccd |
| Q-BND-06 | L7 | mixed file | `pp/pp.h:136-314` | 45 of 67 `pp_` functions are diagnostics and locations; codegen includes the preprocessor to report an error | `diag.h` with its own prefix; handle from `pp *` | M | low | `vcc-diag.py`, `vcc-mutate.py --formats` |
| Q-BND-07 | L7 | misplaced | V10, V11 | three target enums; cc.h drags x86 tables into every file | `common/target.h`: one enum, data-model facts, CPU features behind it | M | low | `vcc-driver.py`, `vcc-same.py` |
| Q-BND-08 | L7 | misplaced | `obj/obj.h:54-55`, `arch/a64/a64.h:17,54-96`, `as/asimpl.h:7` | cycle arch/a64<->obj; V6 | `A64Op`, `A64Fix`, `a64_asm` to `arch/a64/as/`; users include `reloc.h` themselves | S | low | `vas-obj.py`, `vas-enc.py` |
| Q-BND-09 | L1 | duplicate | `ld/ldelf.h:38-71` = `obj/obj.h:39-52` (32 names) + `arch/a64/reloc.h` (29 names), equal values; `ld/ld.c:34` takes x64's from `arch/`, `ld.c:4852-4854` adds three more | a relocation added in one place is missing in the other | one `elf.h` (constants, the five structs); `ldelf.h` and `obj.h`'s enums go | S | low | `vlink-opts.py`, `vas-obj.py` |
| Q-BND-10 | L7, L5 | misplaced | `driver/ppdriver.c:1351-1353`, `vccd/caps.c:92`, `vccd/gen-warnings.sh`; `vccd/caps.h:8-30` | cycle driver<->vccd; ten names in `vccd/` with the prefixes `cc_`, `ld_`, `as_`, `c_` | table to `driver/` (or diag); `caps_` prefix | S | low | `vcc-driver.py`, `vccd-test.py` |
| Q-BND-11 | L7 | pattern | list below | a declaration the compiler cannot check against its definition | declare in the owning header | S | low | builds |
| Q-BND-12 | L2 | duplicate | `VPRIV`: `as/asimpl.h:22`, `cc/ccimpl.h:41`, `emit/emitimpl.h:78`, `obj/objimpl.h:19`, `pp/ppimpl.h:46`, `vir/virimpl.h:19`; `T KIND N`: `cc/ccimpl.h:55-56,66` = `emit/emitimpl.h:89,115-116`; predicates: `cc/type.c:16-33,382` and `emit/emitimpl.h:967-1009` | six copies; eight predicates with one name and two bodies (`is_integer`, `is_ptr`, `is_void`, `is_array`, `is_func`, `is_record`, `is_signed`, `prim`); three one-line wrappers (`cc/builtin.c:450-451`, `cc/token.c:110`) | `VPRIV` in `lib/attr.h`; predicates and accessors inline in `ast.h` | S | low | `vcc-same.py` |
| Q-BND-13 | L7, L9 | duplicate | `obj/obj.h:145-151`, `obj/obj.c:15-23`; `lib/intern.h:154-157`, `lib/intern.c:133-147`; `driver/ppdriver.c:636-637,1335,1770` | two "end this job" mechanisms because `lib/` may not include `obj/`; the driver ties them with a hook | one in `common/` | S | low | `vcc-stress.py --vccd` |
| Q-BND-14 | L8 | global state | `pp/pp.h:104`, `pp/builtin.c:209-214`, `cc/builtin.c:395` | `__has_builtin` is answered by whichever front end's constructor ran last; a C++ front end with other builtins cannot coexist in vccd | a `pp_opts` field | S | low | `vcc-diff.py cmp`, `vcc-builtins.py` |
| Q-BND-15 | L6 | global state | list in section 5 | 22 linker symbols without a module prefix (`basic`, `kwmap`, `hstat`, `no_body`, `tempregs`) in one program | prefix them, or a `VPRIV` for data | S | low | both builds |
| Q-BND-16 | L6 | pattern | section 5 | 131 file-scope macros stay defined for every later file of their unity TU | `#undef` at the end of the file; a check in `quality.py` | S | low | both builds |
| Q-BND-17 | L10 | dead | lists below | 16 exported functions nothing outside their file calls; 44 `VPRIV` functions that are file-local | `static`, or delete | S | low | both builds (`-Wunused-function` then names the dead) |
| Q-BND-18 | L6 | stale doc | `emit/emit.h:4`, `emit/emitimpl.h:5-7`, `as/as.h:4`, `obj/obj.h:4`, `ld/ld.h:2-7`, `ld/ldelf.h:3`, `cc/ccint.h:4-5`, `cc/cc.h:273`, `ccs/protocol.h:1`, `emit/walk/walk.h:9-10` | ten headers describe an arm64-only, IR-less compiler or name files that are gone | rewrite with the module comment of P2 | S | none | review |
| Q-BND-19 | L7 | misplaced | `arch/x64/emit/obj.c:4`, `emit/tu.c:3`, `platform/wasm/wasm.c:12` | codegen and an object writer reach into `arch/*/as/` by relative path (PLAN-tree keeps `"../as/operand.h"`) | publish as `as/x64.h`, `as/wasm.h`; `was.h` internal to as + its writer | S | low | `vcc-x86.py`, `vcc-wasm.py --objcmp` |
| Q-BND-20 | L7, L6 | mixed file | `vir/vir.h:192-240,299-351` | codegen sees the text form: 21 of 40 functions, 9 of 14 types, 97 of 221 enumerators are used only inside `vir/`; `TM_ OK_ EF_ FM_` have no `V` | `vir.h` (format, builder, analyses) and `virtext.h` (units, parser, printer, verifier); `synth.h` out of the public set | S | low | `vir-test.py` |

**Q-BND-01, what code generation takes from the AST headers** (the emit TU:
`emit/**`, `arch/*/emit/`; fields by `c->F`, `->c->F`; record members by the
usual variable names, so approximate):

| Group | Used by emit/ and arch/ | Declared but not used there |
|---|---|---|
| Nodes | `node`, 50 of 51 `N_*`, all 18 `CK_*`, `NF_LVALUE`, `NC_FP_NONNEG`; `c->nodes`, `nnode`, `lists` (153 uses in 24 files) | `N_DESIG`, 7 of 8 `NF_*`, all 4 `NUM_*`; `span`, `paren` |
| Types | `qty`, `QT QI QQ QUNQ`, `ctype`, 29 of 32 `TY_*`, `TF_VARIADIC NOPROTO INCOMPLETE VLA ATOMIC`, `Q_CONST`, `Q_VOLATILE`; `c->ty`, `params`, `t_size`, `t_valist`; `cc_is_signed` | interning (`tyhash`, `fnhash`, `ptrc`, `cplx`), `t_ptrdiff`, `t_wchar`, `t_char16/32`, `TF_ALIGNED TRANSPARENT BLOCK` |
| Records (layout) | `tag`, `field` (`ty off width bitfield align name`), `RF_FLEX`, `RF_COMPLEX`; `c->tags`.  No layout function is called: sizes and offsets are data | 9 of 11 `RF_*`; `field.{invalid,loc,counted_by,tdef,depr}` |
| Symbols | `sym` (`name loc kind sc flags ty align val body attrs next depth`), 4 of 5 `S_*`, all `SC_*`, 15 of 28 `SF_*`; `c->syms` (276 uses in 38 files), `nsym`, `toplevel`, `tlasm`; `attrs` (about 15 of 51 members: section, alias, asmname, cleanup, visibility, constructor ...), `AF_*`; `label`, `c->labels`; `cc_global_named` | `sym.{nref,shadow,ovl,plb,ple,psyms,tdef,defloc}`; scopes (`odir`, `tdir`, `ldir`, `sstack`, `smark`) |
| Constants | `cval`, `CV_*`, `cc_eval` (50 sites, 14 files), `cc_init_subobject`, `cc_bitcast_read`, `cc_object_size`; `c->ipool`, `fpool`, `strs`, `cstr`; `i128`, `u128` | |
| Builtins | 124 of 160 `BI_*`, `CC_LIB_*`, `cc_builtin_libkind`, `cc_builtin_name`, `cc_chk_simplify`, `cc_lib_sig` | `cc_builtin_plain` (no caller outside `cc/builtin.c`) |
| Inline asm | `casm`, `asmop`, `c->asms` | |
| Mangling | nothing: the front end leaves the name in `attrs.asmname` | |
| Options, target | `c->o`: 34 of `cc_opts`' 53 fields, 26 of them read by emit only, 8 by both; `VCC_TLS_*`; `cc_x86_features` | 19 fields read by cc only |
| Diagnostics | `c->p` 9 times on 7 lines (`emit/tu.c:27,45,65,280`, `emit/prepass.c:59,80`, `arch/a64/emit/stmt.c:1025`) for `pp_vdiag`, `pp_diag_begin`, `pp_diag_emit`, `pp_where` | everything else of `pp.h` |

Nothing in emit/ writes an AST array by index (grep for `c->X[..] =`; writes
through a pointer not checked).  Every function takes `cc *`, not `const`, although
worker threads share it: the read-only contract is not in the types (inferred).

Proposed split.  **A, language-neutral (`ast.h`)**: the record types and
enumerators of `cc.h:33-258` whole (flag words keep one numbering; the bits only
sema reads are marked); `cstr`, `label`, `attrs` + `AF_*`, `asmop`, `casm`, the
`BI_*` ids, `cval` + `CV_*`, `CC_LIB_*`; a `struct ast` with the 24 fields (`ty
params tags syms nsym nodes nnode lists ipool fpool strs nstr labels nlabel attr
asms t_size t_valist toplevel ntoplevel tlasm ntlasm`, the options, the diag
handle), embedded first in `struct cc`; `CT CKIND CN` and the type predicates as
inline functions; the constant evaluator's entry points and the builtin tables'
queries as the front end's duties (`cc_eval` and the three beside it,
`cc_builtin_*`, `cc_lib_sig`, `cc_chk_simplify`, `cc_global_named`): a C++ front
end supplies its own.  **B, the C parser (`ccimpl.h`)**: the other 158 fields
(tokens and lookahead, recovery, scopes, type interning, declarator stack,
function and switch state, pragma stacks, `-Wuninitialized` state, deferred
diagnostics, lazy bodies with `lztok`/`lzfn`, the declaration cache and replay
hooks, the heap), `DIR_BITS DIR_PAGE NDIR`, `dr_restored`, `cc_fetch`,
`cc_builtin_plain`, the parse half of `cc_opts`, `cc_run` and the stats calls.
`i128`, `u128`, `likely`, `unlikely` go to `common/`.

**Q-BND-04, target-independent code in arm64's files.**  Call sites from outside
`arch/a64/emit/` (`name(` not preceded by `.`/`->`, files with a definition of
their own skipped): shared `emit/*.c` 13 functions at 38 sites (`is_local_var`
10, `out_chunk` 10, `sym_header` 4, `note_ref` 3, `direct_callee`, `ld_of_d`,
`claim_ref`, `side_effects`, `strip_casts`, `walk_fn`, `emit_function`,
`a64_replay`, `darwin_text`); `emit/walk/` 7 at 43 (`side_effects` 10, `note_ref` 9,
`is_local_var` 7, `ptr_to_vla` 7, `walk_fn` 4, `ael_slot` 3, `cond_fold` 3);
`arch/wasm/emit/` 5 at 15; `arch/x64/emit/` 9 at 23 (`sym_needs_got` 7,
`dbg_remap` 5, `dbg_where`, `ctor_entry` ...); `emit/opt/` 1 at 10.  `walk.h:17-19`
says so ("are arm64's files' for now").  `ael_slot` and `cond_fold` use arm64's
own state (`ecx.ael`, `kval`): whether the shared walker should call them is for
the walker reviews.  `emit/vir/a64.c` is arm64 code
under `emit/vir/` while x86-64's is `arch/x64/emit/vir.c`: the same job filed in
two places; the shared builder `emit/vir/build.c` calls arm64's `abi_align`
(`build.c:1364`) and `note_ref`.  Distinct functions called from outside arm64's
directory (without `emit/vir/a64.c`): 21; three are the dispatch into the target
(`emit_function`, `a64_replay`, `darwin_text`), the other 18 are helpers.

**Q-BND-05.**  `ccsnap.c:384` hashes the bytes of `cc_opts` before `outfd`
(padding included); that range holds seven fields only emit reads (`vcc_kernel`,
`lse`, `no_fp_contract`, `no_jump_tables`, `wasm_sjlj`, `omit_frame_pointer`,
`sibling_calls`), which split the cache for nothing.  `replay.c:1358` lists 16
fields by hand (and the x86 feature sets) and has neither `strict_std` nor
`conv_warn`, both read by cc.
`opt` lies after `outfd` and is in neither key, yet `narrow_math_call` and
`fold_local_builtins` rewrite nodes when it is set.  Whether a snapshot can hold
such a node (bodies in headers are mostly lazy tokens) I did not verify.

**Q-BND-11, declarations by hand** (method: every `extern` line and every
file-scope prototype in a `.c` file whose definition is in another file,
`scratchpad/boundaries/ana.py hand`, plus `grep '^\s*extern'`):
`emit/tu.c:773` `ppd_ns_emit` (also in `ppdriver.h:25`, which the file includes);
`driver/ppdriver.c:1914` `pp_stat_try`, `pp_stat_skip` (`pp/include.c`) and
`vdir_probes` (`pp/dircache.c`), in a block; `lib/intern.c:197` `vlex_stat`
(`lex/lex.c:999`); `ld/ld.c:6689` `wasm_ld_main` (`ld/wasmld.c:2058`, no header
at all); `arch/a64/emit/value.c:113` `sym_needs_got` (also `emitimpl.h:812`);
`cc_eval` twice (`cc/ccimpl.h:437`, `cc/ccint.h:358`).  Inside the emit TU eight
VPRIV prototypes stand in `.c` files; three repeat the header
(`emit/walk/expr.c:162-163` = `proto.h:56-57`; `value.c:113`), three are x86-64's
own (`arch/x64/emit/abi.c:222,320,321`, not in `x64impl.h`), and
`emit/walk/stmt.c:48` declares the lowering operation `proto.h` lacks.

**Q-BND-17, deliverable 2: functions per module** (`scan.py`: file-scope
definitions with their storage class; `ana.py`: a name's users are the files
where it is a token outside a prototype).  Global = neither `static` nor `VPRIV`.

| Module | Global | Used by another module | Only own module, other TU | Only own TU or file | VPRIV | VPRIV used in own file only |
|---|---|---|---|---|---|---|
| lex | 12 | 10 | 2 | 0 | | |
| lib | 15 | 13 | 0 | 2 (through inline) | | |
| pp | 69 | 60 | 1 | 8 | 100 | 3 |
| cc | 18 | 17 | 0 | 1 | 285 | 8 |
| emit | 1 | 1 | 0 | 0 | 545 | 27 |
| vir | 49 | 25 | 15 | 9 | 7 | 2 |
| as | 26 | 20 | 3 | 3 | 50 | 4 |
| obj + platform/linux | 34 | 27 | 0 | 7 (4 through inline) | 5 | 0 |
| ld | 4 | 3 | 1 | 0 | | |
| driver | 8 | 6 | 1 | 1 | | |
| vccd | 13 | 0 | 13 | 0 | | |
| platform/darwin, /wasm | 8 | 8 | 0 | 0 | | |

Global and unused outside their file: `pp_vdiagx`, `pp_diag_records`,
`pp_diag_level`, `pp_warnings`, `pp_src_rest`, `pp_loc_macro`,
`pp_macro_defined` (`pp/diag.c:1829,1570,1358,1846,505,1498,1500`); `elf_free`,
`elf_find_section`, `elf_sym_frag_off` (`obj/obj.c:50,221,425`); `as_free`
(`as/as.c:2537`); `a64_cond` (`arch/a64/as/enc.c:89`); `was_sig_intern`
(`arch/wasm/as/as.c:145`); `cc_builtin_plain` (`cc/builtin.c:289`);
`vir_print_group`, `vir_print_sig` (`vir/print.c:101,117`).
None is named in `ccs/fuzz`, `ccs/wasm` or `bench/` (grep).  VPRIV and file-local,
the notable ones: `at_swp`, `at_ldop`, `ld_cast`, `run_cleanups`, `dbg_row`
(arm64), `wcompare`, `wto_ptr` (wasm), `em_sdivconst32/64`, `em_udivconst32/64`,
`em_slot_access`, `em_slot_detail`, `vw_emit`, `inl_cand`; `cast_expr`,
`cond_expr` (`cc/exprop.c:481,1014`); `vir_term_of`, `vir_succ_csr`.
`em_flow_graph_free` and `em_flow_live_free` have no caller and carry
`__attribute__((unused))` to stay quiet (`emit/flow/flow.h:42-43`).

## 5. Names (L5, L6; deliverable 5)

**Prefix per module, exported functions** (257 global definitions, `main` aside):

| Module | Prefix found | Without it |
|---|---|---|
| lex | `vlex_` 11, `vtok_` 1 | 0 |
| lib | `vintern_` 9, `vstack_` 1 | `vjob_escape`; `ld_xxh3`, `ld_blake3`, `ld_sha256` in `lib/ldhash.c` (the linker's prefix in the library; the driver calls `ld_xxh3` at `ppdriver.c:201`) |
| pp | `pp_` 67, `vdir_` 2 | 0 |
| cc | `cc_` 17 | `dr_restored`; in `driver/ccsnap.c`: `dc_` 6 |
| emit | `emit_tu` | 0 global; inside the TU three rules (`em_`, `wk_`/`vw_`, `w...`) and none for arm64 (`gen`, `load`, `store`, `slot`, `treg`, `jump`, `cast`, `arith`, `classify`) |
| vir | `vir_` 49 | 0 |
| as | `as_` 16, `a64_` 5, `was_`/`wasm_` 5 | 0 |
| obj | `elf_` 33, `macho_` 2 | `vjob_fail`; the module is `obj` and its prefix and types say `elf` (`ElfObj`) though Mach-O is written from it |
| ld | `ld_main`, `ld_macho` | `objcopy_main`, `nm_main`, `wasm_ld_main` |
| driver | `ppd_` 2 | |
| vccd | `dstore_` 4, `rsh_run`, `ar_run`, `vfixdep` | `cc_option`, `cc_predefined`, `ld_option`, `ld_rejects`, `as_source_probe`, `c_source_probe` |

So 8 exported functions carry another module's prefix (3 `ld_` in lib; `cc_` 2,
`ld_` 2, `as_` 1 in vccd) and 7 carry none (`vjob_escape`, `vjob_fail`,
`dr_restored`, `objcopy_main`, `nm_main`, `wasm_ld_main`, `c_source_probe`).
The larger gap is data: variables shared by the files of a unity module
are plain `extern`, because `VPRIV` exists for functions only.  Linker symbols
with no module prefix (Q-BND-15): cc `basic` (`type.c:4`), `kwclass`, `kwmap`,
`kwmap_iso` (`token.c`), `w_all`, `w_conv`, `w_selfassign`, `w_gnu`, `w_ped`
(`diag.c`), `tl_fscr` (`record.c`); pp `hstat`, `hc_mem_max`, `shared_used`,
`idch_tab`, `no_body`, `tk_spare`, `sb_spare`, `rec_spare`, `out_spare`,
`out_spare_cap`; emit `tempregs` (`tu.c`), `cond_name` (`arch/a64/emit/insn.c`).

**What the unity build hides or causes.**
- Hides: a name may mean two things in two modules without any tool objecting.
  92 static or VPRIV function names are defined in more than one module (`now`
  in 6, `reserve` and `put` in 4, `err`, `lookup`, `classify`, `binop`, `apply`
  in 3; `x64_reg`, `x64_imm`, `x64_cc` in both `arch/x64/as/` and
  `arch/x64/emit/`).  They collide the day a file changes module (Q-BND-03 moves
  `ccsnap.c` into cc: its `opt_sig` meets `cc/replay.c:1358`'s).
- Hides: in the file-by-file build every VPRIV function is a linker symbol; no
  VPRIV or global name is defined in two modules today (checked), but nothing
  keeps it so for names like `gen`, `load`, `slot`, `fold`, `err`.
- Hides: an unused VPRIV function is silent in the file-by-file build and needs
  `__attribute__((unused))` in the unity build.
- Causes: every static name must be unique across the up to 100 files of a TU;
  no two files of one unity TU define the same static function or variable
  today (checked), which is why wasm prefixes everything with `w`.
- Causes: a macro defined in one file is defined in every later file of the TU
  (`cc_SRC` and `emit_SRC` are in wildcard order).  131 of 178 file-scope
  `#define`s in unity `.c` files are never undefined: emit 71, cc 30, pp 17, vir
  7, obj 5, as 1.  Short ones: `M`, `K` (`cc/replay.c:3445-3446`), `VEC`, `FD`,
  `ADD` (`cc/replay.c:38,90,540`), `R` (`vir/table.c:7`), `RB`, `XU`, `XS`
  (`arch/a64/emit/opt.c:25,612,614`), `X_D`..`X_R` (`arch/x64/emit/effects.c:12-17`),
  `R16` (`pp/hcentry.c:640`).  Each file still compiles alone, so only the unity
  build can be changed by one.

**Prefixes with two meanings in headers that meet in one file:**

| Name | Where | Meanings |
|---|---|---|
| `TF_` | `lex/lex.h:43-46`, `cc/cc.h:57-64` | token flags and type flags, both starting at 1; `cc.h` includes `lex.h` through `pp.h`, so a wrong one compiles |
| `R_` | `arch/a64/reloc.h`, `arch/a64/emit/ins.h:8` (`R_FP`, `R_LR`), `arch/wasm/as/was.h:35` | relocations, arm64 registers, wasm relocations |
| `PF_` | `pp/pp.h:28-31`, `ld/ldelf.h:55` | painted-token flags, ELF segment flags |
| `A_` | `emit/emitimpl.h:497`, `driver/ccsnap.c:38` | ABI argument kinds, snapshot arrays |
| `C_` | `as/asimpl.h:146`, `arch/a64/emit/ins.h:68` | character classes, condition codes |
| `Val`, `val`, `cval`, `ElfVal`, `Var` | `as/asimpl.h:75,77`, `emit/emitimpl.h:227`, `cc/ccint.h:341`, `obj/obj.h:115` | five value types; `Val` is a typedef of `ElfVal` |
| `sym`, `Sym`, `ElfSym`, `wsym`, `vir_usym` | `cc/cc.h:155`, `ld/ldelf.h:28`, `obj/obj.h:94`, `was.h:56`, `vir.h:308` | |
| `N`, `T`, `KIND`, `X`, `F` | `emitimpl.h:89,115,116,1037`, `x64as.h:41` | one-letter macros and functions in headers; `N(i)` needs a variable named `c` |
| `OK_`, `EF_`, `FM_`, `TM_` | `vir/vir.h:192-231` | public names of `vir` without its prefix |
| type case | `ElfObj`, `As`, `A64Op`, `XOp`, `Sv`, `Macro`, `Ehdr` against `ptok`, `ctype`, `vir_fn` | as, obj and ld use CamelCase types, the rest lower case |

## 6. Decisions needed

1. **Where the constant evaluator sits.**  `cc_eval` is C's rules on the AST.
   (a) Part of the front end's duties behind a small interface in `ast.h`
   (recommended: C++ needs another evaluator, and codegen only asks "is this
   node a constant"); (b) language-neutral in the `ast` layer, extended for C++.
2. **Who calls code generation**: the driver (recommended; removes V1 and V3 and
   lets `cc_opts` split) or the front end as today.
3. **Diagnostics below or beside the preprocessor.**  The engine needs pp's
   location table and pp reports through the engine.  (a) One layer, two public
   headers (`diag.h`, `pp.h`) and a `diag *` that front ends and codegen hold
   (recommended first step); (b) a source-map interface that pp fills, with diag
   below pp.  The diagnostics review should say which.
4. **`obj` or `elf`.**  Rename `elf_`/`ElfObj` to `obj_`/`Obj` in the rename
   window (33 functions, used by as, emit, driver, both writers), or declare
   `elf_` the prefix of `obj/`.  Recommended: rename; the ELF writer keeps `elf_`.
5. **The layer check as a ratchet in premerge**, with today's 11 violations as
   the baseline (recommended), or only a report until P3 orders the fixes.
6. **Data shared inside a unity module**: prefix (`cc_basic`) or a storage-class
   macro beside `VPRIV`.  Recommended: prefix; data cannot be `static` in the
   file-by-file build either way.
7. **`a64.h`**: move the assembler's operand model to `arch/a64/as/`
   (recommended, Q-BND-08) or move `ElfVal` (a symbol expression) down to
   `common/`.

## 7. Not covered

- Read in full: every header of section 2, `emit/emitimpl.h` included.  Not read
  in full: `cc/ccimpl.h` (776 lines), `pp/ppimpl.h` (1125), `arch/wasm/emit/wimpl.h`
  (462), `arch/x64/emit/x64impl.h`, `lib/` header bodies (include lists only, as
  asked), and no `.c` file beyond the lines cited.
- Counts come from two scripts written for this review
  (`scratchpad/boundaries/scan.py`, `ana.py`, `hdruse.py`, `ccuse.py`): a
  brace-depth scanner, not a C parser.  Uses are token matches, so a short name
  (`field`, `label`, `load`) over-counts and a use made only through a macro of
  another header under-counts; the call-site counts of Q-BND-04 use `name(`.
  `struct cc` field use is by `c->F`, `->c->F` and was cross-checked for other
  spellings (none found).
- Q-BND-05's stale-snapshot risk is not reproduced.  Whether `cc_eval` and the
  other entry points are free of writes when emit's threads call them is
  inferred from the design, not read.
- `ld/` beyond its headers and include lines; the reorganised tree was read for
  its include directory and relative includes only (PR #134).
- Users outside `ccs/vcc` were checked by grep in `ccs/fuzz`, `ccs/wasm`,
  `ccs/phase1`, `bench/` only for the functions listed as unused.
