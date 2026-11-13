/* The linker's data model for inputs: objects, input sections, merge inputs and merge sets, all addressed by index. */

/* ---------------- input files ---------------- */

typedef struct Obj {
	const char *path;		/* for diagnostics */
	const uint8_t *buf;
	size_t size;
	const Shdr *sh;
	uint32_t nsh;
	const Sym *sym;
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
} Obj;

static VVEC(Obj *) objs;

enum { K_NONE, K_NORMAL, K_MERGE_IN, K_MERGE, K_THUNK, K_RELADYN, K_BUILDID,
       K_SYMTAB, K_STRTAB, K_SHSTRTAB, K_COMMENT, K_GOT, K_EHHDR,
       K_DYNSYM, K_DYNSTR, K_HASH, K_GNUHASH, K_VERSYM, K_VERDEF, K_DYNAMIC };

typedef struct ISec {
	const uint8_t *data;
	const char *name;
	uint64_t size, flags, off;
	uint32_t type, align, entsize;
	uint32_t obj;			/* owning object or NONE */
	uint32_t shndx;
	uint32_t osec;			/* output section, NONE, or OS_DISCARD */
	uint32_t isd;
	const Rela *rela;
	uint32_t nrela;
	uint32_t aux;			/* K_MERGE_IN: MergeIn idx; K_MERGE: Merge idx; K_THUNK: ThunkSec idx */
	uint32_t ndyn;			/* R_AARCH64_RELATIVE entries produced */
	uint32_t dyn0;
	uint8_t kind;
	bool thunked;			/* some relocation has a tr_put entry */
	bool dropped;			/* member of a COMDAT group seen before */
	uint32_t gnext;			/* next member of its section group + 1, 0: none */
	uint32_t ehcie;			/* .eh_frame FDE piece: its CIE piece + 1 (the CIE pointer is written) */
} ISec;

#define OS_DISCARD 0xfffffffeu
static VVEC(ISec) isecs;

/* SHF_MERGE input split into pieces */
typedef struct {
	uint32_t isec;
	uint32_t syn;			/* merge synthetic ISec id */
	uint32_t npieces;
	uint32_t *inoff;		/* piece input offsets */
	uint32_t *hash;			/* 31-bit hash */
	uint32_t *outoff;		/* offset in the synthetic section */
} MergeIn;
static VVEC(MergeIn) mergeins;

typedef struct {
	uint32_t isec;			/* the synthetic ISec */
	uint32_t osec;
	uint64_t flags;
	uint32_t entsize, align;
	VVEC(uint32_t) members;		/* MergeIn ids in order */
	uint64_t shardoff[32], shardsize[32];
	/* piece lists per shard for writing: (mergein, piece) of unique pieces */
	VVEC(uint64_t) uniq[32];
} Merge;
static VVEC(Merge) merges;
