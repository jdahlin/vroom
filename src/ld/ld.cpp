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
 * (platform/darwin/macho-linker.cpp).
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

#include "link-context.cpp"
#include "diagnostics.cpp"
#include "phase-timing.cpp"
#include "region.cpp"
#include "thread-pool.cpp"
#include "global-symbols.cpp"
#include "address-values.cpp"
#include "expression-eval.cpp"
#include "script-lexer.cpp"
#include "script-expression-parser.cpp"
#include "script-statements.cpp"
#include "glob-match.cpp"
#include "input-mapping.cpp"
#include "dso.cpp"
#include "object-validate.cpp"
#include "input-sections.cpp"
#include "symbol-resolution.cpp"
#include "relocation-target.cpp"
#include "output-state.cpp"
#include "eh-frame.cpp"
#include "../arch/a64/ld/thunk-model.cpp"
#include "section-placement.cpp"
#include "section-gc.cpp"
#include "section-assignment.cpp"
#include "string-merge.cpp"
#include "../arch/a64/ld/relocation-scan.cpp"
#include "../arch/a64/ld/thunk-creation.cpp"
#include "file-layout.cpp"
#include "symbol-table-locals.cpp"
#include "../arch/a64/ld/relocation-apply.cpp"
#include "dynamic-executable.cpp"
#include "../arch/x64/ld/relocation.cpp"
#include "section-write.cpp"
#include "eh-frame-header.cpp"
#include "stub-object.cpp"
#include "option-parse.cpp"
#include "lazy-archive.cpp"
#include "input-load.cpp"
#include "output-file-write.cpp"
#include "script-address-assignment.cpp"
#include "output-section-adjust.cpp"
#include "program-headers.cpp"
#include "symbol-table-write.cpp"
#include "output-headers.cpp"
#include "dynamic-sections.cpp"
#include "script-program-headers.cpp"
#include "gap-fill.cpp"
#include "got-probe.cpp"
#include "common-symbols.cpp"
#include "synthetic-sections.cpp"
#include "script-symbols.cpp"
#include "stub-collection.cpp"
#include "global-order.cpp"
#include "undefined-report.cpp"
#include "symbol-demote.cpp"
#include "link-run.cpp"
#include "relocatable-link.cpp"
#include "macho-probe.cpp"
#include "response-files.cpp"
#include "link-entry.cpp"
#include "vlink-main.cpp"
