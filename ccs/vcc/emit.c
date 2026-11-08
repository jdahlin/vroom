/*
 * vcc code generator: arm64 (AAPCS64, Linux ELF) straight from cc.c's AST.
 *
 * No IR, no SSA, no optimisation passes.  What the kernel needs from an
 * -O0-class compiler, and nothing more:
 *
 *   - integer constant folding while generating (every expression yields a
 *     constant, a symbol address + offset, or a register);
 *   - no code behind constant conditions: a reachability flag follows
 *     branches, so if (IS_ENABLED(..)), compiletime_assert, constant
 *     switches and code after return/noreturn calls are not emitted;
 *   - __always_inline calls are expanded with constant arguments bound to
 *     the parameters, so asm "i" operands, __builtin_constant_p and
 *     conditions see constants again (arch_static_branch,
 *     alternative_has_cap_likely); an inlined body whose live returns all
 *     return the same constant is that constant to the caller;
 *   - lazy emission: static functions and objects are emitted only when
 *     emitted code or data refers to them;
 *   - locals live in stack slots; expression temporaries in x9-x15 and
 *     x19-x28.
 *
 * The kernel also relies on what clang -O2 folds beyond that: a
 * BUILD_BUG_ON inside a plain static or inline function that only folds
 * once the function is inlined where its arguments are constants.  Such
 * functions are inlined too: plain inline functions that call an error()
 * function directly, and static functions in which an error() call
 * survived folding (need_fold: the TU is generated again with them
 * inlined).  An error() call that still survives is one clang removes with
 * value ranges; it becomes a trap.  __builtin_constant_p follows clang's
 * rules (front-end fold, else 0 with side effects or for pointers, else
 * the operand after inlining).
 *
 * Output goes through a narrow instruction-record interface (struct ins,
 * 16 bytes): codegen appends records per function, a backend turns them
 * into assembly text (assembled by clang -c for verification) or, later,
 * into machine code via the assembler's encoder.  Functions are
 * independent: worker threads take them from a queue, each with its own
 * record buffer, text buffer and binding tables (mmap'd, indexed by 32-bit
 * sym/label/node numbers); chunks are stitched in a fixed order at the end.
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "ccint.h"
#include "emit.h"
#include "as.h"
#include "elf.h"
#include "a64.h"

#define N(i) (&c->nodes[i])
/* Apple arm64 (Mach-O): '_' names, GOT for symbols outside the TU, variadic
 * arguments on the stack, va_list a char *, stack arguments packed */
#define DARWIN(c) ((c)->o->target == PP_DARWIN)
#define T(c, t) CT(c, t)
#define KIND(c, t) CKIND(c, t)

static void *reserve(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);

	if (p == MAP_FAILED) {
		perror("vcc: mmap");
		abort();
	}
	return p;
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---------------- types (the few predicates the emitter needs) ---------------- */

static inline qty prim(int k) { return QT(k, 0); }
static inline bool k_int(int k) { return (k >= TY_BOOL && k <= TY_UINT128) || k == TY_ENUM; }
static inline bool is_integer(cc *c, qty t) { return k_int(KIND(c, t)); }
static inline bool is_ptr(cc *c, qty t) { return KIND(c, t) == TY_PTR; }
static inline bool is_flt(cc *c, qty t) { int k = KIND(c, t); return k >= TY_FLOAT16 && k <= TY_LDOUBLE; }
static inline bool is_record(cc *c, qty t) { int k = KIND(c, t); return k == TY_STRUCT || k == TY_UNION; }
static inline bool is_array(cc *c, qty t) { return KIND(c, t) == TY_ARRAY; }
static inline bool is_func(cc *c, qty t) { return KIND(c, t) == TY_FUNC; }
static inline bool is_void(cc *c, qty t) { return KIND(c, t) == TY_VOID; }
static inline bool is_vec(cc *c, qty t) { return KIND(c, t) == TY_VECTOR; }
/* passed and returned like a record (vcc's own convention for vectors) */
static inline bool is_recabi(cc *c, qty t) { return is_record(c, t) || is_vec(c, t); }
static inline bool is_agg(cc *c, qty t) { return is_record(c, t) || is_array(c, t) || is_vec(c, t); }
static inline uint64_t tsize(cc *c, qty t) { return T(c, t)->size; }
static inline uint32_t talign(cc *c, qty t) { return T(c, t)->align; }
static inline bool is_signed(cc *c, qty t) { return cc_is_signed(c, t); }
static inline bool is_wide(cc *c, qty t) { return is_integer(c, t) && tsize(c, t) == 16; }

static i128 trunc_to(cc *c, i128 v, qty t)
{
	int k = KIND(c, t);
	uint64_t sz;

	if (k == TY_BOOL)
		return v != 0;
	if (k == TY_PTR)
		return (i128)(uint64_t)v;
	sz = tsize(c, t);
	if (sz >= 16)
		return is_signed(c, t) ? v : (i128)(u128)v;
	{
		unsigned bits = (unsigned)sz * 8;
		u128 m = ((u128)1 << bits) - 1;
		u128 x = (u128)v & m;

		if (is_signed(c, t) && (x >> (bits - 1)) & 1)
			return (i128)(x | ~m);
		return (i128)x;
	}
}

/* ---------------- instruction records ---------------- */

/* registers: 0-30 x0-x30, ZR, SP, FRAME (pseudo base of stack slots) */
enum { R_FP = 29, R_LR = 30, ZR = 31, SP = 32, FRAME = 33, R_X16 = 16, R_X17 = 17 };

enum {
	I_LABEL,	/* label */
	I_RAW,		/* off: text pool offset, ref: length */
	/* rd = rn op (rm << sh); cond: shift kind (0 lsl, 1 lsr, 2 asr) */
	I_ADD, I_SUB, I_ADDS, I_SUBS, I_AND, I_ORR, I_EOR, I_ANDS, I_BIC, I_ORN, I_ADC, I_SBC, I_ADCS, I_SBCS,
	/* rd = rn op imm (any value: the backend splits or materialises) */
	I_ADDI, I_SUBI, I_ADDSI, I_SUBSI, I_ANDI, I_ORRI, I_EORI, I_ANDSI,
	I_MOVI,		/* rd = imm */
	I_MOV,		/* rd = rn (either may be SP) */
	I_MADD, I_MSUB,	/* rd = ra +- rn * rm */
	I_SDIV, I_UDIV, I_SMULH, I_UMULH, I_LSLV, I_LSRV, I_ASRV,
	I_SBFM, I_UBFM, I_BFM,	/* imm: immr, sh: imms */
	I_CSEL, I_CSINC, I_CSINV, I_CSNEG,	/* cond */
	I_CLZ, I_CLS, I_RBIT, I_REV, I_REV16, I_REV32,
	/* memory: sz = log2 bytes; cond: 0 zero-extend, 1 sign-extend to x, 2 to w; [rn + imm] */
	I_LD, I_ST,
	I_LDP, I_STP,	/* x regs rd, ra; [rn + imm]; cond: 0 offset, 1 pre-index, 2 post-index */
	/* symbol references: ref (see refs below), off: addend */
	I_ADRP,		/* rd = page(ref + off) */
	I_ADDLO,	/* rd = rn + lo12(ref + off) */
	I_LDLO, I_STLO,	/* [rn + lo12(ref + off)], sz/cond as I_LD */
	I_LDGOT,	/* rd = address of ref through the GOT (weak undefined) */
	I_ADR,		/* rd = local label */
	I_B, I_BC,	/* label; I_BC: cond */
	I_CBZ, I_CBNZ,	/* rd, label */
	I_BL,		/* ref */
	I_BLR, I_BR,	/* rn */
	I_RET,
	I_BRK,		/* imm */
	I_PRFM,		/* [rn] */
	I_MRS,		/* rd, ref: raw text (system register name) */
	I_MOVSP,	/* frame setup pseudo (see finish) */
	I_NOP,
	I_WORD,		/* imm: an encoded instruction (registers already in it) */
	I_TLV,		/* x0 = address of thread-local ref (Darwin TLV descriptor call; clobbers x16, lr) */
};

enum { C_EQ, C_NE, C_HS, C_LO, C_MI, C_PL, C_VS, C_VC, C_HI, C_LS, C_GE, C_LT, C_GT, C_LE, C_AL };
static const char cond_name[][8] = { "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al" };

typedef struct ins {
	uint8_t op, sz;		/* sz: 1 = 64-bit (x), 0 = 32-bit (w); memory: log2 bytes */
	uint8_t rd, rn, rm, ra;
	uint8_t cond, sh;
	union {
		int64_t imm;
		struct { int32_t off; uint32_t ref; };
		uint32_t label;
	};
} ins;

/* references to things with a symbol: kind in the top 4 bits */
enum { RK_SYM = 0, RK_STR = 1, RK_LBL = 2, RK_NAME = 3 };
#define REF(k, i) (((uint32_t)(k) << 28) | (uint32_t)(i))
#define RKIND(r) ((r) >> 28)
#define RIDX(r) ((r) & 0x0fffffffu)

/* ---------------- byte buffers ---------------- */

typedef struct tbuf {
	char *p;
	size_t n, cap;
} tbuf;

static void tb_init(tbuf *b, size_t cap)
{
	b->p = reserve(cap);
	b->n = 0;
	b->cap = cap;
}

static inline void tb_need(tbuf *b, size_t n)
{
	if (unlikely(b->n + n > b->cap)) {
		fputs("vcc: emitter buffer overflow\n", stderr);
		abort();
	}
}

static inline void tb_put(tbuf *b, const char *s, size_t n)
{
	tb_need(b, n);
	memcpy(b->p + b->n, s, n);
	b->n += n;
}

static inline void tb_c(tbuf *b, char ch)
{
	tb_need(b, 1);
	b->p[b->n++] = ch;
}

static inline void tb_s(tbuf *b, const char *s) { tb_put(b, s, strlen(s)); }

static void tb_u_slow(tbuf *b, uint64_t v)
{
	char t[24];
	int i = 24;

	do
		t[--i] = (char)('0' + v % 10);
	while ((v /= 10));
	tb_put(b, t + i, (size_t)(24 - i));
}

static inline void tb_u(tbuf *b, uint64_t v)
{
	if (likely(v < 10)) {
		tb_c(b, (char)('0' + v));
	} else if (v < 100) {
		tb_need(b, 2);
		b->p[b->n] = (char)('0' + v / 10);
		b->p[b->n + 1] = (char)('0' + v % 10);
		b->n += 2;
	} else {
		tb_u_slow(b, v);
	}
}

/* lower-case hex digits, no prefix: label numbers */
static inline void tb_x(tbuf *b, uint32_t v)
{
	int n = v ? (35 - __builtin_clz(v)) >> 2 : 1;
	char *p;

	tb_need(b, 8);
	p = b->p + b->n;
	for (int i = n - 1; i >= 0; i--, v >>= 4)
		p[i] = "0123456789abcdef"[v & 15];
	b->n += (size_t)n;
}

/* a name from a char[8] table: one 8-byte copy, the length from the first zero byte */
static inline void tb_n8(tbuf *b, const char s[8])
{
	uint64_t w, z;

	memcpy(&w, s, 8);
	tb_need(b, 8);
	memcpy(b->p + b->n, &w, 8);
	z = (w - 0x0101010101010101ull) & ~w & 0x8080808080808080ull;
	b->n += z ? (size_t)(__builtin_ctzll(z) >> 3) : 8;
}

static void tb_i(tbuf *b, int64_t v)
{
	if (v < 0) {
		tb_c(b, '-');
		tb_u(b, (uint64_t)0 - (uint64_t)v);
	} else {
		tb_u(b, (uint64_t)v);
	}
}

static void tb_hex(tbuf *b, uint64_t v)
{
	char t[20];
	int i = 20;

	do
		t[--i] = "0123456789abcdef"[v & 15];
	while ((v >>= 4));
	t[--i] = 'x';
	t[--i] = '0';
	tb_put(b, t + i, (size_t)(20 - i));
}

static void tb_f(tbuf *b, const char *fmt, ...)
{
	va_list ap;
	int n;

	tb_need(b, 512);
	va_start(ap, fmt);
	n = vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
	va_end(ap);
	b->n += (size_t)n;
}

/* ---------------- immediates ---------------- */

/* AArch64 logical immediate: is v encodable for this width? */
static bool logimm_ok(uint64_t v, int x)
{
	unsigned size = 64;

	if (!x)
		v = (v & 0xffffffffu) | (v << 32);
	if (v == 0 || v == ~(uint64_t)0)
		return false;
	/* smallest repeating element */
	while (size > 2) {
		unsigned h = size / 2;
		uint64_t m = (h == 64 ? ~(uint64_t)0 : (((uint64_t)1 << h) - 1));

		if (((v >> h) & m) != (v & m))
			break;
		size = h;
	}
	{
		uint64_t m = size == 64 ? ~(uint64_t)0 : (((uint64_t)1 << size) - 1);
		uint64_t e = v & m;
		/* e must be a rotated run of ones */
		uint64_t t = e;

		for (unsigned r = 0; r < size; r++) {
			/* contiguous ones starting at bit 0: t & (t + 1) == 0 */
			if ((t & (t + 1)) == 0)
				return true;
			t = ((t >> 1) | ((t & 1) << (size - 1))) & m;
		}
		return false;
	}
}

/* ---------------- register names ---------------- */

static inline void reg(tbuf *b, int r, int x)
{
	char *p;

	tb_need(b, 4);
	p = b->p + b->n;
	p[0] = x ? 'x' : 'w';
	if (likely(r < 10)) {
		p[1] = (char)('0' + r);
		b->n += 2;
	} else if (likely(r < 31)) {
		p[1] = (char)('0' + r / 10);
		p[2] = (char)('0' + r % 10);
		b->n += 3;
	} else if (r == ZR) {
		p[1] = 'z', p[2] = 'r';
		b->n += 3;
	} else {
		if (x)
			p[0] = 's', p[1] = 'p', b->n += 2;
		else
			p[1] = 's', p[2] = 'p', b->n += 3;
	}
}

/* ---------------- emitter state ---------------- */

typedef struct chunk {
	uint64_t key;		/* output order */
	const char *p;
	size_t n;
} chunk;

/* analysis bits (prepass, read-only while emitting) */
enum { SA_WRITTEN = 1, SA_CLEANUP_FN = 2, SA_MUSTFOLD = 4, SA_CWRITE = 8, SA_CMULTI = 16, SA_USED = 32 };	/* USED: a local or param read */
/* CWRITE: only ever assigned integer constants (all cwval unless CMULTI) */	/* per sym; MUSTFOLD: a function that calls an error() function */
enum { NA_LABEL = 1 };				/* per node: statement contains a label or case */

typedef struct emitter {
	cc *c;
	const cc_opts *o;
	_Atomic uint8_t *claimed;	/* per sym: queued for emission */
	_Atomic uint8_t *strdone;	/* per string: emitted */
	_Atomic uint8_t *extref;	/* per sym: referenced (for .weak of undefined syms) */
	_Atomic uint8_t *walked;
	uint16_t *fnsize;		/* per function: AST nodes (prepass) */
	i128 *cwval;		/* per SA_CWRITE variable: the one constant it is assigned */	/* per function: prepass state (0 no, 1 running, 2 done) */
	uint8_t *sflags;		/* SA_* per sym */
	uint8_t *nflags;		/* NA_* per node */
	uint8_t *lref;			/* per label: LR_* */
	uint16_t *lcdepth;		/* per label: cleanup depth at its definition */
	/* work queue (syms) */
	pthread_mutex_t mu;
	pthread_cond_t cv;
	uint32_t *q;
	uint32_t qh, qt;
	int busy, nthr, done;
	/* output */
	pthread_mutex_t cmu;
	chunk *chunks;
	uint32_t nchunk;
	_Atomic uint32_t asmno;
	_Atomic int errors;
	_Atomic uint64_t nins, nfn, nobj, ninl;
	_Atomic uint64_t print_ns, walk_ns, nwalk;
	uint8_t *mf;			/* per sym: static function to inline everywhere (kept across rounds) */
	_Atomic uint32_t newmf;		/* marked this round */
	_Atomic uint32_t nerrcall;	/* error() calls dropped (not folded) */	/* VCC_EMIT_STATS: time in finish_function */
	bool stats;
	bool objtext;			/* VCC_OBJ_TEXT: objects through the printer and as_run (checks the object backend) */
	pthread_mutex_t emu;		/* diagnostics */
} emitter;

typedef struct val {
	uint8_t k;		/* V_* */
	uint8_t r, r2;		/* V_REG: r; V_REG2: r (low), r2 (high) */
	uint32_t ref;		/* V_ADDR */
	i128 i;			/* V_CONST value / V_ADDR addend */
} val;

enum { V_NONE, V_CONST, V_ADDR, V_REG, V_REG2 };

/* per-sym binding inside a function instance */
enum { B_NONE, B_SLOT, B_CONST, B_INDIRECT };

/* per user label (prepass): referenced, defined so far, target of a jump back (or &&) */
enum { LR_REF = 1, LR_DEF = 2, LR_BACK = 4 };

typedef struct bind {
	uint32_t inst;
	uint8_t kind;
	uint8_t reg;		/* register variable: asm("xN") (0xff: none) */
	uint8_t kv;		/* known-value tracking: index + 1 (0: untracked) */
	int32_t slot;		/* B_SLOT, B_INDIRECT */
	val v;			/* B_CONST */
} bind;

typedef struct lbl {
	uint32_t inst, id;
} lbl;

#define NKV 16
#define CAPKVS (1u << 18)
/* lkvi once the label is placed (or dropped): a later edge to it is a jump back */
enum { LKV_PLACED = UINT32_MAX - 2, LKV_KNOWN = UINT32_MAX - 1, LKV_DROPPED = UINT32_MAX };
typedef struct kvst {
	uint16_t valid;
	int64_t v[NKV];
} kvst;

typedef struct loopctx {
	uint32_t brk, cont;	/* local labels (cont 0: switch) */
	int cdepth;
} loopctx;

typedef struct fnctx {		/* one function instance: the emitted function or an inlined body */
	uint32_t fn, inst;
	uint32_t retlbl;
	int32_t retslot;
	qty rty;
	bool inl;
	bool cst_ok, any_ret;
	val cst;
	int cbase;		/* cleanup stack depth at entry */
	int nloop;		/* loop stack depth at entry */
	struct fnctx *up;
} fnctx;

#define MAXLOOP 256
#define MAXCLEAN 256
#define NTEMP 17

typedef struct ecx {
	emitter *E;
	cc *c;
	/* records of the function being emitted */
	ins *in;
	uint32_t nin, capin;
	uint32_t curloc;	/* innermost statement being generated (diagnostics) */
	tbuf raw;		/* inline asm text */
	tbuf out;		/* this thread's output text */
	tbuf data;		/* scratch: data object bytes */
	struct reloc { uint64_t off; uint32_t ref, seq; int64_t add; } *rel;
	uint32_t nrel;
	uint64_t relend;	/* end of the highest relocated word */
	/* bindings */
	bind *b;		/* per sym */
	lbl *lm;		/* per label */
	uint32_t *caselbl;	/* per node (N_CASE / N_DEFAULT): local label */
	uint32_t *lrefc;	/* per local label: branches to it */
	uint32_t nlabel, caplabel;
	/*
	 * Known values of locals only ever assigned constants (SA_CWRITE):
	 * forward edges carry them to their label, which keeps what all
	 * incoming edges agree on; loop heads and unknown targets drop them.
	 */
	uint8_t nkv;
	uint16_t kvalid;
	int64_t kval[NKV];
	uint32_t *lkvi;		/* per local label: kvst index + 1 (0: none) */
	kvst *kvs;
	uint32_t nkvs;
	uint32_t inst_next;
	/* frame */
	int32_t foff, fmax;
	int32_t outmax;		/* outgoing stack argument bytes */
	bool dyn, calls, vararg;
	int32_t vaslot;		/* variadic register save area */
	int ngr_named, nstk_named;	/* variadic: named GPR args, named stack bytes */
	int nvr_named, vrslot;		/* variadic (Linux): named FP/SIMD args, the v0-v7 save area */
	int nsrn;		/* classify: next SIMD/FP argument register */
	/* temporaries */
	uint32_t live;		/* bit i: tempregs[i] in use */
	uint32_t usedcs;	/* callee-saved registers used (bit = x number) */
	/* control */
	bool reach;
	int dry;		/* inside dry(): code is generated and thrown away, no references */
	loopctx loops[MAXLOOP];
	int nloop;
	uint32_t clean[MAXCLEAN];	/* syms with cleanup, innermost last */
	int nclean;
	fnctx *f;
	int inline_depth;
	uint32_t top;		/* sym being emitted */
	uint32_t *cases;	/* scratch for switch case collection */
	uint32_t *weakref;	/* weak undefined syms this thread referenced first */
	uint32_t nweakref;
	uint32_t ncases;
	jmp_buf bail;
} ecx;

static const uint8_t tempregs[NTEMP] = { 9, 10, 11, 12, 13, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28 };

__attribute__((format(printf, 3, 4)))
static void ediag(ecx *e, uint32_t loc, const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&e->E->emu);
	va_start(ap, fmt);
	pp_vdiag(e->c->p, loc, "note", fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&e->E->emu);
}

__attribute__((format(printf, 3, 4), unused))
static void eerror(ecx *e, uint32_t loc, const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&e->E->emu);
	va_start(ap, fmt);
	pp_vdiag(e->c->p, loc, "error", fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&e->E->emu);
	e->E->errors++;
}

__attribute__((format(printf, 3, 4), noreturn))
static void efatal(ecx *e, uint32_t loc, const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&e->E->emu);
	va_start(ap, fmt);
	pp_vdiag(e->c->p, loc, "error", fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&e->E->emu);
	e->E->errors++;
	_longjmp(e->bail, 1);
}

/* ---------------- names ---------------- */

static const char *const fixed_names[] = {
	"memcpy", "memset", "memmove", "__udivti3", "__divti3", "__umodti3", "__modti3", "strlen",
	"__ashlti3", "__lshrti3", "__ashrti3", "__floattisf", "__floattidf", "__floatuntisf", "__floatuntidf",
	"__fixsfti", "__fixdfti", "__fixunssfti", "__fixunsdfti",
};
enum { FN_MEMCPY, FN_MEMSET, FN_MEMMOVE, FN_UDIVTI3, FN_DIVTI3, FN_UMODTI3, FN_MODTI3, FN_STRLEN,
       FN_ASHLTI3, FN_LSHRTI3, FN_ASHRTI3, FN_FLOATTISF, FN_FLOATTIDF, FN_FLOATUNTISF, FN_FLOATUNTIDF,
       FN_FIXSFTI, FN_FIXDFTI, FN_FIXUNSSFTI, FN_FIXUNSDFTI };
#define FIXED(k) REF(RK_NAME, 0x8000000u + (k))

static uint32_t canon_sym(cc *c, uint32_t s)
{
	while (c->syms[s].next && c->syms[s].depth > 0)
		s = c->syms[s].next;
	return s;
}

static const attrs *sym_attrs(cc *c, uint32_t s)
{
	static const attrs none;

	return c->syms[s].attrs ? &c->attr[c->syms[s].attrs] : &none;
}

static void sym_name(cc *c, tbuf *b, uint32_t s)
{
	const sym *y;
	const attrs *a;

	s = canon_sym(c, s);
	y = &c->syms[s];
	a = sym_attrs(c, s);
	if (a->asmname) {
		tb_s(b, c->strs[a->asmname - 1].s);
		return;
	}
	if (!y->name) {
		tb_s(b, ".Lcl.");
		tb_u(b, s);
		return;
	}
	if (DARWIN(c))
		tb_c(b, '_');
	tb_put(b, vintern_ents[y->name].s, vintern_ents[y->name].len);
	if (y->depth > 0 && y->kind == S_VAR && y->sc == SC_STATIC) {
		tb_c(b, '.');
		tb_u(b, s);
	}
}

static void ref_name(cc *c, tbuf *b, uint32_t r)
{
	uint32_t i = RIDX(r);

	switch (RKIND(r)) {
	case RK_SYM:
		sym_name(c, b, i);
		break;
	case RK_STR:
		tb_s(b, ".L.str.");
		tb_u(b, i);
		break;
	case RK_LBL:
		tb_s(b, ".Ltmp.lbl.");
		tb_u(b, i);
		break;
	case RK_NAME:
		if (DARWIN(c))
			tb_c(b, '_');
		if (i >= 0x8000000u) {
			tb_s(b, fixed_names[i - 0x8000000u]);
		} else {
			const char *n = cc_builtin_name(i);

			if (!strncmp(n, "__builtin_", 10))
				n += 10;
			tb_s(b, n);
		}
		break;
	}
}

static void ref_off(cc *c, tbuf *b, uint32_t r, int64_t off)
{
	ref_name(c, b, r);
	if (off > 0) {
		tb_c(b, '+');
		tb_i(b, off);
	} else if (off < 0) {
		tb_i(b, off);
	}
}

/* ---------------- printer: records -> assembly text ---------------- */

static void p_lbl(tbuf *b, uint32_t fid, uint32_t l)
{
	tb_s(b, ".LBB");
	tb_x(b, fid);
	tb_c(b, '_');
	tb_x(b, l);
}

static void p_movi(tbuf *b, int rd, int x, uint64_t v)
{
	int nz = 0, nf = 0, chunks = x ? 4 : 2;
	bool first = true, neg;

	if (!x)
		v &= 0xffffffffu;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		nz += h != 0;
		nf += h == 0xffff;
	}
	if (nz <= 1 || (chunks - nf) <= 1 || logimm_ok(v, x)) {
		tb_s(b, "\tmov\t");
		reg(b, rd, x);
		tb_s(b, ", #");
		if (x)
			tb_hex(b, v);
		else
			tb_hex(b, (uint32_t)v);
		tb_c(b, '\n');
		return;
	}
	neg = nf > nz;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		if (h == (neg ? 0xffff : 0))
			continue;
		if (first) {
			tb_s(b, neg ? "\tmovn\t" : "\tmovz\t");
			h = neg ? (uint16_t)~h : h;
			first = false;
		} else {
			tb_s(b, "\tmovk\t");
		}
		reg(b, rd, x);
		tb_s(b, ", #");
		tb_u(b, h);
		if (i) {
			tb_s(b, ", lsl #");
			tb_u(b, (uint64_t)(16 * i));
		}
		tb_c(b, '\n');
	}
	if (first)
		p_movi(b, rd, x, 0);
}

static const char ld_name[2][4][3][8] = {
	{ { "ldrb", "ldrsb", "ldrsb" }, { "ldrh", "ldrsh", "ldrsh" }, { "ldr", "ldrsw", "ldr" }, { "ldr", "ldr", "ldr" } },
	{ { "ldurb", "ldursb", "ldursb" }, { "ldurh", "ldursh", "ldursh" }, { "ldur", "ldursw", "ldur" }, { "ldur", "ldur", "ldur" } },
};
static const char st_name[2][4][8] = { { "strb", "strh", "str", "str" }, { "sturb", "sturh", "stur", "stur" } };

static inline int ld_x(const ins *i) { return i->sz == 3 || (i->op == I_LD && i->cond == 1) || (i->op == I_LDLO && i->cond == 1); }

static void p_mem(tbuf *b, const ins *i)
{
	int64_t off = i->imm;
	int sz = i->sz, scaled, base = i->rn;
	int x = i->op == I_ST ? sz == 3 : ld_x(i);

	scaled = off >= 0 && (off & ((1 << sz) - 1)) == 0 && (off >> sz) < 4096;
	if (!scaled && !(off >= -256 && off < 256)) {
		p_movi(b, R_X17, 1, (uint64_t)off);
		tb_s(b, "\tadd\tx17, ");
		reg(b, base, 1);
		tb_s(b, ", x17\n");
		base = R_X17;
		off = 0;
		scaled = 1;
	}
	tb_c(b, '\t');
	if (i->op == I_ST)
		tb_n8(b, st_name[!scaled][sz]);
	else
		tb_n8(b, ld_name[!scaled][sz][i->cond]);
	tb_c(b, '\t');
	reg(b, i->rd, x);
	tb_s(b, ", [");
	reg(b, base, 1);
	if (off) {
		tb_s(b, ", #");
		tb_i(b, off);
	}
	tb_s(b, "]\n");
}

static const char alu_name[][8] = {
	[I_ADD] = "add", [I_SUB] = "sub", [I_ADDS] = "adds", [I_SUBS] = "subs", [I_AND] = "and",
	[I_ORR] = "orr", [I_EOR] = "eor", [I_ANDS] = "ands", [I_BIC] = "bic", [I_ORN] = "orn",
	[I_ADC] = "adc", [I_SBC] = "sbc", [I_ADCS] = "adcs", [I_SBCS] = "sbcs",
	[I_ADDI] = "add", [I_SUBI] = "sub", [I_ADDSI] = "adds", [I_SUBSI] = "subs",
	[I_ANDI] = "and", [I_ORRI] = "orr", [I_EORI] = "eor", [I_ANDSI] = "ands",
	[I_MADD] = "madd", [I_MSUB] = "msub", [I_SDIV] = "sdiv", [I_UDIV] = "udiv",
	[I_SMULH] = "smulh", [I_UMULH] = "umulh", [I_LSLV] = "lsl", [I_LSRV] = "lsr", [I_ASRV] = "asr",
	[I_SBFM] = "sbfm", [I_UBFM] = "ubfm", [I_BFM] = "bfm",
	[I_CSEL] = "csel", [I_CSINC] = "csinc", [I_CSINV] = "csinv", [I_CSNEG] = "csneg",
	[I_CLZ] = "clz", [I_CLS] = "cls", [I_RBIT] = "rbit", [I_REV] = "rev", [I_REV16] = "rev16", [I_REV32] = "rev32",
};

static void p_rrr(tbuf *b, const char *mn, const ins *i)
{
	tb_c(b, '\t');
	tb_n8(b, mn);
	tb_c(b, '\t');
	reg(b, i->rd, i->sz);
	tb_s(b, ", ");
	reg(b, i->rn, i->sz);
	tb_s(b, ", ");
	reg(b, i->rm, i->sz);
}

static void p_addi(tbuf *b, const ins *i)
{
	int op = i->op;
	int64_t v = i->imm;
	bool flags = op == I_ADDSI || op == I_SUBSI;
	bool sub = op == I_SUBI || op == I_SUBSI;

	if (v < 0 && v != INT64_MIN) {
		v = -v;
		sub = !sub;
	}
	if (!i->sz)
		v &= 0xffffffff;
	if (v >= 0 && (v < 4096 || ((v & 0xfff) == 0 && v < (1 << 24)) || (!flags && v < (1 << 24)))) {
		const char *mn = sub ? (flags ? "subs" : "sub") : (flags ? "adds" : "add");
		int rn = i->rn;

		if (v >= 4096 && (v & 0xfff)) {
			tb_c(b, '\t'), tb_s(b, mn), tb_c(b, '\t');
			reg(b, i->rd, i->sz);
			tb_s(b, ", ");
			reg(b, rn, i->sz);
			tb_s(b, ", #"), tb_i(b, v >> 12), tb_s(b, ", lsl #12\n");
			rn = i->rd;
			v &= 0xfff;
		}
		tb_c(b, '\t'), tb_s(b, mn), tb_c(b, '\t');
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, rn, i->sz);
		if ((v & 0xfff) == 0 && v)
			tb_s(b, ", #"), tb_i(b, v >> 12), tb_s(b, ", lsl #12\n");
		else
			tb_s(b, ", #"), tb_i(b, v), tb_c(b, '\n');
		return;
	}
	p_movi(b, R_X17, i->sz, (uint64_t)v);
	tb_c(b, '\t'), tb_s(b, sub ? (flags ? "subs" : "sub") : (flags ? "adds" : "add")), tb_c(b, '\t');
	reg(b, i->rd, i->sz);
	tb_s(b, ", ");
	reg(b, i->rn, i->sz);
	tb_s(b, i->sz ? ", x17\n" : ", w17\n");
}

