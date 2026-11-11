#ifndef VCC_CC_H
#define VCC_CC_H
/*
 * vcc front end: parser + semantic analysis for GNU C11 (what the kernel
 * and its host tools use), one TU at a time, pulling tokens from pp.h.
 *
 * Everything lives in per-TU arenas and is addressed by 32-bit indices:
 *
 *   types   interned (two equal types are one index); a qualified type is
 *           (index << 3) | qualifiers, like clang's QualType.  Typedefs are
 *           not kept as sugar, except that an aligned(N) typedef makes a
 *           variant whose canonical type is the original (sizeof unchanged,
 *           _Alignof and field placement use N).
 *   records struct/union definitions with their clang (Itanium/AAPCS64)
 *           layout, computed when the definition is complete.
 *   syms    every declared object, function, typedef and enumerator; the
 *           scope chain is an id-indexed table (two-level, like pp's macro
 *           table) of the innermost binding, with the shadowed one linked.
 *   nodes   expressions and statements, 24 bytes each; lists of node
 *           indices live in a separate u32 array.
 *
 * Semantic analysis runs while parsing: every expression gets its type,
 * implicit conversions are explicit nodes, and constant expressions are
 * evaluated on demand (array sizes, bit-field widths, case labels, enum
 * values, static asserts, __builtin_choose_expr, static initializers).
 */
#include <stdbool.h>
#include <stdint.h>

#include "../pp/pp.h"

/* ---------------- types ---------------- */

enum {
	TY_VOID, TY_BOOL,
	TY_CHAR, TY_SCHAR, TY_UCHAR, TY_SHORT, TY_USHORT, TY_INT, TY_UINT,
	TY_LONG, TY_ULONG, TY_LLONG, TY_ULLONG, TY_INT128, TY_UINT128,
	TY_BFLOAT16, TY_FLOAT16, TY_FLOAT, TY_DOUBLE, TY_LDOUBLE,
	TY_FLOAT128,	/* __float128 (x86-64): binary128 */
	TY_PTR, TY_ARRAY, TY_FUNC, TY_STRUCT, TY_UNION, TY_ENUM,
	TY_VECTOR,	/* GNU vector_size: base element, n lanes */
	TY_SVE,		/* AArch64 builtin scalable type: sizeless; aux is its interned name */
	TY_NKINDS
};

enum { Q_CONST = 1, Q_VOLATILE = 2, Q_RESTRICT = 4, Q_MASK = 7 };

typedef uint32_t qty;	/* (type index << 3) | Q_* */
#define QT(i, q) (((qty)(i) << 3) | (q))
#define QI(t) ((t) >> 3)
#define QQ(t) ((t) & Q_MASK)
#define QUNQ(t) ((t) & ~(qty)Q_MASK)

enum {
	TF_VARIADIC = 1,	/* function */
	TF_NOPROTO = 2,		/* function: K&R "()" */
	TF_INCOMPLETE = 4,	/* array of unknown size */
	TF_VLA = 8,		/* variable length array (size in node n) */
	TF_ALIGNED = 16,	/* aligned typedef variant */
	TF_TRANSPARENT = 32,	/* transparent_union */
	TF_BLOCK = 64,		/* pointer: a block pointer (^, Darwin SDK prototypes only) */
	TF_ATOMIC = 128,	/* _Atomic(T): n is T's index; clang's layout (ty_atomic) */
};

typedef struct ctype {
	uint8_t kind, flags;
	uint16_t ext;		/* vector: ext_vector_type's element count (a distinct type, as clang), 0: GNU */
	uint32_t align;		/* bytes */
	uint64_t size;		/* bytes; arrays: elem size * n */
	qty base;		/* pointee / element / return / enum underlying */
	uint32_t n;		/* integer: _BitInt precision (0: standard); array length; function: params; VLA: size node */
	uint32_t aux;		/* function: index into params; record/enum: tag index */
	uint32_t canon;		/* canonical type index (itself unless an aligned variant) */
} ctype;

/* ---------------- records and enums ---------------- */

typedef struct field {
	uint32_t name;		/* identifier id, 0 for anonymous members and unnamed bit-fields */
	qty ty;
	uint64_t off;		/* bits */
	uint32_t align;		/* effective field alignment, including packed/aligned/pragma pack */
	uint16_t width;		/* bit-field width */
	uint8_t bitfield;
	uint8_t invalid;	/* its type was an error: uses say nothing more (clang) */
	uint32_t loc;
	uint32_t counted_by;	/* counted_by(member): its identifier id */
	uint32_t tdef;		/* declared with exactly this typedef name's type (sym), 0: none */
	uint32_t depr;		/* deprecated/unavailable: attrs record index, 0: none */
} field;

