#ifndef VCC_EMIT_EMITIMPL_H
#define VCC_EMIT_EMITIMPL_H
/* the files of vcc/emit/ share this header: what one of them defines and another uses */
/*
 * vcc code generator: arm64 (AAPCS64, Linux ELF) straight from cc/'s AST.
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
 *   - from -O1 on (cc_opts.opt), the three items below; at -O0 locals
 *     and inlined params and results live in stack slots;
 *   - the emitted function's most used scalar locals and params whose
 *     address is never needed (prepass: luse, lnoreg) live in callee-saved
 *     registers x28 down (B_LREG, L_VREG; VCC_LREGS, default 6), the rest
 *     in stack slots; expression temporaries in x9-x15 and the remaining
 *     x19-x28.  An inlined call's params that its body never writes keep
 *     the argument's register (B_REG), and its result comes back in a
 *     register (fnctx.retreg), not through slots.  Out of temporaries, a
 *     function is generated again without either;
 *   - a b to a label right after it is dropped, and opt.c removes the
 *     register copies this leaves (liveness, copy propagation).
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

#include "../ccint.h"
#include "../emit.h"
#include "../as.h"
#include "../elf.h"
#include "../a64.h"
#include "../ppdriver.h"
#include "../arena.h"

/* functions one file defines and another calls: static when the directory is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

#define N(i) (&c->nodes[i])
/* Apple arm64 (Mach-O): '_' names, GOT for symbols outside the TU, variadic
 * arguments on the stack, va_list a char *, stack arguments packed */
#define DARWIN(c) ((c)->o->target == PP_DARWIN)
#define T(c, t) CT(c, t)
#define KIND(c, t) CKIND(c, t)

/* ---------------- instruction records ---------------- */

/* registers: 0-30 x0-x30, ZR, SP, FRAME (pseudo base of stack slots) */
enum { R_FP = 29, R_LR = 30, ZR = 31, SP = 32, FRAME = 33, R_X16 = 16, R_X17 = 17 };
#define R_BP 28		/* frame base of a realigned dynamic frame (ecx.falign, finish_function) */

