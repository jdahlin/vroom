# Quality review: linkers, objcopy/nm, archives (2026-11-13, main at 2e6d3e73)

## 1. Summary

Five programs share one area and almost no code: the ELF linker (`ld/ld.c` +
five `.inc` fragments, 7.7 k lines, parallel, arena-allocated, lld-exact), the
wasm linker (`ld/wasmld.c`, serial, malloc per symbol), the Mach-O linker
(`platform/darwin/ldmacho.c`, serial), objcopy/nm (`ld/objcopy.c`) and ar
(`utils/ar.c`).  Each has its own symbol table, file reader, archive parser,
error function, option loop and output writer.  `ar.c` is the model of the
five: request state in a struct, `lib/` buffers and mapping, scoped cleanup,
no globals, never aborts.  What matters most:
1. **Link state is file-scope** (about 150 variables in the ELF TU, reset by a
   hand-kept list; 34 in wasmld, 29 in ldmacho): it forces three locks, blocks
   the file split, and has already produced a missed reset (`x86_ld`, Q-ld-02).
2. **`ld.c` holds 30 jobs** in an order that is not the pipeline's; arm64 code
   sits in the generic file and generic code in the arm64 fragments.
3. **Untrusted input is checked three different ways**: ELF well (one
   validator per tool, written three times), wasm objects barely, Mach-O by
   hand; `include/common/ckd.h` is used in 9 places in the whole area.
Nothing was run except `bench/quality.py` and grep; bugs below are from reading.

## 2. Inventory

| File | Lines | What it does | Better name | Header comment |
|---|---|---|---|---|
| `ld/ld.c` | 6713 | the ELF linker, whole; also `main()` of vlink/vobjcopy/vnm/wasm-ld (6688-6713), response files, Mach-O dispatch | split, see below | stale: says no COMDAT, COMMON or TLS, arm64 only (1-32) |
| `include/ld/ld.h` | 28 | entry points of ld, Mach-O ld, objcopy, nm | `linkers.h` or one per tool | stale: "arm64 kernel build" |
| `ld/ldelf.h` | 73 | ELF64 structs and constants, arm64 relocation numbers | `elf64.h`, one copy with `include/obj/obj.h` | stale: names `src/elf.h` (gone); "readers only" (ld.c writes with it) |
| `ld/wasmld.c` | 2220 | wasm linker: reader, resolution, GC, layout, module writer, options | `ld/wasm/link.c` (or split in 5) | good |
| `ld/objcopy.c` | 1622 | objcopy (1-1263) and nm (1265-1622), ELF view + validator, StringTableBuilder | `objcopy.c` + `nm.c` + `elfview.c` + `strtab.c` | good, objcopy scope by example |
| `arch/a64/ld/thunks.inc` | 88 | thunk records and the (section, reloc) -> thunk hash | `thunk.c` | label |
| `arch/a64/ld/scan.inc` | 106 | `pic` flag, generic `reloc_class`, arm64 relocation scan | `scan.c`; the first two belong in `ld/` | label |
| `arch/a64/ld/thunkpass.inc` | 222 | call collection and thunk creation per layout pass | merge with `thunks` | label |
| `arch/a64/ld/relocate.inc` | 278 | arm64 relocation application, relaxations, `.rela.dyn` entries | `relocate.c` | label |
| `arch/x64/ld/x64.inc` | 275 | x86-64 scan, GOT need, relaxation, application (one file where arm64 has four) | `scan.c` + `relocate.c` | good |
| `platform/darwin/ldmacho.c` | 1472 | Mach-O executable linker: .tbd reader, objects, relocations, LINKEDIT, signature | `ld/macho/link.c` (the wasm linker is in `ld/`, this one is not) | good |
| `utils/ar.c` | 1070 | ar: thin, GNU and BSD archives, r m t q s p x d | lives in `vccd/` but is a binutil like objcopy | good; one empty banner (56) |
| `include/utils/ar.h` | 12 | `ar_run` | | stale: "r/m/t on thin archives" |
| `common/ldhash.c` | 391 | XXH3-64, BLAKE3 (NEON x4), SHA-256 | `linkhash.c`; other indent style (4 spaces) | good |

