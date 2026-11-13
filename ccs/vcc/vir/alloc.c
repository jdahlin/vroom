/*
 * VIR register allocation (SPEC-vir.md 15.5-15.9): sweep 5 (backward:
 * liveness, next-use stack, live-in pool, lend, xcall, hints) and sweep 6
 * (forward: greedy assignment, farthest-next-use eviction, edge moves).
 * Inline assembly (15.10) is not handled: a function with asm operands fails.
 *
 * Preconditions besides canonical form (checked; vir_alloc fails otherwise):
 * block 0's `param`s come first, and the `proj`s of a call follow it
 * directly, since both read registers that the ABI fills before anything
 * else may run.
 *
 * Memory: one set of reservations per thread (22.2), made on first use;
 * per-value arrays are written before they are read, except nu/lend/end/hint
 * which sweep 5 clears for the values of the function (22.3).
 */
#include "virimpl.h"
#include "alloc.h"

#define KILL	0x80000000u
#define HREG	0x80000000u	/* hint[v]: a register (low byte) instead of a value */
#define NOPOS	0xffffffffu
#define FAR	((uint64_t)1 << 40)

enum { XCALL = 1, XLOOP = 2, NEEDSLOT = 4, DEAD = 8, CALLFIRST = 16, PCALL = 32, PLOOP = 64 };	/* PCALL/PLOOP: live after a call (in a loop), from the live sets */	/* CALLFIRST: a block parameter whose block calls before its first use */
#define NOWEB	0xffffffffu
#define WEBMAX	4096	/* a web with more members is not shared (the loop check walks them) */

typedef struct pmv { uint32_t dst, src, v; uint8_t kind; } pmv;

/* the thread's arrays; functions reach them through a->x (`ax`): a thread-local
 * access costs a call on Darwin */
static _Thread_local struct actx {
	bool init;
	/* per value */
	uint32_t *nu, *lend, *endp, *hint, *lastpos, *slot, *mark, *estamp, *dmark, *wi, *wd;
	uint8_t *reg, *vfl, *treg, *eloc, *sdef, *epst;
	uint32_t *sst, *tmk;	/* sst: stored on the current path when == bgen; tmk: conform's mark */
	/* webs (15.8): a block parameter and its `br` arguments, by union-find; per root: valid,
	 * one spill slot, one preferred register, live members during sweep 5 */
	uint32_t *wpar, *wslot, *wlive, *wlst, *wnext, *whead, *wcnt;
	uint8_t *wbad, *wreg, *wout;	/* wout: a member left out of its web (it interferes) */
	uint32_t wgen;
	/* per value: the loops it is live round (sweep 5's back-edge targets), as instruction
	 * ranges: rhead[v] -> rlo/rhi/rnext; livein, dmark and the webs take it as live there */
	uint32_t *rhead, *rnext, *rlo, *rhi, nrng;
	uint16_t *nop;	/* per instruction: its operands in ostk (sweep 5 -> 6) */
	/* sweep 5 */
	uint32_t *stk, *ostk, *pool, *lin_off, *lin_n, *soff, *succ, *bmark;
	/* per loop */
	uint8_t *lcall;	/* a call outside cold blocks in the loop (the loops inside included) */
	uint8_t *inp;	/* per block, with a save point: 1 in front of it (not dominated by it) */
	uint32_t *maxp, *lendpos, *lhead, *tend, *tendb, *tbeg, *apre, *tendi, *tendib;
	/* sweep 6 */
	uint32_t *ent_off, *ent_n, *end_off, *end_n, *pv;
	uint8_t *pr, *ps;	/* ps: the value is in its slot too (stored on that path) */
	uint32_t regval[256];
	uint64_t fr[2];
	/* output */
	uint8_t *opr, *ssz;
	uint32_t *boff, *eidx, *send, cfirst[256];
	vir_edit *edit;
	vir_aedge *edge;
	vir_emove *move;
	/* parallel moves */
	pmv *pm;
	uint32_t *lcnt, *lcs, *ldm, *lds, *rq, lgen;
	uint32_t ring[3][8], nring[3], cring[3];	/* by slot size: 4, 8, 16 */
	uint32_t sgen, mgen, bgen;	/* stamps that live across functions (22.3) */
	uint8_t vfm[VO_TARGET0];	/* per opcode: bits 0-2 the value fields a, b, c; 8: a list (vir_oit) */
} ax_tls;

#define R(p, n, sz) p = vreserve((size_t)(n) * (sz), MAP_NORESERVE, "vir: alloc")
static void ax_init(void)
{
	size_t nv = VIR_MAX_INS;

	if (ax_tls.init)
		return;
	for (uint32_t op = 0; op < VO_TARGET0; op++) {
		const vir_opinfo *oi = &vir_optab[op];
		uint8_t m = 0;

		if (oi->name)
			for (int k = 0; k < 3; k++)
				if ((oi->k[k] >= OK_T && oi->k[k] <= OK_BITC) || oi->k[k] == OK_CONSTV)
					m |= 1u << k;
		if (op == VO_CALL || op == VO_RET || op == VO_BR || op == VO_ASM || op == VO_ASMGOTO)
			m |= 8;
		ax_tls.vfm[op] = m;
	}
	R(ax_tls.nu, nv, 4); R(ax_tls.lend, nv, 4); R(ax_tls.endp, nv, 4); R(ax_tls.hint, nv, 4); R(ax_tls.lastpos, nv, 4);
	R(ax_tls.slot, nv, 4); R(ax_tls.mark, nv, 4); R(ax_tls.estamp, nv, 4); R(ax_tls.dmark, nv, 4); R(ax_tls.wi, nv, 4); R(ax_tls.wd, nv, 4);
	R(ax_tls.reg, nv, 1); R(ax_tls.vfl, nv, 1); R(ax_tls.treg, nv, 1); R(ax_tls.eloc, nv, 1);
	R(ax_tls.sdef, nv, 1); R(ax_tls.epst, nv, 1); R(ax_tls.sst, nv, 4); R(ax_tls.tmk, nv, 4);
	R(ax_tls.stk, (size_t)1 << 30, 4); R(ax_tls.ostk, (size_t)1 << 30, 4); R(ax_tls.nop, nv, 2); R(ax_tls.pool, nv, 4);
	R(ax_tls.lin_off, VIR_MAX_BLK, 4); R(ax_tls.lin_n, VIR_MAX_BLK, 4); R(ax_tls.soff, VIR_MAX_BLK + 1, 4);
	R(ax_tls.succ, VIR_MAX_EXTRA, 4); R(ax_tls.bmark, VIR_MAX_BLK, 4);
	R(ax_tls.lcall, VIR_MAX_BLK, 1); R(ax_tls.inp, VIR_MAX_BLK, 1); R(ax_tls.maxp, 2 * (size_t)VIR_MAX_BLK, 4); R(ax_tls.lendpos, VIR_MAX_BLK, 4); R(ax_tls.lhead, VIR_MAX_BLK, 4); R(ax_tls.tend, VIR_MAX_BLK, 4); R(ax_tls.tendb, VIR_MAX_BLK, 4); R(ax_tls.tbeg, VIR_MAX_BLK, 4); R(ax_tls.tendi, VIR_MAX_BLK, 4); R(ax_tls.tendib, VIR_MAX_BLK, 4); R(ax_tls.apre, VIR_MAX_BLK + 1, 4);
	R(ax_tls.ent_off, VIR_MAX_BLK, 4); R(ax_tls.ent_n, VIR_MAX_BLK, 4); R(ax_tls.end_off, VIR_MAX_BLK, 4); R(ax_tls.end_n, VIR_MAX_BLK, 4);
	R(ax_tls.pv, (size_t)VIR_MAX_BLK * 256, 4); R(ax_tls.pr, (size_t)VIR_MAX_BLK * 256, 1); R(ax_tls.ps, (size_t)VIR_MAX_BLK * 256, 1);
	R(ax_tls.opr, (size_t)1 << 32, 1); R(ax_tls.ssz, nv, 1);
	R(ax_tls.boff, 2 * (size_t)VIR_MAX_BLK + 2, 4); R(ax_tls.eidx, VIR_MAX_EXTRA, 4); R(ax_tls.send, nv, 4);
	R(ax_tls.edit, (size_t)1 << 30, sizeof(vir_edit)); R(ax_tls.edge, VIR_MAX_EXTRA, sizeof(vir_aedge));
	R(ax_tls.move, (size_t)1 << 30, sizeof(vir_emove));
	R(ax_tls.pm, (size_t)VIR_MAX_EXTRA, sizeof(pmv));
	R(ax_tls.wpar, nv, 4); R(ax_tls.wslot, nv, 4); R(ax_tls.wlive, nv, 4); R(ax_tls.wlst, nv, 4); R(ax_tls.wnext, nv, 4); R(ax_tls.whead, nv, 4); R(ax_tls.wcnt, nv, 4);
	R(ax_tls.wbad, nv, 1); R(ax_tls.rhead, nv, 4); R(ax_tls.rnext, (size_t)1 << 27, 4); R(ax_tls.rlo, (size_t)1 << 27, 4); R(ax_tls.rhi, (size_t)1 << 27, 4); R(ax_tls.wout, nv, 1); R(ax_tls.wreg, nv, 1);
	R(ax_tls.lcnt, 256 + nv, 4); R(ax_tls.lcs, 256 + nv, 4); R(ax_tls.ldm, 256 + nv, 4); R(ax_tls.lds, 256 + nv, 4); R(ax_tls.rq, VIR_MAX_EXTRA, 4);
	ax_tls.init = true;
}
#undef R

typedef struct A {
	vir_fn *f;
	const vir_loops *lp;
	const vir_regs *rg;
	vir_alloc_out *o;
	uint64_t poolmax;
	uint32_t sp, osp, npool, nopr, nedit, nmove, nedge, npair, nspill, nsucc;
	uint32_t cur_loop, mbase;
	uint64_t used_callee[2], pavoid[2];
	struct actx *x;
	uint64_t rmask[2];	/* registers regval may name: allocatable, and every fixed one seen */
	bool linear, failed;
	uint32_t spoint;	/* the save point (15.12), VIR_NOBLK none */
	bool pre;		/* the block being assigned is in front of it */
	uint32_t ops[512];
	uint8_t byte[512];
	uint8_t dn[512];	/* step 1: operand j is 0 fixed and not placed yet, 1 placed, 2 not fixed */
	uint32_t nxt[512];
} A;

#define ax (*a->x)

static inline bool is_const(const vir_fn *f, uint32_t v) { return v < f->nconst; }
static inline uint32_t vcls(const vir_fn *f, uint32_t v) { return vir_rclass(f->ins[v].ty); }
static inline uint32_t blk_last(const vir_fn *f, uint32_t b) { return f->blk[b].first + f->blk[b].n - 1; }

static uint32_t blk_of(const vir_fn *f, uint32_t v)	/* the block holding instruction v */
{
	uint32_t lo = 0, hi = f->nblk;

	while (lo < hi) {
		uint32_t m = lo + (hi - lo) / 2;

		if (f->blk[m].first + f->blk[m].n <= v)
			lo = m + 1;
		else
			hi = m;
	}
	return lo < f->nblk && f->blk[lo].first <= v ? lo : VIR_NOBLK;
}

/* ---------------- webs ---------------- */

static inline bool w_has(const A *a, uint32_t n, uint32_t v) { return ax.wi[v] < n && ax.wd[ax.wi[v]] == v; }

static uint32_t wroot(const A *a, uint32_t v)
{
	uint32_t r = ax.wpar[v];

	if (r == NOWEB)
		return NOWEB;
	while (ax.wpar[r] != r)
		r = ax.wpar[r] = ax.wpar[ax.wpar[r]];
	return r;
}

