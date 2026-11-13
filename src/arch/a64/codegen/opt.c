/* vcc code generator: record-level clean-ups once a function is generated */
#include "../../../codegen/codegen-internal.h"

/*
 * Register liveness over the function's records (basic blocks split at
 * labels and after branches), then:
 *
 *   - forward copy propagation: `mov t, v` followed by reads of t, until t
 *     dies, becomes those reads of v (t a temporary, v not written meanwhile);
 *   - backward: `op t, ...; ...; mov v, t` with t dead after becomes
 *     `op v, ...` (nothing between reads or writes v, or reads t);
 *   - a mov to a dead register goes;
 *   - a temporary spilled around a call (A64_SPILL_RECORD) and dead after the
 *     restore is neither stored nor restored.
 *
 * The emitter reads a register variable (CODEGEN_BINDING_LOCAL_REGISTER) into a fresh temporary and
 * stores one through a mov; these undo most of that.  Records the pass
 * cannot see into (basic asm, asm goto, encoded words) read every register,
 * so nothing moves across them; extended asm reads and writes just its
 * operands and clobbers (A64_RAW_HAS_REGISTER_MASKS).  A function with an indirect branch is
 * left as it is.  Liveness is only ever made larger after a rewrite, never smaller,
 * so it stays an over-approximation.
 */

typedef uint64_t record_register_mask;
#define RB(r) ((record_register_mask)1 << (r))
#define GPRS (RB(31) - 1)						/* x0-x30 */
#define TEMPS ((RB(16) - RB(9)) | (RB(29) - RB(19)))			/* x9-x15, x19-x28 */
#define COPYOK ((RB(16) - 1) | (RB(29) - RB(19)))			/* x0-x15, x19-x28 */
#define CALLW ((RB(19) - 1) | RB(30))					/* x0-x18, lr */
#define DELETED 0xff

/* operand roles */
enum { RD_R = 1, RN_R = 2, RM_R = 4, RA_R = 8, RD_W = 16, RA_W = 32, RN_W = 64, OP_ALL = 128, OP_CALL = 256, OP_ASM = 512,
       OP_RET = 2048, OP_TLV = 4096, OP_TAIL = 8192 };
#define OP_SLOW (OP_ALL | OP_CALL | OP_ASM | OP_RET | OP_TLV | OP_TAIL)	/* reads()/writes() beyond the operand fields */

/* the function's raw text, for the registers of inline asm (A64_RAW_HAS_REGISTER_MASKS) */
static _Thread_local const char *rawp;
static _Thread_local record_register_mask retreads;
/* control */
enum { BR_NONE, BR_UNCOND, BR_COND, BR_END, BR_INDIRECT };

static unsigned roles_sw(const a64_record *i)
{
	switch (i->opcode) {
	case A64_OP_ADD: case A64_OP_SUB: case A64_OP_ADDS: case A64_OP_SUBS: case A64_OP_AND: case A64_OP_ORR: case A64_OP_EOR:
	case A64_OP_ANDS: case A64_OP_BIC: case A64_OP_ORN: case A64_OP_ADC: case A64_OP_SBC: case A64_OP_ADCS: case A64_OP_SBCS:
	case A64_OP_SDIV: case A64_OP_UDIV: case A64_OP_SMULH: case A64_OP_UMULH: case A64_OP_LSLV: case A64_OP_LSRV: case A64_OP_ASRV:
	case A64_OP_CSEL: case A64_OP_CSINC: case A64_OP_CSINV: case A64_OP_CSNEG:
		return RN_R | RM_R | RD_W;
	case A64_OP_ADDI: case A64_OP_SUBI: case A64_OP_ADDSI: case A64_OP_SUBSI: case A64_OP_ANDI: case A64_OP_ORRI: case A64_OP_EORI: case A64_OP_ANDSI:
	case A64_OP_MOV: case A64_OP_SBFM: case A64_OP_UBFM:
	case A64_OP_CLZ: case A64_OP_CLS: case A64_OP_RBIT: case A64_OP_REV: case A64_OP_REV16: case A64_OP_REV32:
	case A64_OP_LD: case A64_OP_ADDLO: case A64_OP_LDLO:
		return RN_R | RD_W;
	case A64_OP_LDX:
		return RN_R | RM_R | RD_W;
	case A64_OP_STX:
		return RD_R | RN_R | RM_R;
	case A64_OP_BFM:
		return RD_R | RN_R | RD_W;
	case A64_OP_MADD: case A64_OP_MSUB: case A64_OP_SMADDL: case A64_OP_UMADDL:
		return RN_R | RM_R | RA_R | RD_W;
	case A64_OP_MOVI: case A64_OP_ADRP: case A64_OP_LDGOT: case A64_OP_ADR: case A64_OP_MRS:
		return RD_W;
	case A64_OP_ST: case A64_OP_STLO:
		return RD_R | RN_R;
	case A64_OP_LDP:
		return RN_R | RD_W | RA_W | (i->condition ? RN_W : 0);
	case A64_OP_STP:
		return RD_R | RA_R | RN_R | (i->condition ? RN_W : 0);
	case A64_OP_CBZ: case A64_OP_CBNZ: case A64_OP_TBZ: case A64_OP_TBNZ:
		return RD_R;
	case A64_OP_PRFM:
	case A64_OP_LDF: case A64_OP_STF:	/* (rd is an FP register) */
		return RN_R;
	case A64_OP_BL:
		return OP_CALL;
	case A64_OP_TLV:	/* Darwin's TLV thunk keeps every register but x0 (the address); blr x16 */
		return OP_TLV;
	case A64_OP_BLR:
		return RN_R | OP_CALL;
	case A64_OP_BR:
		return RN_R;
	case A64_OP_RET:
		return OP_RET;	/* reads x0, x1 */
	case A64_OP_TAIL:
		if (i->condition == 4)
			return OP_RET;	/* (VIR's return in the body: the epilogue and ret) */
		return OP_TAIL | (i->condition == 3 ? RN_R : 0);	/* outgoing x0-x7 survive until the sibling branch */
	case A64_OP_LABEL: case A64_OP_B: case A64_OP_BC: case A64_OP_NOP:
	case A64_OP_BRK:	/* a trap (BUG, WARN, __builtin_trap): the handler may print registers, needs none */
		return 0;
	case A64_OP_RAW:
		return i->condition == A64_RAW_HAS_REGISTER_MASKS ? OP_ASM : OP_ALL;
	default:	/* A64_OP_RAW, A64_OP_WORD, I_MOVSP: unknown registers */
		return OP_ALL;
	}
}

/* roles and control by opcode, from the switches above; OP_VAR: depends on more than the opcode */
enum { OP_VAR = 1024 };
static uint16_t RT[256];
static uint8_t BT[256];
static uint8_t XT[256];	/* exts: an extension xredundant() may find redundant */
/* register field -> its bit (0: zr/sp/A64_REGISTER_FRAME and other non-registers above 63) */
static record_register_mask RBT[256];

static int brkind_sw(const a64_record *i);
__attribute__((constructor)) static void opt_tables(void)
{
	for (unsigned op = 0; op < 256; op++) {
		a64_record x = { .opcode = (uint8_t)op };

		RT[op] = (uint16_t)roles_sw(&x);
		BT[op] = (uint8_t)brkind_sw(&x);
	}
	RT[A64_OP_LDP] = RT[A64_OP_STP] = RT[A64_OP_RAW] = RT[A64_OP_TAIL] = OP_VAR;
	XT[A64_OP_UBFM] = XT[A64_OP_SBFM] = XT[A64_OP_MOV] = XT[A64_OP_ANDI] = 1;
	for (unsigned r = 0; r < 64; r++)
		RBT[r] = RB(r);
}

static inline unsigned roles(const a64_record *i)
{
	unsigned r = RT[i->opcode];

	return r & OP_VAR ? roles_sw(i) : r;
}

static inline int brkind(const a64_record *i) { return BT[i->opcode]; }

static int brkind_sw(const a64_record *i)
{
	switch (i->opcode) {
	case A64_OP_B: return BR_UNCOND;
	case A64_OP_BC: case A64_OP_CBZ: case A64_OP_CBNZ: case A64_OP_TBZ: case A64_OP_TBNZ: return BR_COND;
	case A64_OP_RET: case A64_OP_TAIL: case A64_OP_JT: return BR_END;
	case A64_OP_BR: return BR_INDIRECT;
	default: return BR_NONE;
	}
}

static inline record_register_mask m(int r) { return r < 64 ? RB(r) : 0; }

/* reads(), writes() of calls, asm, ret and unknown records */
__attribute__((noinline)) static record_register_mask reads_slow(const a64_record *i, unsigned ro)
{
	record_register_mask s = 0;

	if (ro & OP_ALL)
		return GPRS;
	if (ro & OP_ASM) {
		memcpy(&s, rawp + i->offset + i->reference, sizeof(s));
		return s & GPRS;
	}
	if (ro & RD_R) s |= m(i->rd);
	if (ro & RN_R) s |= m(i->rn);
	if (ro & RM_R) s |= m(i->rm);
	if (ro & RA_R) s |= m(i->ra);
	if (ro & OP_CALL)	/* x0-x8: arguments, indirect result (VIR's: the arguments it passes) */
		s |= i->condition == A64_CALL_ARGUMENT_REGISTERS && (i->opcode == A64_OP_BL || i->opcode == A64_OP_BLR) ? (record_register_mask)i->shift : RB(9) - 1;
	if (ro & OP_RET)
		s |= retreads;
	if (ro & OP_TAIL)
		s |= RB(8) - 1;
	return s;
}

__attribute__((noinline)) static record_register_mask writes_slow(const a64_record *i, unsigned ro)
{
	record_register_mask s = 0;

	if (ro & OP_ASM) {
		memcpy(&s, rawp + i->offset + i->reference + sizeof(s), sizeof(s));
		return s & GPRS;
	}
	if (ro & RD_W) s |= m(i->rd);
	if (ro & RA_W) s |= m(i->ra);
	if (ro & RN_W) s |= m(i->rn);
	if (ro & OP_CALL)
		s |= CALLW;
	if (ro & OP_TLV)
		s |= i->condition ? RB(0) | RB(1) | RB(30) : RB(0) | RB(16) | RB(30);
	return s;
}

static inline record_register_mask reads(const a64_record *i, unsigned ro)
{
	if (ro & OP_SLOW)
		return reads_slow(i, ro);
	return (ro & RD_R ? RBT[i->rd] : 0) | (ro & RN_R ? RBT[i->rn] : 0) | (ro & RM_R ? RBT[i->rm] : 0) |
	       (ro & RA_R ? RBT[i->ra] : 0);
}

static inline record_register_mask writes(const a64_record *i, unsigned ro)
{
	if (ro & OP_SLOW)
		return writes_slow(i, ro);
	return (ro & RD_W ? RBT[i->rd] : 0) | (ro & RA_W ? RBT[i->ra] : 0) | (ro & RN_W ? RBT[i->rn] : 0);
}

/* scratch, per thread: grows to the largest function */
static _Thread_local struct {
	record_register_mask *la;		/* per record: live after it */
	record_register_mask *rr, *ww;		/* per record: registers read, written (OP_ALL: rr GPRS), kept up to date by the rewrites */
	uint16_t *ro;		/* per record: roles() */
	uint32_t *blk;		/* per record: its block */
	uint32_t *bs, *be;	/* per block: first and last record */
	int32_t *s0, *s1;	/* per block: successors (-1 none) */
	record_register_mask *in, *out, *use, *def;
	uint32_t *lpos;		/* per label: its block + 1 (UINT32_MAX: jumped to, not placed) */
	uint32_t cap, lcap;
} W;

