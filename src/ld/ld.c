/*
 * vld: ELF linker for the arm64 kernel build (PLAN-fas2 step 2.8).
 *
 * Scope is what the kernel link uses, surveyed from work/build-o0:
 *   - inputs: ELF64 AArch64 relocatable objects, thin and regular archives
 *     (always --whole-archive in the kernel), no COMDAT groups, no COMMON,
 *     no TLS, no debug info;
 *   - relocations: ABS64/ABS32/PREL64/PREL32, CALL26/JUMP26,
 *     ADR_PREL_PG_HI21, ADD_ABS_LO12_NC, LDST{8,16,32,64,128}_ABS_LO12_NC,
 *     LD_PREL_LO19 (the 12 types present, plus the obvious neighbours);
 *   - outputs: the final vmlinux (-shared -Bsymbolic -z notext
 *     --no-apply-dynamic-relocs with the kernel's linker script, so absolute
 *     relocations become R_AARCH64_RELATIVE in .rela.dyn), the vDSO (-shared
 *     with PHDRS, VERSION and lld's .dynsym/.dynstr/.hash/.gnu.hash/
 *     .gnu.version{,_d}/.dynamic, no dynamic relocations) and ld -r.
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
 * over ~20k sections (cheap); copying and relocation run in parallel per
 * input section into one anonymous buffer that is written with one write().
 * No per-symbol malloc: everything comes from bump arenas.
 */
#include "ld/ld.h"
#include "arch/x64/reloc.h"
#include "common/buffer.h"
#include "common/scoped-cleanup.h"
#include "common/checked-arithmetic.h"
#include "common/vector.h"
#include "common/digest.h"
#include "port/performance-counters.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#include <time.h>
#include <unistd.h>

#define LD_NONE 0xffffffffu

#include "ld-internal.h"

#include "link-context.c"


#include "word-access.c"

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

#include "../arch/a64/ld/thunk-model.inc"

#include "section-placement.c"

#include "section-gc.c"

#include "section-assignment.c"

#include "string-merge.c"

#include "../arch/a64/ld/relocation-scan.inc"

#include "../arch/a64/ld/thunk-creation.inc"

#include "file-layout.c"

#include "symbol-table-locals.c"

/* ---------------- writing sections ---------------- */
/* ---------------- writing sections ---------------- */

#include "../arch/a64/ld/relocation-apply.inc"
#include "../arch/x64/ld/relocation.inc"

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
