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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "../cc/ccint.h"
#include "emit.h"
#include "../as/as.h"
#include "../obj/obj.h"
#include "../driver/ppdriver.h"
#include "../lib/arena.h"
#include "../lib/tbuf.h"

#include "name.h"

/* functions one file defines and another calls: static when the directory is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

/* Exact allocation/cell analysis shared by machine-record optimizers. */
#include "rec/slots.h"

#define N(i) (&c->nodes[i])
/* Apple arm64 (Mach-O): '_' names, GOT for symbols outside the TU, variadic
 * arguments on the stack, va_list a char *, stack arguments packed */
#define DARWIN(c) ((c)->o->target == PP_DARWIN)
/* WebAssembly (wasm64): arch/wasm/emit/ generates the functions, data.c the data */
#define WASM(c) PP_IS_WASM((c)->o->target)
#define WASM32(c) ((c)->o->target == PP_WASM32)
/* x86-64 Linux: emit/walk/ and arch/x64/emit/ generate the functions (PLAN-x86.md) */
#define X86(c) ((c)->o->target == PP_LINUX_X86_64)

/*
 * A C99 inline definition or GNU extern inline: not emitted (another TU
 * has the definition).  The wasm target has no inliner, so it emits a local
 * copy of one it calls, as clang's inlining would make it unneeded.
 */
static inline bool inline_only(const sym *y)
{
	if (y->kind != S_FUNC || !(y->flags & SF_DEFINED) || !y->body)
		return false;
	if ((y->flags & SF_GNU_INLINE) && !(y->flags & SF_INLINE_NOEXTERN))
		return true;
	return (y->flags & SF_INLINE) && !(y->flags & (SF_GNU_INLINE | SF_EXTDECL)) && y->sc != SC_EXTERN && y->sc != SC_STATIC;
}
/* a symbol offset the relocation itself can carry: ELF RELA 32 bits here, a
 * Mach-O ARM64_RELOC_ADDEND 24 bits signed (clang adds the rest in code) */
#define SYMOFF_OK(c, off) (DARWIN(c) ? (off) >= -(1 << 23) && (off) < (1 << 23) : (off) >= INT32_MIN && (off) <= INT32_MAX)
#define T(c, t) CT(c, t)
#define KIND(c, t) CKIND(c, t)

#include "../arch/a64/emit/ins.h"
#include "rec/rec.h"
#include "flow/flow.h"
#include "flow/values.h"

/* references to things with a symbol: kind in the top 4 bits */
enum { RK_SYM = 0, RK_STR = 1, RK_LBL = 2, RK_NAME = 3, RK_TABLE = 4 };
#define REF(k, i) (((uint32_t)(k) << 28) | (uint32_t)(i))
#define RKIND(r) ((r) >> 28)
#define RIDX(r) ((r) & 0x0fffffffu)

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
} keep;

/* analysis bits (prepass, read-only while emitting) */
enum { FF_SEEN = 1, FF_NONNEG = 2, FF_ZERO = 4, FF_BAD = 8 };
enum { SA_WRITTEN = 1, SA_CLEANUP_FN = 2, SA_MUSTFOLD = 4, SA_CWRITE = 8, SA_CMULTI = 16, SA_USED = 32, SA_NOINL = 64,
       SA_AESC = 128 /* a local array used other than as a[CONST] */ };
/* USED: a local or param read; NOINL: a static local holds a label address (one copy of the code only) */
/* CWRITE: only ever assigned integer constants (all cwval unless CMULTI) */	/* per sym; MUSTFOLD: a function that calls an error() function */
enum { NA_LABEL = 1, NA_ALLOCA = 2 };				/* per node: statement contains a label or case */

typedef struct em_cg_edge {uint32_t callee,call;} em_cg_edge;

