/*
 * vlink's PE/COFF linker: the model shared by pe-*.c (one TU, pe-linker.c).
 *
 * The model is lld's COFF linker (lld-link in its MinGW mode, driven through
 * ld.lld -m i386pep), because the output is compared with lld's byte for
 * byte: input files own chunks (a section of an object, a COMMON symbol, or
 * a synthetic piece the linker makes: an import directory, a thunk, a base
 * relocation block, ...); chunks go to partial sections keyed by
 * (name, characteristics), partial sections to output sections; symbols are
 * one record per global name that is overwritten as resolution proceeds
 * (lld's replaceSymbol), plus one record per static symbol of an object.
 */
#ifndef VCC_PE_LINKER_INTERNAL_H
#define VCC_PE_LINKER_INTERNAL_H
#include "pe-definitions.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct pe_chunk pe_chunk;
typedef struct pe_symbol pe_symbol;
typedef struct pe_file pe_file;
typedef struct pe_archive pe_archive;
typedef struct pe_output pe_output;

/* ---------------- chunks ---------------- */

enum pe_chunk_kind {
	PE_CHUNK_SECTION,		/* a section of an object file */
	PE_CHUNK_COMMON,		/* a COMMON symbol's storage */
	/* synthetic chunks */
	PE_CHUNK_IMPORT_DIRECTORY,	/* one import directory entry (20 bytes) */
	PE_CHUNK_NULL,			/* zeros without data (lld's NullChunk) */
	PE_CHUNK_LOOKUP,		/* ILT/IAT entry: RVA of a hint/name chunk */
	PE_CHUNK_ORDINAL_ONLY,		/* ILT/IAT entry by ordinal */
	PE_CHUNK_HINT_NAME,		/* hint + name, even size */
	PE_CHUNK_STRING,		/* a NUL-terminated string */
	PE_CHUNK_IMPORT_THUNK,		/* jmp *__imp_X(%rip) */
	PE_CHUNK_ABSOLUTE_POINTER,	/* a constant pointer-sized value */
	PE_CHUNK_PSEUDO_RELOC_TABLE,	/* MinGW runtime pseudo relocations, v2 */
	PE_CHUNK_EMPTY,			/* size 0 (a label) */
	PE_CHUNK_DEBUG_DIRECTORY,
	PE_CHUNK_CODEVIEW_RECORD,	/* RSDS + build id */
	PE_CHUNK_BASEREL_BLOCK,
	PE_CHUNK_LOCAL_IMPORT,		/* __imp_X for a locally defined X: a pointer to it */
	PE_CHUNK_EXPORT_DIRECTORY,
	PE_CHUNK_EXPORT_ADDRESS_TABLE,
	PE_CHUNK_EXPORT_NAME_POINTERS,
	PE_CHUNK_EXPORT_ORDINALS,
};

struct pe_chunk {
	uint8_t kind;
	bool live;
	bool has_data;
	bool is_comdat;
	bool is_dwarf;			/* .debug_*: not traversed by --gc-sections */
	uint8_t selection;		/* COMDAT selection of a section chunk */
	uint32_t align;
	uint32_t chars;			/* output characteristics (type and permission bits) */
	uint32_t header_chars;		/* the input section's full characteristics */
	const char *name;		/* section name (partial section key) */
	uint32_t size;
	uint32_t rva;
	uint32_t order;			/* creation order (stable sorts) */
	pe_output *os;
	pe_file *file;
	/* section chunks */
	const uint8_t *data;		/* contents (NULL when !has_data) */
	const uint8_t *relocs;		/* COFF relocation records */
	uint32_t nrelocs;
	uint32_t section_index;
	pe_chunk *assoc_first, *assoc_next;	/* associative children */
	pe_symbol *leader;		/* COMDAT leader */
	/* synthetic payload */
	pe_symbol *sym;			/* thunk: the __imp_ symbol; local import: the target; common: its symbol */
	pe_chunk *target;		/* lookup: its hint/name chunk; import directory: lookup table */
	pe_chunk *target2;		/* import directory: address table */
	pe_chunk *target3;		/* import directory: the DLL name */
	uint64_t value;			/* absolute pointer value; ordinal; hint */
	const char *str;		/* string, hint/name name */
	uint8_t *bytes;			/* prebuilt contents (base relocation block, export tables) */
	void *aux;			/* pseudo relocation list, debug records, export model */
	uint32_t aux_count;
};

/* ---------------- symbols ---------------- */

enum pe_symbol_kind {
	PE_SYM_UNDEFINED,
	PE_SYM_LAZY_ARCHIVE,		/* a member of an archive defines it */
	PE_SYM_REGULAR,			/* defined in a section chunk */
	PE_SYM_COMMON,
	PE_SYM_ABSOLUTE,
	PE_SYM_SYNTHETIC,		/* defined by the linker in a synthetic chunk (or at RVA 0) */
	PE_SYM_IMPORT_DATA,		/* __imp_X: the IAT slot */
	PE_SYM_IMPORT_THUNK,		/* X: the jmp through the IAT slot */
	PE_SYM_LOCAL_IMPORT,		/* __imp_X for a defined X */
};

