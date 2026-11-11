/* vcc code generator: record-level clean-ups once a function is generated */
#include "../../../emit/emitimpl.h"

/*
 * Register liveness over the function's records (basic blocks split at
 * labels and after branches), then:
 *
 *   - forward copy propagation: `mov t, v` followed by reads of t, until t
 *     dies, becomes those reads of v (t a temporary, v not written meanwhile);
 *   - backward: `op t, ...; ...; mov v, t` with t dead after becomes
 *     `op v, ...` (nothing between reads or writes v, or reads t);
 *   - a mov to a dead register goes;
 *   - a temporary spilled around a call (SPILL_REC) and dead after the
 *     restore is neither stored nor restored.
 *
 * The emitter reads a register variable (B_LREG) into a fresh temporary and
 * stores one through a mov; these undo most of that.  Records the pass
 * cannot see into (basic asm, asm goto, encoded words) read every register,
 * so nothing moves across them; extended asm reads and writes just its
 * operands and clobbers (RAW_MASKS).  A function with an indirect branch is
 * left as it is.  Liveness is only ever made larger after a rewrite, never smaller,
 * so it stays an over-approximation.
 */

typedef uint64_t rmask;
#define RB(r) ((rmask)1 << (r))
#define GPRS (RB(31) - 1)						/* x0-x30 */
#define TEMPS ((RB(16) - RB(9)) | (RB(29) - RB(19)))			/* x9-x15, x19-x28 */
#define COPYOK ((RB(16) - 1) | (RB(29) - RB(19)))			/* x0-x15, x19-x28 */
#define CALLW ((RB(19) - 1) | RB(30))					/* x0-x18, lr */
#define DELETED 0xff

/* operand roles */
enum { RD_R = 1, RN_R = 2, RM_R = 4, RA_R = 8, RD_W = 16, RA_W = 32, RN_W = 64, OP_ALL = 128, OP_CALL = 256, OP_ASM = 512,
       OP_RET = 2048, OP_TLV = 4096 };
#define OP_SLOW (OP_ALL | OP_CALL | OP_ASM | OP_RET | OP_TLV)	/* reads()/writes() beyond the operand fields */

/* the function's raw text, for the registers of inline asm (RAW_MASKS) */
static _Thread_local const char *rawp;
static _Thread_local rmask retreads;
/* control */
enum { BR_NONE, BR_UNCOND, BR_COND, BR_END, BR_INDIRECT };

static unsigned roles_sw(const ins *i)
{
	switch (i->op) {
	case I_ADD: case I_SUB: case I_ADDS: case I_SUBS: case I_AND: case I_ORR: case I_EOR:
	case I_ANDS: case I_BIC: case I_ORN: case I_ADC: case I_SBC: case I_ADCS: case I_SBCS:
	case I_SDIV: case I_UDIV: case I_SMULH: case I_UMULH: case I_LSLV: case I_LSRV: case I_ASRV:
	case I_CSEL: case I_CSINC: case I_CSINV: case I_CSNEG:
		return RN_R | RM_R | RD_W;
	case I_ADDI: case I_SUBI: case I_ADDSI: case I_SUBSI: case I_ANDI: case I_ORRI: case I_EORI: case I_ANDSI:
	case I_MOV: case I_SBFM: case I_UBFM:
	case I_CLZ: case I_CLS: case I_RBIT: case I_REV: case I_REV16: case I_REV32:
	case I_LD: case I_ADDLO: case I_LDLO:
		return RN_R | RD_W;
	case I_BFM:
		return RD_R | RN_R | RD_W;
	case I_MADD: case I_MSUB:
		return RN_R | RM_R | RA_R | RD_W;
	case I_MOVI: case I_ADRP: case I_LDGOT: case I_ADR: case I_MRS:
		return RD_W;
	case I_ST: case I_STLO:
		return RD_R | RN_R;
	case I_LDP:
		return RN_R | RD_W | RA_W | (i->cond ? RN_W : 0);
	case I_STP:
		return RD_R | RA_R | RN_R | (i->cond ? RN_W : 0);
	case I_CBZ: case I_CBNZ:
		return RD_R;
	case I_PRFM:
		return RN_R;
	case I_BL:
		return OP_CALL;
	case I_TLV:	/* Darwin's TLV thunk keeps every register but x0 (the address); blr x16 */
		return OP_TLV;
	case I_BLR:
		return RN_R | OP_CALL;
	case I_BR:
		return RN_R;
	case I_RET:
		return OP_RET;	/* reads x0, x1 */
	case I_LABEL: case I_B: case I_BC: case I_NOP:
	case I_BRK:	/* a trap (BUG, WARN, __builtin_trap): the handler may print registers, needs none */
		return 0;
	case I_RAW:
		return i->cond == RAW_MASKS ? OP_ASM : OP_ALL;
	default:	/* I_RAW, I_WORD, I_MOVSP: unknown registers */
		return OP_ALL;
	}
}