/* v's web when v shares it (no other member left in it is live where v is): NOWEB otherwise */
static inline uint32_t wvalid(const A *a, uint32_t v)
{
	uint32_t r = wroot(a, v);

	return r != NOWEB && !ax.wbad[r] && !ax.wout[v] ? r : NOWEB;
}

static void wsingle(A *a, uint32_t v)	/* while joining: whead/wlst/wnext list a web's parameters */
{
	const vir_fn *f = a->f;
	uint32_t d;

	if (ax.wpar[v] != NOWEB)
		return;
	d = blk_of(f, v);
	ax.wpar[v] = v;
	ax.wnext[v] = NOWEB;
	ax.wcnt[v] = d != VIR_NOBLK && v - f->blk[d].first < f->blk[d].nparam;
	ax.whead[v] = ax.wlst[v] = ax.wcnt[v] ? v : NOWEB;
}

/* join x's and y's webs unless both hold a parameter of the same block (those are live
 * together at its top: one of them would leave the web anyway) */
static void wunion(A *a, uint32_t x, uint32_t y, uint32_t *gen)
{
	const vir_fn *f = a->f;

	wsingle(a, x);
	wsingle(a, y);
	x = wroot(a, x);
	y = wroot(a, y);
	if (x == y)
		return;
	if (ax.wcnt[x] && ax.wcnt[y] && ax.wcnt[x] + ax.wcnt[y] <= 512) {
		++*gen;
		for (uint32_t p = ax.whead[x]; p != NOWEB; p = ax.wnext[p])
			ax.wlive[blk_of(f, p)] = *gen;
		for (uint32_t p = ax.whead[y]; p != NOWEB; p = ax.wnext[p])
			if (ax.wlive[blk_of(f, p)] == *gen)
				return;
	}
	if (x > y) {
		uint32_t t = x;

		x = y;
		y = t;
	}
	ax.wpar[y] = x;
	if (ax.whead[y] != NOWEB) {
		if (ax.whead[x] == NOWEB)
			ax.whead[x] = ax.whead[y];
		else
			ax.wnext[ax.wlst[x]] = ax.whead[y];
		ax.wlst[x] = ax.wlst[y];
	}
	ax.wcnt[x] += ax.wcnt[y];
}

/* the webs of f: each `br` argument joins its parameter (constants do not) */
static void webs(A *a)
{
	vir_fn *f = a->f;
	uint32_t gen = 0;

	memset(ax.wpar, 0xff, (size_t)f->nins * 4);
	memset(ax.wout, 0, f->nins);
	memset(ax.wlive, 0, (size_t)f->nblk * 4);
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_ins *t = &f->ins[blk_last(f, b)];

		if (t->op != VO_BR)
			continue;
		for (uint32_t j = 0; j < f->blk[t->a].nparam; j++) {
			uint32_t x = f->extra[t->c + j];

			if (!is_const(f, x))
				wunion(a, x, f->blk[t->a].first + j, &gen);
		}
	}
	for (uint32_t v = 0; v < f->nins; v++)
		if (ax.wpar[v] == v) {
			ax.wbad[v] = 0;
			ax.wslot[v] = VIR_NOBLK;
			ax.wreg[v] = VR_NONE;
			ax.wlst[v] = 0;
			ax.whead[v] = NOWEB;
			ax.wcnt[v] = 0;
		}
	for (uint32_t v = f->nins; v-- > 0;) {	/* the members of each web, in order */
		uint32_t r = wroot(a, v);

		if (r == NOWEB)
			continue;
		ax.wnext[v] = ax.whead[r];
		ax.whead[r] = v;
		if (++ax.wcnt[r] > WEBMAX)
			ax.wbad[r] = 1;
	}
	ax.wgen = 0;
}

/* v is live round a loop that contains instruction position pos */
static bool inrange(const A *a, uint32_t v, uint32_t pos)
{
	for (uint32_t k = ax.rhead[v]; k != NOWEB; k = ax.rnext[k])
		if (ax.rlo[k] <= pos && pos <= ax.rhi[k])
			return true;
	return false;
}

/* after sweep 5: a member z live round loops (its ranges, where livein and dmark take
 * it as live) interferes with a member defined there: z leaves, or, when z is a block
 * parameter (the web's meeting point), the members defined there leave */
static void webs_loops(A *a)
{
	vir_fn *f = a->f;

	for (uint32_t z = 0; z < f->nins; z++) {
		uint32_t r = wroot(a, z);

		if (r == NOWEB || ax.wbad[r] || ax.wout[z] || !ax.lend[z])
			continue;
		for (uint32_t m = ax.whead[r]; m != NOWEB; m = ax.wnext[m]) {
			if (m == z || ax.wout[m] || !(a->linear ? m > z && m <= ax.lend[z] : inrange(a, z, m)))
				continue;
			if (f->ins[z].op != VO_BPARAM) {
				ax.wout[z] = 1;
				break;
			}
			ax.wout[m] = 1;
		}
	}
	/* a web lives across calls (in a loop) when a member does: every member prefers a
	 * callee-saved register then, so the web keeps one register */
	for (uint32_t v = 0; v < f->nins; v++)
		if (ax.wpar[v] == v)
			ax.wlive[v] = 0;
	for (uint32_t v = 0; v < f->nins; v++) {
		uint32_t r = wvalid(a, v);

		if (r != NOWEB)
			ax.wlive[r] |= ax.vfl[v] & (XCALL | XLOOP);
	}
	for (uint32_t v = 0; v < f->nins; v++) {
		uint32_t r = wvalid(a, v);

		if (r != NOWEB && !(ax.vfl[v] & DEAD))
			ax.vfl[v] |= (uint8_t)ax.wlive[r];
	}
}

/* sweep 5: member v enters the live set; or leaves it at its definition, where another
 * live member means the two interfere */
static inline void wenter(A *a, uint32_t v)
{
	uint32_t r = wroot(a, v);

	if (r == NOWEB || ax.wout[v])
		return;
	if (ax.wlst[r] != ax.wgen) {
		ax.wlst[r] = ax.wgen;
		ax.wlive[r] = 0;
	}
	ax.wlive[r]++;
}

static inline void wleave(A *a, uint32_t v, uint32_t wn)
{
	uint32_t r = wroot(a, v);

	if (r == NOWEB || ax.wout[v] || ax.wlst[r] != ax.wgen)
		return;
	if (!--ax.wlive[r])
		return;
	/* defined where another member is live: a block parameter (the web's meeting point)
	 * stays and the live members leave; any other definition leaves */
	if (a->f->ins[v].op != VO_BPARAM) {
		ax.wout[v] = 1;
		return;
	}
	for (uint32_t j = 0; j < wn; j++) {
		uint32_t u = ax.wd[j];

		if (!ax.wout[u] && wroot(a, u) == r) {
			ax.wout[u] = 1;
			ax.wlive[r]--;
		}
	}
}

static inline void get_fix(const A *a, uint32_t i, vir_fix *x)
{
	vir_fix_clear(x);
	if (!a->rg->opfix || a->rg->opfix[a->f->ins[i].op])
		a->rg->fixed(a->f, i, x, a->rg->ctx);
}

static bool fail(A *a, const char *fmt, uint32_t x)
{
	if (!a->failed)
		snprintf(a->o->err, sizeof(a->o->err), fmt, x);
	a->failed = true;
	return false;
}

/* the value operands of instruction i in vir_oit order, without `multi` ones; n */
static uint32_t operands(A *a, uint32_t i)
{
	vir_ins in = a->f->ins[i];
	vir_oit it;
	uint32_t *p, n = 0;

	if (in.op < VO_TARGET0 && !(ax.vfm[in.op] & 8)) {	/* the fields alone: no iterator */
		uint32_t m = ax.vfm[in.op], w[3] = { in.a, in.b, in.c };

		for (int k = 0; k < 3; k++)
			if ((m >> k & 1) && a->f->ins[w[k]].ty != VT_MULTI)
				a->ops[n++] = w[k];
		return n;
	}

	vir_oit_init(&it, a->f, &in, false);
	while ((p = vir_oit_next(&it))) {
		if (a->f->ins[*p].ty == VT_MULTI)
			continue;
		if (n >= 512)
			return fail(a, "more than 512 operands at instruction %u", i), 0;
		a->ops[n++] = *p;
	}
	return n;
}

/* ---------------- sweep 5 ---------------- */

/* v's definition dominates block b, or either is unreachable: only then can v be live at b's
 * end (strict SSA: a path from the entry to b and on to a use would miss the definition) */
static bool defdom(A *a, uint32_t v, uint32_t b)
{
	const vir_fn *f = a->f;
	uint32_t d = blk_of(f, v);

	return d == VIR_NOBLK || f->blk[d].idom == VIR_NOBLK || f->blk[b].idom == VIR_NOBLK || vir_dominates(f, d, b);
}

/* v (whose live range ends at e) lives across a call: after one, by the live sets; a value
 * live round a loop (not in the live sets past its back edge) or any value in linear mode
 * by the order of instructions (a call before e) */
static uint8_t xcall(const A *a, uint32_t v, uint32_t lastcall, bool callloop, uint32_t e)
{
	if (!a->linear && ax.rhead[v] == NOWEB)
		return (ax.vfl[v] & PCALL ? XCALL : 0) | (ax.vfl[v] & PLOOP ? XLOOP : 0);
	return lastcall && lastcall < e ? XCALL | (callloop ? XLOOP : 0) : 0;
}