struct pe_symbol {
	const char *name;
	uint32_t name_len;
	uint8_t kind;
	bool external;			/* a global (in the symbol table), not an object's static */
	bool is_comdat;			/* a COMDAT leader */
	bool pending_load;		/* an archive member load is queued for it */
	bool gc_root;
	bool written;			/* in the output symbol table already */
	bool runtime_pseudo_reloc;	/* auto-imported: points at the IAT slot */
	bool used_in_regular;		/* referenced or defined by an object (lld isUsedInRegularObj) */
	bool weak_anti_dependency;
	bool can_inline;
	uint8_t storage_class;		/* of the defining COFF symbol (or 0: not a COFF definition) */
	uint16_t type;
	uint64_t value;			/* offset in chunk; absolute VA; common size */
	pe_chunk *chunk;
	pe_file *file;			/* defining (or referencing) file */
	pe_symbol *weak_alias;		/* undefined: the weak external's default */
	pe_archive *archive;		/* lazy: where */
	uint32_t member_offset;
	pe_symbol *wrapped;		/* import thunk: the __imp_ symbol */
	pe_symbol *hash_next;
};

/* ---------------- files ---------------- */

enum pe_file_kind { PE_FILE_OBJECT, PE_FILE_IMPORT };

typedef struct {
	const char *name;		/* exported name */
	const char *symbol_name;	/* the symbol it refers to */
	const char *forward_to;
	pe_symbol *sym;
	uint16_t ordinal;
	bool data, private_, no_name, constant, from_directive;
} pe_export;

struct pe_file {
	uint8_t kind;
	bool live;			/* import file: has an IAT entry */
	bool thunk_live;
	const char *path;		/* the file itself, or the member name */
	const char *parent;		/* the archive (NULL when not a member) */
	const uint8_t *data;
	size_t size;
	uint32_t order;
	/* objects */
	uint32_t nsections;
	coff_section_header *sections;	/* [1..nsections] */
	const char **secnames;		/* [1..nsections]: full names (long ones from the string table) */
	const uint8_t *symtab;
	uint32_t nsyms;
	const char *strtab;
	uint32_t strtab_size;
	pe_chunk **sparse;		/* per section: its chunk, NULL (discarded) or PE_PENDING_COMDAT */
	pe_symbol **syms;		/* per symbol index */
	pe_chunk **chunks;
	uint32_t nchunks, chunk_capacity;
	const char *directives;
	size_t directives_len;
	uint32_t feat00;
	/* import members */
	const char *dll_name;
	const char *external_name;
	uint16_t hint;
	uint8_t import_type;
	uint8_t name_type;
	pe_symbol *imp_sym, *thunk_sym;
	pe_chunk *location;		/* the IAT slot (address table entry) */
};

struct pe_archive {
	const char *path;
	const uint8_t *data;
	size_t size;
	const char *longnames;
	size_t longnames_size;
	uint32_t *seen;			/* member offsets loaded (open hash, 0 = empty) */
	uint32_t seen_capacity, seen_count;
	struct { const char *name; uint32_t offset; } *symbols;
	uint32_t nsymbols;
};

/* ---------------- output ---------------- */

typedef struct {
	const char *name;
	uint32_t chars;
	pe_chunk **chunks;
	uint32_t nchunks, capacity;
} pe_partial;

struct pe_output {
	const char *name;
	uint32_t chars;			/* characteristics written to the header */
	pe_chunk **chunks;
	uint32_t nchunks, capacity;
	uint32_t rva, virtual_size, raw_size, file_offset;
	uint32_t index;			/* 1-based once assigned */
	uint32_t strtab_offset;		/* long name of a discardable section, or 0 */
	int32_t str;			/* its string (pe-write.c), or -1 */
	bool removed;
};

/* ---------------- configuration ---------------- */

typedef struct {
	const char *output;
	bool dll;
	uint64_t image_base;
	bool image_base_set;
	uint16_t subsystem;
	uint16_t major_subsystem, minor_subsystem;
	bool subsystem_version_set;
	uint16_t major_os, minor_os, major_image, minor_image;
	uint64_t stack_reserve, stack_commit, heap_reserve, heap_commit;
	uint32_t file_align, section_align;
	bool dynamic_base, high_entropy_va, nx_compat, ts_aware, large_address_aware, no_seh;
	bool appcontainer, integrity_check, relocatable, no_isolation, no_bind;
	bool gc;
	int debug;			/* 0: -s, 1: -S (symbol table only), 2: DWARF and symbol table */
	bool build_id;
	bool insert_timestamp;
	const char *entry;
	bool no_entry;
	const char *implib;
	bool export_all, export_all_set, exclude_all;
	bool auto_import, pseudo_relocs;
	bool allow_multiple;
	const char *map_file;
	bool verbose;
	bool kill_at;
	bool demangle;
	bool fatal_warnings;
	bool allow_duplicate_weak;
	const char *output_def;
	const char **includes;		/* --require-defined, -include: GC roots that must resolve */
	uint32_t nincludes;
	const char **optional_includes;	/* -u: GC roots, may stay undefined */
	uint32_t noptional_includes;
	const char **exclude_libs;
	uint32_t nexclude_libs;
	bool exclude_libs_all;
	const char **exclude_symbols;
	uint32_t nexclude_symbols;
	pe_export *exports;
	uint32_t nexports, export_capacity;
	struct { const char *name; uint32_t align; } *align_comm;
	uint32_t nalign_comm;
	struct { const char *from, *to; } *alternate_names;
	uint32_t nalternate_names;
	struct { const char *from, *to; } *merges;
	uint32_t nmerges;
} pe_config;

#define PE_PENDING_COMDAT ((pe_chunk *)1)

#endif