/* roles and control by opcode, from the switches above; OP_VAR: depends on more than the opcode */
enum { OP_VAR = 1024 };
static uint16_t RT[256];
static uint8_t BT[256];
static uint8_t XT[256];	/* exts: an extension xredundant() may find redundant */
/* register field -> its bit (0: zr/sp/FRAME and other non-registers above 63) */
static rmask RBT[256];

static int brkind_sw(const ins *i);
__attribute__((constructor)) static void opt_tables(void)
{
	for (unsigned op = 0; op < 256; op++) {
		ins x = { .op = (uint8_t)op };

		RT[op] = (uint16_t)roles_sw(&x);
		BT[op] = (uint8_t)brkind_sw(&x);
	}
	RT[I_LDP] = RT[I_STP] = RT[I_RAW] = OP_VAR;
	XT[I_UBFM] = XT[I_SBFM] = XT[I_MOV] = XT[I_ANDI] = 1;
	for (unsigned r = 0; r < 64; r++)
		RBT[r] = RB(r);
}

static inline unsigned roles(const ins *i)
{
	unsigned r = RT[i->op];

	return r & OP_VAR ? roles_sw(i) : r;
}

static inline int brkind(const ins *i) { return BT[i->op]; }

static int brkind_sw(const ins *i)
{
	switch (i->op) {
	case I_B: return BR_UNCOND;
	case I_BC: case I_CBZ: case I_CBNZ: return BR_COND;
	case I_RET: return BR_END;
	case I_BR: return BR_INDIRECT;
	default: return BR_NONE;
	}
}

static inline rmask m(int r) { return r < 64 ? RB(r) : 0; }

/* reads(), writes() of calls, asm, ret and unknown records */
__attribute__((noinline)) static rmask reads_slow(const ins *i, unsigned ro)
{
	rmask s = 0;

	if (ro & OP_ALL)
		return GPRS;
	if (ro & OP_ASM) {
		memcpy(&s, rawp + i->off + i->ref, sizeof(s));
		return s & GPRS;
	}
	if (ro & RD_R) s |= m(i->rd);
	if (ro & RN_R) s |= m(i->rn);
	if (ro & RM_R) s |= m(i->rm);
	if (ro & RA_R) s |= m(i->ra);
	if (ro & OP_CALL)
		s |= RB(9) - 1;	/* x0-x8: arguments, indirect result */
	if (ro & OP_RET)
		s |= retreads;
	return s;
}

__attribute__((noinline)) static rmask writes_slow(const ins *i, unsigned ro)
{
	rmask s = 0;

	if (ro & OP_ASM) {
		memcpy(&s, rawp + i->off + i->ref + sizeof(s), sizeof(s));
		return s & GPRS;
	}
	if (ro & RD_W) s |= m(i->rd);
	if (ro & RA_W) s |= m(i->ra);
	if (ro & RN_W) s |= m(i->rn);
	if (ro & OP_CALL)
		s |= CALLW;
	if (ro & OP_TLV)
		s |= i->cond ? RB(0) | RB(1) | RB(30) : RB(0) | RB(16) | RB(30);
	return s;
}