typedef struct emitter {
	cc *c;
	const cc_opts *o;
	_Atomic uint8_t *claimed;	/* per sym: queued for emission */
	_Atomic uint8_t *strdone;	/* per string: emitted */
	_Atomic uint8_t *extref;	/* per sym: referenced (for .weak of undefined syms) */
	_Atomic uint8_t *walked;
	uint16_t *fnsize;		/* per function: AST nodes (prepass) */
	uint8_t *inl_read; /* parameter references outside a direct (void)param discard */
	uint8_t *once;			/* VIR lowering on: per function, bit 0 when its one reference is a direct call (fn_once), bit 1 when it calls setjmp (prepass); else NULL */
	/* VIR lowering on (with once): the direct call graph the prepass records, per
	   function its size with the callees it inlines (fn_isize) and its recursion group */
	em_cg_edge *cg;
	uint32_t ncg, capcg, *cgoff, *cgn, *scc;
	uint16_t *isz;
	bool cgrec;			/* the prepass records calls (before the workers start) */
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
	_Atomic int errors;
	_Atomic uint64_t nins, nfn, nobj, ninl;
	_Atomic uint64_t print_ns, walk_ns, nwalk;
	uint8_t *mf;			/* per sym: static function to inline everywhere (kept across rounds) */
	_Atomic uint32_t newmf;		/* marked this round */
	_Atomic uint32_t nerrcall;	/* error() calls dropped (not folded) */	/* VCC_EMIT_STATS: time in finish_function */
	const keep *K;			/* the previous round's functions, or NULL */
	bool stats;
	bool objtext;			/* VCC_OBJ_TEXT: objects through the printer and as_run (checks the object backend) */
	pthread_mutex_t dmu;		/* -g: the file table */
	char **dfile;			/* .file N (1-based) names */
	uint32_t ndfile, dfcap;
	pthread_mutex_t emu;		/* diagnostics */
	uint32_t errcall[256];		/* error() call sites already reported (under emu) */
	uint32_t nerrcall_loc;
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
	int64_t slot;		/* B_SLOT, B_INDIRECT */
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

#define CAPNEST (1u << 22)	/* loops/switches and cleanups in scope */
typedef struct loopctx {
	uint32_t brk, cont;	/* local labels (cont 0: switch) */
	int cdepth;
} loopctx;

typedef struct fnctx {		/* one function instance: the emitted function or an inlined body */
	uint32_t fn, inst, call;
	uint32_t retlbl;
	int64_t retslot;
	uint8_t retreg;		/* inlined: the result's temporary + 1 (0: in retslot) */
	qty rty;
	bool inl;
	bool cst_ok, any_ret;
	val cst;
	int cbase;		/* cleanup stack depth at entry */
	int nloop;		/* loop stack depth at entry */
	struct fnctx *up;
} fnctx;

#define NTEMP 17
#define MAXLREG 10	/* x19-x28 */
#define LREGS_DEFAULT 6	/* the rest stay expression temporaries */
#define INLINE_FREE_FAST 5
#define INLINE_FREE_SAFE 8

/* a stack slot handed out by slot() (offsets are reused across scopes) */
typedef struct slotal {
	int64_t off;
	uint64_t size;
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
	uint32_t reuse_from, reuse_to; /* temporary substitution within one pure expression */
	struct dbgrow { uint32_t k, file, line, col; } *dl;	/* -g: a row at record k */
	uint32_t ndl, dlcap;
	const char *dlname;	/* the last file looked up, and its number */
	uint32_t dlnlen, dlfile;
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
	int64_t foff, fmax;
	uint32_t falign;	/* largest slot alignment; over 16 the prologue realigns sp */
	/* VLA sizes, evaluated where their declaration is reached (stmt.c vla_fix) */
	struct { uint32_t node, inst; int64_t slot; } vsz[NVSZ];
	uint32_t nvsz;
	int64_t cl_top;		/* frame offset above the block's compound literals (kept past statements) */
	int64_t outmax;		/* outgoing stack argument bytes */
	bool dyn, calls, vararg;
	bool discarded_scalar;	/* lowering exposed an already-emitted unused scalar result */
	int64_t vaslot;		/* variadic register save area */
	int ngr_named, nstk_named;	/* variadic: named GPR args, named stack bytes */
	int nvr_named;
	int64_t vrslot;		/* variadic (Linux): named FP/SIMD args, the v0-v7 save area */
	int nsrn;		/* classify: next SIMD/FP argument register */
	/* temporaries */
	uint32_t live;		/* bit i: tempregs[i] in use */
	uint32_t resv;		/* ... of those, never handed out: B_LREG variables', x28 once falign > 16 */
	int inlfree;		/* free temporaries an inlined call keeps (0: INLINE_FREE_SAFE) */
	int spill;		/* last try: a binary operator's left value waits in a frame slot */
	int nlr;		/* B_LREG candidates of the emitted function: sym, register */
	uint32_t lrsym[MAXLREG];
	uint8_t lrreg[MAXLREG];
	uint32_t usedcs;	/* callee-saved registers used (bit = x number) */
	/* control */
	bool reach;
	int dry;		/* inside dry(): code is generated and thrown away, no references */
	loopctx *loops;		/* (nesting is bounded by the node count: reserved for it) */
	int nloop;
	uint32_t *clean;	/* syms with cleanup, innermost last */
	int nclean;
	fnctx *f;
	int inline_depth;
	uint32_t top;		/* sym being emitted */
	uint32_t asmk;		/* inline asm statements emitted in it so far (%=) */
	uint32_t *cases;	/* scratch for switch case collection */
	uint32_t *weakref;	/* weak undefined syms this thread referenced first */
	uint32_t nweakref;
	uint32_t ncases;
	uint32_t *jtl, njtl, capjtl;	/* jump tables of the function: case labels, table after table */
	struct ejt { uint32_t tab, off, n; } *jt;	/* table label, first entry in jtl, entries */
	uint32_t njt, capjt;
	uint32_t *rlog;		/* this round's item logs: (RL_*, value) pairs */
	size_t nrlog, rlog0;	/* ... and where the current item's starts */
	jmp_buf bail;
	struct wst *w;		/* WASM: arch/wasm/emit/ state (wimpl.h) */
	struct wkst *wk;	/* X86: the walker's state (emit/walk/walk.h) */
	struct xst *x64;	/* X86: arch/x64/emit/ state (x64impl.h) */
	struct vwst *vw;	/* the VIR builder's state (emit/vir/, PLAN-vir.md) */
	bool vtry;		/* the VIR walker runs: an error is not reported, it makes the function fall back */
	bool vlow;		/* the records are VIR's lowering (finish_function's clean-ups for it) */
	const char *vfail;	/* ... and why (NULL: it did not fail) */
	struct em_switch_values_data *switch_values; /* accepted tables of this real function */
} ecx;
enum { FN_MEMCPY, FN_MEMSET, FN_MEMMOVE, FN_UDIVTI3, FN_DIVTI3, FN_UMODTI3, FN_MODTI3, FN_STRLEN,
       FN_ASHLTI3, FN_LSHRTI3, FN_ASHRTI3, FN_FLOATTISF, FN_FLOATTIDF, FN_FLOATUNTISF, FN_FLOATUNTIDF,
       FN_FIXSFTI, FN_FIXDFTI, FN_FIXUNSSFTI, FN_FIXUNSDFTI,
       FN_ADDTF3, FN_SUBTF3, FN_MULTF3, FN_DIVTF3, FN_EQTF2, FN_NETF2, FN_LTTF2, FN_LETF2, FN_GTTF2, FN_GETF2, FN_UNORDTF2,
       FN_EXTENDDFTF2, FN_EXTENDSFTF2, FN_TRUNCTFDF2, FN_TRUNCTFSF2, FN_FLOATSITF, FN_FLOATDITF, FN_FLOATUNSITF,
       FN_FLOATUNDITF, FN_FLOATTITF, FN_FLOATUNTITF, FN_FIXTFSI, FN_FIXTFDI, FN_FIXUNSTFSI, FN_FIXUNSTFDI,
       FN_FIXTFTI, FN_FIXUNSTFTI, FN_EXTENDXFTF2, FN_TRUNCTFXF2, FN_EXTENDBFSF2, FN_TRUNCSFBF2, FN_TRUNCDFBF2, FN_TRUNCXFBF2, FN_TRUNCTFBF2, FN_EXTENDHFSF2, FN_TRUNCSFHF2, FN_TRUNCDFHF2, FN_TRUNCXFHF2, FN_TRUNCTFHF2,
       FN_FLOATTIXF, FN_FLOATUNTIXF, FN_FIXXFTI, FN_FIXUNSXFTI,
       FN_FLOORF, FN_CEILF, FN_TRUNCF, FN_ROUNDF, FN_RINTF, FN_NEARBYINTF, FN_FABSF };
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

enum { L_FRAME, L_SYM, L_REG, L_GLOBREG, L_VREG, L_VAR };
/* L_VREG: a B_LREG variable, value in r (no address).
   L_VAR: a variable of a virtual lowering target (PLAN-vir.md, SPEC-vir.md
   X3), its number in lval.ref, no address.  Only that target makes one; the
   real targets treat it as an internal error. */

typedef struct lval {
	uint8_t k;
	uint8_t r;		/* L_REG: base register (a temp, owned); L_GLOBREG: the register */
	uint8_t bf;		/* bit-field */
	uint8_t sgn;		/* bit-field signedness */
	int64_t dynbit_slot;	/* packed vector lane: saved dynamic bit index, 0 for static */
	uint16_t bitoff, width;	/* bit-field: bits from the byte at off */
	uint32_t ref;		/* L_SYM */
	int64_t off;		/* byte offset (L_FRAME: slot offset) */
	uint32_t align;		/* known alignment of the address */
} lval;

enum { A_IGNORE, A_REG, A_STACK, A_REF_REG, A_REF_STACK, A_VREG, A_HFA, A_BOOL_LANES };	/* A_VREG: float/double in v<reg>; A_HFA: nreg members of fsz bytes in v<reg>.. */

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
	int64_t slot[NTEMP];
} spill;

