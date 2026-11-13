/*
 * What the parts of the ELF linker share: struct ld_context (one link's
 * state) and the types its fields need, the process-wide state, the
 * functions one part defines and another calls, and the small inline
 * helpers every part uses.
 *
 * ld.c includes every part after this header, as one translation unit
 * (VCC_UNITY, the default build); then VPRIV and LD_SHARED are static and
 * the linker's functions stay internal.  With `make VCC_UNITY=` each part
 * compiles alone, and they are external.
 *
 * One link's state is the context, created by ld_main for each link
 * (ld_context_create in link-context.c), all zero but the fields named
 * there, and passed to every function that uses it.  Process-wide state:
 * the region allocator (region.c), the thread pool's width and the
 * standalone flag (thread-pool.c), ld_diagnostics_lock (diagnostics.c),
 * ld_link_lock (link-entry.c) and ld_keep_mappings (region.c); per-thread: the region
 * cursor and the symbol-table scratch.  One ELF link runs at a time
 * (ld_link_lock).
 */
#ifndef VCC_LD_INTERNAL_H
#define VCC_LD_INTERNAL_H

/* a function or object one part defines and another uses: static in the
   one-TU build (VCC_UNITY), external when each part compiles alone */
#ifdef VCC_UNITY
#define VPRIV static
#define LD_SHARED static
#else
#define VPRIV
#define LD_SHARED extern
#endif

#include "ld/ld.h"
#include "arch/x64/reloc.h"
#include "common/buffer.h"
#include "common/scoped-cleanup.h"
#include "common/checked-arithmetic.h"
#include "common/vector.h"
#include "common/digest.h"
#include "port/file.h"
#include "port/performance-counters.h"
#include "port/task.h"
#include "elf-definitions.h"

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

/* ---- the command line (option-parse.c) ---- */
typedef struct {
	const char *path;
	bool whole;
} ld_input_argument;

struct ld_options {
	const char *output_path;
	const char *script;
	const char *soname;
	const char *entry;
	int orphan_handling;			/* --orphan-handling: 0 place, 1 warn, 2 error */		/* -e/--entry: over the script's ENTRY() */
	const char *comment;		/* .comment linker string */
	const char *build_id;		/* NULL, "sha1", "md5", "fast" */
	const char *map;
	bool relocatable, shared, bsymbolic, notext, no_apply_dynamic_relocations, no_undefined;
	bool discard_locals, discard_all, strip_debug, pic_veneers, nmagic;
	bool stubs;			/* define unresolved symbols as -O0 stubs */
	bool relax;
	uint64_t max_page_size;
	VVEC(ld_input_argument) inputs;
	VVEC(const char *) library_directories;
	bool static_libraries_only;
	bool garbage_collect_sections, print_garbage_collected_sections;		/* --gc-sections, --print-gc-sections */
	bool no_relro;			/* -z norelro: no PT_GNU_RELRO */
	bool eh_frame_header;		/* --eh-frame-hdr: .eh_frame_hdr and PT_GNU_EH_FRAME */
	int directory_fd;		/* relative paths are relative to it (ld_main's dirfd) */
};

/* ---- the inputs: objects, input sections, merge inputs and merge sets, all addressed by index ---- */
typedef struct ld_object {
	const char *path;		/* for diagnostics */
	const uint8_t *file_bytes;
	size_t size;
	const elf_section_header *section_headers;
	uint32_t section_header_count;
	const elf_symbol *symbols;
	uint32_t symbol_count, first_global_index;
	const char *string_table;		/* symbol string table */
	const char *section_name_table;
	uint32_t first_input_section;			/* ISec id of section 0 */
	uint32_t *global_slots;			/* global slot of sym[firstg + i] */
	_Atomic uint8_t *local_needs_got;		/* local k needs a .got slot (only when there is a .got) */
	uint32_t *local_got_index;		/* its slot */
	uint32_t index;
	/* symbol table output */
	uint32_t kept_local_count, first_local_output_index;		/* kept locals, first output index */
	uint32_t (*kept_locals)[2];		/* kept locals: symbol index, name length */
	uint64_t local_string_bytes;		/* strtab bytes of kept locals */
	uint64_t string_offset;
	bool mapped;
} ld_object;


enum { LD_SECTION_KIND_NONE, LD_SECTION_KIND_NORMAL, LD_SECTION_KIND_MERGE_INPUT, LD_SECTION_KIND_MERGE, LD_SECTION_KIND_THUNK, LD_SECTION_KIND_DYNAMIC_RELOCATIONS, LD_SECTION_KIND_BUILD_ID,
       LD_SECTION_KIND_SYMBOL_TABLE, LD_SECTION_KIND_STRING_TABLE, LD_SECTION_KIND_SECTION_NAME_TABLE, LD_SECTION_KIND_COMMENT, LD_SECTION_KIND_GOT, LD_SECTION_KIND_EH_FRAME_HEADER,
       LD_SECTION_KIND_DYNAMIC_SYMBOLS, LD_SECTION_KIND_DYNAMIC_STRINGS, LD_SECTION_KIND_HASH_TABLE, LD_SECTION_KIND_GNU_HASH_TABLE, LD_SECTION_KIND_SYMBOL_VERSIONS, LD_SECTION_KIND_VERSION_DEFINITIONS, LD_SECTION_KIND_DYNAMIC };