static inline rmask reads(const ins *i, unsigned ro)
{
	if (ro & OP_SLOW)
		return reads_slow(i, ro);
	return (ro & RD_R ? RBT[i->rd] : 0) | (ro & RN_R ? RBT[i->rn] : 0) | (ro & RM_R ? RBT[i->rm] : 0) |
	       (ro & RA_R ? RBT[i->ra] : 0);
}

static inline rmask writes(const ins *i, unsigned ro)
{
	if (ro & OP_SLOW)
		return writes_slow(i, ro);
	return (ro & RD_W ? RBT[i->rd] : 0) | (ro & RA_W ? RBT[i->ra] : 0) | (ro & RN_W ? RBT[i->rn] : 0);
}

/* scratch, per thread: grows to the largest function */
static _Thread_local struct {
	rmask *la;		/* per record: live after it */
	rmask *rr, *ww;		/* per record: registers read, written (OP_ALL: rr GPRS), kept up to date by the rewrites */
	uint16_t *ro;		/* per record: roles() */
	uint32_t *blk;		/* per record: its block */
	uint32_t *bs, *be;	/* per block: first and last record */
	int32_t *s0, *s1;	/* per block: successors (-1 none) */
	rmask *in, *out, *use, *def;
	uint32_t *lpos;		/* per label: its block + 1 (UINT32_MAX: jumped to, not placed) */
	uint32_t cap, lcap;
} W;

