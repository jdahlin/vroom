/* vcc code generator, arm64: register names, record emission, FP moves
 * (included at the end of codegen/emitimpl.h) */

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

/* Address-valued arbitrary-width integers, canonical little-endian limbs. */
VPRIV val a64_bigconst(ecx *e, qty t, const uint32_t *words, i128 small);
VPRIV val a64_bigbin(ecx *e, int op, qty t, val a, val b);
VPRIV val a64_bigcmp(ecx *e, int op, qty t, val a, val b);
VPRIV val a64_bigcvt(ecx *e, val v, qty from, qty to);
VPRIV val a64_bigbool(ecx *e, val v, qty t);
VPRIV val a64_bigcopy(ecx *e, val v, qty t);

/*
 * VCC_REC_STATS: what each record clean-up pass of finish_function does, per TU, for the
 * functions that came through VIR's lowering and through the direct emitter (opt.c rp_snap,
 * rp_count, rp_print).  The names are those of doc/quality/VIR-MIGRATION.md's table.
 */
enum {
	RP_FUNC,	/* the functions that reached the clean-ups (-O1 and up) */
	RP_UNREACHABLE, RP_FALLTHROUGH, RP_LIVENESS, RP_SPILL_PAIRS, RP_COPIES1, RP_SLOTS, RP_EXTS,
	RP_COPIES2, RP_DEAD, RP_FOLD, RP_FLAGS, RP_OPT_REGS, RP_EXPAND_TABLES, RP_NOFRAME,
	RP_TAIL_EXPAND, RP_FRAME_REWRITE, RP_TBZ_RELAX,
	RP_N
};
VPRIV void rp_snap(ecx *e);
VPRIV void rp_count(ecx *e, int pass);
VPRIV void rp_print(emitter *E);
/* a counter increment behind the statistics flag: without the switch, one untaken branch */
#define RP_BEGIN(e) do { if (unlikely((e)->E->rstats)) rp_snap(e); } while (0)
#define RP_END(e, p) do { if (unlikely((e)->E->rstats)) rp_count(e, p); } while (0)
