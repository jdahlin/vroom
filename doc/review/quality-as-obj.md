# Quality review: assemblers, object model, object writers (2026-11-13, main at 2e6d3e73)

## 1. Summary

The area works as three layers (text to object model, model to layout, layout
to file) but only the middle one, `obj/`, is cleanly one job per file.  What
matters most:

1. `as/as.c` holds twelve jobs in 4081 lines, a text-to-text Mach-O dialect
   translator among them that uses nothing of the assembler.  It also holds
   the target switch: `machine == EM_X86_64` is tested 20 times in it.
2. The same thing is written per format and per CPU instead of once: fixup
   resolution three times (arm64, x86-64, Mach-O), byte and LEB128 helpers in
   every writer, the ELF/Mach-O section mapping in three files, and a second
   complete assembler front end for wasm (lexer, expressions, strings, symbol
   directives, its own symbol table and its own out-of-memory policy: abort).
3. Instruction lookup has no single form: arm64 uses six mechanisms in one
   file, x86-64 a chain of 14 functions that compare the mnemonic with
   `strcmp` against local tables, each opening with the same dead boilerplate.

Private variants of `lib/` facilities are everywhere here: 12 copies of one
grow-an-array block, 9 hash tables, 5 FNV hashes, 9 LEB128 functions, `put`
twice and its loop by hand in six more places.  Everything below was read in the files named; costs marked
"inferred" were not measured (no builds allowed).

## 2. Inventory

| File | Lines | What it does | Better name | Header |
|---|---|---|---|---|
| `as/as.c` | 4081 | 12 jobs, see Q-asobj-01 | split | stale: "for AArch64", "Instructions go to a64_asm()" |
| `include/as/as.h` | 46 | public API: lifecycle, `as_run`, the snippet API the emitter uses, `as_darwin_text` | | stale: "for AArch64"; line 16 documents a `dir` parameter `as_run` does not have |
| `as/asimpl.h` | 219 | `struct As`, limits, character classes, `hsv`, the `.req` hash, VPRIV prototypes | | label only |
| `bin/vasm/vasm.c` | 125 | driver: ELF, wasm, Mach-O, `--bench` | | good |
| `arch/a64/as/parse.c` | 561 | arm64 registers and `.req` lookup, operands, literal pools, the instruction statement | | label only |
| `arch/a64/as/enc.c` | 3022 | sysreg/condition/bitmask lookups, mnemonic table, 75 class encoders, atomics, AdvSIMD table, dispatch | | stale: calls itself `a64.c`, "sorted mnemonic table" (it is hashed at run time) |
| `arch/a64/as/sve.inc` | 740 | SVE encoders; `k_sve` is one 455-line if-chain (278-732) | `sve.c`, a real file | good |
| `include/arch/a64/a64.h` | 160 | `A64Op`, `a64_asm`, inline encoders (used by `arch/a64/codegen/obj.c` only) | | good |
| `include/arch/a64/reloc.h` | 47 | `R_AARCH64_*`, internal `R_VCC_*` | | label only |
| `arch/x64/as/asm.c` | 759 | operand parser (9-271), encoder `x64_emit` (273-458), shared forms (460-617), statement and dispatch (619-759) | `parse.c` + `enc.c` | good |
| `arch/x64/as/x64as.h`, `operand.h` | 82, 21 | `XForm`/`XEnc`, prototypes; `XOp` and `as_x64_insn` for the emitter | | good; label |
| `arch/x64/as/sys.c` | 241 | segment/control-register moves, the no-operand table (x87 and string instructions too), system instructions | | label only |
| `arch/x64/as/gpr.c` | 403 | jumps, setcc/cmov, shld/bt/prefetch, integer instructions, movz/movs | | label only |
| `arch/x64/as/x87.c` | 114 | x87 with memory and `%st` operands | | label only |
| `arch/x64/as/sse.c` | 365 | SSE to SSE4.2, AES, SHA; also crc32, FS/GS base, emms, fnsave/frstor | | label, narrower than the content |
| `arch/x64/as/avx.c` | 181 | the VEX table, special VEX forms, rorx | | label only |
| `arch/wasm/as/as.c` | 1255 | the whole wasm assembler: symbols, lexer, expressions, instruction table and encoder, data, directives, driver | `wasmas.c` (the tree has two `as.c`) | good |
| `arch/wasm/as/was.h`, `wasmas.h` | 130, 13 | the model `wmod`; `wasm_assemble` | | good; label |
| `include/obj/obj.h` | 251 | the model and its API, ELF constants, `vjob_fail`, the Mach-O entry points | | stale: "ELF64 ... for AArch64" |
| `obj/obj.c` | 489 | model construction, deferred expressions, layout; defines `vjob_fail` | | good |
| `obj/objimpl.h` | 54 | internal helpers (`rpush`, `symoff`, `insec`, `put`) | | label only |
| `arch/a64/obj/fixup.c` | 127 | arm64 field patcher and resolver; also the dispatch to x86-64 (27, 84) | | good |
| `arch/x64/obj/fixup.c` | 104 | x86-64 patcher and resolver | | good |
| `platform/linux/elf.c` | 388 | relocation records, string table, `elf_finish`, `elf_emit`, file write | | stale: "See elf.h" |
| `platform/darwin/macho.c` | 586 | arm64 Mach-O from the same model (no `macho.h` exists) | | good, but "(elf.h)" |
| `platform/wasm/wasm.c` | 666 | wasm object from `wmod`; `wasm_write` is 416 lines (250-666) | | good |

