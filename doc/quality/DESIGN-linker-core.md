# Design: one linker core (WORK-linker step 4)

`PLAN-quality.md` decided "one linker core with output formats, not three
linkers".  This is what that core is after steps 1-3, the order of the PRs
that make it, the proof of each, and what stays per format.

## Where we start

- **ELF** (`src/ld/`, 50 part files, one unity TU): lld's semantics for the
  kernel build.  All per-link state is in `struct ld_context`
  (`ld-internal.h`), created per link by `ld_main`; regions, a thread pool, a
  lock-free global symbol table, linker scripts, COMDAT, `ld -r`, `-shared`.
- **Mach-O** (`platform/darwin/macho-linker.c`, 1470 lines, its own TU): ld64's
  layout for the kernel's host programs and the self build.  About 25
  file-scope variables reset per link (`macho_state_reset`), its own lock,
  name table (FNV, growable), argv loop, calloc/realloc, single thread.
  `ld_main` hands it a link whose first input is Mach-O.
- **wasm** (`ld/wasmld.c`): not built since 2026-11-13 and not maintained.
- **Shared already** (step 3): file mapping (`port/file.h`, #231), the
  archive reader (`common/archive-reader.c`, #236), the diagnostics function
  (`struct ld_diagnostics`, `ld/diagnostics.c`, #238), response files
  (`ld_main` expands them before choosing the format).
- **No Mach-O harness.**  Nothing in `bench/` or CI links Mach-O with vld (the
  self build links with the host cc), which is how a layout bug that crashed
  vld on half of the small test programs went unseen (#237).  The proof of
  every Mach-O PR below is a comparison script (80 programs of `src/test/vcc`
  compiled with `vpp -c`, linked by base and new vld, byte-compared and run,
  plus error cases); checking it in as `bench/vlink-macho.py` (macOS only) is
  the first PR.

## The core

What every link does, whatever it writes:

| Part | Owner after step 4 |
|---|---|
| Entry: response files, the one link lock, the context's creation and end, the regions' release | `link-entry.c` (`ld_main`) for both formats |
| The context: options common to both (inputs, `-o`, `-L`/`-l`, directory), diagnostics, the format's own state as a member | `struct ld_context`, with `struct macho_context *macho` beside the ELF groups |
| Diagnostics | `struct ld_diagnostics` (done in step 3) |
| Input files and archives | `port/file.h`, `common/archive-reader.c` (done) |
| Name tables of a single-threaded format | `common/string-index.h` |

Per format, and staying so:

| Part | Why it is not shared |
|---|---|
| Symbol resolution | ELF is lld's rules (priority key by atomic minimum, COMDAT, common, weak, versions, `--defsym`, scripts); Mach-O is first definition wins with libSystem imports.  One function with two rule sets would be two functions behind a switch. |
| The ELF global symbol table | Lock-free and filled by all threads of the kernel link from regions sized for it; it is the linker's hot path (`vbench ld`).  Mach-O's links have a few hundred names and one thread; they take the common table instead. |
| Section assignment and layout | ELF follows the kernel's linker script and lld's orphan ranking; Mach-O has ld64's fixed segments.  No common algorithm, only a common word. |
| Relocations, GOT/stubs, dynamic sections, chained fixups, signature | Format and CPU specific by nature. |
| Writing the output file | ELF unlinks and writes in place (a reader may have the old file mapped); Mach-O writes a temporary and renames it (a running binary must not change).  Both behaviours and their messages are user-visible; merging them is a decision, not a refactor. |
| Mach-O's input reading | Its inputs are read into memory with padding, not mapped (step 3 left it, decision of 2026-11-13). |
| wasm | Not built.  The core must not block it: a wasm link would be a third member of the context, entered from `ld_main` the same way. |

## PRs, in order

Each builds and passes alone, and none changes an output byte.

0. **`bench/vlink-macho.py`**: the comparison above, against a base
   directory (`--base`), as `vlink-pool.py` does for ELF.
1. **Mach-O state in a context.**  Its file-scope per-link variables go into
   `struct macho_context`, created per link and passed as a parameter;
   `macho_state_reset` goes.  The process-wide libSystem cache stays
   process-wide (read once, under the link lock).
   Its two qsort comparators that read the name table sort (name, id)
   pairs instead (names are unique, so the order is the same).
   Proof: `vlink-macho.py` byte-identical, also with the ASan build
   (lifetimes move); the ELF proof (`vlink-pool`, LDCMP) unchanged.
2. **One entry.**  `ld_main` takes the link lock and creates the context for
   both formats; Mach-O's own lock goes; `macho_link` receives the context and
   reports through `ctx->diagnostics` (limit 20, not announced: unchanged
   messages).  Proof: as 1, plus `vccd-test`, and TSan with concurrent
   Mach-O and ELF links through one vccd, since the lock changes.
3. **Mach-O's names through `common/string-index.h`.**  The FNV table and
   its growth go; ids stay in insertion order, so every output is the same.
   Proof: as 1, and `vbench` is not concerned (ELF untouched).

Not in this list, and why: moving Mach-O onto the ELF option parser (the two
accept different options and ignore different ones; a shared parser changes
which command lines are accepted, a decision for the owner), and one output
writer (above).

## Questions for the owner

- The plan's step 4 said "one symbol table, resolution and section assignment
  with a format writer each".  This design shares the entry, context,
  inputs, archives and diagnostics, and keeps resolution, layout and the ELF
  symbol table per format for the reasons above.  If one resolution is still
  wanted, it needs Mach-O to follow lld's rules (a behaviour change for host
  programs) or ELF to grow a second rule set.
- One output writer: in place (ELF) or temporary and rename (Mach-O)?
