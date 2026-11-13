/*
 * The ELF linker's per-link state: struct ld_context and the types its
 * fields need.
 *
 * Everything that belongs to one link moves here, one group of the
 * inventory (doc/quality/link-context.md) per change; what has not moved
 * yet is still a file-scope variable in the part that defines it.  The
 * context is zeroed at the start of each link (ld_context_init in
 * link-context.c), so a field needs no reset of its own: a field whose
 * start value is not zero is set there.
 *
 * Until every function takes the context as a parameter, ld.c defines the
 * one context of the process and a constant file-scope pointer `ctx` to
 * it; code reads fields as `ctx->group.field` whether `ctx` is that
 * pointer or a parameter that shadows it, so passing the context down is a
 * change of signatures only.  One link runs at a time (ldlock) until then.
 *
 * Process-wide state (the thread pool, the arena's block list, errlock,
 * keep_maps) and per-thread state (the arena's cursor, the symbol-table
 * scratch) are not part of the context.
 */
#ifndef VCC_LD_INTERNAL_H
#define VCC_LD_INTERNAL_H

#include "ldelf.h"
#include "common/vector.h"
#include "port/performance-counters.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* the first and last output section of a program header; hdr: FILEHDR/PHDRS */
typedef struct { int first, last; bool lma_set, hdr; } PhdrSecs;

/* an entry of the input-section-name cache: name -> its ISD or NONE */
typedef struct { const char *name; uint32_t h; uint32_t isd; } NameEnt;

/* error output, the error count and limit, and the VLD_TIME=1 phase timer */
struct ld_diagnostics {
	FILE *error_stream;		/* ld_main's stream; written under errlock */
	atomic_int error_count;		/* errors reported, also past the limit */
	int error_limit;		/* --error-limit; 0: none */
	bool last_was_multiline;	/* lld: a blank line after a multi-line diagnostic */
	bool phase_timing;		/* VLD_TIME is set */
	double phase_start_time;	/* seconds, CLOCK_MONOTONIC, at the last phase */
	vperf phase_start_counters;	/* the main thread's counters at the last phase */
	long phase_start_faults;	/* the process's minor faults at the last phase */
};

/* output section addresses, file offsets and program headers */
struct ld_layout {
	/* the address-assignment pass (lld's ExprValue state) */
	uint64_t dot;			/* the script's location counter */
	uint32_t current_output_section; /* AETHER or an output section */
	bool in_layout;			/* undefined symbols in expressions are errors */
	uint64_t tbss_offset;		/* lld: .tbss takes no address space; consecutive ones stack from here */

	/* matching input section names to the script */
	NameEnt *name_cache;		/* open addressing, grown at half full; in the arena */
	uint32_t name_cache_mask, name_cache_count;
	VVEC(uint32_t) orphans;		/* input sections no script rule placed */

	/* program headers and file offsets */
	VVEC(Phdr) program_headers;
	VVEC(PhdrSecs) program_header_sections;	/* parallel to program_headers */
	VVEC(uint32_t) output_sections;	/* live output sections in header order */
	VVEC(uint32_t) eh_frame_fdes;	/* the FDE pieces in the output, for .eh_frame_hdr */
	uint64_t file_header_address;	/* PHDRS FILEHDR: address of the ELF header */
	bool headers_loaded;		/* the ELF headers would be in the first PT_LOAD */
	uint64_t section_header_offset;
	uint64_t file_size;

	/* TLS: arm64 variant 1, tp = the PT_TLS start - alignup(16, p_align);
	   x86-64 variant 2, tp at the block's end; tprel = va - tls_thread_pointer */
	bool has_tls;			/* the output has a PT_TLS */
	uint64_t tls_start;		/* x86: the PT_TLS start (dtpoff) */
	uint64_t tls_thread_pointer;
};

struct ld_context {
	struct ld_diagnostics diagnostics;
	struct ld_layout layout;
};

#endif