static void print_ins(cc *c, tbuf *b, const ins *i, uint32_t fid, const char *raw)
{
	switch (i->op) {
	case I_LABEL:
		p_lbl(b, fid, i->label);
		tb_s(b, ":\n");
		return;
	case I_RAW:
		tb_put(b, raw + i->off, i->ref);
		tb_c(b, '\n');
		return;
	case I_ADD: case I_SUB: case I_ADDS: case I_SUBS: case I_AND: case I_ORR: case I_EOR:
	case I_ANDS: case I_BIC: case I_ORN: case I_ADC: case I_SBC: case I_ADCS: case I_SBCS:
		p_rrr(b, alu_name[i->op], i);
		if (i->sh) {
			tb_s(b, i->cond == 0 ? ", lsl #" : i->cond == 1 ? ", lsr #" : ", asr #");
			tb_u(b, i->sh);
		}
		tb_c(b, '\n');
		return;
	case I_ADDI: case I_SUBI: case I_ADDSI: case I_SUBSI:
		p_addi(b, i);
		return;
	case I_ANDI: case I_ORRI: case I_EORI: case I_ANDSI:
		tb_c(b, '\t');
		tb_n8(b, alu_name[i->op]);
		tb_c(b, '\t');
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, i->rn, i->sz);
		tb_s(b, ", #");
		tb_hex(b, i->sz ? (uint64_t)i->imm : (uint32_t)i->imm);
		tb_c(b, '\n');
		return;
	case I_MOVI:
		p_movi(b, i->rd, i->sz, (uint64_t)i->imm);
		return;
	case I_MOV:
		tb_s(b, "\tmov\t");
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, i->rn, i->sz);
		tb_c(b, '\n');
		return;
	case I_MADD: case I_MSUB:
		p_rrr(b, alu_name[i->op], i);
		tb_s(b, ", ");
		reg(b, i->ra, i->sz);
		tb_c(b, '\n');
		return;
	case I_SDIV: case I_UDIV: case I_SMULH: case I_UMULH: case I_LSLV: case I_LSRV: case I_ASRV:
		p_rrr(b, alu_name[i->op], i);
		tb_c(b, '\n');
		return;
	case I_SBFM: case I_UBFM: case I_BFM:
		tb_c(b, '\t');
		tb_n8(b, alu_name[i->op]);
		tb_c(b, '\t');
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, i->rn, i->sz);
		tb_s(b, ", #");
		tb_u(b, (uint64_t)i->imm);
		tb_s(b, ", #");
		tb_u(b, i->sh);
		tb_c(b, '\n');
		return;
	case I_CSEL: case I_CSINC: case I_CSINV: case I_CSNEG:
		p_rrr(b, alu_name[i->op], i);
		tb_s(b, ", ");
		tb_n8(b, cond_name[i->cond]);
		tb_c(b, '\n');
		return;
	case I_CLZ: case I_CLS: case I_RBIT: case I_REV: case I_REV16: case I_REV32:
		tb_c(b, '\t');
		tb_n8(b, alu_name[i->op]);
		tb_c(b, '\t');
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, i->rn, i->sz);
		tb_c(b, '\n');
		return;
	case I_LD: case I_ST:
		p_mem(b, i);
		return;
	case I_LDP: case I_STP:
		tb_s(b, i->op == I_LDP ? "\tldp\t" : "\tstp\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		reg(b, i->ra, 1);
		tb_s(b, ", [");
		reg(b, i->rn, 1);
		if (i->cond == 2) {
			tb_s(b, "], #");
			tb_i(b, i->imm);
			tb_c(b, '\n');
		} else {
			if (i->imm) {
				tb_s(b, ", #");
				tb_i(b, i->imm);
			}
			tb_s(b, i->cond == 1 ? "]!\n" : "]\n");
		}
		return;
	case I_ADRP:
		tb_s(b, "\tadrp\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		ref_off(c, b, i->ref, i->off);
		tb_c(b, '\n');
		return;
	case I_ADDLO:
		tb_s(b, "\tadd\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		reg(b, i->rn, 1);
		tb_s(b, ", :lo12:");
		ref_off(c, b, i->ref, i->off);
		tb_c(b, '\n');
		return;
	case I_LDLO: case I_STLO:
		tb_c(b, '\t');
		tb_s(b, i->op == I_STLO ? st_name[0][i->sz] : ld_name[0][i->sz][i->cond]);
		tb_c(b, '\t');
		reg(b, i->rd, i->op == I_STLO ? i->sz == 3 : ld_x(i));
		tb_s(b, ", [");
		reg(b, i->rn, 1);
		tb_s(b, ", :lo12:");
		ref_off(c, b, i->ref, i->off);
		tb_s(b, "]\n");
		return;
	case I_LDGOT:
		tb_s(b, "\tadrp\t");
		reg(b, i->rd, 1);
		tb_s(b, ", :got:");
		ref_name(c, b, i->ref);
		tb_s(b, "\n\tldr\t");
		reg(b, i->rd, 1);
		tb_s(b, ", [");
		reg(b, i->rd, 1);
		tb_s(b, ", :got_lo12:");
		ref_name(c, b, i->ref);
		tb_s(b, "]\n");
		return;
	case I_ADR:
		tb_s(b, "\tadr\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_B:
		tb_s(b, "\tb\t");
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_BC:
		tb_s(b, "\tb.");
		tb_n8(b, cond_name[i->cond]);
		tb_c(b, '\t');
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_CBZ: case I_CBNZ:
		tb_s(b, i->op == I_CBZ ? "\tcbz\t" : "\tcbnz\t");
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_BL:
		tb_s(b, "\tbl\t");
		ref_name(c, b, i->ref);
		tb_c(b, '\n');
		return;
	case I_BLR: case I_BR:
		tb_s(b, i->op == I_BLR ? "\tblr\t" : "\tbr\t");
		reg(b, i->rn, 1);
		tb_c(b, '\n');
		return;
	case I_RET:
		tb_s(b, "\tret\n");
		return;
	case I_BRK:
		tb_s(b, "\tbrk\t#");
		tb_hex(b, (uint64_t)i->imm);
		tb_c(b, '\n');
		return;
	case I_PRFM:
		tb_s(b, "\tprfm\tpldl1keep, [");
		reg(b, i->rn, 1);
		tb_s(b, "]\n");
		return;
	case I_MRS:
		tb_s(b, "\tmrs\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		tb_s(b, c->strs[i->ref].s);
		tb_c(b, '\n');
		return;
	case I_NOP:
		return;
	case I_WORD:
		tb_s(b, "\t.inst\t");
		tb_hex(b, (uint32_t)i->imm);
		tb_c(b, '\n');
		return;
	case I_TLV:
		tb_s(b, "\t// tlv ");
		ref_name(c, b, i->ref);
		tb_c(b, '\n');
		return;
	default:
		tb_s(b, "\t// ?\n");
		return;
	}
}

/* ---------------- object backend: records -> machine code ---------------- */

/*
 * With cc_opts.emit == 2 a chunk is a stream of items instead of text:
 *
 *   IT_TEXT   directives (section switches, .globl/.type/.size, aliases,
 *             top-level asm), run by the TU's assembler (as_snippet);
 *   IT_CODE   a function's records (prologue and epilogue included) and its
 *             inline asm text, encoded here with a64.h straight into the
 *             current frag; asm statements go through as_snippet in place;
 *   IT_DATA   an object's bytes and relocations; IT_LABEL + IT_BYTES: a
 *             string literal.
 *
 * Replaying the chunks in output order makes exactly the ElfObj calls the
 * assembler makes for the text the printer would have written (sections,
 * symbol registration order, mapping symbols, fixups), so the object is
 * byte-identical with the text path; bench/vcc-asdiff.py checks that.
 * Branches inside a function without inline asm are resolved here; with
 * inline asm (which may name the labels: asm goto) labels are .LBB symbols.
 */
enum { IT_TEXT = 1, IT_CODE, IT_DATA, IT_LABEL, IT_BYTES };
typedef struct item { uint32_t kind, len; } item;	/* len payload bytes follow, padded to 8 */
typedef struct code_hdr { uint32_t fid, nin, nraw, nlabel, sym_labels, pad; } code_hdr;
typedef struct data_hdr { uint64_t size, n; uint32_t nrel, zero; } data_hdr;
typedef struct ireloc { uint64_t off; uint32_t ref, seq; int64_t add; } ireloc;

static inline bool direct(const ecx *e) { return e->E->o->emit == 2 && !e->E->objtext; }

static size_t it_open(ecx *e, uint32_t kind)
{
	tbuf *b = &e->out;
	size_t h;

	if (!direct(e))
		return SIZE_MAX;
	tb_need(b, 16);
	b->n = (b->n + 7) & ~(size_t)7;
	h = b->n;
	((item *)(b->p + h))->kind = kind;
	b->n += sizeof(item);
	return h;
}

static void it_close(ecx *e, size_t h)
{
	tbuf *b = &e->out;

	if (h == SIZE_MAX)
		return;
	((item *)(b->p + h))->len = (uint32_t)(b->n - h - sizeof(item));
	tb_need(b, 8);
	while (b->n & 7)
		b->p[b->n++] = 0;
}

static void *it_room(ecx *e, size_t n)
{
	void *p;

	tb_need(&e->out, n + 8);
	p = e->out.p + e->out.n;
	e->out.n += n;
	return p;
}

/* replay state: one per TU */
typedef struct rctx {
	cc *c;
	As *as;
	ElfObj *o;
	uint32_t *symc, *strc, *lblc;	/* ref -> ElfSym (0: not yet) */
	uint32_t *loff;			/* direct labels: word offset in the function */
	tbuf nb;			/* name scratch */
} rctx;

/* the assembler's symbol for a reference (named as the printer names it) */
static uint32_t r_sym(rctx *R, uint32_t ref)
{
	uint32_t *slot = NULL, i = RIDX(ref), y;

	switch (RKIND(ref)) {
	case RK_SYM: slot = &R->symc[canon_sym(R->c, i)]; break;
	case RK_STR: slot = &R->strc[i]; break;
	case RK_LBL: slot = &R->lblc[i]; break;
	}
	if (slot && *slot)
		return *slot;
	R->nb.n = 0;
	ref_name(R->c, &R->nb, ref);
	y = as_sym(R->as, R->nb.p, R->nb.n);
	if (slot)
		*slot = y;
	return y;
}

static uint32_t r_label(rctx *R, uint32_t fid, uint32_t l)
{
	R->nb.n = 0;
	p_lbl(&R->nb, fid, l);
	return as_sym(R->as, R->nb.p, R->nb.n);
}

/* a pending fixup on word k of an instruction */
typedef struct pfix { uint8_t k, lbl; uint32_t type, ref; int64_t off; } pfix;

static unsigned e_movi(uint32_t *w, int rd, int x, uint64_t v)
{
	int nz = 0, nf = 0, chunks = x ? 4 : 2, n = 0;
	bool neg, first = true;
	uint64_t mask = x ? ~0ull : 0xffffffffull;

	v &= mask;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		nz += h != 0;
		nf += h == 0xffff;
	}
	if (nz <= 1 || (chunks - nf) <= 1 || logimm_ok(v, x)) {
		/* mov: movz, movn, or orr with a bitmask immediate (as.c mov_imm) */
		unsigned hw;

		if (rd != SP) {
			for (hw = 0; hw < (unsigned)chunks; hw++)
				if (!(v & ~(0xffffull << 16 * hw))) {
					w[0] = a64_movz(x, (unsigned)rd, (unsigned)(v >> 16 * hw) & 0xffff, hw);
					return 1;
				}
			for (hw = 0; hw < (unsigned)chunks; hw++)
				if (!(~v & mask & ~(0xffffull << 16 * hw))) {
					w[0] = a64_movn(x, (unsigned)rd, (unsigned)(~v >> 16 * hw) & 0xffff, hw);
					return 1;
				}
		}
		w[0] = a64_logic_imm(x, 1, (unsigned)rd & 31, 31, (unsigned)a64_logimm(v, x));
		return 1;
	}
	neg = nf > nz;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		if (h == (neg ? 0xffff : 0))
			continue;
		if (first) {
			w[n++] = neg ? a64_movn(x, (unsigned)rd, (uint16_t)~h, (unsigned)i) : a64_movz(x, (unsigned)rd, h, (unsigned)i);
			first = false;
		} else {
			w[n++] = a64_movk(x, (unsigned)rd, h, (unsigned)i);
		}
	}
	return (unsigned)n;
}

static inline unsigned R5(int r) { return r == SP ? 31u : (unsigned)r & 31; }	/* SP and ZR are both 31 */

/* add/sub (register), the extended form when sp is involved (as.c addsub_reg) */
static uint32_t e_addsub_reg(int sf, int op, int s, int rd, int rn, int rm, unsigned kind, unsigned amt)
{
	if (rd == SP || rn == SP)
		return 0x0b200000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | (uint32_t)s << 29 | R5(rm) << 16 |
		       (sf ? 3u : 2u) << 13 | (amt & 7) << 10 | R5(rn) << 5 | R5(rd);
	return a64_addsub_reg(sf, op, s, R5(rd), R5(rn), R5(rm), kind, amt);
}

static const uint8_t ld_opc[4][3] = { { 1, 2, 3 }, { 1, 2, 3 }, { 1, 2, 1 }, { 1, 1, 1 } };

/* encode one record into w (returns the word count; the fixup, if any, in *f) */
static unsigned enc(const ins *i, uint32_t *w, pfix *f, const uint32_t *loff, uint32_t at)
{
	int sf = i->sz;

	f->type = 0;
	switch (i->op) {
	case I_LABEL: case I_RAW: case I_NOP: case I_MOVSP:
		return 0;
	case I_ADD: case I_SUB: case I_ADDS: case I_SUBS:
		w[0] = e_addsub_reg(sf, i->op == I_SUB || i->op == I_SUBS, i->op == I_ADDS || i->op == I_SUBS,
				    i->rd, i->rn, i->rm, i->sh ? i->cond : 0, i->sh);
		return 1;
	case I_AND: case I_ORR: case I_EOR: case I_ANDS: case I_BIC: case I_ORN: {
		static const uint8_t opc[] = { [I_AND] = 0, [I_ORR] = 1, [I_EOR] = 2, [I_ANDS] = 3, [I_BIC] = 0, [I_ORN] = 1 };

		w[0] = a64_logic_reg(sf, opc[i->op], i->op == I_BIC || i->op == I_ORN, R5(i->rd), R5(i->rn), R5(i->rm),
				     i->sh ? i->cond : 0, i->sh);
		return 1;
	}
	case I_ADC: case I_SBC: case I_ADCS: case I_SBCS: {
		static const uint32_t b[] = { [I_ADC] = 0x1a000000, [I_SBC] = 0x5a000000, [I_ADCS] = 0x3a000000, [I_SBCS] = 0x7a000000 };

		w[0] = b[i->op] | (uint32_t)sf << 31 | R5(i->rm) << 16 | R5(i->rn) << 5 | R5(i->rd);
		return 1;
	}
	case I_ADDI: case I_SUBI: case I_ADDSI: case I_SUBSI: {
		int64_t v = i->imm;
		bool flags = i->op == I_ADDSI || i->op == I_SUBSI;
		bool sub = i->op == I_SUBI || i->op == I_SUBSI;
		unsigned n;

		if (v < 0 && v != INT64_MIN) {
			v = -v;
			sub = !sub;
		}
		if (!sf)
			v &= 0xffffffff;
		if (v >= 0 && (v < 4096 || ((v & 0xfff) == 0 && v < (1 << 24)) || (!flags && v < (1 << 24)))) {
			int rn = i->rn;

			n = 0;
			if (v >= 4096 && (v & 0xfff)) {
				w[n++] = a64_addsub_imm(sf, sub, flags, R5(i->rd), R5(rn), (unsigned)(v >> 12), 1);
				rn = i->rd;
				v &= 0xfff;
			}
			if ((v & 0xfff) == 0 && v)
				w[n++] = a64_addsub_imm(sf, sub, flags, R5(i->rd), R5(rn), (unsigned)(v >> 12), 1);
			else
				w[n++] = a64_addsub_imm(sf, sub, flags, R5(i->rd), R5(rn), (unsigned)v, 0);
			return n;
		}
		n = e_movi(w, R_X17, sf, (uint64_t)v);
		w[n] = e_addsub_reg(sf, sub, flags, i->rd, i->rn, R_X17, 0, 0);
		return n + 1;
	}
	case I_ANDI: case I_ORRI: case I_EORI: case I_ANDSI: {
		static const uint8_t opc[] = { [I_ANDI] = 0, [I_ORRI] = 1, [I_EORI] = 2, [I_ANDSI] = 3 };
		uint64_t v = sf ? (uint64_t)i->imm : (uint32_t)i->imm;

		w[0] = a64_logic_imm(sf, opc[i->op], R5(i->rd), R5(i->rn), (unsigned)a64_logimm(v, sf));
		return 1;
	}
	case I_MOVI:
		return e_movi(w, i->rd, sf, (uint64_t)i->imm);
	case I_MOV:
		if (i->rd == SP || i->rn == SP)
			w[0] = a64_addsub_imm(sf, 0, 0, R5(i->rd), R5(i->rn), 0, 0);
		else
			w[0] = a64_logic_reg(sf, 1, 0, R5(i->rd), 31, R5(i->rn), 0, 0);
		return 1;
	case I_MADD: case I_MSUB:
		w[0] = a64_madd(sf, i->op == I_MSUB, R5(i->rd), R5(i->rn), R5(i->rm), R5(i->ra));
		return 1;
	case I_SDIV: case I_UDIV: case I_LSLV: case I_LSRV: case I_ASRV: {
		static const uint8_t opc[] = { [I_SDIV] = 3, [I_UDIV] = 2, [I_LSLV] = 8, [I_LSRV] = 9, [I_ASRV] = 10 };

		w[0] = a64_dp2(sf, opc[i->op], R5(i->rd), R5(i->rn), R5(i->rm));
		return 1;
	}
	case I_SMULH: case I_UMULH:
		w[0] = (i->op == I_SMULH ? 0x9b407c00u : 0x9bc07c00u) | R5(i->rm) << 16 | R5(i->rn) << 5 | R5(i->rd);
		return 1;
	case I_SBFM: case I_UBFM: case I_BFM:
		w[0] = a64_bfm(sf, i->op == I_SBFM ? 0 : i->op == I_BFM ? 1 : 2, R5(i->rd), R5(i->rn), (unsigned)i->imm, i->sh);
		return 1;
	case I_CSEL: case I_CSINC: case I_CSINV: case I_CSNEG:
		w[0] = a64_csel(sf, i->op == I_CSINV || i->op == I_CSNEG, i->op == I_CSINC || i->op == I_CSNEG,
				R5(i->rd), R5(i->rn), R5(i->rm), i->cond);
		return 1;
	case I_CLZ: case I_CLS: case I_RBIT: case I_REV: case I_REV16: case I_REV32: {
		uint32_t b;

		switch (i->op) {
		case I_CLZ: b = 0x5ac01000u | (uint32_t)sf << 31; break;
		case I_CLS: b = 0x5ac01400u | (uint32_t)sf << 31; break;
		case I_RBIT: b = 0x5ac00000u | (uint32_t)sf << 31; break;
		case I_REV: b = 0x5ac00800u | (uint32_t)sf << 31 | (sf ? 0x400u : 0); break;
		case I_REV16: b = 0x5ac00400u | (uint32_t)sf << 31; break;
		default: b = 0xdac00800u; break;
		}
		w[0] = b | R5(i->rn) << 5 | R5(i->rd);
		return 1;
	}
	case I_LD: case I_ST: {
		int64_t off = i->imm;
		int sz = i->sz, base = i->rn;
		unsigned n = 0, opc = i->op == I_ST ? 0 : ld_opc[sz][i->cond];
		bool scaled = off >= 0 && (off & ((1 << sz) - 1)) == 0 && (off >> sz) < 4096;

		if (!scaled && !(off >= -256 && off < 256)) {
			n = e_movi(w, R_X17, 1, (uint64_t)off);
			w[n++] = e_addsub_reg(1, 0, 0, R_X17, base, R_X17, 0, 0);
			base = R_X17;
			off = 0;
			scaled = true;
		}
		w[n++] = scaled ? a64_ldst_uoff((unsigned)sz, 0, opc, R5(i->rd), R5(base), (unsigned)(off >> sz))
				: a64_ldst_imm9((unsigned)sz, 0, opc, 0, R5(i->rd), R5(base), (int)off);
		return n;
	}
	case I_LDP: case I_STP:
		w[0] = a64_ldstp(2, 0, i->cond == 0 ? 2 : i->cond == 1 ? 3 : 1, i->op == I_LDP, R5(i->rd), R5(i->ra), R5(i->rn),
				 (int)(i->imm / 8));
		return 1;
	case I_ADRP:
		w[0] = 0x90000000u | R5(i->rd);
		*f = (pfix){ 0, 0, R_AARCH64_ADR_PREL_PG_HI21, i->ref, i->off };
		return 1;
	case I_ADDLO:
		w[0] = a64_addsub_imm(1, 0, 0, R5(i->rd), R5(i->rn), 0, 0);
		*f = (pfix){ 0, 0, R_AARCH64_ADD_ABS_LO12_NC, i->ref, i->off };
		return 1;
	case I_LDLO: case I_STLO: {
		static const uint32_t lo12[] = { R_AARCH64_LDST8_ABS_LO12_NC, R_AARCH64_LDST16_ABS_LO12_NC,
						 R_AARCH64_LDST32_ABS_LO12_NC, R_AARCH64_LDST64_ABS_LO12_NC };
		unsigned opc = i->op == I_STLO ? 0 : ld_opc[i->sz][i->cond];

		w[0] = a64_ldst_uoff(i->sz, 0, opc, R5(i->rd), R5(i->rn), 0);
		*f = (pfix){ 0, 0, lo12[i->sz], i->ref, i->off };
		return 1;
	}
	case I_LDGOT:
		/* two fixups: the caller handles I_LDGOT's second word */
		w[0] = 0x90000000u | R5(i->rd);
		w[1] = a64_ldst_uoff(3, 0, 1, R5(i->rd), R5(i->rd), 0);
		*f = (pfix){ 0, 0, R_AARCH64_ADR_GOT_PAGE, i->ref, 0 };
		return 2;
	case I_ADR:
		if (loff) {
			w[0] = a64_adr(R5(i->rd), ((int64_t)loff[i->label] - at) * 4);
			return 1;
		}
		w[0] = 0x10000000u | R5(i->rd);
		*f = (pfix){ 0, 1, R_AARCH64_ADR_PREL_LO21, i->label, 0 };
		return 1;
	case I_B: case I_BC: case I_CBZ: case I_CBNZ: {
		int64_t d = loff ? ((int64_t)loff[i->label] - at) * 4 : 0;

		switch (i->op) {
		case I_B: w[0] = a64_b(d); break;
		case I_BC: w[0] = a64_bcond(i->cond, d); break;
		default: w[0] = a64_cbz(sf, i->op == I_CBNZ, R5(i->rd), d); break;
		}
		if (!loff)
			*f = (pfix){ 0, 1, i->op == I_B ? R_AARCH64_JUMP26 : R_AARCH64_CONDBR19, i->label, 0 };
		return 1;
	}
	case I_BL:
		w[0] = 0x94000000u;
		*f = (pfix){ 0, 0, R_AARCH64_CALL26, i->ref, 0 };
		return 1;
	case I_BLR:
		w[0] = a64_blr(R5(i->rn));
		return 1;
	case I_BR:
		w[0] = a64_br(R5(i->rn));
		return 1;
	case I_RET:
		w[0] = a64_ret(30);
		return 1;
	case I_BRK:
		w[0] = 0xd4200000u | ((uint32_t)i->imm & 0xffff) << 5;
		return 1;
	case I_PRFM:
		w[0] = 0xf9800000u | R5(i->rn) << 5;	/* prfm pldl1keep */
		return 1;
	case I_MRS:
		w[0] = 0;	/* the caller looks the register up */
		return 1;
	case I_WORD:
		w[0] = (uint32_t)i->imm;
		return 1;
	case I_TLV:
		/* adrp x0, ref@TLVPPAGE; ldr x0, [x0, ref@TLVPPAGEOFF]; ldr x16, [x0]; blr x16 */
		w[0] = 0x90000000u;
		w[1] = a64_ldst_uoff(3, 0, 1, 0, 0, 0);
		w[2] = a64_ldst_uoff(3, 0, 1, 16, 0, 0);
		w[3] = a64_blr(16);
		*f = (pfix){ 0, 0, R_VCC_TLVP_PAGE21, i->ref, 0 };
		return 4;
	}
	return 0;
}

static int r_fail(rctx *R, const char *msg)
{
	elf_error(R->o, "%s", msg);
	return -1;
}

/* emit one instruction's words at the end of frag, with its fixup(s) */
static void r_words(rctx *R, uint32_t frag, const uint32_t *w, unsigned n, const pfix *f, uint32_t fid, const ins *i)
{
	ElfObj *o = R->o;
	uint64_t off;
	uint8_t *p;

	elf_code(o, frag);
	off = elf_frag(o, frag)->size;
	p = elf_grow(o, frag, (size_t)n * 4);
	for (unsigned k = 0; k < n; k++)
		memcpy(p + 4 * k, &w[k], 4);
	if (f->type) {
		uint32_t y = f->lbl ? r_label(R, fid, f->ref) : r_sym(R, f->ref);

		elf_fixup(o, frag, off + 4u * f->k, p + 4 * f->k, f->type, as_val(R->as, y, f->off));
		if (i->op == I_LDGOT || i->op == I_TLV)
			elf_fixup(o, frag, off + 4, p + 4, i->op == I_TLV ? R_VCC_TLVP_PAGEOFF12 : R_AARCH64_LD64_GOT_LO12_NC,
				  as_val(R->as, y, 0));
	}
}

static int r_code(rctx *R, const code_hdr *h)
{
	const ins *in = (const ins *)(h + 1);
	const char *raw = (const char *)(in + h->nin);
	uint32_t w[16];
	pfix f;

	if (!h->sym_labels) {
		/* one block, branches resolved here */
		uint32_t at = 0, frag = as_frag(R->as), *loff = R->loff;
		ElfObj *o = R->o;
		uint64_t base;
		uint8_t *p;

		for (uint32_t k = 0; k < h->nin; k++) {
			if (in[k].op == I_LABEL)
				loff[in[k].label] = at;
			else
				at += enc(&in[k], w, &f, NULL, 0);
		}
		elf_code(o, frag);
		base = elf_frag(o, frag)->size;
		p = elf_grow(o, frag, (size_t)at * 4);
		at = 0;
		for (uint32_t k = 0; k < h->nin; k++) {
			const ins *i = &in[k];
			unsigned n = enc(i, w, &f, loff, at);

			if (unlikely(i->op == I_MRS)) {
				int r = a64_sysreg(R->c->strs[i->ref].s, strlen(R->c->strs[i->ref].s), 0);

				if (r < 0)
					return r_fail(R, "mrs: expected readable system register");
				w[0] = 0xd5200000u | ((unsigned)r & 0xffff) << 5 | R5(i->rd);
			}
			memcpy(p + 4 * at, w, (size_t)n * 4);
			if (f.type) {
				uint32_t y = r_sym(R, f.ref);

				elf_fixup(o, frag, base + 4u * at, p + 4 * at, f.type, as_val(R->as, y, f.off));
				if (i->op == I_LDGOT || i->op == I_TLV)
					elf_fixup(o, frag, base + 4u * at + 4, p + 4 * at + 4,
						  i->op == I_TLV ? R_VCC_TLVP_PAGEOFF12 : R_AARCH64_LD64_GOT_LO12_NC,
						  as_val(R->as, y, 0));
			}
			at += n;
		}
		return 0;
	}
	/* inline asm: in order, labels are symbols */
	for (uint32_t k = 0; k < h->nin; k++) {
		const ins *i = &in[k];
		unsigned n;

		if (i->op == I_RAW) {
			if (as_snippet(R->as, raw + i->off, i->ref))
				return -1;
			continue;
		}
		if (i->op == I_LABEL) {
			if (as_label(R->as, r_label(R, h->fid, i->label)))
				return -1;
			continue;
		}
		n = enc(i, w, &f, NULL, 0);
		if (unlikely(i->op == I_MRS)) {
			int r = a64_sysreg(R->c->strs[i->ref].s, strlen(R->c->strs[i->ref].s), 0);

			if (r < 0)
				return r_fail(R, "mrs: expected readable system register");
			w[0] = 0xd5200000u | ((unsigned)r & 0xffff) << 5 | R5(i->rd);
		}
		if (n)
			r_words(R, as_frag(R->as), w, n, &f, h->fid, i);
	}
	return 0;
}

static void r_zero(ElfObj *o, uint32_t frag, uint64_t n)
{
	if (n)
		elf_fill(o, frag, n, 0);
}

static int r_data(rctx *R, const data_hdr *h)
{
	const ireloc *rel = (const ireloc *)(h + 1);
	const uint8_t *d = (const uint8_t *)(rel + h->nrel);
	ElfObj *o = R->o;
	uint32_t frag = as_frag(R->as);
	uint64_t at = 0;

	if (!h->size)
		return 0;
	elf_data(o, frag);
	if (h->zero) {
		elf_fill(o, frag, h->size, 0);
		return 0;
	}
	for (uint32_t k = 0; k <= h->nrel; k++) {
		uint64_t end = k < h->nrel ? rel[k].off : h->size;

		if (end > at) {
			/* bytes in [at, end): what was initialised, then zeros */
			uint64_t have = at < h->n ? (end < h->n ? end : h->n) - at : 0;

			if (have)
				elf_bytes(o, frag, d + at, have);
			r_zero(o, frag, end - at - have);
			at = end;
		}
		if (k < h->nrel) {
			uint64_t off = elf_frag(o, frag)->size;
			uint8_t *p = elf_grow(o, frag, 8);
			ElfVal v = as_val(R->as, r_sym(R, rel[k].ref), rel[k].add);

			if (!v.add && !v.sub) {
				for (int b = 0; b < 8; b++)
					p[b] = (uint8_t)((uint64_t)v.c >> 8 * b);
			} else {
				memset(p, 0, 8);
				elf_fixup(o, frag, off, p, R_AARCH64_ABS64, v);
			}
			at += 8;
		}
	}
	return 0;
}

/* replay one chunk's items */
static int r_chunk(rctx *R, const char *p, size_t n)
{
	const char *e = p + n;

	while (p < e) {
		const item *it = (const item *)p;
		const char *q = (const char *)(it + 1);
		int r = 0;

		switch (it->kind) {
		case IT_TEXT:
			r = as_snippet(R->as, q, it->len);
			break;
		case IT_CODE:
			r = r_code(R, (const code_hdr *)q);
			break;
		case IT_DATA:
			r = r_data(R, (const data_hdr *)q);
			break;
		case IT_LABEL:
			r = as_label(R->as, r_sym(R, *(const uint32_t *)q));
			break;
		case IT_BYTES:
			if (it->len) {
				elf_data(R->o, as_frag(R->as));
				elf_bytes(R->o, as_frag(R->as), q, it->len);
			}
			break;
		}
		if (r || R->o->has_err || as_failed(R->as))
			return -1;
		p = q + ((it->len + 7) & ~7u);
	}
	return 0;
}

/* ---------------- record emission ---------------- */

static inline ins *put(ecx *e, int op)
{
	ins *i;

	if (unlikely(e->nin == e->capin)) {
		fputs("vcc: function too large\n", stderr);
		abort();
	}
	i = &e->in[e->nin++];
	memset(i, 0, sizeof(*i));
	i->op = (uint8_t)op;
	return i;
}

static inline void op3(ecx *e, int op, int x, int rd, int rn, int rm)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn, i->rm = (uint8_t)rm;
}

static inline void op3s(ecx *e, int op, int x, int rd, int rn, int rm, int kind, int sh)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn, i->rm = (uint8_t)rm;
	i->cond = (uint8_t)kind, i->sh = (uint8_t)sh;
}

static inline void op4(ecx *e, int op, int x, int rd, int rn, int rm, int ra)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn, i->rm = (uint8_t)rm, i->ra = (uint8_t)ra;
}

static inline void opi(ecx *e, int op, int x, int rd, int rn, int64_t imm)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn;
	i->imm = imm;
}

static inline void opbf(ecx *e, int op, int x, int rd, int rn, int immr, int imms)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn;
	i->imm = immr, i->sh = (uint8_t)imms;
}

static inline void movi(ecx *e, int x, int rd, int64_t v) { opi(e, I_MOVI, x, rd, 0, v); }
static inline void mov(ecx *e, int x, int rd, int rn) { if (rd != rn) op3(e, I_MOV, x, rd, rn, 0); }

static inline void cset(ecx *e, int rd, int cond)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, I_CSINC);
	i->sz = 0, i->rd = (uint8_t)rd, i->rn = ZR, i->rm = ZR, i->cond = (uint8_t)(cond ^ 1);
}

static inline void mem(ecx *e, int op, int sz, int ext, int rd, int base, int64_t off)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = (uint8_t)sz, i->cond = (uint8_t)ext, i->rd = (uint8_t)rd, i->rn = (uint8_t)base;
	i->imm = off;
}

static inline void symop(ecx *e, int op, int sz, int ext, int rd, int rn, uint32_t ref, int64_t off)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = (uint8_t)sz, i->cond = (uint8_t)ext, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn;
	i->ref = ref, i->off = (int32_t)off;
}

/* ---------------- local labels and reachability ---------------- */

static uint32_t newlabel(ecx *e)
{
	if (e->nlabel == e->caplabel) {
		fputs("vcc: too many labels\n", stderr);
		abort();
	}
	e->lrefc[e->nlabel] = 0;
	e->lkvi[e->nlabel] = 0;
	return e->nlabel++;
}

static void back_edge(ecx *e, uint32_t l)
{
	efatal(e, e->curloc, "internal: jump back to label %u, %s", l,
	       e->lkvi[l] == LKV_DROPPED ? "dropped as unreachable" : "placed with known values");
}

/* a reachable edge to l (call before counting it in lrefc) */
static void kv_edge(ecx *e, uint32_t l)
{
	kvst *st;

	if (e->lkvi[l] >= LKV_PLACED) {
		if (e->lkvi[l] != LKV_PLACED && !e->dry)
			back_edge(e, l);
		return;
	}
	if (!e->nkv || !e->reach)
		return;
	if (!e->lkvi[l]) {
		if (e->nkvs == CAPKVS) {
			e->nkv = 0, e->kvalid = 0;	/* out of room: stop tracking */
			return;
		}
		st = &e->kvs[e->nkvs++];
		e->lkvi[l] = e->nkvs;
		st->valid = e->lrefc[l] ? 0 : e->kvalid;	/* earlier edges were untracked */
		memcpy(st->v, e->kval, sizeof(st->v));
		return;
	}
	st = &e->kvs[e->lkvi[l] - 1];
	for (uint16_t m = st->valid; m; m &= m - 1) {
		int k = __builtin_ctz(m);

		if (!(e->kvalid >> k & 1) || st->v[k] != e->kval[k])
			st->valid &= (uint16_t)~(1u << k);
	}
}

/* label l is placed: what is known is what every way in agrees on */
static void kv_join(ecx *e, uint32_t l)
{
	const kvst *st;

	if (!e->nkv || !e->lrefc[l])
		return;
	if (!e->lkvi[l] || e->lkvi[l] >= LKV_PLACED) {
		e->kvalid = 0;
		return;
	}
	st = &e->kvs[e->lkvi[l] - 1];
	if (!e->reach) {
		e->kvalid = st->valid;
		memcpy(e->kval, st->v, sizeof(e->kval));
		return;
	}
	for (uint16_t m = e->kvalid; m; m &= m - 1) {
		int k = __builtin_ctz(m);

		if (!(st->valid >> k & 1) || st->v[k] != e->kval[k])
			e->kvalid &= (uint16_t)~(1u << k);
	}
}

static void deflabel(ecx *e, uint32_t l)
{
	kv_join(e, l);
	if (e->lrefc[l])
		e->reach = true;
	if (!e->dry)
		e->lkvi[l] = !e->reach ? LKV_DROPPED : e->kvalid ? LKV_KNOWN : LKV_PLACED;
	if (!e->reach)
		return;
	put(e, I_LABEL)->label = l;
}

/* a label that is reachable from elsewhere whatever the counts say */
static void deflabel_forced(ecx *e, uint32_t l)
{
	e->kvalid = 0;
	e->reach = true;
	if (!e->dry)
		e->lkvi[l] = LKV_PLACED;
	put(e, I_LABEL)->label = l;
}

/* a loop head: the back edges come later */
static void deflabel_loop(ecx *e, uint32_t l, bool forced)
{
	e->kvalid = 0;
	if (forced)
		deflabel_forced(e, l);
	else
		deflabel(e, l);
}

static void jump(ecx *e, uint32_t l)
{
	if (!e->reach)
		return;
	kv_edge(e, l);
	put(e, I_B)->label = l;
	e->lrefc[l]++;
	e->reach = false;
}

static void bcond(ecx *e, int cond, uint32_t l)
{
	ins *i;

	if (!e->reach)
		return;
	kv_edge(e, l);
	i = put(e, I_BC);
	i->cond = (uint8_t)cond;
	i->label = l;
	e->lrefc[l]++;
}

static void cbz(ecx *e, bool nz, int x, int r, uint32_t l)
{
	ins *i;

	if (!e->reach)
		return;
	kv_edge(e, l);
	i = put(e, nz ? I_CBNZ : I_CBZ);
	i->sz = (uint8_t)x, i->rd = (uint8_t)r;
	i->label = l;
	e->lrefc[l]++;
}

/* ---------------- temporaries and frame slots ---------------- */

static int treg(ecx *e)
{
	for (int i = 0; i < NTEMP; i++)
		if (!(e->live & (1u << i))) {
			e->live |= 1u << i;
			if (tempregs[i] >= 19)
				e->usedcs |= 1u << tempregs[i];
			return tempregs[i];
		}
	{
		const fnctx *f = e->f;

		while (f->up)
			f = f->up;
		if (getenv("VCC_LEAKCHK"))
			ediag(e, e->curloc, "here");
		efatal(e, e->c->syms[f->fn].loc, "expression too complex (out of registers) in '%.*s'",
		       (int)vintern_ents[e->c->syms[f->fn].name].len, vintern_ents[e->c->syms[f->fn].name].s);
	}
}

static const int8_t tidx_of[34] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, -1, -1, -1,
	7, 8, 9, 10, 11, 12, 13, 14, 15, 16, -1, -1, -1, -1, -1,
};

static inline int tidx(int r) { return (unsigned)r < 34 ? tidx_of[r] : -1; }

static void tfree(ecx *e, int r)
{
	int i = tidx(r);

	if (i >= 0)
		e->live &= ~(1u << i);
}

static void vfree(ecx *e, const val *v)
{
	if (v->k == V_REG)
		tfree(e, v->r);
	else if (v->k == V_REG2)
		tfree(e, v->r), tfree(e, v->r2);
}

/* a stack slot: returns its offset (FRAME base) */
static int32_t slot(ecx *e, uint64_t size, uint32_t align)
{
	if (align < 1)
		align = 1;
	if (align > 16)
		align = 16;	/* the frame is only 16-aligned; larger alignment is approximated */
	if (size > (1u << 28))
		efatal(e, 0, "stack object too large");
	e->foff = (int32_t)(((uint64_t)e->foff + size + align - 1) & ~(uint64_t)(align - 1));
	if (e->foff > e->fmax)
		e->fmax = e->foff;
	return -e->foff;
}

/* ---------------- values ---------------- */

static inline val vconst(i128 i) { val v = { .k = V_CONST, .i = i }; return v; }
static inline val vreg(int r) { val v = { .k = V_REG, .r = (uint8_t)r }; return v; }
static inline val vaddr(uint32_t ref, i128 off) { val v = { .k = V_ADDR, .ref = ref, .i = off }; return v; }
static inline val vnone(void) { val v = { .k = V_NONE }; return v; }

static void note_ref(ecx *e, uint32_t ref);
static bool sym_needs_got(cc *c, uint32_t ref);

/* address of a symbol into rd */
static void addr_of_ref(ecx *e, int rd, uint32_t ref, int64_t off)
{
	cc *c = e->c;

	note_ref(e, ref);
	if (sym_needs_got(c, ref)) {
		symop(e, I_LDGOT, 1, 0, rd, 0, ref, 0);
		if (off)
			opi(e, I_ADDI, 1, rd, rd, off);
		return;
	}
	if (off < INT32_MIN || off > INT32_MAX) {
		symop(e, I_ADRP, 1, 0, rd, 0, ref, 0);
		symop(e, I_ADDLO, 1, 0, rd, rd, ref, 0);
		opi(e, I_ADDI, 1, rd, rd, (int64_t)off);
		return;
	}
	symop(e, I_ADRP, 1, 0, rd, 0, ref, off);
	symop(e, I_ADDLO, 1, 0, rd, rd, ref, off);
}

/* value into a (new) register; constants are materialised */
static int toreg(ecx *e, val *v)
{
	int r;

	switch (v->k) {
	case V_REG:
		return v->r;
	case V_REG2:
		tfree(e, v->r2);
		*v = vreg(v->r);
		return v->r;
	case V_CONST:
		r = treg(e);
		movi(e, 1, r, (int64_t)v->i);
		*v = vreg(r);
		return r;
	case V_ADDR:
		r = treg(e);
		addr_of_ref(e, r, v->ref, (int64_t)v->i);
		*v = vreg(r);
		return r;
	default:
		r = treg(e);
		movi(e, 1, r, 0);
		*v = vreg(r);
		return r;
	}
}

/* 128-bit value into a register pair */
static void toreg2(ecx *e, val *v, bool sgn)
{
	if (v->k == V_REG2)
		return;
	if (v->k == V_CONST) {
		int lo = treg(e), hi = treg(e);

		movi(e, 1, lo, (int64_t)(uint64_t)(u128)v->i);
		movi(e, 1, hi, (int64_t)(uint64_t)((u128)v->i >> 64));
		v->k = V_REG2, v->r = (uint8_t)lo, v->r2 = (uint8_t)hi;
		return;
	}
	{
		int lo = toreg(e, v), hi = treg(e);

		if (sgn)
			opbf(e, I_SBFM, 1, hi, lo, 63, 63);
		else
			movi(e, 1, hi, 0);
		v->k = V_REG2, v->r = (uint8_t)lo, v->r2 = (uint8_t)hi;
	}
}

/* ---------------- lvalues ---------------- */

enum { L_FRAME, L_SYM, L_REG, L_GLOBREG };

typedef struct lval {
	uint8_t k;
	uint8_t r;		/* L_REG: base register (a temp, owned); L_GLOBREG: the register */
	uint8_t bf;		/* bit-field */
	uint8_t sgn;		/* bit-field signedness */
	uint16_t bitoff, width;	/* bit-field: bits from the byte at off */
	uint32_t ref;		/* L_SYM */
	int64_t off;		/* byte offset (L_FRAME: slot offset) */
	uint32_t align;		/* known alignment of the address */
} lval;

static void lfree(ecx *e, const lval *l)
{
	if (l->k == L_REG)
		tfree(e, l->r);
}

/* base register + offset for a memory access; may load a symbol page into x16 */
static int lbase(ecx *e, const lval *l, int64_t *off, int size)
{
	switch (l->k) {
	case L_FRAME:
		*off = l->off;
		return FRAME;
	case L_REG:
		*off = l->off;
		return l->r;
	default:
		addr_of_ref(e, R_X16, l->ref, l->off);
		*off = 0;
		(void)size;
		return R_X16;
	}
}

static int log2sz(uint64_t n) { return n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : 3; }

