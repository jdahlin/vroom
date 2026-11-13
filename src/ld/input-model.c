/* The linker's data model for inputs: objects, input sections, merge inputs and merge sets, all addressed by index. */

/* ---------------- input files ---------------- */

typedef struct ld_object {
	const char *path;		/* for diagnostics */
	const uint8_t *buf;
	size_t size;
	const elf_section_header *sh;
	uint32_t nsh;
	const elf_symbol *sym;
	uint32_t nsym, firstg;
	const char *str;		/* symbol string table */
	const char *shstr;
	uint32_t isec0;			/* ISec id of section 0 */
	uint32_t *gs;			/* global slot of sym[firstg + i] */
	_Atomic uint8_t *lgot;		/* local k needs a .got slot (only when there is a .got) */
	uint32_t *lgotidx;		/* its slot */
	uint32_t idx;
	/* symbol table output */
	uint32_t nloc, loc0;		/* kept locals, first output index */
	uint32_t (*kept)[2];		/* kept locals: symbol index, name length */
	uint64_t locstr;		/* strtab bytes of kept locals */
	uint64_t str0;
	bool mapped;
} ld_object;

static VVEC(ld_object *) objs;

enum { LD_SECTION_KIND_NONE, LD_SECTION_KIND_NORMAL, LD_SECTION_KIND_MERGE_INPUT, LD_SECTION_KIND_MERGE, LD_SECTION_KIND_THUNK, LD_SECTION_KIND_DYNAMIC_RELOCATIONS, LD_SECTION_KIND_BUILD_ID,
       LD_SECTION_KIND_SYMBOL_TABLE, LD_SECTION_KIND_STRING_TABLE, LD_SECTION_KIND_SECTION_NAME_TABLE, LD_SECTION_KIND_COMMENT, LD_SECTION_KIND_GOT, LD_SECTION_KIND_EH_FRAME_HEADER,
       LD_SECTION_KIND_DYNAMIC_SYMBOLS, LD_SECTION_KIND_DYNAMIC_STRINGS, LD_SECTION_KIND_HASH_TABLE, LD_SECTION_KIND_GNU_HASH_TABLE, LD_SECTION_KIND_SYMBOL_VERSIONS, LD_SECTION_KIND_VERSION_DEFINITIONS, LD_SECTION_KIND_DYNAMIC };

typedef struct ld_input_section {
	const uint8_t *data;
	const char *name;
	uint64_t size, flags, off;
	uint32_t type, align, entsize;
	uint32_t obj;			/* owning object or LD_NONE */
	uint32_t shndx;
	uint32_t osec;			/* output section, LD_NONE, or LD_OUTPUT_SECTION_DISCARD */
	uint32_t isd;
	const elf_rela *rela;
	uint32_t nrela;
	uint32_t aux;			/* LD_SECTION_KIND_MERGE_INPUT: MergeIn idx; LD_SECTION_KIND_MERGE: Merge idx; LD_SECTION_KIND_THUNK: ThunkSec idx */
	uint32_t ndyn;			/* R_AARCH64_RELATIVE entries produced */
	uint32_t dyn0;
	uint8_t kind;
	bool thunked;			/* some relocation has a a64_ld_thunk_relocation_record entry */
	bool dropped;			/* member of a COMDAT group seen before */
	uint32_t gnext;			/* next member of its section group + 1, 0: none */
	uint32_t ehcie;			/* .eh_frame FDE piece: its CIE piece + 1 (the CIE pointer is written) */
} ld_input_section;

#define LD_OUTPUT_SECTION_DISCARD 0xfffffffeu
static VVEC(ld_input_section) isecs;

/* SHF_MERGE input split into pieces */
typedef struct {
	uint32_t isec;
	uint32_t syn;			/* merge synthetic ISec id */
	uint32_t npieces;
	uint32_t *inoff;		/* piece input offsets */
	uint32_t *hash;			/* 31-bit hash */
	uint32_t *outoff;		/* offset in the synthetic section */
} ld_merge_input;
static VVEC(ld_merge_input) mergeins;

typedef struct {
	uint32_t isec;			/* the synthetic ISec */
	uint32_t osec;
	uint64_t flags;
	uint32_t entsize, align;
	VVEC(uint32_t) members;		/* MergeIn ids in order */
	uint64_t shardoff[32], shardsize[32];
	/* piece lists per shard for writing: (mergein, piece) of unique pieces */
	VVEC(uint64_t) uniq[32];
} ld_merge_section;
static VVEC(ld_merge_section) merges;