#define RLOG_CAP ((size_t)1 << 28)

/* ---------------- calls ---------------- */

typedef struct carg {
	val v;
	qty t;
	abiarg a;
	int64_t slot;		/* value parked in a frame slot (-1: v is a constant/address) */
} carg;

/* ---------------- across files ---------------- */

/* opt/switch-values.c: bounded integer-result tables, no AST mutation. */
typedef struct em_switch_values_recipe {
 i128 lo;
 uint64_t values[256], fallback;
 qty ty;
 uint32_t destination;
 uint16_t slots;
 uint8_t selector_bits, element_bytes;
 bool is_return;
} em_switch_values_recipe;
VPRIV bool em_switch_values_plan(cc *c, uint32_t n, em_switch_values_recipe *r);
VPRIV bool em_switch_values_queue(ecx *e, uint32_t n, const em_switch_values_recipe *r);
VPRIV void em_switch_values_reset(ecx *e);
VPRIV void em_switch_values_emit(ecx *e);
VPRIV bool a64_switch_values(ecx *e, uint32_t n);
VPRIV bool wk_switch_values(ecx *e, uint32_t n);
VPRIV bool wswitch_values(ecx *e, uint32_t n);
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
/* opt/divpow2.c */
VPRIV bool em_divpow2(cc *c, qty ty, int *op, i128 *rhs);
/* opt/sdivpow2.c: bias negative dividends to preserve truncation toward zero. */
typedef struct em_sdivpow2_recipe {
	uint8_t bits, shift;
	bool negative, remainder;
} em_sdivpow2_recipe;
VPRIV bool em_sdivpow2(cc *c, qty ty, int op, i128 rhs, em_sdivpow2_recipe *r);
/* opt/mulconst.c: shift for x + (x << shift), or zero to retain multiply. */
VPRIV unsigned em_mulconst(unsigned bits, i128 rhs);