static void grow(uint32_t n, uint32_t nl)
{
	if (n > W.cap) {
		uint32_t c = n * 2;

		W.la = realloc(W.la, c * sizeof(rmask));
		W.rr = realloc(W.rr, c * sizeof(rmask));
		W.ww = realloc(W.ww, c * sizeof(rmask));
		W.blk = realloc(W.blk, c * sizeof(uint32_t));
		W.bs = realloc(W.bs, c * sizeof(uint32_t));
		W.be = realloc(W.be, c * sizeof(uint32_t));
		W.s0 = realloc(W.s0, c * sizeof(int32_t));
		W.s1 = realloc(W.s1, c * sizeof(int32_t));
		W.in = realloc(W.in, c * sizeof(rmask));
		W.out = realloc(W.out, c * sizeof(rmask));
		W.use = realloc(W.use, c * sizeof(rmask));
		W.def = realloc(W.def, c * sizeof(rmask));
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
static bool liveness(ecx *e)
{
	const ins *in = e->in;
	uint32_t n = e->nin, nb = 0;
	bool changed;

	grow(n + 1, e->nlabel + 1);
	memset(W.lpos, 0, (size_t)e->nlabel * sizeof(uint32_t));
	/*
	 * Each record's roles and registers; labels something jumps to (+ asm,
	 * label addresses), as the others (scopes, inlining) split nothing.
	 */
	{
		rmask *rr = W.rr, *ww = W.ww;
		uint16_t *rop = W.ro;
		uint32_t *lpos = W.lpos;

		for (uint32_t k = 0; k < n; k++) {
			const ins *i = &in[k];
			unsigned ro = roles(i), bk = BT[i->op];

			rop[k] = (uint16_t)ro;
			rr[k] = reads(i, ro), ww[k] = writes(i, ro);
			if (bk == BR_INDIRECT)
				return false;
			if (bk - BR_UNCOND <= BR_COND - BR_UNCOND || (i->op == I_LABEL && (e->lrefc[i->label] & LREF_EXT)))
				lpos[i->label] = UINT32_MAX;
		}
	}
	for (uint32_t k = 0, prev = BR_UNCOND; k < n; k++) {
		const ins *i = &in[k];

		if (prev != BR_NONE || (i->op == I_LABEL && W.lpos[i->label])) {
			if (nb)
				W.be[nb - 1] = k - 1;
			W.bs[nb++] = k;
		}
		W.blk[k] = nb - 1;
		if (i->op == I_LABEL)
			W.lpos[i->label] = nb;	/* the label's block + 1 */
		prev = BT[i->op];
	}
	if (!n)
		return false;
	W.be[nb - 1] = n - 1;
	for (uint32_t b = 0; b < nb; b++) {
		const ins *l = &in[W.be[b]];
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
			rmask use = 0, def = 0;
			const rmask *rr = W.rr, *ww = W.ww;

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
			rmask o = 0, i;

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
		rmask l = W.out[b], *la = W.la;
		const rmask *rr = W.rr, *ww = W.ww;

		for (uint32_t j = W.be[b] + 1; j-- > W.bs[b];) {
			la[j] = l;
			l = (l & ~ww[j]) | rr[j];
		}
	}
	return true;
}

/* replace reads of t by v in record j: false if t is read where it is also written */
static bool can_rewrite(const ins *i, unsigned ro, int t)
{
	if ((ro & RD_R) && i->rd == t && (ro & RD_W))
		return false;	/* bfm t, ...: reads and writes rd */
	if ((ro & RN_R) && i->rn == t && (ro & RN_W))
		return false;	/* writeback */
	if ((ro & RA_R) && i->ra == t && (ro & RA_W))
		return false;
	return true;
}

static void rewrite(ins *i, unsigned ro, int t, int v)
{
	if ((ro & RD_R) && i->rd == t) i->rd = (uint8_t)v;
	if ((ro & RN_R) && i->rn == t) i->rn = (uint8_t)v;
	if ((ro & RM_R) && i->rm == t) i->rm = (uint8_t)v;
	if ((ro & RA_R) && i->ra == t) i->ra = (uint8_t)v;
}

static bool is_copy(const ins *i)
{
	return i->op == I_MOV && i->sz == 1 && i->rd < 64 && i->rn < 64 && i->rd != i->rn &&
	       (COPYOK & RB(i->rd)) && (COPYOK & RB(i->rn));
}

/* Operand policy for the shared transaction. Asm registers live in its text,
 * while tied read/write operands cannot be redirected independently.
 */
static bool a64_copy_rewrite(void *ctx, uint32_t j, uint8_t t, uint8_t v, bool apply)
{
	ecx *e = ctx;
	unsigned ro = W.ro[j];

	if (!apply)
		return !(ro & OP_ASM) && can_rewrite(&e->in[j], ro, t);
	rewrite(&e->in[j], ro, t, v);
	return true;
}

/* mov v, t at k with t dead after: the instruction that made t makes v */
static bool backward(ecx *e, uint32_t k)
{
	ins *in = e->in;
	int v = in[k].rd, t = in[k].rn;
	uint32_t b = W.blk[k];

	if (!(TEMPS & RB(t)) || (W.la[k] & RB(t)))
		return false;
	for (uint32_t j = k; j-- > W.bs[b];) {
		ins *i = &in[j];
		unsigned ro;
		rmask r, w;

		if (i->op == DELETED)
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
			in[k].op = DELETED;
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
 * records' SLOT_TAG) that every access reaches with an I_LD/I_ST of exactly
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
 * points into the allocation (or just past its end: SLOT_TAG2) the cell is
 * escaped: a call, inline asm or a store elsewhere may write it (forwarding
 * stops there) and anything may read it (no store goes).  An untagged
 * address turns the pass off for the function.
 */
static _Thread_local struct {
	em_slot_cells cells;
	int32_t *rc;		/* per record: its cell, or -1 */
	uint32_t caprc;
	em_slot_values values;
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
static void load_from_reg(ins *i, int r)
{
	int sz = i->sz, ex = i->cond;
	ins n = { .rd = i->rd, .rn = (uint8_t)r };

	if (r == ZR) {
		n.op = I_MOVI, n.sz = 1, n.rn = 0, n.imm = 0;
	} else if (sz == 3) {
		n.op = I_MOV, n.sz = 1;
	} else if (sz == 2 && ex != 1) {
		n.op = I_MOV, n.sz = 0;	/* ldr w: zero-extended */
	} else {
		n.op = ex ? I_SBFM : I_UBFM;
		n.sz = ex == 2 ? 0 : 1;	/* 2: sign-extended into w */
		n.imm = 0, n.sh = (uint8_t)(sz == 0 ? 7 : sz == 1 ? 15 : 31);
	}
	*i = n;
}

static void slots(ecx *e)
{
	ins *in = e->in;
	uint32_t n = e->nin, na = e->nsla + 1, ncell, nb, nst = 0;
	uint32_t ver[32] = { 0 }, mver = 0; /* pointer/call memory version */
	em_slot_cells *cells = &SL.cells;

	if (e->nsla > 0xffff || !n || !em_slot_begin(cells, na))
		return;
	for (uint32_t k = 0; k < n; k++) {
		const ins *i = &in[k];
		uint32_t id;

		if (i->rn != FRAME || i->op == DELETED)
			continue;
		id = SLOT_TAG(i);
		if (i->op == I_LD || i->op == I_ST) {
			em_slot_collect(cells, id, i->imm, 1u << i->sz);
		} else {
			if (!id)
				return; /* an address vcc cannot place */
			em_slot_escape(cells, id);
			if (SLOT_TAG2(i) < na)
				em_slot_escape(cells, SLOT_TAG2(i));
		}
	}
	if (!em_slot_finish(cells))
		return;
	ncell = cells->count;
	SL.rc = grow_to(SL.rc, &SL.caprc, n, sizeof(int32_t));
	for (uint32_t k = 0; k < n; k++) {
		const ins *i = &in[k];
		uint32_t id, cell;

		SL.rc[k] = -1;
		if (i->rn != FRAME || (i->op != I_LD && i->op != I_ST) || !(id = SLOT_TAG(i)))
			continue;
		cell = em_slot_find(cells, id, i->imm, 1u << i->sz);
		if (!(cells->flags[cell] & EM_SLOT_BAD))
			SL.rc[k] = (int32_t)cell;
	}
	if (!em_slot_values_begin(&SL.values, ncell))
		return;
	nb = W.blk[n - 1] + 1;
	for (uint32_t b = 0; b < nb; b++) {
		uint32_t gen = b + 1;

		for (uint32_t k = W.bs[b]; k <= W.be[b]; k++) {
			ins *i = &in[k];
			int32_t ci = SL.rc[k];
			em_slot_value *a = ci >= 0 ? em_slot_value_get(&SL.values, (uint32_t)ci, gen, i->op == I_LD) : NULL;
			bool esc = ci >= 0 && (cells->flags[ci] & EM_SLOT_ESCAPED);
			rmask w;

			if (i->op == DELETED)
				continue;
			if (a && i->op == I_LD) {
				uint32_t version = a->value < ZR ? ver[a->value] : a->version;

				if (em_slot_value_available(a, version, mver, esc)) {
					int r = (int)a->value;

					if (r != ZR)
						for (uint32_t q = a->pos; q < k; q++)
							W.la[q] |= RB(r);
					a->pos = k;	/* (r is live up to here now) */
					load_from_reg(i, r);
					W.ro[k] = (uint16_t)roles(i);
					W.rr[k] = reads(i, W.ro[k]);
					SL.rc[k] = -1;
					if (i->op == I_MOV && i->sz == 1 && i->rd == i->rn) {
						i->op = DELETED;
						continue;
					}
				}
			}
			/* (a load made a mov writes the same register); OP_ALL may write any */
			w = (W.rr[k] == GPRS ? GPRS : W.ww[k]) & GPRS;
			for (; w; w &= w - 1)
				ver[__builtin_ctzll(w)]++;
			if (SL.rc[k] < 0) {
				if (i->op == I_ST || i->op == I_STLO || i->op == I_STP || i->op == I_BL || i->op == I_BLR ||
				    i->op == I_TLV || i->op == I_RAW || W.rr[k] == GPRS)
					mver++;	/* may write an escaped cell */
				continue;
			}
			uint32_t overwritten = em_slot_value_record(a, i->op == I_LD, esc, i->rd,
				i->rd == ZR ? 0 : ver[i->rd], mver, k);

			if (i->op == I_ST)
				SL.rc[nst++] = (int32_t)k; /* rc of earlier records is done with */
			if (overwritten != EM_SLOT_NO_STORE)
				in[overwritten].op = DELETED;
		}
	}
	/* a cell no load reads: its stores go */
	for (uint32_t q = 0; q < nst; q++) {
		ins *i = &in[SL.rc[q]];
		uint32_t cell = SLOT_TAG(i);

		if (i->op != I_ST)
			continue;	/* (deleted meanwhile) */
		cell = em_slot_find(cells, cell, i->imm, 1u << i->sz);
		if (em_slot_value_dead(&SL.values, cell, cells->flags[cell] & EM_SLOT_ESCAPED))
			i->op = DELETED;
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
 * label that inline asm or a label address may reach (LREF_EXT) start
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
static bool xresult(const xf *f, const ins *i, unsigned *zo, unsigned *so)
{
	int rn = i->rn, rm = i->rm, x = i->sz;
	unsigned z = XU, s = XU, w = x ? 64 : 32;
	unsigned zn = xz(f, rn), sn = xs(f, rn);

	switch (i->op) {
	case I_LD: case I_LDLO:
		if (i->sz == 3)
			return false;
		if (i->cond == 0)
			z = 8u << i->sz;
		else if (i->cond == 1)
			s = 8u << i->sz;
		else
			z = 32;	/* sign-extended into w */
		goto done;
	case I_MOVI: {
		uint64_t v = x ? (uint64_t)i->imm : (uint32_t)i->imm;

		z = zbits(v), s = sbits((int64_t)v);
		goto done;
	}
	case I_MOV:
		z = zn, s = sn;
		break;
	case I_CSINC:
		if (rn != ZR || rm != ZR)
			return x ? false : (*zo = 32, *so = 33, true);
		z = 1;	/* cset */
		break;
	case I_CSINV:
		if (rn != ZR || rm != ZR)
			return x ? false : (*zo = 32, *so = 33, true);
		s = 1;	/* csetm */
		break;
	case I_CSEL:
		z = umax(zn, xz(f, rm)), s = umax(sn, xs(f, rm));
		break;
	case I_UBFM: {
		unsigned r = (unsigned)i->imm, h = i->sh;

		if (h >= r)	/* bits r..h to 0 (lsr, ubfx) */
			z = umin(h - r + 1, zn > r ? zn - r : 0);
		else		/* bits 0..h to w-r (lsl, ubfiz) */
			z = umin(h + 1, zn) + (w - r);
		break;
	}
	case I_SBFM: {
		unsigned r = (unsigned)i->imm, h = i->sh;

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
	case I_ANDI: {
		uint64_t v = x ? (uint64_t)i->imm : (uint32_t)i->imm;

		z = umin(zn, zbits(v));
		if (x && (int64_t)v < 0 && zbits(~v) < sn)
			s = sn;	/* the mask keeps every bit from the sign up */
		break;
	}
	case I_AND: case I_ORR: case I_EOR:
		if (i->sh)
			break;
		if (i->op == I_AND)
			z = umin(zn, xz(f, rm));
		else
			z = umax(zn, xz(f, rm));
		s = umax(sn, xs(f, rm));
		break;
	case I_ORRI: case I_EORI: {
		uint64_t v = x ? (uint64_t)i->imm : (uint32_t)i->imm;

		z = umax(zn, zbits(v)), s = umax(sn, sbits((int64_t)v));
		break;
	}
	case I_ADD:
		if (i->sh)
			break;
		{
			unsigned a = umax(zn, xz(f, rm)), b = umax(sn, xs(f, rm));

			z = a < XU ? a + 1 : XU, s = b < XU ? b + 1 : XU;
		}
		break;
	case I_ADDI: case I_SUBI: {
		int64_t v = i->imm;

		if (v == INT64_MIN)
			break;
		if (i->op == I_SUBI)
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
	case I_LSRV:
		z = zn;
		break;
	case I_ASRV:
		z = zn;
		if (x)
			s = sn;
		break;
	case I_UDIV:
		z = zn;
		break;
	case I_CLZ: case I_CLS:
		z = 7;
		break;
	default:
		if (x || i->op == I_ADRP || i->op == I_ADR || i->op == I_LDGOT || i->op == I_MRS || i->op == I_RAW)
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
static bool xredundant(const xf *f, const ins *i)
{
	int rn = i->rn;
	unsigned zn = xz(f, rn), sn = xs(f, rn);

	if (rn >= 31)
		return false;
	switch (i->op) {
	case I_UBFM:
		return i->imm == 0 && zn <= (unsigned)i->sh + 1u;
	case I_SBFM:
		return i->imm == 0 && (zn < (unsigned)i->sh + 1u || (i->sz && sn <= (unsigned)i->sh + 1u));
	case I_MOV:
		return !i->sz && zn <= 32;
	case I_ANDI: {
		uint64_t v = i->sz ? (uint64_t)i->imm : (uint32_t)i->imm;
		uint64_t lo = zn >= 64 ? ~0ull : (1ull << zn) - 1;

		return zn <= (i->sz ? 64u : 32u) && (v & lo) == lo;
	}
	default:
		return false;
	}
}

static inline void xkill(xf *f, rmask w)
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
static void xblock(ecx *e, uint32_t b, xf *f, uint8_t *red)
{
	const ins *in = e->in;

	for (uint32_t k = W.bs[b]; k <= W.be[b]; k++) {
		const ins *i = &in[k];
		unsigned z, s;
		int rd = i->rd;

		if (i->op == DELETED)
			continue;
		if (W.rr[k] == GPRS) {	/* OP_ALL */
			memset(f, XU, sizeof(*f));
			continue;
		}
		if (!W.ww[k])
			continue;
		if (XT[i->op])
			red[k] = (uint8_t)xredundant(f, i);
		if (W.ww[k] == RB(rd) && rd < 31 && (COPYOK & RB(rd)) && xresult(f, i, &z, &s)) {
			f->z[rd] = (uint8_t)z, XS(f)[rd] = (uint8_t)s;
			continue;
		}
		xkill(f, W.ww[k]);
	}
}

static void exts(ecx *e)
{
	uint32_t n = e->nin, nb = W.blk[n - 1] + 1;
	xf f;
	bool changed;
	uint32_t nc = 0;

	for (uint32_t k = 0; k < n; k++) {
		const ins *i = &e->in[k];

		nc += XT[i->op] && (i->op == I_ANDI || (i->op == I_MOV ? !i->sz : !i->imm));
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
		const ins *l = &e->in[W.bs[b]];

		XF.vis[b] = b == 0 || (l->op == I_LABEL && (e->lrefc[l->label] & LREF_EXT)) ? 2 : 0;
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
			ins *i = &e->in[k];

			i->op = i->rd == i->rn ? DELETED : I_MOV;
			i->sz = 1;
		}
}

/* ---------------- dead results ---------------- */

/* an instruction with no effect but its rd (no flags, memory, traps) */
static bool pure(const ins *i)
{
	switch (i->op) {
	case I_ADD: case I_SUB: case I_AND: case I_ORR: case I_EOR: case I_BIC: case I_ORN: case I_ADC: case I_SBC:
	case I_ADDI: case I_SUBI: case I_ANDI: case I_ORRI: case I_EORI:
	case I_MOVI: case I_MOV: case I_MADD: case I_MSUB:
	case I_SDIV: case I_UDIV: case I_SMULH: case I_UMULH: case I_LSLV: case I_LSRV: case I_ASRV:
	case I_SBFM: case I_UBFM: case I_BFM: case I_CSEL: case I_CSINC: case I_CSINV: case I_CSNEG:
	case I_CLZ: case I_CLS: case I_RBIT: case I_REV: case I_REV16: case I_REV32:
	case I_ADRP: case I_ADDLO: case I_LDGOT: case I_ADR:
		return true;
	case I_LD:	/* a stack slot vcc placed (volatile ones are untagged) */
		return i->rn == FRAME && SLOT_TAG(i);
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
static void dead(ecx *e)
{
	ins *in = e->in;
	uint32_t nb = W.blk[e->nin - 1] + 1;

	for (uint32_t b = 0; b < nb; b++) {
		rmask live = W.out[b];

		if (b == nb - 1 && brkind(&in[W.be[b]]) != BR_UNCOND)
			live |= retreads;
		for (uint32_t k = W.be[b] + 1; k-- > W.bs[b];) {
			ins *i = &in[k];

			if (i->op == DELETED)
				continue;
			if (i->rd < 29 && !(live & RB(i->rd)) && pure(i)) {
				i->op = DELETED;
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
VPRIV uint64_t opt_regs(ecx *e, unsigned *fl)
{
	const ins *in = e->in;
	rmask u = 0;
	unsigned f = 0;

	rawp = e->raw.p;
	for (uint32_t k = 0; k < e->nin; k++) {
		const ins *i = &in[k];
		unsigned ro = roles(i);
		rmask r = reads(i, ro), w = writes(i, ro);

		if (ro & OP_ALL)
			f |= OPF_ALL;
		if (ro & OP_CALL)
			f |= OPF_CALL, w &= ~CALLW;	/* (a call's own clobbers are not the function's) */
		if (ro & OP_ASM)
			r &= ~(RB(18) | RB(29) | RB(30));
		if (i->rn == FRAME)
			f |= OPF_FRAME;
		u |= r | w;
	}
	*fl = f;
	return u;
}

/* copy propagation (forward, then backward) and dead copies */
static void copies(ecx *e)
{
	ins *in = e->in;
	const em_rec_copy_scan scan = {
		.records = in, .stride = sizeof(ins), .op_offset = offsetof(ins, op), .deleted_op = DELETED,
		.flags = W.ro, .flags_stride = sizeof(*W.ro), .opaque_mask = OP_ALL,
		.read = W.rr, .write = W.ww, .live = W.la, .rewrite = a64_copy_rewrite, .ctx = e,
	};

	for (uint32_t k = 0; k < e->nin; k++) {
		if (in[k].op == I_MOV && in[k].sz == 1 && in[k].rd == in[k].rn && in[k].rd < 31) {
			in[k].op = DELETED;	/* mov x, x */
			continue;
		}
		if (is_copy(&in[k])) {
			if (!(W.la[k] & RB(in[k].rd)) && in[k].rd != 29 && in[k].rd != 30)
				in[k].op = DELETED;	/* dead */
			else if (TEMPS & RB(in[k].rd))
				em_rec_copy_forward(&scan, k, W.be[W.blk[k]], in[k].rd, in[k].rn);
		}
	}
	for (uint32_t k = 0; k < e->nin; k++)
		if (is_copy(&in[k]))
			backward(e, k);
}

VPRIV void opt_function(ecx *e)
{
	ins *in = e->in;
	uint32_t n = 0;

	rawp = e->raw.p;
	retreads = RB(0) | RB(1);
	qty rt = T(e->c, e->c->syms[e->top].ty)->base;
	if (is_bvec(e->c, rt)) {
		unsigned lanes = T(e->c, rt)->n;
		if (lanes <= 8 && lanes != 2 && lanes != 4 && lanes != 8)
			retreads = RB(lanes) - 1;
	}
	if (!liveness(e))
		return;
	/* a temporary saved around a call and dead after its restore: neither */
	for (uint32_t k = 0; k < e->nin; k++)
		if (in[k].op == I_LD && in[k].sh == SPILL_REC && !(W.la[k] & RB(in[k].rd))) {
			for (uint32_t j = k; j-- > W.bs[W.blk[k]];)
				if (in[j].op == I_ST && in[j].sh == SPILL_REC && in[j].rd == in[k].rd && in[j].imm == in[k].imm) {
					in[j].op = DELETED;
					break;
				}
			in[k].op = DELETED;
		}
	/* copies first, so slots sees the registers values really stay in */
	copies(e);
	slots(e);
	exts(e);
	copies(e);
	dead(e);
	uint32_t dj = 0;

	for (uint32_t k = 0; k < e->nin; k++) {
		if (e->ndl)
			dbg_remap(e, &dj, k, n);
		if (in[k].op != DELETED)
			in[n++] = in[k];
	}
	if (e->ndl)
		dbg_remap(e, &dj, UINT32_MAX, n);
	e->nin = n;
}