static void grow(uint32_t n, uint32_t nl)
{
	if (n > W.cap) {
		uint32_t c = n * 2;

		W.la = realloc(W.la, c * sizeof(record_register_mask));
		W.rr = realloc(W.rr, c * sizeof(record_register_mask));
		W.ww = realloc(W.ww, c * sizeof(record_register_mask));
		W.blk = realloc(W.blk, c * sizeof(uint32_t));
		W.bs = realloc(W.bs, c * sizeof(uint32_t));
		W.be = realloc(W.be, c * sizeof(uint32_t));
		W.s0 = realloc(W.s0, c * sizeof(int32_t));
		W.s1 = realloc(W.s1, c * sizeof(int32_t));
		W.in = realloc(W.in, c * sizeof(record_register_mask));
		W.out = realloc(W.out, c * sizeof(record_register_mask));
		W.use = realloc(W.use, c * sizeof(record_register_mask));
		W.def = realloc(W.def, c * sizeof(record_register_mask));
		W.ro = realloc(W.ro, c * sizeof(uint16_t));
		if (!W.ro || !W.la || !W.rr || !W.ww || !W.blk || !W.bs || !W.be || !W.s0 || !W.s1 || !W.in || !W.out || !W.use || !W.def)
			abort();
		W.cap = c;
	}
	if (nl > W.lcap) {
		W.lpos = realloc(W.lpos, (size_t)nl * 2 * sizeof(uint32_t));
		if (!W.lpos)
			abort();
		W.lcap = nl * 2;
	}
}

/* live-after for every record; false if the control flow is not known */
static bool liveness(codegen_context *e)
{
	const a64_record *in = e->records;
	uint32_t n = e->record_count, nb = 0;
	bool changed;

	grow(n + 1, e->label_count + 1);
	memset(W.lpos, 0, (size_t)e->label_count * sizeof(uint32_t));
	/*
	 * Each record's roles and registers; labels something jumps to (+ asm,
	 * label addresses), as the others (scopes, inlining) split nothing.
	 */
	{
		record_register_mask *rr = W.rr, *ww = W.ww;
		uint16_t *rop = W.ro;
		uint32_t *lpos = W.lpos;

		for (uint32_t k = 0; k < n; k++) {
			const a64_record *i = &in[k];
			unsigned ro = roles(i), bk = BT[i->opcode];

			rop[k] = (uint16_t)ro;
			rr[k] = reads(i, ro), ww[k] = writes(i, ro);
			if (bk == BR_INDIRECT)
				return false;
			if (bk - BR_UNCOND <= BR_COND - BR_UNCOND || (i->opcode == A64_OP_LABEL && (e->label_reference_counts[i->label] & CODEGEN_LABEL_EXTERNAL)))
				lpos[i->label] = UINT32_MAX;
		}
	}
	for (uint32_t k = 0, prev = BR_UNCOND; k < n; k++) {
		const a64_record *i = &in[k];

		if (prev != BR_NONE || (i->opcode == A64_OP_LABEL && W.lpos[i->label])) {
			if (nb)
				W.be[nb - 1] = k - 1;
			W.bs[nb++] = k;
		}
		W.blk[k] = nb - 1;
		if (i->opcode == A64_OP_LABEL)
			W.lpos[i->label] = nb;	/* the label's block + 1 */
		prev = BT[i->opcode];
	}
	if (!n)
		return false;
	W.be[nb - 1] = n - 1;
	for (uint32_t b = 0; b < nb; b++) {
		const a64_record *l = &in[W.be[b]];
		int k = brkind(l);

		W.s0[b] = W.s1[b] = -1;
		if (k == BR_NONE || k == BR_COND)
			W.s0[b] = b + 1 < nb ? (int32_t)b + 1 : -1;
		if (k == BR_UNCOND || k == BR_COND) {
			if (W.lpos[l->label] == UINT32_MAX)
				return false;	/* a branch to a label not in the records */
			W.s1[b] = (int32_t)W.lpos[l->label] - 1;
		}
		{
			record_register_mask use = 0, def = 0;
			const record_register_mask *rr = W.rr, *ww = W.ww;

			for (uint32_t j = W.be[b] + 1; j-- > W.bs[b];) {
				use = (use & ~ww[j]) | rr[j];
				def |= ww[j];
			}
			W.use[b] = W.in[b] = use, W.def[b] = def;
		}
		W.out[b] = 0;
	}
	do {
		changed = false;
		for (uint32_t b = nb; b-- > 0;) {
			record_register_mask o = 0, i;

			if (b == nb - 1 && brkind(&in[W.be[b]]) != BR_UNCOND)
				o |= retreads;	/* falls into the epilogue: the return value */
			if (W.s0[b] >= 0)
				o |= W.in[W.s0[b]];
			if (W.s1[b] >= 0)
				o |= W.in[W.s1[b]];
			i = W.use[b] | (o & ~W.def[b]);
			if (o != W.out[b] || i != W.in[b])
				changed = true;
			W.out[b] = o, W.in[b] = i;
		}
	} while (changed);
	for (uint32_t b = 0; b < nb; b++) {
		record_register_mask l = W.out[b], *la = W.la;
		const record_register_mask *rr = W.rr, *ww = W.ww;

		for (uint32_t j = W.be[b] + 1; j-- > W.bs[b];) {
			la[j] = l;
			l = (l & ~ww[j]) | rr[j];
		}
	}
	return true;
}

/* replace reads of t by v in record j: false if t is read where it is also written */
static bool can_rewrite(const a64_record *i, unsigned ro, int t)
{
	if ((ro & RD_R) && i->rd == t && (ro & RD_W))
		return false;	/* bfm t, ...: reads and writes rd */
	if ((ro & RN_R) && i->rn == t && (ro & RN_W))
		return false;	/* writeback */
	if ((ro & RA_R) && i->ra == t && (ro & RA_W))
		return false;
	return true;
}

static void rewrite(a64_record *i, unsigned ro, int t, int v)
{
	if ((ro & RD_R) && i->rd == t) i->rd = (uint8_t)v;
	if ((ro & RN_R) && i->rn == t) i->rn = (uint8_t)v;
	if ((ro & RM_R) && i->rm == t) i->rm = (uint8_t)v;
	if ((ro & RA_R) && i->ra == t) i->ra = (uint8_t)v;
}

static bool is_copy(const a64_record *i)
{
	return i->opcode == A64_OP_MOV && i->size == 1 && i->rd < 64 && i->rn < 64 && i->rd != i->rn &&
	       (COPYOK & RB(i->rd)) && (COPYOK & RB(i->rn));
}

/* Operand policy for the shared transaction. Asm registers live in its text,
 * while tied read/write operands cannot be redirected independently.
 */
static bool a64_copy_rewrite(void *ctx, uint32_t j, uint8_t t, uint8_t v, bool apply)
{
	codegen_context *e = ctx;
	unsigned ro = W.ro[j];

	if (!apply)
		return !(ro & OP_ASM) && can_rewrite(&e->records[j], ro, t);
	rewrite(&e->records[j], ro, t, v);
	return true;
}

/* mov v, t at k with t dead after: the instruction that made t makes v */
static bool backward(codegen_context *e, uint32_t k)
{
	a64_record *in = e->records;
	int v = in[k].rd, t = in[k].rn;
	uint32_t b = W.blk[k];

	if (!(TEMPS & RB(t)) || (W.la[k] & RB(t)))
		return false;
	for (uint32_t j = k; j-- > W.bs[b];) {
		a64_record *i = &in[j];
		unsigned ro;
		record_register_mask r, w;

		if (i->opcode == DELETED)
			continue;
		ro = W.ro[j];
		if (ro & (OP_ALL | OP_CALL))
			return false;
		r = W.rr[j], w = W.ww[j];
		if (w & RB(t)) {
			if (!(ro & RD_W) || i->rd != t || (ro & (RD_R | RA_W | RN_W)) || (w & ~RB(t)))
				return false;
			if (w & RB(v))
				return false;
			i->rd = (uint8_t)v;
			W.ww[j] = RB(v);
			for (uint32_t q = j; q < k; q++)
				W.la[q] |= RB(v);
			in[k].opcode = DELETED;
			return true;
		}
		if ((r & RB(t)) || ((r | w) & RB(v)))
			return false;
	}
	return false;
}

/* the clean-ups above; then the deleted records go */
/* ---------------- stack slots ---------------- */

/*
 * A cell is a frame range [off, off+size) of one slot() allocation (the
 * records' CODEGEN_SLOT_TAG) that every access reaches with an A64_OP_LD/A64_OP_ST of exactly
 * that range: no other access of the allocation overlaps it, nor does an
 * untagged one (volatile accesses stay untagged).  Another allocation at the
 * same offsets lives at another time.  Per block:
 *
 *   - a load whose value is still in a register (stored from it, or loaded
 *     into it, with the register not written since) becomes a mov or the
 *     load's extension of that register;
 *   - a store overwritten in the block before any load goes;
 *
 * and a cell that is never loaded loses all its stores.  If an address
 * points into the allocation (or just past its end: CODEGEN_SLOT_TAG_SECOND) the cell is
 * escaped: a call, inline asm or a store elsewhere may write it (forwarding
 * stops there) and anything may read it (no store goes).  An untagged
 * address turns the pass off for the function.
 */
static _Thread_local struct {
	record_slot_cells cells;
	int32_t *rc;		/* per record: its cell, or -1 */
	uint32_t caprc;
	record_slot_values values;
} SL;

static void *grow_to(void *p, uint32_t *cap, uint32_t n, size_t sz)
{
	if (n <= *cap)
		return p;
	*cap = n * 2;
	p = realloc(p, (size_t)*cap * sz);
	if (!p)
		abort();
	return p;
}

/* the load at i, its value taken from register r instead (the slot holds r's low bits) */
static void load_from_reg(a64_record *i, int r)
{
	int sz = i->size, ex = i->condition;
	a64_record n = { .rd = i->rd, .rn = (uint8_t)r };

	if (r == ZR) {
		n.opcode = A64_OP_MOVI, n.size = 1, n.rn = 0, n.immediate = 0;
	} else if (sz == 3) {
		n.opcode = A64_OP_MOV, n.size = 1;
	} else if (sz == 2 && ex != 1) {
		n.opcode = A64_OP_MOV, n.size = 0;	/* ldr w: zero-extended */
	} else {
		n.opcode = ex ? A64_OP_SBFM : A64_OP_UBFM;
		n.size = ex == 2 ? 0 : 1;	/* 2: sign-extended into w */
		n.immediate = 0, n.shift = (uint8_t)(sz == 0 ? 7 : sz == 1 ? 15 : 31);
	}
	*i = n;
}