/* opt/udivconst.c: unsigned32/64 reciprocal; quotient = high_bits(n*magic),
 * optional ((n-q)>>1)+q correction, then shift. compare means q=(n>=d). */
typedef struct em_udivconst_recipe {
	uint64_t divisor, magic;
	uint8_t bits, shift;
	bool add, compare, remainder;
} em_udivconst_recipe;
VPRIV bool em_udivconst32(uint32_t divisor, bool remainder, em_udivconst_recipe *r);
VPRIV bool em_udivconst64(uint64_t divisor, bool remainder, em_udivconst_recipe *r);
VPRIV bool em_udivconst(cc *c, qty ty, int op, i128 rhs, em_udivconst_recipe *r);
VPRIV bool a64_udivconst_ok(ecx *e, val a, const em_udivconst_recipe *r);
VPRIV val a64_udivconst(ecx *e, val a, const em_udivconst_recipe *r);
/* opt/sdivconst.c: reciprocal for a positive divisor magnitude. Signed high
 * product, optional numerator addition, arithmetic shift, then sign correction. */
typedef struct em_sdivconst_recipe {
	uint64_t divisor;
	int64_t magic;
	uint8_t bits, shift;
	bool add, negative, remainder;
} em_sdivconst_recipe;
VPRIV bool em_sdivconst32(int32_t divisor, bool remainder, em_sdivconst_recipe *r);
VPRIV bool em_sdivconst64(int64_t divisor, bool remainder, em_sdivconst_recipe *r);
VPRIV bool em_sdivconst(cc *c, qty ty, int op, i128 rhs, em_sdivconst_recipe *r);
VPRIV bool a64_sdivconst_ok(ecx *e, val a, const em_sdivconst_recipe *r);
VPRIV val a64_sdivconst(ecx *e, val a, const em_sdivconst_recipe *r);
/* opt/mulshift.c and opt/xor-not.c: machine recipes, no operand evaluation. */
VPRIV bool em_bound_constant(cc *c, uint32_t n, i128 *value);
VPRIV int em_cmpbounds(ecx *e, uint32_t n, uint32_t *operand);
VPRIV int em_repeated_integer(ecx *e, uint32_t n);
VPRIV bool em_sibling_values(ecx *e, uint32_t n);
VPRIV bool em_decl_reuse_values(ecx *e, uint32_t decl, uint32_t consumer, const uint32_t *killed, uint32_t nkilled, uint32_t *from, uint32_t *to);
VPRIV bool em_block_value_reuse(ecx *e, const node *block, uint32_t first, uint32_t i, uint32_t *from, uint32_t *to);
VPRIV uint32_t em_double_unary(ecx *e, uint32_t n);