typedef struct ld_input_section {
	const uint8_t *data;
	const char *name;
	uint64_t size, flags, offset;
	uint32_t type, alignment, entry_size;
	uint32_t object;			/* owning object or LD_NONE */
	uint32_t section_header_index;
	uint32_t output_section;			/* output section, LD_NONE, or LD_OUTPUT_SECTION_DISCARD */
	uint32_t input_description;
	const elf_rela *rela;
	uint32_t rela_count;
	uint32_t auxiliary_index;			/* LD_SECTION_KIND_MERGE_INPUT: MergeIn idx; LD_SECTION_KIND_MERGE: Merge idx; LD_SECTION_KIND_THUNK: ThunkSec idx */
	uint32_t dynamic_relocation_count;			/* R_AARCH64_RELATIVE entries produced */
	uint32_t first_dynamic_relocation;
	uint8_t kind;
	bool thunked;			/* some relocation has a a64_ld_thunk_relocation_record entry */
	bool dropped;			/* member of a COMDAT group seen before */
	uint32_t next_group_member;			/* next member of its section group + 1, 0: none */
	uint32_t eh_frame_cie_piece;			/* .eh_frame FDE piece: its CIE piece + 1 (the CIE pointer is written) */
} ld_input_section;

#define LD_OUTPUT_SECTION_DISCARD 0xfffffffeu

/* SHF_MERGE input split into pieces */
typedef struct {
	uint32_t input_section;
	uint32_t synthetic_section;			/* merge synthetic ISec id */
	uint32_t piece_count;
	uint32_t *input_offsets;		/* piece input offsets */
	uint32_t *hash;			/* 31-bit hash */
	uint32_t *output_offsets;		/* offset in the synthetic section */
} ld_merge_input;

typedef struct {
	uint32_t synthetic_section;			/* the synthetic ISec */
	uint32_t output_section;
	uint64_t flags;
	uint32_t entry_size, alignment;
	VVEC(uint32_t) members;		/* MergeIn ids in order */
	uint64_t shard_offsets[32], shard_sizes[32];
	/* piece lists per shard for writing: (mergein, piece) of unique pieces */
	VVEC(uint64_t) unique_pieces[32];
} ld_merge_section;

/* an archive member or object waiting to be loaded (input-mapping.c) */
typedef struct { const char *path; const uint8_t *file_bytes; size_t size; } ld_pending_member;
typedef VVEC(ld_pending_member) ld_pending_member_vector;

/* the lazy-archive pass (lazy-archive.c) */
enum { LD_LAZY_SYMBOL_NONE, LD_LAZY_SYMBOL_UNDEFINED, LD_LAZY_SYMBOL_WEAK_REFERENCE, LD_LAZY_SYMBOL_DEFINED, LD_LAZY_SYMBOL_LAZY };
typedef struct { const char *name; uint32_t length, hash; uint8_t state; uint32_t candidate; } ld_lazy_symbol;
struct ld_lazy_symbol_table { ld_lazy_symbol *v; uint32_t mask, n; };

/* ---- symbols (global-symbols.c) ---- */
typedef struct {
	uint8_t kind;			/* LD_VALUE_KIND_ABSOLUTE / LD_VALUE_KIND_OUTPUT_SECTION / LD_VALUE_KIND_INPUT_SECTION */
	uint8_t force_absolute, type;
	uint32_t index;
	uint64_t value, alignment;
} ld_expression_value;
enum { LD_VALUE_KIND_ABSOLUTE, LD_VALUE_KIND_OUTPUT_SECTION, LD_VALUE_KIND_INPUT_SECTION };

enum { LD_SYMBOL_STATE_UNDEFINED, LD_SYMBOL_STATE_OBJECT, LD_SYMBOL_STATE_SCRIPT };

typedef struct ld_global_symbol {
	_Atomic uint64_t definition;		/* best definition: rank<<62 | obj<<32 | symidx (rank: 0 strong, 1 common, 2 weak) */
	_Atomic uint64_t first_occurrence;		/* first occurrence, for symbol table order */
	_Atomic uint8_t visibility_rank;	/* min visibility (rank, see ld_visibility_rank()) */
	uint8_t state, type, binding, visibility, hidden_by_script;
	uint8_t demoted, provide_is_live, defined_by_script, duplicate, common;
	_Atomic uint8_t strong_reference;	/* some non-weak undefined reference */
	uint32_t object;			/* defining object, LD_NONE for script */
	uint32_t symbol_index;
	uint32_t input_section;			/* LD_SYMBOL_STATE_OBJECT: ISec id or LD_NONE (absolute) */
	uint64_t value, size;
	ld_expression_value expression_value;				/* LD_SYMBOL_STATE_SCRIPT: section + offset */
	uint32_t order;			/* index among globals in insertion order */
	uint32_t output_index;		/* index in the output .symtab */
	uint32_t thunk;
	uint64_t final_address;			/* after layout: ld_global_symbol_address() */
	bool is_absolute, is_undefined;		/* ld_global_symbol_is_absolute(); undefined or demoted */
	_Atomic uint8_t needs_got;	/* some GOT relocation names it */
	uint32_t got_index;		/* its .got slot */
	uint16_t version;			/* VERSION index: 0 local, 1 global, 2.. named */
	uint32_t dynamic_index;		/* index in .dynsym, 0 if not exported */
} ld_global_symbol;