static bool sym_needs_got(cc *c, uint32_t ref)
{
	if (RKIND(ref) == RK_NAME)
		return DARWIN(c);
	if (RKIND(ref) != RK_SYM)
		return false;
	{
		const sym *y = &c->syms[canon_sym(c, RIDX(ref))];

		if (DARWIN(c) && y->name && y->sc != SC_STATIC && (y->kind == S_FUNC || y->kind == S_VAR) &&
		    (!(y->flags & (SF_DEFINED | SF_TENTATIVE)) || (y->flags & SF_WEAK) || (y->kind == S_FUNC && !y->body)))
			return true;	/* may live in a dylib: ld64 relaxes the GOT load when it does not */
		return (y->flags & SF_WEAK) && !(y->flags & SF_DEFINED);
	}
}

static uint32_t sym_align(cc *c, uint32_t s);

/* the alignment the object a reference names is known to have */
static uint32_t ref_align(ecx *e, const lval *l)
{
	cc *c = e->c;
	uint32_t a;

	switch (RKIND(l->ref)) {
	case RK_SYM: a = sym_align(c, canon_sym(c, RIDX(l->ref))); break;
	case RK_STR: a = c->strs[RIDX(l->ref)].esz; break;
	case RK_LBL: a = 1; break;
	default: return l->align;
	}
	return a < l->align ? a : l->align;
}

/* load a scalar of size n (1,2,4,8) into rd; ext: 0 zero, 1 sign to x */
static void load_n(ecx *e, int rd, const lval *l, int n, int ext)
{
	int64_t off;
	int sz = log2sz((uint64_t)n);

	if (l->k == L_SYM && !sym_needs_got(e->c, l->ref) && ref_align(e, l) >= (uint32_t)n && (l->off & (n - 1)) == 0 &&
	    l->off >= INT32_MIN && l->off <= INT32_MAX) {
		note_ref(e, l->ref);
		symop(e, I_ADRP, 1, 0, R_X16, 0, l->ref, l->off);
		symop(e, I_LDLO, sz, ext, rd, R_X16, l->ref, l->off);
		return;
	}
	{
		int b = lbase(e, l, &off, n);

		mem(e, I_LD, sz, ext, rd, b, off);
	}
}

static void store_n(ecx *e, int rs, const lval *l, int64_t extra, int n)
{
	int64_t off;
	int sz = log2sz((uint64_t)n);

	if (l->k == L_SYM && !sym_needs_got(e->c, l->ref) && ref_align(e, l) >= (uint32_t)n && ((l->off + extra) & (n - 1)) == 0 &&
	    l->off + extra >= INT32_MIN && l->off + extra <= INT32_MAX) {
		note_ref(e, l->ref);
		symop(e, I_ADRP, 1, 0, R_X16, 0, l->ref, l->off + extra);
		symop(e, I_STLO, sz, 0, rs, R_X16, l->ref, l->off + extra);
		return;
	}
	{
		int b = lbase(e, l, &off, n);

		mem(e, I_ST, sz, 0, rs, b, off + extra);
	}
}

/* address of an lvalue into a new register (or a constant address) */
static val laddr(ecx *e, lval *l)
{
	int r;

	switch (l->k) {
	case L_SYM:
		return vaddr(l->ref, l->off);
	case L_FRAME:
		r = treg(e);
		opi(e, I_ADDI, 1, r, FRAME, l->off);
		return vreg(r);
	case L_REG:
		if (l->off)
			opi(e, I_ADDI, 1, l->r, l->r, l->off);
		l->off = 0;
		return vreg(l->r);
	default:
		r = treg(e);
		mov(e, 1, r, l->r);
		return vreg(r);
	}
}

/* lvalue from an address value */
static lval lfromval(ecx *e, val v, uint32_t align)
{
	lval l = { 0 };

	l.align = align;
	if (v.k == V_ADDR) {
		l.k = L_SYM;
		l.ref = v.ref;
		l.off = (int64_t)v.i;
		return l;
	}
	l.k = L_REG;
	l.r = (uint8_t)toreg(e, &v);
	return l;
}

/* copy n bytes between two addresses (registers); small copies inline */
static void call_fixed(ecx *e, int fn, int a0, int a1, int a2, int64_t n);

static void copy_mem(ecx *e, int dst, int src, uint64_t n)
{
	if (n <= 128) {
		uint64_t o = 0;
		int t = R_X16;	/* not x17: the backend uses it for large offsets */

		while (o < n) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			mem(e, I_LD, log2sz(k), 0, t, src, (int64_t)o);
			mem(e, I_ST, log2sz(k), 0, t, dst, (int64_t)o);
			o += k;
		}
		return;
	}
	call_fixed(e, FN_MEMCPY, dst, src, -1, (int64_t)n);
}

static void zero_mem(ecx *e, int dst, uint64_t n)
{
	if (n <= 256) {
		uint64_t o = 0;

		while (o < n) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			mem(e, I_ST, log2sz(k), 0, ZR, dst, (int64_t)o);
			o += k;
		}
		return;
	}
	call_fixed(e, FN_MEMSET, dst, -2, -1, (int64_t)n);
}

static int fpk(ecx *e, qty t);
static int f_cmp(ecx *e, int op, int d, val *a, val *b);
static val f_tobool(ecx *e, val v, qty t);
static val call_named(ecx *e, uint32_t ref, val *vals, qty *tys, int n, qty rty, bool noret);

/* ---------------- AAPCS64 (floating point in v0-v7, HFAs not done) ---------------- */

/* a homogeneous floating-point aggregate (top-level members only): goes in v registers, not done */
static bool is_hfa(cc *c, qty t)
{
	const tag *g = &c->tags[T(c, t)->aux];
	bool hfa = g->nfield > 0 && g->nfield <= 4;

	for (uint32_t k = 0; hfa && k < g->nfield; k++)
		hfa = is_flt(c, g->f[k].ty) && KIND(c, g->f[k].ty) == KIND(c, g->f[0].ty);
	return hfa;
}

enum { A_IGNORE, A_REG, A_STACK, A_REF_REG, A_REF_STACK, A_VREG };	/* A_VREG: float/double in v<reg> */

typedef struct abiarg {
	uint8_t kind, reg, nreg;
	int32_t stk;		/* A_STACK / A_REF_STACK: offset in the argument area */
	uint32_t size;
} abiarg;

/* Apple: an anonymous (variadic) argument always goes on the stack in 8-byte slots */
static void classify_va(ecx *e, qty t, int *nsaa, abiarg *a)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	uint32_t al = talign(c, t) > 8 ? 16 : 8;

	memset(a, 0, sizeof(*a));
	a->size = (uint32_t)size;
	if (is_record(c, t) && size > 16) {
		a->kind = A_REF_STACK;
		a->stk = *nsaa;
		*nsaa += 8;
		return;
	}
	*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
	a->kind = A_STACK;
	a->stk = *nsaa;
	a->nreg = (uint8_t)(size > 8 ? 2 : 1);
	*nsaa += a->nreg * 8;
}

static void classify(ecx *e, qty t, int *ngrn, int *nsaa, abiarg *a)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int n;
	uint32_t al = talign(c, t);

	memset(a, 0, sizeof(*a));
	a->size = (uint32_t)size;
	if (is_flt(c, t)) {
		(void)fpk(e, t);
		if (e->nsrn < 8) {
			a->kind = A_VREG;
			a->reg = (uint8_t)e->nsrn++;
			a->nreg = 1;
			return;
		}
		e->nsrn = 8;
		n = 1;
		goto stack;
	}
	if (is_recabi(c, t)) {
		if (is_hfa(c, t))
			efatal(e, e->curloc, "floating-point aggregates as arguments or results are not supported");
		if (size == 0) {
			a->kind = A_IGNORE;
			return;
		}
		if (size > 16) {
			if (*ngrn < 8) {
				a->kind = A_REF_REG;
				a->reg = (uint8_t)(*ngrn)++;
				a->nreg = 1;
			} else {
				a->kind = A_REF_STACK;
				a->stk = *nsaa;
				*nsaa += 8;
			}
			return;
		}
		n = (int)((size + 7) / 8);
	} else {
		n = size == 16 ? 2 : 1;
	}
	if (n == 2 && al == 16 && (*ngrn & 1))
		(*ngrn)++;
	if (*ngrn + n <= 8) {
		a->kind = A_REG;
		a->reg = (uint8_t)*ngrn;
		a->nreg = (uint8_t)n;
		*ngrn += n;
		return;
	}
	*ngrn = 8;
stack:
	if (DARWIN(c) && !is_record(c, t)) {
		/* Apple: scalars on the stack take their natural size and alignment */
		*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
		a->kind = A_STACK;
		a->stk = *nsaa;
		a->nreg = (uint8_t)n;
		a->size = (uint32_t)size;
		*nsaa += (int)size;
		return;
	}
	if (al < 8)
		al = 8;
	if (al > 16)
		al = 16;
	*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
	a->kind = A_STACK;
	a->stk = *nsaa;
	a->nreg = (uint8_t)n;
	*nsaa += n * 8;
}

/* ---------------- spilling caller-saved temporaries around calls ---------------- */

typedef struct spill {
	uint32_t mask;
	int32_t slot[NTEMP];
} spill;

static void spill_live(ecx *e, spill *s)
{
	s->mask = 0;
	for (int i = 0; i < NTEMP; i++)
		if ((e->live & (1u << i)) && tempregs[i] < 19) {
			s->slot[i] = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, tempregs[i], FRAME, s->slot[i]);
			s->mask |= 1u << i;
		}
}

static void unspill(ecx *e, const spill *s)
{
	for (int i = 0; i < NTEMP; i++)
		if (s->mask & (1u << i))
			mem(e, I_LD, 3, 0, tempregs[i], FRAME, s->slot[i]);
}

/* memcpy(a0, a1, n) / memset(a0, 0, n) with a0/a1 in temporaries */
static void call_fixed(ecx *e, int fn, int a0, int a1, int a2, int64_t n)
{
	spill sp;
	ins *i;

	(void)a2;
	if (!e->reach)
		return;
	e->calls = true;
	spill_live(e, &sp);
	mov(e, 1, 0, a0);
	if (a1 == -2)
		movi(e, 0, 1, 0);
	else
		mov(e, 1, 1, a1);
	movi(e, 1, 2, n);
	i = put(e, I_BL);
	i->ref = FIXED(fn);
	unspill(e, &sp);
}

/* ---------------- the emitter proper ---------------- */

static val gen(ecx *e, uint32_t n);
static val vla_bytes(ecx *e, qty t);
static lval gen_lval(ecx *e, uint32_t n);
static void gen_stmt(ecx *e, uint32_t n);
static void gen_cond(ecx *e, uint32_t n, bool jump_if, uint32_t l);
static val gen_block(ecx *e, uint32_t n, bool want);
static void gen_init_local(ecx *e, const lval *dst, qty ty, uint32_t init);
static void enqueue(emitter *E, uint32_t item);

static inline int X(cc *c, qty t) { return tsize(c, t) == 8 || is_ptr(c, t) || is_func(c, t) ? 1 : 0; }

static void note_ref(ecx *e, uint32_t ref)
{
	emitter *E = e->E;
	cc *c = e->c;

	if (!e->reach || e->dry)
		return;
	if (RKIND(ref) == RK_STR) {
		uint32_t i = RIDX(ref);

		if (!atomic_load_explicit(&E->strdone[i], memory_order_relaxed) &&
		    !atomic_exchange(&E->strdone[i], 1))
			enqueue(E, 0x80000000u | i);
		return;
	}
	if (RKIND(ref) != RK_SYM)
		return;
	{
		uint32_t s = canon_sym(c, RIDX(ref));
		const sym *y = &c->syms[s];

		if (unlikely((y->flags & SF_WEAK) && !(y->flags & SF_DEFINED)) &&
		    !atomic_load_explicit(&E->extref[s], memory_order_relaxed) &&
		    !atomic_exchange(&E->extref[s], 1))
			e->weakref[e->nweakref++] = s;
		if (atomic_load_explicit(&E->claimed[s], memory_order_relaxed))
			return;
		if (y->kind == S_FUNC) {
			if (!(y->flags & SF_DEFINED) || y->sc != SC_STATIC || !y->body)
				return;
		} else if (y->kind == S_VAR) {
			bool local_static = y->depth > 0 && y->sc == SC_STATIC;
			bool file_static = y->depth == 0 && y->sc == SC_STATIC;

			if (!(local_static || file_static || !y->name))
				return;
			if (!(y->flags & (SF_DEFINED | SF_TENTATIVE)) && !local_static && y->name)
				return;
		} else {
			return;
		}
		if (!atomic_exchange(&E->claimed[s], 1))
			enqueue(E, s);
	}
}

/* ---------------- bindings ---------------- */

static bind *binding(ecx *e, uint32_t s)
{
	return &e->b[s];
}

static int regvar_reg(cc *c, uint32_t s)
{
	const attrs *a = sym_attrs(c, s);
	const char *n;

	if (c->syms[s].sc != SC_REGISTER || !a->asmname)
		return -1;
	n = c->strs[a->asmname - 1].s;
	if (!strcmp(n, "sp"))
		return SP;
	if ((n[0] == 'x' || n[0] == 'w') && n[1] >= '0' && n[1] <= '9')
		return atoi(n + 1);
	return -1;
}

static int32_t bind_slot(ecx *e, uint32_t s)
{
	cc *c = e->c;
	bind *b = binding(e, s);
	qty t = c->syms[s].ty;
	uint32_t al = talign(c, t);

	if (c->syms[s].align > al)
		al = c->syms[s].align;
	b->inst = e->f->inst;
	b->kind = B_SLOT;
	b->kv = 0;
	b->reg = 0xff;
	if (c->syms[s].sc == SC_REGISTER) {
		int rr = regvar_reg(c, s);

		if (rr >= 0)
			b->reg = (uint8_t)rr;
	}
	b->slot = slot(e, tsize(c, t) ? tsize(c, t) : 1, al);
	return b->slot;
}

/* the lvalue of a local (auto/param/register) variable */
static lval local_lval(ecx *e, uint32_t s)
{
	cc *c = e->c;
	bind *b = binding(e, s);
	lval l = { 0 };
	qty t = c->syms[s].ty;

	l.align = talign(c, t);
	if (b->kind == B_NONE || b->inst == 0) {
		/* declaration not executed (jumped over, or dead): give it a slot */
		bind_slot(e, s);
	} else if (b->kind == B_CONST) {
		val v = b->v;
		int r;

		bind_slot(e, s);
		if (is_wide(c, t)) {
			toreg2(e, &v, is_signed(c, t));
			mem(e, I_ST, 3, 0, v.r, FRAME, b->slot);
			mem(e, I_ST, 3, 0, v.r2, FRAME, b->slot + 8);
		} else {
			r = toreg(e, &v);
			mem(e, I_ST, log2sz(tsize(c, t)), 0, r, FRAME, b->slot);
		}
		vfree(e, &v);
	}
	if (b->kind == B_INDIRECT) {
		int r = treg(e);

		mem(e, I_LD, 3, 0, r, FRAME, b->slot);
		l.k = L_REG;
		l.r = (uint8_t)r;
		return l;
	}
	l.k = L_FRAME;
	l.off = b->slot;
	return l;
}

static uint32_t sym_align(cc *c, uint32_t s)
{
	uint32_t a = talign(c, c->syms[s].ty);

	if (c->syms[s].align > a)
		a = c->syms[s].align;
	return a;
}

static bool is_local_var(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	return y->kind == S_VAR && y->depth > 0 && (y->sc == SC_AUTO || y->sc == SC_REGISTER || (y->flags & SF_PARAM) ||
						   (y->sc == SC_NONE && !(y->flags & SF_GLOBAL)));
}


/* ---------------- lvalues from expressions ---------------- */

static uint32_t align_at(uint64_t off, uint32_t a)
{
	while (a > 1 && (off & (a - 1)))
		a >>= 1;
	return a ? a : 1;
}

static uint64_t elem_size(cc *c, qty ptr)
{
	qty b = T(c, ptr)->base;
	int k = KIND(c, b);

	if (k == TY_VOID || k == TY_FUNC)
		return 1;
	return tsize(c, b);
}

/* p + i * scale, as a value */
static val ptr_add(ecx *e, val p, val i, int64_t scale, bool sub)
{
	if (i.k == V_CONST) {
		i128 d = i.i * scale;

		if (sub)
			d = -d;
		if (p.k == V_CONST || p.k == V_ADDR) {
			p.i = p.k == V_CONST ? (i128)(uint64_t)(p.i + d) : p.i + d;
			return p;
		}
		{
			int r = toreg(e, &p);

			if (d)
				opi(e, I_ADDI, 1, r, r, (int64_t)d);
			return p;
		}
	}
	{
		int rp = toreg(e, &p), ri = toreg(e, &i);

		if (scale > 0 && !(scale & (scale - 1))) {
			op3s(e, sub ? I_SUB : I_ADD, 1, rp, rp, ri, 0, __builtin_ctzll((uint64_t)scale));
		} else {
			movi(e, 1, R_X16, scale);
			op4(e, I_MADD, 1, ri, ri, R_X16, ZR);
			op3(e, sub ? I_SUB : I_ADD, 1, rp, rp, ri);
		}
		tfree(e, ri);
		return p;
	}
}

static val vla_bytes(ecx *e, qty t);

/* p + i elements of a pointer to a VLA: the element size is computed now */
static val ptr_add_vla(ecx *e, val p, val i, qty ptr, bool sub)
{
	val b = vla_bytes(e, e->c->ty[QI(ptr)].base);
	int ri = toreg(e, &i), rb = toreg(e, &b);

	op4(e, I_MADD, 1, ri, ri, rb, ZR);
	vfree(e, &b);
	return ptr_add(e, p, i, 1, sub);
}

static bool ptr_to_vla(cc *c, qty ptr)
{
	const ctype *b = T(c, T(c, ptr)->base);

	return b->kind == TY_ARRAY && (b->flags & TF_VLA);
}

static lval gen_lval(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	lval l = { 0 };

	switch (x->op) {
	case N_VAR: {
		uint32_t s = x->a, cs;

		if (is_local_var(c, s))
			return local_lval(e, s);
		cs = canon_sym(c, s);
		if (c->syms[cs].sc == SC_REGISTER && c->syms[cs].depth == 0) {
			int r = regvar_reg(c, cs);

			if (r < 0)
				efatal(e, x->loc, "unsupported global register variable");
			l.k = L_GLOBREG;
			l.r = (uint8_t)r;
			return l;
		}
		if (DARWIN(c) && ((c->syms[cs].flags | c->syms[s].flags) & SF_THREAD)) {
			/* Darwin TLV: the descriptor's thunk returns the address in x0 */
			int r;

			note_ref(e, REF(RK_SYM, cs));
			e->calls = true;
			if (e->reach)
				put(e, I_TLV)->ref = REF(RK_SYM, cs);
			r = treg(e);
			mov(e, 1, r, 0);
			l.k = L_REG;
			l.r = (uint8_t)r;
			l.align = sym_align(c, cs);
			return l;
		}
		l.k = L_SYM;
		l.ref = REF(RK_SYM, cs);
		l.align = sym_align(c, cs);
		return l;
	}
	case N_STR:
		l.k = L_SYM;
		l.ref = REF(RK_STR, x->a);
		l.align = c->strs[x->a].esz;
		return l;
	case N_DEREF: {
		val p = gen(e, x->a);
		qty bt = T(c, N(x->a)->ty)->base;

		return lfromval(e, p, KIND(c, bt) == TY_VOID ? 1 : talign(c, bt));
	}
	case N_MEMBER: {
		const field *f = &c->tags[x->b].f[x->c];
		const node *b = N(x->a);

		if (b->flags & NF_LVALUE || b->op == N_VAR || b->op == N_DEREF || b->op == N_MEMBER || b->op == N_INDEX ||
		    b->op == N_COMPLIT || b->op == N_STR)
			l = gen_lval(e, x->a);
		else
			l = lfromval(e, gen(e, x->a), talign(c, b->ty));
		l.off += (int64_t)(f->off / 8);
		l.align = align_at((uint64_t)l.off, l.align ? l.align : 1);
		if (l.k != L_SYM && l.k != L_FRAME)
			l.align = align_at(f->off / 8, talign(c, f->ty) < l.align ? talign(c, f->ty) : l.align);
		if (f->bitfield) {
			l.bf = 1;
			l.bitoff = (uint16_t)(f->off % 8);
			l.width = f->width;
			l.sgn = is_signed(c, f->ty);
		}
		return l;
	}
	case N_INDEX: {
		val p = gen(e, x->a), i = gen(e, x->b);
		uint64_t es = elem_size(c, N(x->a)->ty);
		qty et = T(c, N(x->a)->ty)->base;

		if (ptr_to_vla(c, N(x->a)->ty))
			return lfromval(e, ptr_add_vla(e, p, i, N(x->a)->ty, false), talign(c, et));
		if (i.k == V_CONST && (p.k == V_ADDR)) {
			l.k = L_SYM;
			l.ref = p.ref;
			l.off = (int64_t)(p.i + i.i * (i128)es);
			l.align = talign(c, et);
			return l;
		}
		if (i.k == V_CONST && p.k == V_REG) {
			l.k = L_REG;
			l.r = p.r;
			l.off = (int64_t)(i.i * (i128)es);
			l.align = talign(c, et);
			return l;
		}
		p = ptr_add(e, p, i, (int64_t)es, false);
		return lfromval(e, p, talign(c, et));
	}
	case N_COMPLIT: {
		uint32_t s = x->b;
		const sym *y = &c->syms[s];

		if (y->sc == SC_STATIC || (y->flags & SF_GLOBAL)) {
			l.k = L_SYM;
			l.ref = REF(RK_SYM, s);
			l.align = sym_align(c, s);
			return l;
		}
		{
			int32_t sl = bind_slot(e, s);
			lval d = { .k = L_FRAME, .off = sl, .align = sym_align(c, s) };

			gen_init_local(e, &d, y->ty, x->a);
			return d;
		}
	}
	case N_CAST:
		if (x->aux == CK_NOP && (N(x->a)->flags & NF_LVALUE))
			return gen_lval(e, x->a);
		/* fall through */
	default: {
		val v = gen(e, n);

		if (!is_agg(c, x->ty) && !is_func(c, x->ty))
			efatal(e, x->loc, "not an lvalue (node %d)", x->op);
		return lfromval(e, v, talign(c, x->ty));
	}
	}
}

/* ---------------- loads and stores of typed values ---------------- */

static int bf_bytes(int bits) { return bits <= 8 ? 1 : bits <= 16 ? 2 : bits <= 32 ? 4 : 8; }

static val load(ecx *e, lval *l, qty t)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int r;

	if (is_agg(c, t) || is_func(c, t))
		return laddr(e, l);
	if (is_flt(c, t))
		fpk(e, t);	/* bits, zero-extended (below: not signed) */
	if (l->k == L_GLOBREG) {
		r = treg(e);
		mov(e, 1, r, l->r);
		return vreg(r);
	}
	if (l->bf) {
		int bits = l->bitoff + l->width, nb = bf_bytes(bits);

		if (bits > 64)
			efatal(e, 0, "unsupported bit-field layout");
		r = treg(e);
		load_n(e, r, l, nb, 0);
		opbf(e, l->sgn ? I_SBFM : I_UBFM, 1, r, r, l->bitoff, l->bitoff + l->width - 1);
		return vreg(r);
	}
	if (size == 16) {
		val v = { .k = V_REG2 };
		lval h = *l;

		v.r = (uint8_t)treg(e);
		v.r2 = (uint8_t)treg(e);
		load_n(e, v.r, l, 8, 0);
		h.off += 8;
		load_n(e, v.r2, &h, 8, 0);
		return v;
	}
	r = treg(e);
	if (size == 0)
		movi(e, 1, r, 0);
	else
		load_n(e, r, l, (int)size, size < 8 && !is_flt(c, t) && is_signed(c, t) ? 1 : 0);
	return vreg(r);
}

/* store v (of type t) to l; v is consumed unless keep; returns v (in a register if kept) */
static void store(ecx *e, lval *l, qty t, val *v)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);

	if (is_agg(c, t)) {
		val d = laddr(e, l);
		int rs = toreg(e, v), rd = toreg(e, &d);

		copy_mem(e, rd, rs, size);
		vfree(e, &d);
		return;
	}
	if (l->k == L_GLOBREG) {
		int r = toreg(e, v);

		mov(e, 1, l->r, r);
		return;
	}
	if (l->bf) {
		int bits = l->bitoff + l->width, nb = bf_bytes(bits);
		int r = toreg(e, v), w = treg(e);
		lval l2 = *l;

		load_n(e, w, &l2, nb, 0);
		/* bfi w, r, #bitoff, #width = bfm w, r, #(-bitoff mod 64), #(width-1) */
		opbf(e, I_BFM, 1, w, r, (64 - l->bitoff) & 63, l->width - 1);
		store_n(e, w, l, 0, nb);
		tfree(e, w);
		return;
	}
	if (size == 16) {
		toreg2(e, v, true);
		store_n(e, v->r, l, 0, 8);
		store_n(e, v->r2, l, 8, 8);
		return;
	}
	if (size == 0)
		return;
	if (v->k == V_CONST && v->i == 0) {
		store_n(e, ZR, l, 0, (int)size);
		return;
	}
	{
		int r = toreg(e, v);

		store_n(e, r, l, 0, (int)size);
	}
}

/* ---------------- integer helpers ---------------- */

/* extend the low `from` bytes of r to 64 bits */
static void extend(ecx *e, int rd, int rs, int from, bool sgn)
{
	switch (from) {
	case 1: opbf(e, sgn ? I_SBFM : I_UBFM, 1, rd, rs, 0, 7); break;
	case 2: opbf(e, sgn ? I_SBFM : I_UBFM, 1, rd, rs, 0, 15); break;
	case 4:
		if (sgn)
			opbf(e, I_SBFM, 1, rd, rs, 0, 31);
		else
			op3(e, I_MOV, 0, rd, rs, 0);
		break;
	default:
		mov(e, 1, rd, rs);
	}
}

static val fold_int_bin(ecx *e, int op, qty ot, qty rt, i128 l, i128 r, bool *ok)
{
	cc *c = e->c;
	bool sg = is_signed(c, ot);
	i128 v = 0;
	u128 ul = (u128)trunc_to(c, l, ot), ur = (u128)trunc_to(c, r, ot);

	if (!sg && tsize(c, ot) < 16) {
		ul = (u128)trunc_to(c, l, ot) & (((u128)1 << (tsize(c, ot) * 8)) - 1);
		ur = (u128)trunc_to(c, r, ot) & (((u128)1 << (tsize(c, ot) * 8)) - 1);
	}
	*ok = true;
	switch (op) {
	case T_PLUS: v = l + r; break;
	case T_MINUS: v = l - r; break;
	case T_STAR: v = (i128)((u128)l * (u128)r); break;
	case T_SLASH:
	case T_PERCENT:
		if (r == 0 || (sg && r == -1)) {
			*ok = false;
			return vnone();
		}
		if (sg)
			v = op == T_SLASH ? l / r : l % r;
		else
			v = (i128)(op == T_SLASH ? ul / ur : ul % ur);
		break;
	case T_LESSLESS:
		if (r < 0 || (uint64_t)r >= tsize(c, rt) * 8) {
			*ok = false;
			return vnone();
		}
		v = (i128)((u128)l << r);
		break;
	case T_GREATERGREATER:
		if (r < 0 || (uint64_t)r >= tsize(c, rt) * 8) {
			*ok = false;
			return vnone();
		}
		v = is_signed(c, rt) ? trunc_to(c, l, rt) >> r : (i128)(ul >> r);
		break;
	case T_AMP: v = l & r; break;
	case T_PIPE: v = l | r; break;
	case T_CARET: v = l ^ r; break;
	case T_LESS: v = sg ? trunc_to(c, l, ot) < trunc_to(c, r, ot) : ul < ur; break;
	case T_LESSEQ: v = sg ? trunc_to(c, l, ot) <= trunc_to(c, r, ot) : ul <= ur; break;
	case T_GREATER: v = sg ? trunc_to(c, l, ot) > trunc_to(c, r, ot) : ul > ur; break;
	case T_GREATEREQ: v = sg ? trunc_to(c, l, ot) >= trunc_to(c, r, ot) : ul >= ur; break;
	case T_EQUALEQUAL: v = ul == ur; break;
	case T_EXCLAIMEQ: v = ul != ur; break;
	default:
		*ok = false;
		return vnone();
	}
	return vconst(trunc_to(c, v, rt));
}

static int cmp_cond(int op, bool sg)
{
	switch (op) {
	case T_EQUALEQUAL: return C_EQ;
	case T_EXCLAIMEQ: return C_NE;
	case T_LESS: return sg ? C_LT : C_LO;
	case T_LESSEQ: return sg ? C_LE : C_LS;
	case T_GREATER: return sg ? C_GT : C_HI;
	default: return sg ? C_GE : C_HS;
	}
}

static bool is_cmp(int op)
{
	return op == T_LESS || op == T_LESSEQ || op == T_GREATER || op == T_GREATEREQ || op == T_EQUALEQUAL || op == T_EXCLAIMEQ;
}

/* compare a with b (width x); returns the condition for "op is true" */
static int emit_cmp(ecx *e, int op, int x, bool sg, val *a, val *b)
{
	int ra = toreg(e, a);

	if (b->k == V_CONST && b->i >= -4095 && b->i <= 4095) {
		if (b->i < 0)
			opi(e, I_ADDSI, x, ZR, ra, (int64_t)-b->i);
		else
			opi(e, I_SUBSI, x, ZR, ra, (int64_t)b->i);
	} else {
		int rb = toreg(e, b);

		op3(e, I_SUBS, x, ZR, ra, rb);
	}
	return cmp_cond(op, sg);
}

/* 128-bit compare: returns the condition */
static int emit_cmp2(ecx *e, int op, bool sg, val *a, val *b)
{
	toreg2(e, a, sg);
	toreg2(e, b, sg);
	if (op == T_EQUALEQUAL || op == T_EXCLAIMEQ) {
		op3(e, I_EOR, 1, a->r, a->r, b->r);
		op3(e, I_EOR, 1, a->r2, a->r2, b->r2);
		op3(e, I_ORR, 1, a->r, a->r, a->r2);
		op3(e, I_SUBS, 1, ZR, a->r, ZR);
		return op == T_EQUALEQUAL ? C_EQ : C_NE;
	}
	if (op == T_GREATER || op == T_LESSEQ) {
		val t = *a;

		*a = *b;
		*b = t;
		op = op == T_GREATER ? T_LESS : T_GREATEREQ;
	}
	op3(e, I_SUBS, 1, ZR, a->r, b->r);
	op3(e, I_SBCS, 1, ZR, a->r2, b->r2);
	return cmp_cond(op, sg);
}

/* ---------------- truth tests ---------------- */

/* branch to l if (v != 0) == nz; consumes v.  Returns the constant truth or -1. */
static int test_zero(ecx *e, val *v, qty t, uint32_t l, bool nz)
{
	cc *c = e->c;
	uint64_t sz = tsize(c, t);
	int r;

	if (v->k == V_CONST || v->k == V_ADDR) {
		bool truth = v->k == V_ADDR || v->i != 0;

		if (truth == nz)
			jump(e, l);
		return truth;
	}
	if (v->k == V_NONE)
		return -1;
	if (is_flt(c, t)) {
		int d = fpk(e, t), cond = f_cmp(e, T_EXCLAIMEQ, d, v, NULL);

		bcond(e, nz ? cond : C_EQ, l);
		vfree(e, v);
		return -1;
	}
	if (v->k == V_REG2) {
		op3(e, I_ORR, 1, R_X16, v->r, v->r2);
		cbz(e, nz, 1, R_X16, l);
		vfree(e, v);
		return -1;
	}
	r = v->r;
	if (is_ptr(c, t) || sz == 8) {
		cbz(e, nz, 1, r, l);
	} else if (sz == 4) {
		cbz(e, nz, 0, r, l);
	} else {
		opi(e, I_ANDSI, 0, ZR, r, sz == 2 ? 0xffff : 0xff);
		bcond(e, nz ? C_NE : C_EQ, l);
	}
	vfree(e, v);
	return -1;
}

/* v as 0/1 (int) */
static val tobool(ecx *e, val v, qty t)
{
	cc *c = e->c;
	uint64_t sz = tsize(c, t);
	int r;

	if (is_flt(c, t))
		return f_tobool(e, v, t);
	if (v.k == V_CONST)
		return vconst(v.i != 0);
	if (v.k == V_ADDR)
		return vconst(1);
	if (v.k == V_REG2) {
		op3(e, I_ORR, 1, v.r, v.r, v.r2);
		tfree(e, v.r2);
		v = vreg(v.r);
		sz = 8;
	}
	r = toreg(e, &v);
	if (KIND(c, t) == TY_BOOL)
		return v;
	if (is_ptr(c, t) || sz == 8)
		op3(e, I_SUBS, 1, ZR, r, ZR);
	else if (sz == 4)
		op3(e, I_SUBS, 0, ZR, r, ZR);
	else
		opi(e, I_ANDSI, 0, ZR, r, sz == 2 ? 0xffff : 0xff);
	cset(e, r, C_NE);
	return v;
}

/* ---------------- floating point ---------------- */

/*
 * float and double values live in general registers as their bit patterns
 * (a float zero-extended); an operation moves them to the scratch FP/SIMD
 * registers v16-v18 with fmov, works there and moves the result back.  So
 * registers, spills, loads, stores and calls stay integer code.  Constants
 * are folded with the host's IEEE arithmetic.  long double is double on
 * Darwin; _Float16 and 128-bit long double are not supported.
 */
static inline void word(ecx *e, uint32_t w)
{
	if (e->reach)
		put(e, I_WORD)->imm = w;
}

enum { FV0 = 16, FV1 = 17 };

static int fpk(ecx *e, qty t)
{
	uint64_t sz = tsize(e->c, t);

	if (sz != 4 && sz != 8)
		efatal(e, e->curloc, "%s floating point is not supported", sz == 2 ? "16-bit" : "128-bit");
	return sz == 8;
}

static inline uint32_t f_in(int d, int vr, int r) { return (d ? 0x9e670000u : 0x1e270000u) | (unsigned)r << 5 | (unsigned)vr; }	/* fmov v, x/w */
static inline uint32_t f_out(int d, int r, int vr) { return (d ? 0x9e660000u : 0x1e260000u) | (unsigned)vr << 5 | (unsigned)r; }	/* fmov x/w, v */

static double f_val(int d, i128 bits)
{
	if (d) {
		uint64_t u = (uint64_t)bits;
		double x;

		memcpy(&x, &u, 8);
		return x;
	} else {
		uint32_t u = (uint32_t)bits;
		float x;

		memcpy(&x, &u, 4);
		return x;
	}
}

static val f_const(int d, double x)
{
	if (d) {
		uint64_t u;

		memcpy(&u, &x, 8);
		return vconst((i128)u);
	} else {
		float f = (float)x;
		uint32_t u;

		memcpy(&u, &f, 4);
		return vconst((i128)u);
	}
}

/* v into FP register vr */
static void f_load(ecx *e, int d, int vr, val *v)
{
	int r = toreg(e, v);

	word(e, f_in(d, vr, r));
}

static val f_arith(ecx *e, int op, qty ty, val a, val b)
{
	int d = fpk(e, ty), ra;
	uint32_t w;

	if (a.k == V_CONST && b.k == V_CONST) {
		double x = f_val(d, a.i), y = f_val(d, b.i);

		switch (op) {
		case T_PLUS: return f_const(d, d ? x + y : (double)((float)x + (float)y));
		case T_MINUS: return f_const(d, d ? x - y : (double)((float)x - (float)y));
		case T_STAR: return f_const(d, d ? x * y : (double)((float)x * (float)y));
		case T_SLASH: return f_const(d, d ? x / y : (double)((float)x / (float)y));
		}
	}
	switch (op) {
	case T_PLUS: w = 0x1e202800u; break;
	case T_MINUS: w = 0x1e203800u; break;
	case T_STAR: w = 0x1e200800u; break;
	case T_SLASH: w = 0x1e201800u; break;
	default: efatal(e, e->curloc, "unsupported floating point operator");
	}
	f_load(e, d, FV0, &a);
	f_load(e, d, FV1, &b);
	vfree(e, &b);
	ra = a.r;
	word(e, w | (unsigned)d << 22 | FV1 << 16 | FV0 << 5 | FV0);
	word(e, f_out(d, ra, FV0));
	return a;
}

/* fcmp a, b (b none: #0.0); the condition for op */
static int f_cmp(ecx *e, int op, int d, val *a, val *b)
{
	f_load(e, d, FV0, a);
	if (b) {
		f_load(e, d, FV1, b);
		word(e, 0x1e202000u | (unsigned)d << 22 | FV1 << 16 | FV0 << 5);
	} else {
		word(e, 0x1e202008u | (unsigned)d << 22 | FV0 << 5);
	}
	switch (op) {
	case T_EQUALEQUAL: return C_EQ;
	case T_EXCLAIMEQ: return C_NE;	/* true when unordered */
	case T_LESS: return C_MI;
	case T_LESSEQ: return C_LS;
	case T_GREATER: return C_GT;
	case T_GREATEREQ: return C_GE;
	}
	efatal(e, e->curloc, "unsupported floating point comparison");
}

static val f_compare(ecx *e, int op, qty t, val a, val b)
{
	int d = fpk(e, t), cond;

	if (a.k == V_CONST && b.k == V_CONST) {
		double x = f_val(d, a.i), y = f_val(d, b.i);

		switch (op) {
		case T_EQUALEQUAL: return vconst(x == y);
		case T_EXCLAIMEQ: return vconst(x != y);
		case T_LESS: return vconst(x < y);
		case T_LESSEQ: return vconst(x <= y);
		case T_GREATER: return vconst(x > y);
		case T_GREATEREQ: return vconst(x >= y);
		}
	}
	cond = f_cmp(e, op, d, &a, &b);
	vfree(e, &b);
	cset(e, a.r, cond);
	return vreg(a.r);
}