static void slots(codegen_context *e)
{
	a64_record *in = e->records;
	uint32_t n = e->record_count, na = e->slot_allocation_count + 1, ncell, nb, nst = 0;
	uint32_t ver[32] = { 0 }, mver = 0; /* pointer/call memory version */
	record_slot_cells *cells = &SL.cells;

	if (e->slot_allocation_count > 0xffff || !n || !record_slot_begin(cells, na))
		return;
	for (uint32_t k = 0; k < n; k++) {
		const a64_record *i = &in[k];
		uint32_t id;

		if (i->rn != A64_REGISTER_FRAME || i->opcode == DELETED)
			continue;
		id = CODEGEN_SLOT_TAG(i);
		if (i->opcode == A64_OP_LD || i->opcode == A64_OP_ST) {
			record_slot_collect(cells, id, i->immediate, 1u << i->size);
		} else if (i->opcode == A64_OP_LDF || i->opcode == A64_OP_STF) {
			/* an FP register's access: no value to track, so its object is one the pass leaves alone */
			record_slot_collect(cells, id, i->immediate, 1u << i->size);
			if (id)
				record_slot_escape(cells, id);
		} else {
			if (!id)
				return; /* an address vcc cannot place */
			record_slot_escape(cells, id);
			if (CODEGEN_SLOT_TAG_SECOND(i) < na)
				record_slot_escape(cells, CODEGEN_SLOT_TAG_SECOND(i));
		}
	}
	if (!record_slot_finish(cells))
		return;
	ncell = cells->count;
	SL.rc = grow_to(SL.rc, &SL.caprc, n, sizeof(int32_t));
	for (uint32_t k = 0; k < n; k++) {
		const a64_record *i = &in[k];
		uint32_t id, cell;

		SL.rc[k] = -1;
		if (i->rn != A64_REGISTER_FRAME || (i->opcode != A64_OP_LD && i->opcode != A64_OP_ST) || !(id = CODEGEN_SLOT_TAG(i)))
			continue;
		cell = record_slot_find(cells, id, i->immediate, 1u << i->size);
		if (!(cells->flags[cell] & RECORD_SLOT_BAD))
			SL.rc[k] = (int32_t)cell;
	}
	if (!record_slot_values_begin(&SL.values, ncell))
		return;
	nb = W.blk[n - 1] + 1;
	for (uint32_t b = 0; b < nb; b++) {
		uint32_t gen = b + 1;

		for (uint32_t k = W.bs[b]; k <= W.be[b]; k++) {
			a64_record *i = &in[k];
			int32_t ci = SL.rc[k];
			record_slot_value *a = ci >= 0 ? record_slot_value_get(&SL.values, (uint32_t)ci, gen, i->opcode == A64_OP_LD) : NULL;
			bool esc = ci >= 0 && (cells->flags[ci] & RECORD_SLOT_ESCAPED);
			record_register_mask w;

			if (i->opcode == DELETED)
				continue;
			if (a && i->opcode == A64_OP_LD) {
				uint32_t version = a->value < ZR ? ver[a->value] : a->version;

				if (record_slot_value_is_available(a, version, mver, esc)) {
					int r = (int)a->value;

					if (r != ZR)
						for (uint32_t q = a->position; q < k; q++)
							W.la[q] |= RB(r);
					a->position = k;	/* (r is live up to here now) */
					load_from_reg(i, r);
					W.ro[k] = (uint16_t)roles(i);
					W.rr[k] = reads(i, W.ro[k]);
					SL.rc[k] = -1;
					if (i->opcode == A64_OP_MOV && i->size == 1 && i->rd == i->rn) {
						i->opcode = DELETED;
						continue;
					}
				}
			}
			/* (a load made a mov writes the same register); OP_ALL may write any */
			w = (W.rr[k] == GPRS ? GPRS : W.ww[k]) & GPRS;
			for (; w; w &= w - 1)
				ver[__builtin_ctzll(w)]++;
			if (SL.rc[k] < 0) {
				if (i->opcode == A64_OP_ST || i->opcode == A64_OP_STX || i->opcode == A64_OP_STLO || i->opcode == A64_OP_STP || i->opcode == A64_OP_STF || i->opcode == A64_OP_BL || i->opcode == A64_OP_BLR ||
				    i->opcode == A64_OP_TLV || i->opcode == A64_OP_RAW || W.rr[k] == GPRS)
					mver++;	/* may write an escaped cell */
				continue;
			}
			uint32_t overwritten = record_slot_value_note(a, i->opcode == A64_OP_LD, esc, i->rd,
				i->rd == ZR ? 0 : ver[i->rd], mver, k);

			if (i->opcode == A64_OP_ST)
				SL.rc[nst++] = (int32_t)k; /* rc of earlier records is done with */
			if (overwritten != RECORD_SLOT_NO_STORE)
				in[overwritten].opcode = DELETED;
		}
	}
	/* a cell no load reads: its stores go */
	for (uint32_t q = 0; q < nst; q++) {
		a64_record *i = &in[SL.rc[q]];
		uint32_t cell = CODEGEN_SLOT_TAG(i);

		if (i->opcode != A64_OP_ST)
			continue;	/* (deleted meanwhile) */
		cell = record_slot_find(cells, cell, i->immediate, 1u << i->size);
		if (record_slot_value_is_dead(&SL.values, cell, cells->flags[cell] & RECORD_SLOT_ESCAPED))
			i->opcode = DELETED;
	}
}

/* ---------------- known extensions ---------------- */

/*
 * Forward over the blocks, meeting at joins, per register: z, the value
 * fits in its low z bits (the rest are zero), and s, it is the sign
 * extension of its low s bits (64: nothing known).  A zero or sign
 * extension (ubfm/sbfm #0, a 32-bit mov, an and with a low mask) of a
 * register that already has that form becomes a 64-bit mov, which the copy
 * clean-ups then remove: the emitter keeps every value extended to 64 bits
 * after each load, compare and 32-bit operation, often twice.
 *
 * Only x0-x15 and x19-x28 carry facts (x16/x17 are the printer's and the
 * linker's scratch).  A function's entry, a block nothing branches to and a
 * label that inline asm or a label address may reach (CODEGEN_LABEL_EXTERNAL) start
 * with nothing known.
 */
#define XU 64
typedef struct xf { uint8_t z[64]; } xf;	/* z[r], then s[r] at z[32 + r] */
#define XS(f) ((f)->z + 32)

static _Thread_local struct {
	xf *in;			/* per block */
	uint8_t *vis;		/* per block: 0 in not set, 1 set, 2 changed since the block was gone through */
	uint8_t *red;		/* per record: a redundant extension */
	uint32_t cap, capr;
} XF;

static inline unsigned xz(const xf *f, int r) { return r == ZR ? 0 : r < 31 ? f->z[r] : XU; }
static inline unsigned xs(const xf *f, int r) { return r == ZR ? 1 : r < 31 ? XS(f)[r] : XU; }
static inline unsigned umin(unsigned a, unsigned b) { return a < b ? a : b; }
static inline unsigned umax(unsigned a, unsigned b) { return a > b ? a : b; }
static inline unsigned zbits(uint64_t v) { return v ? 64 - (unsigned)__builtin_clzll(v) : 0; }
static inline unsigned sbits(int64_t v) { return 64 - (unsigned)__builtin_clrsbll(v); }

/* the record's result: its z and s, or false if nothing is known of it */
static bool xresult(const xf *f, const a64_record *i, unsigned *zo, unsigned *so)
{
	int rn = i->rn, rm = i->rm, x = i->size;
	unsigned z = XU, s = XU, w = x ? 64 : 32;
	unsigned zn = xz(f, rn), sn = xs(f, rn);

	switch (i->opcode) {
	case A64_OP_LD: case A64_OP_LDX: case A64_OP_LDLO:
		if (i->size == 3)
			return false;
		if (i->condition == 0)
			z = 8u << i->size;
		else if (i->condition == 1)
			s = 8u << i->size;
		else
			z = 32;	/* sign-extended into w */
		goto done;
	case A64_OP_MOVI: {
		uint64_t v = x ? (uint64_t)i->immediate : (uint32_t)i->immediate;

		z = zbits(v), s = sbits((int64_t)v);
		goto done;
	}
	case A64_OP_MOV:
		z = zn, s = sn;
		break;
	case A64_OP_CSINC:
		if (rn != ZR || rm != ZR)
			return x ? false : (*zo = 32, *so = 33, true);
		z = 1;	/* cset */
		break;
	case A64_OP_CSINV:
		if (rn != ZR || rm != ZR)
			return x ? false : (*zo = 32, *so = 33, true);
		s = 1;	/* csetm */
		break;
	case A64_OP_CSEL:
		z = umax(zn, xz(f, rm)), s = umax(sn, xs(f, rm));
		break;
	case A64_OP_UBFM: {
		unsigned r = (unsigned)i->immediate, h = i->shift;

		if (h >= r)	/* bits r..h to 0 (lsr, ubfx) */
			z = umin(h - r + 1, zn > r ? zn - r : 0);
		else		/* bits 0..h to w-r (lsl, ubfiz) */
			z = umin(h + 1, zn) + (w - r);
		break;
	}
	case A64_OP_SBFM: {
		unsigned r = (unsigned)i->immediate, h = i->shift;

		if (r == 0) {	/* sxt: bits 0..h */
			if (zn < h + 1)
				z = zn;
			else if (x)
				s = umin(h + 1, sn);
		} else if (h == w - 1) {	/* asr */
			if (zn < w)	/* (not negative in w bits: an lsr) */
				z = zn > r ? zn - r : 0;
			else if (x)
				s = sn > r ? sn - r : 1;
		}
		break;
	}
	case A64_OP_ANDI: {
		uint64_t v = x ? (uint64_t)i->immediate : (uint32_t)i->immediate;

		z = umin(zn, zbits(v));
		if (x && (int64_t)v < 0 && zbits(~v) < sn)
			s = sn;	/* the mask keeps every bit from the sign up */
		break;
	}
	case A64_OP_AND: case A64_OP_ORR: case A64_OP_EOR:
		if (i->shift)
			break;
		if (i->opcode == A64_OP_AND)
			z = umin(zn, xz(f, rm));
		else
			z = umax(zn, xz(f, rm));
		s = umax(sn, xs(f, rm));
		break;
	case A64_OP_ORRI: case A64_OP_EORI: {
		uint64_t v = x ? (uint64_t)i->immediate : (uint32_t)i->immediate;

		z = umax(zn, zbits(v)), s = umax(sn, sbits((int64_t)v));
		break;
	}
	case A64_OP_ADD:
		if (i->shift || i->condition)	/* (shifted, or rm extended, cond 8 + option: nothing known) */
			break;
		{
			unsigned a = umax(zn, xz(f, rm)), b = umax(sn, xs(f, rm));

			z = a < XU ? a + 1 : XU, s = b < XU ? b + 1 : XU;
		}
		break;
	case A64_OP_ADDI: case A64_OP_SUBI: {
		int64_t v = i->immediate;

		if (v == INT64_MIN)
			break;
		if (i->opcode == A64_OP_SUBI)
			v = -v;
		if (v >= 0) {
			unsigned a = umax(zn, zbits((uint64_t)v));

			z = a < XU ? a + 1 : XU;
		}
		if (x) {
			unsigned b = umax(sn, sbits(v));

			s = b < XU ? b + 1 : XU;
		}
		break;
	}
	case A64_OP_LSRV:
		z = zn;
		break;
	case A64_OP_ASRV:
		z = zn;
		if (x)
			s = sn;
		break;
	case A64_OP_UDIV:
		z = zn;
		break;
	case A64_OP_CLZ: case A64_OP_CLS:
		z = 7;
		break;
	default:
		if (x || i->opcode == A64_OP_ADRP || i->opcode == A64_OP_ADR || i->opcode == A64_OP_LDGOT || i->opcode == A64_OP_MRS || i->opcode == A64_OP_RAW)
			return false;
		break;	/* another 32-bit operation: its upper half is zero */
	}
	/*
	 * A 32-bit operation zero-extends its result.  The rules above are on
	 * 64-bit values; where the inputs fit in 32 bits and the result does too
	 * they hold for the 32-bit operation, and otherwise the cap does.
	 */
	if (!x) {
		z = umin(z, 32);
		s = XU;
	}
done:
	if (z > XU) z = XU;
	if (s > XU) s = XU;
	if (z < XU && s > z + 1)
		s = z + 1;
	*zo = z, *so = s;
	return true;
}

