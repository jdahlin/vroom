#ifndef VCC_FRONTEND_C_CC_H
#define VCC_FRONTEND_C_CC_H
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

#include "frontend/pp/pp.h"
#include "arch/x64/x86features.h"

/* ---------------- types ---------------- */

enum {
	AST_TYPE_VOID, AST_TYPE_BOOL,
	AST_TYPE_CHAR, AST_TYPE_SIGNED_CHAR, AST_TYPE_UNSIGNED_CHAR, AST_TYPE_SHORT, AST_TYPE_UNSIGNED_SHORT, AST_TYPE_INT, AST_TYPE_UNSIGNED_INT,
	AST_TYPE_LONG, AST_TYPE_UNSIGNED_LONG, AST_TYPE_LONG_LONG, AST_TYPE_UNSIGNED_LONG_LONG, AST_TYPE_INT128, AST_TYPE_UINT128,
	AST_TYPE_BFLOAT16, AST_TYPE_FLOAT16, AST_TYPE_FLOAT, AST_TYPE_DOUBLE, AST_TYPE_LONG_DOUBLE,
	AST_TYPE_FLOAT128,	/* __float128 (x86-64): binary128 */
	AST_TYPE_POINTER, AST_TYPE_ARRAY, AST_TYPE_FUNCTION, AST_TYPE_STRUCT, AST_TYPE_UNION, AST_TYPE_ENUM,
	AST_TYPE_VECTOR,	/* GNU vector_size: base element, n lanes */
	AST_TYPE_SVE_BUILTIN,		/* AArch64 builtin scalable type: sizeless; aux is its interned name */
	AST_TYPE_BITINT, AST_TYPE_UNSIGNED_BITINT, /* bit-precise integers wider than 128 bits */
	AST_TYPE_KIND_COUNT
};

enum { AST_QUALIFIER_CONST = 1, AST_QUALIFIER_VOLATILE = 2, AST_QUALIFIER_RESTRICT = 4, AST_QUALIFIER_MASK = 7 };

typedef uint32_t ast_qualified_type;	/* (type index << 3) | Q_* */
#define AST_QUALIFIED_TYPE(i, q) (((ast_qualified_type)(i) << 3) | (q))
#define AST_QUALIFIED_TYPE_INDEX(t) ((t) >> 3)
#define AST_QUALIFIED_TYPE_QUALIFIERS(t) ((t) & AST_QUALIFIER_MASK)
#define AST_QUALIFIED_TYPE_UNQUALIFIED(t) ((t) & ~(ast_qualified_type)AST_QUALIFIER_MASK)

enum {
	AST_TYPE_FLAG_VARIADIC = 1,	/* function */
	AST_TYPE_FLAG_NO_PROTOTYPE = 2,		/* function: K&R "()" */
	AST_TYPE_FLAG_INCOMPLETE = 4,	/* array of unknown size */
	AST_TYPE_FLAG_VLA = 8,		/* variable length array (size in node n) */
	AST_TYPE_FLAG_ALIGNED = 16,	/* aligned typedef variant */
	AST_TYPE_FLAG_TRANSPARENT = 32,	/* transparent_union */
	AST_TYPE_FLAG_BLOCK = 64,		/* pointer: a block pointer (^, Darwin SDK prototypes only) */
	AST_TYPE_FLAG_ATOMIC = 128,	/* _Atomic(T): n is T's index; clang's layout (ty_atomic) */
};

typedef struct ctype {
	uint8_t kind, flags;
	uint16_t ext;		/* vector: ext_vector_type's element count (a distinct type, as clang), 0: GNU */
	uint32_t align;		/* bytes */
	uint64_t size;		/* bytes; arrays: elem size * n */
	ast_qualified_type base;		/* pointee / element / return / enum underlying */
	uint32_t n;		/* integer: _BitInt precision (0: standard); array length; function: params; VLA: size node */
	uint32_t aux;		/* function: index into params; record/enum: tag index */
	uint32_t canon;		/* canonical type index (itself unless an aligned variant) */
} ctype;

