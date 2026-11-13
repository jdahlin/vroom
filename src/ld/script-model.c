/* The parsed linker script: expressions, input section descriptions, output sections, PHDRS, VERSION nodes and symbol assignments. */

/* ---------------- linker script ---------------- */

typedef struct ld_expression ld_expression;
enum {
	LD_EXPRESSION_NUMBER, LD_EXPRESSION_SYMBOL, LD_EXPRESSION_DOT, LD_EXPRESSION_ADDRESS, LD_EXPRESSION_LOAD_ADDRESS, LD_EXPRESSION_SIZEOF, LD_EXPRESSION_ALIGNOF, LD_EXPRESSION_ALIGN_ONE_ARGUMENT, LD_EXPRESSION_ALIGN_TWO_ARGUMENTS,
	LD_EXPRESSION_ABSOLUTE, LD_EXPRESSION_BINARY, LD_EXPRESSION_NEGATE, LD_EXPRESSION_BITWISE_NOT, LD_EXPRESSION_LOGICAL_NOT, LD_EXPRESSION_TERNARY, LD_EXPRESSION_ASSERT, LD_EXPRESSION_DEFINED, LD_EXPRESSION_MAX, LD_EXPRESSION_MIN,
	LD_EXPRESSION_LOG2CEIL, LD_EXPRESSION_SIZEOF_HEADERS,
};
struct ld_expression {
	uint8_t op;
	char bop[4];
	ld_expression *a, *b, *c;
	uint64_t num;
	uint32_t sym;			/* LD_EXPRESSION_SYMBOL/LD_EXPRESSION_DEFINED: global slot; LD_EXPRESSION_ADDRESS etc: osec */
	const char *msg;
};

typedef struct {
	char pat[16][64];		/* not used; patterns live in Pat */
} Unused;

typedef struct {
	const char **globs;		/* section name globs */
	uint32_t nglobs;
	uint8_t sort;			/* LD_SORT_NONE, LD_SORT_NAME, LD_SORT_ALIGNMENT */
} ld_pattern;
enum { LD_SORT_NONE, LD_SORT_NAME, LD_SORT_ALIGNMENT };

typedef struct ld_input_section_description {
	uint32_t osec;
	ld_pattern *pats;
	uint32_t npats;
	bool keep;
	VVEC(uint32_t) secs;		/* ISec ids, in final order */
	uint32_t thunksec;		/* ISec of this ld_input_section_description's thunk section, or LD_NONE */
	uint64_t thunk_off;		/* pass-0 isecLimit */
	struct a64_ld_call *calls;		/* CALL26/JUMP26 relocations, built by thunk pass 0 */
	uint32_t ncalls;
} ld_input_section_description;
static VVEC(ld_input_section_description) isds;

enum { LD_COMMAND_ASSIGNMENT, LD_COMMAND_OUTPUT_SECTION, LD_COMMAND_INPUT_SECTION_DESCRIPTION };
typedef struct ld_assignment {
	uint32_t sym;			/* LD_NONE for '.' */
	ld_expression *e;
	char op;			/* '=' or the operator of op= */
	bool provide, hidden;
	bool defined;			/* shouldDefineSym() */
	bool assert_;
} ld_assignment;
typedef struct ld_command {
	uint8_t kind;
	uint32_t idx;			/* LD_COMMAND_OUTPUT_SECTION: osec, LD_COMMAND_INPUT_SECTION_DESCRIPTION: isd */
	ld_assignment *as;
} ld_command;

typedef struct ld_output_section {
	const char *name;
	ld_expression *addr, *alignx, *lma, *subalign;
	VVEC(ld_command) cmds;
	bool nonalloc, noload, discard, used_in_expr, has_input, live, type_set;
	bool uses_syms;			/* expressionsUseSymbols */
	uint32_t type, addralign, entsize;
	uint64_t flags;
	uint64_t va, size, offset;
	uint32_t shidx, nameoff;
	int ptload;			/* index into the context's program_headers, -1 */
	uint32_t link, info;
	ld_expression *fill;			/* =fill */
	const char *phn[4];		/* :phdr names */
	uint8_t nphn;
} ld_output_section;
static VVEC(ld_output_section) osecs;
static VVEC(ld_command) topcmds;
static const char *entry_name;

/* PHDRS */
typedef struct {
	const char *name;
	uint32_t type;
	ld_expression *flags;			/* FLAGS(expr) or NULL */
	bool filehdr, phdrs;
} ld_script_segment;
static VVEC(ld_script_segment) sphdrs;
static bool have_phdrs;

/* VERSION: named nodes get version indices 2.. in order (1 is the file) */
typedef struct {
	const char *name;		/* "" for an anonymous node */
	VVEC(const char *) globs;
} ld_version_node;
static VVEC(ld_version_node) vernodes;
static VVEC(const char *) ver_locals;
static uint32_t nverdefs;		/* named nodes */

/* names referenced by expressions, in parse order (lld referencedSymbols) */
static VVEC(uint32_t) refsyms;
typedef struct { uint32_t sym; VVEC(uint32_t) refs; } ld_provide;
static VVEC(ld_provide) provides;

#define LD_OUTPUT_SECTION_AETHER 0xfffffffdu