/* x != 0.0 as 0/1 (NaN is true, -0.0 false) */
static val f_tobool(ecx *e, val v, qty t)
{
	int d = fpk(e, t);

	if (v.k == V_CONST)
		return vconst(f_val(d, v.i) != 0);
	f_cmp(e, T_EXCLAIMEQ, d, &v, NULL);
	cset(e, v.r, C_NE);
	return v;
}

static val f_neg(ecx *e, val v, qty t)
{
	int d = fpk(e, t);

	if (v.k == V_CONST)
		return f_const(d, -f_val(d, v.i));
	f_load(e, d, FV0, &v);
	word(e, 0x1e214000u | (unsigned)d << 22 | FV0 << 5 | FV0);
	word(e, f_out(d, v.r, FV0));
	return v;
}

static val f_cast(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool ff = is_flt(c, from), tf = is_flt(c, to);

	if (v.k == V_NONE)
		return v;
	if (KIND(c, to) == TY_BOOL)
		return f_tobool(e, v, from);
	if (ff && tf) {
		int fd = fpk(e, from), td = fpk(e, to);

		if (fd == td)
			return v;
		if (v.k == V_CONST)
			return f_const(td, f_val(fd, v.i));
		f_load(e, fd, FV0, &v);
		word(e, (fd ? 0x1e624000u : 0x1e22c000u) | FV0 << 5 | FV0);	/* fcvt s<-d / d<-s */
		word(e, f_out(td, v.r, FV0));
		return v;
	}
	if (tf) {
		/* integer -> floating */
		int td = fpk(e, to);
		uint64_t fs = tsize(c, from);
		bool sg = !is_ptr(c, from) && is_signed(c, from);

		if (fs == 16) {
			/* __floattidf & co. */
			val r;
			qty ft = sg ? prim(TY_INT128) : prim(TY_UINT128);

			if (v.k == V_CONST)
				return f_const(td, sg ? (double)v.i : (double)(u128)v.i);
			r = call_named(e, FIXED(sg ? (td ? FN_FLOATTIDF : FN_FLOATTISF) : (td ? FN_FLOATUNTIDF : FN_FLOATUNTISF)),
				       &v, &ft, 1, to, false);
			return r;
		}
		if (v.k == V_CONST) {
			i128 x = trunc_to(c, v.i, from);

			return f_const(td, sg ? (double)(int64_t)x : (double)(uint64_t)x);
		}
		toreg(e, &v);
		if (fs < 4)
			extend(e, v.r, v.r, (int)fs, sg);
		/* scvtf/ucvtf v16, x/w */
		word(e, (fs == 8 ? 0x9e220000u : 0x1e220000u) | (sg ? 0 : 0x10000u) | (unsigned)td << 22 | (unsigned)v.r << 5 | FV0);
		word(e, f_out(td, v.r, FV0));
		return v;
	}
	{
		/* floating -> integer (fcvtzs/fcvtzu, x or w) */
		int fd = fpk(e, from);
		uint64_t ts = tsize(c, to);
		bool sg = !is_ptr(c, to) && is_signed(c, to);

		if (ts == 16) {
			/* __fixdfti & co. */
			if (v.k == V_CONST) {
				double x = f_val(fd, v.i);

				return vconst(sg ? (i128)x : (i128)(u128)x);
			}
			return call_named(e, FIXED(sg ? (fd ? FN_FIXDFTI : FN_FIXSFTI) : (fd ? FN_FIXUNSDFTI : FN_FIXUNSSFTI)),
					  &v, &from, 1, to, false);
		}
		if (v.k == V_CONST) {
			double x = f_val(fd, v.i);

			return vconst(trunc_to(c, sg ? (i128)(int64_t)x : (i128)(uint64_t)x, to));
		}
		f_load(e, fd, FV0, &v);
		word(e, (ts == 8 ? 0x9e380000u : 0x1e380000u) | (sg ? 0 : 0x10000u) | (unsigned)fd << 22 | FV0 << 5 | (unsigned)v.r);
		if (ts < 4)
			extend(e, v.r, v.r, (int)ts, sg);
		return v;
	}
}

/* ---------------- casts ---------------- */

static val cast(ecx *e, val v, qty from, qty to, int kind)
{
	cc *c = e->c;
	uint64_t fs = tsize(c, from), ts = tsize(c, to);

	if ((is_flt(c, from) || is_flt(c, to)) && kind != CK_VOID)
		return f_cast(e, v, from, to);
	switch (kind) {
	case CK_NOP:
	case CK_P2P:
		if (v.k == V_CONST && !is_ptr(c, to))
			v.i = trunc_to(c, v.i, to);
		return v;
	case CK_BOOL:
		return tobool(e, v, from);
	case CK_NULL:
		vfree(e, &v);
		return vconst(0);
	case CK_INT:
	case CK_P2I:
	case CK_I2P:
		if (v.k == V_CONST) {
			if (kind == CK_I2P)
				return vconst((i128)(uint64_t)trunc_to(c, v.i, from));
			return vconst(trunc_to(c, v.i, kind == CK_P2I && ts == 16 ? prim(TY_UINT128) : to));
		}
		if (v.k == V_ADDR) {
			if (ts == 8)
				return v;
			toreg(e, &v);
		}
		if (v.k == V_NONE)
			return v;
		if (ts == 16 && fs < 16) {
			bool sg = kind == CK_INT && is_signed(c, from);
			int r = toreg(e, &v);

			if (fs < 8)
				extend(e, r, r, (int)fs, sg);
			toreg2(e, &v, sg);
			return v;
		}
		if (fs == 16 && ts < 16) {
			if (v.k == V_REG2) {
				tfree(e, v.r2);
				v = vreg(v.r);
			}
			return v;
		}
		if (ts > fs) {
			int r = toreg(e, &v);

			extend(e, r, r, (int)fs, kind != CK_P2I && is_signed(c, from));
		}
		return v;
	case CK_VOID:
		vfree(e, &v);
		return vnone();
	default:
		return v;
	}
}

/* ---------------- arithmetic ---------------- */

static void shift_imm(ecx *e, int op, int x, int rd, int rn, int k)
{
	int w = x ? 64 : 32;

	k &= w - 1;
	if (op == T_LESSLESS)
		opbf(e, I_UBFM, x, rd, rn, (w - k) % w, w - 1 - k);
	else if (op == I_ASRV)
		opbf(e, I_SBFM, x, rd, rn, k, w - 1);
	else
		opbf(e, I_UBFM, x, rd, rn, k, w - 1);
}

static val arith(ecx *e, int op, qty ty, val a, val b)
{
	cc *c = e->c;
	int x = X(c, ty), ra, rb;
	bool sg = is_signed(c, ty);

	/* commutative: constant on the right */
	if (a.k == V_CONST && b.k != V_CONST && (op == T_PLUS || op == T_STAR || op == T_AMP || op == T_PIPE || op == T_CARET)) {
		val t = a;

		a = b;
		b = t;
	}
	ra = toreg(e, &a);
	if (b.k == V_CONST) {
		int64_t k = (int64_t)b.i;
		uint64_t m = x ? (uint64_t)k : (uint32_t)k;

		switch (op) {
		case T_PLUS:
			opi(e, I_ADDI, x, ra, ra, x ? k : (int32_t)k);
			return a;
		case T_MINUS:
			opi(e, I_SUBI, x, ra, ra, x ? k : (int32_t)k);
			return a;
		case T_STAR:
			if (m && !(m & (m - 1))) {
				shift_imm(e, T_LESSLESS, x, ra, ra, __builtin_ctzll(m));
				return a;
			}
			break;
		case T_AMP: case T_PIPE: case T_CARET:
			if (logimm_ok(m, x)) {
				opi(e, op == T_AMP ? I_ANDI : op == T_PIPE ? I_ORRI : I_EORI, x, ra, ra, (int64_t)m);
				return a;
			}
			if (op == T_AMP && m == 0) {
				movi(e, x, ra, 0);
				return a;
			}
			if ((op == T_PIPE || op == T_CARET) && m == 0)
				return a;
			break;
		case T_LESSLESS:
			shift_imm(e, T_LESSLESS, x, ra, ra, (int)k);
			return a;
		case T_GREATERGREATER:
			shift_imm(e, sg ? I_ASRV : I_LSRV, x, ra, ra, (int)k);
			return a;
		}
	}
	rb = toreg(e, &b);
	switch (op) {
	case T_PLUS: op3(e, I_ADD, x, ra, ra, rb); break;
	case T_MINUS: op3(e, I_SUB, x, ra, ra, rb); break;
	case T_STAR: op4(e, I_MADD, x, ra, ra, rb, ZR); break;
	case T_SLASH: op3(e, sg ? I_SDIV : I_UDIV, x, ra, ra, rb); break;
	case T_PERCENT:
		op3(e, sg ? I_SDIV : I_UDIV, x, R_X16, ra, rb);
		op4(e, I_MSUB, x, ra, R_X16, rb, ra);
		break;
	case T_AMP: op3(e, I_AND, x, ra, ra, rb); break;
	case T_PIPE: op3(e, I_ORR, x, ra, ra, rb); break;
	case T_CARET: op3(e, I_EOR, x, ra, ra, rb); break;
	case T_LESSLESS: op3(e, I_LSLV, x, ra, ra, rb); break;
	case T_GREATERGREATER: op3(e, sg ? I_ASRV : I_LSRV, x, ra, ra, rb); break;
	default:
		efatal(e, 0, "unsupported operator %d", op);
	}
	tfree(e, rb);
	return a;
}


/* 128-bit arithmetic on register pairs */
static val arith2(ecx *e, int op, qty ty, val a, val b)
{
	cc *c = e->c;
	bool sg = is_signed(c, ty);

	if (op == T_LESSLESS || op == T_GREATERGREATER) {
		if (b.k == V_CONST) {
			int k = (int)(b.i & 127);

			toreg2(e, &a, sg);
			if (k == 0)
				return a;
			if (op == T_LESSLESS) {
				if (k >= 64) {
					shift_imm(e, T_LESSLESS, 1, a.r2, a.r, k - 64);
					movi(e, 1, a.r, 0);
				} else {
					shift_imm(e, T_LESSLESS, 1, a.r2, a.r2, k);
					shift_imm(e, I_LSRV, 1, R_X16, a.r, 64 - k);
					op3(e, I_ORR, 1, a.r2, a.r2, R_X16);
					shift_imm(e, T_LESSLESS, 1, a.r, a.r, k);
				}
			} else {
				if (k >= 64) {
					shift_imm(e, sg ? I_ASRV : I_LSRV, 1, a.r, a.r2, k - 64);
					if (sg)
						shift_imm(e, I_ASRV, 1, a.r2, a.r2, 63);
					else
						movi(e, 1, a.r2, 0);
				} else {
					shift_imm(e, I_LSRV, 1, a.r, a.r, k);
					shift_imm(e, T_LESSLESS, 1, R_X16, a.r2, 64 - k);
					op3(e, I_ORR, 1, a.r, a.r, R_X16);
					shift_imm(e, sg ? I_ASRV : I_LSRV, 1, a.r2, a.r2, k);
				}
			}
			return a;
		}
		{
			/* variable shift: both halves, then select on bit 6 of the count */
			int n, t1, t2, t3;

			toreg2(e, &a, sg);
			n = toreg(e, &b);
			t1 = treg(e), t2 = treg(e), t3 = treg(e);
			if (op == T_LESSLESS) {
				shift_imm(e, I_LSRV, 1, t1, a.r, 1);
				op3(e, I_ORN, 0, t2, ZR, n);
				op3(e, I_LSLV, 1, t3, a.r2, n);
				op3(e, I_LSRV, 1, t1, t1, t2);
				op3(e, I_ORR, 1, t1, t3, t1);		/* high, n < 64 */
				op3(e, I_LSLV, 1, t2, a.r, n);		/* low << n */
				opi(e, I_ANDSI, 1, ZR, n, 64);
				op3s(e, I_CSEL, 1, a.r2, t2, t1, C_NE, 0);
				op3s(e, I_CSEL, 1, a.r, ZR, t2, C_NE, 0);
			} else {
				shift_imm(e, T_LESSLESS, 1, t1, a.r2, 1);
				op3(e, I_ORN, 0, t2, ZR, n);
				op3(e, I_LSRV, 1, t3, a.r, n);
				op3(e, I_LSLV, 1, t1, t1, t2);
				op3(e, I_ORR, 1, t1, t1, t3);		/* low, n < 64 */
				op3(e, sg ? I_ASRV : I_LSRV, 1, t2, a.r2, n);	/* high >> n */
				if (sg)
					shift_imm(e, I_ASRV, 1, t3, a.r2, 63);
				opi(e, I_ANDSI, 1, ZR, n, 64);
				op3s(e, I_CSEL, 1, a.r, t2, t1, C_NE, 0);
				op3s(e, I_CSEL, 1, a.r2, sg ? t3 : ZR, t2, C_NE, 0);
			}
			tfree(e, t1), tfree(e, t2), tfree(e, t3);
			vfree(e, &b);
			return a;
		}
	}
	if (op == T_SLASH || op == T_PERCENT) {
		val args[2] = { a, b };
		qty tys[2] = { ty, ty };
		int fn = op == T_SLASH ? (sg ? FN_DIVTI3 : FN_UDIVTI3) : (sg ? FN_MODTI3 : FN_UMODTI3);

		return call_named(e, FIXED(fn), args, tys, 2, ty, false);
	}
	toreg2(e, &a, sg);
	toreg2(e, &b, sg);
	switch (op) {
	case T_PLUS:
		op3(e, I_ADDS, 1, a.r, a.r, b.r);
		op3(e, I_ADC, 1, a.r2, a.r2, b.r2);
		break;
	case T_MINUS:
		op3(e, I_SUBS, 1, a.r, a.r, b.r);
		op3(e, I_SBC, 1, a.r2, a.r2, b.r2);
		break;
	case T_AMP: op3(e, I_AND, 1, a.r, a.r, b.r); op3(e, I_AND, 1, a.r2, a.r2, b.r2); break;
	case T_PIPE: op3(e, I_ORR, 1, a.r, a.r, b.r); op3(e, I_ORR, 1, a.r2, a.r2, b.r2); break;
	case T_CARET: op3(e, I_EOR, 1, a.r, a.r, b.r); op3(e, I_EOR, 1, a.r2, a.r2, b.r2); break;
	case T_STAR:
		/* hi = umulh(alo, blo) + alo * bhi + ahi * blo; lo = alo * blo */
		op3(e, I_UMULH, 1, R_X16, a.r, b.r);
		op4(e, I_MADD, 1, R_X16, a.r, b.r2, R_X16);
		op4(e, I_MADD, 1, a.r2, a.r2, b.r, R_X16);
		op4(e, I_MADD, 1, a.r, a.r, b.r, ZR);
		break;
	default:
		efatal(e, 0, "unsupported 128-bit operator %d", op);
	}
	vfree(e, &b);
	return a;
}

/* ---------------- calls ---------------- */

typedef struct carg {
	val v;
	qty t;
	abiarg a;
	int32_t slot;		/* value parked in a frame slot (-1: v is a constant/address) */
} carg;

#define MAXARGS 128

/* park an argument value: registers go to a slot, aggregates are copied */
static void park(ecx *e, carg *a)
{
	cc *c = e->c;

	if (a->slot != 1)
		return;		/* parked while the arguments were evaluated */
	if (is_agg(c, a->t)) {
		uint64_t sz = (tsize(c, a->t) + 7) & ~(uint64_t)7;
		int32_t sl = slot(e, sz ? sz : 8, talign(c, a->t) > 8 ? 16 : 8);
		int rs = toreg(e, &a->v), rd = treg(e);

		opi(e, I_ADDI, 1, rd, FRAME, sl);
		copy_mem(e, rd, rs, tsize(c, a->t));
		tfree(e, rd);
		vfree(e, &a->v);
		a->slot = sl;
		return;
	}
	if (a->v.k == V_REG) {
		a->slot = slot(e, 8, 8);
		mem(e, I_ST, 3, 0, a->v.r, FRAME, a->slot);
		vfree(e, &a->v);
	} else if (a->v.k == V_REG2) {
		a->slot = slot(e, 16, 16);
		mem(e, I_ST, 3, 0, a->v.r, FRAME, a->slot);
		mem(e, I_ST, 3, 0, a->v.r2, FRAME, a->slot + 8);
		vfree(e, &a->v);
	}
}

/* put 8-byte word k of argument a into register rd */
static void arg_word(ecx *e, const carg *a, int k, int rd)
{
	if (a->slot != 1) {
		mem(e, I_LD, 3, 0, rd, FRAME, a->slot + 8 * k);
		return;
	}
	if (a->v.k == V_CONST) {
		movi(e, 1, rd, (int64_t)(uint64_t)((u128)a->v.i >> (64 * k)));
	} else if (a->v.k == V_ADDR) {
		addr_of_ref(e, rd, a->v.ref, (int64_t)a->v.i);
	} else {
		movi(e, 1, rd, 0);
	}
}

/*
 * The call proper: args are evaluated (vals); callee is a ref (direct) or a
 * value (indirect).  Returns the result value.
 */
/*
 * A call to an error() function survived folding.  clang -O2 would have
 * inlined the static function this code belongs to into its callers, where
 * the arguments are constants: mark it, and emit_tu generates the TU again
 * with it inlined.
 */
static void need_fold(ecx *e)
{
	const fnctx *f = e->f;
	uint32_t s;

	while (f->up)
		f = f->up;
	s = f->fn;
	if (e->c->syms[s].sc == SC_STATIC && !e->E->mf[s]) {
		e->E->mf[s] = 1;
		e->E->newmf++;
	}
}

/* nfix: the callee's named parameters if it is variadic, else -1 */
static val do_call(ecx *e, uint32_t ref, val *fnv, carg *args, int n, qty rty, bool noret, int nfix)
{
	cc *c = e->c;
	int ngrn = 0, nsaa = 0;
	int32_t fslot = 1, rslot = 1;
	bool sret = is_recabi(c, rty) && tsize(c, rty) > 16;
	spill sp;
	val res = vnone();

	e->calls = true;
	if (fnv) {
		if (fnv->k == V_ADDR && fnv->i == 0) {
			ref = fnv->ref;
			note_ref(e, ref);
		} else {
			int r = toreg(e, fnv);

			fslot = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, r, FRAME, fslot);
			vfree(e, fnv);
		}
	} else {
		note_ref(e, ref);
	}
	e->nsrn = 0;
	if (is_record(c, rty) && is_hfa(c, rty))
		efatal(e, e->curloc, "floating-point aggregates as arguments or results are not supported");
	for (int i = 0; i < n; i++) {
		park(e, &args[i]);
		if (nfix >= 0 && i >= nfix && DARWIN(c))
			classify_va(e, args[i].t, &nsaa, &args[i].a);
		else
			classify(e, args[i].t, &ngrn, &nsaa, &args[i].a);
	}
	if (DARWIN(c))
		nsaa = (nsaa + 7) & ~7;
	if (nsaa > e->outmax)
		e->outmax = nsaa;
	if (sret)
		rslot = slot(e, tsize(c, rty), talign(c, rty) > 16 ? 16 : talign(c, rty));
	if (!e->reach)
		return is_void(c, rty) ? vnone() : vconst(0);
	if (unlikely(RKIND(ref) == RK_SYM && !fnv && sym_attrs(c, RIDX(ref))->error_fn)) {
		/*
		 * An error() call that folding did not remove.  clang removed it
		 * (the tree builds with clang), with value ranges vcc does not
		 * have (min(len, ...) inside while (len > 0)).  Drop the call and
		 * go on, as the kernel's own -O0 fallback does
		 * (__compiletime_assert is then ((void)(condition))): a trap
		 * here would fire on paths clang proved are fine.
		 */
		if (!e->dry) {
			need_fold(e);
			e->E->nerrcall++;
			if (e->E->stats)
				ediag(e, e->curloc, "error() call '%.*s' not folded: dropped", (int)vintern_ents[c->syms[RIDX(ref)].name].len,
				      vintern_ents[c->syms[RIDX(ref)].name].s);
		}
		return is_void(c, rty) ? vnone() : vconst(0);
	}
	spill_live(e, &sp);
	for (int i = 0; i < n; i++) {
		carg *a = &args[i];

		switch (a->a.kind) {
		case A_REG:
			for (int k = 0; k < a->a.nreg; k++)
				arg_word(e, a, k, a->a.reg + k);
			break;
		case A_STACK:
			if (DARWIN(c) && a->a.size < 8 && !is_agg(c, a->t)) {	/* Apple: packed */
				arg_word(e, a, 0, R_X16);
				mem(e, I_ST, log2sz(a->a.size), 0, R_X16, SP, a->a.stk);
				break;
			}
			for (int k = 0; k < a->a.nreg; k++) {
				arg_word(e, a, k, R_X16);
				mem(e, I_ST, 3, 0, R_X16, SP, a->a.stk + 8 * k);
			}
			break;
		case A_REF_REG:
			opi(e, I_ADDI, 1, a->a.reg, FRAME, a->slot);
			break;
		case A_REF_STACK:
			opi(e, I_ADDI, 1, R_X16, FRAME, a->slot);
			mem(e, I_ST, 3, 0, R_X16, SP, a->a.stk);
			break;
		case A_VREG:
			arg_word(e, a, 0, R_X16);
			word(e, f_in(a->a.size == 8, a->a.reg, R_X16));
			break;
		}
	}
	if (sret)
		opi(e, I_ADDI, 1, 8, FRAME, rslot);
	if (fslot != 1) {
		mem(e, I_LD, 3, 0, R_X16, FRAME, fslot);
		put(e, I_BLR)->rn = R_X16;
	} else {
		put(e, I_BL)->ref = ref;
	}
	if (is_void(c, rty)) {
		res = vnone();
	} else if (sret) {
		int r = treg(e);

		opi(e, I_ADDI, 1, r, FRAME, rslot);
		res = vreg(r);
	} else if (is_recabi(c, rty)) {
		int32_t sl = slot(e, 16, 16);
		int r = treg(e);

		mem(e, I_ST, 3, 0, 0, FRAME, sl);
		if (tsize(c, rty) > 8)
			mem(e, I_ST, 3, 0, 1, FRAME, sl + 8);
		opi(e, I_ADDI, 1, r, FRAME, sl);
		res = vreg(r);
	} else if (tsize(c, rty) == 16 && !is_flt(c, rty)) {
		res.k = V_REG2;
		res.r = (uint8_t)treg(e);
		res.r2 = (uint8_t)treg(e);
		mov(e, 1, res.r, 0);
		mov(e, 1, res.r2, 1);
	} else if (is_flt(c, rty)) {
		int r = treg(e);

		word(e, f_out(fpk(e, rty), r, 0));
		res = vreg(r);
	} else {
		int r = treg(e);

		mov(e, 1, r, 0);
		res = vreg(r);
	}
	unspill(e, &sp);
	if (noret)
		e->reach = false;
	return res;
}

static val call_named(ecx *e, uint32_t ref, val *vals, qty *tys, int n, qty rty, bool noret)
{
	carg a[8];

	for (int i = 0; i < n; i++) {
		a[i].v = vals[i];
		a[i].t = tys[i];
		a[i].slot = 1;
	}
	return do_call(e, ref, NULL, a, n, rty, noret, -1);
}

/* ---------------- inline expansion ---------------- */

static bool on_inline_stack(ecx *e, uint32_t fn)
{
	for (fnctx *f = e->f; f; f = f->up)
		if (f->fn == fn)
			return true;
	return false;
}

static void ensure_walked(emitter *E, cc *c, uint32_t s);

/* a local only ever assigned constants: follow its value (v: the initializer, or none) */
static void kv_track(ecx *e, uint32_t s, const val *v)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	bind *b = binding(e, s);
	int k;

	if ((e->E->sflags[s] & (SA_WRITTEN | SA_CWRITE)) != SA_CWRITE || !is_integer(c, y->ty) || is_wide(c, y->ty) ||
	    (QQ(y->ty) & Q_VOLATILE) || sym_attrs(c, s)->cleanup || e->nkv == NKV)
		return;
	k = e->nkv++;
	b->kv = (uint8_t)(k + 1);
	e->kvalid &= (uint16_t)~(1u << k);
	if (v && v->k == V_CONST) {
		e->kval[k] = (int64_t)trunc_to(c, v->i, y->ty);
		e->kvalid |= (uint16_t)(1u << k);
	}
}

/* may variable s (param or local) be bound to the constant v for good? */
static bool const_var(ecx *e, uint32_t s, const val *v)
{
	uint8_t f = e->E->sflags[s];

	if (f & (SA_WRITTEN | SA_CMULTI))
		return false;
	if (f & SA_CWRITE)
		return v->k == V_CONST && trunc_to(e->c, v->i, e->c->syms[s].ty) == e->E->cwval[s];
	return v->k == V_CONST || v->k == V_ADDR;
}

#define INLINE_NODES 40	/* AST nodes: a few statements */

static bool can_inline(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];

	if (!(y->flags & SF_DEFINED) || !y->body)
		return false;
	if (!(y->flags & SF_ALWAYS_INLINE)) {
		/*
		 * A plain inline function that calls an error() function (the
		 * kernel's BUILD_BUG_ON) only compiles when an optimizing compiler
		 * inlines it and folds the call away: inline it too.
		 */
		if (y->flags & SF_NOINLINE)
			return false;
		if (!e->E->mf[s]) {
			if (!(y->flags & SF_INLINE))
				return false;
			ensure_walked(e->E, c, s);
			/*
			 * clang -O2 inlines small inline functions, and the kernel
			 * leans on it: scs_is_enabled() { return false; } guards
			 * references to objects that only exist when it is true.
			 */
			if (!(e->E->sflags[s] & SA_MUSTFOLD) && e->E->fnsize[s] > INLINE_NODES)
				return false;
		}
	}
	if (T(c, y->ty)->flags & TF_VARIADIC)
		return false;
	if (e->inline_depth >= 48 || on_inline_stack(e, s))
		return false;
	return true;
}

static void run_cleanups(ecx *e, int to);


static val inline_call(ecx *e, uint32_t s, carg *args, int n)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const node *body = N(y->body);
	fnctx F;
	qty rty = T(c, y->ty)->base;
	val res;

	ensure_walked(e->E, c, s);
	memset(&F, 0, sizeof(F));
	F.fn = s;
	F.inst = ++e->inst_next;
	F.inl = true;
	F.rty = rty;
	F.retlbl = newlabel(e);
	F.cst_ok = true;
	F.cbase = e->nclean;
	F.nloop = e->nloop;
	F.up = e->f;
	if (!is_void(c, rty))
		F.retslot = slot(e, tsize(c, rty) ? tsize(c, rty) : 8, talign(c, rty) > 16 ? 16 : talign(c, rty));
	for (int i = 0; i < n; i++)
		if (args[i].slot != 1) {	/* parked: back in a register */
			if (is_agg(c, args[i].t)) {
				int r = treg(e);

				opi(e, I_ADDI, 1, r, FRAME, args[i].slot);
				args[i].v = vreg(r);
			} else {
				lval l = { .k = L_FRAME, .off = args[i].slot, .align = 8 };

				args[i].v = load(e, &l, args[i].t);
			}
			args[i].slot = 1;
		}
	for (int i = 0; i < (int)body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *ps = &c->syms[p];
		bind *b = binding(e, p);
		val v = i < n ? args[i].v : vconst(0);

		if (const_var(e, p, &v) && !is_agg(c, ps->ty) &&
		    !(QQ(ps->ty) & Q_VOLATILE) && !is_wide(c, ps->ty)) {
			b->inst = F.inst;
			b->kind = B_CONST;
			b->kv = 0;
			b->v = v;
			continue;
		}
		{
			int32_t sl;
			lval l = { .k = L_FRAME };

			b->inst = 0;
			sl = bind_slot(e, p);
			binding(e, p)->inst = F.inst;
			l.off = sl;
			l.align = talign(c, ps->ty);
			store(e, &l, ps->ty, &v);
			vfree(e, &v);
		}
	}
	e->f = &F;
	e->inline_depth++;
	e->E->ninl++;
	gen_stmt(e, y->body);
	e->inline_depth--;
	e->f = F.up;
	deflabel(e, F.retlbl);
	if (is_void(c, rty))
		return vnone();
	if (F.cst_ok && F.any_ret)
		return F.cst;
	{
		lval l = { .k = L_FRAME, .off = F.retslot, .align = talign(c, rty) };

		res = load(e, &l, rty);
	}
	return res;
}

/* the callee sym of a direct call, or 0 */
static uint32_t direct_callee(cc *c, uint32_t fn, uint32_t *ref)
{
	const node *f = N(fn);

	while (f->op == N_CAST && (f->aux == CK_FDECAY || f->aux == CK_NOP || f->aux == CK_P2P) &&
	       (N(f->a)->op == N_VAR || N(f->a)->op == N_CAST))
		f = N(f->a);
	if (f->op != N_VAR)
		return 0;
	if (c->syms[f->a].kind == S_BUILTIN) {
		*ref = REF(RK_NAME, (uint32_t)c->syms[f->a].val);
		return 0;
	}
	if (c->syms[f->a].kind != S_FUNC)
		return 0;
	/* calls through a cast to another function type stay direct only if the types agree */
	if (N(fn)->op == N_CAST && N(N(fn)->a)->op == N_CAST)
		return 0;
	*ref = REF(RK_SYM, canon_sym(c, f->a));
	return canon_sym(c, f->a);
}

static uint32_t strip_casts(cc *c, uint32_t n);
static bool side_effects(cc *c, uint32_t n);

/* __builtin_strcmp of two string literals (the kernel picks operators by name) */
static val fold_strcmp(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t f = strip_casts(c, x->a), a = strip_casts(c, c->lists[x->b]), b = strip_casts(c, c->lists[x->b + 1]);
	int r;

	if (N(f)->op != N_VAR || c->syms[N(f)->a].kind != S_BUILTIN || N(a)->op != N_STR || N(b)->op != N_STR ||
	    strcmp(cc_builtin_name(c->syms[N(f)->a].val), "__builtin_strcmp"))
		return vnone();
	r = strcmp(c->strs[N(a)->a].s, c->strs[N(b)->a].s);
	return vconst(r < 0 ? -1 : r > 0);
}

static val gen_call(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t ref = 0, s = direct_callee(c, x->a, &ref);
	carg args[MAXARGS];
	int na = (int)x->c;
	val fnv = vnone();
	bool noret = false, inl;

	if (na > MAXARGS)
		efatal(e, x->loc, "too many arguments");
	if (s) {
		const sym *y = &c->syms[s];

		noret = (y->flags & SF_NORETURN) != 0;
		/* use the definition's flags (always_inline may be on a later declaration) */
	}
	if (!s && ref && na == 2) {
		val f = fold_strcmp(e, x);

		if (f.k == V_CONST)
			return f;
	}
	if (!s && !ref)
		fnv = gen(e, x->a);
	inl = s && can_inline(e, s);
	if (inl)
		ensure_walked(e->E, c, s);
	for (int i = 0; i < na; i++) {
		uint32_t an = c->lists[x->b + i];

		/* an argument the inlined body never reads: only its side effects count */
		if (inl && i < (int)N(c->syms[s].body)->aux &&
		    !(e->E->sflags[c->lists[N(c->syms[s].body)->a + i]] & SA_USED) && !side_effects(c, an) &&
		    !is_agg(c, c->syms[c->lists[N(c->syms[s].body)->a + i]].ty)) {
			args[i].v = vconst(0);
			args[i].t = N(an)->ty;
			args[i].slot = 1;
			continue;
		}
		args[i].v = gen(e, an);
		args[i].t = N(an)->ty;
		args[i].slot = 1;
		/* many arguments: keep the registers for evaluating the rest */
		if (__builtin_popcount(e->live) > NTEMP / 2 && (args[i].v.k == V_REG || args[i].v.k == V_REG2))
			park(e, &args[i]);
	}
	if (inl)
		return inline_call(e, s, args, na);
	{
		qty ft = N(x->a)->ty;
		int nfix = -1;

		if (is_ptr(c, ft))
			ft = T(c, ft)->base;
		if (is_func(c, ft) && (T(c, ft)->flags & TF_VARIADIC))
			nfix = (int)T(c, ft)->n;
		if (s || ref)
			return do_call(e, ref, NULL, args, na, x->ty, noret, nfix);
		return do_call(e, 0, &fnv, args, na, x->ty, false, nfix);
	}
}

/* ---------------- builtins ---------------- */

static uint32_t strip_casts(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST)
		n = N(n)->a;
	return n;
}

/* clang's Expr::HasSideEffects (with possible effects): calls, writes, volatile reads */
static bool side_effects(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	if ((QQ(x->ty) & Q_VOLATILE) && (x->op == N_VAR || x->op == N_DEREF || x->op == N_MEMBER || x->op == N_INDEX))
		return true;
	switch (x->op) {
	case N_NUM: case N_FNUM: case N_STR: case N_VAR: case N_ZERO: case N_LABELADDR:
		return false;
	case N_CALL: case N_ASSIGN: case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
	case N_ASM:
		return true;
	case N_BUILTIN:
		switch (x->aux) {
		case BI_VA_START: case BI_VA_COPY: case BI_VA_END: case BI_ALLOCA: case BI_TRAP:
		case BI_UNREACHABLE:
			return true;
		default:
			if (x->aux >= BI_ATOMIC_LOAD_N && x->aux <= BI_ATOMIC_SIGNAL_FENCE)
				return true;
			if (x->aux == BI_CONSTANT_P)
				return false;	/* its operand is not evaluated */
			for (uint32_t i = 0; i < x->c; i++)
				if (side_effects(c, c->lists[x->b + i]))
					return true;
			return false;
		}
	case N_MEMBER: case N_DEREF: case N_UNARY: case N_CAST: case N_ADDR: case N_COMPLIT:
	case N_STMTEXPR: case N_EXPR: case N_RETURN:
		return side_effects(c, x->a);
	case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR: case N_COMMA:
		return side_effects(c, x->a) || side_effects(c, x->b);
	case N_COND: case N_IF:
		return side_effects(c, x->a) || side_effects(c, x->b) || side_effects(c, x->c);
	case N_DECL:
		return side_effects(c, x->b);
	case N_BLOCK: case N_INIT:
		if (x->op == N_INIT && x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				if (side_effects(c, c->lists[x->b + 3 * i + 2]))
					return true;
			return false;
		}
		for (uint32_t i = 0; i < x->c; i++)
			if (side_effects(c, c->lists[x->b + i]))
				return true;
		return false;
	default:
		return true;	/* loops, gotos, labels, va_arg: conservatively yes */
	}
}

/* evaluate n without emitting anything: constant or address constant? */
/*
 * __builtin_constant_p: generate the operand as if it were reachable (so
 * inlined bodies run their returns and fold), then throw the code away.
 * References made meanwhile do not count: nothing gets queued for emission.
 */
static val dry(ecx *e, uint32_t n)
{
	uint32_t nin = e->nin, live = e->live, usedcs = e->usedcs;
	size_t raw = e->raw.n;
	int32_t foff = e->foff;
	bool reach = e->reach, calls = e->calls;
	int nclean = e->nclean, nloop = e->nloop;
	uint8_t nkv = e->nkv;
	uint16_t kvalid = e->kvalid;
	int64_t kval[NKV];
	val v;

	memcpy(kval, e->kval, sizeof(kval));
	e->dry++;
	v = gen(e, n);
	e->dry--;
	e->nkv = nkv, e->kvalid = kvalid;
	memcpy(e->kval, kval, sizeof(kval));
	e->nin = nin, e->live = live, e->usedcs = usedcs, e->foff = foff;	/* labels stay: user labels may map to them */
	e->reach = reach, e->nclean = nclean, e->nloop = nloop, e->raw.n = raw, e->calls = calls;
	if (v.k != V_CONST && v.k != V_ADDR)
		v = vnone();
	return v;
}

static val popcount(ecx *e, val v, int x)
{
	int r = toreg(e, &v), t = treg(e);
	int w = x ? 1 : 0;

	if (!x)
		op3(e, I_MOV, 0, r, r, 0);	/* zero-extend */
	/* v - ((v >> 1) & 0x55..) */
	shift_imm(e, I_LSRV, 1, t, r, 1);
	opi(e, I_ANDI, 1, t, t, 0x5555555555555555LL);
	op3(e, I_SUB, 1, r, r, t);
	/* (v & 0x33..) + ((v >> 2) & 0x33..) */
	shift_imm(e, I_LSRV, 1, t, r, 2);
	opi(e, I_ANDI, 1, t, t, 0x3333333333333333LL);
	opi(e, I_ANDI, 1, r, r, 0x3333333333333333LL);
	op3(e, I_ADD, 1, r, r, t);
	/* (v + (v >> 4)) & 0x0f.. */
	shift_imm(e, I_LSRV, 1, t, r, 4);
	op3(e, I_ADD, 1, r, r, t);
	opi(e, I_ANDI, 1, r, r, 0x0f0f0f0f0f0f0f0fLL);
	/* (v * 0x0101..) >> 56 */
	movi(e, 1, t, 0x0101010101010101LL);
	op4(e, I_MADD, 1, r, r, t, ZR);
	shift_imm(e, I_LSRV, 1, r, r, 56);
	tfree(e, t);
	(void)w;
	return v;
}

