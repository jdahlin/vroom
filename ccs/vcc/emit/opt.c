/* vcc code generator: record-level clean-ups once a function is generated */
#include "emitimpl.h"

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
 * cannot see into (inline asm, encoded words, brk) read every register, so
 * nothing moves across them; a function with an indirect branch is left as
 * it is.  Liveness is only ever made larger after a rewrite, never smaller,
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
enum { RD_R = 1, RN_R = 2, RM_R = 4, RA_R = 8, RD_W = 16, RA_W = 32, RN_W = 64, OP_ALL = 128, OP_CALL = 256 };
/* control */
enum { BR_NONE, BR_UNCOND, BR_COND, BR_END, BR_INDIRECT };

static unsigned roles(const ins *i)
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
	case I_BL: case I_TLV:
		return OP_CALL;
	case I_BLR:
		return RN_R | OP_CALL;
	case I_BR:
		return RN_R;
	case I_LABEL: case I_B: case I_BC: case I_NOP: case I_RET:
		return 0;
	default:	/* I_RAW, I_WORD, I_BRK, I_MOVSP: unknown registers */
		return OP_ALL;
	}
}

static int brkind(const ins *i)
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

static rmask reads(const ins *i, unsigned ro)
{
	rmask s = 0;

	if (ro & OP_ALL)
		return GPRS;
	if (ro & RD_R) s |= m(i->rd);
	if (ro & RN_R) s |= m(i->rn);
	if (ro & RM_R) s |= m(i->rm);
	if (ro & RA_R) s |= m(i->ra);
	if (ro & OP_CALL)
		s |= RB(9) - 1;	/* x0-x8: arguments, indirect result */
	if (i->op == I_RET)
		s |= RB(0) | RB(1);
	return s;
}

static rmask writes(const ins *i, unsigned ro)
{
	rmask s = 0;

	if (ro & RD_W) s |= m(i->rd);
	if (ro & RA_W) s |= m(i->ra);
	if (ro & RN_W) s |= m(i->rn);
	if (ro & OP_CALL)
		s |= CALLW;
	return s;
}

/* scratch, per thread: grows to the largest function */
static _Thread_local struct {
	rmask *la;		/* per record: live after it */
	uint32_t *blk;		/* per record: its block */
	uint32_t *bs, *be;	/* per block: first and last record */
	int32_t *s0, *s1;	/* per block: successors (-1 none) */
	rmask *in, *out, *use, *def;
	uint32_t *lpos;		/* per label: block that starts with it (+1, 0: not placed) */
	uint32_t cap, lcap;
} W;

static void grow(uint32_t n, uint32_t nl)
{
	if (n > W.cap) {
		uint32_t c = n * 2;

		W.la = realloc(W.la, c * sizeof(rmask));
		W.blk = realloc(W.blk, c * sizeof(uint32_t));
		W.bs = realloc(W.bs, c * sizeof(uint32_t));
		W.be = realloc(W.be, c * sizeof(uint32_t));
		W.s0 = realloc(W.s0, c * sizeof(int32_t));
		W.s1 = realloc(W.s1, c * sizeof(int32_t));
		W.in = realloc(W.in, c * sizeof(rmask));
		W.out = realloc(W.out, c * sizeof(rmask));
		W.use = realloc(W.use, c * sizeof(rmask));
		W.def = realloc(W.def, c * sizeof(rmask));
		if (!W.la || !W.blk || !W.bs || !W.be || !W.s0 || !W.s1 || !W.in || !W.out || !W.use || !W.def)
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
	for (uint32_t k = 0; k < n; k++) {
		if (k == 0 || in[k].op == I_LABEL || brkind(&in[k - 1]) != BR_NONE) {
			if (nb)
				W.be[nb - 1] = k - 1;
			W.bs[nb++] = k;
		}
		W.blk[k] = nb - 1;
		if (in[k].op == I_LABEL)
			W.lpos[in[k].label] = nb;	/* the label's block + 1 */
		if (in[k].op == I_BR)
			return false;
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
			if (!W.lpos[l->label])
				return false;	/* a branch to a label not in the records */
			W.s1[b] = (int32_t)W.lpos[l->label] - 1;
		}
		W.use[b] = W.def[b] = 0;
		for (uint32_t j = W.be[b] + 1; j-- > W.bs[b];) {
			unsigned ro = roles(&in[j]);
			rmask w = writes(&in[j], ro), r = reads(&in[j], ro);

			W.use[b] = (W.use[b] & ~w) | r;
			W.def[b] |= w;
		}
		W.in[b] = W.use[b];
		W.out[b] = 0;
	}
	do {
		changed = false;
		for (uint32_t b = nb; b-- > 0;) {
			rmask o = 0, i;

			if (b == nb - 1 && brkind(&in[W.be[b]]) != BR_UNCOND)
				o |= RB(0) | RB(1);	/* falls into the epilogue: the return value */
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
		rmask l = W.out[b];

		for (uint32_t j = W.be[b] + 1; j-- > W.bs[b];) {
			unsigned ro = roles(&in[j]);

			W.la[j] = l;
			l = (l & ~writes(&in[j], ro)) | reads(&in[j], ro);
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

/* mov t, v at k: uses of t read v instead */
static bool forward(ecx *e, uint32_t k)
{
	ins *in = e->in;
	int t = in[k].rd, v = in[k].rn;
	uint32_t b = W.blk[k], j, nrw = 0;
	static _Thread_local uint32_t rw[256];

	if (!(TEMPS & RB(t)))
		return false;
	for (j = k + 1; j <= W.be[b]; j++) {
		const ins *i = &in[j];
		unsigned ro;
		rmask r, w;

		if (i->op == DELETED)
			continue;
		ro = roles(i);
		if (ro & OP_ALL)
			return false;
		r = reads(i, ro), w = writes(i, ro);
		if (r & RB(t)) {
			if (!can_rewrite(i, ro, t) || nrw == 256)
				return false;
			rw[nrw++] = j;
		}
		if ((w & RB(t)) || !(W.la[j] & RB(t)))
			break;	/* t's value dies here */
		if (w & RB(v))
			return false;	/* v changes while t still holds the old value */
	}
	if (j > W.be[b])
		return false;
	for (uint32_t q = 0; q < nrw; q++)
		rewrite(&in[rw[q]], roles(&in[rw[q]]), t, v);
	for (uint32_t q = k; nrw && q < rw[nrw - 1]; q++)
		W.la[q] |= RB(v);	/* v is read further on now */
	in[k].op = DELETED;
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
		ro = roles(i);
		if (ro & (OP_ALL | OP_CALL))
			return false;
		r = reads(i, ro), w = writes(i, ro);
		if (w & RB(t)) {
			if (!(ro & RD_W) || i->rd != t || (ro & (RD_R | RA_W | RN_W)) || (w & ~RB(t)))
				return false;
			if (w & RB(v))
				return false;
			i->rd = (uint8_t)v;
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
VPRIV void opt_function(ecx *e)
{
	ins *in = e->in;
	uint32_t n = 0;

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
	for (uint32_t k = 0; k < e->nin; k++)
		if (is_copy(&in[k])) {
			if (!(W.la[k] & RB(in[k].rd)) && in[k].rd != 29 && in[k].rd != 30)
				in[k].op = DELETED;	/* dead */
			else
				forward(e, k);
		}
	for (uint32_t k = 0; k < e->nin; k++)
		if (is_copy(&in[k]))
			backward(e, k);
	for (uint32_t k = 0; k < e->nin; k++)
		if (in[k].op != DELETED)
			in[n++] = in[k];
	e->nin = n;
}