static bool sweep5(A *a)
{
	vir_fn *f = a->f;
	const vir_loops *lp = a->lp;
	uint32_t nb = f->nblk, wn = 0, cnt[2] = { 0, 0 }, lastcall = 0;
	bool callloop = false;

	memset(ax.nu, 0, (size_t)f->nins * 4);
	memset(ax.lend, 0, (size_t)f->nins * 4);
	memset(ax.rhead, 0xff, (size_t)f->nins * 4);
	ax.nrng = 0;
	memset(ax.endp, 0, (size_t)f->nins * 4);
	memset(ax.hint, 0, (size_t)f->nins * 4);
	memset(ax.vfl, 0, f->nins);
	memset(ax.maxp, 0, (size_t)lp->n * 8);
	memset(ax.lcall, 0, lp->n);
	a->sp = a->osp = 0;
	a->npool = 0;
	ax.wgen++;
	for (uint32_t b = nb; b-- > 0;) {
		const vir_blk *k = &f->blk[b];
		uint32_t li = lp->inner[b];
		bool cold = k->fl & VB_COLD;

		if (!a->linear) {
			wn = cnt[0] = cnt[1] = 0;
			ax.wgen++;
			for (uint32_t s = ax.soff[b]; s < ax.soff[b + 1]; s++) {
				uint32_t t = ax.succ[s];

				if (t <= b)
					continue;
				for (uint32_t j = 0; j < ax.lin_n[t]; j++) {
					uint32_t v = ax.pool[ax.lin_off[t] + j];

					if (!w_has(a, wn, v)) {
						ax.wi[v] = wn;
						ax.wd[wn++] = v;
						cnt[vcls(f, v)]++;
						wenter(a, v);
					}
				}
				/* an edge into a loop: the values live around the loop are live at t without
				 * being in its W when they are used only past a back edge (into the middle of
				 * a loop, or at its head when the loop is irreducible); they are the W of its
				 * back-edge targets, defined before t (and dominating b: live at b at all) */
				uint32_t lo = VIR_NOBLK;

				for (uint32_t l = lp->inner[t]; l != VIR_NOBLK && lp->l[l].head > b; l = lp->l[l].parent)
					lo = l;
				if (lo != VIR_NOBLK)
					for (uint32_t x = lp->l[lo].head; x <= lp->l[lo].end; x++) {
						if (!ax.tend[x])
							continue;
						for (uint32_t j = 0; j < ax.lin_n[x]; j++) {
							uint32_t v = ax.pool[ax.lin_off[x] + j];

							if (v < f->blk[t].first && !w_has(a, wn, v) && defdom(a, v, b)) {
								ax.wi[v] = wn;
								ax.wd[wn++] = v;
								cnt[vcls(f, v)]++;
								wenter(a, v);
							}
						}
					}
			}
		}
		for (uint32_t i = k->first + k->n; i-- > k->first;) {
			const vir_ins *in = &f->ins[i];
			vir_fix fx;
			uint32_t n;

			if (in->op == VO_BPARAM)
				break;
			get_fix(a, i, &fx);
			if (in->ty != VT_VOID && in->ty != VT_MULTI) {	/* the definition */
				uint32_t e = ax.endp[i] > ax.lend[i] ? ax.endp[i] : ax.lend[i];

				if (w_has(a, wn, i)) {
					uint32_t x = ax.wd[--wn];

					ax.wd[ax.wi[i]] = x;
					ax.wi[x] = ax.wi[i];
					cnt[vcls(f, i)]--;
					wleave(a, i, wn);
				}
				ax.endp[i] = e;
				ax.vfl[i] |= xcall(a, i, lastcall, callloop, e);
				if (!ax.nu[i] && !ax.lend[i])
					ax.vfl[i] |= DEAD;
				if ((fx.fl & VFX_TIED) && ax.hint[i] && in->a && !is_const(f, in->a) && !ax.hint[in->a])
					ax.hint[in->a] = ax.hint[i];
				if (fx.res != VR_NONE && !ax.hint[i])
					ax.hint[i] = HREG | fx.res;
			}
			if (in->op == VO_CALL && !cold) {
				lastcall = i;
				callloop = li != VIR_NOBLK;
				if (callloop)
					ax.lcall[li] = 1;
				if (!a->linear)	/* the values live after it (W, before its operands join) */
					for (uint32_t j = 0; j < wn; j++)
						ax.vfl[ax.wd[j]] |= PCALL | (callloop ? PLOOP : 0);
			}
			n = operands(a, i);
			if (a->failed)
				return false;
			/* sweep 6 takes them from here, in the opposite order of instructions */
			memcpy(&ax.ostk[a->osp], a->ops, (size_t)n * 4);
			a->osp += n;
			ax.nop[i] = (uint16_t)n;
			for (uint32_t j = n; j-- > 0;) {
				uint32_t v = a->ops[j];

				if (is_const(f, v)) {
					ax.stk[a->sp++] = ax.nu[v];
					ax.nu[v] = i;
					continue;
				}
				bool live = w_has(a, wn, v);

				ax.stk[a->sp++] = ax.nu[v] | (live ? 0 : KILL);
				if (!ax.nu[v])
					ax.endp[v] = i;
				ax.nu[v] = i;
				if (!live) {
					ax.wi[v] = wn;
					ax.wd[wn++] = v;
					cnt[vcls(f, v)]++;
					wenter(a, v);
				}
				if (j < fx.nopnd && fx.opnd[j] != VR_NONE && (in->op != VO_CALL || !live))
					ax.hint[v] = HREG | fx.opnd[j];
				else if (in->op == VO_BR && j < f->blk[in->a].nparam)
					ax.hint[v] = f->blk[in->a].first + j;
			}
			if (li != VIR_NOBLK) {
				uint32_t *m = &ax.maxp[2 * li];

				if (cnt[0] > m[0]) m[0] = cnt[0];
				if (cnt[1] > m[1]) m[1] = cnt[1];
			}
		}
		for (uint32_t j = k->nparam; j-- > 0;) {	/* block parameters: definitions at the top */
			uint32_t q = k->first + j, e = ax.endp[q] > ax.lend[q] ? ax.endp[q] : ax.lend[q];

			if (w_has(a, wn, q)) {
				uint32_t x = ax.wd[--wn];

				ax.wd[ax.wi[q]] = x;
				ax.wi[x] = ax.wi[q];
				cnt[vcls(f, q)]--;
				wleave(a, q, wn);
			}
			ax.endp[q] = e;
			ax.vfl[q] |= xcall(a, q, lastcall, callloop, e);
			if (!ax.nu[q] && !ax.lend[q])
				ax.vfl[q] |= DEAD;
			else if (lastcall && lastcall <= blk_last(f, b) && (!ax.nu[q] || ax.nu[q] > lastcall))
				ax.vfl[q] |= CALLFIRST;
		}
		/* a block entered with every value in its slot: an `ibr` target, or one with no earlier predecessor */
		if (b && (k->fl & VB_ADDR || ax.bmark[b])) {
			for (uint32_t j = 0; j < wn; j++)
				ax.vfl[ax.wd[j]] |= NEEDSLOT;
		}
		if (!a->linear) {
			if (a->npool + (uint64_t)wn > a->poolmax)
				return false;	/* the caller restarts in linear mode */
			ax.lin_off[b] = a->npool;
			ax.lin_n[b] = wn;
			memcpy(&ax.pool[a->npool], ax.wd, (size_t)wn * 4);
			a->npool += wn;
		}
		if (ax.tend[b]) {	/* the target of a back edge (a loop header, or an entry of an irreducible one) */
			uint32_t h = ax.tbeg[b];
			/* a block of the loop that is entered with everything in slots (before b too: the
			 * values are live around the whole loop) holds them without having them in its W */
			uint8_t ns = ax.apre[ax.tendb[b] + 1] > ax.apre[ax.tbeg[b]] ? NEEDSLOT : 0;
			uint8_t nsi = ax.apre[ax.tendib[b] + 1] > ax.apre[ax.tbeg[b]] ? NEEDSLOT : 0;

			for (uint32_t j = 0; j < wn; j++) {
				uint32_t v = ax.wd[j];
				/* one whose definition does not dominate the loop's head (one defined in the
				 * loop): a path from the head to b passes the definition, so it is live only to
				 * the end of the back edges that reach b without passing the head */
				uint32_t d = blk_of(f, v);
				bool in = d != VIR_NOBLK && f->blk[h].idom != VIR_NOBLK && f->blk[d].idom != VIR_NOBLK &&
					  (d == h || !vir_dominates(f, d, h));
				uint32_t e = in ? ax.tendi[b] : ax.tend[b];

				if (ax.lend[v] < e)
					ax.lend[v] = e;
				ax.vfl[v] |= in ? nsi : ns;
				if (ax.nrng < (1u << 27)) {
					ax.rlo[ax.nrng] = f->blk[ax.tbeg[b]].first;
					ax.rhi[ax.nrng] = e;
					ax.rnext[ax.nrng] = ax.rhead[v];
					ax.rhead[v] = ax.nrng++;
				} else {
					a->failed = true;
					snprintf(a->o->err, sizeof(a->o->err), "too many loop ranges");
					return false;
				}
			}
		}
	}
	/* loop pressure includes the loops inside */
	for (uint32_t l = lp->n; l-- > 0;) {
		uint32_t p = lp->l[l].parent;

		if (p != VIR_NOBLK) {
			for (int c = 0; c < 2; c++)
				if (ax.maxp[2 * l + c] > ax.maxp[2 * p + c])
					ax.maxp[2 * p + c] = ax.maxp[2 * l + c];
			ax.lcall[p] |= ax.lcall[l];
		}
	}
	return true;
}

/* ---------------- sweep 6: registers ---------------- */

static inline bool inreg(const A *a, uint32_t v) { return ax.reg[v] != VR_NONE && ax.regval[ax.reg[v]] == v; }

static inline void setreg(A *a, uint32_t v, uint32_t r)
{
	uint32_t w = wvalid(a, v);

	ax.regval[r] = v;
	ax.reg[v] = (uint8_t)r;
	ax.fr[VR_CLASS(r)] &= ~(1ull << VR_NUM(r));
	if (w != NOWEB && ax.wreg[w] == VR_NONE && a->f->ins[v].op == VO_BPARAM)
		ax.wreg[w] = (uint8_t)r;	/* the web's register: its first parameter's */
	if (a->rg->callee[VR_CLASS(r)] >> VR_NUM(r) & 1)
		a->used_callee[VR_CLASS(r)] |= 1ull << VR_NUM(r);
}

/* a new stamp for ax.estamp (never 0; the arrays are cleared on wrap-around) */
static uint32_t stamp(A *a)
{
	if (!++ax.sgen) {
		memset(ax.estamp, 0, (size_t)a->f->nins * 4);
		ax.sgen = 1;
	}
	return ax.sgen;
}

static inline void clearreg(A *a, uint32_t r)
{
	ax.regval[r] = 0;
	ax.fr[VR_CLASS(r)] |= 1ull << VR_NUM(r);
}

static void edit(A *a, uint32_t pos, uint32_t kind, uint32_t dst, uint32_t src, uint32_t v)
{
	vir_edit *e = &ax.edit[a->nedit++];

	e->pos = pos;
	e->kind = (uint8_t)kind;
	e->dst = (uint8_t)dst;
	e->src = (uint8_t)src;
	e->pad = 0;
	e->v = v;
	if (kind != VE_SWAP && kind != VE_STORE && dst < 128 && (a->rg->callee[VR_CLASS(dst)] >> VR_NUM(dst) & 1))
		a->used_callee[VR_CLASS(dst)] |= 1ull << VR_NUM(dst);	/* a register written only by an edit */
	switch (kind) {
	case VE_RELOAD: a->o->st.reloads++; break;
	case VE_REMAT: a->o->st.remats++; break;
	case VE_MOVE: a->o->st.moves++; break;
	case VE_SWAP: a->o->st.swaps++; break;
	case VE_STORE: a->o->st.stores++; break;
	}
}

static uint32_t defpos(const vir_fn *f, uint32_t v) { return v; }

/* a spill slot for v: shared with an earlier occupant whose range ended before v's definition */
static void need_slot(A *a, uint32_t v)
{
	vir_fn *f = a->f;
	uint32_t c, s = VIR_NOBLK;

	if (ax.slot[v] != VIR_NOBLK || is_const(f, v))
		return;
	c = vir_tysize(f->ins[v].ty) == 4 ? 0 : vir_tysize(f->ins[v].ty) == 16 ? 2 : 1;
	{
		uint32_t w = wvalid(a, v);

		if (w != NOWEB) {	/* one slot for the web, its own (outside the ring) */
			if (ax.wslot[w] == VIR_NOBLK) {
				ax.wslot[w] = a->nspill++;
				ax.ssz[ax.wslot[w]] = (uint8_t)(4u << c);
			}
			ax.slot[v] = ax.wslot[w];
			a->o->st.spills++;
			return;
		}
	}
	for (uint32_t j = 0; j < ax.nring[c]; j++) {
		uint32_t t = ax.ring[c][j];

		if (ax.send[t] < defpos(f, v)) {
			s = t;
			break;
		}
	}
	if (s == VIR_NOBLK) {
		s = a->nspill++;
		ax.ssz[s] = (uint8_t)(4u << c);
		ax.send[s] = 0;
		if (ax.nring[c] < 8) {
			ax.ring[c][ax.nring[c]++] = s;
		} else {
			ax.ring[c][ax.cring[c]] = s;
			ax.cring[c] = (ax.cring[c] + 1) & 7;
		}
	}
	if (ax.endp[v] > ax.send[s])
		ax.send[s] = ax.endp[v];
	if (defpos(f, v) > ax.send[s])
		ax.send[s] = defpos(f, v);
	ax.slot[v] = s;
	a->o->st.spills++;
}