/* the extension at i is a copy of its source: rn then holds the result */
static bool xredundant(const xf *f, const a64_record *i)
{
	int rn = i->rn;
	unsigned zn = xz(f, rn), sn = xs(f, rn);

	if (rn >= 31)
		return false;
	switch (i->opcode) {
	case A64_OP_UBFM:
		return i->immediate == 0 && zn <= (unsigned)i->shift + 1u;
	case A64_OP_SBFM:
		return i->immediate == 0 && (zn < (unsigned)i->shift + 1u || (i->size && sn <= (unsigned)i->shift + 1u));
	case A64_OP_MOV:
		return !i->size && zn <= 32;
	case A64_OP_ANDI: {
		uint64_t v = i->size ? (uint64_t)i->immediate : (uint32_t)i->immediate;
		uint64_t lo = zn >= 64 ? ~0ull : (1ull << zn) - 1;

		return zn <= (i->size ? 64u : 32u) && (v & lo) == lo;
	}
	default:
		return false;
	}
}

static inline void xkill(xf *f, record_register_mask w)
{
	for (w &= COPYOK; w; w &= w - 1) {
		int r = __builtin_ctzll(w);

		f->z[r] = XS(f)[r] = XU;
	}
}

/*
 * The block's records over f.  red: per record, whether its extension is a
 * copy given f.  Facts only ever grow weaker, and a block's last visit is
 * with its final in, so that visit's answer stands.
 */
static void xblock(codegen_context *e, uint32_t b, xf *f, uint8_t *red)
{
	const a64_record *in = e->records;

	for (uint32_t k = W.bs[b]; k <= W.be[b]; k++) {
		const a64_record *i = &in[k];
		unsigned z, s;
		int rd = i->rd;

		if (i->opcode == DELETED)
			continue;
		if (W.rr[k] == GPRS) {	/* OP_ALL */
			memset(f, XU, sizeof(*f));
			continue;
		}
		if (!W.ww[k])
			continue;
		if (XT[i->opcode])
			red[k] = (uint8_t)xredundant(f, i);
		if (W.ww[k] == RB(rd) && rd < 31 && (COPYOK & RB(rd)) && xresult(f, i, &z, &s)) {
			f->z[rd] = (uint8_t)z, XS(f)[rd] = (uint8_t)s;
			continue;
		}
		xkill(f, W.ww[k]);
	}
}

static void exts(codegen_context *e)
{
	uint32_t n = e->record_count, nb = W.blk[n - 1] + 1;
	xf f;
	bool changed;
	uint32_t nc = 0;

	for (uint32_t k = 0; k < n; k++) {
		const a64_record *i = &e->records[k];

		nc += XT[i->opcode] && (i->opcode == A64_OP_ANDI || (i->opcode == A64_OP_MOV ? !i->size : !i->immediate));
	}
	if (!nc)
		return;	/* no extension to find redundant */
	if (nb > XF.cap) {
		XF.cap = nb * 2;
		XF.in = realloc(XF.in, XF.cap * sizeof(xf));
		XF.vis = realloc(XF.vis, XF.cap);
		if (!XF.in || !XF.vis)
			abort();
	}
	XF.red = grow_to(XF.red, &XF.capr, n, 1);
	memset(XF.red, 0, n);
	for (uint32_t b = 0; b < nb; b++) {
		const a64_record *l = &e->records[W.bs[b]];

		XF.vis[b] = b == 0 || (l->opcode == A64_OP_LABEL && (e->label_reference_counts[l->label] & CODEGEN_LABEL_EXTERNAL)) ? 2 : 0;
		if (XF.vis[b])
			memset(&XF.in[b], XU, sizeof(xf));
	}
	/*
	 * In order, but a back edge that weakens its target goes back there at
	 * once: what follows a loop is first gone through once the loop is
	 * settled (one visit, not one per round).
	 */
	do {
		changed = false;
		for (uint32_t b = 0; b < nb; b++) {
			uint32_t back = UINT32_MAX;

			if (XF.vis[b] != 2)
				continue;
			XF.vis[b] = 1;
			f = XF.in[b];
			xblock(e, b, &f, XF.red);
			for (int q = 0; q < 2; q++) {
				int32_t sb = q ? W.s1[b] : W.s0[b];
				xf *t;

				if (sb < 0)
					continue;
				t = &XF.in[sb];
				if (!XF.vis[sb]) {
					*t = f, XF.vis[sb] = 2;
					if ((uint32_t)sb <= b && (uint32_t)sb < back)
						back = (uint32_t)sb;
					continue;
				}
				{
					uint8_t up = 0, *tz = t->z, *fz = f.z;	/* (z then s: 64 bytes, vectorized) */

					for (int r = 0; r < 64; r++) {
						uint8_t v = fz[r] > tz[r] ? fz[r] : tz[r];

						up |= v ^ tz[r];
						tz[r] = v;
					}
					if (up) {
						XF.vis[sb] = 2;
						if ((uint32_t)sb <= b && (uint32_t)sb < back)
							back = (uint32_t)sb;
					}
				}
			}
			if (back != UINT32_MAX)
				b = back - 1, changed = true;	/* (b++: back) */
		}
	} while (changed);
	for (uint32_t k = 0; k < n; k++)
		if (XF.red[k]) {
			a64_record *i = &e->records[k];

			i->opcode = i->rd == i->rn ? DELETED : A64_OP_MOV;
			i->size = 1;
		}
}

/* ---------------- dead results ---------------- */

/* an instruction with no effect but its rd (no flags, memory, traps) */
static bool pure(const a64_record *i)
{
	switch (i->opcode) {
	case A64_OP_ADD: case A64_OP_SUB: case A64_OP_AND: case A64_OP_ORR: case A64_OP_EOR: case A64_OP_BIC: case A64_OP_ORN: case A64_OP_ADC: case A64_OP_SBC:
	case A64_OP_ADDI: case A64_OP_SUBI: case A64_OP_ANDI: case A64_OP_ORRI: case A64_OP_EORI:
	case A64_OP_MOVI: case A64_OP_MOV: case A64_OP_MADD: case A64_OP_MSUB: case A64_OP_SMADDL: case A64_OP_UMADDL:
	case A64_OP_SDIV: case A64_OP_UDIV: case A64_OP_SMULH: case A64_OP_UMULH: case A64_OP_LSLV: case A64_OP_LSRV: case A64_OP_ASRV:
	case A64_OP_SBFM: case A64_OP_UBFM: case A64_OP_BFM: case A64_OP_CSEL: case A64_OP_CSINC: case A64_OP_CSINV: case A64_OP_CSNEG:
	case A64_OP_CLZ: case A64_OP_CLS: case A64_OP_RBIT: case A64_OP_REV: case A64_OP_REV16: case A64_OP_REV32:
	case A64_OP_ADRP: case A64_OP_ADDLO: case A64_OP_LDGOT: case A64_OP_ADR:
		return true;
	case A64_OP_LD:	/* a stack slot vcc placed (volatile ones are untagged) */
		return i->rn == A64_REGISTER_FRAME && CODEGEN_SLOT_TAG(i);
	default:
		return false;
	}
}

/*
 * Backward over each block from its live-out (still an over-approximation
 * after the rewrites, which only add reads inside a block): a pure
 * instruction whose rd is dead goes, so chains of them go together.  Reads
 * and writes are taken afresh, as the rewrites changed the records.
 */
static void dead(codegen_context *e)
{
	a64_record *in = e->records;
	uint32_t nb = W.blk[e->record_count - 1] + 1;

	for (uint32_t b = 0; b < nb; b++) {
		record_register_mask live = W.out[b];

		if (b == nb - 1 && brkind(&in[W.be[b]]) != BR_UNCOND)
			live |= retreads;
		for (uint32_t k = W.be[b] + 1; k-- > W.bs[b];) {
			a64_record *i = &in[k];

			if (i->opcode == DELETED)
				continue;
			if (i->rd < 29 && !(live & RB(i->rd)) && pure(i)) {
				i->opcode = DELETED;
				continue;
			}
			live = (live & ~W.ww[k]) | W.rr[k];
		}
	}
}

/*
 * For the frame, after the clean-ups: the registers the records read or
 * write (inline asm: what it writes, and its reads but x18/x29/x30, which
 * every asm lists).  *fl: OPF_ALL a record may touch any register,
 * OPF_CALL a call, OPF_FRAME a frame slot is referenced.
 */
/*
 * The general registers an encoded instruction may name, for the frame decision: every
 * register field read as a general register (a vector register's number then counts as the
 * general one's: more is kept, never less).  False when the word may do more than that: a
 * branch, a call, an exception, or a field 31 that can mean sp.
 */
static bool a64_word_registers(uint32_t w, record_register_mask *m)
{
	unsigned d = w & 31, n = w >> 5 & 31, a = w >> 10 & 31, x = w >> 16 & 31;
	bool fp = (w >> 25 & 7) == 7;	/* scalar FP and Advanced SIMD data processing: 31 is v31 */

	*m = 0;
	if ((w >> 26 & 7) == 5) {	/* branches, exceptions, system */
		if ((w & 0xfffff01fu) == 0xd503201fu || (w & 0xfffff01fu) == 0xd503301fu)
			return true;	/* hints and barriers: no register */
		if ((w & 0xffc00000u) != 0xd5000000u)
			return false;
		*m = d < 31 ? RB(d) : 0;	/* msr/mrs/sys: Rt */
		return true;
	}
	if (!fp && (d == 31 || n == 31))
		return false;
	*m = (d < 31 ? RB(d) : 0) | (n < 31 ? RB(n) : 0) | (a < 31 ? RB(a) : 0) | (x < 31 ? RB(x) : 0);
	return true;
}

VPRIV uint64_t a64_registers_used(codegen_context *e, unsigned *fl)
{
	const a64_record *in = e->records;
	record_register_mask u = 0;
	unsigned f = 0;

	rawp = e->raw.p;
	for (uint32_t k = 0; k < e->record_count; k++) {
		const a64_record *i = &in[k];
		unsigned ro;
		record_register_mask r, w;

		if (i->opcode == A64_OP_JT) {	/* (OP_ALL for the clean-ups, which do not follow it; its registers are known) */
			u |= RBT[i->rd] | RB(16) | RB(17);
			continue;
		}
		if (i->opcode == A64_OP_WORD && e->vir_lowering && a64_word_registers((uint32_t)i->immediate, &r)) {
			u |= r;	/* (VIR's lowering: FP, vector and system instructions with their registers in them) */
			continue;
		}
		ro = roles(i);
		r = reads(i, ro);
		w = writes(i, ro);
		if (ro & OP_ALL)
			f |= OPF_ALL;
		if (ro & OP_CALL)
			f |= OPF_CALL, w &= ~CALLW;	/* (a call's own clobbers are not the function's) */
		if (ro & OP_ASM)
			r &= ~(RB(18) | RB(29) | RB(30));
		if (i->rn == A64_REGISTER_FRAME)
			f |= OPF_FRAME;
		u |= r | w;
	}
	*fl = f;
	return u;
}

/*
 * Shrink-wrapping: the first n records can run in front of the prologue.  They call
 * nothing, name no frame slot, and neither read nor write x19-x30 (a callee-saved register
 * would be written before it is saved, lr and the frame pointer are the caller's).  Their
 * returns and sibling calls are marked (A64_OP_TAIL.sh).
 */
VPRIV bool a64_function_is_frameless(codegen_context *e, uint32_t n)
{
	const a64_record *in = e->records;

	rawp = e->raw.p;
	for (uint32_t k = 0; k < n; k++) {
		const a64_record *i = &in[k];
		unsigned ro;
		record_register_mask r;

		if (i->opcode == A64_OP_JT)
			continue;	/* (its index is not callee-saved: the registers before it are checked) */
		if (i->opcode == A64_OP_WORD) {
			if (!a64_word_registers((uint32_t)i->immediate, &r) || (r & (RB(31) - RB(19))))
				return false;
			continue;
		}
		ro = roles(i);
		if ((ro & (OP_ALL | OP_CALL | OP_ASM | OP_TLV)) || i->rn == A64_REGISTER_FRAME || i->rn == SP || i->rd == SP)
			return false;
		if (i->opcode == A64_OP_TAIL && i->condition >= 2 && !i->shift)
			return false;
		if ((reads(i, ro) | writes(i, ro)) & (RB(31) - RB(19)))
			return false;
	}
	return true;
}