enum {
	I_LABEL,	/* label */
	I_RAW,		/* off: text pool offset, ref: length; cond 1: inline asm (//APP in -S), RAW_MASKS: with its registers */
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

/* I_RAW cond 2: inline asm whose registers follow its text: uint64_t reads, writes */
#define RAW_MASKS 2

/* lrefc: a label that inline asm (asm goto) or a label address (&&l) may reach */
#define LREF_EXT (1u << 31)

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

/* ---------------- emitter state ---------------- */

typedef struct chunk {
	uint64_t key;		/* output order */
	const char *p;
	size_t n;
	const uint32_t *log;	/* the item's log (ecx.rlog): what a later round needs to reuse it */
	uint32_t nlog;
} chunk;

/*
 * A round that marks functions for inlining (need_fold) is followed by
 * another; a function whose generation never asked about a function marked
 * since then comes out the same, so the next round reuses its chunk.  The
 * log says what its generation did outside the chunk: references (claims of
 * statics and strings, weak undefined syms), prepass walks (which queue
 * __used static locals), and the static functions whose mark it read.
 */
enum { RL_REF, RL_DEP, RL_WALK };
typedef struct kent {
	uint64_t key;
	const char *p;
	size_t n;
	const uint32_t *log;
	uint32_t nlog;
} kent;
typedef struct keep {
	uint32_t *idx;		/* per sym: kent index + 1 */
	kent *ent;
	char *bytes;
	uint32_t *logs;
	size_t nidx, nent, nbytes, nlogs;
	uint32_t asmno;		/* inline asm %= numbers go on from here */
} keep;

/* analysis bits (prepass, read-only while emitting) */
enum { FF_SEEN = 1, FF_NONNEG = 2, FF_ZERO = 4, FF_BAD = 8 };
enum { SA_WRITTEN = 1, SA_CLEANUP_FN = 2, SA_MUSTFOLD = 4, SA_CWRITE = 8, SA_CMULTI = 16, SA_USED = 32, SA_NOINL = 64,
       SA_AESC = 128 /* a local array used other than as a[CONST] */ };
/* USED: a local or param read; NOINL: a static local holds a label address (one copy of the code only) */
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
	uint16_t *luse;			/* per local: uses weighted by loop depth (prepass) */
	uint8_t *lnoreg;		/* per local: needs memory (address taken, asm operand, {} init) */
	uint32_t *lspan;		/* per function: [2s] lowest, [2s+1] highest local sym seen */
	int nlreg;			/* locals given callee-saved registers per function (VCC_LREGS) */
	bool noopt;			/* VCC_NOOPT: no opt.c clean-ups (to compare) */
	i128 *cwval;		/* per SA_CWRITE variable: the one constant it is assigned */	/* per function: prepass state (0 no, 1 running, 2 done) */
	uint8_t *sflags;		/* SA_* per sym */
	uint8_t *ffacts;		/* FF_* per floating local: what every write stores */
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
	const keep *K;			/* the previous round's functions, or NULL */
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
enum { B_NONE, B_SLOT, B_CONST, B_INDIRECT, B_REG, B_LREG };
/* B_REG: an inlined call's read-only param in temporary v.r; B_LREG: a local or param of the
   emitted function that lives in callee-saved register v.r for the whole function */

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
	uint8_t retreg;		/* inlined: the result's temporary + 1 (0: in retslot) */
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
#define MAXLREG 10	/* x19-x28 */
#define LREGS_DEFAULT 6	/* the rest stay expression temporaries */
#define INLINE_FREE_FAST 5
#define INLINE_FREE_SAFE 8

/* a stack slot handed out by slot() (offsets are reused across scopes) */
typedef struct slotal {
	int32_t off;
	uint32_t size;
} slotal;
#define CAPSLA (1u << 20)
/*
 * At -O1 and up a record addressing a frame slot (I_LD/I_ST, add/sub
 * immediate with rn FRAME) names the live allocation it is inside in
 * rm | ra << 8 (index + 1; 0: none found, or past 65535).  Slots are
 * reused once a scope ends, so this tells opt.c which accesses belong to
 * one object.  An address at the start of an allocation that is also just
 * past the end of the live one below names that one too, in cond | sh << 8
 * (SLOT_TAG2).  finish_function clears the tags before the frame layout.
 */
#define SLOT_TAG(i) ((uint32_t)(i)->rm | (uint32_t)(i)->ra << 8)
#define SLOT_TAG2(i) ((uint32_t)(i)->cond | (uint32_t)(i)->sh << 8)

#define NVSZ 64		/* VLA size expressions live at once in a function */

typedef struct ecx {
	emitter *E;
	cc *c;
	/* records of the function being emitted */
	ins *in;
	uint32_t nin, capin;
	slotal *sla;		/* the function's slot() allocations */
	uint32_t nsla;
	uint32_t *sstk, nsstk;	/* the live ones (indices into sla, deepest last) */
	int novtag;		/* inside a volatile access: its records stay untagged */
	uint32_t curloc;	/* innermost statement being generated (diagnostics) */
	tbuf raw;		/* inline asm text */
	tbuf out;		/* this thread's output text */
	tbuf data;		/* scratch: data object bytes */
	struct reloc { uint64_t off; uint32_t ref, seq; int64_t add; uint32_t sub, sz; } *rel;	/* sub: ref subtracted (0: none); sz 4 or 8 (0: 8) */
	uint32_t nrel;
	uint64_t relend;	/* end of the highest relocated word */
	/* bindings */
	bind *b;		/* per sym */
	lbl *lm;		/* per label */
	uint32_t *caselbl;	/* per node (N_CASE / N_DEFAULT): local label */
	uint32_t *lrefc;	/* per local label: branches to it; LREF_EXT: also reached from outside the records */
	uint32_t nlabel, caplabel;
	/*
	 * Known values of locals only ever assigned constants (SA_CWRITE):
	 * forward edges carry them to their label, which keeps what all
	 * incoming edges agree on; loop heads and unknown targets drop them.
	 */
	uint8_t nkv;
	uint16_t kvalid;
	int64_t kval[NKV];
	/* elements a[CONST] of local arrays that do not escape (no SA_AESC), tracked like those locals */
	uint8_t nael;
	struct { uint32_t s; uint32_t idx; uint8_t kv; } ael[NKV];
	uint32_t *lkvi;		/* per local label: kvst index + 1 (0: none) */
	kvst *kvs;
	uint32_t nkvs;
	uint32_t inst_next;
	/* frame */
	int32_t foff, fmax;
	uint32_t falign;	/* largest slot alignment; over 16 the prologue realigns sp */
	/* VLA sizes, evaluated where their declaration is reached (stmt.c vla_fix) */
	struct { uint32_t node, inst; int32_t slot; } vsz[NVSZ];
	uint32_t nvsz;
	int32_t cl_top;		/* frame offset above the block's compound literals (kept past statements) */
	int32_t outmax;		/* outgoing stack argument bytes */
	bool dyn, calls, vararg;
	int32_t vaslot;		/* variadic register save area */
	int ngr_named, nstk_named;	/* variadic: named GPR args, named stack bytes */
	int nvr_named, vrslot;		/* variadic (Linux): named FP/SIMD args, the v0-v7 save area */
	int nsrn;		/* classify: next SIMD/FP argument register */
	/* temporaries */
	uint32_t live;		/* bit i: tempregs[i] in use */
	uint32_t resv;		/* ... of those, never handed out: B_LREG variables', x28 once falign > 16 */
	int inlfree;		/* free temporaries an inlined call keeps (0: INLINE_FREE_SAFE) */
	int nlr;		/* B_LREG candidates of the emitted function: sym, register */
	uint32_t lrsym[MAXLREG];
	uint8_t lrreg[MAXLREG];
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
	uint32_t *rlog;		/* this round's item logs: (RL_*, value) pairs */
	size_t nrlog, rlog0;	/* ... and where the current item's starts */
	jmp_buf bail;
} ecx;
enum { FN_MEMCPY, FN_MEMSET, FN_MEMMOVE, FN_UDIVTI3, FN_DIVTI3, FN_UMODTI3, FN_MODTI3, FN_STRLEN,
       FN_ASHLTI3, FN_LSHRTI3, FN_ASHRTI3, FN_FLOATTISF, FN_FLOATTIDF, FN_FLOATUNTISF, FN_FLOATUNTIDF,
       FN_FIXSFTI, FN_FIXDFTI, FN_FIXUNSSFTI, FN_FIXUNSDFTI,
       FN_ADDTF3, FN_SUBTF3, FN_MULTF3, FN_DIVTF3, FN_EQTF2, FN_NETF2, FN_LTTF2, FN_LETF2, FN_GTTF2, FN_GETF2,
       FN_EXTENDDFTF2, FN_EXTENDSFTF2, FN_TRUNCTFDF2, FN_TRUNCTFSF2, FN_FLOATSITF, FN_FLOATDITF, FN_FLOATUNSITF,
       FN_FLOATUNDITF, FN_FLOATTITF, FN_FLOATUNTITF, FN_FIXTFSI, FN_FIXTFDI, FN_FIXUNSTFSI, FN_FIXUNSTFDI,
       FN_FIXTFTI, FN_FIXUNSTFTI };
#define FIXED(k) REF(RK_NAME, 0x8000000u + (k))

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
enum { IT_TEXT = 1, IT_CODE, IT_DATA, IT_LABEL, IT_BYTES, IT_SYMHDR, IT_FEND, IT_OSIZE };
typedef struct item { uint32_t kind, len; } item;	/* len payload bytes follow, padded to 8 */
typedef struct code_hdr { uint32_t fid, nin, nraw, nlabel, sym_labels, pad; } code_hdr;
typedef struct data_hdr { uint64_t size, n; uint32_t nrel, zero; } data_hdr;
typedef struct ireloc { uint64_t off; uint32_t ref, seq; int64_t add; uint32_t sub, sz; } ireloc;
/*
 * A symbol's header after its section directive, what the assembler does
 * for ".globl/.weak s; .hidden/.protected s; .p2align p2; .type s,@function
 * or @object; s:" (IT_SYMHDR), for ".Lfe<s>: .size s, .Lfe<s>-s" (IT_FEND)
 * and ".size s, n" (IT_OSIZE), without the text.
 */
typedef struct symhdr { uint32_t sym; uint8_t bind, vis, p2, fn; } symhdr;	/* bind: 0, STB_GLOBAL, STB_WEAK */
typedef struct osize { uint32_t sym, pad; uint64_t size; } osize;

/* replay state: one per TU */
typedef struct rctx {
	cc *c;
	As *as;
	ElfObj *o;
	uint32_t *symc, *strc, *lblc;	/* ref -> ElfSym (0: not yet) */
	uint32_t *loff;			/* direct labels: word offset in the function */
	uint32_t *wb;			/* direct functions: the words, before they go to the frag */
	struct rpatch *pt;		/* ... and the records with a fixup */
	uint32_t *lsym, *lser, ser;	/* functions with inline asm: label -> .LBB symbol (valid if lser == ser) */
	struct rlab *lab;		/* ... label state */
	struct rpend *pend;		/* ... branches waiting for their label */
	tbuf nb;			/* name scratch */
} rctx;

/* a pending fixup on word k of an instruction */
typedef struct pfix { uint8_t k, lbl; uint32_t type, ref; int64_t off; } pfix;
typedef struct rpatch { uint32_t k, at; pfix f; uint8_t lng; } rpatch;	/* record k at word at; lng: relaxed (r_relax) */
typedef struct rlab { uint32_t ser, flags, frag, off, pend; } rlab;
typedef struct rpend { uint32_t next, k, frag, off; uint8_t *p; } rpend;	/* a branch waiting for its label */

/* ---------------- lvalues ---------------- */

enum { L_FRAME, L_SYM, L_REG, L_GLOBREG, L_VREG };	/* L_VREG: a B_LREG variable, value in r (no address) */

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

enum { A_IGNORE, A_REG, A_STACK, A_REF_REG, A_REF_STACK, A_VREG, A_HFA };	/* A_VREG: float/double in v<reg>; A_HFA: nreg members of fsz bytes in v<reg>.. */

typedef struct abiarg {
	uint8_t kind, reg, nreg, fsz;
	int32_t stk;		/* A_STACK / A_REF_STACK: offset in the argument area */
	uint32_t size;
} abiarg;

/* ---------------- spilling caller-saved temporaries around calls ---------------- */

/* ins.sh of the I_ST/I_LD pair spill_live/unspill put around a call (opt.c drops dead ones) */
#define SPILL_REC 1

typedef struct spill {
	uint32_t mask;
	int32_t slot[NTEMP];
} spill;

#define RLOG_CAP ((size_t)1 << 28)

/* ---------------- calls ---------------- */

typedef struct carg {
	val v;
	qty t;
	abiarg a;
	int32_t slot;		/* value parked in a frame slot (-1: v is a constant/address) */
} carg;

/* ---------------- across files ---------------- */

/* tu.c */
VPRIV void *reserve(size_t n);
VPRIV double now(void);
/* type.c */
static inline qty prim(int k);
static inline bool k_int(int k);
static inline bool is_integer(cc *c, qty t);
static inline bool is_ptr(cc *c, qty t);
static inline bool is_flt(cc *c, qty t);
static inline bool is_record(cc *c, qty t);
static inline bool is_array(cc *c, qty t);
static inline bool is_func(cc *c, qty t);
static inline bool is_void(cc *c, qty t);
static inline bool is_vec(cc *c, qty t);
static inline bool is_recabi(cc *c, qty t);
static inline bool is_agg(cc *c, qty t);
static inline uint64_t tsize(cc *c, qty t);
static inline uint32_t talign(cc *c, qty t);
static inline bool is_signed(cc *c, qty t);
static inline bool is_ld(cc *c, qty t);
static inline bool is_wide(cc *c, qty t);
VPRIV i128 em_trunc_to(cc *c, i128 v, qty t);
/* insn.c */
extern const char cond_name[][8];
VPRIV void tb_init(tbuf *b, size_t cap);
VPRIV void tb_reg(tbuf *b, vmreg *r, size_t cap, const char *name);
static inline void tb_need(tbuf *b, size_t n);
static inline void tb_put(tbuf *b, const char *s, size_t n);
static inline void tb_c(tbuf *b, char ch);
static inline void tb_s(tbuf *b, const char *s);
VPRIV void tb_u_slow(tbuf *b, uint64_t v);
static inline void tb_u(tbuf *b, uint64_t v);
static inline void tb_x(tbuf *b, uint32_t v);
static inline void tb_n8(tbuf *b, const char s[8]);
VPRIV void tb_i(tbuf *b, int64_t v);
VPRIV void tb_hex(tbuf *b, uint64_t v);
VPRIV void tb_f(tbuf *b, const char *fmt, ...);
VPRIV bool logimm_ok(uint64_t v, int x);
static inline void reg(tbuf *b, int r, int x);
/* tu.c */
extern const uint8_t tempregs[NTEMP];

/* the tempregs index of x<r>, or -1 */
static inline int tidx(int r)
{
	static const int8_t tidx_of[34] = {
		-1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, -1, -1, -1,
		7, 8, 9, 10, 11, 12, 13, 14, 15, 16, -1, -1, -1, -1, -1,
	};

	return (unsigned)r < 34 ? tidx_of[r] : -1;
}
VPRIV __attribute__((format(printf, 3, 4))) void ediag(ecx *e, uint32_t loc, const char *fmt, ...);
VPRIV __attribute__((format(printf, 3, 4), noreturn)) void efatal(ecx *e, uint32_t loc, const char *fmt, ...);
/* type.c */
VPRIV uint32_t canon_sym(cc *c, uint32_t s);
VPRIV const attrs *sym_attrs(cc *c, uint32_t s);
VPRIV void sym_name(cc *c, tbuf *b, uint32_t s);
VPRIV void ref_name(cc *c, tbuf *b, uint32_t r);
VPRIV void ref_off(cc *c, tbuf *b, uint32_t r, int64_t off);
/* print.c */
VPRIV void p_lbl(tbuf *b, uint32_t fid, uint32_t l);
VPRIV void print_ins(cc *c, tbuf *b, const ins *i, uint32_t fid, const char *raw);
/* obj.c */
static inline bool direct(const ecx *e);
VPRIV size_t it_open(ecx *e, uint32_t kind);
VPRIV void it_close(ecx *e, size_t h);
VPRIV void *it_room(ecx *e, size_t n);
VPRIV int r_chunk(rctx *R, const char *p, size_t n);
/* label.c */
static inline ins *put(ecx *e, int op);
static inline void op3(ecx *e, int op, int x, int rd, int rn, int rm);
static inline void op3s(ecx *e, int op, int x, int rd, int rn, int rm, int kind, int sh);
static inline void op4(ecx *e, int op, int x, int rd, int rn, int rm, int ra);
static inline void opi(ecx *e, int op, int x, int rd, int rn, int64_t imm);
static inline void opbf(ecx *e, int op, int x, int rd, int rn, int immr, int imms);
static inline void movi(ecx *e, int x, int rd, int64_t v);
static inline void mov(ecx *e, int x, int rd, int rn);
static inline void cset(ecx *e, int rd, int cond);
static inline int last_cset(ecx *e, const val *v);
static inline void mem(ecx *e, int op, int sz, int ext, int rd, int base, int64_t off);
static inline void symop(ecx *e, int op, int sz, int ext, int rd, int rn, uint32_t ref, int64_t off);
VPRIV uint32_t newlabel(ecx *e);
VPRIV void kv_edge(ecx *e, uint32_t l);
VPRIV void deflabel(ecx *e, uint32_t l);
VPRIV void deflabel_forced(ecx *e, uint32_t l);
VPRIV void deflabel_loop(ecx *e, uint32_t l, bool forced);
VPRIV void jump(ecx *e, uint32_t l);
VPRIV void bcond(ecx *e, int cond, uint32_t l);
VPRIV void cbz(ecx *e, bool nz, int x, int r, uint32_t l);
/* value.c */
VPRIV int treg(ecx *e);
VPRIV void tfree(ecx *e, int r);
VPRIV void val_free(ecx *e, const val *v);
VPRIV int32_t slot(ecx *e, uint64_t size, uint32_t align);
VPRIV void slot_tag(ecx *e, ins *i, int64_t off, int64_t len);
static inline val vconst(i128 i);
static inline val vreg(int r);
static inline val vaddr(uint32_t ref, i128 off);
static inline val vnone(void);
VPRIV void addr_of_ref(ecx *e, int rd, uint32_t ref, int64_t off);
VPRIV int toreg(ecx *e, val *v);
VPRIV void toreg2(ecx *e, val *v, bool sgn);
VPRIV void lfree(ecx *e, const lval *l);
VPRIV int lbase(ecx *e, const lval *l, int64_t *off, int size);
VPRIV int log2sz(uint64_t n);
VPRIV void load_n(ecx *e, int rd, const lval *l, int n, int ext);
VPRIV void store_n(ecx *e, int rs, const lval *l, int64_t extra, int n);
VPRIV val laddr(ecx *e, lval *l);
VPRIV lval lfromval(ecx *e, val v, uint32_t align);
VPRIV void copy_mem(ecx *e, int dst, int src, uint64_t n);
VPRIV void zero_mem(ecx *e, int dst, uint64_t n);
/* abi.c */
VPRIV int hfa(cc *c, qty t, int *n);
VPRIV bool vec_small(cc *c, qty t);
VPRIV void vec_small_ret(ecx *e, qty t, int rs);
VPRIV void vec_small_res(ecx *e, qty t, int32_t sl);
VPRIV bool is_sret(cc *c, qty t);
VPRIV void classify_va(ecx *e, qty t, int *nsaa, abiarg *a);
VPRIV void classify(ecx *e, qty t, int *ngrn, int *nsaa, abiarg *a);
VPRIV void spill_live(ecx *e, spill *s);
VPRIV void unspill(ecx *e, const spill *s);
VPRIV void call_fixed(ecx *e, int fn, int a0, int a1, int a2, int64_t n);
/* bind.c */
static inline int32_t keep_cl(const ecx *e, int32_t mark);
static inline int X(cc *c, qty t);
static inline void rlog(ecx *e, uint32_t tag, uint32_t v);
VPRIV void note_ref(ecx *e, uint32_t ref);
VPRIV void claim_ref(ecx *e, uint32_t ref);
VPRIV bind *binding(ecx *e, uint32_t s);
VPRIV int regvar_reg(cc *c, uint32_t s);
VPRIV int32_t bind_slot(ecx *e, uint32_t s);
VPRIV lval local_lval(ecx *e, uint32_t s);
VPRIV uint32_t sym_align(cc *c, uint32_t s);
VPRIV bool is_local_var(cc *c, uint32_t s);
/* lval.c */
VPRIV uint32_t align_at(uint64_t off, uint32_t a);
VPRIV uint64_t elem_size(cc *c, qty ptr);
VPRIV val ptr_add(ecx *e, val p, val i, int64_t scale, bool sub);
VPRIV val ptr_add_vla(ecx *e, val p, val i, qty ptr, bool sub);
VPRIV bool ptr_to_vla(cc *c, qty ptr);
VPRIV lval gen_lval(ecx *e, uint32_t n);
VPRIV val load(ecx *e, lval *l, qty t);
VPRIV bool is_atomic_lv(ecx *e, const lval *l, qty t);
VPRIV int atomic_sz(ecx *e, qty t);
VPRIV int atomic_addr(ecx *e, lval *l, bool *own);
VPRIV void store(ecx *e, lval *l, qty t, val *v);
/* arith.c */
VPRIV void extend(ecx *e, int rd, int rs, int from, bool sgn);
VPRIV int lreg_of(ecx *e, uint32_t s);
VPRIV void opt_function(ecx *e);
VPRIV lval bind_lreg(ecx *e, uint32_t s);
VPRIV val fold_int_bin(ecx *e, int op, qty ot, qty rt, i128 l, i128 r, bool *ok);
VPRIV bool em_is_cmp(int op);
VPRIV int emit_cmp(ecx *e, int op, int x, bool sg, val *a, val *b);
VPRIV int emit_cmp2(ecx *e, int op, bool sg, val *a, val *b);
VPRIV int test_zero(ecx *e, val *v, qty t, uint32_t l, bool nz);
VPRIV val tobool(ecx *e, val v, qty t);
/* fp.c */
static inline void word(ecx *e, uint32_t w);
VPRIV int fpk(ecx *e, qty t);
static inline uint32_t f_in(int d, int vr, int r);
static inline uint32_t f_out(int d, int r, int vr);
VPRIV val f_const(int d, double x);
static inline uint32_t q_in_hi(int vr, int r);
static inline uint32_t q_out_hi(int r, int vr);
VPRIV void vr_store(ecx *e, int sz, int vr, int base, int32_t off);
VPRIV void vr_load(ecx *e, int sz, int vr, int base, int32_t off);
VPRIV i128 ld_of_d(double x);
VPRIV val f_const_cv(ecx *e, qty t, const cval *cv);
VPRIV val f_const_t(ecx *e, qty t, double x);
VPRIV val ld_compare(ecx *e, int op, qty t, val a, val b);
VPRIV val f_arith(ecx *e, int op, qty ty, val a, val b);
VPRIV int f_cmp(ecx *e, int op, int d, val *a, val *b);
VPRIV val f_compare(ecx *e, int op, qty t, val a, val b);
VPRIV val f_tobool(ecx *e, val v, qty t);
VPRIV val f_neg(ecx *e, val v, qty t);
VPRIV val ld_cast(ecx *e, val v, qty from, qty to);
VPRIV val f_cast(ecx *e, val v, qty from, qty to);
/* arith.c */
VPRIV val cast(ecx *e, val v, qty from, qty to, int kind);
VPRIV void shift_imm(ecx *e, int op, int x, int rd, int rn, int k);
VPRIV val arith(ecx *e, int op, qty ty, val a, val b);
VPRIV val arith2(ecx *e, int op, qty ty, val a, val b);
/* call.c */
VPRIV val do_call(ecx *e, uint32_t ref, val *fnv, carg *args, int n, qty rty, bool noret, int nfix);
VPRIV val call_named(ecx *e, uint32_t ref, val *vals, qty *tys, int n, qty rty, bool noret);
VPRIV void walk_fn(ecx *e, uint32_t s);
VPRIV void kv_track(ecx *e, uint32_t s, const val *v);
VPRIV int ael_slot(ecx *e, uint32_t n, bool create);
VPRIV bool const_var(ecx *e, uint32_t s, const val *v);
VPRIV bool can_inline(ecx *e, uint32_t s);
VPRIV val inline_call(ecx *e, uint32_t s, carg *args, int n);
VPRIV uint32_t direct_callee(cc *c, uint32_t fn, uint32_t *ref);
VPRIV val gen_call(ecx *e, uint32_t n);
/* builtin.c */
VPRIV uint32_t strip_casts(cc *c, uint32_t n);
VPRIV bool side_effects(cc *c, uint32_t n);
VPRIV val dry(ecx *e, uint32_t n);
VPRIV val va_arg_(ecx *e, const node *x);
VPRIV val gen_builtin(ecx *e, uint32_t n);
/* expr.c */
VPRIV void move_to(ecx *e, int r, int r2, val *v, qty t);
VPRIV val gen(ecx *e, uint32_t n);
/* cond.c */
VPRIV int cond_fold(ecx *e, uint32_t n);
VPRIV void gen_cond(ecx *e, uint32_t n, bool jump_if, uint32_t l);
/* stmt.c */
VPRIV void gen_init_local(ecx *e, const lval *dst, qty ty, uint32_t init);
VPRIV void run_cleanups(ecx *e, int to);
VPRIV uint32_t user_label(ecx *e, uint32_t l);
VPRIV val gen_block(ecx *e, uint32_t n, bool want);
VPRIV val vla_bytes(ecx *e, qty t);
VPRIV void vla_fix(ecx *e, qty t, bool pure);
VPRIV void gen_stmt(ecx *e, uint32_t n);
/* asm.c */
VPRIV void gen_asm(ecx *e, uint32_t n);
/* func.c */
VPRIV void out_chunk(ecx *e, uint64_t key, size_t start);
VPRIV void sym_header(ecx *e, tbuf *b, uint32_t s, bool fn, const char *defsec, uint32_t align);
VPRIV void emit_function(ecx *e, uint32_t s);
/* data.c */
VPRIV void emit_object(ecx *e, uint32_t s);
VPRIV void emit_string(ecx *e, uint32_t i);
/* prepass.c */
VPRIV bool is_aconst(cc *c, uint32_t n);
VPRIV uint32_t aconst_idx(cc *c, uint32_t n);
VPRIV void ensure_walked(emitter *E, cc *c, uint32_t s);
VPRIV int u32_cmp(const void *a, const void *b);
/* tu.c */
VPRIV void enqueue(emitter *E, uint32_t item);

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

/* Apple arm64: an integer under 4 bytes in a register is extended to 32 bits by its producer */
static inline bool darwin_narrow(cc *c, qty t) { return DARWIN(c) && T(c, t)->size < 4 && is_integer(c, t); }

/* a VLA somewhere in t's pointer/array chain (int a[n], int (*p)[n][4]) */
static inline bool vm_type(cc *c, qty t)
{
	for (;;) {
		const ctype *a = T(c, t);

		if (a->kind == TY_ARRAY && (a->flags & TF_VLA))
			return true;
		if (a->kind != TY_ARRAY && a->kind != TY_PTR)
			return false;
		t = a->base;
	}
}
static inline uint32_t talign(cc *c, qty t) { return T(c, t)->align; }
static inline bool is_signed(cc *c, qty t) { return cc_is_signed(c, t); }
/* long double: binary128, kept as its bits in a register pair (arithmetic: the __*tf* helpers) */
static inline bool is_ld(cc *c, qty t) { return is_flt(c, t) && tsize(c, t) == 16; }
static inline bool is_wide(cc *c, qty t) { return (is_integer(c, t) || is_flt(c, t)) && tsize(c, t) == 16; }

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

static inline bool direct(const ecx *e) { return e->E->o->emit == 2 && !e->E->objtext; }

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
	if (rn == FRAME)
		slot_tag(e, i, imm, -1);	/* an address: may point just past the end */
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

/* the last record is `cset v.r, cond` (nothing since, no label: the flags still say it): cond, else -1.
 * The value is then 0/1 already, and a test of it can use the flags. */
static inline int last_cset(ecx *e, const val *v)
{
	const ins *i;

	if (v->k != V_REG || !e->reach || !e->nin || e->E->o->opt < 1)
		return -1;
	i = &e->in[e->nin - 1];
	if (i->op != I_CSINC || i->rn != ZR || i->rm != ZR || i->rd != v->r)
		return -1;
	return i->cond ^ 1;
}

static inline void mem(ecx *e, int op, int sz, int ext, int rd, int base, int64_t off)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = (uint8_t)sz, i->cond = (uint8_t)ext, i->rd = (uint8_t)rd, i->rn = (uint8_t)base;
	i->imm = off;
	if (base == FRAME)
		slot_tag(e, i, off, (int64_t)1 << sz);
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

/* ---------------- values ---------------- */

static inline val vconst(i128 i) { val v = { .k = V_CONST, .i = i }; return v; }
static inline val vreg(int r) { val v = { .k = V_REG, .r = (uint8_t)r }; return v; }
static inline val vaddr(uint32_t ref, i128 off) { val v = { .k = V_ADDR, .ref = ref, .i = off }; return v; }
/* an undefined weak symbol's address may be 0: a register, never a V_ADDR (which every fold takes as nonzero) */
static inline bool ref_weak(const cc *c, uint32_t ref)
{
	const sym *y = &c->syms[RIDX(ref)];

	return RKIND(ref) == RK_SYM && (y->flags & SF_WEAK) &&
	       !(y->kind == S_FUNC ? (y->flags & SF_DEFINED) && y->body : (y->flags & (SF_DEFINED | SF_TENTATIVE)));
}
static inline val vnone(void) { val v = { .k = V_NONE }; return v; }

/* a statement's temporaries end with it; compound literals live on to the end of their block */
static inline int32_t keep_cl(const ecx *e, int32_t mark)
{
	return mark > e->cl_top ? mark : e->cl_top;
}

static inline int X(cc *c, qty t) { return tsize(c, t) == 8 || is_ptr(c, t) || is_func(c, t) ? 1 : 0; }
static inline void rlog(ecx *e, uint32_t tag, uint32_t v)
{
	if (unlikely(e->nrlog + 2 > RLOG_CAP)) {
		fputs("vcc: emit log full\n", stderr);
		abort();
	}
	e->rlog[e->nrlog++] = tag;
	e->rlog[e->nrlog++] = v;
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

static inline uint32_t f_in(int d, int vr, int r) { return (d ? 0x9e670000u : 0x1e270000u) | (unsigned)r << 5 | (unsigned)vr; }	/* fmov v, x/w */
static inline uint32_t f_out(int d, int r, int vr) { return (d ? 0x9e660000u : 0x1e260000u) | (unsigned)vr << 5 | (unsigned)r; }	/* fmov x/w, v */

/* fmov v<vr>.d[1], x<r> / fmov x<r>, v<vr>.d[1] */
static inline uint32_t q_in_hi(int vr, int r) { return 0x9eaf0000u | (unsigned)r << 5 | (unsigned)vr; }
static inline uint32_t q_out_hi(int r, int vr) { return 0x9eae0000u | (unsigned)vr << 5 | (unsigned)r; }

#endif
