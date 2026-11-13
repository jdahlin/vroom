/* The parsed linker script: expressions, input section descriptions, output sections, PHDRS, VERSION nodes and symbol assignments. */

/* ---------------- linker script ---------------- */

typedef struct Expr Expr;
enum {
	E_NUM, E_SYM, E_DOT, E_ADDR, E_LOADADDR, E_SIZEOF, E_ALIGNOF, E_ALIGN1, E_ALIGN2,
	E_ABSOLUTE, E_BIN, E_NEG, E_NOT, E_LNOT, E_TERN, E_ASSERT, E_DEFINED, E_MAX, E_MIN,
	E_LOG2CEIL, E_SIZEOF_HEADERS,
};
struct Expr {
	uint8_t op;
	char bop[4];
	Expr *a, *b, *c;
	uint64_t num;
	uint32_t sym;			/* E_SYM/E_DEFINED: global slot; E_ADDR etc: osec */
	const char *msg;
};

typedef struct {
	char pat[16][64];		/* not used; patterns live in Pat */
} Unused;

typedef struct {
	const char **globs;		/* section name globs */
	uint32_t nglobs;
	uint8_t sort;			/* SORT_NONE, SORT_NAME, SORT_ALIGN */
} Pat;
enum { SORT_NONE, SORT_NAME, SORT_ALIGN };

typedef struct ISD {
	uint32_t osec;
	Pat *pats;
	uint32_t npats;
	bool keep;
	VVEC(uint32_t) secs;		/* ISec ids, in final order */
	uint32_t thunksec;		/* ISec of this ISD's thunk section, or NONE */
	uint64_t thunk_off;		/* pass-0 isecLimit */
	struct Call *calls;		/* CALL26/JUMP26 relocations, built by thunk pass 0 */
	uint32_t ncalls;
} ISD;
static VVEC(ISD) isds;

enum { C_ASSIGN, C_OSEC, C_ISD };
typedef struct Assign {
	uint32_t sym;			/* NONE for '.' */
	Expr *e;
	char op;			/* '=' or the operator of op= */
	bool provide, hidden;
	bool defined;			/* shouldDefineSym() */
	bool assert_;
} Assign;
typedef struct Cmd {
	uint8_t kind;
	uint32_t idx;			/* C_OSEC: osec, C_ISD: isd */
	Assign *as;
} Cmd;

typedef struct OSec {
	const char *name;
	Expr *addr, *alignx, *lma, *subalign;
	VVEC(Cmd) cmds;
	bool nonalloc, noload, discard, used_in_expr, has_input, live, type_set;
	bool uses_syms;			/* expressionsUseSymbols */
	uint32_t type, addralign, entsize;
	uint64_t flags;
	uint64_t va, size, offset;
	uint32_t shidx, nameoff;
	int ptload;			/* index into phdrs, -1 */
	uint32_t link, info;
	Expr *fill;			/* =fill */
	const char *phn[4];		/* :phdr names */
	uint8_t nphn;
} OSec;
static VVEC(OSec) osecs;
static VVEC(Cmd) topcmds;
static const char *entry_name;

/* PHDRS */
typedef struct {
	const char *name;
	uint32_t type;
	Expr *flags;			/* FLAGS(expr) or NULL */
	bool filehdr, phdrs;
} SPhdr;
static VVEC(SPhdr) sphdrs;
static bool have_phdrs;

/* VERSION: named nodes get version indices 2.. in order (1 is the file) */
typedef struct {
	const char *name;		/* "" for an anonymous node */
	VVEC(const char *) globs;
} VerNode;
static VVEC(VerNode) vernodes;
static VVEC(const char *) ver_locals;
static uint32_t nverdefs;		/* named nodes */

/* names referenced by expressions, in parse order (lld referencedSymbols) */
static VVEC(uint32_t) refsyms;
typedef struct { uint32_t sym; VVEC(uint32_t) refs; } Provide;
static VVEC(Provide) provides;

#define AETHER 0xfffffffdu