/*
 * x86-64 calling conventions.  A function type's `ext` is 0 for the target's own convention (System V
 * on Linux, Microsoft x64 on Windows) or the other one, written as ms_abi or sysv_abi; it is part of
 * the type (clang: incompatible function pointer types).  cc_func_abi (ccint.h) gives the effective
 * one.  The va_list kinds: __builtin_va_list (System V: struct __va_list_tag[1]; Windows: char *) and
 * __builtin_ms_va_list (char *, every x86-64 OS); cc_valist_is_ms tells them apart.
 */
enum { CC_FNABI_DEFAULT, CC_FNABI_MS, CC_FNABI_SYSV };
/* dllimport / dllexport on a declaration (attrs.dll) */
enum { CC_DLLIMPORT = 1, CC_DLLEXPORT = 2 };

/* ---------------- records and enums ---------------- */

typedef struct field {
	uint32_t name;		/* identifier id, 0 for anonymous members and unnamed bit-fields */
	ast_qualified_type ty;
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
	AST_RECORD_FLAG_COMPLETE = 1, AST_RECORD_FLAG_UNION = 2, AST_RECORD_FLAG_PACKED = 4, AST_RECORD_FLAG_FLEXIBLE = 8, AST_RECORD_FLAG_ENUM = 16,
	AST_RECORD_FLAG_BEING_DEFINED = 32,
	AST_RECORD_FLAG_ENUM_FIXED = 256, AST_RECORD_FLAG_ENUM_DEFINED = 512, AST_RECORD_FLAG_ENUM_DECLARED = 1024,
	AST_RECORD_FLAG_MS_STRUCT = 128,	/* Microsoft allocation units for bit-fields */
	AST_RECORD_FLAG_COMPLEX = 64,	/* _Complex T: struct { T re, im; } (AAPCS64 passes it like that) */
	AST_RECORD_FLAG_GCC_STRUCT = 2048,	/* gcc_struct: not the Microsoft layout, whatever -mms-bitfields and #pragma ms_struct say */
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

enum { AST_SYMBOL_VARIABLE, AST_SYMBOL_FUNCTION, AST_SYMBOL_TYPEDEF, AST_SYMBOL_ENUMERATOR, AST_SYMBOL_BUILTIN };
enum { AST_STORAGE_NONE, AST_STORAGE_EXTERN, AST_STORAGE_STATIC, AST_STORAGE_AUTO, AST_STORAGE_REGISTER, AST_STORAGE_TYPEDEF };

enum {
	AST_SYMBOL_FLAG_DEFINED = 1,		/* function body / object definition (initialized) */
	AST_SYMBOL_FLAG_TENTATIVE = 2,
	AST_SYMBOL_FLAG_INLINE = 4,
	AST_SYMBOL_FLAG_USED = 8,		/* referenced */
	AST_SYMBOL_FLAG_PARAMETER = 16,
	AST_SYMBOL_FLAG_GLOBAL = 32,		/* file scope or extern */
	AST_SYMBOL_FLAG_GNU_INLINE = 64,
	AST_SYMBOL_FLAG_NORETURN = 128,
	AST_SYMBOL_FLAG_WEAK = 256,
	AST_SYMBOL_FLAG_ALWAYS_INLINE = 512,
	AST_SYMBOL_FLAG_NOINLINE = 1024,
	AST_SYMBOL_FLAG_THREAD = 2048,
	AST_SYMBOL_FLAG_ATTRIBUTE_USED = 4096,
	AST_SYMBOL_FLAG_ADDRESS_TAKEN = 8192,
	AST_SYMBOL_FLAG_INLINE_WITHOUT_EXTERN = 16384,	/* some declaration is 'inline' without 'extern' */
	AST_SYMBOL_FLAG_NO_INSTRUMENT = 65536,	/* no_instrument_function */
	AST_SYMBOL_FLAG_OVERLOAD = 32768,	/* __attribute__((overloadable)): mangled name, member of an overload set */
	AST_SYMBOL_FLAG_LAZY = 1u << 17,	/* a header function whose body is kept as tokens, parsed only if referenced */
	AST_SYMBOL_FLAG_REFERENCED = 1u << 18,	/* named anywhere (also in sizeof/typeof): a lazy body must be parsed */
	AST_SYMBOL_FLAG_DEPRECATED = 1u << 19,	/* deprecated: attrs record's depr/deprmsg */
	AST_SYMBOL_FLAG_WARN_UNUSED_RESULT = 1u << 20,	/* warn_unused_result / nodiscard (attrs record's wur) */
	AST_SYMBOL_FLAG_NONNULL = 1u << 21,	/* a parameter declared nonnull */
	AST_SYMBOL_FLAG_INVALID = 1u << 23,	/* declared with an error (incomplete type): uses report nothing more (clang) */
	AST_SYMBOL_FLAG_OBJECT_SIZE_QUERY = 1u << 24,	/* function contains an object-size query */
	AST_SYMBOL_FLAG_CONSTEXPR = 1u << 26,	/* C23 constant object: initializer is a constant expression */
	AST_SYMBOL_FLAG_EXTERNAL_DECLARATION = 1u << 27,	/* function: a file-scope declaration without inline, or extern (C99 6.7.4p7: an inline definition is external) */
	AST_SYMBOL_FLAG_OBJECT_SIZE_INITIALIZER = 1u << 25,	/* local pointer has a certified immutable initializer */
	AST_SYMBOL_FLAG_UNINITIALIZED_PENDING = 1u << 22,	/* -Wuninitialized: declared without an initializer, not yet assigned in the text */
};

typedef struct sym {
	uint32_t name, loc;
	uint8_t kind, sc;
	uint16_t nref;		/* local variable: references minus discarded assignments (clang's RefsMinusAssignments) */
	uint32_t flags;
	ast_qualified_type ty;
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
	AST_NODE_NONE,
	/* expressions */
	AST_NODE_NUMBER,		/* a: constant pool index (integer); aux: AST_NUMBER_RADIX (a literal not written in decimal), AST_NUMBER_ENUMERATOR */
	AST_NODE_FLOAT_NUMBER,		/* a: constant pool index (double) */
	AST_NODE_BITINT_NUMBER,	/* a: immutable LE32 words in lists, b: full storage word count */
	AST_NODE_STRING,		/* a: string index */
	AST_NODE_VARIABLE,		/* a: sym */
	AST_NODE_MEMBER,	/* a: base (lvalue struct), b: tag, c: field index */
	AST_NODE_INDEX,	/* a[b] (pointer + integer, already in that order) */
	AST_NODE_CALL,		/* a: callee, b: list, c: count */
	AST_NODE_BUILTIN,	/* aux: builtin id, b: list, c: count */
	AST_NODE_CAST,		/* aux: cast kind, a: operand, b: an explicit cast's typedef (sym, in a body) or 0; c: folded FP emit value or 0 */
	AST_NODE_UNARY,	/* aux: op (T_* kind), a: operand */
	AST_NODE_ADDRESS, AST_NODE_DEREFERENCE,
	AST_NODE_PRE_INCREMENT, AST_NODE_PRE_DECREMENT, AST_NODE_POST_INCREMENT, AST_NODE_POST_DECREMENT,
	AST_NODE_BINARY,	/* aux: op (T_* kind), a, b */
	AST_NODE_LOGICAL_AND, AST_NODE_LOGICAL_OR,
	AST_NODE_ASSIGN,	/* aux: 0 or the compound op (LEX_TOKEN_PLUS, ...), a, b */
	AST_NODE_CONDITIONAL,		/* a ? b : c (b == 0: GNU a ?: c) */
	AST_NODE_COMMA,
	AST_NODE_SIZEOF_VLA,	/* a: expression, b: VLA type; aux=1 captures a new type-name bound */
	AST_NODE_STATEMENT_EXPRESSION,	/* a: compound statement */
	AST_NODE_COMPOUND_LITERAL,	/* a: init, b: sym of the object */
	AST_NODE_INITIALIZER,		/* initializer list: b: struct/union (aux 0/2): c values; array (aux 1): c (lo, hi, value) */
	AST_NODE_DESIGNATOR,	/* aux: 0 field (b: tag, c: field) / 1 index range (a..b) */
	AST_NODE_LABEL_ADDRESS,	/* &&label: a: label */
	AST_NODE_VA_ARG,	/* a: va_list expr */
	AST_NODE_ZERO,		/* implicit zero initialization */
	AST_NODE_COMPLEX,		/* a + b i: a _Complex value from its parts (a, b of the element type) */
	/* statements */
	AST_NODE_BLOCK,	/* b: list, c: count */
	AST_NODE_EXPRESSION,		/* a */
	AST_NODE_DECLARATION,		/* a: sym, b: init or 0; aux=1: initializer supplies inferred-type bounds */
	AST_NODE_IF, AST_NODE_WHILE, AST_NODE_DO,
	AST_NODE_FOR,		/* a: init, b: cond, c: list of [step, body] */
	AST_NODE_SWITCH,	/* a: cond, b: body */
	AST_NODE_CASE,		/* a: lo, b: hi (pool), c: stmt */
	AST_NODE_DEFAULT,	/* c: stmt */
	AST_NODE_LABEL,	/* a: label, c: stmt */
	AST_NODE_GOTO,		/* a: label */
	AST_NODE_GOTO_INDIRECT,	/* a: expr */
	AST_NODE_BREAK, AST_NODE_CONTINUE,
	AST_NODE_RETURN,	/* a: expr or 0 */
	AST_NODE_ASM,		/* a: asm record */
	AST_NODE_KIND_COUNT
};

enum {
	AST_CAST_NOP,		/* same representation (qualifiers, typedef variants, signedness) */
	AST_CAST_INTEGER,		/* integer -> integer */
	AST_CAST_BOOL,	/* scalar -> _Bool */
	AST_CAST_INTEGER_TO_FLOAT, AST_CAST_FLOAT_TO_INTEGER, AST_CAST_FLOAT_TO_FLOAT,
	AST_CAST_POINTER_TO_INTEGER, AST_CAST_INTEGER_TO_POINTER, AST_CAST_POINTER_TO_POINTER,
	AST_CAST_ARRAY_DECAY,	/* array -> pointer to first element */
	AST_CAST_FUNCTION_DECAY,	/* function -> pointer */
	AST_CAST_VOID,	/* -> void */
	AST_CAST_UNION,	/* -> transparent union / GNU cast to union */
	AST_CAST_NULL_POINTER,	/* null pointer constant -> pointer */
	AST_CAST_SPLAT,	/* scalar (already the element type) -> every lane of an ext_vector_type */
	AST_CAST_BIT_CAST,	/* __builtin_bit_cast: a is the source object, without array decay */
	AST_CAST_VECTOR_BITS,	/* GNU vector -> scalar of the same size (the bits) */
	AST_CAST_VECTOR_ADDRESS,	/* GNU vector rvalue -> pointer to its first lane (v[i] on an rvalue) */
};

enum { AST_NUMBER_RADIX = 1, AST_NUMBER_ENUMERATOR = 2, AST_NUMBER_NOT_ICE = 4, AST_NUMBER_NOT_CONSTEXPR = 8 };	/* AST_NODE_NUMBER aux: spelled not in decimal; an enumerator's value */
enum { AST_NODE_FLAG_LVALUE = 1, AST_NODE_FLAG_BITFIELD = 2, AST_NODE_FLAG_PARENTHESIZED = 4, AST_NODE_FLAG_NULL_POINTER_CONSTANT = 8,
       AST_NODE_FLAG_ERROR = 16,
       AST_NODE_FLAG_FROM_MACRO = 32, AST_NODE_FLAG_BOUNDS_CHECKED = 64, AST_NODE_FLAG_OVERFLOW_REPORTED = 128 };	/* AST_NODE_FLAG_OVERFLOW_REPORTED: a constant subexpression overflowed (-Winteger-overflow given once) */	/* AST_NODE_FLAG_BOUNDS_CHECKED: a[CONST] already checked (-Warray-bounds) */	/* AST_NODE_FLAG_FROM_MACRO: the operator came from a macro expansion (no style warnings) */	/* AST_NODE_FLAG_ERROR: stands in for an expression already diagnosed (no follow-on errors) */

/* AST_NODE_COMMA result facts, attached by semantic lowering. NaNs are permitted. */
enum { AST_NODE_COMMA_FP_NONNEGATIVE = 1 };

typedef struct node {
	uint8_t op, flags;
	uint16_t aux;
	ast_qualified_type ty;
	uint32_t loc;
	uint32_t a, b, c;
} node;

/* ---------------- the TU ---------------- */

/* TLS models, in the order clang strengthens them (the larger wins) */
enum { CC_TLS_GLOBAL_DYNAMIC = 1, CC_TLS_LOCAL_DYNAMIC, CC_TLS_INITIAL_EXEC, CC_TLS_LOCAL_EXEC };
static inline int cc_tls_model_parse(const char *m)
{
	return !strcmp(m, "global-dynamic") ? CC_TLS_GLOBAL_DYNAMIC : !strcmp(m, "local-dynamic") ? CC_TLS_LOCAL_DYNAMIC :
	       !strcmp(m, "initial-exec") ? CC_TLS_INITIAL_EXEC : !strcmp(m, "local-exec") ? CC_TLS_LOCAL_EXEC : 0;
}

enum { CC_DISABLED_BUILTIN_WORDS = 6 };	/* generated libbuiltins.inc indices */

typedef struct cc_options {	/* fields before outfd: parse options, all in the decl cache key */
	int target;		/* PP_LINUX_AARCH64 / PP_DARWIN / PP_WASM64 / PP_WASM32 / PP_LINUX_X86_64 / PP_WIN64 */
	int unsigned_char;
	int ms_anon;		/* -fms-anonymous-structs / -fms-extensions: "struct S;" members */
	int strict_flex_arrays; /* -fstrict-flex-arrays=0..3 */
	int short_wchar;	/* -fshort-wchar (always on for Windows: wchar_t is unsigned short) */
	int ms_bitfields;	/* -mms-bitfields (the default for Windows): records without gcc_struct get the MS layout */
	int std;		/* -std=: 89, 99, 11, 17, 23 (0: gnu17) */
	int implicit_int;	/* implicit int allowed (-std=gnu89, -Wno-error=implicit-int) */
	int gnu89_inline;	/* -std=gnu89 or -fgnu89-inline: plain inline is gnu_inline */
	int instrument;		/* -finstrument-functions */
	int vcc_kernel;		/* -fvcc-kernel (or -D__KERNEL__): error() calls left after folding are dropped */
	int lse;		/* target has LSE atomics (else ldaxr/stlxr loops) */
	x86_features x86_features, x86_disabled, x86_raw, x86_override_on, x86_override_off;
	int x86_features_set;	/* explicitly resolved by the driver; zero uses x86-64 */
	int general_regs_only;	/* -mgeneral-regs-only: no FP/SIMD registers (variadic: no v0-v7 save area) */
	int fast_math;		/* full fast-math permits finite, unsigned-zero root transforms */
	int no_fp_contract;	/* -ffp-contract=off: a * b + c is two operations (default on, as clang: fused within an expression) */
	int no_jump_tables;	/* -fno-jump-tables: switches are compare trees */
	int no_builtin;		/* -ffreestanding / -fno-builtin: abs() etc. are plain calls */
	uint64_t disabled_builtins[CC_DISABLED_BUILTIN_WORDS]; /* -fno-builtin-NAME */
	int wasm_sjlj;		/* -mllvm -wasm-enable-sjlj: setjmp/longjmp through wasm exceptions */
	int implicit_fn;	/* implicit function declarations allowed (... implicit-function-declaration) */
	int c2y;		/* C2y additions, with the C23 keyword set */
	int strict_std;		/* -std=cNN (not gnuNN): clang's GNU-only library functions are not known */
	int conv_warn;		/* -Wno-error= (or -Wno-) of: 1 int-conversion, 2 incompatible-pointer-types,
				   4 incompatible-function-pointer-types: a warning, not an error */
	int dump_layouts;	/* append _Static_assert checks for clang to verify */
	/* Before outfd so declaration-cache identity includes the frame policy. */
	int omit_frame_pointer; /* x86: permit frame-free leaf omission; explicit flags win */
	int sibling_calls; /* 0: optimization-level default, 1: explicit enable, -1: explicit disable */
	int outfd;
	const char *field_uses;	/* VCC_FIELD_USES: the selection file (test aid; after outfd: not in the cache key) */
	int lint;		/* vpp --lint: the style checks (frontend/c/naming-lint.c); not in the decl cache key */
	int emit;		/* generate code (codegen/): 1 = assembly text to outfd, 2 = object into obj */
	int opt;		/* -O level for code generation: 0 (default, -O0), 1 (-O1, -Og), 2 (-O2, -O3, -Os, -Oz, -O) */
	int wrapv;		/* -fwrapv, -fno-strict-overflow: signed overflow wraps (VIR sets no nsw) */
	int sections;		/* 1 -ffunction-sections, 2 -fdata-sections (ELF) */
	int debug;		/* -g: DWARF line tables (ELF, COFF) */
	int dbg_version;	/* -gdwarf-N: 2..5; 0 the target's default (Windows 4, else 5) */
	int cfi;		/* -fasynchronous-unwind-tables: .cfi_* (ELF), .eh_frame from the assembler */
	int no_seh;		/* Windows: -fno-asynchronous-unwind-tables without -funwind-tables: no .seh_* (no .pdata/.xdata) */
	const char *dbg_dir, *dbg_name;	/* the compilation directory and the main file, as named */
	int tlsmodel;		/* -ftls-model=: VCC_TLS_*, 0: the default */
	int pic;		/* ELF: 0 none (-fno-pic), 1 PIE (-fpie, the hosted default), 2 PIC (-fpic) */
	/* x86-64 hardening, as clang (the kernel): xcf bits 1 -fcf-protection=branch (endbr64, IBT
	   note), 2 -mfunction-return=thunk-extern, 4 -mretpoline-external-thunk, 8
	   -mindirect-branch-cs-prefix; -fpatchable-function-entry=N,M; -fstack-protector (1),
	   -strong (2), -all (3) with its guard (sp_gs: %gs, sp_sym: the symbol, else %fs:40);
	   -ftrivial-auto-var-init=zero */
	int xcf, pfe_n, pfe_m, sprot, sp_gs, autozero;
	const char *sp_sym;
	int nthreads;		/* emitter threads (0: 1) */
	struct ElfObj *obj;	/* emit == 2: the object (reset first; the caller finishes it) */
	struct as_context *as;		/* emit == 2: the assembler bound to obj (one per thread) */
	struct vbuffer *wasm_obj;	/* emit == 2 for wasm64: the object's bytes (arch/wasm/as) */
} cc_options;

void cc_builtin_disable(uint64_t *bits, const char *name);

typedef struct cc cc;

/* parse and analyse the whole TU; returns the number of errors */
int cc_translation_unit_run(pp *p, const cc_options *o);
/* lazy bodies kept and parsed so far (all TUs of the process) */
void cc_lazy_body_statistics(char *buf, size_t n);
/* VCC_DRCOV: header-cache hits that start and end at file-scope declaration boundaries */
void cc_replay_coverage_statistics(char *buf, size_t n);
/* VCC_DR / VCC_DRREC: per-region declaration replay (frontend/c/replay.c) */
void cc_replay_statistics(char *buf, size_t n);

#endif
