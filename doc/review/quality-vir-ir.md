# Quality review: VIR, the format, parser, verifier, analyses, allocator (2026-11-13, main at 2e6d3e73)

## 1. Summary

`vir/` is 19 files, 9804 lines, 6.2 % redundant by the scan (114 runs of 40+
tokens).  The format and its three readers (print, parse, verify) are sound and
well commented; the module depends on nothing outside `lib/`.  It is not sharp
in three places:

1. **Analyses have no owner.**  Nothing records whether the graph, the
   dominators or the loops of a function are valid, so every user recomputes:
   on the way to arm64 code one function gets 5 CFG builds, 6 successor-list
   builds and 3 dominator runs in `vir/` (4 with the builder's own).
2. **The allocator's output has a writer and five hand-written readers**
   (checker, dumper, both lowerings), and one reader uses another rule.
3. **Seven kinds of per-opcode fact are outside the opcode table**, each in two
   to five places; one of them lets the parser store a flag the printer spells
   differently.

Around these: `vir.c` holds nine jobs, the module is built as two halves
because of one static name and one macro, three thread-local contexts hold 96
reservations with scratch arrays shared by comment, and the two test tools
carry a diverged copy of their file handling.

## 2. Inventory

| File | Lines | What it actually does | Better name | Header |
|---|---|---|---|---|
| `vir.h` | 353 | Public format: records, opcodes, flags, signature words, limits, the opcode-table types, the API of the core | | good; does not mention the functions of `facts.c` it declares (273-280) |
| `virimpl.h` | 58 | `struct vir_ctx` (the thread's arrays), `VPRIV`, six internal prototypes | | stale: lists 5 of the 12 files that include it; documents a macro nobody uses (55-56) |
| `table.c` | 142 | The opcode table; type and condition-code names; lookup by name; `vir_opnd_fixed` | | good |
| `vir.c` | 895 | Nine jobs: reservations, constant table, construction, operand iterator, CFG, RPO and dominators, use counts, canonical form, the unit's tables and function store | split, see below | good (an honest list) |
| `print.c` | 699 | Text form out | | good |
| `parse.c` | 2123 | A private vector and name map; lexer; token helpers; unit declarations; operands and fix-ups; instructions; function assembly | split (Q-vir-ir-08) | good |
| `verify.c` | 1083 | Every rule of SPEC 12; its own exposure analysis (149-258) | | good |
| `facts.c` | 180 | One backward demand analysis (`vir_low32`) and three idiom matchers (`vir_mask32`, `vir_read_be`, `vir_sext_shift`) | `demand.c` + `idiom.c` | label for the first function only |
| `layout.c` | 142 | Block order after allocation: chase lone branches, rotate loop headers, say which joins to duplicate | `blockorder.c` | good |
| `layout.h` | 15 | Its three prototypes; includes `alloc.h` | `blockorder.h` | good |
| `loops.c` | 101 | Loop forest as block ranges, in its own thread-local arrays | | stale sentence (3: "vir_canon ... does not exist yet") |
| `alloc.h` | 156 | Register description, the allocator's output, the loop forest's types, checker and printer prototypes | | good; the usage lines (6-8) have old signatures |
| `alloc.c` | 2156 | Webs, liveness sweep, register assignment, edge moves, the save point, the driver, 74 reservations | split (Q-vir-ir-07) | good |
| `allocchk.c` | 423 | The independent check of an allocation | `alloc_check.c` | good |
| `allocdump.c` | 250 | `vir_alloc_print`; also `vir_reg_name`, which the checker uses | `alloc_print.c` | good |
| `synth.c` | 226 | A made-up register target for the allocator's tests | `alloc_testtarget.c` | good; "until V2 gives the real x86-64 one" is past |
| `synth.h` | 16 | Its type and `vir_synth_init` | `alloc_testtarget.h` | label only |
| `virtest.c` | 429 | Tool: parse, canon, verify, print twice; a self-test of the builder API, `facts.c` and `layout.c` | | good |
| `viralloc.c` | 357 | Tool: allocate a text unit on the test target; sweep, mutate, time | | good |

**What the module is for, and what a newcomer must know** (draft of the
opening comment):

VIR is the SSA form of one function at `-O2`: a flat array of 16-byte
instructions, blocks as ranges of it, block parameters instead of phis, and an
`extra` word array for lists.  `vir/` owns the format, its text form, the
verifier, the graph analyses, register allocation and block order.  It builds
no IR from C and emits no machine code.

- Index 0 is a `nop` and means "no value"; `extra` offset 0 means "none";
  `VIR_NOBLK` means "no block".
- A function is in one of two forms.  Builder form: constants may lie inside
  blocks, `nconst` is 0.  Canonical form (`vir_canon`): constants first in
  order of first use, each block after its immediate dominator, no `nop`, no
  unreachable block.  Everything except construction, `vir_canon` and the
  printer needs canonical form.
- One function per thread is live: its arrays are the thread's reservations.
  `vir_begin` and `vir_fn_load` invalidate the previous `vir_fn`.
- Predecessor lists are the tail of `extra`; `blk.idom/pre/post`, the loop
  forest and the allocator's output are valid until the next call that
  computes them on the thread.  Nothing records that they are valid.
- `f->tmp` and the context's `bs[]`, `aux`, `sc` are scratch of whichever
  function runs; none survives a call into the module.
- An iterator (`vir_oit`) reads the instruction when it is made: iterate a
  copy when the list may move.

**Split of `vir.c`**, one job per file (moves only; all stay in the unity TU):

| New file | From `vir/vir.c` | Job |
|---|---|---|
| `ctx.c` | 8-38 | The thread's reservations; `loops.c:16-30` and `alloc.c:32-111` register theirs here |
| `consttab.c` | 40-98, 151-174 | Hash-consed constants: reset, find, rebuild, `vir_const` |
| `construct.c` | 100-149, 176-257 | `vir_begin`, add instruction, block, slot, extra, location |
| `operands.c` | 259-352 | `vir_oit`: the value and block operands of an instruction (next to `table.c`: it is the table's missing list column, Q-vir-ir-03) |
| `cfg.c` | 354-427 | Successor lists, predecessor lists |
| `dom.c` | 429-542 | Reverse postorder, dominators, tree numbers |
| `uses.c` | 544-563 | Use counts (with `vir_low32`, the other per-value demand fact) |
| `canon.c` | 565-739 | The canonical form |
| `unit.c` | 741-895 | Symbols, signatures, saved functions of a text unit |

## 3. Passes and data flow (L4)

There are no optimization passes in `vir/` (SPEC 14's `simplify`, `gvn`, `mem`,
`dce` run on the builder's pre-IR in `codegen/vir/build.c`).  What `vir/` runs for
one function on the way to arm64 code, in order:

| Step | Entry | Calls and walks |
|---|---|---|
| Build | `codegen/vir/build.c` | Its own RPO (`vw_rpo`, 1861) and its own Cooper-Harvey-Kennedy dominators (`vw_doms`, 3075) on the pre-IR |
| Canon | `vir/vir.c:583` | `vir_dom` (593), which calls `vir_cfg` (479); constants sweep (627); copying sweep (678); constant-table rebuild (735); `vir_dom` and `vir_cfg` again (738) |
| Verify, only `VCC_VIR=v` | `vir/verify.c:869` | About nine sweeps (969, 992, 1060, 676, 772, 788, 805, 201 and 228, 840) and `vir_dom` (1073) |
| Loops | `vir/loops.c:39` | `vir_cfg` (49); predecessor pass; `qsort`; block pass |
| Allocate | `vir/alloc.c:1946` | `vir_cfg`, then `vir_dom` which calls `vir_cfg` (1962); successor lists again (1965-1982); back-edge ends per block and per loop (1987-2047); `webs` (2048); `sweep5` backward (2049; both again in linear mode, 2054-2055); `webs_loops` (2058); `save_point`, a full sweep (2060); sweep 6 forward (2094) |
| Check, only `VCC_VIR_CHECK` | `vir/allocchk.c:359` | Blocks to a fixed point, then once more reporting |
| Order | `vir/layout.c:27`, `106` | Lone-branch chase; rotation and order |
| Demand | `vir/vir.c:544`, `vir/facts.c:8` | `vir_uses` one sweep; `vir_low32` backward sweeps until nothing changes |
| Lower | `codegen/vir/a64.c:2119` | One forward sweep reading the allocation |

**Who computes what, and who invalidates it:**

| Analysis | Result lives in | Recomputed by | Marked invalid by |
|---|---|---|---|
| `vir_cfg` | `blk.pred/npred`, lists at the tail of `extra`; successor lists in `ctx->bs[0]`, `ctx->sc` (private) | every `vir_dom`, `vir_loops_build`, `vir_alloc` | nothing; `cfg_mark/cfg_end` are in the thread context (`vir/virimpl.h:40`), so a function loaded from a unit forgets them and the next call appends a second copy of the lists |
| `vir_dom` | `blk.idom/pre/post` | `vir_canon` twice, `vir_verify`, `vir_alloc` | nothing |
| `vir_uses` | caller's byte array | each lowering (`codegen/vir/a64.c:2443`, `arch/x64/codegen/vir.c:1030`) | caller |
| `vir_loops_build` | `loops.c`'s thread-local arrays | each lowering; `viralloc` | the next call on the thread |
| Back-edge ends (`tend`, `tendi`, `apre`, `lendpos`) | `alloc.c`'s thread-local arrays | every `vir_alloc` (twice on x86-64's retry, `arch/x64/codegen/vir.c:1033`, `1042`) | the next call |
| Exposure (SPEC 6.4) | private to the verifier (`vir/verify.c:195`) | the builder computes the claim, the verifier checks it: two on purpose | |

**Computed twice for one function:** predecessor lists 5 times, successor
lists 6 times, dominators 3 times in `vir/` and once more in `build.c`, by a
second implementation of the same algorithm (`vir/vir.c:486-518`,
`codegen/vir/build.c:3091-3117`).  The cost in instructions is for
`quality-passes.md` to measure; `REVIEW-vir-lowering.md` section 3 has
`vir_dom` at 1.89 s on a 16 000-block ladder, so the repeats are of the
expensive one.

**Is there one place that says what is valid where?**  No.  The only
statements are field comments (`include/vir/vir.h:127-128`) and two notes that tell
the caller to make the redundant call ("vir_cfg first", `include/vir/vir.h:268`,
`include/vir/alloc.h:82`; both callees call it themselves).  `PLAN-vir.md:172` rules
out a pass manager.  A validity byte in `vir_fn` keeps that rule and removes
the repeats (Q-vir-ir-01).

**Files with several passes:** `vir.c` (nine jobs), `alloc.c` (six phases),
`facts.c` (one analysis, three matchers), `verify.c` (rules and the exposure
analysis).  **A pass spread over files:** loop analysis (`loops.c` builds the
forest, `alloc.c:1987-2047` the back-edge ends, `vir_blk.depth` and `VB_LOOP`
in `vir.h` hold nothing).

**The allocator's phases** (`vir/alloc.c`):

| Phase | Lines | Does |
|---|---|---|
| Preconditions | 1824-1846 | `param` first, `proj` after its call |
| Graph | 1962-1982 | CFG, dominators, its own successor lists |
| Loop ends | 1984-2047 | Per back-edge target the end of what it keeps live |
| Webs | 154-269, 283-358 | Union-find of a block parameter and its arguments; members that interfere leave |
| Sweep 5, liveness | 404-643 | Backward: next-use stack, live-in pool, call and loop flags, hints |
| Save point | 1848-1944 | Shrink-wrapping: the block that dominates everything needing the frame |
| Sweep 6, assignment | 645-940, 1560-1820 | Per instruction: fixed operands, the rest, clobbers, the result |
| Block entry | 1271-1558 | Entry state from the best predecessor, loop-header rule, parameters, preload |
| Edges | 942-1198 | Parallel moves sequenced (`sequence`), entry state made to fit (`conform`) |
| Output | 2132-2155 | Pointers into the thread's arrays |

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-vir-ir-01 | L4 | redundant walk | `vir/vir.c:479`, `593`, `738`; `vir/loops.c:49`; `vir/alloc.c:1962`, `1965-1982`; `codegen/vir/build.c:3075`; `include/vir/vir.h:268`, `include/vir/alloc.h:82` | 5 CFG builds, 6 successor-list builds, 3+1 dominator runs per function; two dominator implementations | A validity byte in `vir_fn` (CFG, DOM, LOOPS), set by the analysis, cleared by `vir_begin`, `vir_canon` and whoever edits a terminator; each analysis returns at once when valid; canon's closing `vir_dom` becomes a renumbering | M | low | `bench/vir-test.py` (`dom/`, `canon/`), `bench/vir-alloc.py`, `vcc-same.py`, `vbench.py gate` (must fall) |
| Q-vir-ir-02 | L1 | pattern | writer `vir/alloc.c:376-400`, `1583`, `1705`, `1793`; readers `vir/allocchk.c:149-230`, `276`; `vir/allocdump.c:110-117`, `144`, `184-192`; `codegen/vir/a64.c:2125-2160`; `arch/x64/codegen/vir.c:962-969` | One layout contract (a comment, `include/vir/alloc.h:122-124`) decoded by hand five times; the x86-64 reader disagrees with the writer on `multi` | A cursor in `alloc.h` (block start, edits at an instruction, operand registers, result register) used by all four readers | M | medium (lowering's hot loop: inline) | `bench/vir-alloc.py` (dumps identical, `mutate`), `vcc-same.py`, the C suites with `VCC_VIR_CHECK` |
| Q-vir-ir-03 | L1 | pattern | see the paragraph: seven kinds of fact, over 30 sites | A new opcode or flag is edited in up to five files; `call ... nsw` parses and prints as `noreturn` | Columns and small tables in `table.c` | M | low; one changes what the parser accepts | `bench/vir-test.py` with `--mutate 3000`, `bench/virgen-test.py`, new `bad/` cases |
| Q-vir-ir-04 | L4, L5 | mixed file | `vir/vir.c` sections at 8, 100, 259, 354, 429, 544, 565, 741 | Nine jobs behind one name; the iterator every pass uses is in the middle | The split in section 2 | S | none (move) | unity and `VCC_UNITY=` builds, `vcc-same.py` |
| Q-vir-ir-05 | L7 | misplaced | `src/Makefile:98-99` ("not in the unity TU: own statics"); `vir/print.c:211` and `vir/alloc.c:135` (`is_const`, two meanings); `vir/table.c:7` and `vir/alloc.c:72` (`R`); `include/vir/layout.h:5`; `include/vir/alloc.h:70-83` | The module is two halves; `VPRIV` helpers cannot be called from the allocator half, so it rebuilds successor lists; the loop forest's types sit in the allocator's header; block order sits in the core half but needs the allocator's output | Rename the two names, one unity TU; `vir_loops` to `vir.h` with the analyses; `layout.c` to the allocator group | S | low | both builds |
| Q-vir-ir-06 | L8, L6 | global state | `vir/vir.c:10`, `22-33`; `vir/loops.c:16-29`; `vir/alloc.c:32-108`; aliases in the paragraph | 96 reservations in three contexts, about 125 GB of address space per thread (added by hand), no teardown; a new analysis must read every other one to find a free array | Named scratch in `vir_ctx`; the other two contexts hang off it; one function releases a thread's VIR memory | M | low | both builds, sanitizer suites, `vbench.py gate` |
| Q-vir-ir-07 | L4 | long function | `vir/alloc.c`: `vir_alloc` 210 lines (1946), `instr` 260 (1560), `sweep5` 219 (424), `enter` 193 (1365); copies in the paragraph | Loop analysis inside the driver, redone per call; six phases in one file | `alloc_live.c` (webs, sweep 5), `alloc_assign.c` (sweep 6, entry), `alloc_edge.c`, `alloc_shrink.c`, `alloc.c` (driver); loop ends to `loops.c` | M | medium | `bench/vir-alloc.py` (dumps, `random -n`, `mutate`, `time`), `vcc-same.py`, `vbench.py gate` |
| Q-vir-ir-08 | L4 | long function | `vir/parse.c`: `parse_instr` 392 lines (1141), `lex_all` 209 (210), `finish_function` 153 (1702), `parse_func` 144 (1873); copies in the paragraph | Five jobs in 2123 lines; eight idioms written two to seven times | `parse_lex.c` (17-530), `parse_unit.c` (531-828), `parse_fn.c` (the rest); helpers for the idioms | M | low | `bench/vir-test.py` (messages and `line:col` identical), `--mutate` under ASan |
| Q-vir-ir-09 | L1 | duplicate | numbering `vir/print.c:607-612`, `vir/allocchk.c:376-381`, `vir/allocdump.c:89-95`; value text `vir/print.c:162-209`, `vir/allocchk.c:40-54`, `vir/allocdump.c:32-62`; locations `vir/allocchk.c:56-62`, `vir/allocdump.c:64-70`; `vir/allocdump.c:97-104`, `133` | Three numberings that agree only in canonical form; three spellings of one value; the dumper finds block and instruction lines by the shape of the printer's text | `print.c` exports the numbering, one value printer and a per-line callback; `vir_reg_name` and one location printer in `alloc_print.c` | S | low | `bench/vir-alloc.py` expected dumps |
| Q-vir-ir-10 | L1 | duplicate | `bin/virtest/virtest.c:25-67`, `306-363`, `407-420`; `bin/viralloc/viralloc.c:33-68`, `167-205`, `338-348` | Two copies of a test-file convention, already diverged (`find_sep`) | `vir/testfile.h`: read, split, directives, bless, compare | S | none | `bench/vir-test.py`, `bench/vir-alloc.py` |
| Q-vir-ir-11 | L2 | pattern | `vir/parse.c:19-28`, `30-106`, `199-208`, `697-702`, `815-820`; `vir/vir.c:751-756`, `770-776`, `802-804`, `817-822`; `vir/print.c:22-33`; `vir/table.c:106-115`; `vir/verify.c:214-218` | Four ways to grow an array, a private name map with its own FNV-1a, a private `printf` into a `vbuf`, two linear name lookups | `lib/`: see the paragraph | S | none | `bench/vir-test.py`, `--mutate` under ASan |
| Q-vir-ir-12 | L4 | redundant walk | `vir/verify.c:892-944`, `1073`, `871`; sweeps at 969, 992, 1060, 676, 772, 788, 805, 840; `716-717` | The verifier computes dominators and rewrites the constant table and `f->tmp`: code after it can depend on it having run (`codegen/vir/try.c:120` runs it only under `v`) | With Q-vir-ir-01 the verifier asks for dominators and changes nothing else; F1 and F2 into `check_ins` | S | low | `bench/vir-test.py` (`bad/`) |
| Q-vir-ir-13 | L1 | pattern | field select at `vir/print.c:227`, `516`, `vir/parse.c:1233`, `1523`, `vir/vir.c:344`, `vir/verify.c:323`; signature words at `vir/facts.c:41`, `vir/print.c:125`, `vir/synth.c:44`, `102`, `vir/verify.c:95`, `550`, `1033`; switch stride at `vir/print.c:452`, `vir/vir.c:313`, `vir/parse.c:1807`, `vir/verify.c:278`, `603` (and 5 sites in the lowerings); type name at `vir/print.c:203`, `321`, `575`, `vir/verify.c:50`; `blk_last` at `vir/alloc.c:137` against `vir/layout.c:64`, `92`, `137`, `vir/verify.c:1061` | The format's arithmetic is repeated at each use | `static inline` accessors in `vir.h`: field k, signature group g, switch stride, type name, last instruction of a block | S | none | both builds, `vcc-same.py` |
| Q-vir-ir-14 | L9 | error policy | `vir/parse.c:175`; `vir/verify.c:25`; `vir/vir.c:38`; `vir/alloc.c:367`, `1963`; `abort()` 23 times; `vir/allocdump.c:171` | Four error channels and an abort in one module; `fail` has no format attribute and line 1963 passes the integer 0 to `%s` | The policy `quality-diag.md` proposes; now: `fail` variadic with the attribute | S | none | both builds with warnings |
| Q-vir-ir-15 | L5 | misnamed | `vir/facts.c:8`, `64`, `135`, `171`; `vir/allocdump.c:12`, `82`; `vir/allocchk.c` | `facts.c` says nothing; the matchers depend on the builder's emission order (`vir/facts.c:114-131`, `174`); the printer is in "dump" | Names in section 2 | S | none (move) | both builds |
| Q-vir-ir-16 | L10 | dead, stale doc | lists in the paragraph | Fields and parameters that mean nothing; comments that send the reader the wrong way | Delete; correct | S | none | both builds, `bench/vir-test.py` |
| Q-vir-ir-17 | L6 | misnamed | `vir/alloc.c:32-70`, `113-133`; section 5 | Sixty arrays with two- to four-letter names, three of them reused for other data in a later phase | Section 5 | M | low (rename by script) | `vcc-same.py` |

**Q-vir-ir-01.**  `vir_canon` runs `vir_dom` on entry and again on exit; the
second run can be a renumbering of `idom` through the old-to-new block map
(the tree over reachable blocks does not change when blocks are renumbered,
and `pre/post` are copied with the block record at `vir/vir.c:683`) plus one
`vir_cfg`.  `vir_alloc` then needs neither call at 1962, and reads the
successor lists `vir_cfg` already built if they are made public.  The pre-IR's
`vw_doms` is the same algorithm on other arrays; whether it can share code is
for `quality-vir-build.md`.  SPEC 22.5 says dominators "are computed once
here".

**Q-vir-ir-02.**  The allocator writes one register byte per value operand in
`vir_oit` order without `multi` operands (`vir/alloc.c:393`), then one for the
result unless it is `void` or `multi` (1730).  arm64's reader does the same
(`codegen/vir/a64.c:2152-2154`).  x86-64's counts every operand and takes a
result byte for every non-void type (`arch/x64/codegen/vir.c:964-967`), so a call
with a two-piece result would shift every later register of the block.
Inferred: no `multi` value reaches x86-64 today (the file never names
`VT_MULTI`).  The checker is meant to know nothing of how the allocation was
made; sharing the reader of the format does not weaken that, the checker
already shares `vir_oit`.  The same holds for `get_fix`
(`vir/alloc.c:360-365` = `vir/allocchk.c:220-222`) and the "may be memory"
test (`vir/alloc.c:1669` = `vir/allocchk.c:235`): both belong in `alloc.h`.

**Q-vir-ir-03: is every per-opcode fact in the table?**  No.  The table has
the name, text form, result types, operand kinds, flag mask and effects.
`vir/print.c:322` and `vir/parse.c:1162` each switch on the form (16 and 17
cases); that is inherent in a text form with 20 shapes.  Outside the table:

1. Which fields hold values: `vir/vir.c:261` and `267-275` (a table filled by a
   constructor), `vir/verify.c:83` (an exact copy of the predicate),
   `vir/alloc.c:79-90` (a third table).  All three depend on the order of the
   `OK_*` enum, which `include/vir/vir.h:199-223` does not say.
2. Which opcode has a list, where and how long (`REVIEW-vir-lowering.md` 2.6
   names three decoders and proposes a list-kind byte; the count is higher):
   `vir/vir.c:288-334`, `vir/verify.c:269-291`, `vir/print.c:384-500`,
   `vir/parse.c:1271-1506`, `vir/alloc.c:87`, and direct readers of a `br`
   list at `vir/facts.c:25`, `vir/verify.c:692`, `vir/alloc.c:243`, `1164`.
3. Flag names: by opcode group in `vir/print.c:238-272`, as one flat list in
   `vir/parse.c:1013-1018`.  The parser sets the bit of any flag word on any
   opcode; `nsw` on a `call` is bit 1, inside the call's mask, and prints as
   `noreturn` (by reading; not run).
4. Function, block, slot and group words, each list twice:
   `vir/parse.c:1890-1895` and `vir/print.c:544-549`; `1611-1616` and
   `579-581`; `1951-1956` and `558-561`; `538-566` and `104-111`.
5. The counts of condition codes, 10 and 8: `vir/table.c:103-104`,
   `vir/parse.c:1187`, `1191`, `vir/print.c:277`, `279`, `vir/verify.c:372-385`.
6. What an operand kind accepts: `vir/verify.c:56-81` and
   `vir/table.c:125-142`, two switches over `OK_*`.
7. Opcode sets: needs a frame (`vir/alloc.c:1902-1909`), not a plain
   instruction (`vir/layout.c:67-69`), how a pointer operand is used
   (`vir/verify.c:157-181`), has a width (`vir/verify.c:394-399`).

Fix: a list column (kind and field), the two field masks as constants, one
flag-name table keyed by form, one `{name, bit}` table per flag set, one row
per operand kind, and a class column for the sets of item 7.

**Q-vir-ir-06.**  Scratch that changes meaning by position in the code:
`bs[0]` is successor offsets (`vir/vir.c:400`), then the block renumbering
(604), then the verifier's stamp ("bs[0] is free", `vir/verify.c:662`);
`bs[1]` is a count (400) and subtree sizes (476); `bs[3]` is the DFS stack
and the preorder cursor (475-476); `aux` is canon's block order (603) and the
verifier's exposure marks (800); `f->tmp` is canon's renumbering (620), the
verifier's value-to-block map (871) and the printer's names
(`vir/print.c:603`), so printing from inside the verifier would corrupt it.
`alloc.c` keeps its own successor lists (48) beside the context's.

**Q-vir-ir-07.**  Copies inside `alloc.c`: a definition leaving the live set
(499-513 = 567-581, the scan's 158 tokens); a value entering it (459-464,
481-486, 549-554); the move to a callee-saved register (1552-1555,
1811-1815); `pmv` (28) is `vir_emove` (`include/vir/alloc.h:96-101`) field for field;
`defpos` (699) returns its argument.  The loop-end tables (1984-2047) depend
only on the forest and the graph, yet every `vir_alloc` call rebuilds them.

**Q-vir-ir-08.**  Copies inside `parse.c`: `perr_at` (175) written out seven
times in the lexer (272-274, 299-301, 312-314, 346-348, 361-363, 377-379,
397-400); a counted list reserved in `extra` (1288-1296, 1374-1382,
1473-1481, 1493-1501); a block list (1363-1372, 1462-1472); the width suffix
(1105-1111, 1206-1211); a float literal (778-808, 937-950); a signature
looked up (1277-1284, 1857-1871); a symbol's addend (769-776, 894-900); a
slot declaration inline in `parse_func` (1924-1967).  `finish_function` scans
every fix-up for each untyped `icmp` constant (1748-1754): quadratic on a
generated unit.  Vectors local to a function leak when an error longjmps
(1306-1361, 1706-1715).

**Q-vir-ir-10: what the two tools share.**  `slurp` exactly.  `find_sep`
differs: `virtest` wants only blanks after `; ---` (`bin/virtest/virtest.c:56-62`),
`viralloc` takes any line that starts so (`bin/viralloc/viralloc.c:63`).  Also the loop
over the directive comments, the parse call with its `file:line:col` line,
the per-function load, canon, verify loop, and the bless writer.  Only
`viralloc` compares with the expected part (325-336).

**Q-vir-ir-11: which `lib/` facility.**  `PV`/`PV_PUSH` and `UGROW` are
`include/common/vec.h`'s `VVEC` with `realloc` instead of an arena; `include/common/vec.h` has no
heap variant and one user, so `quality-lib.md` decides its shape.  `sput` is a
`vbuf` (`include/common/buf.h`).  `putf` is `vbuf_f` (`include/common/buf.h:57`), which
`print.c:106` already calls.  The name map with FNV-1a (`vir/parse.c:30-106`)
has the shape of `arch/wasm/as/as.c:100-106` and `ld/wasmld.c:357-364` (scan):
one string map in `lib/` over `include/common/hash.h`.  `vir_unit_sym` (strlen and
compare over all symbols per reference) and `vir_opfind` (128 names per
instruction line) want the same map.  The overflow checks of
`vir/verify.c:214-218` and the 20 list bounds of the form
`(uint64_t)off + 1 + n > ne` are `include/common/ckd.h`'s `vckd_mul` and `vckd_range`.
`slurp` could be `include/port/mapfile.h`.

**Q-vir-ir-16.**  Dead: `vir_blk.depth` is never written or read
(`include/vir/vir.h:125`); `VB_LOOP` is only parsed and printed; `VIR_FMT`
(`vir/virimpl.h:56`); `vir_term_of` and `vir_succ_csr` are `VPRIV` but used in
`vir.c` alone (`vir/virimpl.h:46-47`); `parse_oplist`'s `mode` and `aux`
(`vir/parse.c:1126`, `1136-1137`); `vir/parse.c:1244-1247`, `1973-1975`,
`2012-2016`; `vir/print.c:535`, `551`; `vir/verify.c:767-768`, `794`, `1035`;
`vir/loops.c:42`.  Stale: "vir_cfg first" (`include/vir/vir.h:268`, `include/vir/alloc.h:82`);
the usage in `include/vir/alloc.h:6-8` (`vir_alloc` takes five arguments,
`vir_alloc_check` no forest, and `f` is changed: the graph is rewritten);
"false (function unchanged)" for `vir_canon` (`include/vir/vir.h:282`; on failure the
constant table is already reset, `vir/vir.c:622`); `vir/loops.c:3`;
`vir/virimpl.h:2-3`; `vir/synth.c:3`; AGENTS.md ("linked into no compiler tool
yet", "one module `vir_SRC`"; `src/Makefile:99`, `107`).

**Does the module depend on nothing in `frontend/c/`, `codegen/` or `arch/`?**  It holds:
the only includes are `include/common/arena.h` and `include/common/buf.h` (`include/vir/vir.h:35`,
`vir/virimpl.h:15-16`), and no file outside `vir/` includes `virimpl.h`.  Two
soft ties: `facts.c`'s matchers assume the builder's instruction order, and
`layout.c` and `synth.c` encode arm64's and x86-64's habits by description.

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `A`, `ax` | `vir/alloc.c:113`, `133` | The allocation in progress; a macro that hides `(*a->x)` | `alloc_run`; write `x->` |
| `nu`, `lend`, `endp`, `wi`, `wd`, `pv`, `pr`, `ps`, `sst`, `tmk`, `rq`, `lcs`, `ldm`, `lds` | `vir/alloc.c:35-66` | next use, live-to-loop-end, last use, live set index and members, state pairs (value, register, stored), stored stamp, entry mark, ready queue, location stamps | `next_use`, `loop_end`, `last_use`, `live_idx`, `live`, `st_val`, `st_reg`, `st_stored`, ... |
| `wlive` | `vir/alloc.c:206`, `333`, `311` | A stamp per block, then a count per web, then call flags per web | three names |
| `wlst` | `vir/alloc.c:223`, `254`, `329` | A list tail, then a generation stamp | two names |
| `bmark` | `vir/alloc.c:1979`, `1527` | "No earlier predecessor", then "edges done" | two names |
| sweep 5, sweep 6 | `vir/alloc.c:424`, `645` | Liveness; assignment (numbers from SPEC 22.6, whose sweeps 3 and 4 do not exist in `vir/`) | `live_sweep`, `assign_sweep` |
| `pop`, `push_pop` | `vir/parse.c:121`, `832` | A parsed operand | `opnd` |
| `ps` | `vir/parse.c:146`; `vir/alloc.c:55` | Parser state; a stored flag | `parser`; above |
| `nm` | `vir/parse.c:37`; `vir/print.c:13`, `vir/allocchk.c:33` | A name map type; a value-number array | `namemap`; `vnum` |
| `bs[6]`, `sc`, `aux` | `vir/virimpl.h:34-42` | Scratch per block; successor lists; scratch per value | Q-vir-ir-06 |
| `fms`, `vfm` | `vir/vir.c:267`, `vir/alloc.c:69` | Value-field masks per opcode | a table column |
| `vir_oit` | `include/vir/vir.h:285` | Operand iterator | `vir_opnd_iter` |
| `facts.c`, `allocdump.c`, `synth.c` | | | section 2 |

Prefix rule: public names are `vir_`, enums `VO_ VT_ VIF_ VB_ VS_ VF_ VTG_
VG_ VC_ VR_ VFX_ VE_ VU_`.  It breaks at `TM_`, `OK_`, `EF_`, `FM_`
(`include/vir/vir.h:192-231`), public and unprefixed, visible in the `emit` unity TU;
at `VL_SLOT` (`include/vir/alloc.h:95`), while `vl_` and `VL_` are the arm64
lowering's prefix; and at every static (`put`, `cur`, `next`, `fail`, `bad`,
`block`, `loc`, `pad`, `one`, `fixed`, `assign`), which is what keeps the
module in two build halves.

Words with two meanings: **slot** (a VIR stack slot `$N`; a spill slot `ssN`,
`out->slot[v]`; a hash-table entry, `vir/vir.c:66`; `soff` is successor
offsets in `include/vir/alloc.h:129` and slot offsets in `codegen/vir/a64.c:33`).
**pos** (operand position `vir_oit.pos`; instruction index `vir_edit.pos`; a
cursor into `opr`, `vir/allocchk.c:149`; tree numbers `pre/post`).  **location**
(a source location `vir_loc`; a register or spill slot, encoded as `256 + s`
in `vir/alloc.c:946` and `vir/allocchk.c:38` and as `VL_SLOT | s` in
`include/vir/alloc.h:95`).  **fix** (`vir_fix`, fixed registers; `fixup`, a parser
back-patch).

## 6. Decisions needed

1. **How analyses are known to be valid.**  (a) a validity byte in `vir_fn`,
   analyses that return when valid, the order written once in
   ARCHITECTURE.md; (b) a pass table with declared analyses (PLAN-quality
   decision 4); (c) only a documented order.  Recommendation: (a).  It keeps
   `PLAN-vir.md:172` ("no pass manager") and is checkable: a debug build can
   recompute and compare.
2. **Where the loop forest lives.**  SPEC 22.5 and 15.5 give it to
   `vir_canon`; the code has `loops.c`, dead `vir_blk.depth` and `VB_LOOP`,
   and loop-end tables in `alloc.c`.  Either canon builds it and fills the
   block fields, or `vir_loops` is an analysis of its own and the spec and the
   two fields change.  Recommendation: its own analysis with the loop ends,
   behind the validity byte; delete `depth`.
3. **One reader of the allocation for the checker too?**  Recommendation: yes
   for the cursor, `get_fix` and the memory test (they define the format), no
   for anything that decides.
4. **Flag names in the table** change what the parser accepts (`call ... nsw`
   becomes an error).  Recommendation: accept the change, add `bad/` cases.
5. **One unity TU for all of `vir/`**, after two renames.  Recommendation:
   yes; it also lets the allocator call internal helpers.
6. **A heap-backed vector and a string map in `lib/`** (for `quality-lib.md`):
   `include/common/vec.h` is arena-only and the parser must free on error.
7. **Per-thread VIR memory**: three contexts, about 125 GB of address space a
   thread, never released.  The scheduler's design (threads times
   reservations) decides whether they need a teardown; this review only asks
   that they be reachable from one place.
8. **Formatting.**  `facts.c`, `bin/virtest/virtest.c:146-236`, `vir/alloc.c:1709-1713`
   and parts of `layout.c` are written without spaces and with one-space
   indents.  PLAN-quality decision 6 (no mass reformat) leaves them; say
   whether new files of a split are reformatted when they move.

## 7. Not covered

- Nothing was built or run.  The `call ... nsw` round trip, the x86-64 `multi`
  mismatch and the quadratic `icmp` search are from reading; the 125 GB is the
  sum of the sizes at `vir/vir.c:22-33`, `vir/alloc.c:91-108` and
  `vir/loops.c:26-29`.
- The cost of the repeated CFG and dominator runs in instructions
  (`quality-passes.md`).
- `codegen/vir/` and `arch/x64/codegen/vir.c` beyond the call sites cited
  (`codegen/vir/a64.c:2118-2160`, `2355-2446`; `arch/x64/codegen/vir.c:955-972`,
  `1000-1082`; `codegen/vir/build.c:3075-3120`; `codegen/vir/try.c:100-135`).
- `test/vir/`, `test/vir-alloc/` and the `bench/vir-*.py` harnesses were
  not read; that `virtest`'s expected output is compared by the harness is
  inferred from `virtest.c` not comparing.
- SPEC-vir.md beyond sections 14 and 22.5-22.6; PLAN-vir.md beyond the lines
  cited.  The allocator's algorithm was read for structure, not for
  correctness (`REVIEW-vir-lowering.md` section 7 leaves it uncovered too).
- The scan's cross-module runs under 60 tokens (loop shapes shared with
  `frontend/diag/diag.c`, `ld/objcopy.c`, `frontend/c/replay.c`) were triaged as shape, not code.