VPRIV bool em_cmpzero(ecx *e, uint32_t n, uint32_t *operand);
VPRIV bool em_mulshift(cc *c, qty ty, int *op, i128 *rhs);
VPRIV bool em_xor_not(cc *c, qty ty, int op, i128 rhs);
/* opt/identity.c */
enum { EM_ID_NONE, EM_ID_LEFT, EM_ID_ZERO, EM_ID_ONES, EM_ID_TRUE };
VPRIV int em_bin_identity(cc *c, qty ty, int op, i128 rhs);
/* opt/switch.c: inclusive ranges in controlling-type order. Preparation only
 * for tree dispatch (>=8 cases); no allocation or target state in the policy.
 * Preparation rejects wrapped/overlapping ranges: callers retain the chain. */
typedef struct em_switch_case {
	i128 lo, hi;
	uint32_t target;
} em_switch_case;
enum { EM_SWITCH_LINEAR, EM_SWITCH_TREE, EM_SWITCH_TABLE };
VPRIV int em_switch_strategy(uint32_t n, i128 lo, i128 hi, bool ranges, bool tables);
VPRIV bool em_switch_prepare(em_switch_case *cases, uint32_t n, unsigned bits, bool sg);
/* One bounded membership test for a run of stacked case labels. The original
 * body remains intact; node names its final case label, not a new destination. */
typedef struct em_switch_bitset_recipe {
	i128 lo;
	uint64_t mask;
	uint32_t node;
	uint8_t span, bits;
} em_switch_bitset_recipe;
VPRIV bool em_switch_bitset_plan(cc *c, uint32_t body, uint32_t nlabels,
				 unsigned bits, bool sg, em_switch_bitset_recipe *out);
VPRIV bool a64_switch_bitset(ecx *e, uint32_t body, uint32_t base, int r,
			    int cx, bool sg, uint32_t dflt);
/* opt/unusedinit.c: bounded recipes for never-referenced integer declarations. */
enum { EM_UNUSED_KEEP, EM_UNUSED_OMIT, EM_UNUSED_EVAL };
VPRIV bool em_unused_local(ecx *e, uint32_t s, uint32_t fn);
VPRIV unsigned em_unused_init(ecx *e, const node *decl, uint32_t fn, unsigned budget);
/* opt/locals.c: eligibility and bounded selection from the existing prepass. */
/* opt/tail.c: exact direct forwarding wrappers; target capability/ABI checks follow. */
VPRIV bool em_tail_caller_ok(ecx *e, uint32_t fn);
VPRIV bool em_tail_target_ok(ecx *e, uint32_t s);
VPRIV bool em_tail_candidate(ecx *e, uint32_t fn, uint32_t expr, uint32_t maxargs, uint32_t *callee);
VPRIV bool a64_tail_return(ecx *e, uint32_t expr);
VPRIV uint32_t a64_tail_ref(const ecx *e);
VPRIV bool em_local_eligible(ecx *e, uint32_t s);
VPRIV bool em_local_gpr_type(cc *c, qty ty);
VPRIV unsigned em_select_locals(ecx *e, uint32_t fn, uint32_t *cand, unsigned max,
			       bool (*target_ok)(cc *, qty));
/* rec/fallthrough.c: opaque records; no shared struct layout is assumed. */
typedef struct em_rec_layout {
	size_t stride, op_offset, label_offset;
	uint8_t jump_op, label_op;
} em_rec_layout;
VPRIV uint32_t em_rec_fallthrough(void *records, uint32_t count, const em_rec_layout *layout,
				void (*remap)(void *, uint32_t, uint32_t), void *ctx);
/* rec/unreachable.c: unknown opcodes and opaque records are entry barriers. */
VPRIV uint32_t em_rec_unreachable(void *records, uint32_t count, size_t stride, size_t op_offset,
				const uint8_t *flow, size_t nflow,
				void (*remap)(void *, uint32_t, uint32_t), void *ctx);