enum {
	RF_COMPLETE = 1, RF_UNION = 2, RF_PACKED = 4, RF_FLEX = 8, RF_ENUM = 16,
	RF_BEING_DEFINED = 32,
	RF_ENUM_FIXED = 256, RF_ENUM_DEFINED = 512, RF_ENUM_DECLARED = 1024,
	RF_MS_STRUCT = 128,	/* Microsoft allocation units for bit-fields */
	RF_COMPLEX = 64,	/* _Complex T: struct { T re, im; } (AAPCS64 passes it like that) */
};

typedef struct tag {
	uint32_t name;		/* identifier id, 0 if anonymous */
	uint32_t loc;
	uint32_t ty;		/* type index */
	uint32_t flags;		/* RF_* */
	uint32_t nfield;
	field *f;
	uint64_t size;		/* bytes */
	uint32_t align;		/* bytes */
	uint32_t maxalign;	/* aligned() on the record, bytes */
	uint32_t ualign;	/* bytes: the fields' alignment without the record's aligned()
				   (clang's unadjusted alignment: AAPCS64 argument passing) */
	uint32_t typedef_name;	/* for anonymous records: first typedef naming it */
	uint32_t enums, nenum;	/* enum: its enumerator syms (list) */
	uint32_t depr;		/* deprecated: attrs record index, 0: none */
} tag;

/* ---------------- symbols ---------------- */

enum { S_VAR, S_FUNC, S_TYPEDEF, S_ENUMCONST, S_BUILTIN };
enum { SC_NONE, SC_EXTERN, SC_STATIC, SC_AUTO, SC_REGISTER, SC_TYPEDEF };

enum {
	SF_DEFINED = 1,		/* function body / object definition (initialized) */
	SF_TENTATIVE = 2,
	SF_INLINE = 4,
	SF_USED = 8,		/* referenced */
	SF_PARAM = 16,
	SF_GLOBAL = 32,		/* file scope or extern */
	SF_GNU_INLINE = 64,
	SF_NORETURN = 128,
	SF_WEAK = 256,
	SF_ALWAYS_INLINE = 512,
	SF_NOINLINE = 1024,
	SF_THREAD = 2048,
	SF_ATTR_USED = 4096,
	SF_ADDR_TAKEN = 8192,
	SF_INLINE_NOEXTERN = 16384,	/* some declaration is 'inline' without 'extern' */
	SF_NOINSTR = 65536,	/* no_instrument_function */
	SF_OVERLOAD = 32768,	/* __attribute__((overloadable)): mangled name, member of an overload set */
	SF_LAZY = 1u << 17,	/* a header function whose body is kept as tokens, parsed only if referenced */
	SF_REF = 1u << 18,	/* named anywhere (also in sizeof/typeof): a lazy body must be parsed */
	SF_DEPRECATED = 1u << 19,	/* deprecated: attrs record's depr/deprmsg */
	SF_WUR = 1u << 20,	/* warn_unused_result / nodiscard (attrs record's wur) */
	SF_NONNULL = 1u << 21,	/* a parameter declared nonnull */
	SF_INVALID = 1u << 23,	/* declared with an error (incomplete type): uses report nothing more (clang) */
	SF_BOS_QUERY = 1u << 24,	/* function contains an object-size query */
	SF_BOS_INIT = 1u << 25,	/* local pointer has a certified immutable initializer */
	SF_UVPEND = 1u << 22,	/* -Wuninitialized: declared without an initializer, not yet assigned in the text */
};

typedef struct sym {
	uint32_t name, loc;
	uint8_t kind, sc;
	uint16_t nref;		/* local variable: references minus discarded assignments (clang's RefsMinusAssignments) */
	uint32_t flags;
	qty ty;
	uint32_t align;		/* aligned() on the declaration, bytes (0: none) */
	int64_t val;		/* enumerator value */
	uint32_t body;		/* function body / initializer node */
	uint32_t asmname;	/* string index: asm("name"), section, alias, cleanup... see attrs */
	uint32_t attrs;		/* index into attribute records (0: none) */
	uint32_t shadow;	/* binding this one shadows in the scope table */
	uint32_t depth;		/* scope depth */
	uint32_t next;		/* previous declaration of the same entity (0: none) */
	uint32_t ovl;		/* overloadable: the next function of the set (0: none); the set's
				   first function is the one bound in the scope table */
	uint32_t plb, ple;	/* function: its parameter list's first token and ')'; variable: its declaration's first and last token */
	uint32_t psyms;		/* function: its latest prototype's parameter syms (list index + 1, 0: none) */
	uint32_t tdef;		/* object/typedef: declared with exactly this typedef name's type (sym), 0: none */
	uint32_t defloc;	/* the definition's name (a redeclaration keeps the first loc), 0: loc */
} sym;