**What the module is for (draft opening comment).**  The assembler turns
GNU-as text (arm64, or x86-64 in AT&T syntax) into the in-memory object, one
statement at a time; the code generator feeds the same assembler directives
and inline asm between the machine code it writes straight into the object.
The object is laid out and written as ELF or, for Darwin, Mach-O.  Wasm has
its own assembler and model.  Invariants a newcomer must know:

- One `As` and one object per thread; nothing is shared or locked.  Memory is
  a few large reservations reset per TU; no `malloc` on the hot path.
- The reference is LLVM MC, byte for byte: symbols enter the symbol table in
  the order MC registers them (`elf_reg`), a fixup resolves in place exactly
  when MC's would, relaxation follows MC's passes.
- Offsets are frag-relative until layout.  `frag->base` is valid only after
  `elf_layout` (or `as.c`'s `frag_bases`, once relaxation is done).
- A frag is a (section, subsection) piece, or a continuation made by
  `elf_split`; frag id == section id for subsection 0, other ids count down
  from `FRAG_CAP`.
- A fixup's type is the CPU's ELF relocation number or `R_VCC_*`; which CPU
  is `o->machine`.  Section names are ELF's even when the output is Mach-O.
- The text of every snippet must stay valid until `as_end`: macro bodies and
  names point into it.
- Errors: the assembler collects up to 20 messages and goes on; the object
  keeps the first; a full reservation leaves through `vjob_fail` (longjmp).

## 3. Passes and data flow (L4)

**Text to model (`as_snippet`, as.c 2580).**  One pass.  `next_stmt` finds
the statement (NEON scan; x86-64 always takes the copying scanner, 581);
`statement` (2457) tries, in order: conditionals, labels, `=`, `.req`, macro,
directive (a `switch` on the first letter with `memcmp` chains, 2173-2455),
instruction.  Macro and loop bodies are scanned once to find their end
(`collect_body`), then every expansion is scanned again as text (as MC).
For Darwin `.s`, `as_darwin_text` first rewrites the whole text into a
`malloc`ed copy (3979-4081): an extra pass over every byte.

**End of text (`as_end`, 3645).**  Literal pools; numeric labels (walk of the
table); `.set` variables (walk); `var_finish` (up to 64 relaxation passes,
each over all frags and all items); `cfi_finish`; `dwline_finish`;
`symver_finish` (walks every fixup once per `.symver`/`.weakref`, 3606, 3635).

**Model to file.**  `elf_finish`: `elf_layout` (frags, every symbol for
`.size`, deferred expressions), fixups in layout order, then eight sweeps over
sections and two over symbols before `elf_emit` copies chunks.
`macho_finish`: `elf_layout`, section map, layout, one walk of all fixups to
count (398), one to resolve (408), three passes over all symbols (418), emit.
`wasm_write`: layout and emission in one function; relocations are walked
five times (262-280, 360-366, 367-386, 403-413, `reloc_section`).

**The same fact computed again.**  A frag's section offset is summed from the
frag chain in six places: `frag_bases` (as.c 2787, called at 2811 and 3490),
`frag_base` (3022), `d_reloc` (894, 898), `dwline_finish` (3531), `var_finish`
(3099, 3113), and `elf_layout` (obj.c 438), which then does it once more for
the writer.  Whether a symbol is a deferred expression is a linear scan of a
list (`elf_expr_get`, obj.c 167) made for every `+`/`-` with a symbol
(`is_xsym`, as.c 147, 358).  Finding a section by name is a linear scan
(obj.c 221); group membership is quadratic in sections (elf.c 141, 246, 347).
`dw_str` scans the whole `.debug_line_str` per string (as.c 3418).

**Files with several passes:** `as/as.c` (all of the above), `wasm.c`
(`wasm_write`).  **A pass spread over files:** fixup resolution: the walk in
`elf.c` 115-119 and `macho.c` 407-411, the rules in two `fixup.c` and
`macho.c` 228-309, the record in `elf.c` 34-59.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-asobj-01 | L5, L4 | mixed file | `as/as.c`: 12 jobs, table below; 9 forward-declaration sites (653-659, 810, 1087, 1174-1176, 1430, 2171-2172) | nobody finds CFI or relaxation; every as change touches one 4 k-line file | 11 files by job in `as/`, one in `platform/darwin/` | M (moves) | low | `vcc-same.py`, `vas-obj.py`, `vas-corpus.py`, unity and file-by-file builds |
| Q-asobj-02 | L1 | near-duplicate | `arch/x64/obj/fixup.c` 59-104 = `arch/a64/obj/fixup.c` 88-126 (scan: 60-69 = 88-96, 70-83 = 95-108, 83-94 = 107-118, 98-104 = 121-126); third copy of the opening in `macho.c` 228-256 | three copies of MC's resolve rule to keep in step; a fourth with the next CPU | `obj/resolve.c` with a per-CPU hook table, see below | S | medium | `vas-obj.py`, `VCC_LDCMP=1 vcc-ctests.py`, `vcc-x86.py`, `vcc-darwin-s.py` |
| Q-asobj-03 | L2 | pattern | byte and LEB128 helpers per file, list below | 9 LEB functions, 2 `put` and 6 hand-written copies of its loop in this area alone; `pad_uleb` = `pad_sleb` | `lib/bytes.h`, `lib/leb.h` | S | low | `vcc-same.py`, `vcc-wasm.py --objcmp`, `vas-obj.py` |
| Q-asobj-04 | L1, L7 | duplicate | `arch/wasm/as/as.c` is a second front end, see below | two lexers, two expression grammars, two string decoders that already differ; wasm `.s` has no macros or conditionals | shared scanner, expressions, strings; decision D1 | M | medium | `vcc-wasm.py --objcmp`, `vcc-abi.py --target wasm64` |
| Q-asobj-05 | L1, L4 | pattern | x86-64 families: boilerplate 14 times, dispatch by `strcmp` chain (asm.c 734-757), see below | dead text in every family; an integer `mov` costs dozens of `strcmp` (inferred), and the x86 emitter encodes through the same chain (`arch/x64/codegen/obj.c:78`) | one mnemonic table, hashed, as arm64's `mns[]` | M | medium | `vas-obj.py` (`x86-*.s`), `vcc-x86.py`, `vbench.py gate` |
| Q-asobj-06 | L7 | misplaced | CPU choice by `machine == EM_X86_64`: 20 tests in `as.c` (255, 531, 536, 581, 770, 804, 931, 1437, 2178, 2200, 2417, 2519, 2642, 2720, 2806, 3132, 3228, 3337, 3541, 3548), 3 in `obj.c` (368, 400, 408), `elf.c:44`; x86 jump bytes in `as.c` 3151-3173, 3196-3213; x86 state in `struct As` (asimpl.h 124-126); `asimpl.h:7` includes `a64.h` for all | a third CPU edits 25 places in generic files; arm64's file dispatches to x86 (`a64/obj/fixup.c` 27, 84) | a per-CPU table for the assembler and the object (comment character, instruction, CFI registers, data relocation types, nop fill, branch relaxation); decision D3 | M | medium | `vcc-same.py`, `vas-obj.py`, gate |
| Q-asobj-07 | L2 | pattern | grow-an-array block 12 times; 9 private hash tables; list below | TODO.md:1677-1680 lists two of them; `include/common/vec.h` (`VVEC_PUSH` over a caller's allocator) fits and is unused here | `include/common/vec.h`; one `lib/` table keyed by `uint32_t`, one by string | S-M | low | `vcc-same.py`, gate |
| Q-asobj-08 | L1, L7 | duplicate | the ELF/Mach-O mapping in three files: `as.c` `dt_section` 3749-3761 and `dt_operands` 3797-3800; `macho.c` `mtab` 47-57 and `sec_map` 170-195; `arch/a64/codegen/darwin.c` (its header, 1-14, says it is the inverse); `as.c` 3737-3743 = `darwin.c` 19-27 | a new section kind is added in three tables in three modules; Darwin `.s` is rewritten to ELF dialect, parsed, then mapped back by name | one table in `platform/darwin/`; decision D5 | M | medium | `vcc-darwin-s.py` both ways, `vcc-abi.py --host`, `bench/pg` |
| Q-asobj-09 | L4 | redundant walk | frag offsets summed in six places; `elf_expr_get` and `elf_find_section` linear; group scans quadratic (section 3) | inferred: quadratic time on TUs with many sections or deferred expressions (the kernel's ALTERNATIVE) | layout once in `obj/` and cached; an expression flag on the symbol; section lookup through the symbol hash | S-M | low | gate; a `-ffunction-sections` TU timed before and after (missing today) |
| Q-asobj-10 | L1, L7 | duplicate | `macho.c` re-defines what `objimpl.h` has because it is outside `obj_SRC` (Makefile:83, 100): `put` 226 = objimpl.h:40, `defined` 197 = `insec` :39, `zalloc` 79 = `rpush`, `al` 320 = `elf.c:102`, `pfx` 90 = `as.c` `hasprefix` 603; section copy `macho.c` 548-554 = `elf.c` 288-294; fixup walk 407-410 = `elf.c` 115-118 | the second writer copies instead of sharing | `macho.c` joins the object module; the two loops become `obj_copy_section`, `obj_each_fixup` | S | low | `vcc-darwin-s.py`, `vcc-same.py` |
| Q-asobj-11 | L1 | duplicate | operators: `as.c` `apply` 395-417 = `obj.c` `elf_expr_op` 178-203; arm64 nop fill `obj.c` 368-372 = `as.c` 3228-3232; `elf_insn` (obj.h 192-197, one user) = `parse.c` 553-556; `flush_pools` 354-357 = `ltorg` 371-374; `d_space` 1208-1223 = `d_fill` 1236-1256; `fits8` (asm.c 281) = `fits_i8` (as.c 3042); `parse_regnum` (parse.c 26) = `cinum` (enc.c 31); `@MOD` names asm.c 133-136 and as.c 936-939; hex digit value five times (as.c 178, 821, 971, 3245, 3293) | a GNU operator or a relocation modifier is added twice | call the existing function; `c_xval()` in `asimpl.h` | S | low | `vas-obj.py`, `vcc-same.py` |
| Q-asobj-12 | L4 | long function | `enc.c`: six lookup mechanisms (hash 644, hash 60, binary search 2714, name parsing 2106 and 2547, `sve_insn`), four instruction families inline in `a64_asm` (2884-2933), `K_` enum 180-189 plus a 75-case `switch` 2938-3018; `sve.inc` `k_sve` 455 lines; `wasm.c` `wasm_write` 416 lines | where an arm64 mnemonic is encoded cannot be told from its name; layout and emission of wasm are not separable | `Mn` carries the class function; the inline families become classes; `wasm_layout` + `wasm_emit` as ELF and Mach-O have | M | medium | `vas-enc.py`, `vas-corpus.py`, `vcc-wasm.py --objcmp` |
| Q-asobj-13 | L9 | error policy | four policies: `err` (as.c 28, 20 messages), `elf_error` (obj.c 27, first wins), `vjob_fail` (defined obj.c 15-25, declared obj.h 145-151), and `abort()` in the wasm assembler (as.c 39-41, 96-97, 404-405, 563-564; wasm.c 285-286); unchecked `malloc`/`realloc` (wasm as.c 123, 788, 808; wasm.c 380; as.c `db_put` 3730) | inside vccd an abort ends the server; TODO.md:1681 asks for "fail the request, never abort" | `vjob_fail` moves to `lib/`; wasm allocations fail through it | S | low | `vccd-test.py`, `vcc-stress.py --vccd` |
| Q-asobj-14 | L8 | global state | `as_trace` (parse.c 380: defined in the arm64 file, declared in as.h 44, read per instruction); hidden parameters as thread-locals: `as_dtype` (as.c 778), `var_prev` (2991), `sort_o` (macho.c 311), `rb` (asm.c 666); tables built at run time: `srh`/`mnh` under `pthread_once` (enc.c 47-58, 629-642, the same code twice), `insn_find` with atomics and an admitted race (wasm as.c 396-415); `static sink` shared by threads (obj.c 317) | state that is not in `As`; a first-use cost and a lock per process for tables that are constant | parameters or `As` fields; tables generated like `a64sysreg.inc`; decision D7 | S | low | TSan `vcc-cbatch.py`, gate |
| Q-asobj-15 | L7 | misplaced | ELF constants in `include/obj/obj.h` 39-52 and again in `ld/ldelf.h` 38-52; raw numbers beside them: `17` (elf.c 353, `SHT_GROUP` exists), `0x6fff4c03` (365), `0xfff1`/`0xfff2` (300-301), `0x70000001` (as.c 2807), `0x200000` (as.c 688), `10` for STB_GNU_UNIQUE (as.c 1397, elf.c 320), `type >= 523 && type <= 573` (elf.c 41); Mach-O: enum in `ldmacho.c` 44-55, partial enum in `macho.c` 30-43, raw hex 513-542 | see below | `platform/linux/elf.h`, `platform/darwin/macho.h`; decision D4 | S | low | compiles; `vcc-same.py` |
| Q-asobj-16 | L10 | dead | as.c: 1366 `&& 0`, 1919-1920 one error in both arms, 1936-1939 a test repeated, 2055-2056 empty `if`, 2771 `#undef CFIS` (the macro is `CIS`), 120 and 1718 unused parameters; asimpl.h 95 `pool_dummy`; x64: gpr.c 401-402 (the last family returns the error, so asm.c 758 is unreachable), gpr.c 303 both arms `&o[1]`, sse.c 44 `&& 0`, x64as.h 21 `x87 /* (unused) */`, x87.c 91 `fnstenv`/`fldenv` shadowed by sse.c 220, sse.c 151 `pcmpistri` shadowed by sse.c 119; obj.c 240/253 `save`; elf.c 32 `out_of`, 92 `prev = 0`; wasm.c 568/645 `sub`; wasm as.c 606/617 `p` | misleads readers | delete | S | low | `vcc-same.py`; `-Wunused` |
| Q-asobj-17 | L6 | stale doc | obj.h 4, as.h 4, as.c 2: "for AArch64" (x86-64 since `elf_set_machine`); as.c 8 `a64_asm()`; as.h 16 `dir`; enc.c 2-4; elf.c 3 and macho.c 4 "elf.h"; ldelf.h 3 "src/elf.h"; asimpl.h 121: the comment on `npfa` describes `dwlocp`/`dwtab` (117); AGENTS.md lists `platform/darwin/` as "the Mach-O writer" with no file and `macho.c` is built with the assembler, not the object | wrong first impression of the module | rewrite with section 2's text | S | none | reading |
| Q-asobj-18 | L2 | pattern | `vasm.c`: private `map` 20-29 (`include/port/mapfile.h` exists), `now` 31, inline FNV 52/64, two `fopen` writers 101-102 and 114-117 beside `elf_write_file` | one more copy of each helper | `lib/` | S | none | `vas-obj.py` |

**Q-asobj-01: the split of `as/as.c`.**  Confirmed; the survey's list missed
symbols, macros' loops, `.symver` and the driver.

| New file | Lines today | Job |
|---|---|---|
| `as/as.c` | 15-42, 454-599, 2150-2169, 2173-2610, 3645-3720 | messages, frames and the statement scanner, labels, directive dispatch, `statement`, lifecycle, `as_end` |
| `as/sym.c` | 44-113, 1352-1499, 3552-3643 | symbols, numeric labels, `.set` variables, attribute directives, `.symver`/`.weakref` |
| `as/expr.c` | 115-452 | expressions |
| `as/section.c` | 601-774 | `.section`, standard sections, name parsing |
| `as/data.c` | 776-1350 | values, strings, floats, alignment, `.comm`, `.space`/`.fill`/`.org`, `.incbin`, `.reloc` (839-920) |
| `as/macro.c` | 1501-2029 | `.macro`, arguments, templates, `.rept`/`.irp`, `.exitm` |
| `as/cond.c` | 2031-2148 | conditionals |
| `as/cfi.c` | 2612-2772, 2799-2927 | `.cfi_*` and `.eh_frame` |
| `as/relax.c` | 2929-3234 | variable items: `.prefalign`, deferred alignment, LEB of differences, `.skip`, x86 jumps |
| `as/dwline.c` | 3236-3550 | `.file N`, `.loc`, `.debug_line` |
| `platform/darwin/astext.c` | 3722-4081 | Apple dialect to GNU dialect (uses `malloc`, `<ctype.h>`, nothing of `As`) |
| `lib/leb.h`, `obj/` | 2775-2783, 2787-2797 | LEB128; frag offsets (Q-asobj-09) |

**Q-asobj-02: the target-independent part of a fixup.**  Everything except
four facts.  Shared: load the frag, section, place and both symbols
(a64 89-95), fold absolute symbols (96-97), the difference case (98-116:
undefined subtrahend, same-section difference patched, cross-section error,
`c += P - S(b)`, absolute type to pc-relative type, patch or relocate), a
constant, a local pc-relative target in the same section patched in place,
else `elf_reloc`.  Per CPU: (1) `is_pcrel(type)`; (2) the absolute to
pc-relative type map (a64 106-111, x64 81-87); (3) which pc-relative types
may never resolve in place (x64 97-98: GOT, TLS, `sym@PLT`); (4) the patcher.
One real difference to keep as a flag: a constant target is stored as given
on arm64 (118) and as `c - P` on x86-64 (94).  The keep-symbol rule is the
fifth hook: x86-64's is `x64_keepsym` in its `fixup.c`, arm64's is inline in
`platform/linux/elf.c` 41-42.  `macho.c` 240-247 can use the shared opening.

**Q-asobj-03: every byte and LEB128 helper.**  The survey's line numbers are
corrected here.  From `lib/bytes.h` (new; `include/common/buf.h` holds buffers, not
stores): `put` objimpl.h:40 and macho.c:226 (verbatim); `p16`/`p32`/`p64`
elf.c:264-266; `p32`/`p64` macho.c:496-497; `get32` a64/obj/fixup.c:17; `ew`
x64/as/asm.c:279; hand-written stores at as.c 799, 830, 1136, 1268, obj.h
196, parse.c 556; host-endian `memcpy`/`vbuf_put` of integers at as.c 2858,
2863, 2881, 2915, 3488, 3535, wasm as.c 621/623 and 693, wasm.c 214/218.
Outside the area, the other side: `rd32`/`wr32`/`wr64` ld.c:70-72 =
ldmacho.c:93-95, `b32`/`b64` ldmacho.c:684-685, `le32` ar.c:427.  From
`lib/leb.h` (new): `uleb`/`sleb` as.c:2775-2783 (over `Buf8`), `leb_len`
as.c:3044, inline ULEB elf.c:200, `put8`/`uleb`/`sleb` wasm as.c:45-69 =
wasm.c:17-41 (verbatim), `pad_uleb`/`pad_sleb` wasm.c:52-66 (one body twice);
outside: wasmld.c:96-116 (= wasm as.c 45-72), 143, 158, 1264-1278 (=
wasm.c 48-63), x64/emit/print.c:182.  One encoder into a caller's bytes
(`size_t vleb_u(uint8_t *, uint64_t)`) serves `Buf8`, `vbuf` and the padded
form.  Also shared verbatim with the wasm linker: `GROW` wasm as.c:35-43 =
wasmld.c:33-43.

**Q-asobj-04: the wasm assembler against `as/as.c`.**  Written twice:
- lexical layer: `ws`/`idch`/`ident`/`accept`/`at_end`/`number` (wasm 161-238)
  against `ws`, `isidc`, `parse_name`, `parse_number` (asimpl.h 153-159, as.c
  171, 634); statement splitting on `;` and `#` with strings (wasm 1206-1241)
  against `next_stmt_copy` (as.c 501-568);
- expressions: wasm `expr` (241-272) accepts `number`, `sym`, `sym +/- n`,
  `n + sym` only; as.c 115-452 is the full GNU grammar.  `.int32 a - b` or
  `(1 << 3)` is an error in wasm text;
- strings: wasm `data_string` 698-749 against `parse_string` as.c 958-986;
  they already disagree (`\v`, `\a` only in wasm; `\x` takes two digits in
  wasm, any number in as.c);
- directives with the same meaning: `.section .text .data .bss .globl .weak
  .hidden .type .size .set .p2align .balign .byte .. .quad .zero .skip .ascii
  .asciz` (wasm 836-1090; as.c 2173-2455) and `name = value` (wasm 1142-1153,
  as.c 2499);
- the symbol table: FNV with open addressing and `malloc` per name (wasm
  81-132) against `elf_sym` (obj.c 105-129).

What forces a separate back half: the model.  `wmod` has functions with
bodies and locals, typed symbols (function, data, global, table, tag),
signatures and relocations that are padded LEB fields patched at layout;
`ElfObj` has sections, frags and fixups.  Instructions are stack-machine
mnemonics with immediates, no registers or addressing modes, and a label in
`.text` starts a function, so nothing of `arch/<cpu>/as/` applies.  What does
not force it: everything before a directive's meaning.  A shared front
(scanner, `Lx` and expressions over a symbol callback, string decoding,
conditionals and macros, which are text only) would give wasm `.s` macros and
`.if` for free and leave wasm about 600 lines: its table, `instruction`, the
directive meanings, the model.

**Q-asobj-05: a table-driven x86-64 assembler.**  Every `x64_i_*` function
opens with `char stem[32]; int sz = 0, cc; (void)a, (void)mn, (void)o,
(void)nops, (void)stem, (void)sz, (void)cc;` (sys.c 6-9, 48-51, 108-111;
gpr.c 6-9, 36-39, 69-72, 146-149, 381-384; x87.c 6-9, 59-62; sse.c 6-9,
101-104, 234-237; avx.c 6-9): the locals of the one function these were cut
from.  Only `x64_i_int` uses `stem` and `sz`.  Inside, most instructions are
already rows of small tables (`zero[]` sys.c 54-96, `sm[]` 114-132, `vx[]`
avx.c 14-39, `pk[]` sse.c 240-263, `x3[]` 134-152, `sse[]`, `mv[]`, `fm[]`,
`im[]`), each searched linearly with `strcmp`, and the 14 functions are tried
in order.  The form that exists for arm64 fits: one table `{ name, class,
opcode bytes, digit, flags }` found by hash after one suffix split, and a
class function per operand shape (raw bytes, `op /digit` on one operand,
`reg, r/m` with a prefix, the same with imm8, VEX forms 0-3, ALU, shift).
The irregular ones (`mov`, `imul`, `xchg`, `in`/`out`) stay code as classes.
Three repeated statements become helpers on the way: `if (o[i].kind ==
XK_EXPR) o[i].kind = XK_MEM;` (about 40 times), `.w = z == 8, .opsz16 = z ==
2` (about 30), `x64_imm(a, &o[0], &f, &c); f.imm = c;` (14).

**Q-asobj-07: private containers.**  The block "if full: double, `rget` from
`a->defs`, `memcpy`, swap" is at as.c 1455-1460, 2191-2196, 2407-2412,
2502-2507, 2675-2680, 2710-2715, 2935-2940, 3254-3259, 3304-3311, 3381-3386,
3578-3583 and parse.c 331-336.  Hash tables: `numlab` as.c 68-89 and
`var_slot` 91-113 (the same code, another key); macros 1503-1509, 1872-1883,
1887-1894; the `.req` buckets (TODO.md:1680, confirmed: asimpl.h 72-73 and
188-196, as.c 2377-2386 and 2500-2512, parse.c 6-24); the object's symbol
table (TODO.md:1680, confirmed: obj.h 131, obj.c 90-129, hash in
`include/common/hash.h:14`); wasm `was_sym` 81-132; `srh`/`mnh` in enc.c; wasm
`insn_find`.  `hsv` (asimpl.h 171-185) is `vhash_obj` (include/common/hash.h 14-28)
without the final `| 1`: the scan's longest run in the area (250 tokens).
FNV-1a is written five times here: as.c 260-263 and 303-305 (the same
register hash twice), enc.c 40-45, wasm as.c 81-88, vasm.c 52/64.

**Q-asobj-15: ELF and Mach-O constants.**  The reason in `ldelf.h:3` ("the
assembler's src/elf.h is separate on purpose") names a file that no
longer exists, and what it protected against is gone: both headers define the
same enumerators with the same values, and `ldelf.h` 55-69 repeats 30
`R_AARCH64_*` values of `include/arch/a64/reloc.h`.  The only real difference is that
readers need the structs (`Ehdr`, `Shdr`, `Sym`, `Rela`) and the writer stores
fields by offset (`shdr`, elf.c 268-273, with 24 and 64 as sizes).  No TU
includes both today, and none can.  One `platform/linux/elf.h` (constants and
structs) plus `arch/<cpu>/reloc.h` serves both sides; `obj.h` then stops
defining a file format.  Mach-O the same: `ldmacho.c` 44-55 has the names,
`macho.c` writes `0xfeedfacf`, `0x0100000c`, `0x19`, `0x32`, `0x2`, `0xb`
(513-539) and keeps a second relocation enum (30-35 against `R_UNSIGNED ...`
in `ldmacho.c` 53-54).

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `ElfObj`, `ElfSec`, `ElfFrag`, `ElfSym`, `ElfFix`, `elf_*` | obj.h; 150 mentions in 18 files, 642 call sites outside `obj/`, `ld/`, `platform/linux/` | the format-neutral object; Mach-O is written from it | `Obj*`, `obj_*`; `elf_` only for `elf_finish`/`elf_emit`/`elf_write_file` (D2) |
| `as.c` | `as/` and `arch/wasm/as/` | two assemblers | `arch/wasm/as/wasmas.c` |
| `a->pfa`, `npfa` | asimpl.h 120 | every variable item, not only `.prefalign` | `a->items` |
| `a->sv`, `rm == 2` | asimpl.h 109, as.c 2413, 3595 | `.symver` list that also holds `.weakref` | `a->alias_at_end`, a `kind` field |
| `Sv` / `a->sv`; `Lx` (as.c 167) / `lx` (wasm 157) | | string view / symver; two lexer structs | `Str`; one `Lx` |
| `ElfSym.size_sub` | elf.c 230, 321 | reused as the name's string-table offset | its own field or the `Out` array |
| `ElfFix.pad` | obj.h 120, asm.c 590, x64 fixup.c 97 | "never resolve in place" flag | `keep_reloc` |
| `Out` | elf.c 19, objimpl.h 43 | the ELF writer's layout | `ElfLayout`; `Mo` as `MachoLayout`, `lay` as `WasmLayout` |
| `a->al`, `al()` (elf.c 102, macho.c 320), `al` (as.c 1145) | | `.req` aliases; align up; an alignment | `a->req`; `valign_up` in `lib/` |
| `a->cs`, `a->ss`, `a->fr`, `a->nl`, `a->xs`, `a->sb`, `a->dwf/dwd/dwl` | asimpl.h 83-116 | conditionals, section stack, frames, numeric labels, expansion stack, statement buffer, DWARF tables | words, not two letters |
| `sym`, `here`, `fold`, `apply`, `expr`, `primary`, `trim`, `latest`, `err` | as.c | unprefixed statics; `err` is VPRIV in four modules | `as_` prefix once split (they become VPRIV) |
| `statement`, `directive`, `instruction`, `expr`, `ws` | as.c and wasm as.c | the same names for other functions (`ws` has two signatures) | `was_` prefix or sharing (Q-asobj-04) |
| `was->cur` 0/1/2/3 | wasm as.c 429 | none, text, data, `.init_array` | an enum |
| `x64_i_*`, `k_*`, `d_*`, `dt_*` | | x86 family, arm64 class, directive, Darwin text | keep; write the convention down |

Prefix conventions today: `elf_` public object API (and both writers'
internals), `as_` the assembler's public API, `a64_`/`x64_` per CPU, `was_`
wasm model, `d_` directives, `k_` encoder classes, `dw_` DWARF, `var_`
relaxation, `dt_` Darwin text.  They break in `as.c` (most statics have none)
and in `arch/x64/as` (`eb`, `ew`, `fits8`, `isconst`, `opsize`, `rex8`,
`sfxsize`, `split_sfx` are VPRIV without `x64_`).

Words with two meanings: **reg** is a register (`parse_reg`, `cfi_reg`) and
"registered in the symbol table" (`elf_reg`, `ElfSym.reg`, `was_reg`);
**frag** is not MC's fragment but a subsection piece or a split continuation;
**fix** is `ElfFix` (pending) and `A64Fix` (an encoder's result); **section**
in wasm text is a segment, a custom section, or a mode; **elf** is a format
and the neutral model.

## 6. Decisions needed

- **D1 wasm assembler.**  (a) A shared front end (scanner, expressions,
  strings, conditionals, macros) with wasm as a second directive and
  instruction back end; (b) a documented exception that shares only `lib/`.
  Recommendation: (a), after Q-asobj-01 has put expressions and macros in
  files of their own; it removes about half of `arch/wasm/as/as.c`.
- **D2 the object's name.**  Rename `ElfObj`/`elf_*` to `Obj`/`obj_*` (642
  call sites) in one scripted window, or keep the name and say in `obj.h`
  that it is historical.  Recommendation: rename, with aliases for one
  release; it is also what makes `platform/linux/elf.c` findable.
- **D3 the CPU interface of the assembler and object.**  A `const` ops table
  chosen from `o->machine` (one indirect call per instruction), or a
  compile-time include per CPU as the walker's `LO()`.  Needs a measurement
  on the kernel's `.S` files; recommendation: the table, since the assembler
  already dispatches at run time 25 times.
- **D4 format constants.**  One `platform/linux/elf.h` and one
  `platform/darwin/macho.h` shared by writers and linkers, or keep two
  copies.  Recommendation: one each; the stated reason no longer holds.
- **D5 Mach-O through ELF names.**  Keep the round trip (Apple dialect to GNU
  dialect to ELF-named sections to Mach-O) with one mapping table, or give a
  section its Mach-O identity in the model and let the assembler read Apple's
  directives directly.  Recommendation: one table now (Q-asobj-08); the
  second is a feature and belongs with Mach-O unwind info.
- **D6 the x86-64 mnemonic table.**  Hand-written as arm64's, or generated.
  Needs the cost of today's chain measured first (`vbench` on an x86 TU);
  recommendation: hand-written, hashed, one table.
- **D7 lookup tables.**  Built at first use (today: `pthread_once` twice, an
  atomic once) or generated and `const`.  Recommendation: generated, as
  `a64sysreg.inc` already is; it removes three pieces of process state.

## 7. Not covered

- `arch/a64/as/enc.c` was read for structure as instructed: the header, the
  helpers and tables' ends (1-215, 625-800), the atomics opening, the AdvSIMD
  lookup and all of the dispatch (2084-2110, 2585-2600, 2710-2745,
  2855-3022), plus the function list; the encoders of 800-2580 were not read
  line by line.  `sve.inc`: 1-112, 278-330, 725-740 and the function list.
  The scan reports 12 k redundant tokens inside `arch/a64/as` (operand-check
  preambles such as enc.c 731-737 repeated at 1872, 2069, 2453, 2487); they
  were not triaged one by one.
- `a64sysreg.inc` (generated) was not read.  `ld/ldelf.h`, `ldmacho.c`,
  `ld/wasmld.c` and `arch/a64/codegen/darwin.c` were read only where they are
  the other side of a duplicate.
- Nothing was built or run except `bench/quality.py`.  The costs in
  Q-asobj-05 and Q-asobj-09 are inferred from the code; the number of
  `strcmp` per x86 instruction and the quadratic section cases need a
  measurement.
- Whether each dead line in Q-asobj-16 is covered by a test was not checked.
- `platform/darwin/macho.h` does not exist; `include/arch/x64/reloc.h` (16 lines) was
  read although not listed.