static val gen_overflow(ecx *e, const node *x, const uint32_t *arg)
{
	cc *c = e->c;
	qty ta = N(arg[0])->ty, tb = N(arg[1])->ty, tr = T(c, N(arg[2])->ty)->base;
	val a = gen(e, arg[0]), b = gen(e, arg[1]), p = gen(e, arg[2]);
	int bits = (int)tsize(c, tr) * 8;
	bool sr = is_signed(c, tr);
	int lo, hi, t, ok;
	lval l;

	if (tsize(c, ta) > 8 || tsize(c, tb) > 8 || bits > 64)
		efatal(e, x->loc, "128-bit overflow builtins are not supported");
	/* exact result in 128 bits */
	{
		int ra = toreg(e, &a), rb = toreg(e, &b);

		extend(e, ra, ra, (int)tsize(c, ta), is_signed(c, ta));
		extend(e, rb, rb, (int)tsize(c, tb), is_signed(c, tb));
		toreg2(e, &a, is_signed(c, ta));
		toreg2(e, &b, is_signed(c, tb));
	}
	lo = a.r, hi = a.r2;
	switch (x->aux) {
	case BI_ADD_OVERFLOW:
		op3(e, I_ADDS, 1, lo, lo, b.r);
		op3(e, I_ADC, 1, hi, hi, b.r2);
		break;
	case BI_SUB_OVERFLOW:
		op3(e, I_SUBS, 1, lo, lo, b.r);
		op3(e, I_SBC, 1, hi, hi, b.r2);
		break;
	default:
		op3(e, I_UMULH, 1, R_X16, lo, b.r);
		op4(e, I_MADD, 1, R_X16, lo, b.r2, R_X16);
		op4(e, I_MADD, 1, hi, hi, b.r, R_X16);
		op4(e, I_MADD, 1, lo, lo, b.r, ZR);
		break;
	}
	vfree(e, &b);
	/* fits iff ext(lo) == lo && hi == (signed ? lo >> 63 : 0) */
	t = treg(e);
	ok = treg(e);
	if (bits < 64) {
		opbf(e, sr ? I_SBFM : I_UBFM, 1, t, lo, 0, bits - 1);
		op3(e, I_SUBS, 1, ZR, t, lo);
		cset(e, ok, C_NE);
	} else {
		movi(e, 0, ok, 0);
	}
	if (sr || bits < 64)
		shift_imm(e, sr ? I_ASRV : I_LSRV, 1, t, lo, 63);
	if (sr)
		op3(e, I_SUBS, 1, ZR, hi, t);
	else
		op3(e, I_SUBS, 1, ZR, hi, ZR);
	/* ok |= (hi != expected) */
	cset(e, t, C_NE);
	op3(e, I_ORR, 0, ok, ok, t);
	tfree(e, t);
	l = lfromval(e, p, talign(c, tr));
	{
		val r = vreg(lo);

		store(e, &l, tr, &r);
	}
	lfree(e, &l);
	tfree(e, lo);
	tfree(e, hi);
	return vreg(ok);
}

/* __builtin_addc* / __builtin_subc*: x +- y +- carry_in, *carry_out = the carry (borrow) of either step */
static val gen_carry(ecx *e, const node *x, const uint32_t *arg)
{
	cc *c = e->c;
	qty ty = x->ty, tc = T(c, N(arg[3])->ty)->base;
	int sz = (int)tsize(c, ty), bits = sz * 8;
	bool add = x->aux == BI_ADDC;
	val a = gen(e, arg[0]), b = gen(e, arg[1]), ci = gen(e, arg[2]), p = gen(e, arg[3]);
	int ra = toreg(e, &a), rb = toreg(e, &b), rc = toreg(e, &ci), co = treg(e);
	lval l;

	extend(e, ra, ra, sz, false);
	extend(e, rb, rb, sz, false);
	extend(e, rc, rc, sz, false);
	if (bits == 64) {
		int t = treg(e);

		op3(e, add ? I_ADDS : I_SUBS, 1, ra, ra, rb);
		cset(e, co, add ? C_HS : C_LO);
		op3(e, add ? I_ADDS : I_SUBS, 1, ra, ra, rc);
		cset(e, t, add ? C_HS : C_LO);
		op3(e, I_ORR, 0, co, co, t);
		tfree(e, t);
	} else {
		op3(e, add ? I_ADD : I_SUB, 1, ra, ra, rb);
		op3(e, add ? I_ADD : I_SUB, 1, ra, ra, rc);
		/* add: anything at or above bit 'bits' is the carry; sub: a negative result is the borrow */
		shift_imm(e, I_LSRV, 1, co, ra, add ? bits : 63);
		op3(e, I_SUBS, 1, ZR, co, ZR);
		cset(e, co, C_NE);
		opbf(e, I_UBFM, 1, ra, ra, 0, bits - 1);
	}
	vfree(e, &b);
	vfree(e, &ci);
	l = lfromval(e, p, talign(c, tc));
	{
		val r = vreg(co);

		store(e, &l, tc, &r);
	}
	lfree(e, &l);
	tfree(e, co);
	return vreg(ra);
}

static void va_start_(ecx *e, uint32_t apn)
{
	lval l = gen_lval(e, apn);
	val a = laddr(e, &l);
	int ra = toreg(e, &a), t = treg(e);

	if (DARWIN(e->c)) {
		/* char *: the first anonymous argument, after the named stack ones */
		opi(e, I_ADDI, 1, t, R_FP, 16 + ((e->nstk_named + 7) & ~7));
		mem(e, I_ST, 3, 0, t, ra, 0);
		tfree(e, t);
		vfree(e, &a);
		return;
	}

	/* __stack */
	opi(e, I_ADDI, 1, t, R_FP, 16 + e->nstk_named);
	mem(e, I_ST, 3, 0, t, ra, 0);
	/* __gr_top: end of the register save area */
	opi(e, I_ADDI, 1, t, FRAME, e->vaslot + 64);
	mem(e, I_ST, 3, 0, t, ra, 8);
	/* __vr_top, __gr_offs, __vr_offs */
	opi(e, I_ADDI, 1, t, FRAME, e->vrslot + 128);
	mem(e, I_ST, 3, 0, t, ra, 16);
	movi(e, 0, t, -(8 - e->ngr_named) * 8);
	mem(e, I_ST, 2, 0, t, ra, 24);
	movi(e, 0, t, -(8 - e->nvr_named) * 16);
	mem(e, I_ST, 2, 0, t, ra, 28);
	tfree(e, t);
	vfree(e, &a);
}

static val va_arg_(ecx *e, const node *x)
{
	cc *c = e->c;
	qty t = x->ty;
	uint64_t size = tsize(c, t);
	bool big = is_recabi(c, t) && size > 16;
	int nreg = big ? 1 : (int)((size + 7) / 8);
	bool al16 = !big && talign(c, t) == 16;
	lval l = gen_lval(e, x->a);
	val a = laddr(e, &l);
	int ra = toreg(e, &a), off = treg(e), addr = treg(e);
	uint32_t lstack = newlabel(e), ldone = newlabel(e);

	if (DARWIN(c)) {
		mem(e, I_LD, 3, 0, addr, ra, 0);
		if (al16) {
			opi(e, I_ADDI, 1, addr, addr, 15);
			opi(e, I_ANDI, 1, addr, addr, -16);
		}
		opi(e, I_ADDI, 1, off, addr, big ? 8 : ((int64_t)size + 7) & ~7);
		mem(e, I_ST, 3, 0, off, ra, 0);
	} else {
	if (is_flt(c, t)) {
		/* __vr_offs / __vr_top, 16 bytes a register */
		if (size > 8)
			efatal(e, x->loc, "128-bit floating point va_arg is not supported");
		mem(e, I_LD, 2, 1, off, ra, 28);
		opi(e, I_SUBSI, 1, ZR, off, 0);
		bcond(e, C_GE, lstack);
		opi(e, I_ADDI, 1, addr, off, 16);
		mem(e, I_ST, 2, 0, addr, ra, 28);
		opi(e, I_SUBSI, 1, ZR, addr, 0);
		bcond(e, C_GT, lstack);
		mem(e, I_LD, 3, 0, addr, ra, 16);
		op3(e, I_ADD, 1, addr, addr, off);
		jump(e, ldone);
		nreg = 1;
		al16 = false;
		goto stack;
	}
	mem(e, I_LD, 2, 1, off, ra, 24);
	opi(e, I_SUBSI, 1, ZR, off, 0);
	bcond(e, C_GE, lstack);
	if (al16) {
		opi(e, I_ADDI, 1, off, off, 15);
		opi(e, I_ANDI, 1, off, off, -16);
	}
	opi(e, I_ADDI, 1, addr, off, nreg * 8);
	mem(e, I_ST, 2, 0, addr, ra, 24);
	opi(e, I_SUBSI, 1, ZR, addr, 0);
	bcond(e, C_GT, lstack);
	mem(e, I_LD, 3, 0, addr, ra, 8);
	op3(e, I_ADD, 1, addr, addr, off);
	jump(e, ldone);
stack:
	deflabel(e, lstack);
	mem(e, I_LD, 3, 0, addr, ra, 0);
	if (al16) {
		opi(e, I_ADDI, 1, addr, addr, 15);
		opi(e, I_ANDI, 1, addr, addr, -16);
	}
	opi(e, I_ADDI, 1, off, addr, nreg * 8);
	mem(e, I_ST, 3, 0, off, ra, 0);
	deflabel(e, ldone);
	}
	tfree(e, off);
	vfree(e, &a);
	if (big)
		mem(e, I_LD, 3, 0, addr, addr, 0);
	{
		lval m = { .k = L_REG, .r = (uint8_t)addr, .align = 8 };
		val v = load(e, &m, t);

		if (!(v.k == V_REG && v.r == addr))
			tfree(e, addr);
		return v;
	}
}

/*
 * __atomic_*: LSE instructions, always acquire+release (ldar/stlr, swpal,
 * casal, ld<op>al), which is at least what any memory order asks for.
 */
static val gen_atomic(ecx *e, const node *x, const uint32_t *arg)
{
	cc *c = e->c;
	int id = x->aux;
	qty t = QUNQ(T(c, N(arg[0])->ty)->base);
	uint64_t n = tsize(c, t);
	unsigned sz = (unsigned)log2sz(n), xs = n == 8;
	bool sg = is_integer(c, t) && is_signed(c, t);
	val p = gen(e, arg[0]), v1 = vnone(), v2 = vnone(), res = vnone();
	int rp, nmo = id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG ? 2 : 1, nargs = (int)x->c;

	if (n == 16)
		efatal(e, x->loc, "16-byte atomics are not supported");
	rp = toreg(e, &p);
	/* operands, then the memory orders (evaluated for their side effects) */
	if (nargs - nmo > 1)
		v1 = gen(e, arg[1]);
	if (nargs - nmo > 2)
		v2 = gen(e, arg[2]);
	for (int k = nargs - nmo; k < nargs; k++) {
		val m = gen(e, arg[k]);

		vfree(e, &m);
	}
	if (nargs - nmo > 3) {	/* weak: ignored */
		val w = gen(e, arg[3]);

		vfree(e, &w);
	}
	switch (id) {
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_LOAD: {
		int r = treg(e);

		word(e, sz << 30 | 0x08dffc00u | (unsigned)rp << 5 | (unsigned)r);	/* ldar */
		if (id == BI_ATOMIC_LOAD) {
			int rr = toreg(e, &v1);

			mem(e, I_ST, (int)sz, 0, r, rr, 0);
			tfree(e, r);
			break;
		}
		if (sg && n < 8)
			extend(e, r, r, (int)n, true);
		res = vreg(r);
		break;
	}
	case BI_ATOMIC_STORE_N: case BI_ATOMIC_STORE: {
		int r;

		if (id == BI_ATOMIC_STORE) {
			int rv = toreg(e, &v1);

			r = treg(e);
			mem(e, I_LD, (int)sz, 0, r, rv, 0);
			tfree(e, r);
		}
		r = id == BI_ATOMIC_STORE ? r : toreg(e, &v1);
		word(e, sz << 30 | 0x089ffc00u | (unsigned)rp << 5 | (unsigned)r);	/* stlr */
		break;
	}
	case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_EXCHANGE: {
		int rs, r = treg(e);

		if (id == BI_ATOMIC_EXCHANGE) {
			int rv = toreg(e, &v1);

			rs = treg(e);
			mem(e, I_LD, (int)sz, 0, rs, rv, 0);
		} else {
			rs = toreg(e, &v1);
		}
		word(e, sz << 30 | 0x38e08000u | (unsigned)rs << 16 | (unsigned)rp << 5 | (unsigned)r);	/* swpal */
		if (id == BI_ATOMIC_EXCHANGE) {
			int rr = toreg(e, &v2);

			mem(e, I_ST, (int)sz, 0, r, rr, 0);
			tfree(e, rs);
			tfree(e, r);
			break;
		}
		if (sg && n < 8)
			extend(e, r, r, (int)n, true);
		res = vreg(r);
		break;
	}
	case BI_ATOMIC_CMPXCHG_N: case BI_ATOMIC_CMPXCHG: {
		/* old = *expected; casal old, desired, [p]; ok = old == *expected; if !ok *expected = old */
		int re = toreg(e, &v1), rd, old = treg(e), cmpv = treg(e), ok;
		uint32_t l = newlabel(e);

		if (id == BI_ATOMIC_CMPXCHG) {
			int rv = toreg(e, &v2);

			rd = treg(e);
			mem(e, I_LD, (int)sz, 0, rd, rv, 0);
		} else {
			rd = toreg(e, &v2);
		}
		mem(e, I_LD, (int)sz, 0, old, re, 0);
		mov(e, 1, cmpv, old);
		word(e, sz << 30 | 0x08e0fc00u | (unsigned)old << 16 | (unsigned)rp << 5 | (unsigned)rd);	/* casal */
		if (n < 4)
			extend(e, cmpv, cmpv, (int)n, false), extend(e, old, old, (int)n, false);
		op3(e, I_SUBS, xs, ZR, old, cmpv);
		ok = treg(e);
		cset(e, ok, C_EQ);
		cbz(e, true, 0, ok, l);
		mem(e, I_ST, (int)sz, 0, old, re, 0);
		deflabel(e, l);
		tfree(e, old);
		tfree(e, cmpv);
		if (id == BI_ATOMIC_CMPXCHG)
			tfree(e, rd);
		res = vreg(ok);
		break;
	}
	default: {
		/* ld<op>al: add 000, clr 001, eor 010, set 011 */
		int base = id >= BI_ATOMIC_ADD_FETCH ? id - BI_ATOMIC_ADD_FETCH : id - BI_ATOMIC_FETCH_ADD;	/* add sub and or xor */
		static const unsigned opc[] = { 0, 0, 1, 3, 2 };
		int rv = toreg(e, &v1), rs = treg(e), r = treg(e);

		if (base == 1)
			op3(e, I_SUB, 1, rs, ZR, rv);		/* neg */
		else if (base == 2)
			op3(e, I_ORN, 1, rs, ZR, rv);		/* mvn: clear the bits not in v */
		else
			mov(e, 1, rs, rv);
		word(e, sz << 30 | 0x38e00000u | (unsigned)rs << 16 | opc[base] << 12 | (unsigned)rp << 5 | (unsigned)r);
		tfree(e, rs);
		if (id >= BI_ATOMIC_ADD_FETCH) {
			static const int bop[] = { I_ADD, I_SUB, I_AND, I_ORR, I_EOR };

			op3(e, bop[base], 1, r, r, rv);
		}
		if (n < 8)
			extend(e, r, r, (int)n, sg);
		res = vreg(r);
		break;
	}
	}
	vfree(e, &v1);
	vfree(e, &v2);
	vfree(e, &p);
	return res;
}

static val gen_builtin(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	val v;

	switch (x->aux) {
	case BI_CONSTANT_P: {
		/*
		 * clang: 1 if the front end folds the operand (a pointer only as a
		 * string literal at offset 0); else 0 if it has side effects or is
		 * not arithmetic; else whatever the optimizer folds, which here is
		 * the operand after inlining (dry).
		 */
		cval cv;
		qty at = N(arg[0])->ty;

		if (cc_eval(c, arg[0], &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT ||
						 (cv.kind == CV_ADDR && cv.str && cv.i == 0 && !cv.sym && !cv.label)))
			return vconst(1);
		if (side_effects(c, arg[0]) || !(is_integer(c, at) || is_flt(c, at)))
			return vconst(0);
		v = dry(e, arg[0]);
		return vconst(v.k == V_CONST);
	}
	case BI_EXPECT:
	case BI_EXPECT_PROB:
		/* the hint is still evaluated (argument order is unspecified: first, so that
		 * nothing is live across it) */
		for (uint32_t i = 1; i < x->c; i++)
			if (side_effects(c, arg[i])) {
				v = gen(e, arg[i]);
				vfree(e, &v);
			}
		v = gen(e, arg[0]);
		return v;
	case BI_ASSUME_ALIGNED:
	case BI_EXTRACT_RETURN_ADDR:
		return gen(e, arg[0]);
	case BI_ASSUME:
	case BI_VA_END:
		return vnone();
	case BI_VA_START:
		va_start_(e, arg[0]);
		return vnone();
	case BI_VA_COPY: {
		lval d = gen_lval(e, arg[0]), s = gen_lval(e, arg[1]);
		val dv = laddr(e, &d), sv = laddr(e, &s);
		int rd = toreg(e, &dv), rs = toreg(e, &sv);

		copy_mem(e, rd, rs, tsize(c, c->t_valist));
		vfree(e, &dv);
		vfree(e, &sv);
		return vnone();
	}
	case BI_UNREACHABLE:
		e->reach = false;
		return vnone();
	case BI_TRAP:
		if (e->reach)
			put(e, I_BRK)->imm = 0x3e8;
		e->reach = false;
		return vnone();
	case BI_DEBUGTRAP:
		if (e->reach)
			put(e, I_BRK)->imm = 0xf000;
		return vnone();
	case BI_RETURN_ADDRESS: {
		int r = treg(e);

		mem(e, I_LD, 3, 0, r, R_FP, 8);
		return vreg(r);
	}
	case BI_FRAME_ADDRESS: {
		int r = treg(e);

		mov(e, 1, r, R_FP);
		return vreg(r);
	}
	case BI_OBJECT_SIZE:
	case BI_DYNAMIC_OBJECT_SIZE: {
		val t = gen(e, arg[1]);

		return vconst(t.k == V_CONST && (t.i & 2) ? 0 : (i128)(uint64_t)-1);
	}
	case BI_IS_CONSTANT_EVALUATED:
		return vconst(0);
	case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
		return gen_overflow(e, x, arg);
	case BI_ADDC: case BI_SUBC:
		return gen_carry(e, x, arg);
	case BI_PREFETCH: {
		val p = gen(e, arg[0]);
		int r = toreg(e, &p);

		if (e->reach)
			put(e, I_PRFM)->rn = (uint8_t)r;
		vfree(e, &p);
		return vnone();
	}
	case BI_ALLOCA:
	case BI_ALLOCA_UNINIT: {
		val s = gen(e, arg[0]);
		int r = toreg(e, &s);
		ins *i;

		e->dyn = true;
		opi(e, I_ADDI, 1, r, r, 15);
		opi(e, I_ANDI, 1, r, r, -16);
		op3(e, I_SUB, 1, SP, SP, r);
		if (e->reach) {
			i = put(e, I_ADDI);
			i->sz = 1, i->rd = (uint8_t)r, i->rn = SP, i->imm = 0, i->cond = 0xee;	/* + outgoing area */
		}
		return s;
	}
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_STORE_N: case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_CMPXCHG_N:
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE: case BI_ATOMIC_EXCHANGE: case BI_ATOMIC_CMPXCHG:
	case BI_ATOMIC_FETCH_ADD: case BI_ATOMIC_FETCH_SUB: case BI_ATOMIC_FETCH_AND: case BI_ATOMIC_FETCH_OR:
	case BI_ATOMIC_FETCH_XOR: case BI_ATOMIC_ADD_FETCH: case BI_ATOMIC_SUB_FETCH: case BI_ATOMIC_AND_FETCH:
	case BI_ATOMIC_OR_FETCH: case BI_ATOMIC_XOR_FETCH:
		return gen_atomic(e, x, arg);
	case BI_ATOMIC_THREAD_FENCE: case BI_ATOMIC_SIGNAL_FENCE: {
		val m = gen(e, arg[0]);

		vfree(e, &m);
		if (e->reach && x->aux == BI_ATOMIC_THREAD_FENCE)
			put(e, I_WORD)->imm = 0xd5033bbf;	/* dmb ish */
		return vnone();
	}
	case BI_ATOMIC_LOCK_FREE: {
		val a = gen(e, arg[0]), b = gen(e, arg[1]);

		vfree(e, &a);
		vfree(e, &b);
		return vconst(1);
	}
	case BI_ARM_RSR: {
		uint32_t sn = strip_casts(c, arg[0]);
		int r = treg(e);

		if (N(sn)->op != N_STR)
			efatal(e, x->loc, "__builtin_arm_rsr64 needs a string literal");
		if (e->reach) {
			ins *i = put(e, I_MRS);

			i->rd = (uint8_t)r;
			i->ref = N(sn)->a;
		}
		return vreg(r);
	}
	case BI_CLZ: case BI_CLZL: case BI_CLZLL: case BI_CTZ: case BI_CTZL: case BI_CTZLL:
	case BI_FFS: case BI_FFSL: case BI_FFSLL: case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL: case BI_BSWAP16: case BI_BSWAP32: case BI_BSWAP64:
	case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: case BI_BITREVERSE8: case BI_BITREVERSE16:
	case BI_BITREVERSE32: case BI_BITREVERSE64: {
		qty at = N(arg[0])->ty;
		int ax = tsize(c, at) == 8, r, t;

		v = gen(e, arg[0]);
		if (v.k == V_CONST) {
			u128 u = (u128)trunc_to(c, v.i, at) & (ax ? ~(u128)0 >> 64 : 0xffffffffu);
			unsigned bits = ax ? 64 : 32;
			i128 res = 0;

			if (tsize(c, at) == 2)
				u &= 0xffff;
			switch (x->aux) {
			case BI_CLZ: case BI_CLZL: case BI_CLZLL:
				if (!u)
					goto runtime;
				res = __builtin_clzll((uint64_t)u) - (64 - bits);
				break;
			case BI_CTZ: case BI_CTZL: case BI_CTZLL:
				if (!u)
					goto runtime;
				res = __builtin_ctzll((uint64_t)u);
				break;
			case BI_FFS: case BI_FFSL: case BI_FFSLL:
				res = u ? __builtin_ctzll((uint64_t)u) + 1 : 0;
				break;
			case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
				res = __builtin_popcountll((uint64_t)u);
				break;
			case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
				res = __builtin_popcountll((uint64_t)u) & 1;
				break;
			case BI_BSWAP16: res = __builtin_bswap16((uint16_t)u); break;
			case BI_BSWAP32: res = __builtin_bswap32((uint32_t)u); break;
			case BI_BSWAP64: res = (i128)__builtin_bswap64((uint64_t)u); break;
			default:
				goto runtime;
			}
			return vconst(trunc_to(c, res, x->ty));
		}
runtime:
		r = toreg(e, &v);
		switch (x->aux) {
		case BI_CLZ: case BI_CLZL: case BI_CLZLL:
			op3(e, I_CLZ, ax, r, r, 0);
			break;
		case BI_CTZ: case BI_CTZL: case BI_CTZLL:
			op3(e, I_RBIT, ax, r, r, 0);
			op3(e, I_CLZ, ax, r, r, 0);
			break;
		case BI_FFS: case BI_FFSL: case BI_FFSLL:
			t = treg(e);
			op3(e, I_RBIT, ax, t, r, 0);
			op3(e, I_CLZ, ax, t, t, 0);
			op3(e, I_SUBS, ax, ZR, r, ZR);
			op3s(e, I_CSINC, 0, r, ZR, t, C_EQ, 0);
			tfree(e, t);
			break;
		case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
			return popcount(e, v, ax);
		case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
			v = popcount(e, v, ax);
			opi(e, I_ANDI, 0, v.r, v.r, 1);
			return v;
		case BI_BSWAP16:
			op3(e, I_REV16, 0, r, r, 0);
			break;
		case BI_BSWAP32:
			op3(e, I_REV, 0, r, r, 0);
			break;
		case BI_BSWAP64:
			op3(e, I_REV, 1, r, r, 0);
			break;
		case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL:
			op3(e, I_CLS, ax, r, r, 0);
			break;
		case BI_BITREVERSE8:
			op3(e, I_RBIT, 0, r, r, 0);
			shift_imm(e, I_LSRV, 0, r, r, 24);
			break;
		case BI_BITREVERSE16:
			op3(e, I_RBIT, 0, r, r, 0);
			shift_imm(e, I_LSRV, 0, r, r, 16);
			break;
		case BI_BITREVERSE32:
			op3(e, I_RBIT, 0, r, r, 0);
			break;
		case BI_BITREVERSE64:
			op3(e, I_RBIT, 1, r, r, 0);
			break;
		}
		return v;
	}
	default: {
		cval cv;

		if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
			return vconst(cv.i);
		if (cc_eval(c, n, &cv) && cv.kind == CV_FLT && is_flt(c, x->ty))
			return f_const(fpk(e, x->ty), cv.f);
		efatal(e, x->loc, "unsupported builtin (%d)", x->aux);
	}
	}
}

/* ---------------- expressions ---------------- */

static val const_global(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	cval cv;

	if (y->kind == S_VAR && (QQ(y->ty) & (Q_CONST | Q_VOLATILE)) == Q_CONST && is_integer(c, y->ty) && y->body &&
	    N(y->body)->op != N_INIT && cc_eval(c, y->body, &cv) && cv.kind == CV_INT)
		return vconst(trunc_to(c, cv.i, y->ty));
	return vnone();
}

static val gen_var(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t s = x->a;
	const sym *y = &c->syms[s];
	lval l;
	val v;

	if (y->kind == S_FUNC) {
		uint32_t r = REF(RK_SYM, canon_sym(c, s));

		note_ref(e, r);
		return vaddr(r, 0);
	}
	if (y->kind == S_BUILTIN)
		return vaddr(REF(RK_NAME, (uint32_t)y->val), 0);
	if (is_local_var(c, s)) {
		bind *b = binding(e, s);

		if (b->kind == B_CONST && b->inst)
			return b->v;
		if (b->kv && (e->kvalid >> (b->kv - 1) & 1))
			return vconst(e->kval[b->kv - 1]);
	} else {
		v = const_global(e, canon_sym(c, s));
		if (v.k == V_CONST)
			return v;
	}
	l = gen_lval(e, n);
	v = load(e, &l, x->ty);
	if (!(is_agg(c, x->ty) && l.k == L_REG))
		lfree(e, &l);
	return v;
}

/* result of a load through l, freeing the lvalue's base unless it became the value */
static val load_free(ecx *e, lval *l, qty t)
{
	val v = load(e, l, t);

	if (l->k == L_REG && !(v.k == V_REG && v.r == l->r))
		tfree(e, l->r);
	return v;
}

/* move v into the fixed register r (for joins) */
static void move_to(ecx *e, int r, int r2, val *v, qty t)
{
	cc *c = e->c;

	if (v->k == V_NONE)
		return;
	if (r2 >= 0 && is_wide(c, t)) {
		toreg2(e, v, is_signed(c, t));
		mov(e, 1, r, v->r);
		mov(e, 1, r2, v->r2);
		vfree(e, v);
		return;
	}
	switch (v->k) {
	case V_CONST:
		movi(e, 1, r, (int64_t)v->i);
		break;
	case V_ADDR:
		addr_of_ref(e, r, v->ref, (int64_t)v->i);
		break;
	default:
		toreg(e, v);
		mov(e, 1, r, v->r);
		vfree(e, v);
	}
}

static val gen_cond_expr(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = x->ty;
	bool vd = is_void(c, t), wide = is_wide(c, t);
	uint32_t lelse = newlabel(e), lend = newlabel(e);
	int r = -1, r2 = -1;
	val v;

	if (!x->b) {		/* a ?: c */
		v = gen(e, x->a);
		if (v.k == V_CONST || v.k == V_ADDR) {
			if (v.k == V_ADDR || v.i)
				return v;
			return gen(e, x->c);
		}
		if (wide) {
			toreg2(e, &v, is_signed(c, t));
			r = v.r, r2 = v.r2;
			op3(e, I_ORR, 1, R_X16, r, r2);
			cbz(e, true, 1, R_X16, lend);
		} else {
			r = toreg(e, &v);
			cbz(e, true, X(c, t), r, lend);
		}
		v = gen(e, x->c);
		move_to(e, r, r2, &v, t);
		deflabel(e, lend);
		return wide ? (val){ .k = V_REG2, .r = (uint8_t)r, .r2 = (uint8_t)r2 } : vreg(r);
	}
	{
		int k = 0;
		val ca = gen(e, x->a);

		if (ca.k == V_CONST || ca.k == V_ADDR)
			return gen(e, (ca.k == V_ADDR || ca.i) ? x->b : x->c);
		k = test_zero(e, &ca, N(x->a)->ty, lelse, false);
		(void)k;
	}
	if (!vd) {
		r = treg(e);
		if (wide)
			r2 = treg(e);
	}
	v = gen(e, x->b);
	if (!vd)
		move_to(e, r, r2, &v, t);
	else
		vfree(e, &v);
	jump(e, lend);
	deflabel(e, lelse);
	v = gen(e, x->c);
	if (!vd)
		move_to(e, r, r2, &v, t);
	else
		vfree(e, &v);
	deflabel(e, lend);
	if (vd)
		return vnone();
	return wide ? (val){ .k = V_REG2, .r = (uint8_t)r, .r2 = (uint8_t)r2 } : vreg(r);
}

static val gen_logical(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	bool isand = x->op == N_LOGAND;
	val a = gen(e, x->a), b;
	uint32_t lshort, lend;
	int r;

	if (a.k == V_CONST || a.k == V_ADDR) {
		bool t = a.k == V_ADDR || a.i;

		if (isand && !t)
			return vconst(0);
		if (!isand && t)
			return vconst(1);
		b = gen(e, x->b);
		return tobool(e, b, N(x->b)->ty);
	}
	lshort = newlabel(e);
	lend = newlabel(e);
	r = treg(e);
	test_zero(e, &a, N(x->a)->ty, lshort, !isand);
	b = tobool(e, gen(e, x->b), N(x->b)->ty);
	if ((b.k == V_CONST && (isand ? !b.i : b.i)) || (b.k == V_ADDR && !isand)) {
		/* x && 0, x || 1: x's effects are done, the value is known (clang folds it,
		   and code it guards, such as calls to functions that do not exist, goes) */
		vfree(e, &b);
		deflabel(e, lshort);
		tfree(e, r);
		return vconst(isand ? 0 : 1);
	}
	move_to(e, r, -1, &b, prim(TY_INT));
	jump(e, lend);
	deflabel(e, lshort);
	movi(e, 0, r, isand ? 0 : 1);
	deflabel(e, lend);
	return vreg(r);
}

/* a lane-wise vector operation on the vectors at ra and rb into a new frame temporary; returns its address */
static val gen_vbin(ecx *e, int op, int ra, int rb, qty t, uint32_t loc)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	qty et = T(c, t)->base;
	unsigned es = (unsigned)tsize(c, et);
	bool q = size == 16;
	const char *mn, *arr;
	char ab[8];
	tbuf *rb_ = &e->raw;
	size_t st;
	int32_t sl;
	int rd;
	ins *i;

	if (is_flt(c, et))
		efatal(e, loc, "floating point vectors are not supported");
	if (size != 8 && size != 16)
		efatal(e, loc, "unsupported vector size");
	switch (op) {
	case T_AMP: mn = "and"; break;
	case T_PIPE: mn = "orr"; break;
	case T_CARET: mn = "eor"; break;
	case T_PLUS: mn = "add"; break;
	case T_MINUS: mn = "sub"; break;
	default: efatal(e, loc, "unsupported vector operator");
	}
	if (op == T_AMP || op == T_PIPE || op == T_CARET) {
		arr = q ? "16b" : "8b";
	} else {
		snprintf(ab, sizeof ab, "%u%c", (unsigned)(size / es), es == 1 ? 'b' : es == 2 ? 'h' : es == 4 ? 's' : 'd');
		arr = ab;
	}
	sl = slot(e, size, 16);
	rd = treg(e);
	opi(e, I_ADDI, 1, rd, FRAME, sl);
	st = rb_->n;
	tb_s(rb_, q ? "\tldr\tq16, [" : "\tldr\td16, [");
	reg(rb_, ra, 1);
	tb_s(rb_, q ? "]\n\tldr\tq17, [" : "]\n\tldr\td17, [");
	reg(rb_, rb, 1);
	tb_s(rb_, "]\n\t");
	tb_s(rb_, mn);
	tb_s(rb_, "\tv16.");
	tb_s(rb_, arr);
	tb_s(rb_, ", v16.");
	tb_s(rb_, arr);
	tb_s(rb_, ", v17.");
	tb_s(rb_, arr);
	tb_s(rb_, q ? "\n\tstr\tq16, [" : "\n\tstr\td16, [");
	reg(rb_, rd, 1);
	tb_c(rb_, ']');
	i = put(e, I_RAW);
	i->off = (int32_t)st;
	i->ref = (uint32_t)(rb_->n - st);
	return vreg(rd);
}

/* operand k of a lane-wise operation: lane i of a vector at ra, or the scalar in rs (copied) */
static val vlane(ecx *e, qty t, int r, unsigned i)
{
	cc *c = e->c;

	if (is_vec(c, t)) {
		qty et = T(c, t)->base;
		lval l = { .k = L_REG, .r = (uint8_t)r, .off = (int64_t)i * (int64_t)tsize(c, et), .align = talign(c, et) };

		return load(e, &l, et);
	}
	{
		int d = treg(e);

		mov(e, 1, d, r);
		return vreg(d);
	}
}

/*
 * Lane by lane through memory, for what the NEON path (gen_vbin) does not do:
 * other sizes, *, /, %, shifts, comparisons (-1 / 0 lanes), floating-point
 * lanes, a scalar operand (each lane against it), unary - and ~ (b NULL).
 */
static val gen_vlanes(ecx *e, int op, qty at, val *a, qty bt, val *b, qty rt, bool unary)
{
	cc *c = e->c;
	qty vt = is_vec(c, at) ? at : bt, et = T(c, vt)->base, rte = T(c, rt)->base;
	unsigned n = (unsigned)(tsize(c, vt) / tsize(c, et));
	int ra = toreg(e, a), rb = unary ? -1 : toreg(e, b), rd;
	int32_t sl = slot(e, tsize(c, rt), 16);

	rd = treg(e);
	opi(e, I_ADDI, 1, rd, FRAME, sl);
	for (unsigned i = 0; i < n; i++) {
		val x = vlane(e, at, ra, i), y = unary ? vconst(0) : vlane(e, bt, rb, i), r;
		qty yt = unary ? et : is_vec(c, bt) ? T(c, bt)->base : bt;

		if (unary) {
			if (op == T_MINUS)
				r = is_flt(c, et) ? f_neg(e, x, et) : arith(e, T_MINUS, et, vconst(0), x);
			else
				r = arith(e, T_CARET, et, x, vconst(-1));
		} else if (is_cmp(op)) {
			if (is_flt(c, et)) {
				r = f_compare(e, op, et, x, y);
			} else {
				int cond = is_wide(c, et) ? emit_cmp2(e, op, is_signed(c, et), &x, &y)
							  : emit_cmp(e, op, X(c, et), is_signed(c, et), &x, &y);
				int t;

				vfree(e, &y);
				t = toreg(e, &x);
				if (x.k == V_REG2)
					tfree(e, x.r2);
				cset(e, t, cond);
				r = vreg(t);
			}
			if (is_wide(c, rte)) {
				/* 0 / 1 -> 0 / all ones */
				int h = treg(e);

				op3(e, I_SUB, 1, r.r, ZR, r.r);
				mov(e, 1, h, r.r);
				r.k = V_REG2;
				r.r2 = (uint8_t)h;
			} else {
				r = arith(e, T_MINUS, rte, vconst(0), r);
			}
		} else if (is_flt(c, et)) {
			r = f_arith(e, op, et, x, y);
		} else {
			(void)yt;
			r = is_wide(c, et) ? arith2(e, op, et, x, y) : arith(e, op, et, x, y);
		}
		{
			lval l = { .k = L_REG, .r = (uint8_t)rd, .off = (int64_t)i * (int64_t)tsize(c, rte), .align = talign(c, rte) };

			store(e, &l, rte, &r);
			vfree(e, &r);
		}
	}
	tfree(e, ra);
	if (rb >= 0)
		tfree(e, rb);
	return vreg(rd);
}

static bool vbin_fast(cc *c, int op, qty lt, qty rt, qty ty)
{
	uint64_t size = tsize(c, ty);

	return is_vec(c, lt) && is_vec(c, rt) && (size == 8 || size == 16) && !is_flt(c, T(c, ty)->base) &&
	       (op == T_AMP || op == T_PIPE || op == T_CARET || op == T_PLUS || op == T_MINUS);
}