/* ---------------- nodes ---------------- */

enum {
	N_NONE,
	/* expressions */
	N_NUM,		/* a: constant pool index (integer); aux: NUM_RADIX (a literal not written in decimal), NUM_ENUM */
	N_FNUM,		/* a: constant pool index (double) */
	N_STR,		/* a: string index */
	N_VAR,		/* a: sym */
	N_MEMBER,	/* a: base (lvalue struct), b: tag, c: field index */
	N_INDEX,	/* a[b] (pointer + integer, already in that order) */
	N_CALL,		/* a: callee, b: list, c: count */
	N_BUILTIN,	/* aux: builtin id, b: list, c: count */
	N_CAST,		/* aux: cast kind, a: operand, b: an explicit cast's typedef (sym, in a body) or 0 */
	N_UNARY,	/* aux: op (T_* kind), a: operand */
	N_ADDR, N_DEREF,
	N_PREINC, N_PREDEC, N_POSTINC, N_POSTDEC,
	N_BINARY,	/* aux: op (T_* kind), a, b */
	N_LOGAND, N_LOGOR,
	N_ASSIGN,	/* aux: 0 or the compound op (T_PLUS, ...), a, b */
	N_COND,		/* a ? b : c (b == 0: GNU a ?: c) */
	N_COMMA,
	N_SIZEOF_VLA,	/* a: expression of VLA type */
	N_STMTEXPR,	/* a: compound statement */
	N_COMPLIT,	/* a: init, b: sym of the object */
	N_INIT,		/* initializer list: b: struct/union (aux 0/2): c values; array (aux 1): c (lo, hi, value) */
	N_DESIG,	/* aux: 0 field (b: tag, c: field) / 1 index range (a..b) */
	N_LABELADDR,	/* &&label: a: label */
	N_VAARG,	/* a: va_list expr */
	N_ZERO,		/* implicit zero initialization */
	N_CPLX,		/* a + b i: a _Complex value from its parts (a, b of the element type) */
	/* statements */
	N_BLOCK,	/* b: list, c: count */
	N_EXPR,		/* a */
	N_DECL,		/* a: sym, b: init or 0 */
	N_IF, N_WHILE, N_DO,
	N_FOR,		/* a: init, b: cond, c: list of [step, body] */
	N_SWITCH,	/* a: cond, b: body */
	N_CASE,		/* a: lo, b: hi (pool), c: stmt */
	N_DEFAULT,	/* c: stmt */
	N_LABEL,	/* a: label, c: stmt */
	N_GOTO,		/* a: label */
	N_GOTO_IND,	/* a: expr */
	N_BREAK, N_CONTINUE,
	N_RETURN,	/* a: expr or 0 */
	N_ASM,		/* a: asm record */
	N_NKINDS
};

enum {
	CK_NOP,		/* same representation (qualifiers, typedef variants, signedness) */
	CK_INT,		/* integer -> integer */
	CK_BOOL,	/* scalar -> _Bool */
	CK_I2F, CK_F2I, CK_F2F,
	CK_P2I, CK_I2P, CK_P2P,
	CK_DECAY,	/* array -> pointer to first element */
	CK_FDECAY,	/* function -> pointer */
	CK_VOID,	/* -> void */
	CK_UNION,	/* -> transparent union / GNU cast to union */
	CK_NULL,	/* null pointer constant -> pointer */
	CK_SPLAT,	/* scalar (already the element type) -> every lane of an ext_vector_type */
	CK_VBITS,	/* GNU vector -> scalar of the same size (the bits) */
	CK_VADDR,	/* GNU vector rvalue -> pointer to its first lane (v[i] on an rvalue) */
};

enum { NUM_RADIX = 1, NUM_ENUM = 2 };	/* N_NUM aux: spelled not in decimal; an enumerator's value */
enum { NF_LVALUE = 1, NF_BITFIELD = 2, NF_PAREN = 4, NF_NPC = 8,
       NF_ERR = 16,
       NF_MACRO = 32, NF_BOUNDS = 64, NF_OVF = 128 };	/* NF_OVF: a constant subexpression overflowed (-Winteger-overflow given once) */	/* NF_BOUNDS: a[CONST] already checked (-Warray-bounds) */	/* NF_MACRO: the operator came from a macro expansion (no style warnings) */	/* NF_ERR: stands in for an expression already diagnosed (no follow-on errors) */