static inline bool is_stored(A *a, uint32_t v) { return ax.sst[v] == ax.bgen; }
static inline void stored(A *a, uint32_t v) { ax.sst[v] = ax.bgen; }

/* v leaves register r before instruction pos: into its slot, stored there now unless it is
 * already (NOPOS: at a block's entry, where the edges into it store it) */
static void evict(A *a, uint32_t r, uint32_t pos)
{
	uint32_t v = ax.regval[r];

	if (v && ax.reg[v] == r && !is_const(a->f, v)) {
		need_slot(a, v);
		if (pos != NOPOS && !is_stored(a, v)) {
			edit(a, pos, VE_STORE, VR_NONE, r, v);
			stored(a, v);
		}
	}
	clearreg(a, r);
}

/* the distance to v's next use from position p (15.6) */
static uint64_t dist(A *a, uint32_t v, uint32_t p)
{
	const vir_loops *lp = a->lp;
	uint32_t n = ax.nu[v], l = a->cur_loop;
	uint64_t d;

	if (l != VIR_NOBLK) {
		uint32_t le = ax.lendpos[l];

		if ((n == 0 || n > le) && ax.lastpos[v] != NOPOS && ax.lastpos[v] >= ax.lhead[l] && ax.lend[v] >= le)
			return le - p;
	}
	if (!n)
		return FAR;
	d = n > p ? n - p : 0;
	for (; l != VIR_NOBLK && ax.lendpos[l] < n; l = lp->l[l].parent)
		d += 100000;
	return d;
}

static int lowbit(uint64_t m) { return m ? __builtin_ctzll(m) : -1; }

/* hint r is caller-saved, v lives across a call in a loop, and a callee-saved register is free */
static inline bool caller_for_call(const A *a, uint32_t v, uint32_t c, uint64_t m, uint32_t r)
{
	uint64_t cs = a->rg->callee[c];

	return (ax.vfl[v] & (XCALL | XLOOP)) == (XCALL | XLOOP) && !(cs >> VR_NUM(r) & 1) && (m & cs);
}

/* a free register of class c not in avoid, by v's hint and call preference; -1 if none */
static int choose(A *a, uint32_t v, uint32_t c, uint64_t avoid)
{
	uint64_t m = ax.fr[c] & a->rg->alloc[c] & ~avoid & ~a->pavoid[c], cs = a->rg->callee[c];
	uint32_t h = ax.hint[v];

	if (!m)
		return -1;
	if (a->pre && (m & ~cs))
		m &= ~cs;	/* in front of the save point: nothing that the frame has to save */
	if (h) {
		uint32_t r = VR_NONE;

		if (h & HREG)
			r = h & 0xff;
		else if (h < a->f->nins)
			r = inreg(a, h) ? ax.reg[h] : (a->f->ins[h].op == VO_BPARAM ? ax.treg[h] : VR_NONE);
		if (r < 128 && VR_CLASS(r) == c && (m >> VR_NUM(r) & 1) && !caller_for_call(a, v, c, m, r))
			return (int)r;
	}
	{
		uint32_t w = wvalid(a, v), r;

		if (w != NOWEB && (r = ax.wreg[w]) != VR_NONE && VR_CLASS(r) == c && (m >> VR_NUM(r) & 1) &&
		    !caller_for_call(a, v, c, m, r))
			return (int)r;
	}
	/* used next by an instruction with fixed operands (a call's arguments): not in the register
	 * another of its operands goes to, when there is a choice */
	if (ax.nu[v] && ax.nu[v] < a->f->nins) {
		vir_fix x;
		uint64_t o = 0;

		get_fix(a, ax.nu[v], &x);
		for (uint32_t j = 0; j < x.nopnd; j++)
			if (x.opnd[j] != VR_NONE && VR_CLASS(x.opnd[j]) == c)
				o |= 1ull << VR_NUM(x.opnd[j]);
		if (m & ~o)
			m &= ~o;
	}
	if ((ax.vfl[v] & XCALL) && (m & cs)) {	/* (a caller-saved one is moved to a callee-saved one at the call) */
		uint64_t u = m & cs & a->used_callee[c];

		return VR_MAKE(c, lowbit(u ? u : m & cs));
	}
	if (m & ~cs)
		return VR_MAKE(c, lowbit(m & ~cs));
	return VR_MAKE(c, lowbit(m));
}

/* the register to evict in class c (not in avoid), or -1 */
static int victim(A *a, uint32_t c, uint64_t avoid, uint32_t p)
{
	uint64_t m = a->rg->alloc[c] & ~avoid & ~ax.fr[c];
	int best = -1;
	uint64_t bd = 0;

	while (m) {
		uint32_t r = VR_MAKE(c, lowbit(m)), v = ax.regval[r];
		uint64_t d;

		m &= m - 1;
		d = dist(a, v, p) * 2 + (is_const(a->f, v) ? 1 : 0);	/* at equal distance a constant first */
		if (best < 0 || d > bd) {
			best = (int)r;
			bd = d;
		}
	}
	return best;
}

/* a register for v of class c at p, evicting if needed; -1 if every register is in avoid */
static int getreg(A *a, uint32_t v, uint32_t c, uint64_t avoid, uint32_t p)
{
	int r = choose(a, v, c, avoid);

	if (r >= 0)
		return r;
	r = victim(a, c, avoid, p);
	if (r < 0)
		return -1;
	evict(a, (uint32_t)r, p);
	return r;
}

/* bring v (not in a register) into r: rematerialize or reload */
static bool fetch(A *a, uint32_t v, uint32_t r, uint32_t p)
{
	if (is_const(a->f, v)) {
		edit(a, p, VE_REMAT, r, VR_NONE, v);
	} else {
		if (ax.slot[v] == VIR_NOBLK)
			return fail(a, "internal: value %u is in no register and has no slot", v);
		edit(a, p, VE_RELOAD, r, VR_NONE, v);
		stored(a, v);	/* (it was out of its register: in its slot) */
	}
	return true;
}

/* move w out of register r (it stays readable there until the instruction): to a free register
 * outside avoid, else evict it */
static void displace(A *a, uint32_t r, uint64_t *avoid, uint32_t p)
{
	uint32_t w = ax.regval[r], c = VR_CLASS(r);

	if (!w)
		return;
	if (ax.reg[w] != r || is_const(a->f, w)) {	/* a copy, or a constant: drop */
		clearreg(a, r);
		return;
	}
	int r2 = choose(a, w, c, avoid[c]);

	if (r2 >= 0) {
		edit(a, p, VE_MOVE, (uint32_t)r2, r, w);
		clearreg(a, r);
		setreg(a, w, (uint32_t)r2);
	} else {
		evict(a, r, p);
	}
}

/* Applied after operand placement, including terminators whose outgoing
 * state must describe the registers after the machine instruction. */
static void clobbers(A *a, const vir_fix *fx, const uint64_t *lock, const uint64_t *fixm, uint32_t pos)
{
	uint64_t avoid[2] = { lock[0] | fixm[0] | fx->clob[0] | fx->eclob[0],
	                      lock[1] | fixm[1] | fx->clob[1] | fx->eclob[1] };

	for (int c = 0; c < 2; c++) {
		uint64_t m = (fx->clob[c] | fx->eclob[c]) & a->rg->alloc[c];

		while (m) {
			uint32_t r = VR_MAKE(c, lowbit(m));

			m &= m - 1;
			if (ax.regval[r])
				displace(a, r, avoid, pos);
		}
	}
}

/* ---------------- sweep 6: edges ---------------- */

static bool livein(A *a, uint32_t v, uint32_t b);

static inline uint32_t lidx(uint32_t l) { return l & VL_SLOT ? 256 + (l & ~VL_SLOT) : l; }

static void emove(A *a, uint32_t kind, uint32_t dst, uint32_t src, uint32_t v)
{
	vir_emove *m = &ax.move[a->nmove++];
	vir_astats *s = &a->o->st;

	m->dst = dst;
	m->src = src;
	m->v = v;
	m->kind = (uint8_t)kind;
	m->pad[0] = m->pad[1] = m->pad[2] = 0;
	if (kind == VE_XCHG)
		s->xchg++;
	else if (kind == VE_REMAT)
		s->eremats++;
	else if (dst & VL_SLOT)
		s->estores++;
	else if (src & VL_SLOT)
		s->ereloads++;
	else
		s->emoves++;
	if (!(dst & VL_SLOT) && dst < 128 && (a->rg->callee[VR_CLASS(dst)] >> VR_NUM(dst) & 1)) {
		a->used_callee[VR_CLASS(dst)] |= 1ull << VR_NUM(dst);
	}
}

/* sequence the n parallel moves in ax.pm (unique destinations) into a->move; nmask: the
 * registers of the target's entry state (not free as temporaries) */
static void sequence(A *a, uint32_t n, const uint64_t *nmask)
{
	pmv *pm = ax.pm;
	uint32_t g = ++ax.lgen, done = 0;

	if (!g) {
		memset(ax.lcs, 0, (256 + (size_t)a->nspill) * 4);
		memset(ax.lds, 0, (256 + (size_t)a->nspill) * 4);
		g = ax.lgen = 1;
	}
	for (uint32_t i = 0; i < n; i++) {
		uint32_t d = lidx(pm[i].dst);

		ax.lds[d] = g;
		ax.ldm[d] = i;
		if (pm[i].kind != VE_REMAT) {
			uint32_t s = lidx(pm[i].src);

			if (ax.lcs[s] != g) {
				ax.lcs[s] = g;
				ax.lcnt[s] = 0;
			}
			ax.lcnt[s]++;
		}
	}
#define CNT(l) (ax.lcs[l] == g ? ax.lcnt[l] : 0)
	/* a worklist of moves whose destination nobody still reads; a move done may free the
	 * move that writes its source */
	uint32_t *rq = ax.rq, nr = 0, h = 0;	/* a queue: moves keep their order where they can */

	for (uint32_t i = 0; i < n; i++)
		if (!CNT(lidx(pm[i].dst)))
			rq[nr++] = i;
	while (done < n) {
		while (h < nr) {
			uint32_t i = rq[h++], d, kind = pm[i].kind;

			if (kind == 0xff || CNT(d = lidx(pm[i].dst)))
				continue;
			emove(a, kind, pm[i].dst, pm[i].src, pm[i].v);
			ax.lds[d] = 0;
			pm[i].kind = 0xff;
			done++;
			if (kind != VE_REMAT) {
				uint32_t s = lidx(pm[i].src);

				if (!--ax.lcnt[s] && ax.lds[s] == g)
					rq[nr++] = ax.ldm[s];
			}
		}
		if (done == n)
			break;
		h = nr = 0;
		/* a cycle: every pending destination is read by another pending move */
		for (uint32_t i = 0; i < n; i++) {
			uint32_t d, c, t = VR_NONE;

			if (pm[i].kind == 0xff)
				continue;
			d = lidx(pm[i].dst);
			c = vcls(a->f, pm[i].v);
			for (uint32_t j = 0; j < n; j++)
				if (pm[j].kind != 0xff && pm[j].kind != VE_REMAT && lidx(pm[j].src) == d) {
					c = vcls(a->f, pm[j].v);	/* the class of what d holds */
					break;
				}
			{
				uint64_t m = a->rg->alloc[c] & ~nmask[c];

				while (m) {
					uint32_t r = VR_MAKE(c, lowbit(m));

					m &= m - 1;
					if (!CNT(r) && ax.lds[r] != g) {
						t = r;
						break;
					}
				}
			}
			if (t == VR_NONE && !(pm[i].dst & VL_SLOT) && !(pm[i].src & VL_SLOT) && pm[i].kind == VE_MOVE) {
				uint32_t s = lidx(pm[i].src);

				/* exchange: d gets s's value (this move is done), s gets d's: d's readers read s */
				emove(a, VE_XCHG, pm[i].dst, pm[i].src, pm[i].v);
				ax.lcnt[s]--;
				for (uint32_t j = 0; j < n; j++)
					if (pm[j].kind != 0xff && pm[j].kind != VE_REMAT && j != i && lidx(pm[j].src) == d) {
						pm[j].src = pm[i].src;
						if (pm[j].src == pm[j].dst) {	/* the exchange already put it there */
							pm[j].kind = 0xff;
							ax.lds[s] = 0;
							done++;
						} else {
							ax.lcnt[s]++;
						}
					}
				ax.lcnt[d] = 0;
				ax.lds[d] = 0;
				pm[i].kind = 0xff;
				done++;
				break;
			}
			if (t == VR_NONE)
				t = VR_SCR(c);
			/* save d's content in t (the value its readers read); d's readers read t */
			emove(a, VE_MOVE, t, pm[i].dst, pm[i].v);
			for (uint32_t j = 0, lab = 0; j < n; j++)
				if (pm[j].kind != 0xff && pm[j].kind != VE_REMAT && lidx(pm[j].src) == d) {
					if (!lab++)
						ax.move[a->nmove - 1].v = pm[j].v;
					pm[j].src = t;
				}
			if (ax.lcs[t] != g)
				ax.lcs[t] = g;
			ax.lcnt[t] = ax.lcnt[d];
			ax.lcnt[d] = 0;
			break;
		}
		for (uint32_t i = 0; i < n; i++)	/* what the break freed */
			if (pm[i].kind != 0xff && !CNT(lidx(pm[i].dst)))
				rq[nr++] = i;
	}
#undef CNT
}