static val gen_binary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	int op = x->aux;
	qty lt = N(x->a)->ty, rt = N(x->b)->ty, ty = x->ty;
	val a = gen(e, x->a), b = gen(e, x->b);

	if (is_vec(c, ty) && !vbin_fast(c, op, lt, rt, ty))
		return gen_vlanes(e, op, lt, &a, rt, &b, ty, false);
	if (is_vec(c, ty)) {
		int ra = toreg(e, &a), rb = toreg(e, &b);
		val r = gen_vbin(e, op, ra, rb, ty, x->loc);

		vfree(e, &a);
		vfree(e, &b);
		return r;
	}

	if (is_flt(c, lt) || is_flt(c, ty))
		return is_cmp(op) ? f_compare(e, op, lt, a, b) : f_arith(e, op, ty, a, b);
	/* pointer arithmetic */
	if (is_ptr(c, ty) && (op == T_PLUS || op == T_MINUS) && is_ptr(c, lt))
		return ptr_to_vla(c, lt) ? ptr_add_vla(e, a, b, lt, op == T_MINUS)
					 : ptr_add(e, a, b, (int64_t)elem_size(c, lt), op == T_MINUS);
	if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt)) {
		int64_t es = (int64_t)elem_size(c, lt);

		if ((a.k == V_ADDR && b.k == V_ADDR && a.ref == b.ref) || (a.k == V_CONST && b.k == V_CONST))
			return vconst((a.i - b.i) / (es ? es : 1));
		{
			int ra = toreg(e, &a), rb = toreg(e, &b);

			op3(e, I_SUB, 1, ra, ra, rb);
			tfree(e, rb);
			if (es > 1 && !(es & (es - 1))) {
				shift_imm(e, I_ASRV, 1, ra, ra, __builtin_ctzll((uint64_t)es));
			} else if (es > 1) {
				movi(e, 1, R_X16, es);
				op3(e, I_SDIV, 1, ra, ra, R_X16);
			}
			return a;
		}
	}
	if (is_cmp(op)) {
		bool sg = !is_ptr(c, lt) && is_signed(c, lt);
		int cond, r;

		if (a.k == V_CONST && b.k == V_CONST) {
			bool ok;
			val v = fold_int_bin(e, op, is_ptr(c, lt) ? prim(TY_ULONG) : lt, ty, a.i, b.i, &ok);

			if (ok)
				return v;
		}
		if ((a.k == V_ADDR && b.k == V_ADDR && a.ref == b.ref) ||
		    (a.k == V_ADDR && b.k == V_CONST && b.i == 0) || (b.k == V_ADDR && a.k == V_CONST && a.i == 0)) {
			if (a.k == V_ADDR && b.k == V_ADDR) {
				bool ok;

				return fold_int_bin(e, op, prim(TY_LONG), ty, a.i, b.i, &ok);
			}
			if (op == T_EQUALEQUAL)
				return vconst(0);
			if (op == T_EXCLAIMEQ)
				return vconst(1);
		}
		if (is_wide(c, lt))
			cond = emit_cmp2(e, op, sg, &a, &b);
		else
			cond = emit_cmp(e, op, X(c, lt), sg, &a, &b);
		vfree(e, &b);
		r = toreg(e, &a);
		if (a.k == V_REG2)
			tfree(e, a.r2);
		cset(e, r, cond);
		return vreg(r);
	}
	if (a.k == V_CONST && b.k == V_CONST) {
		bool ok;
		val v = fold_int_bin(e, op, ty, ty, a.i, b.i, &ok);

		if (op == T_LESSLESS || op == T_GREATERGREATER)
			v = fold_int_bin(e, op, lt, ty, a.i, b.i, &ok);
		if (ok)
			return v;
	}
	if (a.k == V_ADDR && b.k == V_CONST && (op == T_PLUS || op == T_MINUS) && tsize(c, ty) == 8) {
		a.i += op == T_PLUS ? b.i : -b.i;
		return a;
	}
	if (is_wide(c, ty))
		return arith2(e, op, ty, a, b);
	return arith(e, op, ty, a, b);
}

static val gen_unary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = N(x->a)->ty;
	val v = gen(e, x->a);

	if (is_vec(c, x->ty) && (x->aux == T_MINUS || x->aux == T_TILDE))
		return gen_vlanes(e, x->aux, t, &v, t, NULL, x->ty, true);
	switch (x->aux) {
	case T_PLUS:
		return v;
	case T_MINUS:
		if (is_flt(c, x->ty))
			return f_neg(e, v, x->ty);
		if (v.k == V_CONST)
			return vconst(trunc_to(c, -v.i, x->ty));
		if (is_wide(c, x->ty)) {
			toreg2(e, &v, is_signed(c, x->ty));
			op3(e, I_SUBS, 1, v.r, ZR, v.r);
			op3(e, I_SBC, 1, v.r2, ZR, v.r2);
			return v;
		}
		toreg(e, &v);
		op3(e, I_SUB, X(c, x->ty), v.r, ZR, v.r);
		return v;
	case T_TILDE:
		if (v.k == V_CONST)
			return vconst(trunc_to(c, ~v.i, x->ty));
		if (is_wide(c, x->ty)) {
			toreg2(e, &v, is_signed(c, x->ty));
			op3(e, I_ORN, 1, v.r, ZR, v.r);
			op3(e, I_ORN, 1, v.r2, ZR, v.r2);
			return v;
		}
		toreg(e, &v);
		op3(e, I_ORN, X(c, x->ty), v.r, ZR, v.r);
		return v;
	case T_EXCLAIM:
		v = tobool(e, v, t);
		if (v.k == V_CONST)
			return vconst(!v.i);
		opi(e, I_EORI, 0, v.r, v.r, 1);
		return v;
	}
	efatal(e, x->loc, "unsupported unary operator");
}

/* convert v from type `from` to `to` the way an assignment back to an integer lvalue does */
static val conv_back(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;

	if (KIND(c, to) == TY_BOOL)
		return tobool(e, v, from);
	if (is_ptr(c, to) || is_ptr(c, from))
		return v;
	return cast(e, v, from, to, CK_INT);
}

static val gen_assign(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty lt = x->ty;
	lval l;
	val v, old;

	if (x->aux && is_vec(c, lt)) {
		/* v op= w */
		val w = gen(e, x->b), av, r;
		int ra, rw;

		l = gen_lval(e, x->a);
		{
			lval l2 = l;

			av = laddr(e, &l2);
		}
		if (!vbin_fast(c, x->aux, lt, N(x->b)->ty, lt)) {
			qty bt = N(x->b)->ty;

			r = gen_vlanes(e, x->aux, lt, &av, bt, &w, lt, false);
			store(e, &l, lt, &r);
			return r;
		}
		ra = toreg(e, &av), rw = toreg(e, &w);
		r = gen_vbin(e, x->aux, ra, rw, lt, x->loc);
		vfree(e, &w);
		vfree(e, &av);
		store(e, &l, lt, &r);
		return r;
	}
	if (!x->aux) {
		v = gen(e, x->b);
		if (N(x->a)->op == N_VAR && (e->E->sflags[N(x->a)->a] & SA_CWRITE)) {
			bind *b = binding(e, N(x->a)->a);

			if (b->kind == B_CONST && b->inst) {
				/* the same constant again (note_assign) */
				vfree(e, &v);
				return b->v;
			}
			if (b->kv) {
				int k = b->kv - 1;
				val cv = v;	/* store() may move v into a register */

				l = gen_lval(e, x->a);
				store(e, &l, lt, &v);
				lfree(e, &l);
				e->kvalid &= (uint16_t)~(1u << k);
				if (cv.k == V_CONST) {
					e->kval[k] = (int64_t)trunc_to(c, cv.i, lt);
					e->kvalid |= (uint16_t)(1u << k);
				}
				return v;
			}
		}
		l = gen_lval(e, x->a);
		store(e, &l, lt, &v);
		lfree(e, &l);
		if (l.bf && v.k == V_REG) {
			/* the value of a bit-field assignment is the truncated value */
			opbf(e, l.sgn ? I_SBFM : I_UBFM, 1, v.r, v.r, 0, l.width - 1);
		}
		return v;
	}
	{
		qty pt = x->c;
		int op = x->aux;

		v = gen(e, x->b);
		l = gen_lval(e, x->a);
		old = load(e, &l, lt);
		if (is_ptr(c, lt)) {
			old = ptr_add(e, old, v, (int64_t)elem_size(c, lt), op == T_MINUS);
		} else if (is_flt(c, pt) || is_flt(c, lt)) {
			old = cast(e, old, lt, pt, CK_INT);
			old = is_flt(c, pt) ? f_arith(e, op, pt, old, v) : arith(e, op, pt, old, v);
			old = cast(e, old, pt, lt, CK_INT);
		} else {
			old = conv_back(e, old, lt, pt);
			if (old.k == V_CONST && v.k == V_CONST) {
				bool ok;
				val f = fold_int_bin(e, op, (op == T_LESSLESS || op == T_GREATERGREATER) ? pt : pt, pt, old.i, v.i, &ok);

				if (ok)
					old = f;
				else
					old = is_wide(c, pt) ? arith2(e, op, pt, old, v) : arith(e, op, pt, old, v);
			} else {
				old = is_wide(c, pt) ? arith2(e, op, pt, old, v) : arith(e, op, pt, old, v);
			}
			old = conv_back(e, old, pt, lt);
		}
		store(e, &l, lt, &old);
		lfree(e, &l);
		return old;
	}
}

static val gen_incdec(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = x->ty;
	bool inc = x->op == N_PREINC || x->op == N_POSTINC, post = x->op == N_POSTINC || x->op == N_POSTDEC;
	lval l = gen_lval(e, x->a);
	val old = load(e, &l, t), nv;
	int64_t d = is_ptr(c, t) ? (int64_t)elem_size(c, t) : 1;

	if (KIND(c, t) == TY_BOOL) {
		nv = vconst(1);
		if (!inc) {
			/* --b: b = b - 1 != 0, i.e. !b */
			int r = treg(e), ro = toreg(e, &old);

			opi(e, I_EORI, 0, r, ro, 1);
			nv = vreg(r);
		}
	} else if (is_flt(c, t)) {
		int d = fpk(e, t);

		toreg(e, &old);
		if (post) {
			int r = treg(e);

			mov(e, 1, r, old.r);
			nv = f_arith(e, inc ? T_PLUS : T_MINUS, t, vreg(r), f_const(d, 1.0));
		} else {
			nv = old = f_arith(e, inc ? T_PLUS : T_MINUS, t, old, f_const(d, 1.0));
		}
	} else if (is_wide(c, t)) {
		val one = vconst(1), cp = old;

		toreg2(e, &cp, is_signed(c, t));
		if (post) {
			nv.k = V_REG2;
			nv.r = (uint8_t)treg(e);
			nv.r2 = (uint8_t)treg(e);
			mov(e, 1, nv.r, cp.r);
			mov(e, 1, nv.r2, cp.r2);
			nv = arith2(e, inc ? T_PLUS : T_MINUS, t, nv, one);
			old = cp;
		} else {
			nv = arith2(e, inc ? T_PLUS : T_MINUS, t, cp, one);
			old = nv;
		}
	} else {
		int ro = toreg(e, &old);

		if (post) {
			int r = treg(e);

			opi(e, inc ? I_ADDI : I_SUBI, X(c, t), r, ro, d);
			nv = vreg(r);
		} else {
			opi(e, inc ? I_ADDI : I_SUBI, X(c, t), ro, ro, d);
			nv = old;
		}
	}
	store(e, &l, t, &nv);
	lfree(e, &l);
	if (post) {
		vfree(e, &nv);
		return old;
	}
	if (KIND(c, t) == TY_BOOL)
		vfree(e, &old);
	return nv;
}

static val gen(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);

	switch (x->op) {
	case N_NUM:
		return vconst(c->ipool[x->a]);
	case N_FNUM:
		return f_const(fpk(e, x->ty), c->fpool[x->a]);
	case N_STR:
		note_ref(e, REF(RK_STR, x->a));
		return vaddr(REF(RK_STR, x->a), 0);
	case N_VAR:
		return gen_var(e, n);
	case N_MEMBER:
	case N_INDEX:
	case N_DEREF: {
		lval l = gen_lval(e, n);

		return load_free(e, &l, x->ty);
	}
	case N_COMPLIT: {
		lval l = gen_lval(e, n);

		return load_free(e, &l, x->ty);
	}
	case N_ADDR: {
		const node *a = N(x->a);
		lval l;

		if (a->op == N_LABELADDR || is_func(c, a->ty))
			return gen(e, x->a);
		l = gen_lval(e, x->a);
		if (l.k == L_SYM)
			note_ref(e, l.ref);
		return laddr(e, &l);
	}
	case N_LABELADDR: {
		uint32_t l = x->a;
		lbl *m = &e->lm[l];
		int r = treg(e);

		if (m->inst != e->f->inst) {
			m->inst = e->f->inst;
			m->id = newlabel(e);
		}
		if (e->reach) {
			ins *i = put(e, I_ADR);

			i->rd = (uint8_t)r;
			i->label = m->id;
		}
		e->lrefc[m->id]++;
		return vreg(r);
	}
	case N_CAST:
		switch (x->aux) {
		case CK_DECAY: {
			lval l = gen_lval(e, x->a);

			if (l.k == L_SYM)
				note_ref(e, l.ref);
			return laddr(e, &l);
		}
		case CK_FDECAY:
			return gen(e, x->a);
		case CK_VADDR:
			return gen(e, x->a);	/* a vector value is its address */
		case CK_VBITS: {
			/* the vector's bits as a scalar: load from its address */
			val v = gen(e, x->a);
			lval l = { .k = L_REG, .align = talign(c, N(x->a)->ty) };

			l.r = (uint8_t)toreg(e, &v);
			v = load(e, &l, x->ty);
			lfree(e, &l);
			return v;
		}
		case CK_UNION: {
			int32_t sl = slot(e, tsize(c, x->ty), talign(c, x->ty));
			lval l = { .k = L_FRAME, .off = sl, .align = talign(c, x->ty) };
			val v = gen(e, x->a);
			int r;

			if (tsize(c, x->ty) > tsize(c, N(x->a)->ty))
				for (uint64_t o = 0; o < tsize(c, x->ty); o += 8)
					mem(e, I_ST, tsize(c, x->ty) - o >= 8 ? 3 : 0, 0, ZR, FRAME, sl + (int64_t)o);
			store(e, &l, N(x->a)->ty, &v);
			vfree(e, &v);
			r = treg(e);
			opi(e, I_ADDI, 1, r, FRAME, sl);
			return vreg(r);
		}
		default: {
			val v = gen(e, x->a);

			return cast(e, v, N(x->a)->ty, x->ty, x->aux);
		}
		}
	case N_UNARY:
		return gen_unary(e, n);
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		return gen_incdec(e, n);
	case N_BINARY:
		return gen_binary(e, n);
	case N_LOGAND:
	case N_LOGOR:
		return gen_logical(e, n);
	case N_ASSIGN:
		return gen_assign(e, n);
	case N_COND:
		return gen_cond_expr(e, n);
	case N_COMMA: {
		if (side_effects(c, x->a)) {
			val v = gen(e, x->a);

			vfree(e, &v);
		}
		return gen(e, x->b);
	}
	case N_CALL:
		return gen_call(e, n);
	case N_BUILTIN:
		return gen_builtin(e, n);
	case N_STMTEXPR:
		return gen_block(e, x->a, true);
	case N_VAARG:
		return va_arg_(e, x);
	case N_ZERO:
		return vconst(0);
	case N_SIZEOF_VLA: {
		const sym *y = x->a && N(x->a)->op == N_VAR ? &c->syms[N(x->a)->a] : NULL;

		if (y && y->kind == S_VAR && binding(e, N(x->a)->a)->kind == B_INDIRECT &&
		    binding(e, N(x->a)->a)->inst == e->f->inst && (T(c, y->ty)->flags & TF_VLA)) {
			int r = treg(e);

			mem(e, I_LD, 3, 0, r, FRAME, binding(e, N(x->a)->a)->slot + 16);
			return vreg(r);
		}
		return vla_bytes(e, x->b);
	}
	default:
		efatal(e, x->loc, "unsupported expression (node %d)", x->op);
	}
}

/* ---------------- conditions ---------------- */

/* jump to l if n's truth == jump_if; returns the constant truth or -1 */
static void gen_cond(ecx *e, uint32_t n, bool jump_if, uint32_t l)
{
	cc *c = e->c;
	const node *x = N(n);

	switch (x->op) {
	case N_LOGAND:
	case N_LOGOR: {
		bool isand = x->op == N_LOGAND;

		if (isand != jump_if) {
			/* && jumping on false / || jumping on true: both operands jump to l */
			gen_cond(e, x->a, jump_if, l);
			gen_cond(e, x->b, jump_if, l);
		} else {
			uint32_t skip = newlabel(e);

			gen_cond(e, x->a, !jump_if, skip);
			gen_cond(e, x->b, jump_if, l);
			deflabel(e, skip);
		}
		return;
	}
	case N_UNARY:
		if (x->aux == T_EXCLAIM) {
			gen_cond(e, x->a, !jump_if, l);
			return;
		}
		break;
	case N_CAST:
		if (x->aux == CK_BOOL) {
			gen_cond(e, x->a, jump_if, l);
			return;
		}
		break;
	case N_BINARY:
		if (is_cmp(x->aux) && !is_wide(c, N(x->a)->ty) && !is_flt(c, N(x->a)->ty)) {
			val v = gen(e, n);

			if (v.k == V_CONST || v.k == V_ADDR) {
				test_zero(e, &v, x->ty, l, jump_if);
				return;
			}
			/* gen left a cset: turn it back into a branch on the flags */
			if (e->reach && e->nin && e->in[e->nin - 1].op == I_CSINC && e->in[e->nin - 1].rn == ZR &&
			    e->in[e->nin - 1].rm == ZR && e->in[e->nin - 1].rd == v.r) {
				int cond = e->in[e->nin - 1].cond ^ 1;

				e->nin--;
				bcond(e, jump_if ? cond : cond ^ 1, l);
				vfree(e, &v);
				return;
			}
			test_zero(e, &v, x->ty, l, jump_if);
			return;
		}
		break;
	}
	{
		val v = gen(e, n);

		test_zero(e, &v, x->ty, l, jump_if);
	}
}

/* ---------------- local initialisers ---------------- */

static void zero_lval(ecx *e, const lval *d, uint64_t size)
{
	int64_t off;
	int b;

	if (size > 256) {
		val a;
		lval t = *d;
		int r;

		a = laddr(e, &t);
		r = toreg(e, &a);
		zero_mem(e, r, size);
		if (d->k != L_REG)
			vfree(e, &a);
		return;
	}
	b = lbase(e, d, &off, 8);
	for (uint64_t o = 0; o < size;) {
		uint64_t k = size - o >= 8 ? 8 : size - o >= 4 ? 4 : size - o >= 2 ? 2 : 1;

		mem(e, I_ST, log2sz(k), 0, ZR, b, off + (int64_t)o);
		o += k;
	}
}

static bool is_zero_init(cc *c, uint32_t v)
{
	const node *x = N(v);

	if (!v || x->op == N_ZERO)
		return true;
	if (x->op == N_NUM)
		return c->ipool[x->a] == 0;
	if (x->op == N_CAST && (x->aux == CK_INT || x->aux == CK_NOP || x->aux == CK_NULL || x->aux == CK_I2P))
		return x->aux == CK_NULL || is_zero_init(c, x->a);
	return false;
}

/* store the elements of init into the (already zeroed) object at d */
static void init_elems(ecx *e, const lval *d, qty ty, uint32_t init)
{
	cc *c = e->c;
	const node *x = N(init);

	if (!init || x->op == N_ZERO)
		return;
	if (x->op == N_INIT) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			qty et = T(c, ty)->base;
			uint64_t es = tsize(c, et);

			for (uint32_t i = 0; i < x->c; i++) {
				uint32_t lo = v[3 * i], hi = v[3 * i + 1], val_ = v[3 * i + 2];

				for (uint64_t k = lo; k <= hi; k++) {
					lval el = *d;

					el.off += (int64_t)(k * es);
					el.align = align_at((uint64_t)el.off, d->align);
					init_elems(e, &el, et, val_);
				}
			}
			return;
		}
		{
			const tag *r = &c->tags[T(c, ty)->aux];

			for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
				const field *f = &r->f[i];
				lval el = *d;

				if (x->aux == 2 && i != x->a)
					continue;
				if (!v[i])
					continue;
				el.off += (int64_t)(f->off / 8);
				el.align = align_at((uint64_t)el.off, d->align);
				if (f->bitfield) {
					val bv;

					if (is_zero_init(c, v[i]))
						continue;
					bv = gen(e, v[i]);
					el.bf = 1;
					el.bitoff = (uint16_t)(f->off % 8);
					el.width = f->width;
					el.sgn = is_signed(c, f->ty);
					store(e, &el, f->ty, &bv);
					vfree(e, &bv);
					continue;
				}
				init_elems(e, &el, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == N_STR && is_array(c, ty)) {
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz, cap = tsize(c, ty);
		int64_t off;
		int b = lbase(e, d, &off, 8);

		if (n > cap)
			n = cap;
		for (uint64_t o = 0; o < n;) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;
			uint64_t w = 0;

			memcpy(&w, s->s + o, k);
			if (w) {
				movi(e, 1, R_X17 - 1, (int64_t)w);
				mem(e, I_ST, log2sz(k), 0, R_X16, b, off + (int64_t)o);
			}
			o += k;
		}
		return;
	}
	if (is_zero_init(c, init) && !is_agg(c, ty))
		return;
	{
		val v = gen(e, init);
		lval dd = *d;

		store(e, &dd, ty, &v);
		vfree(e, &v);
	}
}

static void gen_init_local(ecx *e, const lval *dst, qty ty, uint32_t init)
{
	cc *c = e->c;
	const node *x = N(init);

	if (!init)
		return;
	if (x->op == N_INIT || x->op == N_ZERO || (x->op == N_STR && is_array(c, ty))) {
		zero_lval(e, dst, tsize(c, ty));
		init_elems(e, dst, ty, init);
		return;
	}
	{
		val v = gen(e, init);
		lval d = *dst;

		store(e, &d, ty, &v);
		vfree(e, &v);
	}
}

/* ---------------- cleanups ---------------- */

static void run_cleanup(ecx *e, uint32_t s)
{
	cc *c = e->c;
	uint32_t fn = sym_attrs(c, s)->cleanup;
	carg a;
	lval l;

	if (T(c, c->syms[s].ty)->flags & TF_VLA) {
		const bind *b = binding(e, s);
		int r;

		if (!e->reach || b->kind != B_INDIRECT || b->inst != e->f->inst)
			return;
		r = treg(e);
		mem(e, I_LD, 3, 0, r, FRAME, b->slot + 8);
		mov(e, 1, SP, r);
		{
			val v = vreg(r);

			vfree(e, &v);
		}
		return;
	}
	if (!fn || !e->reach)
		return;
	l = local_lval(e, s);
	a.v = laddr(e, &l);
	a.t = prim(TY_LONG);
	a.slot = 1;
	fn = canon_sym(c, fn);
	if (can_inline(e, fn)) {
		val r = inline_call(e, fn, &a, 1);

		vfree(e, &r);
		return;
	}
	{
		val r = do_call(e, REF(RK_SYM, fn), NULL, &a, 1, prim(TY_VOID), false, -1);

		vfree(e, &r);
	}
}

/* run cleanups (innermost first) down to depth `to` without popping them */
static void run_cleanups(ecx *e, int to)
{
	for (int i = e->nclean; i-- > to;)
		run_cleanup(e, e->clean[i]);
}

/* ---------------- statements ---------------- */

static uint32_t user_label(ecx *e, uint32_t l)
{
	lbl *m = &e->lm[l];

	if (m->inst != e->f->inst) {
		m->inst = e->f->inst;
		m->id = newlabel(e);
	}
	return m->id;
}

static val gen_block(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	int32_t mark = e->foff;
	int cmark = e->nclean;
	val v = vnone();
	uint32_t vstmt = 0;
	qty vt = 0;

	if (want && x->c) {
		uint32_t ls = c->lists[x->b + x->c - 1];

		while (ls && N(ls)->op == N_LABEL)
			ls = N(ls)->c;
		if (ls && N(ls)->op == N_EXPR && N(ls)->a) {
			vstmt = ls;
			vt = N(N(ls)->a)->ty;
		}
	}
	/* an aggregate value must outlive the block's slots */
	int32_t vslot = 1;

	if (vstmt && is_agg(c, vt))
		vslot = slot(e, tsize(c, vt), talign(c, vt));
	mark = e->foff;
	for (uint32_t i = 0; i < x->c; i++) {
		uint32_t s = c->lists[x->b + i];

		if (i + 1 == x->c && vstmt) {
			uint32_t t = s;

			/* labels in front of the value */
			while (N(t)->op == N_LABEL) {
				uint32_t l = user_label(e, N(t)->a);

				if (e->E->lref[N(t)->a] & LR_BACK)
					deflabel_forced(e, l);
				else
					deflabel(e, l);
				t = N(t)->c;
			}
			v = gen(e, N(vstmt)->a);
			if (vslot != 1) {
				lval d = { .k = L_FRAME, .off = vslot, .align = talign(c, vt) };
				int r;

				store(e, &d, vt, &v);
				vfree(e, &v);
				r = treg(e);
				opi(e, I_ADDI, 1, r, FRAME, vslot);
				v = vreg(r);
			}
			break;
		}
		gen_stmt(e, s);
	}
	if (e->nclean > cmark) {
		run_cleanups(e, cmark);
		e->nclean = cmark;
	}
	e->foff = mark;
	return v;
}

static void collect_cases(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case N_CASE:
	case N_DEFAULT:
		e->cases[e->ncases++] = n;
		collect_cases(e, x->c);
		return;
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			collect_cases(e, c->lists[x->b + i]);
		return;
	case N_IF:
		collect_cases(e, x->b);
		collect_cases(e, x->c);
		return;
	case N_WHILE: case N_DO:
		collect_cases(e, x->b);
		return;
	case N_FOR:
		collect_cases(e, c->lists[x->c + 1]);
		return;
	case N_LABEL:
		collect_cases(e, x->c);
		return;
	default:
		return;
	}
}

static void gen_switch(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty ct = N(x->a)->ty;
	int cx = X(c, ct);
	val v = gen(e, x->a);
	uint32_t base = e->ncases, dflt = 0, lbrk = newlabel(e);

	if (is_wide(c, ct))
		efatal(e, x->loc, "128-bit switch is not supported");
	collect_cases(e, x->b);
	for (uint32_t k = base; k < e->ncases; k++) {
		uint32_t cn = e->cases[k];

		e->caselbl[cn] = newlabel(e);
		if (N(cn)->op == N_DEFAULT)
			dflt = e->caselbl[cn];
	}
	if (!dflt)
		dflt = lbrk;
	if (v.k == V_CONST) {
		uint32_t target = dflt;
		i128 cv = trunc_to(c, v.i, ct);

		for (uint32_t k = base; k < e->ncases; k++) {
			const node *cn = N(e->cases[k]);

			if (cn->op == N_CASE && c->ipool[cn->a] <= cv && cv <= c->ipool[cn->b]) {
				target = e->caselbl[e->cases[k]];
				break;
			}
		}
		jump(e, target);
	} else {
		int r = toreg(e, &v);

		for (uint32_t k = base; k < e->ncases; k++) {
			const node *cn = N(e->cases[k]);
			i128 lo, hi;

			if (cn->op != N_CASE)
				continue;
			lo = c->ipool[cn->a], hi = c->ipool[cn->b];
			if (lo == hi) {
				val a = vreg(r), b = vconst(lo);

				emit_cmp(e, T_EQUALEQUAL, cx, false, &a, &b);
				vfree(e, &b);
				bcond(e, C_EQ, e->caselbl[e->cases[k]]);
			} else {
				opi(e, I_SUBI, cx, R_X16, r, (int64_t)lo);
				if (hi - lo <= 4095) {
					opi(e, I_SUBSI, cx, ZR, R_X16, (int64_t)(hi - lo));
				} else {
					movi(e, cx, R_X17, (int64_t)(hi - lo));
					op3(e, I_SUBS, cx, ZR, R_X16, R_X17);
				}
				bcond(e, C_LS, e->caselbl[e->cases[k]]);
			}
		}
		vfree(e, &v);
		jump(e, dflt);
	}
	e->ncases = base + (e->ncases - base);	/* keep the labels until the body is done */
	if (e->nloop >= MAXLOOP)
		efatal(e, x->loc, "nesting too deep");
	e->loops[e->nloop++] = (loopctx){ lbrk, 0, e->nclean };
	e->reach = false;
	gen_stmt(e, x->b);
	e->nloop--;
	e->ncases = base;
	deflabel(e, lbrk);
}

static void gen_return(ecx *e, const node *x)
{
	cc *c = e->c;
	fnctx *f = e->f;
	qty rt = f->rty;
	val v = x->a ? gen(e, x->a) : vnone();

	if (!e->reach) {
		vfree(e, &v);
		return;
	}
	if (f->inl) {
		if (!is_void(c, rt) && x->a) {
			lval l = { .k = L_FRAME, .off = f->retslot, .align = talign(c, rt) };

			if (v.k == V_CONST || v.k == V_ADDR) {
				if (!f->any_ret)
					f->cst = v;
				else if (f->cst.k != v.k || f->cst.i != v.i || f->cst.ref != v.ref)
					f->cst_ok = false;
			} else {
				f->cst_ok = false;
			}
			f->any_ret = true;
			store(e, &l, rt, &v);
		}
		vfree(e, &v);
		run_cleanups(e, f->cbase);
		jump(e, f->retlbl);
		return;
	}
	if (!is_void(c, rt) && x->a && v.k != V_NONE) {
		if (e->nclean > f->cbase) {
			/* park the value while the cleanups run */
			if (is_agg(c, rt)) {
				int32_t sl = slot(e, tsize(c, rt), 16);
				int rs = toreg(e, &v), rd = treg(e);

				opi(e, I_ADDI, 1, rd, FRAME, sl);
				copy_mem(e, rd, rs, tsize(c, rt));
				tfree(e, rd);
				vfree(e, &v);
				run_cleanups(e, f->cbase);
				rd = treg(e);
				opi(e, I_ADDI, 1, rd, FRAME, sl);
				v = vreg(rd);
			} else {
				int32_t sl = slot(e, 16, 16);
				lval l = { .k = L_FRAME, .off = sl, .align = 16 };

				store(e, &l, rt, &v);
				vfree(e, &v);
				run_cleanups(e, f->cbase);
				v = load(e, &l, rt);
			}
		}
		if (is_recabi(c, rt)) {
			int rs = toreg(e, &v);

			if (tsize(c, rt) > 16) {
				int rd = treg(e);

				mem(e, I_LD, 3, 0, rd, FRAME, f->retslot);	/* the caller's buffer (x8 at entry) */
				copy_mem(e, rd, rs, tsize(c, rt));
				tfree(e, rd);
			} else {
				mem(e, I_LD, 3, 0, 0, rs, 0);
				if (tsize(c, rt) > 8)
					mem(e, I_LD, 3, 0, 1, rs, 8);
			}
		} else if (is_wide(c, rt)) {
			toreg2(e, &v, is_signed(c, rt));
			mov(e, 1, 0, v.r);
			mov(e, 1, 1, v.r2);
		} else {
			move_to(e, 0, -1, &v, rt);
			if (is_flt(c, rt))
				word(e, f_in(fpk(e, rt), 0, 0));	/* fmov d0/s0, x0/w0 */
		}
		vfree(e, &v);
	} else {
		vfree(e, &v);
		run_cleanups(e, f->cbase);
	}
	jump(e, f->retlbl);
}

static void gen_asm(ecx *e, uint32_t n);

/*
 * A variable length array: the slot holds { pointer, sp before the
 * allocation, size in bytes }.  The array lives below sp like alloca's
 * memory; leaving its scope (block end, break, goto, return) restores sp
 * through the cleanup stack.
 */
static val vla_bytes(ecx *e, qty t)
{
	cc *c = e->c;
	const ctype *a = T(c, t);
	val n, b;
	int r;

	if (a->kind != TY_ARRAY || !(a->flags & TF_VLA))
		return vconst(tsize(c, t));
	n = cast(e, gen(e, a->n), N(a->n)->ty, c->t_size, CK_INT);
	b = vla_bytes(e, a->base);
	r = toreg(e, &n);
	if (b.k == V_CONST) {
		if (b.i != 1) {
			movi(e, 1, R_X16, (int64_t)b.i);
			op4(e, I_MADD, 1, r, r, R_X16, ZR);
		}
	} else {
		int rb = toreg(e, &b);

		op4(e, I_MADD, 1, r, r, rb, ZR);
		vfree(e, &b);
	}
	return vreg(r);
}

static void gen_vla_decl(ecx *e, const node *x, uint32_t s)
{
	cc *c = e->c;
	bind *b = binding(e, s);
	val n = vla_bytes(e, c->syms[s].ty);
	int r = toreg(e, &n), t = treg(e);
	int32_t sl = slot(e, 24, 8);
	ins *i;

	b->inst = e->f->inst;
	b->kind = B_INDIRECT;
	b->kv = 0;
	b->reg = 0xff;
	b->slot = sl;
	e->dyn = true;
	mem(e, I_ST, 3, 0, r, FRAME, sl + 16);
	mov(e, 1, t, SP);
	mem(e, I_ST, 3, 0, t, FRAME, sl + 8);
	opi(e, I_ADDI, 1, r, r, 15);
	opi(e, I_ANDI, 1, r, r, -16);
	op3(e, I_SUB, 1, SP, SP, r);
	if (e->reach) {
		i = put(e, I_ADDI);
		i->sz = 1, i->rd = (uint8_t)t, i->rn = SP, i->imm = 0, i->cond = 0xee;	/* + outgoing area */
	}
	mem(e, I_ST, 3, 0, t, FRAME, sl);
	vfree(e, &n);
	{
		val tv = vreg(t);

		vfree(e, &tv);
	}
	if (e->nclean >= MAXCLEAN)
		efatal(e, x->loc, "too many cleanups");
	e->clean[e->nclean++] = s;
}

static void gen_decl(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t s = x->a;
	const sym *y = &c->syms[s];
	bind *b;

	if (y->kind != S_VAR || y->sc == SC_STATIC || y->sc == SC_EXTERN || y->sc == SC_TYPEDEF)
		return;
	if (T(c, y->ty)->flags & TF_VLA) {
		gen_vla_decl(e, x, s);
		return;
	}
	if (x->b && !(e->E->sflags[s] & SA_WRITTEN) && !is_agg(c, y->ty) && !(QQ(y->ty) & Q_VOLATILE) &&
	    !is_wide(c, y->ty) && y->sc != SC_REGISTER && !sym_attrs(c, s)->cleanup) {
		val v = gen(e, x->b);

		if (const_var(e, s, &v)) {
			b = binding(e, s);
			b->inst = e->f->inst;
			b->kind = B_CONST;
			b->kv = 0;
			b->v = v;
			return;
		}
		{
			lval l = { .k = L_FRAME, .align = sym_align(c, s) };

			l.off = bind_slot(e, s);
			kv_track(e, s, &v);
			store(e, &l, y->ty, &v);
			vfree(e, &v);
		}
	} else {
		lval l = { .k = L_FRAME, .align = sym_align(c, s) };
		int rr = regvar_reg(c, s);

		l.off = bind_slot(e, s);
		if (rr >= 0)
			binding(e, s)->reg = (uint8_t)rr;
		if (x->b)
			gen_init_local(e, &l, y->ty, x->b);
		else if (rr < 0 && y->sc != SC_REGISTER)
			kv_track(e, s, NULL);
	}
	if (sym_attrs(c, s)->cleanup) {
		if (e->nclean >= MAXCLEAN)
			efatal(e, x->loc, "too many cleanups");
		e->clean[e->nclean++] = s;
	}
}

static int find_loop(ecx *e, bool cont)
{
	for (int i = e->nloop; i-- > e->f->nloop;)
		if (!cont || e->loops[i].cont)
			return i;
	return -1;
}

static void gen_stmt1(ecx *e, uint32_t n);
static int leakchk = -1;

/* every statement leaves the temporaries it found; VCC_LEAKCHK reports the first that does not */
static void gen_stmt(ecx *e, uint32_t n)
{
	cc *c = e->c;
	uint32_t live = e->live, loc = e->curloc;

	if (n)
		e->curloc = N(n)->loc;
	gen_stmt1(e, n);
	e->curloc = loc;
	if (unlikely(leakchk < 0))
		leakchk = getenv("VCC_LEAKCHK") != NULL;
	if (unlikely(leakchk) && n && e->live != live) {
		ediag(e, N(n)->loc, "temporaries leaked: %#x -> %#x (op %d)", live, e->live, N(n)->op);
		leakchk = 0;
	}
}