/* rec/copies.c: inclusive block end, whole-register t/v below 64. The target
 * preflights each rewritten record without mutation (apply=false); applying
 * the same rewrite must then succeed. Existing conservative liveness is only
 * enlarged, and no effects/liveness table is built by this scan.
 */
typedef struct em_rec_copy_scan {
	void *records;
	size_t stride, op_offset;
	uint8_t deleted_op;
	const void *flags;
	size_t flags_stride;
	uint16_t opaque_mask;
	uint64_t *read, *live;
	const uint64_t *write;
	/* Optional on-demand effects. False is an opaque boundary. With no live
	 * array a copy must reach an explicit overwrite; block end proves nothing.
	 */
	bool (*inspect)(void *ctx, uint32_t j, uint64_t *read, uint64_t *write);
	bool (*rewrite)(void *ctx, uint32_t j, uint8_t from, uint8_t to, bool apply);
	void *ctx;
} em_rec_copy_scan;
VPRIV bool em_rec_copy_forward(const em_rec_copy_scan *s, uint32_t k, uint32_t end, uint8_t t, uint8_t v);
/* insn.c */
extern const char cond_name[][8];
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
VPRIV __attribute__((format(printf, 3, 4))) void eerror(ecx *e, uint32_t loc, const char *fmt, ...);
VPRIV __attribute__((format(printf, 3, 4), noreturn)) void efatal(ecx *e, uint32_t loc, const char *fmt, ...);
/* type.c */
VPRIV uint32_t canon_sym(cc *c, uint32_t s);
VPRIV const attrs *sym_attrs(cc *c, uint32_t s);

/* memcpy/memmove/memset declared by a header (symbol s, na arguments): clang's builtin at -O1 and up (not
 * under -ffreestanding/-fno-builtin[-NAME]).  CC_LIB_MEMCPY.., or 0 */