**Map of `ld.c`** (line ranges as read): helpers 63-79 · diagnostics 81-134 ·
timing 136-166 · arena 168-237 · thread pool 239-313 · options struct 315-344 ·
input model 346-422 · global symbol table 424-555 · script model 557-657 ·
address helpers, EV 659-783 · expression evaluation 785-917 · script lexer
919-1017 · expression parser 1019-1249 · script statements 1251-1664 · glob
1666-1720 · map_file, archives 1722-1822 · ELF validation, parse_obj 1824-1992 ·
input sections, gnu.property 1994-2102 · resolution 2104-2267 · relocation
targets 2269-2409 · output state 2411-2439 · .eh_frame 2441-2681 · name cache,
orphan rank 2692-2865 · --gc-sections 2867-2973 · assign_sections 2975-3171 ·
merge pieces 3173-3348 · phdr helpers, file offsets 3354-3445 · symtab I
3447-3523 · write_isec 3532-3627 · .eh_frame_hdr 3629-3687 · stub object
3689-3752 · parse_args 3754-3895 · lazy archives 3897-4030 · load_inputs,
tables 4032-4134 · write_output 4136-4164 · script symbols, address assignment
4167-4330 · adjust_output_sections 4332-4374 · program headers 4376-4538 ·
symtab II 4540-4740 · shstrtab, headers, build id 4742-4848 · -shared
4850-5163 · script PHDRS 5165-5210 · fills 5212-5251 · GOT probe, commons
5253-5331 · synthetic sections 5333-5386 · script symbol declaration 5388-5471 ·
stub collection 5473-5524 · gorder 5526-5542 · undefined diagnostics 5544-5703 ·
demote, start/stop 5705-5750 · `run()` 5754-6033 · ld -r 6035-6468 ·
reset_state 6470-6540 · Mach-O probe 6542-6564 · response files 6566-6664 ·
ld_main, main 6666-6713.

**Proposed split** (TODO.md "The linker as a split module"; that item says
~4400 lines, it is 6713): `ldimpl.h` (types, the context) · `diag.c` ·
`opts.c` (parse_args, find_lib, response files) · `input.c` (mapping, archives,
validation, lazy members) · `resolve.c` (table, COMDAT, commons, gorder,
undefined report) · `script.c` (lexer, parser) · `scripteval.c` (EV, eval,
assignments, address passes) · `sections.c` (assignment, orphans, GC, synthetic)
· `merge.c` · `ehframe.c` (with the header) · `layout.c` (phdrs, offsets) ·
`symtab.c` · `dynamic.c` · `write.c` (copy, fills, headers, build id, file) ·
`reloc.c` (targets, the scan/apply driver) · `relocatable.c` · `stubs.c` ·
`link.c` (`run`, `ld_main`) · `vlink.c` (`main`).  Arena and pool go to the
shared library and scheduler.
**What prevents it today:** (1) the state: `objs`, `isecs`, `mergeins`,
`merges`, `gsyms`/`hnames`/`hword`/`gnames`, `gorder`, `osecs`, `topcmds`,
`isds`, `opt`, `out`, `phdrs`, `outsecs`, `thunks`, the 16 `*_isec` ids, are
`static` and of anonymous `VVEC(T)` types, which cannot be declared in a
header; (2) the fragments depend on their include position (`thunks.inc`
declares types `ld.c` uses 700 lines later; `scan.inc:5` defines `pic`,
`relocate.inc:21` `reladyn_buf`, `relocate.inc:16` `fits_int` which
`thunks.inc:68` forward-declares); (3) nine ad-hoc forward declarations
(`header_size`, `eval`, `get_osec`, `pexpr`, `lextract`, `reset_state`,
`ld_relocatable`, `g_referenced`, `pprimary`) instead of prototypes; (4) hot
`static inline` helpers (`isec_va`, `gsym_va`, `reloc_target_final`) must stay
inlinable, so the module needs the unity build frontend/c/pp/emit have; (5) `ld.c` is
compiled twice (`src/Makefile:138,142`) because `main()` is inside it.

**Module text (draft).**  The linkers turn relocatable objects into an image.
ELF (`ld.c`): the output must equal ld.lld 23 byte for byte for the same
command line, so pass order and tie-breaks are lld's (comments name the lld
function).  Invariants: inputs are mapped read-only and never copied except
unaligned archive members; everything is an index (`ISec` id, `G` id, `OSec`
id; `NONE` = absent, `OS_DISCARD` = dropped), never a pointer, because `isecs`
is reallocated; all memory is per-thread bump arenas freed after the link;
`pfor` bodies may only touch their own element and atomics; global symbol ids
depend on thread timing, order comes from `gorder`; after `final_addresses()`
addresses are read from `fva`/`isec_fva`.  One link at a time per process.

## 3. Passes and data flow (L4)