static void gen_stmt1(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x;
	int32_t mark;

	if (!n)
		return;
	if (!e->reach && !(e->E->nflags[n] & NA_LABEL))
		return;
	x = N(n);
	mark = e->foff;
	switch (x->op) {
	case N_BLOCK:
		gen_block(e, n, false);
		return;
	case N_EXPR:
		/* a value thrown away: (void)(RCU) loads nothing (and refers to nothing) */
		if (x->a && side_effects(c, x->a)) {
			val v = gen(e, x->a);

			vfree(e, &v);
		}
		break;
	case N_DECL:
		gen_decl(e, x);
		return;
	case N_IF: {
		uint32_t lelse = newlabel(e), lend = newlabel(e);

		gen_cond(e, x->a, false, lelse);
		e->foff = mark;
		gen_stmt(e, x->b);
		if (x->c) {
			jump(e, lend);
			deflabel(e, lelse);
			gen_stmt(e, x->c);
			deflabel(e, lend);
		} else {
			deflabel(e, lelse);
		}
		return;
	}
	case N_WHILE: {
		uint32_t lhead = newlabel(e), lbrk = newlabel(e);

		deflabel_loop(e, lhead, e->E->nflags[n] & NA_LABEL);
		gen_cond(e, x->a, false, lbrk);
		e->foff = mark;
		e->loops[e->nloop++] = (loopctx){ lbrk, lhead, e->nclean };
		gen_stmt(e, x->b);
		e->nloop--;
		jump(e, lhead);
		deflabel(e, lbrk);
		return;
	}
	case N_DO: {
		uint32_t lhead = newlabel(e), lcont = newlabel(e), lbrk = newlabel(e);

		deflabel_loop(e, lhead, e->E->nflags[n] & NA_LABEL);
		e->loops[e->nloop++] = (loopctx){ lbrk, lcont, e->nclean };
		gen_stmt(e, x->b);
		e->nloop--;
		deflabel(e, lcont);
		gen_cond(e, x->a, true, lhead);
		e->foff = mark;
		deflabel(e, lbrk);
		return;
	}
	case N_FOR: {
		uint32_t lhead = newlabel(e), lcont = newlabel(e), lbrk = newlabel(e);
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int cmark = e->nclean;

		if (x->a) {
			const node *in = N(x->a);

			if (in->op == N_BLOCK) {
				for (uint32_t i = 0; i < in->c; i++)
					gen_stmt(e, c->lists[in->b + i]);
			} else {
				gen_stmt(e, x->a);
			}
		}
		deflabel_loop(e, lhead, e->E->nflags[n] & NA_LABEL);
		if (x->b) {
			int32_t m2 = e->foff;

			gen_cond(e, x->b, false, lbrk);
			e->foff = m2;
		}
		e->loops[e->nloop++] = (loopctx){ lbrk, lcont, e->nclean };
		gen_stmt(e, body);
		e->nloop--;
		deflabel(e, lcont);
		if (step) {
			int32_t m2 = e->foff;
			val v = gen(e, step);

			vfree(e, &v);
			e->foff = m2;
		}
		jump(e, lhead);
		deflabel(e, lbrk);
		if (e->nclean > cmark) {
			run_cleanups(e, cmark);
			e->nclean = cmark;
		}
		break;
	}
	case N_SWITCH:
		gen_switch(e, n);
		break;
	case N_CASE:
	case N_DEFAULT:
		deflabel(e, e->caselbl[n]);
		gen_stmt(e, x->c);
		return;
	case N_LABEL: {
		uint32_t l = user_label(e, x->a);

		if (e->E->lref[x->a] & LR_BACK)
			deflabel_forced(e, l);
		else
			deflabel(e, l);
		if (c->labels[x->a].addr_taken && !e->f->inl) {
			ins *i = put(e, I_RAW);
			char b[48];
			int k = snprintf(b, sizeof(b), ".Ltmp.lbl.%u:", x->a);

			i->off = (int32_t)e->raw.n;
			i->ref = (uint32_t)k;
			tb_put(&e->raw, b, (size_t)k);
		}
		gen_stmt(e, x->c);
		return;
	}
	case N_GOTO:
		if (e->reach) {
			run_cleanups(e, e->f->cbase + e->E->lcdepth[x->a] < e->nclean ? e->f->cbase + e->E->lcdepth[x->a] : e->nclean);
			jump(e, user_label(e, x->a));
		}
		return;
	case N_GOTO_IND: {
		val v = gen(e, x->a);
		int r = toreg(e, &v);

		if (e->reach)
			put(e, I_BR)->rn = (uint8_t)r;
		vfree(e, &v);
		e->reach = false;
		break;
	}
	case N_BREAK:
	case N_CONTINUE: {
		int i = find_loop(e, x->op == N_CONTINUE);

		if (i < 0)
			efatal(e, x->loc, "break/continue outside a loop");
		run_cleanups(e, e->loops[i].cdepth);
		jump(e, x->op == N_BREAK ? e->loops[i].brk : e->loops[i].cont);
		return;
	}
	case N_RETURN:
		gen_return(e, x);
		break;
	case N_ASM:
		gen_asm(e, n);
		break;
	default:
		efatal(e, x->loc, "unsupported statement (node %d)", x->op);
	}
	e->foff = mark;
}

/* ---------------- inline asm ---------------- */

enum { AO_REG, AO_IMM, AO_MEM, AO_ZERO, AO_VREG };

typedef struct asmopnd {
	uint8_t kind, reg, fixed, out, inout, wide;
	val v;			/* AO_IMM: the constant / address */
	lval l;			/* outputs: where the result goes */
	qty t;
	uint32_t name;
} asmopnd;

static bool imm_fits(int ch, i128 v)
{
	switch (ch) {
	case 'I': return v >= 0 && v <= 4095;
	case 'J': return v >= -4095 && v <= 0;
	case 'K': return logimm_ok((uint64_t)v, 0) && v == (i128)(uint32_t)v;
	case 'L': return logimm_ok((uint64_t)v, 1);
	case 'M': return v == (i128)(uint32_t)v;
	case 'N': return true;
	case 'i': case 'n': case 's': return true;
	default: return false;
	}
}

/* the register named by a clobber string, or -1 */
static int clob_reg(const char *s)
{
	if ((s[0] == 'x' || s[0] == 'w') && s[1] >= '0' && s[1] <= '9') {
		int r = atoi(s + 1);

		return r <= 30 ? r : -1;
	}
	if (!strcmp(s, "lr"))
		return 30;
	return -1;
}

/* a vector register load or store as raw text: op qv|dv, [xr] */
static void vraw(ecx *e, const char *opn, bool q, int v, int xr)
{
	tbuf *rb = &e->raw;
	size_t st = rb->n;
	ins *i;

	tb_c(rb, '\t');
	tb_s(rb, opn);
	tb_c(rb, '\t');
	tb_c(rb, q ? 'q' : 'd');
	tb_u(rb, (uint64_t)v);
	tb_s(rb, ", [");
	reg(rb, xr, 1);
	tb_c(rb, ']');
	i = put(e, I_RAW);
	i->off = (int32_t)st;
	i->ref = (uint32_t)(rb->n - st);
}

static void gen_asm(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const casm *a = &c->asms[x->a];
	const char *tp = c->strs[a->tmpl - 1].s;
	uint32_t nops = a->nout + a->nin;
	asmopnd *op;
	uint32_t clobs = 0, reserved = 0;
	uint32_t tramp[64];
	bool basic = nops == 0 && a->nclob == 0 && a->nlabel == 0;
	spill sp = { 0 };
	tbuf *rb = &e->raw;
	size_t start;
	uint32_t uniq;
	int nvreg = 0;

	if (!e->reach)
		return;
	if (nops > 64 || a->nlabel > 64)
		efatal(e, x->loc, "too many asm operands");
	op = alloca((nops + 1) * sizeof(asmopnd));
	memset(op, 0, (nops + 1) * sizeof(asmopnd));
	/* clobbered registers: keep temporaries out of them */
	for (uint32_t i = 0; i < a->nclob; i++) {
		int r = clob_reg(c->strs[a->clob[i] - 1].s);

		if (r >= 0)
			clobs |= 1u << r;
	}
	for (int i = 0; i < NTEMP; i++)
		if ((clobs >> tempregs[i]) & 1) {
			if (e->live & (1u << i)) {
				/* live across a clobber: save it */
				if (!sp.mask)
					memset(&sp, 0, sizeof(sp));
				sp.slot[i] = slot(e, 8, 8);
				mem(e, I_ST, 3, 0, tempregs[i], FRAME, sp.slot[i]);
				sp.mask |= 1u << i;
			} else {
				e->live |= 1u << i;
				reserved |= 1u << i;
			}
		}
	/* outputs: lvalues and registers */
	for (uint32_t i = 0; i < a->nout; i++) {
		const asmop *ao = &a->ops[i];
		const char *cs = c->strs[ao->constraint - 1].s;
		asmopnd *o = &op[i];
		bool mem_ok = strpbrk(cs, "mQog") != NULL, reg_ok = strpbrk(cs, "rg") != NULL;
		uint32_t en = ao->expr;

		o->out = 1;
		o->inout = strchr(cs, '+') != NULL;
		o->t = N(en)->ty;
		o->name = ao->name;
		if (is_vec(c, o->t) && strpbrk(cs, "wx")) {
			/* a vector in v16+: loaded before the template if read, stored after it */
			o->kind = AO_VREG;
			o->reg = (uint8_t)(16 + nvreg++);
			o->l = gen_lval(e, en);
			continue;
		}
		if (strpbrk(cs, "wxy") && !reg_ok)
			efatal(e, x->loc, "floating point asm operands are not supported");
		if (mem_ok && !reg_ok) {
			lval l = gen_lval(e, en);
			val av = laddr(e, &l);

			o->kind = AO_MEM;
			o->reg = (uint8_t)toreg(e, &av);
			continue;
		}
		o->kind = AO_REG;
		o->l = gen_lval(e, en);
		if (N(en)->op == N_VAR && is_local_var(c, N(en)->a) && binding(e, N(en)->a)->reg != 0xff &&
		    binding(e, N(en)->a)->kind == B_SLOT) {
			o->fixed = 1;
			o->reg = binding(e, N(en)->a)->reg;
		}
		o->wide = is_wide(c, o->t);
		if (o->inout) {
			lval l2 = o->l;
			val v = load(e, &l2, o->t);

			o->v = v;
		}
	}
	/* inputs */
	for (uint32_t i = a->nout; i < nops; i++) {
		const asmop *ao = &a->ops[i];
		const char *cs = c->strs[ao->constraint - 1].s;
		asmopnd *o = &op[i];
		uint32_t en = ao->expr;
		bool reg_ok = strpbrk(cs, "rpg") != NULL, mem_ok = strpbrk(cs, "mQog") != NULL;	/* p: address in a register */
		int tied = -1;

		o->t = N(en)->ty;
		o->name = ao->name;
		for (const char *p = cs; *p; p++)
			if (*p >= '0' && *p <= '9')
				tied = atoi(p);
		if (is_vec(c, o->t) && strpbrk(cs, "wx") && tied < 0) {
			val v = gen(e, en);	/* the vector's address */

			toreg(e, &v);
			o->kind = AO_VREG;
			o->reg = (uint8_t)(16 + nvreg++);
			o->v = v;
			continue;
		}
		if (tied >= 0 && (uint32_t)tied < a->nout) {
			/* the input goes into the output's register */
			val v = gen(e, en);
			asmopnd *to = &op[tied];

			vfree(e, &to->v);
			to->v = v;
			to->inout = 1;
			o->kind = AO_REG;
			o->reg = 0xff;
			o->fixed = 2;	/* alias of the output */
			o->inout = (uint8_t)tied;
			continue;
		}
		if (mem_ok && !reg_ok && !strpbrk(cs, "inIJKLMNZ")) {
			lval l;

			if (N(en)->op == N_CAST && N(en)->aux == CK_DECAY)
				en = N(en)->a;	/* an array as a memory operand: the array itself */
			l = gen_lval(e, en);
			val av = laddr(e, &l);

			o->kind = AO_MEM;
			o->reg = (uint8_t)toreg(e, &av);
			continue;
		}
		{
			val v = gen(e, en);
			bool imm = false;

			if (v.k == V_CONST || v.k == V_ADDR) {
				if (strchr(cs, 'Z') && v.k == V_CONST && v.i == 0) {
					o->kind = AO_ZERO;
					continue;
				}
				for (const char *p = cs; *p; p++)
					if (strchr("iInJKLMNsg", *p) && (v.k == V_ADDR ? (*p == 'i' || *p == 's' || *p == 'g') : *p == 'g' || imm_fits(*p, v.i)))
						imm = true;
			}
			if (imm) {
				o->kind = AO_IMM;
				o->v = v;
				if (v.k == V_ADDR)
					note_ref(e, v.ref);
				continue;
			}
			if (!reg_ok && !strchr(cs, 'Z')) {
				if (mem_ok) {
					/* a value for a memory operand: put it in a slot */
					int32_t sl = slot(e, tsize(c, o->t) ? tsize(c, o->t) : 8, 16);
					lval l = { .k = L_FRAME, .off = sl, .align = 16 };
					int r;

					store(e, &l, o->t, &v);
					vfree(e, &v);
					r = treg(e);
					opi(e, I_ADDI, 1, r, FRAME, sl);
					o->kind = AO_MEM;
					o->reg = (uint8_t)r;
					continue;
				}
				efatal(e, x->loc, "impossible constraint in asm: '%s'", cs);
			}
			o->kind = AO_REG;
			o->wide = is_wide(c, o->t);
			if (o->wide)
				toreg2(e, &v, is_signed(c, o->t));
			else
				toreg(e, &v);
			o->v = v;
			if (N(en)->op == N_VAR && is_local_var(c, N(en)->a) && binding(e, N(en)->a)->reg != 0xff &&
			    binding(e, N(en)->a)->kind == B_SLOT) {
				o->fixed = 1;
				o->reg = binding(e, N(en)->a)->reg;
			} else {
				o->reg = v.r;
			}
		}
	}
	/* output registers */
	for (uint32_t i = 0; i < a->nout; i++) {
		asmopnd *o = &op[i];

		if (o->kind != AO_REG)
			continue;
		if (o->inout) {
			if (o->wide)
				toreg2(e, &o->v, false);
			else if (o->v.k != V_REG)
				toreg(e, &o->v);
			if (!o->fixed)
				o->reg = o->v.r;
		} else if (!o->fixed) {
			o->reg = (uint8_t)treg(e);
			o->v = vreg(o->reg);
			if (o->wide) {
				o->v.k = V_REG2;
				o->v.r2 = (uint8_t)treg(e);
			}
		}
	}
	/* fixed registers last, so evaluating the others cannot clobber them */
	for (uint32_t i = 0; i < nops; i++) {
		asmopnd *o = &op[i];

		if (o->fixed == 1 && (o->inout || !o->out) && o->v.k != V_NONE)
			mov(e, 1, o->reg, o->v.r);
	}
	/* asm goto with outputs: the labels go through trampolines that store them */
	for (uint32_t k = 0; k < a->nlabel; k++)
		tramp[k] = a->nout ? newlabel(e) : user_label(e, a->labels[k]);
	/* vector operands into their registers, last so no call in between clobbers them */
	for (uint32_t i = 0; i < nops; i++) {
		asmopnd *o = &op[i];
		bool q = tsize(c, o->t) == 16;

		if (o->kind != AO_VREG)
			continue;
		if (!o->out) {
			vraw(e, "ldr", q, o->reg, o->v.r);
			vfree(e, &o->v);
		} else if (o->inout) {
			lval l2 = o->l;
			val av = laddr(e, &l2);
			int r = toreg(e, &av);

			vraw(e, "ldr", q, o->reg, r);
			vfree(e, &av);
		}
	}
	/* the template */
	start = rb->n;
	uniq = atomic_fetch_add(&e->E->asmno, 1);
	if (basic) {
		tb_s(rb, tp);
	} else {
		for (const char *p = tp; *p; p++) {
			char mod = 0;
			int idx = -1;

			if (*p != '%') {
				tb_c(rb, *p);
				continue;
			}
			p++;
			if (*p == '%') { tb_c(rb, '%'); continue; }
			if (*p == '=') { tb_u(rb, uniq); continue; }
			if (*p == '{' || *p == '|' || *p == '}') { tb_c(rb, *p); continue; }
			if (*p && strchr("wxcalHnPQRsqdbh", *p) && (p[1] == '[' || (p[1] >= '0' && p[1] <= '9')))
				mod = *p++;
			if (*p == '[') {
				const char *q = strchr(p, ']');
				uint32_t len = q ? (uint32_t)(q - p - 1) : 0;

				for (uint32_t i = 0; i < nops; i++)
					if (op[i].name && vintern_ents[op[i].name].len == len &&
					    !memcmp(vintern_ents[op[i].name].s, p + 1, len))
						idx = (int)i;
				if (idx < 0)
					for (uint32_t k = 0; k < a->nlabel; k++) {
						const label *lb = &c->labels[a->labels[k]];

						if (vintern_ents[lb->name].len == len && !memcmp(vintern_ents[lb->name].s, p + 1, len))
							idx = (int)(nops + k);
					}
				if (idx < 0)
					efatal(e, x->loc, "unknown asm operand name '%.*s'", (int)len, p + 1);
				p = q;
			} else if (*p >= '0' && *p <= '9') {
				idx = 0;
				while (p[0] >= '0' && p[0] <= '9')
					idx = idx * 10 + (*p++ - '0');
				p--;
			} else {
				efatal(e, x->loc, "invalid %% in asm string");
			}
			if ((uint32_t)idx >= nops) {
				uint32_t k = (uint32_t)idx - nops;

				if (k >= a->nlabel)
					efatal(e, x->loc, "invalid operand number in asm string");
				p_lbl(rb, e->top, tramp[k]);
				continue;
			}
			{
				asmopnd *o = &op[idx];

				if (o->fixed == 2)
					o = &op[o->inout];
				switch (o->kind) {
				case AO_VREG:
					tb_c(rb, mod == 'q' || mod == 'd' || mod == 's' || mod == 'h' || mod == 'b' ? mod : 'v');
					tb_u(rb, o->reg);
					break;
				case AO_REG:
					if (mod == 'H' && o->wide) {
						reg(rb, o->v.r2, 1);
					} else if (mod == 'a') {	/* address operand: [xN] */
						tb_c(rb, '[');
						reg(rb, o->reg, 1);
						tb_c(rb, ']');
					} else {
						reg(rb, o->reg, mod != 'w');
					}
					break;
				case AO_ZERO:
					reg(rb, ZR, mod != 'w');
					break;
				case AO_MEM:
					tb_c(rb, '[');
					reg(rb, o->reg, 1);
					tb_c(rb, ']');
					break;
				case AO_IMM:
					if (o->v.k == V_ADDR) {
						ref_off(c, rb, o->v.ref, (int64_t)o->v.i);
					} else {	/* clang prints a bare number, as the assembler reads it */
						tb_i(rb, mod == 'n' ? -(int64_t)o->v.i : (int64_t)o->v.i);
					}
					break;
				}
			}
		}
	}
	{
		ins *i = put(e, I_RAW);

		i->off = (int32_t)start;
		i->ref = (uint32_t)(rb->n - start);
	}
	for (uint32_t i = 0; i < a->nout; i++) {
		asmopnd *o = &op[i];

		if (o->kind == AO_VREG) {
			lval l2 = o->l;
			val av = laddr(e, &l2);
			int r = toreg(e, &av);

			vraw(e, "str", tsize(c, o->t) == 16, o->reg, r);
			vfree(e, &av);
		}
	}
	/* fixed output registers back into their temporaries */
	for (uint32_t i = 0; i < a->nout; i++) {
		asmopnd *o = &op[i];

		if (o->kind == AO_REG && o->fixed == 1) {
			if (o->v.k != V_REG) {
				o->v = vreg(treg(e));
			}
			mov(e, 1, o->v.r, o->reg);
		}
	}
	if (sp.mask)
		unspill(e, &sp);
	e->live &= ~reserved;
	/* store the outputs (on the fallthrough path and through each trampoline) */
	{
		uint32_t lafter = a->nlabel && a->nout ? newlabel(e) : 0;

		for (int pass = 0; pass <= (int)(a->nout ? a->nlabel : 0); pass++) {
			if (pass > 0) {
				deflabel_forced(e, tramp[pass - 1]);
			}
			for (uint32_t i = 0; i < a->nout; i++) {
				asmopnd *o = &op[i];

				if (o->kind != AO_REG)
					continue;
				{
					val v = o->v;
					lval l = o->l;

					store(e, &l, o->t, &v);
				}
			}
			if (pass > 0)
				jump(e, user_label(e, a->labels[pass - 1]));
			else if (lafter)
				jump(e, lafter);
		}
		if (lafter)
			deflabel(e, lafter);
	}
	for (uint32_t i = 0; i < nops; i++) {
		asmopnd *o = &op[i];

		if (o->kind == AO_REG && o->fixed != 2)
			vfree(e, &o->v);
		if (o->kind == AO_MEM)
			tfree(e, o->reg);
		if (o->out && o->kind == AO_REG)
			lfree(e, &o->l);
	}
	/* asm goto without outputs: the labels are branch targets */
	if (!a->nout)
		for (uint32_t k = 0; k < a->nlabel; k++) {
			kv_edge(e, tramp[k]);
			e->lrefc[tramp[k]]++;
		}
}

/* ---------------- functions ---------------- */

static void out_chunk(ecx *e, uint64_t key, size_t start)
{
	emitter *E = e->E;

	pthread_mutex_lock(&E->cmu);
	E->chunks[E->nchunk++] = (chunk){ key, e->out.p + start, e->out.n - start };
	pthread_mutex_unlock(&E->cmu);
}

static void sym_header(ecx *e, tbuf *b, uint32_t s, bool fn, const char *defsec, uint32_t align)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const attrs *a = sym_attrs(c, s);
	bool global = y->sc != SC_STATIC && y->depth == 0 && y->name;

	if (a->section) {
		const char *sn = c->strs[a->section - 1].s;
		bool bss = !strncmp(sn, ".bss", 4) || !strncmp(sn, ".tbss", 5);

		tb_s(b, "\t.section\t\"");
		tb_s(b, sn);
		tb_s(b, fn ? "\",\"ax\",@progbits\n" : bss ? "\",\"aw\",@nobits\n" :
			 (QQ(y->ty) & Q_CONST) && !strncmp(sn, ".rodata", 7) ? "\",\"a\",@progbits\n" : "\",\"aw\",@progbits\n");
	} else {
		tb_s(b, defsec);
	}
	if (global) {
		tb_s(b, (y->flags & SF_WEAK) ? "\t.weak\t" : "\t.globl\t");
		sym_name(c, b, s);
		tb_c(b, '\n');
		if (a->visibility) {
			const char *v = c->strs[a->visibility - 1].s;

			if (!strcmp(v, "hidden") || !strcmp(v, "protected")) {
				tb_s(b, !strcmp(v, "hidden") ? "\t.hidden\t" : "\t.protected\t");
				sym_name(c, b, s);
				tb_c(b, '\n');
			}
		}
	}
	tb_s(b, "\t.p2align\t");
	tb_u(b, (uint64_t)__builtin_ctz(align ? align : 1));
	tb_s(b, fn ? "\n\t.type\t" : "\n\t.type\t");
	sym_name(c, b, s);
	tb_s(b, fn ? ",@function\n" : ",@object\n");
	sym_name(c, b, s);
	tb_s(b, ":\n");
}

/* finish_function for the object backend: the same function as items */
static void finish_direct(ecx *e, uint32_t s, uint32_t align, const int *cs, int ncs, int32_t csave, int32_t total, bool ctor)
{
	cc *c = e->c;
	tbuf *b = &e->out;
	size_t start, h;
	code_hdr *ch;
	ins *in;
	uint32_t n = 0, sym_labels = 0;

	b->n = (b->n + 7) & ~(size_t)7;
	start = b->n;
	h = it_open(e, IT_TEXT);
	sym_header(e, b, s, true, "\t.text\n", align);
	it_close(e, h);
	for (uint32_t k = 0; k < e->nin && !sym_labels; k++)
		sym_labels = e->in[k].op == I_RAW;
	h = it_open(e, IT_CODE);
	ch = it_room(e, sizeof *ch);
	in = it_room(e, ((size_t)e->nin + 2 * (size_t)ncs + 8) * sizeof(ins));
#define PUT(...) (in[n++] = (ins){ __VA_ARGS__ })
	PUT(.op = I_STP, .sz = 1, .rd = R_FP, .ra = R_LR, .rn = SP, .cond = 1, .imm = -16);
	PUT(.op = I_MOV, .sz = 1, .rd = R_FP, .rn = SP);
	if (total > 16)
		PUT(.op = I_SUBI, .sz = 1, .rd = SP, .rn = SP, .imm = total - 16);
	for (int k = 0; k + 1 < ncs; k += 2)
		PUT(.op = I_STP, .sz = 1, .rd = (uint8_t)cs[k], .ra = (uint8_t)cs[k + 1], .rn = R_FP, .imm = -csave + 8 * k);
	if (ncs & 1)
		PUT(.op = I_ST, .sz = 3, .rd = (uint8_t)cs[ncs - 1], .rn = R_FP, .imm = -csave + 8 * (ncs - 1));
	memcpy(in + n, e->in, (size_t)e->nin * sizeof(ins));
	n += e->nin;
	for (int k = 0; k + 1 < ncs; k += 2)
		PUT(.op = I_LDP, .sz = 1, .rd = (uint8_t)cs[k], .ra = (uint8_t)cs[k + 1], .rn = R_FP, .imm = -csave + 8 * k);
	if (ncs & 1)
		PUT(.op = I_LD, .sz = 3, .rd = (uint8_t)cs[ncs - 1], .rn = R_FP, .imm = -csave + 8 * (ncs - 1));
	PUT(.op = I_MOV, .sz = 1, .rd = SP, .rn = R_FP);
	PUT(.op = I_LDP, .sz = 1, .rd = R_FP, .ra = R_LR, .rn = SP, .cond = 2, .imm = 16);
	PUT(.op = I_RET);
#undef PUT
	/* give back the unused room, then the inline asm text */
	b->n -= ((size_t)e->nin + 2 * (size_t)ncs + 8 - n) * sizeof(ins);
	*ch = (code_hdr){ .fid = s, .nin = n, .nraw = sym_labels ? (uint32_t)e->raw.n : 0, .nlabel = e->nlabel, .sym_labels = sym_labels };
	if (sym_labels)
		memcpy(it_room(e, e->raw.n), e->raw.p, e->raw.n);
	it_close(e, h);
	h = it_open(e, IT_TEXT);
	tb_s(b, ".Lfe");
	tb_u(b, s);
	tb_s(b, ":\n\t.size\t");
	sym_name(c, b, s);
	tb_s(b, ", .Lfe");
	tb_u(b, s);
	tb_c(b, '-');
	sym_name(c, b, s);
	tb_c(b, '\n');
	if (ctor) {
		tb_s(b, "\t.section\t.init_array,\"aw\",@init_array\n\t.p2align\t3\n\t.xword\t");
		sym_name(c, b, s);
		tb_c(b, '\n');
	}
	it_close(e, h);
	e->E->nins += e->nin;
	e->E->nfn++;
	out_chunk(e, (uint64_t)s << 1, start);
}

static void finish_function(ecx *e, uint32_t s)
{
	cc *c = e->c;
	tbuf *b = &e->out;
	size_t start = b->n;
	int cs[16], ncs = 0;
	int32_t csave, locals, outs, total;
	const attrs *a = sym_attrs(c, s);
	uint32_t align = 4;

	for (int r = 19; r <= 28; r++)
		if (e->usedcs & (1u << r))
			cs[ncs++] = r;
	csave = (ncs * 8 + 15) & ~15;
	locals = (e->fmax + 15) & ~15;
	outs = (e->outmax + 15) & ~15;
	total = 16 + csave + locals + outs;
	/* rewrite frame-slot references */
	for (uint32_t k = 0; k < e->nin; k++) {
		ins *i = &e->in[k];

		if (i->op == I_ADDI && i->rn == SP && i->cond == 0xee) {
			i->imm += outs;
			i->cond = 0;
			continue;
		}
		if (i->rn != FRAME)
			continue;
		if (e->dyn) {
			i->rn = R_FP;
			i->imm = i->imm - csave;
		} else {
			i->rn = SP;
			i->imm = i->imm - csave + (total - 16);
		}
	}
	if (a->aligned > align)
		align = a->aligned;
	if (c->syms[s].align > align)
		align = c->syms[s].align;
	if (direct(e)) {
		finish_direct(e, s, align, cs, ncs, csave, total, a->constructor != 0);
		return;
	}
	sym_header(e, b, s, true, "\t.text\n", align);
	/* prologue */
	tb_s(b, "\tstp\tx29, x30, [sp, #-16]!\n\tmov\tx29, sp\n");
	if (total > 16) {
		ins i = { .op = I_SUBI, .sz = 1, .rd = SP, .rn = SP, .imm = total - 16 };

		print_ins(c, b, &i, s, e->raw.p);
	}
	for (int k = 0; k + 1 < ncs; k += 2)
		tb_f(b, "\tstp\tx%d, x%d, [x29, #%d]\n", cs[k], cs[k + 1], -csave + 8 * k);
	if (ncs & 1)
		tb_f(b, "\tstr\tx%d, [x29, #%d]\n", cs[ncs - 1], -csave + 8 * (ncs - 1));
	for (uint32_t k = 0; k < e->nin; k++)
		print_ins(c, b, &e->in[k], s, e->raw.p);
	/* epilogue */
	for (int k = 0; k + 1 < ncs; k += 2)
		tb_f(b, "\tldp\tx%d, x%d, [x29, #%d]\n", cs[k], cs[k + 1], -csave + 8 * k);
	if (ncs & 1)
		tb_f(b, "\tldr\tx%d, [x29, #%d]\n", cs[ncs - 1], -csave + 8 * (ncs - 1));
	tb_s(b, "\tmov\tsp, x29\n\tldp\tx29, x30, [sp], #16\n\tret\n.Lfe");
	tb_u(b, s);
	tb_s(b, ":\n\t.size\t");
	sym_name(c, b, s);
	tb_s(b, ", .Lfe");
	tb_u(b, s);
	tb_c(b, '-');
	sym_name(c, b, s);
	tb_c(b, '\n');
	if (a->constructor) {
		tb_s(b, "\t.section\t.init_array,\"aw\",@init_array\n\t.p2align\t3\n\t.xword\t");
		sym_name(c, b, s);
		tb_c(b, '\n');
	}
	e->E->nins += e->nin;
	e->E->nfn++;
	out_chunk(e, (uint64_t)s << 1, start);
}

static void emit_function(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const node *body = N(y->body);
	const ctype *ft = T(c, y->ty);
	qty rty = ft->base;
	fnctx F;
	int ngrn = 0, nsaa = 0;

	ensure_walked(e->E, c, s);
	e->nin = 0;
	e->raw.n = 0;
	e->nlabel = 0;
	e->nkv = 0, e->kvalid = 0, e->nkvs = 0;
	e->foff = e->fmax = e->outmax = 0;
	e->dyn = e->calls = false;
	e->live = 0;
	e->usedcs = 0;
	e->nloop = e->nclean = 0;
	e->ncases = 0;
	e->reach = true;
	e->top = s;
	e->inline_depth = 0;
	e->vararg = (ft->flags & TF_VARIADIC) != 0;
	memset(&F, 0, sizeof(F));
	F.fn = s;
	F.inst = ++e->inst_next;
	F.rty = rty;
	F.retlbl = newlabel(e);
	e->f = &F;
	if (_setjmp(e->bail)) {
		e->f = NULL;
		return;
	}
	if (is_recabi(c, rty) && tsize(c, rty) > 16) {
		F.retslot = slot(e, 8, 8);
		mem(e, I_ST, 3, 0, 8, FRAME, F.retslot);
	}
	/* parameters */
	e->nsrn = 0;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		qty pt = c->syms[p].ty;
		abiarg ab;
		bind *bd = binding(e, p);
		uint64_t sz = tsize(c, pt);

		classify(e, pt, &ngrn, &nsaa, &ab);
		bd->inst = F.inst;
		bd->reg = 0xff;
		switch (ab.kind) {
		case A_IGNORE:
			bd->kind = B_SLOT;
			bd->slot = slot(e, 8, 8);
			break;
		case A_REG:
			bd->kind = B_SLOT;
			bd->slot = slot(e, (sz + 7) & ~(uint64_t)7, talign(c, pt) > 8 ? 16 : 8);
			for (int k = 0; k < ab.nreg; k++)
				mem(e, I_ST, is_agg(c, pt) || sz == 16 ? 3 : log2sz(sz), 0, ab.reg + k, FRAME, bd->slot + 8 * k);
			break;
		case A_STACK:
			bd->kind = B_SLOT;
			bd->slot = slot(e, (sz + 7) & ~(uint64_t)7, talign(c, pt) > 8 ? 16 : 8);
			if (DARWIN(c) && sz < 8 && !is_agg(c, pt)) {	/* Apple: packed */
				mem(e, I_LD, log2sz(sz), 0, R_X16, R_FP, 16 + ab.stk);
				mem(e, I_ST, log2sz(sz), 0, R_X16, FRAME, bd->slot);
				break;
			}
			for (int k = 0; k < ab.nreg; k++) {
				mem(e, I_LD, 3, 0, R_X16, R_FP, 16 + ab.stk + 8 * k);
				mem(e, I_ST, 3, 0, R_X16, FRAME, bd->slot + 8 * k);
			}
			break;
		case A_REF_REG:
			bd->kind = B_INDIRECT;
			bd->slot = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, ab.reg, FRAME, bd->slot);
			break;
		case A_REF_STACK:
			bd->kind = B_INDIRECT;
			bd->slot = slot(e, 8, 8);
			mem(e, I_LD, 3, 0, R_X16, R_FP, 16 + ab.stk);
			mem(e, I_ST, 3, 0, R_X16, FRAME, bd->slot);
			break;
		case A_VREG:
			bd->kind = B_SLOT;
			bd->slot = slot(e, 8, 8);
			word(e, f_out(sz == 8, R_X16, ab.reg));
			mem(e, I_ST, log2sz(sz), 0, R_X16, FRAME, bd->slot);
			break;
		}
	}
	if (e->vararg && DARWIN(c)) {
		e->ngr_named = ngrn;
		e->nstk_named = nsaa;
	} else if (e->vararg) {
		e->ngr_named = ngrn;
		e->nstk_named = nsaa;
		e->vaslot = slot(e, 64, 16);
		for (int r = ngrn; r < 8; r++)
			mem(e, I_ST, 3, 0, r, FRAME, e->vaslot + 8 * r);
		/* v0-v7: 16 bytes each (the low 8 hold a float or double) */
		e->nvr_named = e->nsrn;
		e->vrslot = slot(e, 128, 16);
		for (int r = e->nsrn; r < 8; r++) {
			word(e, f_out(1, R_X16, r));
			mem(e, I_ST, 3, 0, R_X16, FRAME, e->vrslot + 16 * r);
		}
	}
	gen_stmt(e, y->body);
	/* falling off the end of main returns 0 */
	if (e->reach && y->name && vintern_ents[y->name].len == 4 && !memcmp(vintern_ents[y->name].s, "main", 4))
		movi(e, 0, 0, 0);
	deflabel(e, F.retlbl);
	e->f = NULL;
	if (unlikely(e->E->stats)) {
		double t = now();

		finish_function(e, s);
		e->E->print_ns += (uint64_t)((now() - t) * 1e9);
	} else {
		finish_function(e, s);
	}
}

/* ---------------- data ---------------- */

static void data_put(ecx *e, uint64_t off, uint64_t v, int n)
{
	if (off + (uint64_t)n > e->data.n) {
		if (off + (uint64_t)n > e->data.cap)
			efatal(e, 0, "static object too large");
		memset(e->data.p + e->data.n, 0, off + (uint64_t)n - e->data.n);
		e->data.n = off + (uint64_t)n;
	}
	for (int i = 0; i < n; i++)
		e->data.p[off + (uint64_t)i] = (char)(v >> (8 * i));
	if (unlikely(n && off < e->relend))
		for (uint32_t j = 0; j < e->nrel; j++)	/* overwritten: the relocation goes */
			if (e->rel[j].off < off + (uint64_t)n && off < e->rel[j].off + 8)
				e->rel[j].seq = UINT32_MAX;
}

static void data_or(ecx *e, uint64_t off, uint8_t bits)
{
	data_put(e, off, 0, 0);
	if (off >= e->data.n)
		data_put(e, off, 0, 1);
	e->data.p[off] |= (char)bits;
}

static void data_reloc(ecx *e, uint64_t off, uint32_t ref, int64_t add)
{
	data_put(e, off, 0, 8);
	e->rel[e->nrel] = (struct reloc){ off, ref, e->nrel, add };
	e->nrel++;
	if (off + 8 > e->relend)
		e->relend = off + 8;
	note_ref(e, ref);
}

static void data_init(ecx *e, uint64_t off, qty ty, uint32_t init)
{
	cc *c = e->c;
	const node *x;
	cval cv;

	if (!init)
		return;
	x = N(init);
	if (x->op == N_ZERO)
		return;
	if (x->op == N_INIT) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			qty et = T(c, ty)->base;
			uint64_t es = tsize(c, et);

			uint64_t n = 0;

			for (uint32_t i = 0; i < x->c; i++) {
				for (uint64_t k = v[3 * i]; k <= v[3 * i + 1]; k++)
					data_init(e, off + k * es, et, v[3 * i + 2]);
				if (v[3 * i + 1] + 1 > n)
					n = v[3 * i + 1] + 1;
			}
			if (T(c, ty)->flags & TF_INCOMPLETE)
				data_put(e, off + n * es, 0, 0);	/* flexible array member: its initialized length counts */
			return;
		}
		{
			const tag *r = &c->tags[T(c, ty)->aux];

			for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
				const field *f = &r->f[i];

				if (x->aux == 2 && i != x->a)
					continue;
				if (!v[i])
					continue;
				if (f->bitfield) {
					uint64_t bo = f->off, w = f->width, bits;

					if (!cc_eval(c, v[i], &cv) || cv.kind != CV_INT)
						efatal(e, x->loc, "initializer element is not a compile-time constant");
					bits = (uint64_t)cv.i & (w >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1));
					for (uint64_t k = 0; k < w; k++) {
						uint64_t at = off + (bo + k) / 8;
						uint8_t m = (uint8_t)(1u << ((bo + k) % 8));

						data_or(e, at, 0);
						e->data.p[at] = (char)((e->data.p[at] & ~m) | (((bits >> k) & 1) ? m : 0));
					}
					continue;
				}
				data_init(e, off + f->off / 8, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == N_STR && is_array(c, ty)) {
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz, cap = tsize(c, ty);

		if (T(c, ty)->flags & TF_INCOMPLETE)
			cap = n;
		if (n > cap)
			n = cap;
		for (uint64_t i = 0; i < n; i++)
			data_put(e, off + i, (uint8_t)s->s[i], 1);
		return;
	}
	if (x->op == N_COMPLIT) {
		data_init(e, off, ty, x->a);
		return;
	}
	if (x->op == N_CAST && x->aux == CK_NOP && is_agg(c, ty)) {
		data_init(e, off, ty, x->a);
		return;
	}
	if (is_agg(c, ty)) {
		/* a const aggregate object as the initializer (clang extension) */
		uint32_t m = strip_casts(c, init);

		if (N(m)->op == N_VAR && c->syms[N(m)->a].body) {
			data_init(e, off, ty, c->syms[N(m)->a].body);
			return;
		}
		efatal(e, x->loc, "initializer element is not a compile-time constant");
	}
	if (!cc_eval(c, init, &cv))
		efatal(e, x->loc, "initializer element is not a compile-time constant");
	{
		int n = (int)tsize(c, ty);

		switch (cv.kind) {
		case CV_INT:
			if (n == 16) {
				data_put(e, off, (uint64_t)(u128)cv.i, 8);
				data_put(e, off + 8, (uint64_t)((u128)cv.i >> 64), 8);
			} else {
				data_put(e, off, (uint64_t)cv.i, n);
			}
			return;
		case CV_FLT:
			if (n == 4) {
				float f = (float)cv.f;
				uint32_t u;

				memcpy(&u, &f, 4);
				data_put(e, off, u, 4);
			} else {
				uint64_t u;

				memcpy(&u, &cv.f, 8);
				data_put(e, off, u, 8);
				if (n == 16)
					efatal(e, x->loc, "long double initializers are not supported");
			}
			return;
		case CV_ADDR:
			if (n != 8)
				efatal(e, x->loc, "address initializer for a %d-byte object", n);
			if (cv.label)
				data_reloc(e, off, REF(RK_LBL, cv.label - 1), (int64_t)cv.i);
			else if (cv.str)
				data_reloc(e, off, REF(RK_STR, cv.str - 1), (int64_t)cv.i);
			else
				data_reloc(e, off, REF(RK_SYM, canon_sym(c, cv.sym)), (int64_t)cv.i);
			return;
		default:
			efatal(e, x->loc, "initializer element is not a compile-time constant");
		}
	}
}

