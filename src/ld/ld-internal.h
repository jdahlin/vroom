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
 * change of signatures only.  One link runs at a time (ld_link_lock) until then.
 *
 * Process-wide state (the thread pool, the region block list, ld_diagnostics_lock,
 * ld_keep_mappings) and per-thread state (the region cursor, the symbol-table
 * scratch) are not part of the context.
 */
#ifndef VCC_LD_INTERNAL_H
#define VCC_LD_INTERNAL_H

#include "elf-definitions.h"
#include "common/vector.h"
#include "port/performance-counters.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* the first and last output section of a program header; hdr: FILEHDR/PHDRS */
typedef struct { int first, last; bool lma_set, hdr; } ld_segment_sections;

/* an entry of the input-section-name cache: name -> its ld_input_section_description or LD_NONE */
typedef struct { const char *name; uint32_t h; uint32_t isd; } ld_name_cache_entry;

/* error output, the error count and limit, and the VLD_TIME=1 phase timer */
struct ld_diagnostics {
	FILE *error_stream;		/* ld_main's stream; written under ld_diagnostics_lock */
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
	uint32_t current_output_section; /* LD_OUTPUT_SECTION_AETHER or an output section */
	bool in_layout;			/* undefined symbols in expressions are errors */
	uint64_t tbss_offset;		/* lld: .tbss takes no address space; consecutive ones stack from here */

	/* matching input section names to the script */
	ld_name_cache_entry *name_cache;		/* open addressing, grown at half full; in the arena */
	uint32_t name_cache_mask, name_cache_count;
	VVEC(uint32_t) orphans;		/* input sections no script rule placed */

	/* program headers and file offsets */
	VVEC(elf_program_header) program_headers;
	VVEC(ld_segment_sections) program_header_sections;	/* parallel to program_headers */
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

/* an arm64 range-extension thunk (arch/a64/ld/thunk-creation.inc) */
typedef struct {
	uint32_t g;			/* destination global (or LD_NONE) */
	uint32_t sec;			/* destination ISec for local/section targets */
	uint64_t val;
	int64_t addend;
	uint32_t tsec;			/* thunk ISec */
	uint32_t off;			/* offset in thunk section */
	uint32_t size;			/* 4 (b) or 12 (adrp/add/br) */
	bool lng;			/* long form; sticky once chosen (lld) */
	const char *name;
} a64_ld_thunk;
typedef struct { VVEC(uint32_t) list; } a64_ld_thunk_section;

/* a .got slot of a local symbol: object and symbol index */
typedef struct { uint32_t obj, sym; } ld_got_location;

/* a global symbol's place in .symtab */
typedef struct { uint32_t g; uint32_t idx; uint64_t str; bool local; } ld_global_symbol_output;

/* an output section of ld -r, grouped by name */
typedef struct {
	const char *name;
	uint32_t h;
	uint32_t type, align, entsize;
	uint64_t flags, size, off;
	VVEC(uint32_t) secs;
	uint64_t nrela, relaoff;
	uint32_t shidx, relaidx, nameoff, relanameoff;
	uint32_t secsym;
	uint32_t grp;			/* a .group: its input SHT_GROUP ISec + 1 */
	bool member;			/* a group member: its own output section, SHF_GROUP */
} ld_relocatable_section;

/* the relocation scan, thunks and the final addresses relocations read */
struct ld_relocations {
	bool pic;			/* -shared or -pie: absolute relocations become RELATIVE */
	_Atomic bool got_seen;		/* some relocation needs a .got slot (the GOT probe) */
	uint64_t *final_section_addresses; /* ld_input_section_address(s, 0), or UINT64_MAX: take ld_relocation_target_find */
	elf_rela *rela_dyn_buffer;	/* .rela.dyn in the output image */

	/* arm64 range-extension thunks */
	VVEC(a64_ld_thunk) thunks;
	VVEC(a64_ld_thunk_section) thunk_sections;
	/* thunk redirection: (input section, relocation index) -> thunk; open hash in the arena */
	uint64_t *thunk_redirect_keys;
	uint32_t *thunk_redirect_values;
	uint32_t thunk_redirect_mask, thunk_redirect_count;
};

/* ld -r: the relocatable output (relocatable-link.c) */
struct ld_relocatable {
	VVEC(ld_relocatable_section) sections;
	uint32_t *section_map;		/* name hash table -> sections */
	uint32_t section_map_mask;
	uint32_t **local_symbol_map;	/* per object: local symbol -> output index */
	uint8_t *image;			/* the output file */
	uint64_t symtab_offset, strtab_offset, shstrtab_offset, shstrtab_size;
	uint64_t symbol_count, local_count;
	uint32_t symtab_index, strtab_index, shstrtab_index, section_header_count;
	uint64_t *local_string_offsets;	/* per object: string offset of its locals */
	uint64_t *local_first_index;	/* per object: symbol index of its first local */
};

/* the output image, the ids of the synthetic input sections and the .got */
struct ld_output {
	uint8_t *image;			/* the output file */
	uint8_t *shstrtab;		/* .shstrtab's contents */
	uint64_t relative_count;	/* RELATIVE relocations */
	VVEC(uint32_t) write_jobs;	/* input sections to write */

	/* synthetic input sections: LD_NONE until created (ld_context_init) */
	uint32_t rela_dyn_section, build_id_section, comment_section, got_section;
	uint32_t eh_frame_header_section;	/* --eh-frame-hdr */
	uint32_t symtab_section, strtab_section, shstrtab_section;
	/* -shared: dynamic sections (lld's, for the vDSO; vmlinux discards them) */
	uint32_t dynsym_section, dynstr_section, hash_section, gnu_hash_section;
	uint32_t versym_section, verdef_section, dynamic_section;
	bool dynamic_live;		/* .dynamic survived the script */

	VVEC(uint32_t) got_symbols;	/* .got slot -> ld_global_symbol id (the globals, first) */
	VVEC(ld_got_location) got_locals;	/* then the locals: slot got_symbols.n + i */
};

/* .symtab and .strtab (symbol-table-write.c) */
struct ld_symbol_table {
	ld_global_symbol_output *globals;
	uint32_t global_count;
	uint8_t *symtab, *strtab;	/* in the output image (ld_symbol_table_fill) */
	uint64_t first_thunk_symbol, first_thunk_string;
	uint64_t symbol_count, string_size;
	uint32_t local_count;
};

/* -shared: .dynsym, .dynstr, the hash tables and .dynamic (dynamic-sections.c) */
struct ld_dynamic {
	VVEC(uint32_t) symbols;		/* exported globals in .dynsym order (index i + 1) */
	uint32_t first_hashed_symbol;	/* first symbol in .gnu.hash */
	uint32_t gnu_hash_buckets, gnu_hash_mask_words;
	uint32_t *symbol_hashes;	/* GNU hash per symbols entry */
	VVEC(char) strings;		/* .dynstr */
	VVEC(uint32_t) string_offsets;	/* offsets of added strings (lld dedups) */
	uint32_t *symbol_names;		/* .dynstr offset per symbols entry */
	uint32_t soname_offset, *version_name_offsets;
	uint32_t entry_count;
};

struct ld_context {
	struct ld_diagnostics diagnostics;
	struct ld_layout layout;
	struct ld_relocations relocations;
	struct ld_relocatable relocatable;
	struct ld_output output;
	struct ld_symbol_table symbol_table;
	struct ld_dynamic dynamic;
};

#endif