/* ---- the parsed linker script: expressions, input section descriptions, output sections, PHDRS, VERSION nodes and symbol assignments ---- */
typedef struct ld_expression ld_expression;
enum {
	LD_EXPRESSION_NUMBER, LD_EXPRESSION_SYMBOL, LD_EXPRESSION_DOT, LD_EXPRESSION_ADDRESS, LD_EXPRESSION_LOAD_ADDRESS, LD_EXPRESSION_SIZEOF, LD_EXPRESSION_ALIGNOF, LD_EXPRESSION_ALIGN_ONE_ARGUMENT, LD_EXPRESSION_ALIGN_TWO_ARGUMENTS,
	LD_EXPRESSION_ABSOLUTE, LD_EXPRESSION_BINARY, LD_EXPRESSION_NEGATE, LD_EXPRESSION_BITWISE_NOT, LD_EXPRESSION_LOGICAL_NOT, LD_EXPRESSION_TERNARY, LD_EXPRESSION_ASSERT, LD_EXPRESSION_DEFINED, LD_EXPRESSION_MAX, LD_EXPRESSION_MIN,
	LD_EXPRESSION_LOG2CEIL, LD_EXPRESSION_SIZEOF_HEADERS,
};
struct ld_expression {
	uint8_t operation;
	char binary_operator[4];
	ld_expression *first_operand, *second_operand, *third_operand;
	uint64_t number;
	uint32_t symbol;			/* LD_EXPRESSION_SYMBOL/LD_EXPRESSION_DEFINED: global slot; LD_EXPRESSION_ADDRESS etc: osec */
	const char *message;
};

typedef struct {
	const char **globs;		/* section name globs */
	uint32_t glob_count;
	uint8_t sort;			/* LD_SORT_NONE, LD_SORT_NAME, LD_SORT_ALIGNMENT */
} ld_pattern;
enum { LD_SORT_NONE, LD_SORT_NAME, LD_SORT_ALIGNMENT };

typedef struct ld_input_section_description {
	uint32_t output_section;
	ld_pattern *patterns;
	uint32_t pattern_count;
	bool keep;
	VVEC(uint32_t) sections;		/* ISec ids, in final order */
	uint32_t thunk_section;		/* ISec of this ld_input_section_description's thunk section, or LD_NONE */
	uint64_t thunk_offset;		/* pass-0 isecLimit */
	struct a64_ld_call *calls;		/* CALL26/JUMP26 relocations, built by thunk pass 0 */
	uint32_t call_count;
} ld_input_section_description;

enum { LD_COMMAND_ASSIGNMENT, LD_COMMAND_OUTPUT_SECTION, LD_COMMAND_INPUT_SECTION_DESCRIPTION };
typedef struct ld_assignment {
	uint32_t symbol;			/* LD_NONE for '.' */
	ld_expression *expression;
	char operator_character;			/* '=' or the operator of op= */
	bool provide, hidden;
	bool defined;			/* shouldDefineSym() */
	bool is_assert;
} ld_assignment;
typedef struct ld_command {
	uint8_t kind;
	uint32_t index;			/* LD_COMMAND_OUTPUT_SECTION: osec, LD_COMMAND_INPUT_SECTION_DESCRIPTION: isd */
	ld_assignment *assignment;
} ld_command;

typedef struct ld_output_section {
	const char *name;
	ld_expression *address_expression, *align_expression, *load_address_expression, *subalign_expression;
	VVEC(ld_command) commands;
	bool not_allocated, noload, discard, used_in_expression, has_input, live, type_set;
	bool uses_symbols;			/* expressionsUseSymbols */
	uint32_t type, address_alignment, entry_size;
	uint64_t flags;
	uint64_t address, size, offset;
	uint32_t section_header_index, name_offset;
	int load_segment;			/* index into the context's program_headers, -1 */
	uint32_t link, info;
	ld_expression *fill;			/* =fill */
	const char *program_header_names[4];		/* :phdr names */
	uint8_t program_header_name_count;
} ld_output_section;

/* PHDRS */
typedef struct {
	const char *name;
	uint32_t type;
	ld_expression *flags;			/* FLAGS(expr) or NULL */
	bool file_header, program_headers;
} ld_script_segment;

/* VERSION: named nodes get version indices 2.. in order (1 is the file) */
typedef struct {
	const char *name;		/* "" for an anonymous node */
	VVEC(const char *) globs;
} ld_version_node;

/* names referenced by expressions, in parse order (lld referencedSymbols) */
typedef struct { uint32_t symbol; VVEC(uint32_t) references; } ld_provide;

#define LD_OUTPUT_SECTION_AETHER 0xfffffffdu

/* the first and last output section of a program header; hdr: FILEHDR/PHDRS */
typedef struct { int first_output_section, last_output_section; bool load_address_set, includes_headers; } ld_segment_sections;

/* an entry of the input-section-name cache: name -> its ld_input_section_description or LD_NONE */
typedef struct { const char *name; uint32_t hash; uint32_t input_description; } ld_name_cache_entry;