**ELF `run()` (ld.c:5754-6033), in order:** args -> map inputs, archive
members, lazy pass (serial, reads member symbol tables) -> `parse_obj`
(parallel: validate) -> symbol table sized -> script parsed (AST) ->
`fill_isecs` (par) -> COMDAT dedup -> gnu.property -> `resolve_obj`,
`check_dups` (par) -> [`ld -r` leaves here] -> stubs -> script symbols ->
`gorder` -> `finish_sym` (par) -> versions -> GC -> commons -> synthetic
sections -> `assign_sections` (+ merge sections, `eh_frames`) -> commit ->
demote -> undefined report -> merge dedup (par, 32 shards) -> script
assignments -> `scan_relocs` (par) -> GOT -> dynamic contents -> address
assignment and thunks to a fixed point (at least 3 `assign_addresses`) ->
phdrs -> symtab sizes -> file offsets -> copy + relocate (par) -> dynamic,
eh_frame_hdr, symtab (par), headers -> build id (par) -> write.
Walks that repeat (cost not measured; Q-ld-14): `obj_valid` twice per archive
member in a lazy link (3961, 1978); `eh_split` twice under `--gc-sections`
(2889, 2603); `.note.gnu.property` found by `strcmp` over all section headers
twice (2065-2101); string merge sections scanned twice (3185-3230); calls
counted then collected (`thunkpass.inc:13-64`); each object's globals walked by
`resolve_obj`, `check_dups`, `collect_stubs`, `build_gorder`, `make_commons` x3;
all relocations by `find_got`, `scan_relocs`, GC, thunk pass 0, the GOT-relax
pre-scan (`relocate.inc:36-50`) and the application.  The nest
topcmds -> osec -> ISD -> sections is spelled out seven times (3113, 3168, 4281,
5222, 5833, 5996, `thunkpass.inc:71`).  One pass in two places: the symbol
table (3447-3523 and 4540-4740).
**Mach-O `link_macho` (722 lines), 22 jobs:** options 753-791 · read 793-820 ·
output sections 822-859 · resolution 861-907 · commons 908-919 · imports
920-944 · reloc scan 946-952 · order 954-960 · command sizes 962-980 · layout
982-1025 · addresses 1027-1034 · fixups 1035-1059 · chained fixups 1061-1129 ·
exports trie 1130-1144 · symtab, indirect, strings 1146-1236 · signature size
1238-1245 · load commands 1247-1329 · contents, stubs, apply 1331-1352 · fixup
words 1353-1377 · UUID 1381-1390 · signature 1391-1428 · write 1437-1464.
`relocs()` runs three times (948, 1039, 1352): the second only to redo the
fixups with addresses.
**wasm `write_module` (564 lines), 9 jobs:** undefined symbols 1501-1544 ·
function and tag indices 1545-1584 · globals 1585-1611 · memory layout
1614-1710 · synthetic values 1711-1719 · exports 1720-1781 · code and data
with relocations 1786-1869 · the 11 sections 1870-2029 · file 2034-2047.
`gc()` runs twice (2200, 2216); `gall` is walked five times.
**`objcopy_run` (550 lines), 16 jobs:** model 610-642 · keep-debug 643-650 ·
rename 651-674 · flags 675-682 · add-section 683-707 · debuglink + CRC 708-741 ·
segments 743-772 · dump 774-795 · removal 797-832 · symbols 834-920 · prefix
922-943 · binary writer 945-977 · string tables 979-998 · generated sections
999-1053 · layout 1055-1105 · image 1106-1156.

## 4. Findings

| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
|---|---|---|---|---|---|---|---|---|
| Q-ld-01 | L8 | global state | ELF TU: about 150 file-scope variables (121 declarations, counted by script), reset by name in `reset_state` ld.c:6471-6540, `ldlock` 6542; ldmacho 29 + a process cache (`tbd`, `ex_hash`, `sdk_version` 219-221), `reset` 703, lock 744; objcopy 4 (`oerr oerrors prog oc`), lock in `server/vccd.c:1182`; wasmld 34, no reset, no lock | one link at a time in vccd; every new global must be added to a list by hand; wasmld can run once per process | a `Link` context per linker (contents below), zeroed per link, owning its arena | L | medium | `vbench.py check --base` (vmlinux identical), `vlink-opts.py`, `VCC_LDCMP=1 vcc-ctests.py`, `vobjcopy-cmp.sh`; missing: two links at once under TSan |
| Q-ld-02 | L8 | global state (bug) | `x86_ld` ld.c:65 is set at 1977 and cleared nowhere (grep: one assignment; absent from 6471-6540).  The comment at 1977 promises a one-machine check that does not exist; the flag is a plain `bool` written from `pfor` | inferred, not run: after one x86-64 link a vccd process links every arm64 input as x86-64; mixed inputs link silently | reset it now; a `machine` field in the context, checked after loading | S | low | new `vccd-test.py` case: x86-64 link, then arm64 link |
| Q-ld-03 | L5 | mixed file | `ld.c` map in section 2.  arm64 in the generic file: thunk encodings 3567-3584, `rel_width` 1825, `find_got` 5258, `R_AARCH64_RELATIVE` 3596/3611.  Generic in the arm64 fragments: `scan.inc:5,8-34`, `relocate.inc:21,269-277`, the thunk-section bookkeeping `thunkpass.inc:201-220` | nothing can be found without the map; a second CPU had to be added by `x86_ld ?` at 12 sites | the split of section 2; a per-CPU table (Q-ld-15) | L | low (moves) | `vbench.py check --base`, unity and `VCC_UNITY=` builds |
| Q-ld-04 | L9 | error policy (bug) | `rel_width` ld.c:1825-1833 knows arm64 numbers only; `obj_valid` 1936-1938 uses it for x86-64 objects, so `R_X86_64_64` (1), `PC64`, `TPOFF64`, `SIZE64` are checked as 4-byte fields; `x64.inc:131` checks only `r_offset < size`, 140/143/176/222 write 8 bytes; 198 reads `loc[-3]` after a check for 2 (`:23`), 235 reads `loc[-1]` before the check at 237 | inferred: a corrupt x86-64 object writes 4 bytes past its section in the image | `rel_width` per CPU | S | low | `vlink-bad.py` has no x86-64 case (grep): add them |
| Q-ld-05 | L9 | error policy | wasmld: counts from the file go to `calloc(k + 1, ..)` unchecked, and `k + 1` wraps for `k = 2^32-1` (455, 510, 577, 594-597, 649, 660, 691, 714, 738); loop without the reader's `bad` test (651); indices from symbols and relocations used unchecked: 1369, 1386, 1404, 1420, 1521-1522, 1569-1573, 1582, 1747-1755, 1877, 1896; `apply` 1446 never checks that the 5/10-byte field lies in the body | a corrupt wasm object crashes or writes outside a buffer; `vlink-bad.py` has 9 wasm cases, all for lengths and the archive table | a count helper (count <= bytes left), index checks in `resolve()`/`rel_value()`, field check in `apply` | M | low | new `vlink-bad.py` cases under ASan; a fuzz target |
| Q-ld-06 | L1 | duplicate | `!<arch>` member headers parsed five times: ld.c:1747-1822, wasmld.c:865-902 + 904-972, objcopy.c:1346-1427, ar.c:228-278 (thin), ar.c:720-784 (regular, BSD) | long names, thin paths, `/SYM64/`, padding and the bounds checks exist in five versions that already differ (only ar reads BSD; only wasmld reads the symbol table) | `archive.h`: a member iterator (name, bytes or thin path, size) and a symbol-table reader | M | low | `var-cmp.py`, `vlink-bad.py`, `vlink-opts.py` (lazy cases), `vobjcopy-cmp.sh` |
| Q-ld-07 | L1 | near-duplicate | ELF input validation three times: `obj_valid` ld.c:1847-1945, `elf_valid` objcopy.c:101-158, `elf_syms` ar.c:323-380.  ELF constants twice: ldelf.h:38-55 = `include/obj/obj.h:40-45`; arm64 relocation numbers twice: ldelf.h:56-71 = `include/arch/a64/reloc.h:26` (ld.c includes x86-64's `reloc.h` but not arm64's); raw numbers in code: `0x70000001` (1999, 2773), `0x200000` (2908), `0x80` (2950), `62`/`183` (1971, 3715, 4779, 6445) | a check added to one tool is missing in the others | one ELF view (`elf64.h` + `elfview.c`: open, validate, sections, symbols) used by ld, objcopy, nm, ar | M | low | `vlink-bad.py`, `vobjcopy-cmp.sh`, `var-cmp.py` |
| Q-ld-08 | L3 | thread | `pfor` ld.c:239-313: threads created at the first parallel loop (296), never joined, one job slot; count = P cores on macOS (5761), all CPUs on Linux (5765), cap 64, `--threads`; runs inside a vccd worker beside vccd's own workers; arena per thread by generation (174-176, 201); `__thread scratch` 3477 never freed | one of the six private thread mechanisms; idle threads for the life of vccd; no common cap | the shared parallel-for of the scheduling review; arena handed out by the context | M | medium | `vbench.py gate`, TSan batch |
| Q-ld-09 | L9 | error policy | calls that end the process: `abort` ld.c:192, objcopy.c:67, wasmld.c:39,87; `exit` ld.c:6578, ldmacho.c:86,91,678.  Unchecked allocation: ld.c:2136, 2494, 2634, 2874-2875, 3486, 5859; ldmacho.c:469, 479, 1162; `mmap` objcopy.c:966, 1106; `fstat` ld.c:1732, objcopy.c:169 | out of memory in one link takes the server down; `ar.c` shows the other policy (`vsbuf.oom`, the request fails) | the one OOM policy TODO.md asks for, on the context | M | low | `vcc-stress.py --vccd` |
| Q-ld-10 | L9 | error policy | nine error returns of `run()` (ld.c:5775-6018) and one of `ld_relocatable` (6452) skip the teardown at 6027-6031: the input mappings, from 5791 on the four symbol tables (4120-4123), and at 6018 the image stay mapped; `lgot` (5859) is never freed even on success; ldmacho `goto fail` at 1059 leaks `order`, `le` | a failed link leaks address space in vccd | one teardown called on every path; context arena | S | low | ASan with leak detection on a failing link |
| Q-ld-11 | L1 | pattern | open-addressing tables written by hand: ld TU seven (gsyms 516-554, COMDAT 2114-2172, `ncache` 2696-2750, merge shards 3263-3317, `lsyms` 3910-3938, `tr_` `thunks.inc:21-66`, `rmap` 6069-6083); ldmacho two (177-209, 332-351 + 364-371); wasmld one, chained, fixed at 65536 buckets (368-396); objcopy one (203-264) | 11 probe-and-grow loops (scan: ld.c:2706-2714 = 2138-2146) | a string -> id set in `lib/` (hash, length, pointer); the lock-free `gintern` stays special | M | low | `vbench.py gate` |
| Q-ld-12 | L1 | duplicate | arm64 instruction patching: the ADRP immediate is encoded eight times (`relocate.inc:135-136, 148-149, 170-171, 174, 194-195, 203`, ld.c:3579, ldmacho.c:537); lo12 `relocate.inc:239-254` and ldmacho.c:539-545; branch26 `relocate.inc:117` and ldmacho.c:613; the adrp/add-or-ldr/br sequence ld.c:3577-3582 and ldmacho.c:1347-1350 | two linkers must agree on the same bit fields | `arch/a64/ld/patch.h` (`a64_put_page21`, `_lo12`, `_branch26`) for both | S | low | `vbench.py check --base`; host-program build + run |
| Q-ld-13 | L1 | duplicate | a script statement (ASSERT, PROVIDE, HIDDEN, assignment) is parsed in three places: ld.c:1634-1659 = 1494-1519 (exact, 220 tokens) and a variant at 1419-1446 | three copies of lld's `readAssignment` rules | one `pstatement()` returning an `Assign` or NULL | S | low | `vlink-opts.py`; vmlinux and vDSO identical |
| Q-ld-14 | L4 | redundant walk | the list in section 3 (ELF six, Mach-O `relocs` x3, wasm `gc` x2, objcopy RELA 858-867 and 907-919) | unmeasured | keep what the first walk found (`EhRec`s, validated `Obj`, a call count); record Mach-O fixups as (object, section, index) | S each | low | `VLD_TIME=1` phases before and after |
| Q-ld-15 | L1 | near-duplicate | the per-relocation loop exists per CPU: `x64.inc:68-91` = `scan.inc:36-62`; `x64.inc:117-135` = `relocate.inc:23-62`; `find_got` ld.c:5258 and `x64.inc:54`; chosen by `x86_ld ?` at ld.c:3550, 5276, 5860 | every further CPU copies the loop and its error messages | one loop in `ld/reloc.c` and a per-CPU table {scan, apply, needs_got, rel_width, thunks, trap fill, TLS layout} | M | low | `vbench.py check --base`, `vcc-x86.py` |
| Q-ld-16 | L2 | pattern | output files written six ways: ld.c:4136-4157 (unlink, then write in place: not atomic), 5509-5524; wasmld.c:2034-2045 (`tmp.PID`, rename, no dirfd); ldmacho.c:1438-1464 (`tmp.PID`, renameat); objcopy.c:499-525 (fixed temp name); ar.c:669-699 (mkstemp), with the write loop three times inside ar.c (682-690, 704-717, 941-951) | six policies for partial output | `write_file_atomic(dirfd, path, p, n, mode)` in the shared layer | S | low | the tools' suites |
| Q-ld-17 | L2 | pattern | whole-file reads: ld.c:1724, wasmld.c:417, objcopy.c:160-185, ldmacho.c:223 and 798-815 (read into malloc), ar.c through `include/port/mapfile.h`.  `vmap_file` takes no dirfd (`mapfile.h:23`), so no linker can use it | five readers | `vmap_file_at(dirfd, ..)` | S | low | the tools' suites |
| Q-ld-18 | L2 | duplicate | byte helpers: ld.c:70-73 = ldmacho.c:93-98 (exact; `alignup` differs for a = 0), `ldhash.c:12-13`, ar.c:313-315; `alignskew` ld.c:75 = objcopy.c:73.  `be32` has three meanings (read wasmld.c:854, store ldmacho.c:96, append ar.c:413).  LEB: wasmld.c:96-118, 143-173, 1264-1278 = `platform/wasm/wasm.c:48-70`, `arch/wasm/as/as.c:45-72`.  `obuf`/`ob_put` wasmld.c:73-92 = `vbuf_put` `include/common/buf.h:41-54` (exact); `Buf`/`bput` ldmacho.c:670-687 | | `lib/bytes.h`, `lib/leb.h`, `vbuf` | S | low | `vcc-same.py`, `vcc-wasm.py --objcmp` |
| Q-ld-19 | L2 | pattern | options parsed by hand in six loops: ld.c:3780-3895, wasmld.c:2065-2130, ldmacho.c:758-785, objcopy.c:1173-1236, 1438-1473, ar.c:858-895; `VAL` defined three ways (ld.c:3789, wasmld.c:2068, objcopy.c:1176); a second list of value-taking options in `first_input_macho` ld.c:6550.  Response files reach ELF and Mach-O only (6669; `main` sends wasm away at 6696).  Parsed into fields nothing reads: `-Map`, `--pic-veneer`, `-z notext`, `-Bstatic` (ld.c:330-338), `--no-entry` (wasmld.c:2113) | `-Map` is accepted and writes nothing | an option table per tool {names, takes a value, action} and one scanner | M | low | `vlink-opts.py`, `vcc-driver.py` style table test |
| Q-ld-20 | L9 | error policy | five reporters: ld.c:89-134 (locked, limit, lld's blank line), ldmacho.c:69-79 (limit 20, "vld:"), wasmld.c:45-55 (stderr only), objcopy.c:51-60, ar.c:80-88 (an fd, exactly two `%s`).  `printf` to the process's stdout: ld.c:2967, 3830, which inside vccd is the server's, not the request's.  Version text twice and different: ld.c:3830, `server/caps.c:174` | | a diagnostic sink (err, out, limit, tool name) in the context | S | low | `vlink-opts.py` compares stderr |
| Q-ld-21 | L6 | misnamed | fields reused for another meaning: `G.hidden_script` = "assigned by the script" (5479), `G.dup` = visited (5497), `G.val` = string offset under -r (6329), `ISec.dyn0` = relocation index under -r (6277), `ISec.osec` = index into `routs` under -r (6273), `Pending.size`/`.buf` = a candidate range (4060), ldmacho `G.calign` = symbol index (1209) | a reader of the struct is misled | fields of their own | S | low | byte-identical outputs |
| Q-ld-22 | L1 | duplicate | small copies inside ld.c: `process_symbol_assignments` 4315-4330 = `for_assigns` 5388-5401; symbol section/value 4556-4568 = 5074-5082; member realignment 3949-3957 = 1957-1965; teardown 6460-6465 = 6027-6031; relro names 2804-2807 and 4388-4397; `got_seen` declared twice (3529, 5257) | | call the one copy | S | low | `vbench.py check --base` |
| Q-ld-23 | L1 | pattern | wasmld: relocation ranges for functions and segments 759-766 = 767-774; index of a function and of a global 1378-1387 = 1412-1421; a stub's dependency list walked three times (1119-1129, 2162-2177); "the stub named by an import's field" three times (1522-1528, 2183-2192 = 2204-2214) | | three helpers | S | low | `vcc-abi.py --target wasm64 --vlink` |
| Q-ld-24 | L4 | long function | `link_macho` 722, `write_module` 564, `objcopy_run` 550, `run` 280, `ld_relocatable` 255, wasm `parse_obj` 245, `ar_run` 229, `assign_sections` 197 | jobs listed in section 3 | one function per job, after the context exists | M | low | the tools' suites |
| Q-ld-25 | L10 | dead | ld.c: `Unused` 574-576; `&& 0` 765-766; dead store 2301; wrappers `usage_error` 3756, `finish_syms` 5722; second `for_assigns` 5501; `(void)nhid` 4722, `(void)n` 5702; empty banners 3354, 3525-3526.  wasmld: `gimps`/`ngi` (always 0) 1490-1491, 1612; `imm_globals` 1364; `stubname` 1897-1899; empty loop 1760-1765; `ty` 1787; `ngsym` 355; 674.  ldmacho: `undef0` 1206-1212.  ar: `(void)create` 972 | | delete | S | low | byte-identical outputs |
| Q-ld-26 | L10 | stale doc | ld.c:1-32, ld.h:2-12, ldelf.h:2-3, ar.h:5, TODO.md's split item ("~4400 lines", arm64 fragments only) | | rewrite with the split | S | none | |
| Q-ld-27 | L5 | misplaced | `ar.c` in `vccd/` while objcopy is in `ld/`; the Mach-O linker in `platform/darwin/` while the wasm linker is in `ld/`; nm inside `objcopy.c`; `main()` in `ld.c`; the objcopy lock in `vccd.c:1182` while the two linker locks are in the callee | | decision 5 | S | low (moves) | builds |
| Q-ld-28 | L4 | pattern | the script keeps a binary operator as a string: `binop` ld.c:836-865 runs up to 20 `strcmp` per node per evaluation, `prec` 1039-1055 re-reads it, 1086-1107 compare strings; the AST is evaluated in every address pass | unmeasured | an operator enum set by the parser | S | low | `VLD_TIME=1` "layout" |
| Q-ld-29 | L1 | duplicate | ar.c: the `/SYM64/` threshold loop 473-489 and 603-616; `getenv("SYM64_THRESHOLD")` 470 and 599 (the server's environment, not the request's) | | one helper; pass the value in | S | low | `var-cmp.py` |

**The duplicate scan, triaged.**  Real second copies: ld.c:1634-1659 (Q-ld-13),
5390-5403 and 6461-6465 (Q-ld-22), 3949-3957 (Q-ld-22), ldmacho.c:93-96
(Q-ld-18), wasmld.c:81-94 and 94-126, 1261-1281 (Q-ld-18), wasmld.c:766-774,
1412-1421, 2161-2167, 2202-2210 (Q-ld-23), `x64.inc:66-89`, `124-128`
(Q-ld-15), `relocate.inc:190-198` (Q-ld-12), ld.c:2706-2714 (Q-ld-11),
objcopy.c:908-911 (Q-ld-14).  Patterns that want an iterator, not a merge:
ld.c:5997-6004 (the script nest), 5529-5535 and 2203-2212 (an object's
globals), wasmld.c:1558-1593 (append to an output list), 2134-2138 (the two
export lists).  Inherent shape: the option chains (objcopy.c:1178-1220,
1452-1466, wasmld.c:2101-2118 = ld.c:3878-3881; they go with Q-ld-19),
ld.c:541-547 (`gfind` after `gintern`), wasmld.c:596-603.

**The three linkers, concern by concern.**

| Concern | ELF | wasm | Mach-O | objcopy/nm, ar | How alike |
|---|---|---|---|---|---|
| name -> symbol | ld.c:459-555, XXH3, lock-free, dense `G` by id | wasmld.c:354-398, FNV, chained, malloc each | ldmacho.c:165-209, FNV, open addressing | | one contract, three tables |
| resolution | 2175-2267 (atomic min on a rank key) | 810-852 | 864-907 | | the same lattice (strong, weak, common, lazy, undefined), different records |
| files in | 1724 mmap, dirfd | 417 mmap, cwd | 798-815 read, dirfd | objcopy 160, ar `mapfile.h` | identical job |
| archives | 1747 (ignores the symbol table; lazy by member symtab 3897-4030) | 865, 904 (lazy by archive symbol table) | none | objcopy 1346, ar 228, 720 | header parsing identical; laziness differs |
| errors | 89-134 | 45-55 | 69-79 | 51-60, ar 80 | identical job |
| options | 3780 | 2065 | 758 | 1173, 1438, ar 858 | identical job |
| response files | 6585 | none | through `ld_main` | none | |
| file out | 4136 | 2034 | 1438 | 499, ar 669 | identical job |
| relocations, layout, synthetic content | script, phdrs, thunks, .dynamic | index spaces, table slots, memory | segments, chained fixups, signature | | format-specific |

A **shared layer** all three (and objcopy, nm, ar) can stand on: file mapping
with a dirfd, the archive reader, a name table, the diagnostic sink, the
option scanner with response files, the atomic file writer, bytes/LEB, the
context with its arena, and for the two arm64 linkers the instruction patches.
Honestly format-specific: object parsing and validation, relocation scan and
application, layout, everything synthetic, and the details of resolution.
That is roughly a tenth of the 14 k lines (an estimate); the three data models (index-based
arenas, pointer graphs, small arrays) do not merge without rewriting two of
them.

**A link context would contain** (ELF, by group; counts approximate):
options and target (`opt`, `x86_ld`, `pic`, TLS: 7) · diagnostics (`errf`,
`nerrors`, limit, lock, dirfd: 6) · arena (`blocks`, lock, generation: 3) ·
inputs (`objs`, `pending`, lazy tables: 7) · sections (`isecs`, `mergeins`,
`merges`, `ncache`, `orphans`: 8) · symbols (`hnames`, `hword`, `gnames`,
`gsyms`, `ngsyms`, `hmask`, `gorder`, `script_new`, `script_ord`: 9) · script
(`osecs`, `topcmds`, `isds`, `sphdrs`, `vernodes`, `provides`, `refsyms`,
entry, parser cursor: 17) · layout (`dot`, `cur_osec`, `in_layout`,
`tbss_off`, recorded errors: 8) · thunks (6) · output (image, sizes, `phdrs`,
`outsecs`, 16 synthetic section ids, GOT, `.rela.dyn`, FDE list, write jobs,
final addresses: about 35) · symtab out (10) · dynamic (13) · `ld -r` (15) ·
stubs (2).  Process-wide and outside it: the pool, `timing`, `keep_maps`.

**Expression evaluators.**  The linker script's (ld.c:785-917 evaluation,
1039-1249 parsing) is not worth sharing with `as/as.c:320` or
`frontend/pp/ifexpr.c:260`.  They differ in every part but the fifteen-line climbing
loop: input (script characters, assembler text, preprocessor tokens), value
(lld's section-relative `ExprValue` with alignment; symbol minus symbol plus
constant with deferred relocations; signed/unsigned 64-bit with short-circuit
liveness), precedence table (C; GNU as, where `|` binds like `+`; C), truth
(1, -1, 1) and time (an AST evaluated in every layout pass; folded while
parsing).  What it needs is Q-ld-28.