/* ---------------- operand folding (VIR's lowering) ---------------- */

/*
 * A shift, extension, address sum or product whose only reader is the next use of
 * its register in the block becomes part of that reader, as LLVM selects: shifted
 * and extended operands of add/sub/logical ops, register-offset loads and stores,
 * madd/msub/smaddl/umaddl, ubfx/sbfx/ubfiz/sbfiz.  The definition d goes when its
 * register is read by nothing else up to the reader k and is dead after it, and no
 * record between writes what d read.
 */
enum { FD_NONE, FD_LSL, FD_LSR, FD_ASR, FD_EXT };
typedef struct fdef { int kind, src, sh, opt, bits; } fdef;	/* FD_EXT: option (0-7), then << sh; bits: field */

static fdef fclass(const a64_record *d)
{
	int w = d->size ? 64 : 32, r = (int)d->immediate, s = d->shift;

	if (d->opcode == A64_OP_UBFM && s != w - 1 && r == (s + 1) % w)
		return (fdef){ FD_LSL, d->rn, w - 1 - s, 0, 0 };
	if ((d->opcode == A64_OP_UBFM || d->opcode == A64_OP_SBFM) && s == w - 1 && r > 0)
		return (fdef){ d->opcode == A64_OP_UBFM ? FD_LSR : FD_ASR, d->rn, r, 0, 0 };
	if ((d->opcode == A64_OP_UBFM || d->opcode == A64_OP_SBFM) && (s == 7 || s == 15 || (s == 31 && d->size))) {
		int sg = d->opcode == A64_OP_SBFM ? 4 : 0, o = sg | (s == 7 ? 0 : s == 15 ? 1 : 2);

		if (r == 0)
			return (fdef){ FD_EXT, d->rn, 0, o, s + 1 };
		if (r > s && w - r <= 4)	/* sbfiz/ubfiz: the field moved up by w - r */
			return (fdef){ FD_EXT, d->rn, w - r, o, s + 1 };
	}
	if (d->opcode == A64_OP_MOV && !d->size && d->rn < 31)
		return (fdef){ FD_EXT, d->rn, 0, 2, 32 };
	if (d->opcode == A64_OP_ANDI && (d->immediate == 0xff || d->immediate == 0xffff))
		return (fdef){ FD_EXT, d->rn, 0, d->immediate == 0xff ? 0 : 1, d->immediate == 0xff ? 8 : 16 };
	return (fdef){ FD_NONE, 0, 0, 0, 0 };
}

/* x16/x17 are written by records that do not say so (an immediate or an offset that does
 * not encode is built in x17, a far branch or a table goes through x16): a value in one
 * of them is only known to stay there across records that are one plain instruction */
#define FSCR (RB(16) | RB(17))

/* record i is one instruction of exactly its own registers: it cannot rebuild x16/x17 */
static bool fplain(const a64_record *i)
{
	switch (i->opcode) {
	case A64_OP_ADD: case A64_OP_SUB: case A64_OP_ADDS: case A64_OP_SUBS: case A64_OP_AND: case A64_OP_ORR: case A64_OP_EOR: case A64_OP_ANDS:
	case A64_OP_BIC: case A64_OP_ORN: case A64_OP_ADC: case A64_OP_SBC: case A64_OP_ADCS: case A64_OP_SBCS:
	case A64_OP_MOV: case A64_OP_MADD: case A64_OP_MSUB: case A64_OP_SMADDL: case A64_OP_UMADDL:
	case A64_OP_SDIV: case A64_OP_UDIV: case A64_OP_SMULH: case A64_OP_UMULH: case A64_OP_LSLV: case A64_OP_LSRV: case A64_OP_ASRV:
	case A64_OP_SBFM: case A64_OP_UBFM: case A64_OP_BFM: case A64_OP_CSEL: case A64_OP_CSINC: case A64_OP_CSINV: case A64_OP_CSNEG:
	case A64_OP_CLZ: case A64_OP_CLS: case A64_OP_RBIT: case A64_OP_REV: case A64_OP_REV16: case A64_OP_REV32:
	case A64_OP_LDX: case A64_OP_STX: case A64_OP_MOVI: case A64_OP_LABEL:
		return i->rn < 32 && i->rd < 32;
	case A64_OP_ADDI: case A64_OP_SUBI: case A64_OP_ADDSI: case A64_OP_SUBSI:
		return i->rn < 32 && i->rd < 32 && i->immediate > -4096 && i->immediate < 4096;
	case A64_OP_LD: case A64_OP_ST:	/* (an offset both the scaled and the unscaled form reach whatever the size) */
		return i->rn < 32 && i->immediate >= 0 && i->immediate < 256;
	default:
		return false;
	}
}

/* the record defining register r for reader k: none read r between, it is dead after k */
static int32_t fdefof(codegen_context *e, uint32_t k, int r)
{
	const a64_record *in = e->records;
	uint32_t lo = W.bs[W.blk[k]], n = 0;
	bool scr = false;	/* a record between may have rebuilt x16/x17 */

	if (r >= 29 || ((W.la[k] & RB(r)) && !(W.ww[k] & RB(r))))
		return -1;
	for (uint32_t j = k; j-- > lo && n < 32;) {
		if (in[j].opcode == DELETED)
			continue;
		n++;
		if (W.ro[j] & OP_SLOW)
			return -1;
		if (W.ww[j] & RB(r))
			return W.ww[j] == RB(r) && (!scr || !(RB(r) & FSCR)) ? (int32_t)j : -1;
		if (W.rr[j] & RB(r))
			return -1;
		scr |= !fplain(&in[j]);
	}
	return -1;
}

/* the record last writing r before k in its block (a constant's MOVI), whoever else reads r */
static const a64_record *fwriter(codegen_context *e, uint32_t k, int r)
{
	uint32_t lo = W.bs[W.blk[k]], n = 0;
	bool scr = false;

	for (uint32_t j = k; j-- > lo && n < 32;) {	/* (bounded, as fdefof) */
		if (e->records[j].opcode == DELETED)
			continue;
		n++;
		if (W.ro[j] & OP_SLOW)
			return NULL;
		if (W.ww[j] & RB(r))
			return !scr || !(RB(r) & FSCR) ? &e->records[j] : NULL;
		scr |= !fplain(&e->records[j]);
	}
	return NULL;
}

/* d's reads are still there at k */
static bool fkept(codegen_context *e, uint32_t d, uint32_t k, record_register_mask src)
{
	for (uint32_t j = d + 1; j < k; j++)
		if (e->records[j].opcode != DELETED && ((W.ww[j] & src) || ((src & FSCR) && !fplain(&e->records[j]))))
			return false;
	return true;
}

static void fdone(codegen_context *e, uint32_t d, uint32_t k)
{
	a64_record *in = e->records;
	record_register_mask src = W.rr[d];

	in[d].opcode = DELETED;
	W.rr[d] = W.ww[d] = 0;
	W.ro[k] = (uint16_t)roles(&in[k]);
	W.rr[k] = reads(&in[k], W.ro[k]);
	for (uint32_t j = d; j < k; j++)
		W.la[j] |= src;
}

static inline bool fgpr(int r) { return r < 31; }