/* the VLD_TIME=1 phase timer (the diagnostics, struct ld_diagnostics, are in ld/ld.h) */
struct ld_phase_timing {
	bool enabled;			/* VLD_TIME is set */
	double start_time;		/* seconds, CLOCK_MONOTONIC, at the last phase */
	vperf start_counters;		/* the main thread's counters at the last phase */
	long start_faults;		/* the process's minor faults at the last phase */
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

/* an arm64 range-extension thunk (arch/a64/ld/thunk-creation.c) */
typedef struct {
	uint32_t global_symbol;			/* destination global (or LD_NONE) */
	uint32_t input_section;			/* destination ISec for local/section targets */
	uint64_t value;
	int64_t addend;
	uint32_t thunk_section;			/* thunk ISec */
	uint32_t offset;			/* offset in thunk section */
	uint32_t size;			/* 4 (b) or 12 (adrp/add/br) */
	bool is_long;			/* long form; sticky once chosen (lld) */
	const char *name;
} a64_ld_thunk;
typedef struct { VVEC(uint32_t) list; } a64_ld_thunk_section;

/* a .got slot of a local symbol: object and symbol index */
typedef struct { uint32_t object_index, symbol_index; } ld_got_location;

/* a global symbol's place in .symtab */
typedef struct { uint32_t global_symbol; uint32_t index; uint64_t string_offset; bool is_local; } ld_global_symbol_output;

/* an output section of ld -r, grouped by name */
typedef struct {
	const char *name;
	uint32_t hash;
	uint32_t type, alignment, entry_size;
	uint64_t flags, size, offset;
	VVEC(uint32_t) sections;
	uint64_t rela_count, rela_offset;
	uint32_t section_header_index, rela_section_header_index, name_offset, rela_name_offset;
	uint32_t section_symbol;
	uint32_t group;			/* a .group: its input SHT_GROUP ISec + 1 */
	bool is_group_member;			/* a group member: its own output section, SHF_GROUP */
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

/* the objects, their sections and the merge sets, and the loading passes */
struct ld_inputs {
	bool x86;			/* the inputs are x86-64 (arch/x64/ld/relocation.c) */
	_Atomic uint16_t machine;	/* e_machine of the first object loaded (ld_object_parse) */
	VVEC(ld_object *) objects;
	VVEC(ld_input_section) sections;
	VVEC(ld_merge_input) merge_inputs;
	VVEC(ld_merge_section) merge_sections;
	uint32_t real_object_count;	/* objects excluding synthetic */
	ld_pending_member_vector pending;	/* archive members and objects to load */
	bool quiet;			/* the sequential lazy-archive pass: no messages */
	/* the lazy-archive pass (its vectors are in the arena; the table is malloc'ed) */
	struct ld_lazy_symbol_table lazy_symbols;
	ld_pending_member_vector lazy_candidates, lazy_order;
	VVEC(uint8_t) lazy_taken;
};

/* the global symbol table and the symbols the script and the stub pass make */
struct ld_symbols {
	_Atomic(const char *) *slot_names;	/* per slot */
	_Atomic uint64_t *slot_words;	/* per slot: hash << 32 | id + 1, 0 until published */
	uint32_t slot_mask;
	const char **names;		/* per id */
	ld_global_symbol *globals;	/* per id */
	_Atomic uint32_t count;
	VVEC(uint32_t) order;		/* ids in symbol table order */
	uint64_t script_order;
	VVEC(uint32_t) script_inserted;	/* slots first inserted by the script, in order */
	const char **stub_names;
	uint32_t stub_count;
	VVEC(uint32_t) stop_symbols;	/* __stop_X: the section size, after layout */
};

/* the parsed linker script and the parser's cursor */
struct ld_script {
	VVEC(ld_input_section_description) input_descriptions;
	VVEC(ld_output_section) sections;
	VVEC(ld_command) commands;	/* the top-level commands */
	const char *entry;		/* ENTRY() */
	VVEC(ld_script_segment) segments;	/* PHDRS */
	bool has_phdrs;
	VVEC(ld_version_node) version_nodes;
	VVEC(const char *) version_locals;
	uint32_t version_definition_count;	/* named nodes */
	VVEC(uint32_t) referenced_symbols;	/* names referenced by expressions, in parse order (lld referencedSymbols) */
	VVEC(ld_provide) provides;
	ld_provide *current_provide;
	VVEC(const char *) assert_failures;	/* recorded failures of the current pass */
	VVEC(const char *) address_errors;	/* errors of the current address pass */
	const char *cursor, *end, *file;	/* the lexer */
};

struct ld_context {
	struct ld_options options;
	struct ld_inputs inputs;
	struct ld_symbols symbols;
	struct ld_script script;
	struct ld_diagnostics diagnostics;
	struct ld_phase_timing timing;
	struct ld_layout layout;
	struct ld_relocations relocations;
	struct ld_relocatable relocatable;
	struct ld_output output;
	struct ld_symbol_table symbol_table;
	struct ld_dynamic dynamic;
};

/* ---- shared by the parts: types and constants ---- */

/* the image and where the chunk hashes go, for ld_build_id_chunk_hash */
typedef struct { const uint8_t *image; uint64_t size; uint8_t *hashes; } ld_build_id_job;

/* an input .eh_frame split into CIE and FDE records (lld EhInputSection::split) */
typedef struct { uint32_t offset, size, first_relocation, end_relocation, cie_offset; bool is_cie; } ld_eh_frame_record;

typedef struct {
	uint64_t address;			/* S + A, merge-aware */
	uint64_t symbol_address;			/* S (without addend; for relaxation) */
	bool is_absolute;			/* target is absolute (no dynamic relocation) */
	bool is_undefined;
	bool discarded;
	uint32_t key;			/* identity for ADRP relaxation */
	bool is_global;
} ld_relocation_target;

struct ld_response_memory { struct ld_response_memory *next; };

enum { LD_SECTION_ORDER_NAME, LD_SECTION_ORDER_ALIGNMENT_DESCENDING, LD_SECTION_ORDER_ADDRESS };

typedef void (*ld_parallel_function)(void *arg, size_t i);

/* character classes of the script lexer (NUL behaves as strchr() made it) */
enum { LD_CHARACTER_NAME_END = 1, LD_CHARACTER_PATTERN_END = 2, LD_CHARACTER_IDENTIFIER = 4, LD_CHARACTER_WORD = 8 };
#define LD_CHARACTER_ALPHANUMERIC (LD_CHARACTER_IDENTIFIER | LD_CHARACTER_WORD)

#define SHT_GNU_HASH 0x6ffffff6u
#define SHT_GNU_VERDEF 0x6ffffffdu
#define SHT_GNU_VERSYM 0x6fffffffu
enum {
	DT_NULL = 0, DT_HASH = 4, DT_STRTAB = 5, DT_SYMTAB = 6, DT_STRSZ = 10, DT_SYMENT = 11,
	DT_SONAME = 14, DT_FLAGS = 30, DF_SYMBOLIC = 2,
};
#define DT_GNU_HASH 0x6ffffef5u
#define DT_VERSYM 0x6ffffff0u
#define DT_VERDEF 0x6ffffffcu
#define DT_VERDEFNUM 0x6ffffffdu

/* ---- process-wide state (defined in the file named) ---- */

LD_SHARED bool ld_keep_mappings;		/* region.c */
LD_SHARED int ld_thread_count;			/* thread-pool.c */
LD_SHARED bool ld_standalone;			/* thread-pool.c */
LD_SHARED const uint8_t ld_script_character_classes[256];	/* script-lexer.c */

/* ---- functions one part defines and another calls, by part ---- */

/* link-context.c */
VPRIV struct ld_context *ld_context_create(FILE *error_stream, int directory_fd);
VPRIV void ld_context_destroy(struct ld_context *ctx);
/* diagnostics.c */
VPRIV void ld_error_report(struct ld_context *ctx, const char *fmt, ...);
VPRIV void ld_warning_report(struct ld_context *ctx, const char *fmt, ...);
/* phase-timing.c */
VPRIV long ld_minor_fault_count(void);
VPRIV double ld_clock_now(void);
VPRIV void ld_phase_time_report(struct ld_context *ctx, const char *name);
/* region.c */
VPRIV void ld_region_unmap(const void *p, size_t n);
VPRIV void *ld_region_large_alloc(size_t n);
VPRIV void *ld_region_alloc(size_t n);
VPRIV void ld_regions_free(void);
VPRIV char *ld_region_string_duplicate(const char *s);
/* thread-pool.c */
VPRIV void ld_parallel_for(size_t n, ld_parallel_function fn, void *arg);
VPRIV int ld_parallel_default_width(void);
/* global-symbols.c */
VPRIV uint32_t ld_visibility_rank(uint8_t v);
VPRIV uint8_t ld_visibility_from_rank(uint32_t r);
VPRIV void ld_atomic_u64_min(_Atomic uint64_t *p, uint64_t v);
VPRIV void ld_atomic_u8_min(_Atomic uint8_t *p, uint8_t v);
VPRIV uint32_t ld_global_name_intern(struct ld_context *ctx, const char *name);
VPRIV uint32_t ld_global_name_find(struct ld_context *ctx, const char *name);
/* address-values.c */
VPRIV uint64_t ld_output_section_address(struct ld_context *ctx, uint32_t o);
VPRIV uint32_t ld_output_section_header_index(struct ld_context *ctx, uint32_t o);
VPRIV uint32_t ld_input_section_output_section(struct ld_context *ctx, uint32_t s);
VPRIV uint64_t ld_input_section_address(struct ld_context *ctx, uint32_t s, uint64_t off);
VPRIV uint64_t ld_expression_value_section_address(struct ld_context *ctx, ld_expression_value v);
VPRIV uint64_t ld_expression_value_resolve(struct ld_context *ctx, ld_expression_value v);
VPRIV uint64_t ld_expression_value_section_offset(struct ld_context *ctx, ld_expression_value v);
VPRIV bool ld_expression_value_is_absolute(ld_expression_value v);
VPRIV ld_expression_value ld_expression_value_absolute_make(uint64_t x);
VPRIV ld_expression_value ld_expression_value_in_section_make(ld_expression_value a, bool fabs, uint64_t off);
VPRIV ld_expression_value ld_global_symbol_expression_value(struct ld_context *ctx, uint32_t g);
VPRIV uint64_t ld_global_symbol_address(struct ld_context *ctx, uint32_t g);
VPRIV bool ld_global_symbol_is_absolute(struct ld_context *ctx, uint32_t g);
/* expression-eval.c */
VPRIV ld_expression_value ld_expression_value_add(struct ld_context *ctx, ld_expression_value a, ld_expression_value b);
VPRIV ld_expression_value ld_expression_value_subtract(struct ld_context *ctx, ld_expression_value a, ld_expression_value b);
VPRIV ld_expression_value ld_dot_expression_value(struct ld_context *ctx);
VPRIV ld_expression_value ld_expression_eval(struct ld_context *ctx, ld_expression *e);
/* script-lexer.c */
VPRIV void ld_script_whitespace_skip(struct ld_context *ctx);
VPRIV bool ld_script_is_at_end(struct ld_context *ctx);
VPRIV bool ld_script_character_peek(struct ld_context *ctx, char c);
VPRIV bool ld_script_character_consume(struct ld_context *ctx, char c);
VPRIV void ld_script_error_report(struct ld_context *ctx, const char *what);
VPRIV void ld_script_character_expect(struct ld_context *ctx, char c);
VPRIV char *ld_script_text_duplicate(const char *b, const char *e);
VPRIV char *ld_script_name_parse(struct ld_context *ctx);
VPRIV char *ld_script_pattern_name_parse(struct ld_context *ctx);
/* script-expression-parser.c */
VPRIV ld_expression *ld_script_expression_create(uint8_t op);
VPRIV int ld_script_operator_peek(struct ld_context *ctx, char out[4]);
VPRIV ld_expression *ld_script_expression_parse(struct ld_context *ctx);
VPRIV ld_expression *ld_script_parenthesized_expression_parse(struct ld_context *ctx);
VPRIV ld_expression *ld_script_primary_expression_parse(struct ld_context *ctx);
/* script-statements.c */
VPRIV uint32_t ld_output_section_find(struct ld_context *ctx, const char *name);
VPRIV void ld_script_parse(struct ld_context *ctx, const char *text, size_t len);
/* glob-match.c */
VPRIV bool ld_glob_match(const char *p, const char *s);
VPRIV bool ld_pattern_match(const ld_pattern *p, const char *name);
/* input-mapping.c */
VPRIV uint8_t *ld_file_map(struct ld_context *ctx, const char *path, size_t *size);
VPRIV void ld_archive_add(struct ld_context *ctx, const char *path, const uint8_t *b, size_t n);
/* object-validate.c */
VPRIV bool ld_object_is_valid(struct ld_context *ctx, ld_object *o, const elf_file_header *eh);
VPRIV void ld_object_parse(void *arg, size_t i);
/* input-sections.c */
VPRIV void ld_input_sections_fill(void *arg, size_t i);
VPRIV void ld_gnu_property_drop(struct ld_context *ctx);
/* symbol-resolution.c */
VPRIV void ld_comdat_groups_deduplicate(struct ld_context *ctx);
VPRIV void ld_object_symbols_resolve(void *arg, size_t i);
VPRIV void ld_duplicate_symbols_check(void *arg, size_t i);
VPRIV void ld_global_symbol_finish(void *arg, size_t i);
/* relocation-target.c */
VPRIV void ld_relocation_target_find(struct ld_context *ctx, const ld_object *o, uint32_t symidx, int64_t addend, ld_relocation_target *t);
VPRIV void ld_final_addresses_compute(struct ld_context *ctx);
/* output-state.c */
VPRIV uint32_t ld_input_section_create(struct ld_context *ctx, const char *name, uint8_t kind, uint32_t type, uint64_t flags, uint32_t align);
/* eh-frame.c */
VPRIV bool ld_eh_frame_is_input(struct ld_context *ctx, const ld_input_section *is);
VPRIV ld_eh_frame_record *ld_eh_frame_records_split(struct ld_context *ctx, uint32_t si, uint32_t *nrec);
VPRIV uint32_t ld_relocation_symbol_input_section(struct ld_context *ctx, const ld_object *o, uint32_t y);
VPRIV void ld_eh_frame_merge(struct ld_context *ctx);
/* arch/a64/ld/thunk-model.c */
VPRIV void a64_ld_thunk_relocation_record(struct ld_context *ctx, uint32_t isec, uint32_t ri, uint32_t th);
VPRIV uint32_t a64_ld_thunk_relocation_find(struct ld_context *ctx, uint32_t isec, uint32_t ri);
VPRIV uint64_t a64_ld_thunk_address(struct ld_context *ctx, uint32_t t);
VPRIV uint64_t a64_ld_thunk_destination(struct ld_context *ctx, uint32_t t);
VPRIV bool a64_ld_branch_is_in_range(uint64_t src, uint64_t dst);
/* section-placement.c */
VPRIV uint64_t ld_headers_size(struct ld_context *ctx);
VPRIV uint32_t ld_input_section_description_match(struct ld_context *ctx, const char *name);
VPRIV void ld_section_ids_sort(struct ld_context *ctx, uint32_t *ids, uint32_t n, int order);
VPRIV void ld_section_commit(struct ld_context *ctx, ld_output_section *os, ld_input_section *is);
VPRIV int ld_orphan_section_rank(uint64_t flags, uint32_t type, const char *name);
VPRIV uint64_t ld_orphan_section_key(const ld_input_section *is);
VPRIV int ld_rank_proximity(int a, int b);
VPRIV int ld_output_section_rank(struct ld_context *ctx, uint32_t oi);
VPRIV void ld_name_cache_init(struct ld_context *ctx);
VPRIV const char *ld_start_stop_section_name(const char *n);
/* section-gc.c */
VPRIV void ld_sections_garbage_collect(struct ld_context *ctx, const char *entry);
/* section-assignment.c */
VPRIV void ld_input_sections_assign(struct ld_context *ctx);
/* string-merge.c */
VPRIV void ld_merges_finalize(struct ld_context *ctx);
/* arch/a64/ld/relocation-scan.c */
VPRIV void a64_ld_relocation_target_classify(struct ld_context *ctx, const ld_object *o, uint32_t symidx, ld_relocation_target *t);
VPRIV void a64_ld_relocation_scan(void *arg, size_t i);
/* arch/a64/ld/thunk-creation.c */
VPRIV bool a64_ld_thunks_create(struct ld_context *ctx, int pass);
/* file-layout.c */
VPRIV bool ld_output_section_is_in_load_segment(ld_output_section *os);
VPRIV uint32_t ld_output_section_segment_flags(ld_output_section *os);
VPRIV void ld_segment_section_add(struct ld_context *ctx, int ph, uint32_t oi);
VPRIV int ld_segment_create(struct ld_context *ctx, uint32_t type, uint32_t flags);
VPRIV void ld_file_offsets_assign(struct ld_context *ctx);
/* symbol-table-locals.c */
VPRIV void ld_local_symbols_count(void *arg, size_t i);
VPRIV uint16_t ld_symbol_section_index_of_input_section(struct ld_context *ctx, uint32_t sec);
VPRIV void ld_symbol_record_store(elf_symbol *d, uint8_t *strbuf, uint64_t *stroff, const char *name, uint8_t info, uint8_t other, uint16_t shndx, uint64_t val, uint64_t size);
/* arch/a64/ld/relocation-apply.c */
VPRIV int a64_ld_relocation_error_report(struct ld_context *ctx, const ld_object *o, const ld_input_section *is, const elf_rela *rl, const char *what, int64_t v);
VPRIV bool ld_value_fits_signed(int64_t v, int bits);
VPRIV void a64_ld_input_section_relocate(struct ld_context *ctx, uint32_t si, uint8_t *buf);
/* arch/x64/ld/relocation.c */
VPRIV void x64_ld_got_find(void *arg, size_t i);
VPRIV void x64_ld_relocation_scan(void *arg, size_t i);
VPRIV void x64_ld_input_section_relocate(struct ld_context *ctx, uint32_t si, uint8_t *buf);
/* section-write.c */
VPRIV void ld_input_section_write(void *arg, size_t j);
/* eh-frame-header.c */
VPRIV void ld_eh_frame_header_write(struct ld_context *ctx);
/* stub-object.c */
VPRIV int ld_string_cmp(const void *a, const void *b);
VPRIV ld_object *ld_stub_object_create(struct ld_context *ctx, const char **names, uint32_t n);
/* option-parse.c */
VPRIV bool ld_arguments_parse(struct ld_context *ctx, int argc, char **argv);
/* lazy-archive.c */
VPRIV void ld_lazy_object_add(struct ld_context *ctx, ld_pending_member p);
VPRIV void ld_lazy_archive_process(struct ld_context *ctx, uint32_t c0, uint32_t c1);
/* input-load.c */
VPRIV void ld_inputs_load(struct ld_context *ctx);
VPRIV void ld_input_sections_alloc(struct ld_context *ctx, uint32_t from);
VPRIV void ld_global_table_init(struct ld_context *ctx);
VPRIV void ld_global_table_free(struct ld_context *ctx);
/* output-file-write.c */
VPRIV int ld_output_file_write(struct ld_context *ctx);
VPRIV void ld_build_id_chunk_hash(void *arg, size_t i);
/* script-address-assignment.c */
VPRIV bool ld_output_section_offsets_assign(struct ld_context *ctx, uint32_t oi, bool *symchg);
VPRIV bool ld_addresses_assign(struct ld_context *ctx);
VPRIV void ld_symbol_assignments_process(struct ld_context *ctx);
/* output-section-adjust.c */
VPRIV void ld_output_sections_adjust(struct ld_context *ctx);
/* program-headers.c */
VPRIV void ld_program_headers_create(struct ld_context *ctx);
VPRIV void ld_program_headers_finish(struct ld_context *ctx);
/* symbol-table-write.c */
VPRIV bool ld_global_symbol_is_in_symbol_table(struct ld_context *ctx, uint32_t g);
VPRIV bool ld_global_symbol_is_local(struct ld_context *ctx, uint32_t g);
VPRIV void ld_symbol_table_build(struct ld_context *ctx);
VPRIV void ld_symbol_table_fill(struct ld_context *ctx);
/* output-headers.c */
VPRIV void ld_section_name_table_build(struct ld_context *ctx);
VPRIV void ld_headers_write(struct ld_context *ctx);
VPRIV void ld_synthetic_sections_write(struct ld_context *ctx);
VPRIV void ld_build_id_write(struct ld_context *ctx);
/* dynamic-sections.c */
VPRIV void ld_symbol_versions_assign(struct ld_context *ctx);
VPRIV bool ld_input_section_is_live(struct ld_context *ctx, uint32_t isec);
VPRIV void ld_dynamic_symbol_define(struct ld_context *ctx);
VPRIV void ld_dynamic_sections_build(struct ld_context *ctx);
VPRIV void ld_dynamic_section_links_set(struct ld_context *ctx);
VPRIV void ld_dynamic_sections_write(struct ld_context *ctx);
/* script-program-headers.c */
VPRIV void ld_script_program_headers_create(struct ld_context *ctx);
/* gap-fill.c */
VPRIV void ld_gap_fills_write(struct ld_context *ctx);
/* got-probe.c */
VPRIV bool ld_got_is_needed(struct ld_context *ctx);
/* common-symbols.c */
VPRIV void ld_common_symbols_replace(struct ld_context *ctx);
/* synthetic-sections.c */
VPRIV void ld_synthetic_sections_add(struct ld_context *ctx);
/* script-symbols.c */
VPRIV void ld_script_assignments_for_each(struct ld_context *ctx, void (*fn)(struct ld_context *, ld_assignment *));
VPRIV bool ld_global_symbol_is_referenced(struct ld_context *ctx, uint32_t g);
VPRIV bool ld_global_symbol_is_defined_by_object(struct ld_context *ctx, uint32_t g);
VPRIV void ld_script_symbols_declare(struct ld_context *ctx);
/* stub-collection.c */
VPRIV void ld_stubs_collect(struct ld_context *ctx);
VPRIV void ld_stub_list_write(struct ld_context *ctx);
/* global-order.c */
VPRIV void ld_global_order_build(struct ld_context *ctx);
/* undefined-report.c */
VPRIV void ld_undefined_symbols_report(struct ld_context *ctx);
/* symbol-demote.c */
VPRIV void ld_symbols_demote(void *arg, size_t i);
VPRIV void ld_global_symbol_finish_task(void *arg, size_t i);
VPRIV void ld_start_stop_symbols_define(struct ld_context *ctx);
/* link-run.c */
VPRIV int ld_link_run(struct ld_context *ctx, int argc, char **argv);
/* relocatable-link.c */
VPRIV int ld_relocatable_link(struct ld_context *ctx);
/* macho-probe.c */
VPRIV bool ld_first_input_is_macho(int argc, char **argv, int dirfd);
/* response-files.c */
VPRIV bool ld_response_files_expand(int *pargc, char ***pargv, int dirfd, struct ld_response_memory **keep);

/* ---- small helpers every part inlines ---- */

/* unaligned 32/64-bit loads and stores, alignment */
static inline uint32_t ld_u32_load(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void ld_u32_store(void *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void ld_u64_store(void *p, uint64_t v) { memcpy(p, &v, 8); }
static inline uint64_t ld_value_align_up(uint64_t v, uint64_t a) { return a <= 1 ? v : (v + a - 1) & ~(a - 1); }
/* smallest x >= v with x % a == skew % a (llvm::alignTo with skew) */
static inline uint64_t ld_value_align_skew(uint64_t v, uint64_t a, uint64_t skew)
{
	skew %= a;
	return ((v + a - 1 - skew) / a) * a + skew;
}

static inline uint32_t ld_name_hash(const char *s, size_t n) { return (uint32_t)vdigest_xxh3(s, n); }

static inline uint64_t ld_key_load(_Atomic uint64_t *p) { return ~atomic_load_explicit(p, memory_order_relaxed); }

static inline uint32_t ld_visibility_load(_Atomic uint8_t *p)
{
	uint32_t r = (uint8_t)~atomic_load_explicit(p, memory_order_relaxed);
	return r > 3 ? 3 : r;
}

static inline const char *ld_global_name(struct ld_context *ctx, uint32_t g) { return ctx->symbols.names[g]; }

static inline uint64_t ld_symbol_key(uint32_t obj, uint32_t idx) { return (uint64_t)obj << 32 | idx; }

static inline void ld_relocation_target_final_find(struct ld_context *ctx, const ld_object *o, uint32_t symidx, int64_t addend, ld_relocation_target *t)
{
	if (symidx >= o->first_global_index) {
		uint32_t g = o->global_slots[symidx - o->first_global_index];
		const ld_global_symbol *s = &ctx->symbols.globals[g];
		if (!s->is_undefined) {
			t->is_undefined = t->discarded = false;
			t->key = g;
			t->is_global = true;
			t->is_absolute = s->is_absolute;
			t->symbol_address = s->final_address;
			t->address = s->final_address + addend;
			return;
		}
	} else {
		const elf_symbol *y = &o->symbols[symidx];
		if (y->st_shndx != SHN_UNDEF && y->st_shndx < 0xff00) {
			uint64_t b = ctx->relocations.final_section_addresses[o->first_input_section + y->st_shndx];
			if (b != UINT64_MAX) {
				t->is_undefined = t->discarded = false;
				t->key = symidx;
				t->is_global = false;
				t->is_absolute = false;
				t->symbol_address = b + ((y->st_info & 15) == STT_SECTION ? 0 : y->st_value);
				t->address = t->symbol_address + addend;
				return;
			}
		}
	}
	ld_relocation_target_find(ctx, o, symidx, addend, t);
}

static inline uint64_t ld_merge_piece_length(struct ld_context *ctx, const ld_merge_input *m, uint32_t p)
{
	uint64_t end = p + 1 < m->piece_count ? m->input_offsets[p + 1] : ctx->inputs.sections.v[m->input_section].size;
	return end - m->input_offsets[p];
}

static inline __attribute__((always_inline)) bool ld_script_word_consume(struct ld_context *ctx, const char *w)
{
	ld_script_whitespace_skip(ctx);
	size_t n = strlen(w);
	if ((size_t)(ctx->script.end - ctx->script.cursor) >= n && !memcmp(ctx->script.cursor, w, n) &&
	    (ctx->script.cursor + n == ctx->script.end || !(ld_script_character_classes[(unsigned char)ctx->script.cursor[n]] & LD_CHARACTER_WORD))) {
		ctx->script.cursor += n;
		return true;
	}
	return false;
}

#endif
