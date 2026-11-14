# Work order: the ELF linker (`src/ld/ld.c`)

The linker's pass of `PLAN-quality.md`, in steps; evidence in
`doc/review/quality-linkers.md` (finding ids `Q-ld-NN`).  Names follow
`doc/STYLE.md`.

**Owns** `src/ld/ld.c` and the new files beside it, the linker's fragments
`src/arch/*/ld/*.inc`, and the linker's source list in `src/Makefile`.  The
wasm linker (`ld/wasmld.c`), objcopy/nm, the Mach-O linker and `ar` belong to
the later steps (the shared layer, the one linker core) and are not touched by
the first ones.

## What done looks like (whole order)

- One job per file; every file opens with a comment; names by STYLE.md.
- Link state lives in one `ld_context` passed everywhere, not in file-scope
  variables; a link can run twice or concurrently in one process.
- One linker core with output formats (ELF, wasm, Mach-O), sharing the
  symbol table, file reader, archive reader, diagnostics and option parser
  with objcopy/nm/ar through the shared layer.

## Steps

Each step is one PR with identical output as its proof.

### 1. Split by job, moves only (this PR)

`ld.c` (6722 lines, 30 jobs by the review's map, in an order that is not the
pipeline's) is cut into 58 files, in the order of the old file, text
unchanged.  The linker stays **one translation unit**: `ld.c` keeps the
includes, the first two statics and the `#include` of every part, in the
original order, as the tree's other unity modules do.  So file-scope statics,
the anonymous `VVEC` types, the `static inline` hot helpers and the position
dependence of the `.inc` fragments all keep working, and the object code is
unchanged (the proof compares the objects).

What could not be cut, and stays as it is:

- **The part files do not compile alone.**  They use statics and types
  defined in the files before them.  `VCC_UNITY=` therefore changes nothing
  for the linker.  Step 2 removes this.  (Done: see step 2's status.)
- **The arch fragments stay in `arch/*/ld/*.inc`, included from `ld.c` at
  their old positions** (`thunks.inc` after `eh-frame.c`; `scan.inc` and
  `thunkpass.inc` after `string-merge.c`; `relocate.inc`, `got_seen` and
  `x64.inc` after `symbol-table-locals.c`): they define types and statics
  that later files use, and declare functions of earlier ones (`fits_int`,
  `pic`, `reladyn_buf` in the review).  (Done in step 2: they are
  `arch/*/ld/*.c`, parts like the others.)
- **`ld.c` is compiled twice** (`src/Makefile`: `ld/ld.o` with `-DVLD_MAIN`,
  `ld/ldlib.o` without) because `main()` is in `vlink-main.c`, under
  `VLD_MAIN`.  Unchanged here.  (Step 2 keeps the two unity objects; with
  `VCC_UNITY=` the parts are compiled once and `vlink-main.c` alone with
  `-DVLD_MAIN`.  `link-run.c`'s own `VLD_MAIN` test became the process-wide
  `ld_standalone`, set by `main()`.)
- Jobs the old file interleaves stay in file order and are named for the
  part they hold: the symbol table is `symbol-table-locals.c` and
  `symbol-table-write.c`; the PHDRS handling is `program-headers.c` and
  `script-program-headers.c`.  Merging them needs the state to be declared
  in a header first.
- The stale header comment of `ld.c` (no COMDAT, COMMON or TLS) is left, as
  are the section banners inside the moved text (including the doubled
  "writing sections" banner, now in `ld.c`, and the "final link" banner at
  the top of `got-probe.c`).  Step 2.
- The Makefile needs no change: `LD_SRC` still lists `ld/ld.c`, and the
  parts are found through the compiler's `-MMD` dependencies.

File map (line ranges are those of `ld.c` before this step):

| New file in `src/ld/` | Old lines | Job |
|---|---|---|
| `word-access.c` | 70-79 | Unaligned 32/64-bit loads and stores and the alignment helpers every other part of the linker uses. |
| `diagnostics.c` | 81-134 | Error and warning output, with the error count and limit, in lld's wording. |
| `phase-timing.c` | 136-166 | The VLD_TIME=1 phase timer: wall time, counters and page faults per phase. |
| `arena.c` | 168-237 | Per-thread bump arenas that every allocation of a link comes from, and the release of big mappings. |
| `thread-pool.c` | 239-313 | The worker threads and pfor(), the parallel loop the linker's passes run on. |
| `option-state.c` | 315-344 | The command-line options of one link (struct opt) and the directory descriptor paths are relative to. |
| `input-model.c` | 346-422 | The linker's data model for inputs: objects, input sections, merge inputs and merge sets, all addressed by index. |
| `global-symbols.c` | 424-555 | The global symbol table: a lock-free open-addressing hash of names to G records, resolved by atomic minimum on a priority key. |
| `script-model.c` | 557-657 | The parsed linker script: expressions, input section descriptions, output sections, PHDRS, VERSION nodes and symbol assignments. |
| `address-values.c` | 659-783 | The address-assignment state and the functions that turn a section or symbol into an address (lld's ExprValue). |
| `expression-eval.c` | 785-917 | Evaluation of linker-script expressions with lld's ExprValue rules. |
| `script-lexer.c` | 919-1017 | The linker script's character scanner: skipping, tokens, names and patterns. |
| `script-expression-parser.c` | 1019-1249 | The linker script's expression parser (precedence climbing) and its primaries. |
| `script-statements.c` | 1251-1664 | The linker script's statements: assignments, PROVIDE, ASSERT, input and output section descriptions, SECTIONS, PHDRS, VERSION, and parse_script. |
| `glob-match.c` | 1666-1720 | Glob matching for input section patterns. |
| `input-mapping.c` | 1722-1822 | Mapping input files and reading archives into the list of pending members. |
| `object-validate.c` | 1824-1992 | Validation of a relocatable ELF input (every offset and size is checked before use) and its parsing into an Obj. |
| `input-sections.c` | 1994-2102 | Turning an object's section headers into input sections, and dropping .note.gnu.property. |
| `symbol-resolution.c` | 2104-2267 | Symbol resolution: COMDAT groups, each object's definitions and references into the global table, duplicate checks and symbol finalisation. |
| `relocation-target.c` | 2269-2409 | Where a relocation points: the target symbol or section, merge-aware, before and after final addresses. |
| `output-state.c` | 2411-2439 | The state of the output image: file buffer, program headers, the ids of the synthetic sections, and new_isec(). |
| `eh-frame.c` | 2441-2681 | Merging .eh_frame input into one output section as lld's EhFrameSection does: CIE/FDE splitting, deduplication and pieces. |
| `section-placement.c` | 2685-2865 | The header size, the input-section-name cache, committing sections to output sections, and lld's orphan section ranking. |
| `section-gc.c` | 2867-2973 | --gc-sections as lld's MarkLive. |
| `section-assignment.c` | 2975-3171 | assign_sections: matching input sections to the script's output sections, orphans, and the order within each. |
| `string-merge.c` | 3173-3348 | SHF_MERGE sections: splitting into pieces and deduplicating them in 32 parallel shards. |
| `file-layout.c` | 3354-3445 | Segment membership helpers and the assignment of file offsets to output sections. |
| `symbol-table-locals.c` | 3447-3523 | The first half of the symbol table: which locals are kept, counting them, and writing a symbol record. |
| `section-write.c` | 3532-3627 | Copying each input section into the output image and applying its relocations, in parallel. |
| `eh-frame-header.c` | 3629-3687 | Writing .eh_frame_hdr (lld's EhFrameHeader::write). |
| `stub-object.c` | 3689-3752 | The in-memory stub object that defines the symbols the -O0 kernel references only in dead branches. |
| `option-parse.c` | 3754-3895 | Command-line parsing (parse_args) and library search. |
| `lazy-archive.c` | 3897-4030 | Lazy archive members: which are extracted and in which order, as lld. |
| `input-load.c` | 4032-4134 | Loading all inputs, allocating their sections, and setting up and freeing the global table. |
| `output-file-write.c` | 4136-4165 | Writing the output file, and the parallel build-id hash chunk. |
| `script-address-assignment.c` | 4167-4330 | Script symbol assignments and the address-assignment passes over output sections. |
| `output-section-adjust.c` | 4332-4374 | lld's adjustOutputSections: which output sections survive. |
| `program-headers.c` | 4376-4538 | Program header creation (lld's createPhdrs) and finishing. |
| `symbol-table-write.c` | 4540-4740 | The second half of the symbol table: sizing, layout of globals and locals, and filling it in parallel. |
| `output-headers.c` | 4742-4848 | The section name table, the ELF and section headers, synthetic contents and the build id. |
| `dynamic-sections.c` | 4850-5163 | -shared output: symbol versions, .dynsym, .dynstr, hash tables, .dynamic. |
| `script-program-headers.c` | 5165-5210 | Segments declared by the script's PHDRS command. |
| `gap-fill.c` | 5212-5251 | The script's =fill patterns between and after input sections. |
| `got-probe.c` | 5253-5278 | Finding out whether any relocation needs a GOT slot. |
| `common-symbols.c` | 5280-5331 | lld's replaceCommonSymbols: a NOBITS section per common symbol. |
| `synthetic-sections.c` | 5333-5386 | Appending the synthetic input sections (.rela.dyn, .symtab, .strtab and the like) after all files. |
| `script-symbols.c` | 5388-5471 | Declaring the symbols a script defines or references (lld's addScriptReferencedSymbolsToSymTable and declareSymbols). |
| `stub-collection.c` | 5473-5524 | Collecting the undefined symbols the stub object is to define. |
| `global-order.c` | 5526-5542 | The order of global symbols: first occurrence in file order, then script insertions. |
| `undefined-report.c` | 5544-5703 | The undefined-symbol diagnostics: spelling suggestions, reference locations, the report. |
| `symbol-demote.c` | 5705-5750 | Demoting symbols to local, and the __start_X/__stop_X symbols. |
| `link-run.c` | 5752-6033 | run(): the passes of one ELF link, in order. |
| `relocatable-link.c` | 6035-6468 | ld -r: relocatable output. |
| `state-reset.c` | 6470-6540 | Resetting all per-link state so the linker can run again in one process. |
| `macho-probe.c` | 6542-6564 | The lock that serialises links and the test for a Mach-O first input. |
| `response-files.c` | 6566-6664 | @FILE response files as GNU ld and lld read them. |
| `link-entry.c` | 6666-6686 | ld_main: the entry point vccd and vlink call for one link. |
| `vlink-main.c` | 6688-6722 | main() of the standalone tools (VLD_MAIN): vlink, vobjcopy, vnm, wasm-ld by argv[0]. |

### 2. Link context and renames

Put the 150 file-scope variables of the review (`objs`, `isecs`, `mergeins`,
`merges`, `gsyms`, `hnames`, `hword`, `gnames`, `gorder`, `osecs`, `topcmds`,
`isds`, `opt`, `out`, `phdrs`, `outsecs`, `thunks`, the `*_isec` ids and the
rest) into `ld_context` in `ld-internal.h`, with named types for the
anonymous `VVEC`s; `reset_state` becomes the initialisation of the struct
(Q-ld-02, the missed `x86_ld`).  Prototypes replace the nine ad-hoc forward
declarations.  Each part file then compiles alone, and `ld.c` stops being a
unity driver.  Rename every file-scope function to `ld_noun_verb`, every
global or field to words, and the types to `ld_*` (scripted, the table
checked in).  Refresh the opening comments and the stale header of `ld.c`.
Proof: `VCC_LDCMP=1 bench/vcc-ctests.py`, the kernel link identical, and
`bench/vbench.py gate` within 1 %.

Status (2026-11-13): the rename half is applied (`rename-ld.tsv`, scoped
mode of `rename-table.py`): 521 whole-word rows (functions, types,
enumerators, macros, per-link and process-wide globals) in `src/ld/*.c`
without `wasmld.c`, `src/arch/*/ld/*.inc` and the Mach-O linker, plus the
file moves (`elf-definitions.h`, `region.c`, `macho-linker.c`, the five arch
fragments).  The 290 field rows followed with `rename-table.py --fields`
(front-end field uses; the rows name each record by its new type, since the
short names `Obj`, `G`, `OSec` were shared by ELF, Mach-O and objcopy records;
objcopy's options struct got the tag `objcopy_option_set` so that it has a
name): the four objects (`ld.o`, `ldlib.o`, `objcopy.o`, `macho-linker.o`) are
byte-identical.  Names the owner may revisit: `macho-linker.c` and `macho_link`,
`elf-definitions.h` with `elf_*` types, `region.c`.

The link context (the other half): the design is decided and its first step
is applied (`link-context.md`, "Design (decided)": the diagnostics and layout
groups are fields of `struct ld_context`, #183); step 2 of the context moves
the output side (relocation scan, `ld -r`, output, symbol table, dynamic
sections); step 3 the input side (script, symbols, inputs, options), which
deletes `ld_state_reset` and moves the data model into `ld-internal.h`.  No
per-link state is left at file scope; step 4 passes the context as a
parameter to every function that uses it and `ld_main` creates one per link
(`link-context.md` section 5).

The part files compile alone (step 2's last piece): `ld-internal.h` holds
everything they share, with the includes and `LD_NONE` that `ld.c` had, the
types and constants two parts use, the process-wide objects (`LD_SHARED`:
`ld_keep_mappings`, `ld_thread_count`, `ld_standalone`, the script lexer's
character table), a prototype for each of the 172 functions one part defines
and another calls (`VPRIV`, static in the unity build), and the small inline
helpers (`word-access.c` is gone; the others moved from their parts).  The
ad-hoc forward declarations are gone.  `ld_link_lock` moved to its only user,
`link-entry.c`.  The arch fragments are `arch/*/ld/*.c`.  `src/Makefile` reads
`ld_SRC` from `ld.c`'s include list.  Decision: the parts keep their order in
`ld.c` and the unity build stays the default (the inline helpers and the
hot static functions stay visible to the optimizer); `ldlib.o` is
byte-identical.  The opening comments of `ld.c` and `ld-internal.h` are
rewritten; step 2 is done.

### 3. The shared layer

Move what ELF, wasm, Mach-O, objcopy/nm and `ar` each wrote on their own
onto `common/` and `port/`: one file reader and mapping, one archive parser,
one diagnostics function through the diagnostics module, one ELF validator,
one option table per tool.  Arena and thread pool go to the shared library
and the scheduler.

One diagnostics function for the linker tools: `struct ld_diagnostics` and
`ld_diagnostic_report` (`ld/ld.h`, `ld/diagnostics.c`) print lld's
"PROGRAM: error: MESSAGE" lines for the ELF linker (its context's member),
the Mach-O linker and vobjcopy/vnm (a static instance each, set per run,
replacing their own stream, count and name variables).  Each keeps its
behaviour through the struct's fields: ELF announces the limit ("too many
errors emitted") and puts lld's blank line after a multi-line message;
Mach-O stops printing after 20 errors without a word; objcopy has no limit.
The VLD_TIME phase timer moved out of the diagnostics into
`struct ld_phase_timing`.  Not through the diagnostics module: that is
track I, held by the owner; `utils/ar.c` keeps `ar_error_report`, which
writes to vccd's request descriptor, until then.

Left in step 3 (2026-11-13), with the reason:
- One ELF validator: `ld_object_is_valid` (relocatable inputs, extended
  section numbering, a message per check) and `objcopy_elf_is_valid`
  (executables too, program headers, one message) check the same section
  header, string table and symbol table rules.  The shared part would be a
  checker in `platform/linux/` that returns which rule failed, so each tool
  keeps its messages; not started.
- One option table per tool: track I, held by the owner.
- Arena and thread pool to the shared library and the scheduler: the
  scheduler's track (one scheduler, PLAN-quality.md); the linker's
  `region.c` and `thread-pool.c` stay until it lands.

Status (2026-11-13): the file mappings of the ELF linker (`ld_file_map`) and
of objcopy and nm (`objcopy_elf_open`, `nm_inputs_collect`) go through
`port/file.h` (`vfile_map_fd`, now public: the callers open the file
themselves, because their messages tell an open failure from a mapping
failure, and an unchanged message is the rule).  Left, with the reason:
the Mach-O linker reads its inputs into its arena with 8 bytes of padding
(`vfile_read` gives 1); objcopy's `-I binary`, `--add-section` and
`--add-gnu-debuglink` reads print "short read", which `vfile_read` cannot
tell apart; `wasmld.c` is not built.

One archive reader, `common/archive-reader.c` (`varchive_*`): magic, member
headers, the `//` table, thin archives and BSD `#1/` names, decoded in place
and never read past the file.  It reports what it found (`header_valid`,
`data_fits`, a long name it cannot find), and the ELF linker
(`ld_archive_add`), nm (`nm_inputs_collect`) and ar (`ar_thin_archive_read`,
`ar_regular_archive_read`) keep their own messages and their own choice of
what is fatal.  The Mach-O linker reads no archives (its inputs are objects
only), so it has no reader to replace; `wasmld.c` keeps its own until wasm is
built again.  Behaviour that moves, all on inputs the old readers misread,
and each now as llvm-ar/llvm-nm/lld: BSD archives (`#1/` names, `__.SYMDEF`)
are read by vlink and vnm (they failed with "not an ELF64 file"); a short
name without a `/` loses its trailing spaces in nm and ar `t` (`a.o`, not `a.o`
and 13 spaces); a corrupt name field is classified by one rule
(`/ ` symbol table, `// ` names, `/DIGITS` long name, `#1/` BSD, anything
else a short name), so the three tools no longer disagree on it; and a thin
archive whose `//` member goes past the end of the file is "malformed"
instead of an over-read.  Valid GNU and thin archives give byte-identical
results.

### 4. One linker core

ELF, wasm and Mach-O become output formats of one core: one symbol table,
resolution and section assignment, with a format writer each.  The decision
of `PLAN-quality.md` ("one linker core, not three linkers") applies; the
step is designed when 2 and 3 are done.

Design (2026-11-13): `DESIGN-linker-core.md`.  The core is the entry, the
context, inputs and archives, and diagnostics; resolution, layout, the ELF
symbol table and the output writer stay per format, with the reasons and two
questions for the owner there.  PRs: a Mach-O comparison harness, Mach-O
state in a context, no lock for Mach-O links, Mach-O names on
`common/string-index.h`.

Status: PR 1 of `DESIGN-linker-core.md` (Mach-O state in a context) is
done: `struct macho_context` holds the Mach-O linker's 24 per-link
variables, `macho_link_locked` creates it and `macho_context_destroy` frees
it (`macho_state_reset` is gone); the name sort that read the table from a
qsort comparator sorts (name, id) pairs.  Left at file scope in
`macho-linker.c`: the libSystem export table (process-wide, read once).

PR 2 (one entry), done differently from the design: with the state in its
context, a Mach-O link needs no lock at all, so instead of taking the ELF
link lock for both formats (which would serialise host-program links with
ELF links), Mach-O's own lock is gone and Mach-O links run concurrently;
the libSystem table is read once under a lock of its own
(`macho_libsystem_load`).  `ld_main` stays the one entry; the ELF lock is
unchanged.

PR 3 is done: the Mach-O linker's two name tables (globals, libSystem
exports) are `common/string-index.h` tables; its private FNV hash and the
hand-written probing and growth are gone.  Ids stay in insertion order, so
the output is the same.

## Decisions (coordinator, on the owner's behalf, 2026-11-13)

- The `vld: error: out of memory` message when the context allocation fails
  (#224) is accepted.
- This chain changes no output bytes.  In particular `vlink` does not take
  lld 23's TLS local-exec `add` to `nop` relaxation (the two LDCMP cases
  below): that is a separate question for the owner.
- The step-3 remainders that would change a message or the allocator (the
  Mach-O linker's inputs read into its arena with 8 bytes of padding;
  objcopy's "short read" paths) stay as they are, listed in step 3 with the
  reason.

## Known problem: two LDCMP cases (cause found, decision open)

`VCC_LDCMP=1 bench/vcc-ctests.py --no-cache` on a macOS host fails two cases
on `origin/main` (rechecked at 7749c528, 2026-11-13): `vcc/data_sections` and
`vcc/tls_static`, both "ref-fail: link: vlink image differs from lld" (clang's
objects, linked by lld and by vlink).  The cause: the host's lld (Homebrew LLD
23.1.4) relaxes a TLS local-exec `add xN, xN, #:tprel_hi12:sym, lsl #12` to a
`nop` when the high 12 bits of the offset are zero; `vlink` writes the `add`
with 0.  The two images differ in exactly those instructions (one in
`tls_static`, two in `data_sections`); the program headers and the section
layout are identical.  Making `vlink` do the same is a change of output bytes
(and depends on which lld is the reference: the kernel build's lld may be
older), so it is a question for the owner, not done here.  Until then a
context PR's proof is "no new failure".

## Proof for step 1

`make -C src -j 3 vcc` and the file-by-file build (`VCC_UNITY=
OUT=../build/src-parts`); the objects of `ld.c` (`ld.o`, `ldlib.o`)
byte for byte against a build of `origin/main`; `bench/vlink-opts.py`;
`bench/vlink-bad.py`; `VCC_LDCMP=1 bench/vcc-ctests.py -j 3 --no-cache`.

## Report

Per step: the PR, the proof commands with their results, anything this order
got wrong.