static int rel_cmp(const void *a, const void *b)
{
	const struct reloc *x = a, *y = b;

	if (x->off != y->off)
		return x->off < y->off ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* sort e->rel by offset; a later initializer for the same element overrides: keep the last */
static void rel_prep(ecx *e)
{
	uint32_t k = 0;

	qsort(e->rel, e->nrel, sizeof(e->rel[0]), rel_cmp);
	for (uint32_t j = 0; j < e->nrel; j++) {
		if (e->rel[j].seq == UINT32_MAX)
			continue;
		if (k && e->rel[k - 1].off == e->rel[j].off)
			k--;
		e->rel[k++] = e->rel[j];
	}
	e->nrel = k;
}

/* the object backend's data item: what data_bytes / .zero would assemble to */
static void data_item(ecx *e, uint64_t size, bool zero)
{
	size_t h = it_open(e, IT_DATA);
	data_hdr *dh = it_room(e, sizeof *dh);
	uint32_t nrel = 0;

	if (zero) {
		*dh = (data_hdr){ .size = size, .zero = 1 };
	} else {
		rel_prep(e);
		while (nrel < e->nrel && e->rel[nrel].off < size)
			nrel++;
		*dh = (data_hdr){ .size = size, .n = e->data.n < size ? e->data.n : size, .nrel = nrel };
		memcpy(it_room(e, nrel * sizeof(ireloc)), e->rel, nrel * sizeof(ireloc));
		memcpy(it_room(e, dh->n), e->data.p, dh->n);
	}
	it_close(e, h);
}

/* the bytes in e->data with e->rel, as directives */
static void data_bytes(ecx *e, tbuf *b, uint64_t size)
{
	uint64_t o = 0;
	uint32_t ri = 0;
	const unsigned char *d = (const unsigned char *)e->data.p;

	rel_prep(e);
	while (o < size) {
		uint64_t end = ri < e->nrel ? e->rel[ri].off : size;

		if (ri < e->nrel && o == e->rel[ri].off) {
			tb_s(b, "\t.xword\t");
			ref_off(e->c, b, e->rel[ri].ref, e->rel[ri].add);
			tb_c(b, '\n');
			o += 8;
			ri++;
			continue;
		}
		while (o < end) {
			uint64_t z = o;

			while (z < end && (z >= e->data.n || !d[z]))
				z++;
			if (z - o >= 8 || z == end) {
				if (z > o) {
					tb_s(b, "\t.zero\t");
					tb_u(b, z - o);
					tb_c(b, '\n');
				}
				o = z;
				continue;
			}
			if (!(o & 7) && o + 8 <= end) {
				uint64_t w = 0;

				/* o < data.n here; past it the bytes are zero, not what the buffer holds */
				memcpy(&w, d + o, o + 8 <= e->data.n ? 8 : e->data.n - o);
				tb_s(b, "\t.xword\t");
				tb_hex(b, w);
				tb_c(b, '\n');
				o += 8;
				continue;
			}
			tb_s(b, "\t.byte\t");
			tb_u(b, o < e->data.n ? d[o] : 0);
			tb_c(b, '\n');
			o++;
		}
	}
}

static bool all_const(cc *c, qty t)
{
	if (QQ(t) & Q_CONST)
		return true;
	if (is_array(c, t))
		return all_const(c, T(c, t)->base);
	return false;
}

static void emit_object(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	tbuf *b = &e->out;
	size_t start = b->n;
	uint64_t size = tsize(c, y->ty);
	uint32_t align = sym_align(c, s);
	const attrs *a = sym_attrs(c, s);
	bool zero;

	if (_setjmp(e->bail))
		return;
	e->data.n = 0;
	e->nrel = 0, e->relend = 0;
	e->reach = true;
	if (y->body)
		data_init(e, 0, y->ty, y->body);
	if (e->data.n > size)
		size = e->data.n;	/* flexible array member initialised */
	if (size == 0 && !y->body && is_array(c, y->ty) && (T(c, y->ty)->flags & TF_INCOMPLETE))
		size = tsize(c, T(c, y->ty)->base);	/* int x[]; is one element */
	zero = e->nrel == 0;
	for (uint64_t i = 0; zero && i < e->data.n; i++)
		zero = !e->data.p[i];
	if (!a->section && (y->flags & SF_THREAD) && !(DARWIN(c) && direct(e)))
		efatal(e, y->loc, "thread-local storage is not supported");
	if (direct(e) && (y->flags & SF_THREAD)) {
		/*
		 * Darwin TLV: the initial value in __thread_data/__thread_bss under
		 * a local label, the symbol itself on a descriptor in
		 * __thread_vars: { __tlv_bootstrap, 0, initial value }.
		 */
		size_t h;
		bool global = y->sc != SC_STATIC && y->depth == 0 && y->name;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		h = it_open(e, IT_TEXT);
		tb_s(b, zero ? "\t.section\t.tbss,\"aw\",@nobits\n" : "\t.section\t.tdata,\"aw\",@progbits\n");
		tb_s(b, "\t.p2align\t");
		tb_u(b, (uint64_t)__builtin_ctz(align ? align : 1));
		tb_s(b, "\n.Ltlvi.");
		tb_u(b, s);
		tb_s(b, ":\n");
		it_close(e, h);
		data_item(e, zero && !e->nrel ? (size ? size : 1) : size, zero && !e->nrel);
		h = it_open(e, IT_TEXT);
		tb_s(b, "\t.section\t.tlv,\"aw\",@progbits\n");
		if (global) {
			tb_s(b, (y->flags & SF_WEAK) ? "\t.weak\t" : "\t.globl\t");
			sym_name(c, b, s);
			tb_c(b, '\n');
		}
		tb_s(b, "\t.p2align\t3\n");
		sym_name(c, b, s);
		tb_s(b, ":\n\t.xword\t__tlv_bootstrap\n\t.xword\t0\n\t.xword\t.Ltlvi.");
		tb_u(b, s);
		tb_c(b, '\n');
		it_close(e, h);
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (direct(e)) {
		size_t h;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		h = it_open(e, IT_TEXT);
		sym_header(e, b, s, false,
			   all_const(c, y->ty) ? "\t.section\t.rodata,\"a\",@progbits\n" : zero ? "\t.bss\n" : "\t.data\n", align);
		it_close(e, h);
		data_item(e, zero && !e->nrel ? (size ? size : 1) : size, zero && !e->nrel);
		h = it_open(e, IT_TEXT);
		tb_s(b, "\t.size\t");
		sym_name(c, b, s);
		tb_s(b, ", ");
		tb_u(b, size);
		tb_c(b, '\n');
		it_close(e, h);
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	sym_header(e, b, s, false,
		   all_const(c, y->ty) ? "\t.section\t.rodata,\"a\",@progbits\n" : zero ? "\t.bss\n" : "\t.data\n", align);
	if (zero && !e->nrel) {
		if (a->section && !strncmp(c->strs[a->section - 1].s, ".bss", 4)) {
			tb_s(b, "\t.zero\t");
			tb_u(b, size ? size : 1);
			tb_c(b, '\n');
		} else {
			tb_s(b, "\t.zero\t");
			tb_u(b, size ? size : 1);
			tb_c(b, '\n');
		}
	} else {
		data_bytes(e, b, size);
	}
	tb_s(b, "\t.size\t");
	sym_name(c, b, s);
	tb_s(b, ", ");
	tb_u(b, size);
	tb_c(b, '\n');
	e->E->nobj++;
	out_chunk(e, ((uint64_t)s << 1) | 1, start);
}

static void emit_string(ecx *e, uint32_t i)
{
	cc *c = e->c;
	const cstr *s = &c->strs[i];
	tbuf *b = &e->out;
	size_t start = b->n;
	bool nul = false;
	uint64_t n = (uint64_t)s->n * s->esz;

	for (uint64_t k = 0; k + s->esz < n; k++)
		if (!s->s[k] && s->esz == 1)
			nul = true;
	if (direct(e)) {
		size_t h;
		uint32_t ref = REF(RK_STR, i);
		bool asciz = s->esz == 1 && !nul;
		uint64_t m = asciz ? (n ? n - 1 : 0) : n;
		char *q;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		h = it_open(e, IT_TEXT);
		if (nul)
			tb_s(b, "\t.section\t.rodata,\"a\",@progbits\n");
		else
			tb_f(b, "\t.section\t.rodata.str%u.%u,\"aMS\",@progbits,%u\n", s->esz, s->esz, s->esz);
		if (s->esz > 1)
			tb_f(b, "\t.p2align\t%d\n", s->esz == 2 ? 1 : 2);
		it_close(e, h);
		h = it_open(e, IT_LABEL);
		memcpy(it_room(e, 4), &ref, 4);
		it_close(e, h);
		h = it_open(e, IT_BYTES);
		q = it_room(e, m + asciz);
		memcpy(q, s->s, m);
		if (asciz)
			q[m] = 0;
		it_close(e, h);
		out_chunk(e, ((uint64_t)1 << 62) | i, start);
		return;
	}
	if (nul)
		tb_s(b, "\t.section\t.rodata,\"a\",@progbits\n");
	else
		tb_f(b, "\t.section\t.rodata.str%u.%u,\"aMS\",@progbits,%u\n", s->esz, s->esz, s->esz);
	if (s->esz > 1)
		tb_f(b, "\t.p2align\t%d\n", s->esz == 2 ? 1 : 2);
	tb_s(b, ".L.str.");
	tb_u(b, i);
	tb_s(b, ":\n");
	if (s->esz == 1 && !nul) {
		tb_s(b, "\t.asciz\t\"");
		for (uint64_t k = 0; k + 1 < n; k++) {
			unsigned char ch = (unsigned char)s->s[k];

			if (ch == '"' || ch == '\\') {
				tb_c(b, '\\');
				tb_c(b, (char)ch);
			} else if (ch >= 32 && ch < 127) {
				tb_c(b, (char)ch);
			} else {
				tb_c(b, '\\');
				tb_c(b, (char)('0' + (ch >> 6)));
				tb_c(b, (char)('0' + ((ch >> 3) & 7)));
				tb_c(b, (char)('0' + (ch & 7)));
			}
		}
		tb_s(b, "\"\n");
	} else {
		for (uint64_t k = 0; k < n; k += s->esz) {
			uint32_t v = 0;

			memcpy(&v, s->s + k, s->esz);
			tb_s(b, s->esz == 1 ? "\t.byte\t" : s->esz == 2 ? "\t.short\t" : "\t.word\t");
			tb_u(b, v);
			tb_c(b, '\n');
		}
	}
	out_chunk(e, ((uint64_t)1 << 62) | i, start);
}

/* ---------------- prepass: what the emitter needs to know before it starts ---------------- */

typedef struct pre {
	emitter *E;
	cc *c;
	uint32_t fn;		/* the function being walked */
	uint32_t nodes;		/* nodes seen (inlining size) */
	int cdepth;		/* cleanup variables in scope */
} pre;

static void mark_written(pre *P, uint32_t n)
{
	cc *c = P->c;

	for (;;) {
		const node *x = N(n);

		if (x->op == N_MEMBER || (x->op == N_CAST && (x->aux == CK_NOP || x->aux == CK_DECAY))) {
			n = x->a;
		} else if (x->op == N_INDEX) {
			const node *b = N(x->a);

			if (b->op == N_CAST && b->aux == CK_DECAY)
				n = b->a;
			else
				return;
		} else {
			break;
		}
	}
	if (N(n)->op == N_VAR)
		P->E->sflags[N(n)->a] |= SA_WRITTEN;
	else if (N(n)->op == N_COMPLIT)
		P->E->sflags[N(n)->b] |= SA_WRITTEN;
}

/*
 * `v = CONST` keeps v constant as long as every assignment stores the same
 * value and the initializer agrees (clang -O2 propagates it: the kernel's
 * `bool enable_scs = false; ... if (x) enable_scs = false;` guards references
 * to symbols that do not exist).
 */
static void note_assign(pre *P, uint32_t s, uint32_t rhs)
{
	cc *c = P->c;
	emitter *E = P->E;
	cval cv;

	if (E->sflags[s] & SA_WRITTEN)
		return;
	if (!is_integer(c, c->syms[s].ty) || !cc_eval(c, rhs, &cv) || cv.kind != CV_INT) {
		E->sflags[s] |= SA_WRITTEN;
		return;
	}
	cv.i = trunc_to(c, cv.i, c->syms[s].ty);
	if (!(E->sflags[s] & SA_CWRITE)) {
		E->sflags[s] |= SA_CWRITE;
		E->cwval[s] = cv.i;
	} else if (E->cwval[s] != cv.i) {
		E->sflags[s] |= SA_CMULTI;
	}
}

/*
 * The prepass runs per function, the first time a function is emitted or
 * inlined: most of a TU's functions are header inlines nobody uses.  The
 * facts it records (writes to params and locals, labels) belong to that
 * function alone, so one walk per function is all that is needed.
 */
static int walk(pre *P, uint32_t n);

static void ensure_walked(emitter *E, cc *c, uint32_t s)
{
	uint8_t st = atomic_load_explicit(&E->walked[s], memory_order_acquire);

	if (likely(st == 2))
		return;
	st = 0;
	if (atomic_compare_exchange_strong(&E->walked[s], &st, 1)) {
		pre P = { E, c, s, 0, 0 };
		double t = E->stats ? now() : 0;

		walk(&P, c->syms[s].body);
		E->fnsize[s] = P.nodes > 65535 ? 65535 : (uint16_t)P.nodes;
		if (E->stats)
			E->walk_ns += (uint64_t)((now() - t) * 1e9), E->nwalk++;
		atomic_store_explicit(&E->walked[s], 2, memory_order_release);
		return;
	}
	while (atomic_load_explicit(&E->walked[s], memory_order_acquire) != 2)
		;	/* another thread is walking it: a few microseconds */
}

/* walks every node; returns whether it contains a label (or case) */
static int walk(pre *P, uint32_t n)
{
	cc *c = P->c;
	const node *x;
	int lab = 0;

	if (!n)
		return false;
	x = N(n);
	P->nodes++;
	switch (x->op) {
	case N_VAR:
		/* locals belong to this walk alone (globals' flags are shared between threads) */
		if (is_local_var(c, x->a))
			P->E->sflags[x->a] |= SA_USED;
		return false;
	case N_NUM: case N_FNUM: case N_STR: case N_ZERO: case N_SIZEOF_VLA:
		return false;
	case N_LABELADDR:
		P->E->lref[x->a] |= LR_REF | LR_BACK;
		return false;
	case N_MEMBER: case N_DEREF: case N_UNARY: case N_VAARG:
		lab = walk(P, x->a);
		break;
	case N_CAST:
		if (x->aux == CK_DECAY)
			mark_written(P, x->a);
		lab = walk(P, x->a);
		break;
	case N_ADDR:
		mark_written(P, x->a);
		lab = walk(P, x->a);
		break;
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		mark_written(P, x->a);
		lab = walk(P, x->a);
		break;
	case N_ASSIGN:
		if (!x->aux && N(x->a)->op == N_VAR)
			note_assign(P, N(x->a)->a, x->b);
		else
			mark_written(P, x->a);
		lab = walk(P, x->a) | walk(P, x->b);
		break;
	case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR: case N_COMMA:
		lab = walk(P, x->a) | walk(P, x->b);
		break;
	case N_COND:
		lab = walk(P, x->a) | walk(P, x->b) | walk(P, x->c);
		break;
	case N_CALL: {
		uint32_t ref, callee = direct_callee(c, x->a, &ref);

		if (callee && sym_attrs(c, callee)->error_fn)
			P->E->sflags[P->fn] |= SA_MUSTFOLD;
		lab = walk(P, x->a);
	}
		/* fall through */
	case N_BUILTIN:
		for (uint32_t i = 0; i < x->c; i++)
			lab |= walk(P, c->lists[x->b + i]);
		if (x->op == N_BUILTIN && (x->aux == BI_VA_START || x->aux == BI_VA_COPY) && x->c)
			mark_written(P, c->lists[x->b]);
		break;
	case N_STMTEXPR:
		lab = walk(P, x->a);
		break;
	case N_COMPLIT:
		lab = walk(P, x->a);
		break;
	case N_INIT:
		if (x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				lab |= walk(P, c->lists[x->b + 3 * i + 2]);
		} else {
			for (uint32_t i = 0; i < x->c; i++)
				lab |= walk(P, c->lists[x->b + i]);
		}
		break;
	case N_BLOCK: {
		int cd = P->cdepth;

		for (uint32_t i = 0; i < x->c; i++)
			lab |= walk(P, c->lists[x->b + i]);
		P->cdepth = cd;
		break;
	}
	case N_EXPR:
	case N_RETURN:
	case N_GOTO_IND:
		lab = walk(P, x->a);
		break;
	case N_DECL: {
		const sym *y = &c->syms[x->a];

		lab = walk(P, x->b);
		if (sym_attrs(c, x->a)->cleanup || (y->kind == S_VAR && (T(c, y->ty)->flags & TF_VLA)))
			P->cdepth++;
		/* a __used static local is emitted with its function, referenced or not */
		if (y->kind == S_VAR && y->sc == SC_STATIC && sym_attrs(c, x->a)->used &&
		    !atomic_load_explicit(&P->E->claimed[x->a], memory_order_relaxed) &&
		    !atomic_exchange(&P->E->claimed[x->a], 1))
			enqueue(P->E, x->a);
		break;
	}
	case N_IF:
		lab = walk(P, x->a) | walk(P, x->b) | walk(P, x->c);
		break;
	/* in the order the code is generated: LR_BACK means a jump back */
	case N_WHILE: case N_SWITCH:
		lab = walk(P, x->a) | walk(P, x->b);
		break;
	case N_DO:
		lab = walk(P, x->b);
		lab |= walk(P, x->a);
		break;
	case N_FOR: {
		int cd = P->cdepth;

		/* the init's declarations live in the for statement (gen does not open a block for them) */
		if (x->a && N(x->a)->op == N_BLOCK) {
			for (uint32_t i = 0; i < N(x->a)->c; i++)
				lab |= walk(P, c->lists[N(x->a)->b + i]);
		} else {
			lab |= walk(P, x->a);
		}
		lab |= walk(P, x->b);
		lab |= walk(P, c->lists[x->c + 1]);
		lab |= walk(P, c->lists[x->c]);
		P->cdepth = cd;
		break;
	}
	case N_CASE: case N_DEFAULT:
		walk(P, x->c);
		lab = 1;
		break;
	case N_LABEL:
		P->E->lcdepth[x->a] = (uint16_t)P->cdepth;
		P->E->lref[x->a] |= LR_DEF;
		if (c->labels[x->a].addr_taken)
			P->E->lref[x->a] |= LR_REF | LR_BACK;
		walk(P, x->c);
		lab = 1;
		break;
	case N_GOTO:
		P->E->lref[x->a] |= LR_REF | (P->E->lref[x->a] & LR_DEF ? LR_BACK : 0);
		break;
	case N_ASM: {
		const casm *a = &c->asms[x->a];

		for (uint32_t i = 0; i < a->nout + a->nin; i++) {
			if (i < a->nout)
				mark_written(P, a->ops[i].expr);
			else if (strpbrk(c->strs[a->ops[i].constraint - 1].s, "mQo"))
				mark_written(P, a->ops[i].expr);
			lab |= walk(P, a->ops[i].expr);
		}
		for (uint32_t k = 0; k < a->nlabel; k++)
			P->E->lref[a->labels[k]] |= LR_REF | (P->E->lref[a->labels[k]] & LR_DEF ? LR_BACK : 0);
		break;
	}
	default:
		break;
	}
	if (lab)
		P->E->nflags[n] |= NA_LABEL;
	return lab;
}

static int u32_cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* ---------------- the work queue and threads ---------------- */

static void enqueue(emitter *E, uint32_t item)
{
	pthread_mutex_lock(&E->mu);
	E->q[E->qt++] = item;
	pthread_cond_signal(&E->cv);
	pthread_mutex_unlock(&E->mu);
}

static void ecx_init(ecx *e, emitter *E)
{
	cc *c = E->c;

	memset(e, 0, sizeof(*e));
	e->E = E;
	e->c = c;
	e->capin = 1u << 24;
	e->in = reserve((size_t)e->capin * sizeof(ins));
	tb_init(&e->raw, (size_t)1 << 28);
	tb_init(&e->out, (size_t)1 << 32);
	tb_init(&e->data, (size_t)1 << 28);
	e->rel = reserve((size_t)1 << 26);
	e->b = reserve((size_t)c->nsym * sizeof(bind) + 4096);
	e->lm = reserve((size_t)c->nlabel * sizeof(lbl) + 4096);
	e->caselbl = reserve((size_t)c->nnode * sizeof(uint32_t) + 4096);
	e->caplabel = 1u << 22;
	e->lrefc = reserve((size_t)e->caplabel * sizeof(uint32_t));
	e->lkvi = reserve((size_t)e->caplabel * sizeof(uint32_t));
	e->kvs = reserve((size_t)CAPKVS * sizeof(kvst));
	e->cases = reserve((size_t)1 << 24);
	e->weakref = reserve((size_t)c->nsym * sizeof(uint32_t) + 4096);
}

static void ecx_free(ecx *e)
{
	cc *c = e->c;

	munmap(e->in, (size_t)e->capin * sizeof(ins));
	munmap(e->weakref, (size_t)c->nsym * sizeof(uint32_t) + 4096);
	munmap(e->raw.p, e->raw.cap);
	munmap(e->data.p, e->data.cap);
	munmap(e->rel, (size_t)1 << 26);
	munmap(e->b, (size_t)c->nsym * sizeof(bind) + 4096);
	munmap(e->lm, (size_t)c->nlabel * sizeof(lbl) + 4096);
	munmap(e->caselbl, (size_t)c->nnode * sizeof(uint32_t) + 4096);
	munmap(e->lrefc, (size_t)e->caplabel * sizeof(uint32_t));
	munmap(e->lkvi, (size_t)e->caplabel * sizeof(uint32_t));
	munmap(e->kvs, (size_t)CAPKVS * sizeof(kvst));
	munmap(e->cases, (size_t)1 << 24);
	/* e->out stays: the chunks point into it (unmapped by emit_tu) */
}

static void do_item(ecx *e, uint32_t it)
{
	cc *c = e->c;

	if (it & 0x80000000u) {
		emit_string(e, it & 0x7fffffffu);
		return;
	}
	if (c->syms[it].kind == S_FUNC)
		emit_function(e, it);
	else
		emit_object(e, it);
}

static void *worker(void *arg)
{
	ecx *e = arg;
	emitter *E = e->E;

	pthread_mutex_lock(&E->mu);
	for (;;) {
		uint32_t it;

		while (E->qh == E->qt && E->busy > 0)
			pthread_cond_wait(&E->cv, &E->mu);
		if (E->qh == E->qt)
			break;
		it = E->q[E->qh++];
		E->busy++;
		pthread_mutex_unlock(&E->mu);
		do_item(e, it);
		pthread_mutex_lock(&E->mu);
		E->busy--;
		if (E->busy == 0 && E->qh == E->qt)
			pthread_cond_broadcast(&E->cv);
	}
	pthread_cond_broadcast(&E->cv);
	pthread_mutex_unlock(&E->mu);
	return NULL;
}

static int chunk_cmp(const void *a, const void *b)
{
	const chunk *x = a, *y = b;

	return x->key < y->key ? -1 : x->key > y->key;
}

static bool is_root(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	const attrs *a = sym_attrs(c, s);

	if (y->depth != 0 && !(y->kind == S_VAR && y->sc == SC_STATIC && a->used))
		return false;
	if (a->alias)
		return false;	/* .set in the tail; its target is a root (emit_tu) */
	if (y->kind == S_FUNC) {
		if (!(y->flags & SF_DEFINED) || !y->body)
			return false;
		if ((y->flags & SF_GNU_INLINE) && !(y->flags & SF_INLINE_NOEXTERN))
			return false;	/* GNU 'extern inline': clang's isInlineDefinitionExternallyVisible */
		if (y->sc == SC_STATIC)
			return a->used || a->constructor;
		if ((y->flags & SF_INLINE) && !(y->flags & SF_GNU_INLINE) && y->sc != SC_EXTERN)
			return false;	/* C99 inline definition */
		return true;
	}
	if (y->kind == S_VAR) {
		if (!(y->flags & (SF_DEFINED | SF_TENTATIVE)))
			return false;
		if (!y->name || y->sc == SC_REGISTER)
			return false;	/* global register variables have no storage */
		if (y->sc == SC_STATIC)
			return a->used;
		return true;
	}
	return false;
}

__attribute__((format(printf, 2, 3)))
static void as_error(cc *c, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	pp_vdiag(c->p, 0, "error", fmt, ap);
	va_end(ap);
}

/* the object backend: header text, chunks (items), tail text */
static int replay(cc *c, const cc_opts *o, const char *hp, size_t hn, const char *tp, size_t tn, const chunk *ch, uint32_t nch)
{
	static _Thread_local uint32_t *loff, *cache;
	static _Thread_local size_t ncache;
	size_t need = (size_t)c->nsym + c->nstr + c->nlabel + 192;
	rctx R = { .c = c, .as = o->as, .o = o->obj };
	int r = 0;

	static _Thread_local tbuf nb;

	if (!loff) {
		loff = reserve((size_t)1 << 32);	/* labels of one function */
		tb_init(&nb, 1 << 20);
	}
	R.nb = nb;
	if (need > ncache) {
		if (cache)
			munmap(cache, ncache * sizeof(uint32_t));
		ncache = need * 2;
		cache = reserve(ncache * sizeof(uint32_t));
	} else {
		memset(cache, 0, need * sizeof(uint32_t));
	}
	R.symc = cache;
	R.strc = cache + c->nsym + 64;
	R.lblc = R.strc + c->nstr + 64;
	R.loff = loff;
	as_begin(R.as, R.o, "<vcc>");
	if (as_snippet(R.as, hp, hn))
		r = -1;
	for (uint32_t i = 0; !r && i < nch; i++)
		r = r_chunk(&R, ch[i].p, ch[i].n);
	if (!r && (as_snippet(R.as, tp, tn) || as_end(R.as)))
		r = -1;
	if (r || R.o->has_err || as_failed(R.as)) {
		as_error(c, "assembler: %s", as_failed(R.as) ? as_msg(R.as) : R.o->err);
		return -1;
	}
	return 0;
}

static int emit_tu1(cc *c, const cc_opts *o, bool write_out, uint8_t *mf, bool *again, bool last)
{
	emitter E;
	int nthr = o->nthreads > 0 ? o->nthreads : 1;
	ecx *ctx;
	pthread_t th[64];
	double t0 = now(), t1, t2;
	tbuf hdr;
	int errors;

	memset(&E, 0, sizeof(E));
	E.c = c;
	E.o = o;
	E.mf = mf;
	E.claimed = reserve(c->nsym + 64);
	E.extref = reserve(c->nsym + 64);
	E.sflags = reserve(c->nsym + 64);
	E.strdone = reserve(c->nstr + 64);
	E.nflags = reserve(c->nnode + 64);
	E.lref = reserve(c->nlabel + 64);
	E.lcdepth = reserve((size_t)c->nlabel * 2 + 64);
	E.q = reserve(((size_t)c->nsym + c->nstr + 64) * sizeof(uint32_t));
	E.chunks = reserve(((size_t)c->nsym + c->nstr + c->ntlasm + 64) * sizeof(chunk));
	pthread_mutex_init(&E.mu, NULL);
	pthread_cond_init(&E.cv, NULL);
	pthread_mutex_init(&E.cmu, NULL);
	pthread_mutex_init(&E.emu, NULL);
	E.walked = reserve(c->nsym + 64);
	E.fnsize = reserve((c->nsym + 64) * sizeof(uint16_t));
	E.cwval = reserve((c->nsym + 64) * sizeof(i128));
	E.stats = getenv("VCC_EMIT_STATS") != NULL;
	E.objtext = getenv("VCC_OBJ_TEXT") != NULL;
	/* roots */
	for (uint32_t i = 0; i < c->ntoplevel; i++) {
		uint32_t s = c->toplevel[i];

		if (is_root(c, s) && !E.claimed[s]) {
			E.claimed[s] = 1;
			E.q[E.qt++] = s;
		}
		if (unlikely(sym_attrs(c, s)->alias)) {
			/* an alias keeps its target, static or not */
			uint32_t t = cc_global_named(c, c->strs[sym_attrs(c, s)->alias - 1].s);

			if (t && !E.claimed[t] && (c->syms[t].flags & (SF_DEFINED | SF_TENTATIVE)) &&
			    (c->syms[t].kind == S_FUNC ? c->syms[t].body != 0 : true)) {
				E.claimed[t] = 1;
				E.q[E.qt++] = t;
			}
		}
	}
	t1 = now();
	if (nthr > 64)
		nthr = 64;
	ctx = calloc((size_t)nthr, sizeof(ecx));
	for (int i = 0; i < nthr; i++)
		ecx_init(&ctx[i], &E);
	for (int i = 1; i < nthr; i++)
		pthread_create(&th[i], NULL, worker, &ctx[i]);
	worker(&ctx[0]);
	for (int i = 1; i < nthr; i++)
		pthread_join(th[i], NULL);
	t2 = now();
	/* stitch */
	qsort(E.chunks, E.nchunk, sizeof(chunk), chunk_cmp);
	tb_init(&hdr, (size_t)1 << 26);
	tb_s(&hdr, "\t.text\n");
	for (uint32_t i = 0; i < c->ntlasm; i++) {
		const casm *a = &c->asms[N(c->tlasm[i])->a];

		tb_s(&hdr, c->strs[a->tmpl - 1].s);
		tb_s(&hdr, "\n\t.text\n");
	}
	{
		tbuf tail;
		struct iovec *iov = malloc(((size_t)E.nchunk + 2) * sizeof(struct iovec));
		int k = 0;

		tb_init(&tail, (size_t)1 << 26);
		{
			/* .weak for weak undefined syms, in symbol order whatever the threads did */
			uint32_t nw = 0, *w;

			for (int t = 0; t < nthr; t++)
				nw += ctx[t].nweakref;
			w = malloc((nw + 1) * sizeof(uint32_t));
			nw = 0;
			for (int t = 0; t < nthr; t++)
				for (uint32_t j = 0; j < ctx[t].nweakref; j++)
					w[nw++] = ctx[t].weakref[j];
			qsort(w, nw, sizeof(uint32_t), u32_cmp);
			for (uint32_t j = 0; j < nw; j++)
				if (!E.claimed[w[j]] && c->syms[w[j]].name && !sym_attrs(c, w[j])->alias) {
					tb_s(&tail, "\t.weak\t");
					sym_name(c, &tail, w[j]);
					tb_c(&tail, '\n');
				}
			free(w);
		}
		for (uint32_t ti = 0; ti < c->ntoplevel; ti++) {
			uint32_t s = c->toplevel[ti];
			const sym *y = &c->syms[s];

			/* aliases */
			if (y->depth == 0 && sym_attrs(c, s)->alias && y->name) {
				const attrs *a = sym_attrs(c, s);

				tb_s(&tail, (y->flags & SF_WEAK) ? "\t.weak\t" : y->sc == SC_STATIC ? "" : "\t.globl\t");
				if (y->sc != SC_STATIC || (y->flags & SF_WEAK)) {
					sym_name(c, &tail, s);
					tb_c(&tail, '\n');
				}
				tb_s(&tail, "\t.type\t");
				sym_name(c, &tail, s);
				tb_s(&tail, y->kind == S_FUNC ? ",@function\n" : ",@object\n");
				tb_s(&tail, "\t.set\t");
				sym_name(c, &tail, s);
				tb_s(&tail, ", ");
				tb_s(&tail, c->strs[a->alias - 1].s);
				tb_c(&tail, '\n');
			}
		}
		tb_s(&tail, "\t.section\t\".note.GNU-stack\",\"\",@progbits\n");
		iov[k++] = (struct iovec){ hdr.p, hdr.n };
		for (uint32_t i = 0; i < E.nchunk; i++)
			iov[k++] = (struct iovec){ (void *)E.chunks[i].p, E.chunks[i].n };
		iov[k++] = (struct iovec){ tail.p, tail.n };
		*again = E.newmf != 0 && !last;
		if (!E.errors && write_out && !*again && o->emit == 2) {
			if (E.objtext) {
				/* the text, gathered once, through the assembler */
				static _Thread_local tbuf all;
				const char *msg;

				if (!all.p)
					tb_init(&all, (size_t)1 << 32);
				all.n = 0;
				for (int i = 0; i < k; i++)
					tb_put(&all, iov[i].iov_base, iov[i].iov_len);
				if (as_run(o->as, o->obj, all.p, all.n, "<vcc>", &msg)) {
					as_error(c, "assembler: %s", msg);
					E.errors++;
				}
			} else if (replay(c, o, hdr.p, hdr.n, tail.p, tail.n, E.chunks, E.nchunk)) {
				/* object: the items replayed into the TU's assembler and ElfObj */
				E.errors++;
			}
		} else if (!E.errors && write_out && !*again) {
			for (int i = 0; i < k;) {
				int m = k - i > 1024 ? 1024 : k - i;
				ssize_t w = writev(o->outfd, iov + i, m);

				if (w < 0) {
					perror("vcc: write");
					E.errors++;
					break;
				}
				/* partial writes: finish the rest one by one */
				{
					size_t want = 0;

					for (int j = i; j < i + m; j++)
						want += iov[j].iov_len;
					if ((size_t)w < want) {
						size_t done = (size_t)w;

						for (int j = i; j < i + m; j++) {
							size_t l = iov[j].iov_len;

							if (done >= l) {
								done -= l;
								continue;
							}
							if (write(o->outfd, (char *)iov[j].iov_base + done, l - done) < 0)
								break;
							done = 0;
						}
					}
				}
				i += m;
			}
		}
		free(iov);
		if (getenv("VCC_EMIT_STATS")) {
			size_t bytes = hdr.n + tail.n;

			for (uint32_t i = 0; i < E.nchunk; i++)
				bytes += E.chunks[i].n;
			fprintf(stderr, "vcc emit: %llu functions, %llu objects, %llu inlined, %llu instructions (%.1f KB code); "
				"setup %.2f ms, emit %.2f ms (%d threads; %.2f ms printing, %.2f ms prepass over %llu functions), %.1f MB text; %.0f MB/s machine code\n",
				(unsigned long long)E.nfn, (unsigned long long)E.nobj, (unsigned long long)E.ninl,
				(unsigned long long)E.nins, E.nins * 4 / 1024.0, (t1 - t0) * 1e3, (t2 - t1) * 1e3, nthr,
				E.print_ns / 1e6, E.walk_ns / 1e6, (unsigned long long)E.nwalk, bytes / 1e6, E.nins * 4 / 1e6 / (t2 - t1));
		}
		munmap(tail.p, tail.cap);
	}
	munmap(hdr.p, hdr.cap);
	for (int i = 0; i < nthr; i++) {
		munmap(ctx[i].out.p, ctx[i].out.cap);
		ecx_free(&ctx[i]);
	}
	free(ctx);
	errors = E.errors;
	munmap(E.claimed, c->nsym + 64);
	munmap(E.extref, c->nsym + 64);
	munmap(E.sflags, c->nsym + 64);
	munmap(E.strdone, c->nstr + 64);
	munmap(E.nflags, c->nnode + 64);
	munmap(E.lref, c->nlabel + 64);
	munmap((void *)E.walked, c->nsym + 64);
	munmap(E.fnsize, (c->nsym + 64) * sizeof(uint16_t));
	munmap(E.cwval, (c->nsym + 64) * sizeof(i128));
	munmap(E.lcdepth, (size_t)c->nlabel * 2 + 64);
	munmap(E.q, ((size_t)c->nsym + c->nstr + 64) * sizeof(uint32_t));
	munmap(E.chunks, ((size_t)c->nsym + c->nstr + c->ntlasm + 64) * sizeof(chunk));
	return errors;
}

/*
 * VCC_EMIT_REPEAT=N: generate N times over the same AST (profiling), write once.
 * A round that marks functions for inlining (need_fold) is generated again.
 */
extern _Atomic unsigned long long ppd_ns_emit;

int emit_tu(cc *c, const cc_opts *o)
{
	double t0 = now();
	const char *r = getenv("VCC_EMIT_REPEAT");
	int n = r ? atoi(r) : 1, err = 0;
	uint8_t *mf = reserve(c->nsym + 64);
	bool again = false;

	for (int i = 1; i < n; i++)
		if (emit_tu1(c, o, false, mf, &again, false))
			break;
	for (int round = 0; round < 8; round++) {
		err = emit_tu1(c, o, true, mf, &again, round == 7);
		if (err || !again)
			break;
	}
	munmap(mf, c->nsym + 64);
	ppd_ns_emit += (unsigned long long)((now() - t0) * 1e9);
	return err;
}