**Bounds checks and `include/common/ckd.h`.**  Nine uses in the area: ld.c 1 (1866),
objcopy.c 1 (124), wasmld.c 7 (878-950, archives only), ldmacho.c 0, ar.c 0.
ld.c otherwise checks by hand in the subtract-then-compare form, which is
right (1853, 1860, 1938, 1779, 1812), except Q-ld-04.  objcopy's `in_file`
(97) is right; nm's version sections add `uint32_t` offsets that can wrap
(1525-1552) and index the string table unchecked (1531, 1545).  ldmacho casts
to 64 bits before adding (383, 396, 405, 422, 426), reads a load command's
header before testing that 8 bytes remain (389-390) and trusts `sz` for the
fields it reads (395, 404).  ar.c by hand, correct as far as read (335-372,
734, 741).  wasmld: Q-ld-05.  `vlink-bad.py` covers ELF (arm64) through
vlink, vobjcopy and vnm, and nine wasm cases; no Mach-O, no ar, no x86-64
(grep).

## 5. Names (L5, L6)

| Name | Where | What it is | Proposal |
|---|---|---|---|
| `G`, `gsym` | ld.c:436, ldmacho.c:154, wasmld.c:330 | three different global-symbol records | `ElfSym`/`MachoSym`/`WasmSym` or one prefix per linker |
| `Obj`/`obj`, `OSec`/`oseg` | ld.c:348/613, ldmacho.c:102/123, objcopy.c:361, wasmld.c:298/1216 | input object; output section, three unrelated structs | prefix per linker |
| `gfind` | ld.c:542, ldmacho.c:177, wasmld.c:368 | three signatures (find; find-or-add by flag; by length) | `sym_find` / `sym_intern` |
| `gs` | ld.c:359 (an object's global ids), ldmacho.c:165 (all globals), ld.c:2190 (one `G *`) | | `gids`, `globals` |
| `K_*` | ld.c:373 (section kinds), wasmld.c:219 (symbol kinds) | | `ISK_*`, `WSYM_*` |
| `ISD`, `EV`, `E_*`, `C_*`, `AETHER` | ld.c:585, 426, 561, 598, 657 | input section description, expression value, expression/command kinds, "outside any section" | spell out; a glossary line each |
| `dot`, `sp`, `out`, `pic`, `job` | ld.c:661, 921, 2413, `scan.inc:5`, ld.c:254 | location counter, script cursor, the image, "-shared", the pool's task | fields of the context |
| `l*` | ld.c:3904-4030 lazy archives; `lmap` 6061 local map; `lgot` 360 local GOT | one letter, three meanings | `lazy_*` |
| `s*`, `p*` | ld.c:937-1017 script lexer, 1081-1609 parser; but `phadd`/`phnew` 3368 are program headers, `put_sym` 3511, objcopy's `sname` 477 is a section's name | | `sc_*` |
| `r_*`, `rout*`, `ROut` | ld.c:6046-6468 | `ld -r` state | `rel_out_*` in its own file |
| `tr_*` | `thunks.inc:21-66` | thunk redirection map | `thunk_map_*` |
| `o` | wasmld.c:1485-2048 | the object, `obuf o` the module, prefix `o*` = output | `mod` for the module |
| `W32`, `memw` | wasmld.c:807-808 | address width of the link | `opt.wasm32` |
| `finish_sym`/`finish_syms`, `fin_isec`/`fin_gsym` | ld.c:2226, 5722, 2354 | set state from the winner; cache final addresses | `sym_settle`, `cache_final_*` |

Tool names: the binary is `vlink`, messages say `vld` (ld.c:87), the version
line `vld (vroom) LLD`, wasm errors `wasm-ld`, AGENTS.md "vld shim".
Words with two meanings: **stub** (zero-filled -O0 symbols ld.c:3689; Mach-O
import stubs; wasm stub libraries; wasm trapping functions), **piece** (a
merge string 3175; an .eh_frame record 2551), **lazy** (archive members;
`-z lazy`), **local** (`g_local` 4620 means "written as local", not
STB_LOCAL), **synthetic** (linker-made sections in ELF; linker-defined
symbols in wasm).

## 6. Decisions needed

1. **One linker core, or three linkers on a shared layer** (PLAN-quality
   decision 3).  Recommendation: the shared layer of section 4 (Q-ld-06, 07,
   11, 16-20), no common core: resolution and layout share ideas, not data.
2. **How the context is passed.**  (a) an explicit `Link *` parameter through
   about 300 functions; (b) a `__thread Link *` set by `ld_main` and by each
   `pfor` task.  (b) is a small diff and keeps the hot helpers' signatures;
   (a) is what removes the hidden state.  Recommendation: (a), done by script
   in the same window as the split, with `vbench.py gate` deciding.
3. **Do links need to run in parallel in vccd at all?**  Measure the wait on
   `ldlock` and on the objcopy lock in a kernel build (scheduling review).  If
   it is zero, Q-ld-01 is about the reset list and the split, not about speed.
4. **wasmld: product or test tool.**  As a product it needs dirfd, an error
   stream, a context and Q-ld-05; as the W8 tool it needs a line saying so.
5. **Where things live.**  Proposal: `ld/elf/`, `ld/macho/`, `ld/wasm/`,
   `ld/common/`, CPU parts in `arch/<cpu>/ld/` as real files, and objcopy, nm
   and ar together (`binutils/`), ar keeping its fd interface.
6. **OOM inside vccd**: abort (today, four of five tools) or fail the request
   (ar).  TODO.md already asks; the linkers need the answer before Q-ld-09.
7. **The linker's arena**: its own block list with generations (ld.c:168-237)
   or `include/common/arena.h`; TODO.md lists it under "variants not yet merged".

## 7. Not covered

- Nothing was built or run except `bench/quality.py --clones` and grep; the
  bugs Q-ld-02, 04, 05, 10 are from reading and are marked inferred.
- No measurement: lock waits, the cost of the repeated walks, the cost of
  `binop`'s string compares.
- The other side of cross-area duplicates was seen only at the lines the scan
  named: `platform/wasm/wasm.c`, `arch/wasm/as/as.c`, `include/obj/obj.h`,
  `include/arch/a64/reloc.h`, `include/common/buf.h`, and `server/vccd.c` only at the three call
  sites (1130-1210).  Whether `platform/linux/elf.c` has a string-table
  builder like objcopy's (objcopy.c:192-355) was not checked.
- Harness coverage (no Mach-O, ar or x86-64 corrupt-input cases) is from grep
  of `bench/vlink-bad.py`, not from running it.
- lld-exactness of any pass was taken as given; only structure was reviewed.