/* the edge from p (its successor number k) to b: moves that turn p's end state into b's entry state */
static void conform(A *a, uint32_t p, uint32_t k, uint32_t b)
{
	vir_fn *f = a->f;
	const vir_blk *t = &f->blk[b];
	uint32_t g = stamp(a), n = 0, term = blk_last(f, p), first = a->nmove;
	uint64_t nmask[2] = { 0, 0 };
	const vir_ins *ti = &f->ins[term];

	for (uint32_t j = 0; j < ax.end_n[p]; j++) {
		uint32_t v = ax.pv[ax.end_off[p] + j];

		ax.estamp[v] = g;
		ax.eloc[v] = ax.pr[ax.end_off[p] + j];
		ax.epst[v] = ax.ps[ax.end_off[p] + j];
	}
	for (uint32_t j = 0; j < ax.ent_n[b]; j++) {
		uint32_t r = ax.pr[ax.ent_off[b] + j];

		nmask[VR_CLASS(r)] |= 1ull << VR_NUM(r);
		ax.tmk[ax.pv[ax.ent_off[b] + j]] = g;
	}
	/* stores: b's entry has the value in its slot (out of a register, or stored on the
	 * earlier paths), p has it only in a register */
	for (uint32_t j = 0; j < ax.end_n[p]; j++) {
		uint32_t v = ax.pv[ax.end_off[p] + j];

		if (ax.ps[ax.end_off[p] + j] || is_const(f, v) || ax.slot[v] == VIR_NOBLK || ax.eloc[v] != ax.pr[ax.end_off[p] + j])
			continue;
		if (v >= t->first && v < t->first + t->nparam)
			continue;
		if (ax.tmk[v] == g) {
			uint32_t x;

			for (x = 0; x < ax.ent_n[b] && ax.pv[ax.ent_off[b] + x] != v; x++)
				;
			if (x == ax.ent_n[b] || !ax.ps[ax.ent_off[b] + x])
				continue;	/* in a register at b, not taken as stored */
		} else if (!livein(a, v, b)) {
			continue;
		}
		ax.pm[n++] = (pmv){ VL_SLOT | ax.slot[v], ax.eloc[v], v, VE_MOVE };
	}
	for (uint32_t j = 0; j < ax.ent_n[b]; j++) {
		uint32_t v = ax.pv[ax.ent_off[b] + j], r = ax.pr[ax.ent_off[b] + j];
		pmv *m = &ax.pm[n];

		if (v >= t->first && v < t->first + t->nparam)
			continue;	/* block parameters below */
		if (ax.estamp[v] == g) {
			if (ax.eloc[v] == r)
				continue;
			*m = (pmv){ r, ax.eloc[v], v, VE_MOVE };
		} else if (is_const(f, v)) {
			*m = (pmv){ r, 0, v, VE_REMAT };
		} else if (ax.slot[v] != VIR_NOBLK) {
			*m = (pmv){ r, VL_SLOT | ax.slot[v], v, VE_MOVE };
		} else {
			continue;	/* not defined on this path (15.5, irreducible) */
		}
		n++;
	}
	if (ti->op == VO_BR && ti->a == b)
		for (uint32_t j = 0; j < t->nparam; j++) {
			uint32_t q = t->first + j, x = f->extra[ti->c + j], dst, src;
			uint8_t r = ax.opr[ax.boff[b] + j];
			pmv *m = &ax.pm[n];

			if (r != VR_NONE)
				dst = r;
			else if (ax.slot[q] != VIR_NOBLK)
				dst = VL_SLOT | ax.slot[q];
			else
				continue;	/* unused parameter */
			if (is_const(f, x)) {
				*m = (pmv){ dst, 0, x, VE_REMAT };
				n++;
				continue;
			}
			if (ax.estamp[x] == g && ax.epst[x] && (dst & VL_SLOT) && ax.slot[x] == (dst & ~VL_SLOT))
				continue;	/* stored there already (a web's slot) */
			if (ax.estamp[x] == g)
				src = ax.eloc[x];
			else if (ax.slot[x] != VIR_NOBLK)
				src = VL_SLOT | ax.slot[x];
			else
				continue;
			if (src == dst)
				continue;
			*m = (pmv){ dst, src, x, VE_MOVE };
			n++;
		}
	if (n) {
		sequence(a, n, nmask);
		ax.eidx[ax.soff[p] + k] = a->nedge;
		ax.edge[a->nedge++] = (vir_aedge){ p, b, k, first, a->nmove - first };
		a->o->st.edges++;
	}
}

static void save_state(A *a, uint32_t *off, uint32_t *cnt, uint32_t b)
{
	off[b] = a->npair;
	for (int c = 0; c < 2; c++) {
		uint64_t m = a->rg->alloc[c] & ~ax.fr[c];

		while (m) {
			uint32_t r = VR_MAKE(c, lowbit(m)), v = ax.regval[r];

			m &= m - 1;
			if (v && ax.reg[v] == r) {
				ax.pv[a->npair] = v;
				ax.ps[a->npair] = is_stored(a, v);
				ax.pr[a->npair++] = (uint8_t)r;
			}
		}
	}
	cnt[b] = a->npair - off[b];
}

/* q, not read in b, is read near the top of each successor of b (two at most) before any call there (a
 * join's value the next test reads: in its slot it would be stored on each edge and loaded
 * at once) */
static bool read_next(A *a, uint32_t b, uint32_t q)
{
	vir_fn *f = (vir_fn *)a->f;

	if (ax.soff[b + 1] - ax.soff[b] > 2)
		return false;
	for (uint32_t s = ax.soff[b]; s < ax.soff[b + 1]; s++) {
		const vir_blk *k = &f->blk[ax.succ[s]];
		uint32_t end = k->first + (k->n < k->nparam + 16 ? k->n : k->nparam + 16);
		bool hit = false;

		for (uint32_t i = k->first + k->nparam; i < end && !hit && f->ins[i].op != VO_CALL; i++) {
			vir_oit it;
			uint32_t *p;

			vir_oit_init(&it, f, &f->ins[i], false);
			while ((p = vir_oit_next(&it)))
				hit |= *p == q;
		}
		if (!hit)
			return false;
	}
	return ax.soff[b + 1] > ax.soff[b];
}

static bool livein(A *a, uint32_t v, uint32_t b)
{
	const vir_fn *f = a->f;
	uint32_t first = f->blk[b].first;

	if (is_const(f, v)) {
		const vir_blk *k = &f->blk[b];
		uint32_t l = a->lp->inner[b];

		if (!ax.nu[v])
			return false;
		if (k->npred == 1)
			return true;
		return l != VIR_NOBLK && a->lp->l[l].head == b && ax.nu[v] <= ax.lendpos[l];
	}
	if (v >= first)
		return false;
	if (a->linear)
		return ax.endp[v] >= first;
	/* (live round a loop: in each of its blocks) */
	return ax.mark[v] == a->mbase + b || inrange(a, v, first);
}

/* parameter j of b (class c), tentatively in h: the register in which most of the
 * predecessors placed already end with their argument, when at least two do and more
 * than have it in h (their edges then move nothing: a loop whose common path passes
 * its values on unchanged pays for the rare path's moves otherwise); -1 if none */
static int arg_vote(A *a, uint32_t b, uint32_t j, uint32_t c, uint32_t h)
{
	const vir_fn *f = a->f;
	const vir_blk *k = &f->blk[b];
	uint8_t cnt[64] = { 0 };
	int best = -1;

	if (k->npred < 3 || k->npred > 32)
		return -1;
	for (uint32_t i = 0; i < k->npred; i++) {
		uint32_t p = f->extra[k->pred + i], x;
		const vir_ins *t;

		if (p >= b)
			continue;
		t = &f->ins[blk_last(f, p)];
		if (t->op != VO_BR || t->a != b || is_const(f, x = f->extra[t->c + j]))
			continue;
		for (uint32_t y = 0; y < ax.end_n[p]; y++)
			if (ax.pv[ax.end_off[p] + y] == x) {
				uint32_t r = ax.pr[ax.end_off[p] + y];

				if (r < 128 && VR_CLASS(r) == c && ++cnt[VR_NUM(r)] > (best < 0 ? 0 : cnt[best]))
					best = (int)VR_NUM(r);
				break;
			}
	}
	if (best < 0 || cnt[best] < 2 || (h < 128 && VR_CLASS(h) == c && cnt[VR_NUM(h)] >= cnt[best]))
		return -1;
	return (int)VR_MAKE(c, best);
}

/* The header b of loop l, entered from `best`: a value that is used in the loop and comes
 * in in its slot takes a register on the way in (the edges into b load it: once, in front
 * of the loop) when the loop leaves one free: beside the values in registers now, the
 * most the body has live at once beyond those live here (maxp) and one more, since maxp
 * does not see a value that is live round the loop below its last use.  In a loop with a call
 * only a callee-saved register keeps it.  Soonest use first. */
