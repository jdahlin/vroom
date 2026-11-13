# The linker's state: inventory and the proposed link context

Input for step 2 of `WORK-linker.md` (the `ld_context` that replaces the
file-scope state).  Document only; nothing in `src/` changed.  Taken from the
tree after the split (PR #145, `src/ld/*.c` and `src/arch/*/ld/*.inc`).

Method: the file-scope variables were listed from clang's AST of `ld.c`
(every `VarDecl` defined in a part or fragment, plus the three function-level
mutable statics found by grep); readers and writers by word search per part,
a use counting as a write when it assigns, increments, pushes (`VVEC_PUSH`),
takes the address, or passes it to `memset`, `free`, `unmap` or an atomic
store.  Those two columns are a heuristic (an alias or a pointer passed on is
not followed), good for sizing, not for proof.  Reset status was read from
`state-reset.c` and the places named in the table.  The function counts in
section 3 come from a regex over function definitions (227 found) and a
transitive closure over calls by name; they are within a few percent.

## Design (decided)

Taken with step 1 of the context (branch `quality/ld-context-1`); the
inventory below is the input, section 5 is the work left.

1. **One `struct ld_context` with one sub-struct per group** (`struct
   ld_diagnostics diagnostics`, `struct ld_layout layout`, ...), declared in
   `src/ld/ld-internal.h` with the types its fields need.  A field is read as
   `ctx->group.field`.  Fields are renamed to words as they move (STYLE.md);
   the types keep their names here (the `ld_*` type renames are
   `doc/quality/rename-ld.tsv`'s, a separate scripted step).
2. **Route: the final spelling first, the parameter later.**  Until every
   function takes the context, `ld.c` defines the process's one context and
   a constant pointer to it:
   `static struct ld_context ld_link_context; static struct ld_context *const ctx = &ld_link_context;`.
   Clang folds the constant pointer, so `ctx->layout.dot` compiles to the
   same direct access as the old `dot`: no extra load in the hot loops
   (relocation application, resolution, section writes on the pool).
   Passing the context down later is a change of signatures only: a
   parameter named `ctx` shadows the file-scope one and no body changes.
   So the work is two mechanical series, each step provable alone:
   (a) one group per PR into the struct (below); (b) once all groups are in,
   one part file per PR gets `struct ld_context *ctx` parameters (`pfor`
   callbacks get it through their `void *arg`), and when no function uses
   the file-scope `ctx` any more, `ld_main` allocates a zeroed context per
   link and the file-scope instance and pointer are deleted.
3. **What replaces `ld_state_reset()`**: `ld_context_init()`
   (`link-context.c`), called by `ld_main` under `ld_link_lock` before the link:
   `memset` of the whole context, then the few fields whose start is not
   zero (`error_limit` 20, `phase_timing` from `VLD_TIME`, the error stream;
   later the `*_isec = LD_NONE` ids).  A moved field needs no reset line and
   cannot miss one (`prev_multi`, a function static, was the case in point:
   it now resets per link).  Each group PR deletes that group's lines from
   `ld_state_reset()`; the PR that moves the last group deletes
   `state-reset.c`.  A field that owns `malloc` memory (only `lsyms.v`
   today) is freed by `ld_context_init` before the `memset`, or moved to
   the arena by its group's PR.
4. **Not in the context**: process-wide state stays file-scope where it is
   (`thread-pool.c`: pool, job slot, locks, `ld_thread_count`; `region.c`: block
   list, generation, `ld_keep_mappings`; `ld_diagnostics_lock`, which guards every link's
   stream and cannot be zero-initialised portably; `ld_link_lock`); per-thread
   state stays `__thread` (`ld_region_current`, `ld_region_end`, `ld_region_thread_generation`, the symbol-table
   scratch).  `argv0` is a constant (`"vld"`), not state.  Before two links
   may run at once (dropping `ld_link_lock`), the arena's block list must become
   per-context (`ld_regions_free` unmaps every thread's blocks) and `pfor` must
   take more than one job: that is the scheduler plan's and step 3's
   ("The shared layer"), not this series'.
5. **Proof for every group PR**: the static's deletion makes the compiler
   find every use (a missed one does not compile); linker output byte for
   byte against the merge-base's `vlink` (`bench/vlink-pool.py
   base=DIR new=build/src` on `work/build-o0`: the vmlinux link with its
   script, `ld -r` of `vmlinux.a`, small `ld -r`; it also gives the time),
   `VCC_LDCMP=1 bench/vcc-ctests.py -j 4 --no-cache`, `bench/vlink-bad.py`,
   `bench/vlink-opts.py`, `bench/vccd-test.py`, `bench/vcc-stress.py
   --vccd`, and the rules' builds and `bench/vcc-ctests.py`.
6. **How a group is moved** (what step 1 did): add the group's struct to
   `ld-internal.h` (with the types it needs, moved unchanged from the part
   that defined them) and a member to `struct ld_context`; delete the
   statics; rewrite every use with a whole-word substitution that skips
   member accesses and designated initialisers (`(?<![.>\w"-])\bNAME\b(?!\s*\()`
   in Perl, comment-only lines left alone); then check by hand: struct
   members with the same name (`SPhdr.phdrs`), `#include "...NAME..."`
   lines, comments that name the old variable, and `-Wshadow` locals (only
   `out` in `eh-frame.c` and `response-files.c` today); delete the group's
   lines from `ld_state_reset()` and put any non-zero start value in
   `ld_context_init`.

Names: the inventory below was taken before the linker's rename (#198,
`rename-ld.tsv`), so its function, type and process-wide names are the old
ones (`reset_state` is `ld_state_reset`, `run` is `ld_link_run`,
`free_arenas` is `ld_regions_free`, `ldlock` is `ld_link_lock`, `errlock` is
`ld_diagnostics_lock`, `arena.c` is `region.c`, the arch fragments are
`thunk-model.inc`, `thunk-creation.inc`, `relocation-scan.inc`,
`relocation-apply.inc` and `x64/relocation.inc`; the types are `ld_*` and
`elf_*`).  The per-link variables kept their names for this series.

Correction to the inventory below: `tls_start` is reset by `ld_state_reset()`
since PR #137 (`tls_tp = tls_start = tbss_off = 0`); the table's "NO" was
written before it.  The real survivor was `prev_multi`.

## Totals (ELF linker, `ld.c` and its parts)

| Class | Variables | Meaning |
|---|---|---|
| per-link | 141 | belongs to one link; goes into the context |
| process-wide | 13 | the thread pool (`pool`, `pool_started`, `plock`, `pcv`, `pdone`, `job`, `nthreads`), the arena block list and generation (`blocks`, `blocklock`, `arena_gen`), `keep_maps`, `errlock`, `ldlock` |
| per-thread (`__thread`) | 5 | the arena's `acur`, `aend`, `my_gen` and the symbol table's scratch (`scratch`, `scap`) |
| constant table | 1 | `ctab`, the script lexer's character classes |
| **all** | **160** | |

The review's "about 150" is this 160 less the five thread-local ones and
`ctab`, plus a few counted twice.

## 1. Every variable

Columns: "Written in" and "Only read in" are parts (file names without `.c`;
`a64/…inc` and `x64/…inc` are the arch fragments).  "Reset" is whether
`reset_state()` returns it to its initial value; when it does not, the place
that sets it before every use is named.

| Variable | Type | Defined in | Written in | Only read in | Reset by `reset_state()` | Class | Group |
|---|---|---|---|---|---|---|---|
| `x86_ld` | bool | ld | object-validate, state-reset | file-layout, gap-fill, got-probe, input-load, input-sections, link-run, output-headers, relocatable-link, section-write, stub-object, synthetic-sections, undefined-report | yes | per-link | inputs |
| `tls_start` | uint64_t | ld | file-layout | x64/x64.inc | **NO**: set only when a PT_TLS exists | per-link | layout |
| `errf` | FILE * | diagnostics | link-entry | diagnostics, link-run, phase-timing | no; set before use by ld_main | per-link | diagnostics |
| `nerrors` | atomic_int | diagnostics | diagnostics, link-run, relocatable-link, state-reset | - | yes | per-link | diagnostics |
| `error_limit` | int | diagnostics | option-parse, state-reset | diagnostics | yes | per-link | diagnostics |
| `errlock` | pthread_mutex_t | diagnostics | - | diagnostics | n/a | process-wide | diagnostics |
| `argv0` | const char * | diagnostics | - | diagnostics | n/a (never assigned) | per-link | diagnostics |
| `timing` | bool | phase-timing | state-reset | link-run, phase-timing | yes | per-link | diagnostics |
| `t_last` | double | phase-timing | link-run, phase-timing | - | no; set before use by run() | per-link | diagnostics |
| `p_last` | vperf | phase-timing | link-run, phase-timing | - | no; set before use by run() | per-link | diagnostics |
| `flt_last` | long | phase-timing | link-run, phase-timing | - | no; set before use by run() | per-link | diagnostics |
| `blocks` | Block * | arena | arena | - | n/a | process-wide | process |
| `blocklock` | pthread_mutex_t | arena | - | arena | n/a | process-wide | process |
| `acur` | uint8_t * | arena | arena | - | n/a | per-thread | process |
| `aend` | uint8_t * | arena | arena | - | n/a | per-thread | process |
| `arena_gen` | atomic unsigned int | arena | arena | - | n/a | process-wide | process |
| `my_gen` | unsigned int | arena | arena | - | n/a | per-thread | process |
| `keep_maps` | bool | arena | vlink-main | arena | n/a | process-wide | process |
| `nthreads` | int | thread-pool | link-run, option-parse | thread-pool | n/a | process-wide | process |
| `pool_started` | int | thread-pool | thread-pool | - | n/a | process-wide | process |
| `pool` | pthread_t[64] | thread-pool | thread-pool | - | n/a | process-wide | process |
| `plock` | pthread_mutex_t | thread-pool | thread-pool | - | n/a | process-wide | process |
| `pcv` | pthread_cond_t | thread-pool | thread-pool | - | n/a | process-wide | process |
| `pdone` | pthread_cond_t | thread-pool | thread-pool | - | n/a | process-wide | process |
| `job` | anonymous struct | thread-pool | thread-pool | - | n/a | process-wide | process |
| `opt` | anonymous struct | option-state | input-load, option-parse, state-reset | a64/relocate.inc, a64/scan.inc, dynamic-sections, eh-frame, file-layout, got-probe, input-sections, link-run, option-state, output-file-write, output-headers, program-headers, script-expression-parser, script-program-headers, section-assignment, section-gc, section-placement, section-write, stub-collection, symbol-resolution, symbol-table-locals, synthetic-sections, undefined-report, x64/x64.inc | yes | per-link | options |
| `dirfd_base` | int | option-state | link-entry | input-mapping, option-parse, output-file-write, stub-collection | no; set before use by ld_main | per-link | options |
| `objs` | VVEC(Obj *) | input-model | input-load, link-run, relocatable-link, state-reset | a64/relocate.inc, a64/scan.inc, a64/thunkpass.inc, common-symbols, dynamic-sections, eh-frame, global-order, input-sections, object-validate, script-symbols, section-assignment, section-gc, section-write, stub-collection, symbol-resolution, symbol-table-locals, symbol-table-write, undefined-report, x64/x64.inc | yes | per-link | inputs |
| `isecs` | VVEC(ISec) | input-model | a64/relocate.inc, a64/scan.inc, a64/thunkpass.inc, a64/thunks.inc, address-values, common-symbols, dynamic-sections, eh-frame, gap-fill, got-probe, input-load, input-sections, link-run, output-headers, output-state, relocatable-link, relocation-target, script-address-assignment, section-assignment, section-gc, section-placement, section-write, state-reset, string-merge, symbol-resolution, symbol-table-locals, symbol-table-write, synthetic-sections, x64/x64.inc | eh-frame-header, symbol-demote | yes | per-link | inputs |
| `mergeins` | VVEC(MergeIn) | input-model | address-values, section-assignment, section-write, state-reset, string-merge | - | yes | per-link | inputs |
| `merges` | VVEC(Merge) | input-model | section-assignment, section-write, state-reset, string-merge | - | yes | per-link | inputs |
| `hnames` | _Atomic(const char *) * | global-symbols | global-symbols, input-load | - | no; set before use by gtable_init | per-link | symbols |
| `hword` | _Atomic(uint64_t) * | global-symbols | global-symbols, input-load | - | no; set before use by gtable_init | per-link | symbols |
| `gnames` | const char ** | global-symbols | global-symbols, input-load | - | no; set before use by gtable_init | per-link | symbols |
| `gsyms` | G * | global-symbols | a64/scan.inc, a64/thunkpass.inc, address-values, common-symbols, dynamic-sections, eh-frame, expression-eval, global-order, input-load, link-run, relocatable-link, relocation-target, script-address-assignment, script-symbols, section-gc, stub-collection, symbol-demote, symbol-resolution, symbol-table-write, undefined-report, x64/x64.inc | a64/relocate.inc, output-headers, section-write | no; set before use by gtable_init | per-link | symbols |
| `ngsyms` | atomic uint32_t | global-symbols | common-symbols, global-symbols, input-load, link-run, relocation-target, section-gc | - | no; set before use by gtable_init | per-link | symbols |
| `hmask` | uint32_t | global-symbols | input-load | global-symbols | no; set before use by gtable_init | per-link | symbols |
| `gorder` | VVEC(uint32_t) | global-symbols | dynamic-sections, global-order, state-reset | link-run, relocatable-link, symbol-demote, symbol-resolution, symbol-table-write, undefined-report | yes | per-link | symbols |
| `isds` | VVEC(ISD) | script-model | a64/thunkpass.inc, eh-frame, gap-fill, link-run, script-address-assignment, script-statements, section-assignment, section-placement, state-reset | output-section-adjust, section-gc | yes | per-link | script |
| `osecs` | VVEC(OSec) | script-model | a64/thunkpass.inc, dynamic-sections, file-layout, gap-fill, link-run, output-headers, output-section-adjust, program-headers, script-address-assignment, script-program-headers, script-statements, script-symbols, section-assignment, section-placement, section-write, state-reset | address-values, eh-frame-header, expression-eval, relocation-target, symbol-demote, symbol-table-locals, symbol-table-write | yes | per-link | script |
| `topcmds` | VVEC(Cmd) | script-model | script-address-assignment, script-statements, script-symbols, section-assignment, section-placement, state-reset | a64/thunkpass.inc, gap-fill, link-run, output-section-adjust, symbol-demote | yes | per-link | script |
| `entry_name` | const char * | script-model | script-statements, state-reset | link-run, output-headers | yes | per-link | script |
| `sphdrs` | VVEC(SPhdr) | script-model | script-program-headers, script-statements, state-reset | section-placement | yes | per-link | script |
| `have_phdrs` | bool | script-model | script-statements, state-reset | link-run, section-placement | yes | per-link | script |
| `vernodes` | VVEC(VerNode) | script-model | dynamic-sections, script-statements, state-reset | - | yes | per-link | script |
| `ver_locals` | VVEC(const char *) | script-model | script-statements, state-reset | dynamic-sections | yes | per-link | script |
| `nverdefs` | uint32_t | script-model | script-statements, state-reset | dynamic-sections, synthetic-sections | yes | per-link | script |
| `refsyms` | VVEC(uint32_t) | script-model | script-expression-parser, state-reset | script-statements, script-symbols | yes | per-link | script |
| `provides` | VVEC(Provide) | script-model | script-statements, script-symbols, state-reset | - | yes | per-link | script |
| `dot` | uint64_t | address-values | link-run, script-address-assignment | expression-eval | no; set before use by each address pass | per-link | layout |
| `cur_osec` | uint32_t | address-values | script-address-assignment | expression-eval | no; set before use by each address pass | per-link | layout |
| `in_layout` | bool | address-values | link-run | address-values, expression-eval | no; set before use by run() | per-link | layout |
| `assert_fail` | VVEC(const char *) | expression-eval | expression-eval, script-address-assignment, state-reset | link-run | yes | per-link | script |
| `sp` | const char * | script-lexer | script-expression-parser, script-lexer, script-statements | - | no; set before use by parse_script | per-link | script |
| `sp_end` | const char * | script-lexer | script-statements | script-expression-parser, script-lexer | no; set before use by parse_script | per-link | script |
| `sp_file` | const char * | script-lexer | script-statements | script-lexer | no; set before use by parse_script | per-link | script |
| `ctab` | const uint8_t[256] | script-lexer | - | script-lexer | n/a | constant table | const |
| `cur_provide` | Provide * | script-expression-parser | script-expression-parser, script-statements, state-reset | - | yes | per-link | script |
| `pending` | PendVec | input-mapping | input-load, input-mapping, state-reset | - | yes | per-link | inputs |
| `obj_quiet` | bool | object-validate | lazy-archive | object-validate | no; set before use by the lazy pass (restores it) | per-link | inputs |
| `isec_fva` | uint64_t * | relocation-target | relocation-target | - | no; set before use by final_addresses | per-link | relocation scan |
| `out` | uint8_t * | output-state | eh-frame, link-run, relocatable-link, response-files, script-expression-parser, state-reset | dynamic-sections, eh-frame-header, gap-fill, option-state, output-file-write, output-headers, section-write, symbol-table-write | yes | per-link | output |
| `filesize` | uint64_t | output-state | file-layout, relocatable-link | link-run, output-file-write, output-headers | no; set before use by file layout | per-link | layout |
| `shoff` | uint64_t | output-state | file-layout | output-headers | no; set before use by file layout | per-link | layout |
| `phdrs` | VVEC(Phdr) | output-state | file-layout, program-headers, script-program-headers, state-reset | output-headers, script-model, section-placement | yes | per-link | layout |
| `phsecs` | VVEC(PhdrSecs) | output-state | file-layout, program-headers, script-program-headers, state-reset | - | yes | per-link | layout |
| `outsecs` | VVEC(uint32_t) | output-state | link-run, state-reset | file-layout, output-headers, program-headers, script-program-headers | yes | per-link | layout |
| `ndyn` | uint64_t | output-state | link-run, state-reset | dynamic-sections, input-model | yes | per-link | output |
| `reladyn_isec` | uint32_t | output-state | state-reset, synthetic-sections | link-run | yes | per-link | output |
| `buildid_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | output-headers | yes | per-link | output |
| `comment_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | - | yes | per-link | output |
| `got_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | a64/relocate.inc, link-run, program-headers, x64/x64.inc | yes | per-link | output |
| `ehhdr_isec` | uint32_t | output-state | state-reset, synthetic-sections | eh-frame-header, program-headers, section-assignment | yes | per-link | output |
| `ehfdes` | VVEC(uint32_t) | output-state | eh-frame, state-reset | eh-frame-header, section-assignment | yes | per-link | layout |
| `gotsyms` | VVEC(uint32_t) | output-state | link-run, state-reset | section-write | yes | per-link | output |
| `gotlocs` | VVEC(GotLoc) | output-state | link-run, state-reset | section-write | yes | per-link | output |
| `symtab_isec` | uint32_t | output-state | state-reset, synthetic-sections | link-run, symbol-table-write | yes | per-link | output |
| `strtab_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | link-run, symbol-table-write | yes | per-link | output |
| `shstrtab_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | output-headers | yes | per-link | output |
| `dynsym_isec` | uint32_t | output-state | state-reset, synthetic-sections | dynamic-sections | yes | per-link | output |
| `dynstr_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | dynamic-sections | yes | per-link | output |
| `hash_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | dynamic-sections | yes | per-link | output |
| `gnuhash_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | dynamic-sections | yes | per-link | output |
| `versym_isec` | uint32_t | output-state | state-reset, synthetic-sections | dynamic-sections | yes | per-link | output |
| `verdef_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | dynamic-sections | yes | per-link | output |
| `dynamic_isec` | uint32_t | output-state | output-state, state-reset, synthetic-sections | dynamic-sections, program-headers | yes | per-link | output |
| `dyn_live` | bool | output-state | dynamic-sections, state-reset | program-headers | yes | per-link | output |
| `nobj_real` | uint32_t | output-state | link-run | common-symbols | no; set before use by run() | per-link | inputs |
| `thunks` | VVEC(Thunk) | a64/thunks.inc | a64/thunkpass.inc, a64/thunks.inc, state-reset, symbol-table-write | section-write | yes | per-link | relocation scan |
| `thunksecs` | VVEC(ThunkSec) | a64/thunks.inc | a64/thunkpass.inc, section-write, state-reset | - | yes | per-link | relocation scan |
| `tr_keys` | uint64_t * | a64/thunks.inc | a64/thunks.inc, state-reset | - | yes | per-link | relocation scan |
| `tr_vals` | uint32_t * | a64/thunks.inc | a64/thunks.inc, state-reset | - | yes | per-link | relocation scan |
| `tr_mask` | uint32_t | a64/thunks.inc | a64/thunks.inc, state-reset | - | yes | per-link | relocation scan |
| `tr_n` | uint32_t | a64/thunks.inc | a64/thunks.inc, state-reset | - | yes | per-link | relocation scan |
| `ncache` | NameEnt * | section-placement | section-placement, state-reset | - | yes | per-link | layout |
| `ncmask` | uint32_t | section-placement | section-placement, state-reset | - | yes | per-link | layout |
| `ncount` | uint32_t | section-placement | section-placement, state-reset | - | yes | per-link | layout |
| `orphans` | VVEC(uint32_t) | section-placement | section-assignment, state-reset | - | yes | per-link | layout |
| `pic` | bool | a64/scan.inc | link-run | a64/relocate.inc, a64/scan.inc, section-write, synthetic-sections, x64/x64.inc | no; set before use by run() | per-link | relocation scan |
| `tls_tp` | uint64_t | file-layout | file-layout, state-reset | a64/relocate.inc, x64/x64.inc | yes | per-link | layout |
| `has_tls` | bool | file-layout | file-layout, state-reset | a64/relocate.inc | yes | per-link | layout |
| `hdr_va` | uint64_t | file-layout | script-program-headers, state-reset | file-layout | yes | per-link | layout |
| `reladyn_buf` | Rela * | a64/relocate.inc | link-run, state-reset | a64/relocate.inc, section-write | yes | per-link | relocation scan |
| `got_seen` | atomic bool | ld | got-probe, x64/x64.inc | - | no; set before use by the GOT probe | per-link | relocation scan |
| `wjobs` | VVEC(uint32_t) | section-write | link-run, relocatable-link, state-reset | section-write | yes | per-link | output |
| `lsyms` | anonymous struct | lazy-archive | lazy-archive, state-reset | - | yes | per-link | inputs |
| `lcand` | PendVec | lazy-archive | input-load, lazy-archive, state-reset | - | yes | per-link | inputs |
| `lout` | PendVec | lazy-archive | lazy-archive, state-reset | input-load | yes | per-link | inputs |
| `ltaken` | VVEC(uint8_t) | lazy-archive | input-load, lazy-archive, state-reset | - | yes | per-link | inputs |
| `recorded` | VVEC(const char *) | script-address-assignment | script-address-assignment, state-reset | link-run | yes | per-link | script |
| `tbss_off` | uint64_t | script-address-assignment | script-address-assignment, state-reset | - | yes | per-link | layout |
| `headers_loaded` | bool | program-headers | program-headers, state-reset | - | yes | per-link | layout |
| `gouts` | GOut * | symbol-table-write | state-reset, symbol-table-write | - | yes | per-link | output |
| `ngouts` | uint32_t | symbol-table-write | state-reset, symbol-table-write | - | yes | per-link | output |
| `symtab_buf` | uint8_t * | symbol-table-write | symbol-table-write | - | no; set before use by fill_symtab | per-link | output |
| `strtab_buf` | uint8_t * | symbol-table-write | symbol-table-write | - | no; set before use by fill_symtab | per-link | output |
| `thunk_sym0` | uint64_t | symbol-table-write | symbol-table-write | - | no; set before use by build_symtab | per-link | output |
| `thunk_str0` | uint64_t | symbol-table-write | symbol-table-write | - | no; set before use by build_symtab | per-link | output |
| `nsymout` | uint64_t | symbol-table-write | symbol-table-write | - | no; set before use by build_symtab | per-link | output |
| `strsize` | uint64_t | symbol-table-write | symbol-table-write | - | no; set before use by build_symtab | per-link | output |
| `nlocal_out` | uint32_t | symbol-table-write | symbol-table-write | link-run | no; set before use by build_symtab | per-link | output |
| `shstrtab_buf` | uint8_t * | output-headers | output-headers | - | no; set before use by build_shstrtab | per-link | output |
| `script_ord` | uint64_t | dynamic-sections | dynamic-sections, script-symbols | - | no; set before use by script_symbols | per-link | symbols |
| `dsyms` | VVEC(uint32_t) | dynamic-sections | dynamic-sections, state-reset | - | yes | per-link | output |
| `dyn_symndx` | uint32_t | dynamic-sections | dynamic-sections | - | no; set before use by build_dynamic | per-link | output |
| `gh_nbuckets` | uint32_t | dynamic-sections | dynamic-sections | - | no; set before use by build_dynamic | per-link | output |
| `gh_maskwords` | uint32_t | dynamic-sections | dynamic-sections | - | no; set before use by build_dynamic | per-link | output |
| `dsym_hash` | uint32_t * | dynamic-sections | dynamic-sections | - | no; set before use by build_dynamic | per-link | output |
| `dynstr` | VVEC(char) | dynamic-sections | dynamic-sections, state-reset | - | yes | per-link | output |
| `dynstr_offs` | VVEC(uint32_t) | dynamic-sections | dynamic-sections, state-reset | - | yes | per-link | output |
| `dsym_name` | uint32_t * | dynamic-sections | dynamic-sections | - | no; set before use by build_dynamic | per-link | output |
| `soname_off` | uint32_t | dynamic-sections | dynamic-sections | - | no; set before use by build_dynamic | per-link | output |
| `vername_off` | uint32_t * | dynamic-sections | dynamic-sections | - | no; set before use by build_dynamic | per-link | output |
| `ndynent` | uint32_t | dynamic-sections | dynamic-sections | - | no; set before use by build_dynamic | per-link | output |
| `script_new` | VVEC(uint32_t) | script-symbols | script-symbols, state-reset | global-order | yes | per-link | symbols |
| `stub_names` | const char ** | stub-collection | state-reset, stub-collection | link-run | yes | per-link | symbols |
| `nstubs` | uint32_t | stub-collection | state-reset, stub-collection | link-run | yes | per-link | symbols |
| `stop_syms` | VVEC(uint32_t) | symbol-demote | state-reset, symbol-demote | link-run | yes | per-link | symbols |
| `routs` | VVEC(ROut) | relocatable-link | relocatable-link, state-reset | - | yes | per-link | relocatable output |
| `rmap` | uint32_t * | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `rmask` | uint32_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `lmap` | uint32_t ** | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `rbuf` | uint8_t * | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_symoff` | uint64_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_stroff` | uint64_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_shstroff` | uint64_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_shstrsize` | uint64_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_nsyms` | uint64_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_nlocal` | uint64_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_symidx` | uint32_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_stridx` | uint32_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_shstridx` | uint32_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_nsh` | uint32_t | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_locstr` | uint64_t * | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `r_loc0` | uint64_t * | relocatable-link | relocatable-link | - | no; set before use by ld_relocatable | per-link | relocatable output |
| `ldlock` | pthread_mutex_t | macho-probe | - | link-entry | n/a | process-wide | process |
| `ld_machine` | _Atomic uint16_t | ld | object-validate, state-reset | - | yes | per-link | inputs |
| `prev_multi` | bool (static in `diag()`) | diagnostics | diagnostics | - | no (function static, never reset) | per-link | diagnostics |
| `scratch` | uint32_t (*)[2] (static in `count_locals()`) | symbol-table-locals | symbol-table-locals | - | no (per-thread scratch, only grows) | per-thread | output |
| `scap` | uint32_t (static in `count_locals()`) | symbol-table-locals | symbol-table-locals | - | no (per-thread scratch, only grows) | per-thread | output |

### What `reset_state()` misses

Of the 141 per-link variables, 81 are reset by `reset_state()` and 60 are
not.  Of those 60, 57 are assigned before their first read in every link (the
"set before use" entries: `run()`, `gtable_init`, the file layout, the
symbol-table builder, `build_dynamic`, `ld_relocatable`, the script parser
and so on), so a missing reset is harmless today but only by the order of the
passes; `argv0` is never assigned.  The two that are not safe, and the
history:

- **`x86_ld`**: was missing (review Q-ld-02); PR #137 added it, together with
  `ld_machine`.  Both are reset now.
- **`tls_start`**: assigned only when the output has a `PT_TLS` segment
  (`file-layout.c`), never reset, and read by `x64.inc` for DTPOFF
  relocations.  After a link with TLS in the same process, a later link whose
  relocations ask for it without a `PT_TLS` would read the old address.  Only
  malformed input can do that, but the reset is one line.
- **`got_seen`**: reset by the probe that uses it (`got-probe.c`), and set by
  `x64.inc`'s own probe function; both paths store `false` first, so safe.
- **`prev_multi`** (function static in `diag()`): the "blank line after a
  multi-line diagnostic" flag survives from one link to the next, so the first
  diagnostic of a link can get a stray blank line after the last link's last
  multi-line one.  Cosmetic, and it lives outside any list a reset would
  walk.
- **`argv0`** is never assigned (always `"vld"`): a constant.
- **`isec_fva`, `symtab_buf`, `strtab_buf`, `shstrtab_buf`, `dsym_*`,
  `vername_off`, `rmap`, `lmap`, `rbuf`, `r_locstr`, `r_loc0`** point into the
  arena, which `free_arenas()` unmaps after each link; they are dangling
  between links and are re-assigned before use.  With a context struct this
  disappears: the struct is zeroed per link.

The hand-kept list is the problem, not any one entry: `reset_state()` is 75
lines for 81 variables, the file has 141, and nothing checks the two against
each other.  (So the two that miss a reset are `tls_start`, which is real, and
`prev_multi`, which is a function static the list does not reach.)

### Why `ldlock` exists

All 141 variables are file scope, so one link at a time may run in a process.
`ld_main` takes `ldlock` for the whole ELF link (`VLOCK(ldlock)` in
`link-entry.c`).  The other tools have their own locks for the same reason:
`ld_macho` has a function-static mutex of its own, and `vccd` takes a
mutex around every `nm_main`/`objcopy_main` call; none excludes another, which
is correct only because their states are disjoint.  Moving the ELF state into
a context removes the need for `ldlock`, but two things in the table are not
per-link and would then be shared between concurrent links, and must move
first:

1. **The arena** (`blocks`, `arena_gen`): `free_arenas()` unmaps every block of
   every thread, so a link that ends frees the memory of one that is running.
   The block list belongs in the context (or the arena becomes a per-link
   object); `acur`/`aend`/`my_gen` stay thread-local but must be keyed to the
   context's generation.
2. **The thread pool** (`job`, `pool_started`, `plock`, `pcv`, `pdone`): one job
   slot, so `pfor` is not reentrant between two links.  Either a pool per
   context or a queue of jobs; the scheduler plan (`PLAN-quality.md`) owns
   this and the context step only has to pass `ctx` to `pfor`'s callbacks
   (they already receive `void *arg`).

`keep_maps` (set once by `vlink-main.c`'s `main`) and `nthreads` (computed per
link in `run()` from the machine) become context fields or options.

## 2. The proposed context

One `ld_context` in `ld-internal.h`, allocated per link, zeroed at its start
(so `reset_state()` is deleted).  141 fields in nine groups; each group a
named struct so that a function that touches only one can take just it.

| Group | Fields | Contents | Names |
|---|---|---|---|
| options | 2 | command line of this link and the base directory | `opt`, `dirfd_base` |
| inputs | 13 | objects, input sections, merge sets, pending archive members, the lazy-archive pass, the machine of the inputs | `x86_ld`, `objs`, `isecs`, `mergeins`, `merges`, `pending`, `obj_quiet`, `nobj_real`, `lsyms`, `lcand`, `lout`, `ltaken`, `ld_machine` |
| symbols | 12 | global table, symbol order, symbols made by the script or the stub pass | `hnames`, `hword`, `gnames`, `gsyms`, `ngsyms`, `hmask`, `gorder`, `script_ord`, `script_new`, `stub_names`, `nstubs`, `stop_syms` |
| script | 17 | the parsed script and the parser's cursor | `isds`, `osecs`, `topcmds`, `entry_name`, `sphdrs`, `have_phdrs`, `vernodes`, `ver_locals`, `nverdefs`, `refsyms`, `provides`, `assert_fail`, `sp`, `sp_end`, `sp_file`, `cur_provide`, `recorded` |
| layout | 19 | name cache, orphans, address pass, program headers, TLS, file sizes | `tls_start`, `dot`, `cur_osec`, `in_layout`, `filesize`, `shoff`, `phdrs`, `phsecs`, `outsecs`, `ehfdes`, `ncache`, `ncmask`, `ncount`, `orphans`, `tls_tp`, `has_tls`, `hdr_va`, `tbss_off`, `headers_loaded` |
| relocation scan | 10 | thunks and their hash, GOT probe, .rela.dyn buffer, `pic`, final section addresses | `isec_fva`, `thunks`, `thunksecs`, `tr_keys`, `tr_vals`, `tr_mask`, `tr_n`, `pic`, `reladyn_buf`, `got_seen` |
| output | 42 | image, synthetic section ids, GOT slots, symbol table and dynamic section buffers | `out`, `ndyn`, `reladyn_isec`, `buildid_isec`, `comment_isec`, `got_isec`, `ehhdr_isec`, `gotsyms`, `gotlocs`, `symtab_isec`, `strtab_isec`, `shstrtab_isec`, `dynsym_isec`, `dynstr_isec`, `hash_isec`, `gnuhash_isec`, `versym_isec`, `verdef_isec`, `dynamic_isec`, `dyn_live`, `wjobs`, `gouts`, `ngouts`, `symtab_buf`, `strtab_buf`, `thunk_sym0`, `thunk_str0`, `nsymout`, `strsize`, `nlocal_out`, `shstrtab_buf`, `dsyms`, `dyn_symndx`, `gh_nbuckets`, `gh_maskwords`, `dsym_hash`, `dynstr`, `dynstr_offs`, `dsym_name`, `soname_off`, `vername_off`, `ndynent` |
| relocatable output | 17 | `ld -r` state | `routs`, `rmap`, `rmask`, `lmap`, `rbuf`, `r_symoff`, `r_stroff`, `r_shstroff`, `r_shstrsize`, `r_nsyms`, `r_nlocal`, `r_symidx`, `r_stridx`, `r_shstridx`, `r_nsh`, `r_locstr`, `r_loc0` |
| diagnostics | 9 | error stream, counters and limit, the phase timer | `errf`, `nerrors`, `error_limit`, `argv0`, `timing`, `t_last`, `p_last`, `flt_last`, `prev_multi` |

Decisions this implies:

- The anonymous `VVEC` types become named (`ld_object_vector` and so on) so
  that the struct can be declared in a header; `VVEC(T)` stays as the
  expansion.
- `opt` stays a struct of its own inside `options` and is `const` after
  `parse_args` (it is written in three parts today: `option-parse`,
  `input-load`, `state-reset`).
- Hot helpers that read the context at every call (`isec_va`, `gsym_va`,
  `reloc_target_final`, `strhash`, `kget`) take the context in a register;
  `gsyms`/`hmask`/`isec_fva` are the loads to watch, so the performance gate
  (`bench/vbench.py gate`, 1 %) is part of the step's proof.
- `errlock` moves into `diagnostics` (it guards that link's `errf`);
  `prev_multi` joins it.
- `ld -r` state (`relocatable output`) could be a separate struct that only
  `relocatable-link.c` sees, reached from the context by pointer.

## 3. How much changes

For each part: functions found, how many touch a per-link variable directly,
how many need the context once their callees are counted (a function that
only calls one that needs it must pass it on), and how many of those need
exactly one group versus two or more.  "Exactly one" can take the group
struct (`ld_symbols *`) instead of the whole context.

| Part | Functions | Touch state directly | Need the context (with callees) | Exactly one group | Two or more groups |
|---|---|---|---|---|---|
| address-values | 10 | 6 | 8 | 3 | 5 |
| arena | 5 | 1 | 2 | 2 | 0 |
| common-symbols | 1 | 1 | 1 | 0 | 1 |
| diagnostics | 3 | 2 | 3 | 3 | 0 |
| dynamic-sections | 10 | 8 | 8 | 1 | 7 |
| eh-frame-header | 2 | 1 | 1 | 0 | 1 |
| eh-frame | 7 | 7 | 7 | 4 | 3 |
| expression-eval | 9 | 5 | 8 | 1 | 7 |
| file-layout | 4 | 3 | 3 | 0 | 3 |
| gap-fill | 2 | 1 | 1 | 0 | 1 |
| glob-match | 2 | 0 | 0 | 0 | 0 |
| global-order | 1 | 1 | 1 | 0 | 1 |
| global-symbols | 7 | 2 | 2 | 2 | 0 |
| got-probe | 2 | 2 | 2 | 0 | 2 |
| input-load | 4 | 4 | 4 | 1 | 3 |
| input-mapping | 2 | 2 | 2 | 0 | 2 |
| input-sections | 3 | 2 | 2 | 0 | 2 |
| lazy-archive | 5 | 5 | 5 | 1 | 4 |
| link-entry | 1 | 1 | 1 | 0 | 1 |
| link-run | 1 | 1 | 1 | 0 | 1 |
| macho-probe | 1 | 0 | 0 | 0 | 0 |
| object-validate | 4 | 2 | 2 | 0 | 2 |
| option-parse | 3 | 2 | 3 | 1 | 2 |
| output-file-write | 2 | 2 | 2 | 0 | 2 |
| output-headers | 4 | 4 | 4 | 0 | 4 |
| output-section-adjust | 2 | 1 | 1 | 0 | 1 |
| output-state | 1 | 1 | 1 | 0 | 1 |
| phase-timing | 3 | 1 | 1 | 1 | 0 |
| program-headers | 3 | 3 | 3 | 0 | 3 |
| relocatable-link | 7 | 7 | 7 | 0 | 7 |
| relocation-target | 5 | 5 | 5 | 0 | 5 |
| response-files | 2 | 1 | 1 | 1 | 0 |
| script-address-assignment | 6 | 5 | 6 | 0 | 6 |
| script-expression-parser | 8 | 4 | 7 | 1 | 6 |
| script-lexer | 8 | 7 | 8 | 4 | 4 |
| script-program-headers | 1 | 1 | 1 | 0 | 1 |
| script-statements | 10 | 9 | 10 | 0 | 10 |
| script-symbols | 5 | 4 | 4 | 1 | 3 |
| section-assignment | 1 | 1 | 1 | 0 | 1 |
| section-gc | 1 | 1 | 1 | 0 | 1 |
| section-placement | 11 | 8 | 8 | 3 | 5 |
| section-write | 1 | 1 | 1 | 0 | 1 |
| state-reset | 1 | 1 | 1 | 0 | 1 |
| string-merge | 5 | 5 | 5 | 2 | 3 |
| stub-collection | 3 | 3 | 3 | 1 | 2 |
| stub-object | 1 | 1 | 1 | 0 | 1 |
| symbol-demote | 3 | 2 | 3 | 0 | 3 |
| symbol-resolution | 4 | 4 | 4 | 1 | 3 |
| symbol-table-locals | 4 | 3 | 3 | 0 | 3 |
| symbol-table-write | 7 | 7 | 7 | 2 | 5 |
| synthetic-sections | 1 | 1 | 1 | 0 | 1 |
| thread-pool | 3 | 0 | 0 | 0 | 0 |
| undefined-report | 5 | 3 | 4 | 0 | 4 |
| vlink-main | 1 | 0 | 1 | 0 | 1 |
| word-access | 1 | 0 | 0 | 0 | 0 |
| a64/relocate.inc | 4 | 1 | 2 | 1 | 1 |
| a64/scan.inc | 2 | 2 | 2 | 0 | 2 |
| a64/thunkpass.inc | 2 | 2 | 2 | 0 | 2 |
| a64/thunks.inc | 5 | 4 | 4 | 1 | 3 |
| x64/x64.inc | 5 | 4 | 5 | 0 | 5 |
| **total** | **227** | **168** | **187** | **38** | **149** |

So 187 of 227 functions (82 %) get a context parameter; 38 of them need only
one group and 149 need two or more, so a one-parameter `ctx` is the practical
signature and the group structs are mostly for the callbacks and the hot
helpers.  The 40 that need nothing are `glob-match`, `word-access`, the
thread pool's own functions and leaf helpers.  Parts with all their functions
in the "two or more" column (`script-statements`, `relocatable-link`,
`relocation-target`, `output-headers`, `program-headers`) are where the diff
will be thickest.  The 141 names occur 2 156 times as whole words in `src/ld/` (without
`objcopy.c` and `wasmld.c`) and the fragments, an upper bound on the sites
that change spelling to `ctx->group.field` (it counts locals that share a
name, such as `out`, `sp` and `dot`, and comments).

## 4. The other linkers, in short form

Counts from the AST (the review said 34, 29 and 4).

| File | Variables | State | Reset | Lock |
|---|---|---|---|---|
| `ld/wasmld.c` | 41 (with `VCC_WASM`; 34 counted by the review before the recent additions) | `nerr`, the global table (`ght`, `ghcap`, `ngsym`, `gall`, `ngall`, `cgall`), objects (`objs`, `nobjs`, `cobjs`), archives (`ars`, `nars`, `cars`), `opt`, the work list (`wl`, `nwl`, `cwl`, `ctors_live`), the type table (`otypes`, `notypes`, `cotypes`), memory layout (`stack_low`, `stack_high`, `data_end`, `heap_base`, `heap_end`, `mem_pages`, `max_pages`, `tls_base`, `tls_size`, `tls_align`, `sp_global`, `memw`), output segments (`osegs`, `nosegs`), slots (`nslots`, `lslots`, `nlslots`, `clslots`, `slot_func`, `cslot_func`) | none; runs once per process (`wasm_ld_main` is only called from `main`, not from vccd) | none |
| `platform/darwin/ldmacho.c` | 29 | `errf`, `nerr`; objects (`objs`, `nobjs`); output sections (`osecs`, `nosecs`, `cap_osecs`, `os_text`, `os_stubs`, `os_got`); globals (`gs`, `ngs`, `cap_gs`, `gh`, `ghmask`); the `.tbd` (`tbd`, `ex_hash`, `ex_mask`, `sdk_version`); fixups and imports (`fixups`, `nfix`, `cap_fix`, `imports`, `nimports`, `cap_imports`, `ngot`, `nstub`, `got_g`, `stub_g`) | `reset()` at the start of `link_macho` frees and zeroes them | function-static mutex in `ld_macho` |
| `ld/objcopy.c` | 4 | `oerr`, `oerrors`, `prog`, `oc` (the options of one objcopy run) | `objcopy_main` and `nm_main` assign them at entry (`memset(&oc, 0, ...)`) | the mutex in `vccd.c` around both calls |

All three have the same shape as the ELF linker (file-scope request state,
one run at a time, a lock somewhere), so the context pattern of step 2
applies to each; for the core of step 4 the three groups "symbols",
"inputs" and "diagnostics" are what they would share.

## 5. The work left: one PR per line

Step 1 (`quality/ld-context-1`) introduced the context and moved
**diagnostics** (`errf`, `nerrors`, `error_limit`, `prev_multi`, `timing`,
`t_last`, `p_last`, `flt_last` -> `error_stream`, `error_count`,
`error_limit`, `last_was_multiline`, `phase_timing`, `phase_start_time`,
`phase_start_counters`, `phase_start_faults`) and **layout** (`dot`,
`cur_osec`, `in_layout`, `tbss_off`, `ncache`, `ncmask`, `ncount`,
`orphans`, `phdrs`, `phsecs`, `outsecs`, `ehfdes`, `hdr_va`,
`headers_loaded`, `shoff`, `filesize`, `has_tls`, `tls_start`, `tls_tp` ->
`dot`, `current_output_section`, `in_layout`, `tbss_offset`, `name_cache`,
`name_cache_mask`, `name_cache_count`, `orphans`, `program_headers`,
`program_header_sections`, `output_sections`, `eh_frame_fdes`,
`file_header_address`, `headers_loaded`, `section_header_offset`,
`file_size`, `has_tls`, `tls_start`, `tls_thread_pointer`).  Each PR below
follows Design 6 and is proven by Design 5; the order puts self-contained
groups first and the hot ones (`symbols`, `inputs`) where the timing of
`vlink-pool.py` matters most.

Series (a), groups into the struct:

2. **relocation scan** (10): `isec_fva`, `thunks`, `thunksecs`, `tr_keys`,
   `tr_vals`, `tr_mask`, `tr_n`, `pic`, `reladyn_buf`, `got_seen`.  Types
   `Thunk`, `ThunkSec` move from `arch/a64/ld/thunk-model.inc` to the header;
   `isec_fva` is read in `ld_relocation_target_final` (hot: time it).
3. **relocatable output** (17): `routs`, `rmap`, `rmask`, `lmap`, `rbuf`,
   `r_symoff`, `r_stroff`, `r_shstroff`, `r_shstrsize`, `r_nsyms`,
   `r_nlocal`, `r_symidx`, `r_stridx`, `r_shstridx`, `r_nsh`, `r_locstr`,
   `r_loc0`; only `relocatable-link.c`.
4. **output, synthetic sections and GOT** (21): `out`, `ndyn`,
   `reladyn_isec`, `buildid_isec`, `comment_isec`, `got_isec`,
   `ehhdr_isec`, `symtab_isec`, `strtab_isec`, `shstrtab_isec`,
   `dynsym_isec`, `dynstr_isec`, `hash_isec`, `gnuhash_isec`,
   `versym_isec`, `verdef_isec`, `dynamic_isec`, `dyn_live`, `gotsyms`,
   `gotlocs`, `wjobs` (type `GotLoc`).  The `*_isec` ids start
   at `LD_NONE`: set them in `ld_context_init`.  `out` is also a local in
   `eh-frame.c` and `response-files.c`: leave those.
5. **output, symbol table** (10): `gouts`, `ngouts`, `symtab_buf`,
   `strtab_buf`, `thunk_sym0`, `thunk_str0`, `nsymout`, `strsize`,
   `nlocal_out`, `shstrtab_buf` (type `GOut`).
6. **output, dynamic sections** (12): `dsyms`, `dyn_symndx`,
   `gh_nbuckets`, `gh_maskwords`, `dsym_hash`, `dynstr`, `dynstr_offs`,
   `dsym_name`, `soname_off`, `vername_off`, `ndynent`, `script_ord`.
7. **script** (17): `isds`, `osecs`, `topcmds`, `entry_name`, `sphdrs`,
   `have_phdrs`, `vernodes`, `ver_locals`, `nverdefs`, `refsyms`,
   `provides`, `assert_fail`, `sp`, `sp_end`, `sp_file`, `cur_provide`,
   `recorded`.  The script model's types (`Expr`, `Cmd`, `ISD`, `OSec`,
   `SPhdr`, `VerNode`, `Provide`, ...) move to the header; `sp` is a common
   local name: check `-Wshadow`.
8. **symbols** (11): `hnames`, `hword`, `gnames`, `gsyms`, `ngsyms`,
   `hmask`, `gorder`, `script_new`, `stub_names`, `nstubs`, `stop_syms`
   (type `G`).  `gsyms`/`hmask` are in resolution's and relocation's inner
   loops: time it.
9. **inputs** (13): `x86_ld`, `ld_machine`, `objs`, `isecs`, `mergeins`,
   `merges`, `pending`, `obj_quiet`, `nobj_real`, `lsyms`, `lcand`, `lout`, `ltaken`
   (types `Obj`, `ISec`, `MergeIn`, `Merge`, `PendVec`, `LSym`).
   `lsyms.v` is `malloc`ed: free it in `ld_context_init` before the
   `memset` (or allocate it from the arena).
10. **options** (2): `opt` (the struct becomes `struct ld_options`),
    `dirfd_base`.  `ld_state_reset()` is then empty: delete `state-reset.c`
    and its call in `ld_link_run()`.

Series (b), the parameter (after 10; one or a few part files per PR, by the
call graph from the leaves up): add `struct ld_context *ctx` to every
function that uses it (section 3: 187 of 227); `pfor` callbacks take it from
`arg`.  Proof as above plus `bench/vbench.py gate` is not enough (it times
the compiler): the `vlink-pool.py` times must stay within noise.  Last PR:
`ld_main` allocates and frees a zeroed context per link, the file-scope
`ld_link_context` and `ctx` are deleted.  Dropping `ld_link_lock` waits for the
per-context arena and a reentrant `pfor` (Design 4).
