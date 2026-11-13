/*
 * vld: the ELF linker (vlink, and ld_main inside vccd), as one translation
 * unit: this file includes its parts in order (VCC_UNITY).  ld-internal.h
 * is what they share; with `make VCC_UNITY=` each part compiles alone.
 * ld/ld.o (with VLD_MAIN: main() of vlink, vobjcopy and vnm) and
 * ld/ldlib.o (for vccd) are both built from this file.
 *
 * It links ELF64 relocatable objects for arm64 and x86-64, from files and
 * archives (thin and regular), into executables, shared objects (-shared)
 * and relocatable objects (ld -r), with GNU linker scripts; the kernel
 * build's links (vmlinux, the vDSO, ld -r of built-in.a and modules) are
 * the reference.  A first input in Mach-O goes to the Mach-O linker
 * (platform/darwin/macho-linker.c).
 *
 * The output is meant to be byte-identical to ld.lld 23 for the same
 * command line, so lld's observable decisions are reproduced: linker-script
 * expression semantics (ExprValue), the order of passes (the first thunk
 * pass sees symbol values from processSymbolAssignments, which is what
 * makes lld emit ADRP thunks for calls into the -O0 stub symbols), the
 * ADRP+ADD -> NOP+ADR relaxation, SHF_MERGE deduplication in 32 XXH3
 * shards, symbol table order, segment formation and the BLAKE3 build id.
 *
 * Performance: inputs are mmap'ed and parsed in parallel; symbols are
 * resolved in parallel into a lock-free open-addressing table (atomic min
 * on a priority key makes the result independent of thread timing); merge
 * pieces are deduplicated per shard in parallel; layout is a serial walk
 * over the sections; copying and relocation run in parallel per input
 * section into one buffer that is written with one write().  No per-symbol
 * malloc: everything comes from bump regions (region.c).
 */
#define VCC_UNITY 1

#include "ld-internal.h"

#include "link-context.c"
#include "diagnostics.c"
#include "phase-timing.c"
#include "region.c"
#include "thread-pool.c"
#include "global-symbols.c"
#include "address-values.c"
#include "expression-eval.c"
#include "script-lexer.c"
#include "script-expression-parser.c"
#include "script-statements.c"
#include "glob-match.c"
#include "input-mapping.c"
#include "object-validate.c"
#include "input-sections.c"
#include "symbol-resolution.c"
#include "relocation-target.c"
#include "output-state.c"
#include "eh-frame.c"
#include "../arch/a64/ld/thunk-model.c"
#include "section-placement.c"
#include "section-gc.c"
#include "section-assignment.c"
#include "string-merge.c"
#include "../arch/a64/ld/relocation-scan.c"
#include "../arch/a64/ld/thunk-creation.c"
#include "file-layout.c"
#include "symbol-table-locals.c"
#include "../arch/a64/ld/relocation-apply.c"
#include "../arch/x64/ld/relocation.c"
#include "section-write.c"
#include "eh-frame-header.c"
#include "stub-object.c"
#include "option-parse.c"
#include "lazy-archive.c"
#include "input-load.c"
#include "output-file-write.c"
#include "script-address-assignment.c"
#include "output-section-adjust.c"
#include "program-headers.c"
#include "symbol-table-write.c"
#include "output-headers.c"
#include "dynamic-sections.c"
#include "script-program-headers.c"
#include "gap-fill.c"
#include "got-probe.c"
#include "common-symbols.c"
#include "synthetic-sections.c"
#include "script-symbols.c"
#include "stub-collection.c"
#include "global-order.c"
#include "undefined-report.c"
#include "symbol-demote.c"
#include "link-run.c"
#include "relocatable-link.c"
#include "macho-probe.c"
#include "response-files.c"
#include "link-entry.c"
#include "vlink-main.c"