static void preload(A *a, uint32_t b, uint32_t l, uint32_t best)
{
	vir_fn *f = a->f;
	const vir_blk *k = &f->blk[b];
	uint32_t le = ax.lendpos[l], live[2] = { 0, 0 }, cand[16], nc = 0;

	for (uint32_t j = 0; j < k->nparam; j++)
		if (!(ax.vfl[k->first + j] & DEAD))
			live[vcls(f, k->first + j)]++;
	for (uint32_t j = 0; j < ax.lin_n[b]; j++) {
		uint32_t v = ax.pool[ax.lin_off[b] + j];

		if (is_const(f, v))
			continue;
		live[vcls(f, v)]++;
		if (nc < 16 && !inreg(a, v) && ax.slot[v] != VIR_NOBLK && ax.nu[v] && ax.nu[v] <= le)
			cand[nc++] = v;
	}
	while (nc) {
		uint32_t x = 0, v, c, nr, used, extra;
		uint64_t set;
		int r;
		bool st = true;

		for (uint32_t j = 1; j < nc; j++)
			if (ax.nu[cand[j]] < ax.nu[cand[x]])
				x = j;
		v = cand[x];
		cand[x] = cand[--nc];
		c = vcls(f, v);
		set = a->rg->alloc[c] & (ax.lcall[l] ? a->rg->callee[c] : ~0ull);
		nr = (uint32_t)__builtin_popcountll(set);
		used = (uint32_t)__builtin_popcountll(set & ~ax.fr[c]);
		extra = ax.maxp[2 * l + c] > live[c] ? ax.maxp[2 * l + c] - live[c] : 0;
		if (used + extra + 1 >= nr || (r = choose(a, v, c, ~set)) < 0)	/* (one is left for a temporary) */
			continue;
		setreg(a, v, (uint32_t)r);
		/* in its slot on the way from `best`; another earlier predecessor may hold it unstored */
		for (uint32_t j = 0; j < k->npred && st; j++) {
			uint32_t p = f->extra[k->pred + j];

			if (p >= b || p == best)
				continue;
			for (uint32_t y = 0; y < ax.end_n[p]; y++)
				if (ax.pv[ax.end_off[p] + y] == v && !ax.ps[ax.end_off[p] + y])
					st = false;
		}
		if (st)
			stored(a, v);
	}
}

static void enter(A *a, uint32_t b)
{
	vir_fn *f = a->f;
	const vir_blk *k = &f->blk[b];
	uint32_t best = VIR_NOBLK, bs = 0, l = a->lp->inner[b];
	bool relocate;

	if (!++ax.bgen) {	/* a new path state: nothing stored (the stamps wrapped: clear) */
		memset(ax.sst, 0, (size_t)VIR_MAX_INS * 4);
		ax.bgen = 1;
	}
	for (int c = 0; c < 2; c++)	/* the registers that can hold a value: allocatable or fixed */
		for (uint64_t m = a->rmask[c] & ~ax.fr[c]; m; m &= m - 1)
			ax.regval[VR_MAKE(c, lowbit(m))] = 0;
	ax.fr[0] = a->rg->alloc[0];
	ax.fr[1] = a->rg->alloc[1];
	if (!a->linear)
		for (uint32_t j = 0; j < ax.lin_n[b]; j++)
			ax.mark[ax.pool[ax.lin_off[b] + j]] = a->mbase + b;
	if (b && !(k->fl & VB_ADDR)) {
		for (uint32_t j = 0; j < k->npred; j++) {
			uint32_t p = f->extra[k->pred + j], s = 0;

			if (p >= b)
				continue;
			if (k->npred == 1) {	/* nothing to choose */
				best = p;
				break;
			}
			for (uint32_t x = 0; x < ax.end_n[p]; x++)
				s += livein(a, ax.pv[ax.end_off[p] + x], b);
			/* most live-in values in registers, then the deeper loop, not cold, the block before */
			s = s * 4 + (f->blk[p].fl & VB_COLD ? 0 : 2) + (p == b - 1);
			{
				uint32_t lp_ = a->lp->inner[p], dp = lp_ == VIR_NOBLK ? 0 : a->lp->l[lp_].depth;

				s += dp * 1024 * 1024;
			}
			if (best == VIR_NOBLK || s > bs) {
				best = p;
				bs = s;
			}
		}
		if (best != VIR_NOBLK) {
			for (uint32_t x = 0; x < ax.end_n[best]; x++) {
				uint32_t v = ax.pv[ax.end_off[best] + x];

				if (livein(a, v, b)) {
					setreg(a, v, ax.pr[ax.end_off[best] + x]);
					if (ax.ps[ax.end_off[best] + x])
						stored(a, v);
				}
			}
			/* stored on entry: on every earlier predecessor (one with it in a register
			 * and not in its slot clears it; a later one stores it on its edge) */
			for (uint32_t j = 0; j < k->npred; j++) {
				uint32_t p = f->extra[k->pred + j];

				if (p >= b || p == best)
					continue;
				for (uint32_t x = 0; x < ax.end_n[p]; x++)
					if (!ax.ps[ax.end_off[p] + x])
						ax.sst[ax.pv[ax.end_off[p] + x]] = 0;
			}
		}
	}
	/* the loop-header rule: drop values live through the loop without a use in it */
	if (l != VIR_NOBLK && a->lp->l[l].head == b)
		for (int c = 0; c < 2; c++) {
			uint32_t nr = (uint32_t)__builtin_popcountll(a->rg->alloc[c]), le = ax.lendpos[l];
			uint32_t excess = ax.maxp[2 * l + c] > nr ? ax.maxp[2 * l + c] - nr : 0;
			uint64_t m = a->rg->alloc[c] & ~ax.fr[c];

			while (m && excess) {
				uint32_t r = VR_MAKE(c, lowbit(m)), v = ax.regval[r];

				m &= m - 1;
				if (!is_const(f, v) && (ax.nu[v] == 0 || ax.nu[v] > le)) {
					evict(a, r, NOPOS);
					excess--;
				}
			}
		}
	/* block parameters */
	for (uint32_t j = 0; j < k->nparam; j++) {
		uint32_t q = k->first + j, c = vcls(f, q);
		int r = -1;

		ax.slot[q] = VIR_NOBLK;
		ax.reg[q] = VR_NONE;
		ax.lastpos[q] = q;
		ax.dmark[q] = 0;	/* (not an earlier function's: it is read for any value in a register) */
		if (!(ax.vfl[q] & DEAD) && !(k->fl & VB_ADDR)) {
			uint32_t h = ax.treg[q];
			int vr = arg_vote(a, b, j, c, h);

			if (vr >= 0 && (ax.fr[c] >> VR_NUM((uint32_t)vr) & 1) &&
			    !caller_for_call(a, q, c, ax.fr[c] & a->rg->alloc[c], (uint32_t)vr))
				h = (uint32_t)vr;
			if (h < 128 && VR_CLASS(h) == c && (ax.fr[c] >> VR_NUM(h) & 1) &&
			    !caller_for_call(a, q, c, ax.fr[c] & a->rg->alloc[c], h))
				r = (int)h;
			else
				r = choose(a, q, c, 0);
			/* a web's parameter that lives across a call and is not used soon (not in b nor
			 * at the top of each successor, or after a call, or only passed on by b's br)
			 * enters in the web's slot: the edges then move nothing for an argument already
			 * there */
			if (r >= 0 && wvalid(a, q) != NOWEB) {
				uint32_t u = ax.nu[q], e = blk_last(f, b);
				const vir_ins *t = &f->ins[e];
				bool late = (!u || u > e || (ax.vfl[q] & CALLFIRST)) &&
					    !((!u || u > e) && !(ax.vfl[q] & CALLFIRST) && read_next(a, b, q));

				if (u == e && t->op == VO_BR && t->a <= b) {
					/* only passed on, to a parameter placed already (a loop's header): where
					 * that one is */
					for (uint32_t x = 0; x < f->blk[t->a].nparam; x++)
						if (f->extra[t->c + x] == q) {
							uint8_t pr = ax.opr[ax.boff[t->a] + x];

							if (pr == VR_NONE)
								r = -1;
							else if (VR_CLASS(pr) == c && (ax.fr[c] >> VR_NUM(pr) & 1))
								r = pr;
							break;
						}
				} else if ((ax.vfl[q] & XCALL) && (late || (u == e && t->op == VO_BR)) &&
					   !((a->rg->callee[c] >> VR_NUM((uint32_t)r) & 1) &&
					     __builtin_popcountll(ax.fr[c] & a->rg->callee[c] & ~(1ull << VR_NUM((uint32_t)r))) >= 2)) {
					/* (a callee-saved register keeps it across the call, when others are free:
					 * otherwise the values used sooner need them) */
					r = -1;
				}
			}
		}
		ax.sdef[q] = 0;
		if (r >= 0) {
			setreg(a, q, (uint32_t)r);
			ax.treg[q] = (uint8_t)r;
		} else if (!(ax.vfl[q] & DEAD)) {
			need_slot(a, q);	/* the edges write the slot */
			stored(a, q);
		}
		if (ax.vfl[q] & NEEDSLOT) {
			need_slot(a, q);
			ax.sdef[q] = 1;	/* stored at the block's top */
			stored(a, q);
		}
		ax.opr[a->nopr++] = r >= 0 ? (uint8_t)r : VR_NONE;
	}
	if (l != VIR_NOBLK && a->lp->l[l].head == b && best != VIR_NOBLK && !a->linear)
		preload(a, b, l, best);
	save_state(a, ax.ent_off, ax.ent_n, b);
	relocate = b == a->spoint;
	/* forward edges into b */
	{
		uint32_t g = stamp(a);

		for (uint32_t j = 0; j < k->npred; j++) {
			uint32_t p = f->extra[k->pred + j];

			if (p >= b || ax.bmark[p] == g)	/* bmark doubles as the "done" stamp after sweep 5 */
				continue;
			ax.bmark[p] = g;
			for (uint32_t s = ax.soff[p]; s < ax.soff[p + 1]; s++)
				if (ax.succ[s] == b)
					conform(a, p, s - ax.soff[p], b);
		}
	}
	/* the save point: the values that came through the blocks in front of it in caller-saved
	 * registers and live across a call move to callee-saved ones here, behind the prologue
	 * (they would have been given them at their definitions) */
	if (relocate)
		for (int c = 0; c < 2; c++) {
			uint64_t m = a->rg->alloc[c] & ~ax.fr[c] & ~a->rg->callee[c];

			while (m) {
				uint32_t r = VR_MAKE(c, lowbit(m)), v = ax.regval[r];
				uint64_t fc = ax.fr[c] & a->rg->alloc[c] & a->rg->callee[c], u = fc & a->used_callee[c];
				uint32_t r2;

				m &= m - 1;
				if (!fc)
					break;
				if (!v || ax.reg[v] != r || is_const(f, v) || !(ax.vfl[v] & XCALL))
					continue;
				r2 = VR_MAKE(c, lowbit(u ? u : fc));
				edit(a, k->first + k->nparam, VE_MOVE, r2, r, v);
				clearreg(a, r);
				setreg(a, v, r2);
			}
		}
}