/* reader k: one fold, true if it changed (k is looked at again) */
static bool fold1(codegen_context *e, uint32_t k)
{
	a64_record *in = e->records, *i = &in[k];
	int32_t d;
	int w = i->size ? 64 : 32;

	switch (i->opcode) {
	case A64_OP_ADD: case A64_OP_SUB: case A64_OP_ADDS: case A64_OP_SUBS: case A64_OP_AND: case A64_OP_ORR: case A64_OP_EOR: case A64_OP_ANDS:
	case A64_OP_BIC: case A64_OP_ORN: {
		bool as = i->opcode <= A64_OP_SUBS, comm = i->opcode == A64_OP_ADD || i->opcode == A64_OP_ADDS || i->opcode == A64_OP_AND || i->opcode == A64_OP_ORR ||
							i->opcode == A64_OP_EOR || i->opcode == A64_OP_ANDS;

		if (i->shift || i->condition || i->rd >= 32 || i->rn >= 32 || i->rn == i->rm)
			return false;
		for (int side = 0; side < 2; side++) {
			int r = side ? i->rn : i->rm;

			if (side && !comm)
				break;
			if ((d = fdefof(e, k, r)) < 0)
				continue;
			const a64_record *di = &in[d];
			fdef f = fclass(di);

			/* madd/msub: the product's register read once */
			if ((di->opcode == A64_OP_MADD || di->opcode == A64_OP_SMADDL || di->opcode == A64_OP_UMADDL) && di->ra == ZR &&
			    (di->opcode == A64_OP_MADD ? di->size == i->size : i->size) && (i->opcode == A64_OP_ADD || (i->opcode == A64_OP_SUB && !side))) {
				int a = side ? i->rm : i->rn;

				if (a >= 31 && a != ZR)
					continue;
				if (!fkept(e, (uint32_t)d, k, RB(di->rn) | RB(di->rm)))
					continue;
				if (di->opcode != A64_OP_MADD && i->opcode == A64_OP_SUB)
					continue;
				*i = (a64_record){ .opcode = di->opcode == A64_OP_MADD ? (i->opcode == A64_OP_ADD ? A64_OP_MADD : A64_OP_MSUB) : di->opcode, .size = i->size,
					    .rd = i->rd, .rn = di->rn, .rm = di->rm, .ra = (uint8_t)a };
				fdone(e, (uint32_t)d, k);
				return true;
			}
			/* (an extension into w that zero-extends serves an x reader too) */
			if (f.kind == FD_NONE || !fgpr(f.src) || !fkept(e, (uint32_t)d, k, RB(f.src)) ||
			    (di->size != i->size && (f.kind != FD_EXT || di->size || (f.opt & 4))))
				continue;
			if (f.kind == FD_EXT) {
				/* extended register: add/sub only; zr as rn (or rd without flags) would be sp */
				if (!as || f.sh > 4 || i->rn == ZR || (i->rd == ZR && (i->opcode == A64_OP_ADD || i->opcode == A64_OP_SUB)) ||
				    (!i->size && (f.opt & 3) == 2) || (side && i->opcode != A64_OP_ADD && i->opcode != A64_OP_ADDS))
					continue;
				if (side)
					i->rn = i->rm;
				i->rm = (uint8_t)f.src;
				i->condition = (uint8_t)(8 + f.opt);
				i->shift = (uint8_t)f.sh;
			} else {
				if (side)
					i->rn = i->rm;
				i->rm = (uint8_t)f.src;
				i->condition = (uint8_t)(f.kind - FD_LSL);
				i->shift = (uint8_t)f.sh;
			}
			fdone(e, (uint32_t)d, k);
			return true;
		}
		return false;
	}
	case A64_OP_LD: case A64_OP_ST: {
		int r = i->rn;

		if (i->immediate || (i->opcode == A64_OP_ST && i->rd == r) || (d = fdefof(e, k, r)) < 0)
			return false;
		const a64_record *di = &in[d];
		int ix;

		if (di->opcode != A64_OP_ADD || !di->size || di->rd >= 31 || di->rn >= 31 || !fgpr(di->rm) ||
		    (di->shift && di->shift != i->size))
			return false;
		if (di->condition == 0 || di->condition == 8 + 3 || di->condition == 8 + 7)
			ix = 3;	/* lsl */
		else if (di->condition == 8 + 6 || di->condition == 8 + 2)
			ix = di->condition - 8;	/* sxtw, uxtw */
		else
			return false;
		if (!fkept(e, (uint32_t)d, k, RB(di->rn) | RB(di->rm)))
			return false;
		i->opcode = i->opcode == A64_OP_LD ? A64_OP_LDX : A64_OP_STX;
		i->rn = di->rn;
		i->rm = di->rm;
		i->ra = (uint8_t)ix;
		i->shift = di->shift != 0;
		fdone(e, (uint32_t)d, k);
		return true;
	}
	case A64_OP_MADD: {	/* of two sign (zero) extended words: smaddl (umaddl) */
		int32_t d2;

		if (!i->size || i->rn == i->rm || i->ra == i->rn || i->ra == i->rm)
			return false;
		for (int side = 0; side < 2; side++) {	/* one of them a constant word */
			int r = side ? i->rn : i->rm, c = side ? i->rm : i->rn;
			const a64_record *mk = fwriter(e, k, c);

			if ((d = fdefof(e, k, r)) < 0 || !mk || mk->opcode != A64_OP_MOVI || !fgpr(c))
				continue;
			fdef f = fclass(&in[d]);
			int64_t v = mk->size ? mk->immediate : (int64_t)(uint32_t)mk->immediate;

			if (f.kind != FD_EXT || f.sh || (f.opt & 3) != 2 || !in[d].size || !fgpr(f.src) ||
			    !(f.opt & 4 ? v >= INT32_MIN && v <= INT32_MAX : v >= 0 && v <= UINT32_MAX) ||
			    !fkept(e, (uint32_t)d, k, RB(f.src)) || in[d].rd == c)
				continue;
			i->opcode = f.opt & 4 ? A64_OP_SMADDL : A64_OP_UMADDL;
			i->rn = (uint8_t)f.src;
			i->rm = (uint8_t)c;
			fdone(e, (uint32_t)d, k);
			return true;
		}
		if ((d = fdefof(e, k, i->rn)) < 0 || (d2 = fdefof(e, k, i->rm)) < 0)
			return false;
		fdef f = fclass(&in[d]), g = fclass(&in[d2]);

		if (f.kind != FD_EXT || g.kind != FD_EXT || f.sh || g.sh || f.opt != g.opt || (f.opt & 3) != 2 ||
		    !in[d].size || !in[d2].size || !fgpr(f.src) || !fgpr(g.src))
			return false;
		uint32_t lo = d < d2 ? (uint32_t)d : (uint32_t)d2;

		if (!fkept(e, (uint32_t)d, k, RB(f.src)) || !fkept(e, (uint32_t)d2, k, RB(g.src)) ||
		    (W.rr[lo == (uint32_t)d ? d2 : d] & RB(in[lo].rd)))
			return false;
		/* (the later definition must not read the earlier one's register: it reads its own source) */
		if (in[d].rd == g.src || in[d2].rd == f.src)
			return false;
		i->opcode = f.opt & 4 ? A64_OP_SMADDL : A64_OP_UMADDL;
		i->rn = (uint8_t)f.src;
		i->rm = (uint8_t)g.src;
		fdone(e, (uint32_t)d, k);
		fdone(e, (uint32_t)d2, k);
		return true;
	}
	case A64_OP_ANDI: {	/* (x >> k) & (2^n - 1): ubfx */
		uint64_t m = i->size ? (uint64_t)i->immediate : (uint32_t)i->immediate;
		int n;

		if (!m || (m & (m + 1)) || i->rd >= 31 || (d = fdefof(e, k, i->rn)) < 0)
			return false;
		n = 64 - __builtin_clzll(m);
		fdef f = fclass(&in[d]);

		if ((f.kind != FD_LSR && f.kind != FD_ASR) || in[d].size != i->size || f.sh + n > w || !fgpr(f.src) ||
		    !fkept(e, (uint32_t)d, k, RB(f.src)))
			return false;
		*i = (a64_record){ .opcode = A64_OP_UBFM, .size = i->size, .rd = i->rd, .rn = (uint8_t)f.src, .immediate = f.sh, .shift = (uint8_t)(f.sh + n - 1) };
		fdone(e, (uint32_t)d, k);
		return true;
	}
	case A64_OP_UBFM: case A64_OP_SBFM: {
		fdef me = fclass(i);

		if (i->rd >= 31 || (d = fdefof(e, k, i->rn)) < 0)
			return false;
		const a64_record *di = &in[d];
		fdef f = fclass(di);

		if (!fgpr(f.src) || (di->size != i->size && (f.kind != FD_EXT || di->size || (f.opt & 4) || di->opcode == A64_OP_ANDI)))
			return false;
		if (me.kind == FD_LSL && (f.kind == FD_EXT || (di->opcode == A64_OP_ANDI && !((uint64_t)di->immediate & ((uint64_t)di->immediate + 1))))) {
			/* (x & (2^n - 1)) << k, an extension << k: ubfiz/sbfiz */
			int bits = f.kind == FD_EXT ? f.bits : 64 - __builtin_clzll((uint64_t)di->immediate), sg;

			if (f.kind == FD_EXT && f.sh)
				return false;
			sg = f.kind == FD_EXT && (f.opt & 4);
			if (!fkept(e, (uint32_t)d, k, RB(f.kind == FD_EXT ? f.src : di->rn)))
				return false;
			int src = f.kind == FD_EXT ? f.src : di->rn;

			if (bits >= w - me.sh) {	/* the field fills the rest: just the shift */
				i->rn = (uint8_t)src;
			} else {
				i->opcode = sg ? A64_OP_SBFM : A64_OP_UBFM;
				i->rn = (uint8_t)src;
				i->immediate = (w - me.sh) % w;
				i->shift = (uint8_t)(bits - 1);
			}
			fdone(e, (uint32_t)d, k);
			return true;
		}
		if ((me.kind == FD_LSR || me.kind == FD_ASR) && f.kind == FD_LSL && me.sh >= f.sh) {
			/* (x << m) >> k: ubfx/sbfx */
			if (!fkept(e, (uint32_t)d, k, RB(f.src)))
				return false;
			i->rn = (uint8_t)f.src;
			i->immediate = me.sh - f.sh;
			i->shift = (uint8_t)(w - 1 - f.sh);
			fdone(e, (uint32_t)d, k);
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

/*
 * A constant the block loaded for one reader, held by the reader instead: zr for a
 * stored zero, an add/sub/compare immediate (12 bits, maybe shifted, either sign:
 * the encoder flips the operation), a logical immediate.
 */
static bool fconst(codegen_context *e, uint32_t k)
{
	a64_record *in = e->records, *i = &in[k];
	int32_t d;

	switch (i->opcode) {
	case A64_OP_ST: case A64_OP_STX: case A64_OP_STP:
		for (int q = 0; q < 2; q++) {
			int r = q ? i->ra : i->rd;

			if ((q && i->opcode != A64_OP_STP) || r >= 31 || r == i->rn || (i->opcode == A64_OP_STX && r == i->rm) ||
			    (i->opcode == A64_OP_STP && i->rd == i->ra) || (d = fdefof(e, k, r)) < 0 || in[d].opcode != A64_OP_MOVI ||
			    ((uint64_t)in[d].immediate & (i->opcode == A64_OP_STP || i->size == 3 ? ~0ull : ((1ull << (8 << i->size)) - 1))))
				continue;
			if (q)
				i->ra = ZR;
			else
				i->rd = ZR;
			fdone(e, (uint32_t)d, k);
			return true;
		}
		return false;
	case A64_OP_ADD: case A64_OP_SUB: case A64_OP_ADDS: case A64_OP_SUBS: case A64_OP_AND: case A64_OP_ORR: case A64_OP_EOR: case A64_OP_ANDS: {
		bool as = i->opcode <= A64_OP_SUBS, comm = i->opcode != A64_OP_SUB && i->opcode != A64_OP_SUBS;

		if (i->shift || i->condition || i->rn >= 31 || i->rn == i->rm || (i->rd == ZR && (i->opcode == A64_OP_ADD || i->opcode == A64_OP_SUB ||
		    i->opcode == A64_OP_AND || i->opcode == A64_OP_ORR || i->opcode == A64_OP_EOR)) || i->rd >= 32)
			return false;
		for (int side = 0; side < 2; side++) {
			int r = side ? i->rn : i->rm, a = side ? i->rm : i->rn;

			if ((side && !comm) || r >= 31 || a >= 31 || (d = fdefof(e, k, r)) < 0 || in[d].opcode != A64_OP_MOVI)
				continue;
			int64_t v = i->size ? in[d].immediate : (int64_t)(int32_t)in[d].immediate, u = v < 0 ? -v : v;
			static const uint8_t to[] = { [A64_OP_ADD] = A64_OP_ADDI, [A64_OP_SUB] = A64_OP_SUBI, [A64_OP_ADDS] = A64_OP_ADDSI, [A64_OP_SUBS] = A64_OP_SUBSI,
						      [A64_OP_AND] = A64_OP_ANDI, [A64_OP_ORR] = A64_OP_ORRI, [A64_OP_EOR] = A64_OP_EORI, [A64_OP_ANDS] = A64_OP_ANDSI };

			if (as ? v == INT64_MIN || !(u < 4096 || (!(u & 0xfff) && u < (1 << 24))) || (i->size == 0 && u > 0x7fffffff)
			       : !a64_logical_immediate_is_encodable(i->size ? (uint64_t)v : (uint32_t)v, i->size))
				continue;
			*i = (a64_record){ .opcode = to[i->opcode], .size = i->size, .rd = i->rd, .rn = (uint8_t)a, .immediate = as ? v : (i->size ? v : (int64_t)(uint32_t)v) };
			fdone(e, (uint32_t)d, k);
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

/*
 * A product by a constant the block just loaded (once, for it), when no add took it
 * (madd): c = 2^n + 1 one add; (2^n + 1) << m and 2^n - 1 two records, the first in
 * the constant's place with its register, as LLVM.
 */
static void fmulk(codegen_context *e, uint32_t k)
{
	a64_record *in = e->records, *i = &in[k];
	int w = i->size ? 64 : 32;

	if (i->opcode != A64_OP_MADD || i->ra != ZR || i->rd >= 31 || i->rn == i->rm)
		return;
	for (int side = 0; side < 2; side++) {
		int r = side ? i->rn : i->rm, a = side ? i->rm : i->rn;
		int32_t d = fdefof(e, k, r);
		uint64_t c, m = i->size ? ~0ull : 0xffffffffull;
		int n, sh;

		if (d < 0 || in[d].opcode != A64_OP_MOVI || !fgpr(a))
			continue;
		c = (uint64_t)in[d].immediate & m;
		if (!c)
			continue;
		if (c == m) {	/* -1: neg */
			*i = (a64_record){ .opcode = A64_OP_SUB, .size = i->size, .rd = i->rd, .rn = ZR, .rm = (uint8_t)a };
			fdone(e, (uint32_t)d, k);
			return;
		}
		sh = __builtin_ctzll(c);
		c >>= sh;
		if (c > 1 && !((c - 1) & (c - 2)) && sh < w) {	/* (2^n + 1) << sh */
			n = __builtin_ctzll(c - 1);
			if (!sh) {
				*i = (a64_record){ .opcode = A64_OP_ADD, .size = i->size, .rd = i->rd, .rn = (uint8_t)a, .rm = (uint8_t)a, .shift = (uint8_t)n };
				fdone(e, (uint32_t)d, k);
				return;
			}
		} else if (!sh && c > 3 && !(c & (c + 1)) && 64 - __builtin_clzll(c) < w) {	/* 2^n - 1 */
			n = 64 - __builtin_clzll(c);
		} else {
			continue;
		}
		/* the first record in the constant's place: a there already (live through d) */
		if (!fkept(e, (uint32_t)d, k, RB(a)) || !(W.la[d] & RB(a)))
			continue;
		if (sh) {	/* add r, a, a, lsl n; lsl d, r, sh */
			in[d] = (a64_record){ .opcode = A64_OP_ADD, .size = i->size, .rd = (uint8_t)r, .rn = (uint8_t)a, .rm = (uint8_t)a, .shift = (uint8_t)n };
			*i = (a64_record){ .opcode = A64_OP_UBFM, .size = i->size, .rd = i->rd, .rn = (uint8_t)r, .immediate = (w - sh) % w, .shift = (uint8_t)(w - 1 - sh) };
		} else {	/* lsl r, a, n; sub d, r, a */
			in[d] = (a64_record){ .opcode = A64_OP_UBFM, .size = i->size, .rd = (uint8_t)r, .rn = (uint8_t)a, .immediate = (w - n) % w, .shift = (uint8_t)(w - 1 - n) };
			*i = (a64_record){ .opcode = A64_OP_SUB, .size = i->size, .rd = i->rd, .rn = (uint8_t)r, .rm = (uint8_t)a };
		}
		W.ro[d] = (uint16_t)roles(&in[d]);
		W.rr[d] = reads(&in[d], W.ro[d]);
		W.ro[k] = (uint16_t)roles(i);
		W.rr[k] = reads(i, W.ro[k]);
		return;
	}
}

/*
 * Bytes assembled into a value: an or of byte loads from one base at adjacent
 * offsets, each shifted to its place in little- or big-endian order (2, 4 or 8 of
 * them), becomes one load at the first byte load's place, then rev/rev16 for big
 * endian, as LLVM.  Nothing between may store, call or be unknown, nor read or
 * write the result's register; the base is the same for every load.
 */
typedef struct fbyte { uint32_t at; int base; int64_t off; int sh, sgn; } fbyte;	/* sgn: ldrsb's cond */
#define FB_MAX 8
#define FB_CHAIN 24

static bool fbytes(codegen_context *e, uint32_t j, int r, int s, int sz, fbyte *b, int *nb, uint32_t *ch, int *nch)
{
	const a64_record *in = e->records;
	int32_t d = fdefof(e, j, r);

	if (d < 0 || *nch >= FB_CHAIN || s >= (sz ? 64 : 32))
		return false;
	const a64_record *di = &in[d];

	ch[(*nch)++] = (uint32_t)d;
	if (di->opcode == A64_OP_LD) {
		if (di->size || di->rn >= 31 || di->ra == A64_LOAD_VOLATILE || *nb >= FB_MAX || (s & 7))
			return false;
		b[(*nb)++] = (fbyte){ (uint32_t)d, di->rn, di->immediate, s, di->condition };
		return true;
	}
	if (di->size > sz)
		return false;
	if (di->size < sz) {	/* a w value in an x one: zero above bit 31, if its bytes stay below */
		int n0 = *nb;

		if (!fbytes(e, j, r, 0, 0, b, nb, ch, nch))	/* (d in ch twice: harmless) */
			return false;
		for (int q = n0; q < *nb; q++) {
			b[q].sh += s;
			b[q].sgn = b[q].sgn ? 2 : 0;	/* (its sign stops at bit 31) */
		}
		return true;
	}
	if (di->opcode == A64_OP_ORR && !di->condition && di->rn != di->rm)
		return fbytes(e, (uint32_t)d, di->rn, s, sz, b, nb, ch, nch) &&
		       fbytes(e, (uint32_t)d, di->rm, s + di->shift, sz, b, nb, ch, nch);
	fdef f = fclass(di);

	if (f.kind == FD_LSL)
		return fbytes(e, (uint32_t)d, f.src, s + f.sh, sz, b, nb, ch, nch);
	return false;
}

static void fbswap(codegen_context *e, uint32_t k)
{
	a64_record *in = e->records, *i = &in[k];
	fbyte b[FB_MAX];
	uint32_t ch[FB_CHAIN], first = k, lastld = 0;
	int nb = 0, nch = 0, n, lg, w = i->size ? 64 : 32;
	int64_t lo = INT64_MAX;
	bool le = true, be = true;
	uint8_t seen = 0;

	if (i->opcode != A64_OP_ORR || i->condition || i->rd >= 29 || i->rn == i->rm || i->rn >= 31 || i->rm >= 31)
		return;
	if (!fbytes(e, k, i->rn, 0, i->size, b, &nb, ch, &nch) || !fbytes(e, k, i->rm, i->shift, i->size, b, &nb, ch, &nch))
		return;
	n = nb;
	if (n != 2 && n != 4 && n != 8)
		return;
	if (n * 8 > w)
		return;
	lg = n == 2 ? 1 : n == 4 ? 2 : 3;
	for (int q = 0; q < n; q++) {
		if (b[q].base != b[0].base)
			return;
		lo = b[q].off < lo ? b[q].off : lo;
		first = b[q].at < first ? b[q].at : first;
		lastld = b[q].at > lastld ? b[q].at : lastld;
	}
	for (int q = 0; q < n; q++) {
		int64_t x = b[q].off - lo;

		if (x < 0 || x >= n || (seen & (1u << x)))
			return;
		seen |= (uint8_t)(1u << x);
		le &= b[q].sh == 8 * x;
		be &= b[q].sh == 8 * (n - 1 - x);
	}
	if (!le && !be)
		return;
	/* a sign-extended byte: only the top one, then the value sign-extended from 8n bits
	 * (nothing when it fills the register; into w, ldrsb's bits above 31 are zero) */
	int sgn = 0;

	for (int q = 0; q < n; q++)
		if (b[q].sgn) {
			if (b[q].sh != 8 * (n - 1) || (i->size && b[q].sgn == 2))
				return;
			sgn = 8 * n < w;
		}
	/* the records between: the chain's, or ones that leave memory, the base and rd alone */
	for (uint32_t j = first; j <= k; j++) {
		bool inch = j == k;

		if (in[j].opcode == DELETED)
			continue;
		for (int q = 0; q < nch && !inch; q++)
			inch = ch[q] == j;
		if (inch)
			continue;
		unsigned op = in[j].opcode;

		if ((W.ro[j] & OP_SLOW) || op == A64_OP_ST || op == A64_OP_STX || op == A64_OP_STP || op == A64_OP_STLO || op == A64_OP_STF || op == A64_OP_BL ||
		    op == A64_OP_BLR || ((W.rr[j] | W.ww[j]) & RB(i->rd)) || (j < lastld && (W.ww[j] & RB(b[0].base))))
			return;
	}
	for (int q = 0; q < nch; q++)	/* (a chain load writing the base: the last) */
		if (ch[q] < lastld && in[ch[q]].rd == b[0].base)
			return;
	int rd = i->rd, base = b[0].base, sz = i->size;

	/* rev (big endian) and the sign extension, the last at k, the rev before it in a
	 * chain record's place (there are at least three: two loads and an or) */
	uint32_t at[2] = { k, k }, na = 0;

	if (!le && sgn) {
		for (int q = 0; q < nch && !na; q++)
			if (ch[q] > first && ch[q] < k)
				at[0] = ch[q], na = 1;
		if (!na)
			return;	/* (cannot happen) */
	}
	for (int q = 0; q < nch; q++) {
		in[ch[q]].opcode = DELETED;
		W.rr[ch[q]] = W.ww[ch[q]] = 0;
	}
	in[first] = (a64_record){ .opcode = A64_OP_LD, .size = (uint8_t)lg, .rd = (uint8_t)rd, .rn = (uint8_t)base, .immediate = lo };
	W.ro[first] = (uint16_t)roles(&in[first]);
	W.rr[first] = reads(&in[first], W.ro[first]);
	W.ww[first] = writes(&in[first], W.ro[first]);
	in[k].opcode = DELETED;
	W.rr[k] = W.ww[k] = 0;
	if (!le)
		in[at[0]] = (a64_record){ .opcode = n == 2 ? A64_OP_REV16 : A64_OP_REV, .size = (uint8_t)(n == 8 ? 1 : 0), .rd = (uint8_t)rd, .rn = (uint8_t)rd };
	if (sgn)
		in[k] = (a64_record){ .opcode = A64_OP_SBFM, .size = (uint8_t)sz, .rd = (uint8_t)rd, .rn = (uint8_t)rd, .immediate = 0, .shift = (uint8_t)(8 * n - 1) };
	for (uint32_t q = 0; q < 2; q++) {
		uint32_t j = q ? k : at[0];

		if (in[j].opcode == DELETED)
			continue;
		W.ro[j] = (uint16_t)roles(&in[j]);
		W.rr[j] = reads(&in[j], W.ro[j]);
		W.ww[j] = writes(&in[j], W.ro[j]);
	}
	for (uint32_t j = first; j < k; j++)
		W.la[j] |= RB(rd);
}

static void fold(codegen_context *e)
{
	for (uint32_t k = 0; k < e->record_count; k++)
		if (e->records[k].opcode != DELETED)
			for (int n = 0; n < 4 && fold1(e, k); n++)
				;
	for (uint32_t k = e->record_count; k-- > 0;)
		if (e->records[k].opcode != DELETED)
			fbswap(e, k);
	for (uint32_t k = 0; k < e->record_count; k++)
		if (e->records[k].opcode != DELETED) {
			fmulk(e, k);
			if (e->records[k].opcode != DELETED)
				fconst(e, k);
		}
}

/* ---------------- repeated compares ---------------- */

static inline bool sets_flags(unsigned op)
{
	return op == A64_OP_ADDS || op == A64_OP_SUBS || op == A64_OP_ANDS || op == A64_OP_ADCS || op == A64_OP_SBCS || op == A64_OP_ADDSI ||
	       op == A64_OP_SUBSI || op == A64_OP_ANDSI;
}

/*
 * A compare (rd zr) identical to the one whose flags still hold goes.  The flags hold
 * from a compare on, through conditional branches, until a flag writer, a call or
 * unknown record, or a write of a register the compare reads; at a label, when every
 * way in (falling through, branches from before it) brings an identical compare.  A
 * label something branches back to, or reached from outside the records, starts with
 * nothing known.
 */
#define NOCMP UINT32_MAX
#define MIXED (UINT32_MAX - 1)
static void flags(codegen_context *e)
{
	static _Thread_local uint32_t *ls, *lp;	/* per label: the compare branches bring (NOCMP, MIXED); its record */
	static _Thread_local uint32_t cap;
	a64_record *in = e->records;
	uint32_t c = NOCMP, nl = e->label_count;
	bool fall = true;	/* the previous record falls through */

	if (nl > cap) {
		cap = nl + nl / 2 + 64;
		free(ls);
		ls = malloc(2 * (size_t)cap * sizeof *ls);
		if (!ls)
			abort();
		lp = ls + cap;
	}
	for (uint32_t l = 0; l < nl; l++)
		ls[l] = NOCMP, lp[l] = UINT32_MAX;
	for (uint32_t k = 0; k < e->record_count; k++)
		if (in[k].opcode == A64_OP_LABEL)
			lp[in[k].label] = k;
	for (uint32_t l = 0; l < nl; l++)	/* (ls: branched to yet; NOCMP below means "nothing came") */
		ls[l] = e->label_reference_counts[l] & CODEGEN_LABEL_EXTERNAL ? MIXED : NOCMP - 2;
	for (uint32_t k = 0; k < e->record_count; k++) {	/* a label something branches back to: nothing known at it */
		int bk = in[k].opcode == DELETED ? BR_NONE : brkind(&in[k]);

		if ((bk == BR_COND || bk == BR_UNCOND) && in[k].label < nl && lp[in[k].label] <= k)
			ls[in[k].label] = MIXED;
	}
	for (uint32_t k = 0; k < e->record_count; k++) {
		a64_record *i = &in[k];
		unsigned ro;
		int bk;

		if (i->opcode == DELETED)
			continue;
		if (i->opcode == A64_OP_LABEL) {
			uint32_t w = ls[i->label];

			if (w == NOCMP - 2)	/* only falling through */
				w = fall ? c : NOCMP;
			else if (w != MIXED && fall && (c == NOCMP || memcmp(&in[c], &in[w], sizeof *i)))
				w = MIXED;
			c = w == MIXED ? NOCMP : w;
			ls[i->label] = MIXED;	/* (placed: a later branch to it is a back edge) */
			fall = true;
			continue;
		}
		if (sets_flags(i->opcode) && i->rd == ZR) {
			uint32_t j = k;

			while (j && in[j - 1].opcode == DELETED)
				j--;
			if (c != NOCMP && !memcmp(&in[c], i, sizeof *i))
				i->opcode = DELETED;
			else if (j && in[j - 1].opcode == (i->opcode == A64_OP_SUBS ? A64_OP_SUB : i->opcode == A64_OP_SUBSI ? A64_OP_SUBI : DELETED) &&
				 in[j - 1].rd < 31 && in[j - 1].rd != i->rn && (i->opcode == A64_OP_SUBSI || in[j - 1].rd != i->rm)) {
				/* the subtraction just before it, of the same operands: that one sets the flags */
				a64_record t = in[j - 1];

				t.opcode = i->opcode, t.rd = ZR;
				if (!memcmp(&t, i, sizeof t)) {
					in[j - 1].opcode = i->opcode;
					i->opcode = DELETED;
					c = NOCMP;
				} else
					c = k;
			} else
				c = k;
			fall = true;
			continue;
		}
		ro = roles(i);
		bk = brkind(i);
		if (c != NOCMP && (sets_flags(i->opcode) || (ro & OP_SLOW) || (writes(i, ro) & reads(&in[c], roles(&in[c])))))
			c = NOCMP;
		if ((bk == BR_COND || bk == BR_UNCOND || i->opcode == A64_OP_ADR) && i->label < nl) {
			uint32_t *w = &ls[i->label];

			if (i->opcode == A64_OP_ADR || lp[i->label] < k || c == NOCMP)
				*w = MIXED;
			else if (*w == NOCMP - 2)
				*w = c;
			else if (*w != MIXED && memcmp(&in[*w], &in[c], sizeof *i))
				*w = MIXED;
		}
		fall = bk == BR_NONE || bk == BR_COND;
		if (!fall)
			c = NOCMP;
	}
}
#undef NOCMP
#undef MIXED

/* copy propagation (forward, then backward) and dead copies */
static void copies(codegen_context *e)
{
	a64_record *in = e->records;
	const record_copy_scan scan = {
		.records = in, .stride = sizeof(a64_record), .op_offset = offsetof(a64_record, opcode), .deleted_op = DELETED,
		.flags = W.ro, .flags_stride = sizeof(*W.ro), .opaque_mask = OP_ALL,
		.read = W.rr, .write = W.ww, .live = W.la, .rewrite = a64_copy_rewrite, .ctx = e,
	};

	for (uint32_t k = 0; k < e->record_count; k++) {
		if (in[k].opcode == A64_OP_MOV && in[k].size == 1 && in[k].rd == in[k].rn && in[k].rd < 31) {
			in[k].opcode = DELETED;	/* mov x, x */
			continue;
		}
		if (is_copy(&in[k])) {
			if (!(W.la[k] & RB(in[k].rd)) && in[k].rd != 29 && in[k].rd != 30)
				in[k].opcode = DELETED;	/* dead */
			else if (TEMPS & RB(in[k].rd))
				record_copy_forward(&scan, k, W.be[W.blk[k]], in[k].rd, in[k].rn);
		}
	}
	for (uint32_t k = 0; k < e->record_count; k++)
		if (is_copy(&in[k]))
			backward(e, k);
}

/* ---------------- VCC_REC_STATS: the clean-up passes' counters ---------------- */

static const char *const rp_names[A64_RECORD_PASS_COUNT] = {
	[A64_RECORD_PASS_FUNCTION] = "functions", [A64_RECORD_PASS_UNREACHABLE] = "unreachable", [A64_RECORD_PASS_FALLTHROUGH] = "fallthrough",
	[A64_RECORD_PASS_LIVENESS] = "liveness", [A64_RECORD_PASS_SPILL_PAIRS] = "spill_pairs", [A64_RECORD_PASS_COPIES_FIRST] = "copies_1",
	[A64_RECORD_PASS_SLOTS] = "slots", [A64_RECORD_PASS_EXTENSIONS] = "exts", [A64_RECORD_PASS_COPIES_SECOND] = "copies_2", [A64_RECORD_PASS_DEAD] = "dead",
	[A64_RECORD_PASS_FOLD] = "fold", [A64_RECORD_PASS_FLAGS] = "flags", [A64_RECORD_PASS_REGISTERS_USED] = "opt_regs",
	[A64_RECORD_PASS_JUMP_TABLES_EXPAND] = "expand_tables", [A64_RECORD_PASS_FRAMELESS] = "opt_noframe", [A64_RECORD_PASS_TAIL_EXPAND] = "tail_expand",
	[A64_RECORD_PASS_FRAME_REWRITE] = "frame_rewrite", [A64_RECORD_PASS_TEST_BRANCH_RELAX] = "tbz_relax",
};
static _Thread_local a64_record *rp_buf;
static _Thread_local uint32_t rp_cap, rp_n;

/* the records before a pass (only with VCC_REC_STATS) */
VPRIV void rp_snap(codegen_context *e)
{
	if (e->record_count > rp_cap) {
		free(rp_buf);
		rp_cap = e->record_count + 1024;
		rp_buf = malloc((size_t)rp_cap * sizeof(a64_record));
		if (!rp_buf)
			abort();
	}
	rp_n = e->record_count;
	memcpy(rp_buf, e->records, (size_t)e->record_count * sizeof(a64_record));
}

/*
 * One run of `pass`, compared with the snapshot: a record that became DELETED or that the
 * pass dropped (the count shrank) is removed, one whose bytes differ is changed, one the pass
 * inserted (the count grew) is added.  rp_count without a snapshot of the pass (A64_RECORD_PASS_FUNCTION)
 * counts the run only: callers pass rp_n = nin first.
 */
VPRIV void rp_count(codegen_context *e, int pass)
{
	_Atomic uint64_t *c = e->unit->record_pass_counts[e->vir_lowering][pass];
	uint64_t ch = 0, rm = 0, ad = 0;

	if (pass != A64_RECORD_PASS_FUNCTION) {
		if (e->record_count == rp_n) {
			for (uint32_t k = 0; k < rp_n; k++) {
				if (rp_buf[k].opcode == DELETED)
					continue;
				if (e->records[k].opcode == DELETED)
					rm++;
				else if (memcmp(&rp_buf[k], &e->records[k], sizeof(a64_record)))
					ch++;
			}
		} else if (e->record_count < rp_n) {
			rm = rp_n - e->record_count;
		} else {
			ad = e->record_count - rp_n;
		}
	}
	atomic_fetch_add_explicit(&c[0], 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&c[1], ch, memory_order_relaxed);
	atomic_fetch_add_explicit(&c[2], rm, memory_order_relaxed);
	atomic_fetch_add_explicit(&c[3], ad, memory_order_relaxed);
}

/* one line per pass and path: "vcc rec-pass: path=vir pass=copies_1 runs=N changed=N removed=N added=N" */
VPRIV void rp_print(codegen_unit *E)
{
	for (int v = 0; v < 2; v++)
		for (int p = 0; p < A64_RECORD_PASS_COUNT; p++) {
			unsigned long long r = E->record_pass_counts[v][p][0], ch = E->record_pass_counts[v][p][1], rm = E->record_pass_counts[v][p][2], ad = E->record_pass_counts[v][p][3];

			if (r)
				fprintf(stderr, "vcc rec-pass: path=%s pass=%s runs=%llu changed=%llu removed=%llu added=%llu\n",
					v ? "vir" : "direct", rp_names[p], r, ch, rm, ad);
		}
}

VPRIV void opt_function(codegen_context *e)
{
	a64_record *in = e->records;
	uint32_t n = 0;

	rawp = e->raw.p;
	retreads = RB(0) | RB(1);
	ast_qualified_type rt = T(e->compile_context, e->compile_context->syms[e->top_symbol].ty)->base;
	if (codegen_type_is_bool_vector(e->compile_context, rt)) {
		unsigned lanes = T(e->compile_context, rt)->n;
		if (lanes <= 8 && lanes != 2 && lanes != 4 && lanes != 8)
			retreads = RB(lanes) - 1;
	}
	if (e->vir_lowering) {	/* (VIR's functions: what the return value takes) */
		if (codegen_type_is_void(e->compile_context, rt) || (codegen_type_is_float(e->compile_context, rt) && codegen_type_size(e->compile_context, rt) <= 8))
			retreads = 0;
		else if ((codegen_type_is_integer(e->compile_context, rt) || codegen_type_is_pointer(e->compile_context, rt)) && codegen_type_size(e->compile_context, rt) <= 8)
			retreads = RB(0);
	}
	A64_RECORD_PASS_BEGIN(e);
	bool lv = liveness(e);

	A64_RECORD_PASS_END(e, A64_RECORD_PASS_LIVENESS);
	if (!lv)
		return;
	/* a temporary saved around a call and dead after its restore: neither */
	A64_RECORD_PASS_BEGIN(e);
	for (uint32_t k = 0; k < e->record_count; k++)
		if (in[k].opcode == A64_OP_LD && in[k].shift == A64_SPILL_RECORD && !(W.la[k] & RB(in[k].rd))) {
			for (uint32_t j = k; j-- > W.bs[W.blk[k]];)
				if (in[j].opcode == A64_OP_ST && in[j].shift == A64_SPILL_RECORD && in[j].rd == in[k].rd && in[j].immediate == in[k].immediate) {
					in[j].opcode = DELETED;
					break;
				}
			in[k].opcode = DELETED;
		}
	A64_RECORD_PASS_END(e, A64_RECORD_PASS_SPILL_PAIRS);
	/* copies first, so slots sees the registers values really stay in */
	A64_RECORD_PASS_BEGIN(e);
	copies(e);
	A64_RECORD_PASS_END(e, A64_RECORD_PASS_COPIES_FIRST);
	A64_RECORD_PASS_BEGIN(e);
	slots(e);
	A64_RECORD_PASS_END(e, A64_RECORD_PASS_SLOTS);
	A64_RECORD_PASS_BEGIN(e);
	exts(e);
	A64_RECORD_PASS_END(e, A64_RECORD_PASS_EXTENSIONS);
	A64_RECORD_PASS_BEGIN(e);
	copies(e);
	A64_RECORD_PASS_END(e, A64_RECORD_PASS_COPIES_SECOND);
	A64_RECORD_PASS_BEGIN(e);
	dead(e);
	A64_RECORD_PASS_END(e, A64_RECORD_PASS_DEAD);
	if (e->vir_lowering) {
		A64_RECORD_PASS_BEGIN(e);
		fold(e);
		A64_RECORD_PASS_END(e, A64_RECORD_PASS_FOLD);
		A64_RECORD_PASS_BEGIN(e);
		flags(e);
		A64_RECORD_PASS_END(e, A64_RECORD_PASS_FLAGS);
	}
	uint32_t dj = 0;

	for (uint32_t k = 0; k < e->record_count; k++) {
		if (e->debug_row_count)
			codegen_debug_rows_remap(e, &dj, k, n);
		if (in[k].opcode != DELETED)
			in[n++] = in[k];
	}
	if (e->debug_row_count)
		codegen_debug_rows_remap(e, &dj, UINT32_MAX, n);
	e->record_count = n;
}