static inline int em_plain_mem_kind(ecx *e, uint32_t s, int na)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];

	if (na != 3 || e->E->o->opt < 1 || e->E->o->no_builtin || y->sc == SC_STATIC || !(y->flags & SF_GLOBAL) ||
	    sym_attrs(c, s)->asmname)
		return 0;
	for (unsigned i = 0; i < CC_DISABLED_BUILTIN_WORDS; i++)
		if (e->E->o->disabled_builtins[i])
			return 0;
	return EM_IDENT_IS(y->name, "memcpy") ? CC_LIB_MEMCPY : EM_IDENT_IS(y->name, "memmove") ? CC_LIB_MEMMOVE :
	       EM_IDENT_IS(y->name, "memset") ? CC_LIB_MEMSET : 0;
}
VPRIV void sym_name(cc *c, tbuf *b, uint32_t s);
VPRIV bool sym_quoted(cc *c, uint32_t s);
VPRIV void ref_name(cc *c, tbuf *b, uint32_t r);
VPRIV void ref_off(cc *c, tbuf *b, uint32_t r, int64_t off);
/* darwin.c */
VPRIV void darwin_text(tbuf *o, const char *t, size_t tn);
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
VPRIV void deflabel_loop(ecx *e, uint32_t l, bool forced);
VPRIV void deflabel_forced(ecx *e, uint32_t l);
/* builtin.c: atomics, LSE or ldaxr/stlxr loops */
VPRIV void at_cas(ecx *e, unsigned sz, int rs, int rt, int rn);
VPRIV void at_swp(ecx *e, unsigned sz, int rs, int rt, int rn);
VPRIV void at_ldop(ecx *e, unsigned sz, unsigned opc, int rs, int rt, int rn);
VPRIV void jump(ecx *e, uint32_t l);
VPRIV void bcond(ecx *e, int cond, uint32_t l);
VPRIV void cbz(ecx *e, bool nz, int x, int r, uint32_t l);
VPRIV void tbz(ecx *e, bool nz, int r, int bit, uint32_t l);
VPRIV uint32_t a64_words(const ins *i);
/* value.c */
VPRIV int treg(ecx *e);
VPRIV void tfree(ecx *e, int r);
VPRIV void val_free(ecx *e, const val *v);
VPRIV int64_t slot(ecx *e, uint64_t size, uint32_t align);
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
VPRIV void bvec_ret(ecx *e, qty t, int rs);
VPRIV void bvec_res(ecx *e, qty t, int64_t sl);
VPRIV void vec_small_ret(ecx *e, qty t, int rs);
VPRIV void vec_small_res(ecx *e, qty t, int64_t sl);
VPRIV bool is_sret(cc *c, qty t);
VPRIV void classify_va(ecx *e, qty t, int *nsaa, abiarg *a);
VPRIV uint32_t abi_align(cc *c, qty t);
VPRIV void classify(ecx *e, qty t, int *ngrn, int *nsaa, abiarg *a);
VPRIV void spill_live(ecx *e, spill *s);
VPRIV void unspill(ecx *e, const spill *s);
VPRIV void call_fixed(ecx *e, int fn, int a0, int a1, int a2, int64_t n);
VPRIV void finish_function(ecx *e, uint32_t s);	/* arch/a64/emit/func.c: optimise, lay out and print the records */
VPRIV bool sym_needs_got(cc *c, uint32_t ref);
/* bind.c */
static inline int64_t keep_cl(const ecx *e, int64_t mark);
static inline int X(cc *c, qty t);
static inline void rlog(ecx *e, uint32_t tag, uint32_t v);
VPRIV void note_ref(ecx *e, uint32_t ref);
VPRIV void claim_ref(ecx *e, uint32_t ref);
VPRIV bind *binding(ecx *e, uint32_t s);
VPRIV int regvar_reg(cc *c, uint32_t s);
VPRIV int64_t bind_slot(ecx *e, uint32_t s);
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
/* arch/a64/emit/unreachable.c */
VPRIV void a64_unreachable(ecx *e);
enum { OPF_ALL = 1, OPF_CALL = 2, OPF_FRAME = 4 };
VPRIV uint64_t opt_regs(ecx *e, unsigned *fl);
VPRIV bool opt_noframe(ecx *e, uint32_t n);
VPRIV lval bind_lreg(ecx *e, uint32_t s);
VPRIV val fold_int_bin(ecx *e, int op, qty ot, qty rt, i128 l, i128 r, bool *ok);
VPRIV bool em_is_cmp(int op);
/* Pure control proofs: bounded shared walk, no committed target instructions. */
VPRIV bool em_const_strcmp(cc *c, uint32_t n, i128 *value);
VPRIV int em_const_truth(ecx *e, uint32_t n, int (*constant_p)(ecx *, uint32_t));
VPRIV int a64_constant_p(ecx *e, uint32_t arg);
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
VPRIV void vr_store(ecx *e, int sz, int vr, int base, int64_t off);
VPRIV void vr_load(ecx *e, int sz, int vr, int base, int64_t off);
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
VPRIV val a64_sdivpow2(ecx *e, val a, const em_sdivpow2_recipe *r);
VPRIV bool a64_mulconst(ecx *e, qty ty, int r, i128 rhs);
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
VPRIV val inline_call(ecx *e, uint32_t s, carg *args, int n, uint32_t call);
VPRIV bool inline_object_size(ecx *e, uint32_t fn, uint32_t call, uint32_t pointer, uint32_t mode, i128 *size);
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
VPRIV bool dbg_where(ecx *e, uint32_t loc, uint32_t *file, uint32_t *line, uint32_t *col);
VPRIV void dbg_row(ecx *e, uint32_t loc);	/* -g: a line-table row at the next record */
VPRIV void dbg_remap(ecx *e, uint32_t *j, uint32_t k, uint32_t n);	/* compaction, k ascending */
/* asm.c */
VPRIV void gen_asm(ecx *e, uint32_t n);
/* func.c */
VPRIV void out_chunk(ecx *e, uint64_t key, size_t start);
VPRIV void sym_header(ecx *e, tbuf *b, uint32_t s, bool fn, const char *defsec, uint32_t align);
VPRIV void emit_function(ecx *e, uint32_t s);
/* data.c */
VPRIV void emit_object(ecx *e, uint32_t s);
VPRIV void emit_string(ecx *e, uint32_t i);
/* arch/wasm/emit/ */
VPRIV void wasm_emit_function(ecx *e, uint32_t s);
VPRIV void wasm_ecx_free(ecx *e);
VPRIV void wk_function(ecx *e, uint32_t s);
VPRIV void wk_ecx_free(ecx *e);
VPRIV bool vw_x64(ecx *e, uint32_t s, const char **why);
VPRIV bool vw_a64(ecx *e, uint32_t s, const char **why);	/* emit/vir/a64.c: the built VIR as arm64 records */
VPRIV bool vw_try(ecx *e, uint32_t s);	/* emit/vir/: s through VIR (VCC_VIR); false: emit it as before */
VPRIV bool vw_enabled(const cc_opts *o);	/* VIR lowers functions (VCC_VIR with l, -O2) */
VPRIV void vw_wk_ecx_free(ecx *e);
VPRIV void wasm_data_ref(ecx *e, uint32_t ref);
VPRIV void wasm_header(cc *c, tbuf *hdr, ecx *ctx, int nthr);
VPRIV void wasm_tail(cc *c, tbuf *t, _Atomic uint8_t *claimed);
/* prepass.c */
VPRIV bool is_aconst(cc *c, uint32_t n);
VPRIV uint32_t aconst_idx(cc *c, uint32_t n);
VPRIV bool sve_codegen_ok(cc *c);
VPRIV bool bitint_codegen_ok(cc *c);
VPRIV void ensure_walked(emitter *E, cc *c, uint32_t s);
VPRIV uint8_t *fn_once(cc *c);
#ifndef WK_INLINE_ONCE
#define WK_INLINE_ONCE 4000 /* under VIR: a static function with one call */
#endif
#ifndef WK_INLINE_VIR
#define WK_INLINE_VIR 150	/* under VIR: AST nodes of a callee with what it inlines (fn_isize) */
#endif
#ifndef WK_INLINE_CONST_FACTOR
#define WK_INLINE_CONST_FACTOR 4
#endif
VPRIV void fn_isize(emitter *E, cc *c, uint32_t lim);	/* E->isz, E->scc: VIR's inlining sizes */
VPRIV bool inl_constarg(const emitter *E, cc *c, uint32_t callee, uint32_t call);
VPRIV bool inl_cand(emitter *E, cc *c, uint32_t s);	/* VIR could inline a call to s (size aside) */	/* per function: 1 when its one reference in the TU is a direct call */
VPRIV int u32_cmp(const void *a, const void *b);
/* tu.c */
VPRIV void enqueue(emitter *E, uint32_t item);