static bool instr(A *a, uint32_t b, uint32_t i, bool term)
{
	vir_fn *f = a->f;
	const vir_ins *in = &f->ins[i];
	vir_fix fx;
	uint32_t n = ax.nop[i], *ops = &ax.ostk[a->osp -= n];
	uint64_t lock[2] = { 0, 0 }, fixm[2], av[2];
	uint8_t *byte = a->byte;

	get_fix(a, i, &fx);
	if ((in->op == VO_ASM || in->op == VO_ASMGOTO) && n)
		return fail(a, "inline assembly with operands (15.10) at instruction %u is not supported", i);
	for (uint32_t j = 0; j < n; j++) {	/* pop in operand order */
		uint32_t w = ax.stk[--a->sp], v = ops[j];

		a->nxt[j] = w;
		ax.nu[v] = w & ~KILL;
		ax.lastpos[v] = i;
		/* dies here: the kill bit is on its last occurrence, which is assigned last */
		ax.dmark[v] = (is_const(f, v) ? !ax.nu[v] : (w & KILL) && (a->linear ? ax.lend[v] <= i : !inrange(a, v, i))) ? i : 0;
	}
	if (in->op == VO_BR) {
		for (uint32_t j = 0; j < n; j++)
			ax.opr[a->nopr++] = inreg(a, ops[j]) ? ax.reg[ops[j]] : VR_NONE;
		save_state(a, ax.end_off, ax.end_n, b);
		return true;
	}
	fixm[0] = fx.eclob[0];
	fixm[1] = fx.eclob[1];
	if (fx.res != VR_NONE)
		fixm[VR_CLASS(fx.res)] |= 1ull << VR_NUM(fx.res);
	for (uint32_t j = 0; j < n && j < fx.nopnd; j++)
		if (fx.opnd[j] != VR_NONE)
			fixm[VR_CLASS(fx.opnd[j])] |= 1ull << VR_NUM(fx.opnd[j]);
	a->rmask[0] |= fixm[0];
	a->rmask[1] |= fixm[1];
	/* 1: fixed operands, as a parallel move: first those whose register does not hold a
	 * fixed operand that is still to be placed (an argument that moves from one argument
	 * register to the next is copied before its register is written, not moved aside);
	 * in a cycle the lowest one, whose register's value is moved aside */
	for (uint32_t j = 0; j < n; j++) {
		byte[j] = VR_NONE;
		a->dn[j] = j < fx.nopnd && fx.opnd[j] != VR_NONE ? 0 : 2;
	}
	for (;;) {
		uint32_t j = n, first = n, v, R;

		for (uint32_t k = 0; k < n && k < fx.nopnd && j == n; k++) {
			uint32_t w;
			bool ok = true;

			if (a->dn[k])
				continue;
			if (first == n)
				first = k;
			w = ax.regval[fx.opnd[k]];
			if (w && w != ops[k])
				for (uint32_t y = 0; y < n && y < fx.nopnd && ok; y++)
					ok = a->dn[y] || y == k || ops[y] != w;
			if (ok)
				j = k;
		}
		if (j == n)
			j = first;
		if (j == n)
			break;
		a->dn[j] = 1;
		v = ops[j];
		R = fx.opnd[j];
		if (ax.regval[R] != v) {
			uint32_t w = ax.regval[R];
			bool dies = w && ax.dmark[w] == i;	/* (an operand here, read before the clobbers) */

			/* a value that dies here, with its every operand already placed (copied to its
			   fixed register): nothing reads it in R any more */
			for (uint32_t k = 0; dies && k < n; k++)
				if (k != j && ops[k] == w && a->dn[k] != 1)
					dies = false;
			if (dies && ax.reg[w] == R) {
				bool placed = false;

				for (uint32_t k = 0; k < n; k++)
					placed |= k != j && a->dn[k] == 1 && ops[k] == w;
				if (placed) {
					clearreg(a, R);
					goto free;
				}
			}
			dies = w && ax.dmark[w] == i;
			av[0] = lock[0] | fixm[0] | (dies ? 0 : fx.clob[0]);
			av[1] = lock[1] | fixm[1] | (dies ? 0 : fx.clob[1]);
			displace(a, R, av, i);
		free:
			if (inreg(a, v)) {
				edit(a, i, VE_MOVE, R, ax.reg[v], v);
				ax.regval[R] = v;	/* a copy: v's register stays where it is */
				ax.fr[VR_CLASS(R)] &= ~(1ull << VR_NUM(R));
			} else {
				if (!fetch(a, v, R, i))
					return false;
				setreg(a, v, R);
			}
		}
		lock[VR_CLASS(R)] |= 1ull << VR_NUM(R);
		byte[j] = (uint8_t)R;
	}
	/* 2: the other operands */
	for (uint32_t j = 0; j < n; j++) {
		uint32_t v = ops[j], c = vcls(f, v);
		bool mem = (j < 32 && (fx.mem >> j & 1)) || (j >= fx.nopnd && (fx.fl & VFX_RESTMEM));

		if (j < fx.nopnd && fx.opnd[j] != VR_NONE)
			continue;
		if (inreg(a, v)) {
			uint32_t r = ax.reg[v];

			if (fx.eclob[c] >> VR_NUM(r) & 1) {
				int r2 = getreg(a, v, c, lock[c] | fixm[c], i);

				if (r2 < 0)
					return fail(a, "no register for an operand at instruction %u", i);
				edit(a, i, VE_MOVE, (uint32_t)r2, r, v);
				clearreg(a, r);
				setreg(a, v, (uint32_t)r2);
				r = (uint32_t)r2;
			}
			lock[c] |= 1ull << VR_NUM(r);
			byte[j] = (uint8_t)r;
			continue;
		}
		if (mem && (is_const(f, v) || ax.slot[v] != VIR_NOBLK))
			continue;	/* an immediate, or memory */
		{
			int r = getreg(a, v, c, lock[c] | fixm[c], i);

			if (r < 0)
				return fail(a, "no register for an operand at instruction %u", i);
			if (!fetch(a, v, (uint32_t)r, i))
				return false;
			setreg(a, v, (uint32_t)r);
			lock[c] |= 1ull << VR_NUM(r);
			byte[j] = (uint8_t)r;
		}
	}
	for (uint32_t j = 0; j < n; j++)
		ax.opr[a->nopr++] = byte[j];
	if (term) {
		/* Terminators may use target temporaries too. Preserve live holders
		 * before recording the edge state; operands remain readable until
		 * the machine terminator clobbers these registers. */
		clobbers(a, &fx, lock, fixm, i);
		save_state(a, ax.end_off, ax.end_n, b);
		return true;
	}
	/* 3: the registers of operands that die here, and copies in fixed registers */
	for (uint32_t j = 0; j < n; j++) {
		uint32_t v = ops[j];

		if (byte[j] != VR_NONE && ax.regval[byte[j]] == v && ax.reg[v] != byte[j])
			clearreg(a, byte[j]);
		if (ax.dmark[v] == i && inreg(a, v))
			clearreg(a, ax.reg[v]);
	}
	/* 4: clobbers: live values leave the clobbered registers */
	clobbers(a, &fx, lock, fixm, i);
	/* 5: the result */
	if (in->ty != VT_VOID && in->ty != VT_MULTI) {
		uint32_t c = vcls(f, i);
		int r = -1;

		ax.slot[i] = VIR_NOBLK;
		ax.reg[i] = VR_NONE;
		ax.lastpos[i] = i;
		ax.dmark[i] = 0;
		if (fx.res != VR_NONE) {
			av[0] = lock[0] | fixm[0];
			av[1] = lock[1] | fixm[1];
			if (ax.regval[fx.res])
				displace(a, fx.res, av, i);
			r = fx.res;
		} else if (ax.vfl[i] & DEAD) {
			r = choose(a, i, c, lock[c]);
		} else if ((fx.fl & VFX_TIED) && n) {
			/* (not in place in a caller-saved register when the result lives across a call
			 * and a callee-saved one is free: one move here, not one at every call) */
			uint64_t fm = ax.fr[c] & a->rg->alloc[c] & ~lock[c] & ~a->pavoid[c];
			bool xc = byte[0] != VR_NONE && (ax.vfl[i] & (XCALL | XLOOP)) == (XCALL | XLOOP) &&
				  !(a->rg->callee[c] >> VR_NUM(byte[0]) & 1) && (fm & a->rg->callee[c]);

			/* an extension is an instruction in any register: in its hint's, when that is free */
			uint32_t h = ax.hint[i];
			bool hx = in->op == VO_EXT && (h & HREG) && VR_CLASS(h & 0xff) == c && (fm >> VR_NUM(h & 0xff) & 1) &&
				  (h & 0xff) != byte[0];

			if (hx)
				r = (int)(h & 0xff);
			else if (!xc && byte[0] != VR_NONE && ax.dmark[ops[0]] == i && !ax.regval[byte[0]])
				r = byte[0];
			else if (!xc && (fx.fl & VFX_COMMUT) && n >= 2 && byte[1] != VR_NONE && ax.dmark[ops[1]] == i && ops[1] != ops[0] &&
				 !ax.regval[byte[1]]) {
				edit(a, i, VE_SWAP, VR_NONE, VR_NONE, i);
				r = byte[1];
			} else {
				r = getreg(a, i, c, lock[c], i);
				if (r < 0 && byte[0] != VR_NONE) {
					/* every register holds an operand: the first one leaves for its slot (or is
					 * dropped, a constant) and the result is computed in place */
					evict(a, byte[0], i);
					r = byte[0];
				}
			}
		} else {
			r = choose(a, i, c, 0);
			if (r < 0)
				r = getreg(a, i, c, 0, i);
		}
		if (r < 0 && !(ax.vfl[i] & DEAD)) {
			snprintf(a->o->err, sizeof(a->o->err), "no register for the result of instruction %u (%s)", i,
				 vir_optab[in->op].name);
			a->failed = true;
			return false;
		}
		if (r >= 0) {
			uint32_t h = ax.hint[i];

			setreg(a, i, (uint32_t)r);
			if (h && !(h & HREG) && h < f->nins && f->ins[h].op == VO_BPARAM && ax.treg[h] == VR_NONE)
				ax.treg[h] = (uint8_t)r;	/* the first argument sets the parameter's tentative register */
		}
		ax.opr[a->nopr++] = r >= 0 ? (uint8_t)r : VR_NONE;
		ax.sdef[i] = 0;
		if (ax.vfl[i] & NEEDSLOT) {
			need_slot(a, i);
			ax.sdef[i] = 1;	/* stored at its definition */
			stored(a, i);
		}
		if ((ax.vfl[i] & DEAD) && r >= 0)
			clearreg(a, (uint32_t)r);
		/* a fixed result (a parameter, a call's) that lives across a later call leaves its
		 * caller-saved register now, for a callee-saved one: the register is free for the
		 * arguments computed before that call, instead of moved away at it */
		if (fx.res != VR_NONE && r >= 0 && !(ax.vfl[i] & DEAD) && (ax.vfl[i] & XCALL) && !a->pre &&
		    !(a->rg->callee[c] >> VR_NUM(r) & 1) && i + 1 <= blk_last(f, b)) {
			uint64_t m = ax.fr[c] & a->rg->alloc[c] & a->rg->callee[c] & ~a->pavoid[c];

			if (m) {
				uint64_t u = m & a->used_callee[c];
				uint32_t r2 = VR_MAKE(c, lowbit(u ? u : m));

				edit(a, i + 1, VE_MOVE, r2, (uint32_t)r, i);
				clearreg(a, (uint32_t)r);
				setreg(a, i, r2);
			}
		}
	}
	return true;
}

/* ---------------- the driver ---------------- */

static bool precond(A *a)
{
	vir_fn *f = a->f;
	const vir_blk *k = &f->blk[0];
	bool body = false;

	for (uint32_t i = k->first; i < k->first + k->n; i++) {
		if (f->ins[i].op == VO_PARAM && body)
			return fail(a, "`param` at instruction %u after other instructions", i);
		if (f->ins[i].op != VO_PARAM)
			body = true;
	}
	for (uint32_t i = f->nconst; i < f->nins; i++)
		if (f->ins[i].op == VO_PROJ) {
			uint32_t c = f->ins[i].a, p = i - 1;

			while (p > c && f->ins[p].op == VO_PROJ && f->ins[p].a == c)
				p--;
			if (p != c)
				return fail(a, "`proj` at instruction %u does not follow its call", i);
		}
	return true;
}

/*
 * Shrink-wrapping (15.12).  The save point S: the block that dominates every block that
 * needs the frame (a call that is not a sibling call, memcpy/memset, inline asm, va_start,
 * a slot's address, a block entered through its address), is in no loop, and dominates
 * every block that can be reached from it.  Each way through the function then passes S
 * once or never, and a return that S does not dominate is reached without the frame.
 * VIR_NOBLK when that is the entry, or the dominator tree is not in block order.
 */
static uint32_t ncd(const vir_fn *f, uint32_t x, uint32_t y)
{
	while (x != y) {
		uint32_t *hi = x > y ? &x : &y, d = f->blk[*hi].idom;

		if (d == VIR_NOBLK || d >= *hi)
			return 0;
		*hi = d;
	}
	return x;
}

/* block t only returns (through lone `br`s at most): the lowering gives each side of the
 * save point its own copy */
