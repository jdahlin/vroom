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

#include "pp.h"

/* ---------------- types ---------------- */

enum {
	TY_VOID, TY_BOOL,
	TY_CHAR, TY_SCHAR, TY_UCHAR, TY_SHORT, TY_USHORT, TY_INT, TY_UINT,
	TY_LONG, TY_ULONG, TY_LLONG, TY_ULLONG, TY_INT128, TY_UINT128,
	TY_FLOAT16, TY_FLOAT, TY_DOUBLE, TY_LDOUBLE,
	TY_PTR, TY_ARRAY, TY_FUNC, TY_STRUCT, TY_UNION, TY_ENUM,
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
};

typedef struct ctype {
	uint8_t kind, flags;
	uint16_t _pad;
	uint32_t align;		/* bytes */
	uint64_t size;		/* bytes; arrays: elem size * n */
	qty base;		/* pointee / element / return / enum underlying */
	uint32_t n;		/* array length; function: number of params; VLA: size node */
	uint32_t aux;		/* function: index into params; record/enum: tag index */
	uint32_t canon;		/* canonical type index (itself unless an aligned variant) */
} ctype;

/* ---------------- records and enums ---------------- */

typedef struct field {
	uint32_t name;		/* identifier id, 0 for anonymous members and unnamed bit-fields */
	qty ty;
	uint64_t off;		/* bits */
	uint16_t width;		/* bit-field width */
	uint8_t bitfield;
	uint8_t _pad;
	uint32_t loc;
	uint32_t counted_by;	/* counted_by(member): its identifier id */
} field;

enum {
	RF_COMPLETE = 1, RF_UNION = 2, RF_PACKED = 4, RF_FLEX = 8, RF_ENUM = 16,
	RF_BEING_DEFINED = 32,
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
	uint32_t typedef_name;	/* for anonymous records: first typedef naming it */
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
};

typedef struct sym {
	uint32_t name, loc;
	uint8_t kind, sc;
	uint16_t _pad;
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
} sym;

/* ---------------- nodes ---------------- */

enum {
	N_NONE,
	/* expressions */
	N_NUM,		/* a: constant pool index (integer) */
	N_FNUM,		/* a: constant pool index (double) */
	N_STR,		/* a: string index */
	N_VAR,		/* a: sym */
	N_MEMBER,	/* a: base (lvalue struct), b: tag, c: field index */
	N_INDEX,	/* a[b] (pointer + integer, already in that order) */
	N_CALL,		/* a: callee, b: list, c: count */
	N_BUILTIN,	/* aux: builtin id, b: list, c: count */
	N_CAST,		/* aux: cast kind, a: operand */
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
	N_INIT,		/* initializer list: b: list of (designator node, value) pairs, c: count */
	N_DESIG,	/* aux: 0 field (b: tag, c: field) / 1 index range (a..b) */
	N_LABELADDR,	/* &&label: a: label */
	N_VAARG,	/* a: va_list expr */
	N_ZERO,		/* implicit zero initialization */
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
};

enum { NF_LVALUE = 1, NF_BITFIELD = 2, NF_PAREN = 4, NF_NPC = 8 };

typedef struct node {
	uint8_t op, flags;
	uint16_t aux;
	qty ty;
	uint32_t loc;
	uint32_t a, b, c;
} node;

/* ---------------- the TU ---------------- */

typedef struct cc_opts {
	int target;		/* PP_LINUX_AARCH64 / PP_DARWIN */
	int unsigned_char;
	int ms_anon;		/* -fms-anonymous-structs / -fms-extensions: "struct S;" members */
	int short_wchar;	/* -fshort-wchar */
	int dump_layouts;	/* append _Static_assert checks for clang to verify */
	int outfd;
	int emit;		/* generate code (emit.c): 1 = assembly text to outfd */
	int nthreads;		/* emitter threads (0: 1) */
} cc_opts;

typedef struct cc cc;

/* parse and analyse the whole TU; returns the number of errors */
int cc_run(pp *p, const cc_opts *o);

#endif