/* ---------------- types (the few predicates the emitter needs) ---------------- */

static inline qty prim(int k) { return QT(k, 0); }
static inline bool k_int(int k) { return (k >= TY_BOOL && k <= TY_UINT128) || k == TY_BITINT || k == TY_UBITINT || k == TY_ENUM; }
static inline bool em_bigint(cc *c, qty t) { int k = KIND(c, t); return k == TY_BITINT || k == TY_UBITINT; }
static inline bool is_integer(cc *c, qty t) { return k_int(KIND(c, t)); }
static inline bool em_bitint(cc *c, qty t)
{
	return ((KIND(c, t) >= TY_CHAR && KIND(c, t) <= TY_UINT128) || em_bigint(c, t)) &&
	       !(T(c, t)->flags & TF_ATOMIC) && T(c, t)->n;
}
static inline bool is_ptr(cc *c, qty t) { return KIND(c, t) == TY_PTR; }
static inline bool is_flt(cc *c, qty t) { int k = KIND(c, t); return k >= TY_BFLOAT16 && k <= TY_FLOAT128; }
static inline bool is_record(cc *c, qty t) { int k = KIND(c, t); return k == TY_STRUCT || k == TY_UNION; }
static inline bool is_array(cc *c, qty t) { return KIND(c, t) == TY_ARRAY; }
static inline bool is_func(cc *c, qty t) { return KIND(c, t) == TY_FUNC; }
static inline bool is_void(cc *c, qty t) { return KIND(c, t) == TY_VOID; }
static inline bool is_vec(cc *c, qty t) { return KIND(c, t) == TY_VECTOR; }
static inline bool is_bvec(cc *c, qty t) { return is_vec(c, t) && KIND(c, T(c, t)->base) == TY_BOOL; }
/* passed and returned like a record (vcc's own convention for vectors) */
static inline bool is_recabi(cc *c, qty t) { return is_record(c, t) || is_vec(c, t) || em_bigint(c, t); }
/* an SVE sizeless value: address-valued, in a frame slot of the largest vector length (a predicate's is 32 bytes) */
static inline bool is_sve(cc *c, qty t) { return KIND(c, t) == TY_SVE; }
static inline bool is_svpred(cc *c, qty t) { return is_sve(c, t) && T(c, t)->size == 32; }
static inline bool is_agg(cc *c, qty t) { return is_record(c, t) || is_array(c, t) || is_vec(c, t) || is_sve(c, t) || em_bigint(c, t); }
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

static inline bool direct(const ecx *e) { return e->E->o->emit == 2 && !e->E->objtext; }

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
static inline int64_t keep_cl(const ecx *e, int64_t mark)
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

#include "../arch/a64/emit/a64impl.h"

#endif