static bool ret_only(const vir_fn *f, uint32_t t)
{
	for (int hop = 0; hop < 8; hop++) {
		const vir_blk *k = &f->blk[t];
		const vir_ins *x = &f->ins[k->first + k->n - 1];

		if (k->fl & VB_ADDR)
			return false;
		if (k->n - k->nparam == 1 && x->op == VO_RET)
			return true;
		if (k->nparam || k->n != 1 || x->op != VO_BR)
			return false;
		t = x->a;
	}
	return false;
}

static uint32_t save_point(A *a, const vir_aopt *opt)
{
	vir_fn *f = a->f;
	uint32_t nb = f->nblk, s = VIR_NOBLK;
	bool ch = true;

	for (uint32_t b = 0; b < nb; b++) {
		const vir_blk *k = &f->blk[b];
		bool need = b && (k->fl & VB_ADDR);

		if (k->idom == VIR_NOBLK)
			return VIR_NOBLK;	/* (an unreachable block: not worth the care) */
		for (uint32_t i = k->first + k->nparam; i < k->first + k->n && !need; i++) {
			uint32_t n;

			switch (f->ins[i].op) {
			case VO_CALL:
				need = !(opt->noframe && opt->noframe[i]);
				break;
			case VO_ASM: case VO_ASMGOTO: case VO_MEMCPY: case VO_MEMSET: case VO_VA_START:
			case VO_FRAMEADDR: case VO_RETADDR: case VO_TLSADDR:
				need = true;
				break;
			}
			n = operands(a, i);
			if (a->failed)
				return VIR_NOBLK;
			for (uint32_t j = 0; j < n; j++)
				need |= f->ins[a->ops[j]].op == VO_SLOTADDR;
		}
		if (need)
			s = s == VIR_NOBLK ? b : ncd(f, s, b);
		if (!s)
			return VIR_NOBLK;
	}
	if (s == VIR_NOBLK)
		return VIR_NOBLK;
	while (ch) {
		ch = false;
		while (s && a->lp->inner[s] != VIR_NOBLK)
			s = f->blk[s].idom < s ? f->blk[s].idom : 0;
		if (!s)
			return VIR_NOBLK;
		for (uint32_t b = s; b < nb && !ch; b++) {
			if (!vir_dominates(f, s, b))
				continue;
			for (uint32_t x = ax.soff[b]; x < ax.soff[b + 1]; x++)
				if (!vir_dominates(f, s, ax.succ[x]) && !ret_only(f, ax.succ[x])) {
					s = ncd(f, s, ax.succ[x]);
					ch = true;
					break;
				}
		}
		if (!s)
			return VIR_NOBLK;
	}
	return s;
}

bool vir_alloc(vir_fn *f, const vir_loops *lp, const vir_regs *rg, const vir_aopt *opt, vir_alloc_out *out)
{
	A a_, *a = &a_;
	uint32_t nb = f->nblk, ns = 0;

	ax_init();
	memset(a, 0, sizeof(*a));
	a->x = &ax_tls;
	memset(out, 0, sizeof(*out));
	a->f = f;
	a->lp = lp;
	a->rg = rg;
	a->o = out;
	a->poolmax = opt && opt->pool ? opt->pool : VIR_MAX_INS;
	if (!nb || !precond(a))
		return false;
	if (!vir_cfg(f) || !vir_dom(f))
		return fail(a, "bad control-flow graph%s", 0);
	/* successors (CSR), loop end positions, blocks with no earlier predecessor */
	for (uint32_t b = 0; b < nb; b++) {
		vir_ins t = f->ins[blk_last(f, b)];
		vir_oit it;
		uint32_t *p;
		bool early = false;

		ax.soff[b] = ns;
		vir_oit_init(&it, f, &t, true);
		while ((p = vir_oit_next(&it))) {
			ax.eidx[ns] = VIR_NOBLK;
			ax.succ[ns++] = *p;
		}
		for (uint32_t j = 0; j < f->blk[b].npred; j++)
			early |= f->extra[f->blk[b].pred + j] < b;
		ax.bmark[b] = !early;
	}
	ax.soff[nb] = ns;
	a->nsucc = ns;
	/* apre[b]: blocks before b entered with every value in its slot */
	ax.apre[0] = 0;
	for (uint32_t b = 0; b < nb; b++)
		ax.apre[b + 1] = ax.apre[b] + (b && ((f->blk[b].fl & VB_ADDR) || ax.bmark[b]));
	for (uint32_t l = 0; l < lp->n; l++) {
		ax.lendpos[l] = blk_last(f, lp->l[l].end);
		ax.lhead[l] = f->blk[lp->l[l].head].first;
	}
	/* per back-edge target t: the end of the innermost loop that contains each edge s -> t (s >= t);
	 * values live into t are live to there (15.5).  In reducible flow t is a loop header and this
	 * is its loop's end; in irreducible flow t can be any entry of a merged range */
	for (uint32_t b = 0; b < nb; b++) {
		const vir_blk *k = &f->blk[b];

		ax.tend[b] = ax.tendb[b] = 0;
		for (uint32_t j = 0; j < k->npred; j++) {
			uint32_t p = f->extra[k->pred + j], l = lp->inner[b];

			if (p < b)
				continue;
			while (l != VIR_NOBLK && lp->l[l].end < p)
				l = lp->l[l].parent;
			if (l != VIR_NOBLK && ax.lendpos[l] > ax.tend[b]) {
				ax.tend[b] = ax.lendpos[l];
				ax.tendb[b] = lp->l[l].end;
				ax.tbeg[b] = lp->l[l].head;
			}
		}
		ax.tendi[b] = ax.tend[b];
		ax.tendib[b] = ax.tendb[b];
	}
	/* tendi: per loop, its back edges to other blocks than its head, merged where they
	 * overlap; a value live into such a target t whose definition does not dominate the head
	 * is live to the end of t's group, not round the loop (15.5) */
	for (uint32_t l = 0; l < lp->n; l++) {
		uint32_t h = lp->l[l].head, e = lp->l[l].end, gs = VIR_NOBLK, ge = 0;

		for (uint32_t x = h + 1; x <= e + 1; x++) {
			uint32_t sm = 0;

			if (x <= e)
				for (uint32_t j = 0; j < f->blk[x].npred; j++) {
					uint32_t p = f->extra[f->blk[x].pred + j];

					if (p >= x && p > sm)
						sm = p;
				}
			if (x <= e && !sm)
				continue;
			if (gs != VIR_NOBLK && x <= ge && x <= e) {
				if (sm > ge)
					ge = sm;
				continue;
			}
			/* a group [gs, ge] ends: its targets of this loop take ge */
			if (gs != VIR_NOBLK)
				for (uint32_t t = gs; t <= ge; t++)
					if (ax.tend[t] == ax.lendpos[l] && ax.tbeg[t] == h) {
						ax.tendi[t] = blk_last(f, ge);
						ax.tendib[t] = ge;
					}
			gs = x;
			ge = sm;
		}
	}
	webs(a);
	if (!sweep5(a)) {
		if (a->failed)
			return false;
		a->linear = true;
		out->st.linear = 1;
		webs(a);
		if (!sweep5(a))
			return false;
	}
	webs_loops(a);
	out->st.pool = a->npool;
	a->spoint = opt && opt->shrink && !a->linear ? save_point(a, opt) : VIR_NOBLK;
	if (a->failed)
		return false;
	if (a->spoint != VIR_NOBLK)
		for (uint32_t b = 0; b < nb; b++)
			ax.inp[b] = !vir_dominates(f, a->spoint, b);
	/* sweep 6 */
	for (uint32_t b = 0; b < nb; b++)
		ax.bmark[b] = 0;
	ax.nring[0] = ax.nring[1] = ax.nring[2] = ax.cring[0] = ax.cring[1] = ax.cring[2] = 0;
	for (uint32_t i = 0; i < f->nconst; i++)
		ax.reg[i] = VR_NONE;
	for (uint32_t r = 0; r < 256; r++)
		ax.cfirst[r] = VIR_NOBLK;
	memset(ax.slot, 0xff, (size_t)f->nins * 4);
	memset(ax.sdef, 0, f->nins);
	memset(ax.treg, VR_NONE, f->nins);
	if (ax.mgen + (uint64_t)nb + 1 >= UINT32_MAX) {
		memset(ax.mark, 0, (size_t)VIR_MAX_INS * 4);	/* rare: the stamps wrapped */
		ax.mgen = 0;
	}
	a->mbase = ax.mgen + 1;
	memset(ax.regval, 0, sizeof(ax.regval));
	ax.fr[0] = a->rmask[0] = rg->alloc[0];
	ax.fr[1] = a->rmask[1] = rg->alloc[1];
	ax.mgen += nb + 1;
	/* the incoming registers of block 0's params are not free before their param */
	for (uint32_t i = f->blk[0].first; i < f->blk[0].first + f->blk[0].n && f->ins[i].op == VO_PARAM; i++) {
		vir_fix fx;

		get_fix(a, i, &fx);
		if (fx.res != VR_NONE && !(ax.vfl[i] & DEAD))
			a->pavoid[VR_CLASS(fx.res)] |= 1ull << VR_NUM(fx.res);
	}
	for (uint32_t b = 0; b < nb; b++) {
		const vir_blk *k = &f->blk[b];
		uint64_t before[2] = { a->used_callee[0], a->used_callee[1] };

		a->cur_loop = lp->inner[b];
		a->pre = a->spoint != VIR_NOBLK && ax.inp[b];
		ax.boff[b] = a->nopr;
		ax.boff[nb + 1 + b] = a->nedit;
		enter(a, b);
		for (uint32_t i = k->first + k->nparam; i < k->first + k->n; i++) {
			if (f->ins[i].op == VO_PARAM) {
				vir_fix fx;

				get_fix(a, i, &fx);
				if (fx.res != VR_NONE)
					a->pavoid[VR_CLASS(fx.res)] &= ~(1ull << VR_NUM(fx.res));
			} else {
				a->pavoid[0] = a->pavoid[1] = 0;
			}
			if (!instr(a, b, i, i + 1 == k->first + k->n))
				return false;
		}
		for (int c = 0; c < 2; c++) {
			uint64_t m = a->used_callee[c] & ~before[c];

			while (m) {
				uint32_t r = VR_MAKE(c, lowbit(m));

				m &= m - 1;
				if (ax.cfirst[r] == VIR_NOBLK)
					ax.cfirst[r] = b;
			}
		}
		/* back edges from b */
		for (uint32_t s = ax.soff[b]; s < ax.soff[b + 1]; s++)
			if (ax.succ[s] <= b)
				conform(a, b, s - ax.soff[b], ax.succ[s]);
	}
	ax.boff[nb] = a->nopr;
	ax.boff[2 * nb + 1] = a->nedit;
	out->slot = ax.slot;
	out->sdef = ax.sdef;
	out->opr = ax.opr;
	out->nopr = a->nopr;
	out->boff = ax.boff;
	out->edit = ax.edit;
	out->nedit = a->nedit;
	out->edge = ax.edge;
	out->nedge = a->nedge;
	out->move = ax.move;
	out->nmove = a->nmove;
	out->eidx = ax.eidx;
	out->soff = ax.soff;
	out->ssz = ax.ssz;
	out->nspill = a->nspill;
	out->callee[0] = a->used_callee[0];
	out->callee[1] = a->used_callee[1];
	out->cfirst = ax.cfirst;
	out->spoint = a->spoint;
	out->st.slots = a->nspill;
	out->st.callee = (uint32_t)(__builtin_popcountll(a->used_callee[0]) + __builtin_popcountll(a->used_callee[1]));
	return true;
}