typedef struct node {
	uint8_t op, flags;
	uint16_t aux;
	qty ty;
	uint32_t loc;
	uint32_t a, b, c;
} node;

/* ---------------- the TU ---------------- */

/* TLS models, in the order clang strengthens them (the larger wins) */
enum { VCC_TLS_GD = 1, VCC_TLS_LD, VCC_TLS_IE, VCC_TLS_LE };
static inline int vcc_tls_model(const char *m)
{
	return !strcmp(m, "global-dynamic") ? VCC_TLS_GD : !strcmp(m, "local-dynamic") ? VCC_TLS_LD :
	       !strcmp(m, "initial-exec") ? VCC_TLS_IE : !strcmp(m, "local-exec") ? VCC_TLS_LE : 0;
}

typedef struct cc_opts {	/* fields before outfd: parse options, all in the decl cache key */
	int target;		/* PP_LINUX_AARCH64 / PP_DARWIN / PP_WASM64 */
	int unsigned_char;
enum { CC_DISABLED_BUILTIN_WORDS = 6 };	/* generated libbuiltins.inc indices */

	int ms_anon;		/* -fms-anonymous-structs / -fms-extensions: "struct S;" members */
	int short_wchar;	/* -fshort-wchar */
	int std;		/* -std=: 89, 99, 11, 17, 23 (0: gnu17) */
	int implicit_int;	/* implicit int allowed (-std=gnu89, -Wno-error=implicit-int) */
	int gnu89_inline;	/* -std=gnu89 or -fgnu89-inline: plain inline is gnu_inline */
	int instrument;		/* -finstrument-functions */
	int vcc_kernel;		/* -fvcc-kernel (or -D__KERNEL__): error() calls left after folding are dropped */
	int lse;		/* target has LSE atomics (else ldaxr/stlxr loops) */
	int general_regs_only;	/* -mgeneral-regs-only: no FP/SIMD registers (variadic: no v0-v7 save area) */
	int no_builtin;		/* -ffreestanding / -fno-builtin: abs() etc. are plain calls */
	uint64_t disabled_builtins[CC_DISABLED_BUILTIN_WORDS]; /* -fno-builtin-NAME */
	int wasm_sjlj;		/* -mllvm -wasm-enable-sjlj: setjmp/longjmp through wasm exceptions */
	int implicit_fn;	/* implicit function declarations allowed (... implicit-function-declaration) */
	int strict_std;		/* -std=cNN (not gnuNN): clang's GNU-only library functions are not known */
	int conv_warn;		/* -Wno-error= (or -Wno-) of: 1 int-conversion, 2 incompatible-pointer-types,
				   4 incompatible-function-pointer-types: a warning, not an error */
	int dump_layouts;	/* append _Static_assert checks for clang to verify */
	int outfd;
	int emit;		/* generate code (emit/): 1 = assembly text to outfd, 2 = object into obj */
	int opt;		/* -O level for code generation: 0 (default, -O0), 1 (-O1, -Og), 2 (-O2, -O3, -Os, -Oz, -O) */
	int sections;		/* 1 -ffunction-sections, 2 -fdata-sections (ELF) */
	int debug;		/* -g: DWARF line tables (ELF) */
	int cfi;		/* -fasynchronous-unwind-tables: .cfi_* (ELF), .eh_frame from the assembler */
	const char *dbg_dir, *dbg_name;	/* the compilation directory and the main file, as named */
	int tlsmodel;		/* -ftls-model=: VCC_TLS_*, 0: the default */
	int pic;		/* ELF: 0 none (-fno-pic), 1 PIE (-fpie, the hosted default), 2 PIC (-fpic) */
	int nthreads;		/* emitter threads (0: 1) */
	struct ElfObj *obj;	/* emit == 2: the object (reset first; the caller finishes it) */
	struct As *as;		/* emit == 2: the assembler bound to obj (one per thread) */
	struct vbuf *wasm_obj;	/* emit == 2 for wasm64: the object's bytes (arch/wasm/as) */
} cc_opts;

typedef struct cc cc;

/* parse and analyse the whole TU; returns the number of errors */
void cc_disable_builtin(uint64_t *bits, const char *name);

int cc_run(pp *p, const cc_opts *o);
/* lazy bodies kept and parsed so far (all TUs of the process) */
void cc_lz_stats(char *buf, size_t n);
/* VCC_DRCOV: header-cache hits that start and end at file-scope declaration boundaries */
void cc_drcov_stats(char *buf, size_t n);
/* VCC_DR / VCC_DRREC: per-region declaration replay (cc/replay.c) */
void cc_dr_stats(char *buf, size_t n);

#endif
