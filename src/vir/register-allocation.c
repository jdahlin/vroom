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
#include "vir-internal.h"
#include "vir/register-allocation.h"

#define VIR_ALLOC_KILL	0x80000000u
#define VIR_ALLOC_HINT_REGISTER	0x80000000u	/* hint[v]: a register (low byte) instead of a value */
#define VIR_ALLOC_NO_POSITION	0xffffffffu
#define VIR_ALLOC_FAR	((uint64_t)1 << 40)

enum { VIR_ALLOC_FLAG_CROSSES_CALL = 1, VIR_ALLOC_FLAG_CROSSES_LOOP = 2, VIR_ALLOC_FLAG_NEEDS_SLOT = 4, VIR_ALLOC_FLAG_DEAD = 8, VIR_ALLOC_FLAG_CALL_FIRST = 16, VIR_ALLOC_FLAG_LIVE_AFTER_CALL = 32, VIR_ALLOC_FLAG_LIVE_AFTER_LOOP_CALL = 64 };	/* VIR_ALLOC_FLAG_LIVE_AFTER_CALL/VIR_ALLOC_FLAG_LIVE_AFTER_LOOP_CALL: live after a call (in a loop), from the live sets */	/* VIR_ALLOC_FLAG_CALL_FIRST: a block parameter whose block calls before its first use */
#define VIR_ALLOC_NO_WEB	0xffffffffu
#define VIR_ALLOC_WEB_MAX	4096	/* a web with more members is not shared (the loop check walks them) */

typedef struct vir_alloc_parallel_move { uint32_t dst, src, value; uint8_t kind; } vir_alloc_parallel_move;

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
	vir_alloc_edge *edge;
	vir_edge_move *move;
	/* parallel moves */
	vir_alloc_parallel_move *pm;
	uint32_t *lcnt, *lcs, *ldm, *lds, *rq, lgen;
	uint32_t ring[3][8], nring[3], cring[3];	/* by slot size: 4, 8, 16 */
	uint32_t sgen, mgen, bgen;	/* stamps that live across functions (22.3) */
	uint8_t vfm[VIR_OP_TARGET_FIRST];	/* per opcode: bits 0-2 the value fields a, b, c; 8: a list (vir_operand_iterator) */
} vir_alloc_thread_arrays_instance;

#define R(p, n, sz) p = vreserve((size_t)(n) * (sz), MAP_NORESERVE, "vir: alloc")
static void vir_alloc_thread_arrays_init(void)
{
	size_t nv = VIR_MAX_INS;

	if (vir_alloc_thread_arrays_instance.init)
		return;
	for (uint32_t op = 0; op < VIR_OP_TARGET_FIRST; op++) {
		const vir_opcode_info *oi = &vir_opcode_table[op];
		uint8_t m = 0;

		if (oi->name)
			for (int k = 0; k < 3; k++)
				if ((oi->operand_kinds[k] >= VIR_OPERAND_KIND_T && oi->operand_kinds[k] <= VIR_OPERAND_KIND_BITC) || oi->operand_kinds[k] == VIR_OPERAND_KIND_CONSTV)
					m |= 1u << k;
		if (op == VIR_OP_CALL || op == VIR_OP_RET || op == VIR_OP_BR || op == VIR_OP_ASM || op == VIR_OP_ASMGOTO)
			m |= 8;
		vir_alloc_thread_arrays_instance.vfm[op] = m;
	}
	R(vir_alloc_thread_arrays_instance.nu, nv, 4); R(vir_alloc_thread_arrays_instance.lend, nv, 4); R(vir_alloc_thread_arrays_instance.endp, nv, 4); R(vir_alloc_thread_arrays_instance.hint, nv, 4); R(vir_alloc_thread_arrays_instance.lastpos, nv, 4);
	R(vir_alloc_thread_arrays_instance.slot, nv, 4); R(vir_alloc_thread_arrays_instance.mark, nv, 4); R(vir_alloc_thread_arrays_instance.estamp, nv, 4); R(vir_alloc_thread_arrays_instance.dmark, nv, 4); R(vir_alloc_thread_arrays_instance.wi, nv, 4); R(vir_alloc_thread_arrays_instance.wd, nv, 4);
	R(vir_alloc_thread_arrays_instance.reg, nv, 1); R(vir_alloc_thread_arrays_instance.vfl, nv, 1); R(vir_alloc_thread_arrays_instance.treg, nv, 1); R(vir_alloc_thread_arrays_instance.eloc, nv, 1);
	R(vir_alloc_thread_arrays_instance.sdef, nv, 1); R(vir_alloc_thread_arrays_instance.epst, nv, 1); R(vir_alloc_thread_arrays_instance.sst, nv, 4); R(vir_alloc_thread_arrays_instance.tmk, nv, 4);
	R(vir_alloc_thread_arrays_instance.stk, (size_t)1 << 30, 4); R(vir_alloc_thread_arrays_instance.ostk, (size_t)1 << 30, 4); R(vir_alloc_thread_arrays_instance.nop, nv, 2); R(vir_alloc_thread_arrays_instance.pool, nv, 4);
	R(vir_alloc_thread_arrays_instance.lin_off, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.lin_n, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.soff, VIR_MAX_BLK + 1, 4);
	R(vir_alloc_thread_arrays_instance.succ, VIR_MAX_EXTRA, 4); R(vir_alloc_thread_arrays_instance.bmark, VIR_MAX_BLK, 4);
	R(vir_alloc_thread_arrays_instance.lcall, VIR_MAX_BLK, 1); R(vir_alloc_thread_arrays_instance.inp, VIR_MAX_BLK, 1); R(vir_alloc_thread_arrays_instance.maxp, 2 * (size_t)VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.lendpos, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.lhead, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.tend, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.tendb, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.tbeg, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.tendi, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.tendib, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.apre, VIR_MAX_BLK + 1, 4);
	R(vir_alloc_thread_arrays_instance.ent_off, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.ent_n, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.end_off, VIR_MAX_BLK, 4); R(vir_alloc_thread_arrays_instance.end_n, VIR_MAX_BLK, 4);
	R(vir_alloc_thread_arrays_instance.pv, (size_t)VIR_MAX_BLK * 256, 4); R(vir_alloc_thread_arrays_instance.pr, (size_t)VIR_MAX_BLK * 256, 1); R(vir_alloc_thread_arrays_instance.ps, (size_t)VIR_MAX_BLK * 256, 1);
	R(vir_alloc_thread_arrays_instance.opr, (size_t)1 << 32, 1); R(vir_alloc_thread_arrays_instance.ssz, nv, 1);
	R(vir_alloc_thread_arrays_instance.boff, 2 * (size_t)VIR_MAX_BLK + 2, 4); R(vir_alloc_thread_arrays_instance.eidx, VIR_MAX_EXTRA, 4); R(vir_alloc_thread_arrays_instance.send, nv, 4);
	R(vir_alloc_thread_arrays_instance.edit, (size_t)1 << 30, sizeof(vir_edit)); R(vir_alloc_thread_arrays_instance.edge, VIR_MAX_EXTRA, sizeof(vir_alloc_edge));
	R(vir_alloc_thread_arrays_instance.move, (size_t)1 << 30, sizeof(vir_edge_move));
	R(vir_alloc_thread_arrays_instance.pm, (size_t)VIR_MAX_EXTRA, sizeof(vir_alloc_parallel_move));
	R(vir_alloc_thread_arrays_instance.wpar, nv, 4); R(vir_alloc_thread_arrays_instance.wslot, nv, 4); R(vir_alloc_thread_arrays_instance.wlive, nv, 4); R(vir_alloc_thread_arrays_instance.wlst, nv, 4); R(vir_alloc_thread_arrays_instance.wnext, nv, 4); R(vir_alloc_thread_arrays_instance.whead, nv, 4); R(vir_alloc_thread_arrays_instance.wcnt, nv, 4);
	R(vir_alloc_thread_arrays_instance.wbad, nv, 1); R(vir_alloc_thread_arrays_instance.rhead, nv, 4); R(vir_alloc_thread_arrays_instance.rnext, (size_t)1 << 27, 4); R(vir_alloc_thread_arrays_instance.rlo, (size_t)1 << 27, 4); R(vir_alloc_thread_arrays_instance.rhi, (size_t)1 << 27, 4); R(vir_alloc_thread_arrays_instance.wout, nv, 1); R(vir_alloc_thread_arrays_instance.wreg, nv, 1);
	R(vir_alloc_thread_arrays_instance.lcnt, 256 + nv, 4); R(vir_alloc_thread_arrays_instance.lcs, 256 + nv, 4); R(vir_alloc_thread_arrays_instance.ldm, 256 + nv, 4); R(vir_alloc_thread_arrays_instance.lds, 256 + nv, 4); R(vir_alloc_thread_arrays_instance.rq, VIR_MAX_EXTRA, 4);
	vir_alloc_thread_arrays_instance.init = true;
}
#undef R

typedef struct vir_allocator {
	vir_function *function;
	const vir_loops *loops;
	const vir_target_registers *registers;
	const vir_alloc_options *options;
	vir_alloc_out *out;
	uint64_t pool_maximum;
	uint32_t stack_count, operand_stack_count, pool_count, operand_register_count, edit_count, move_count, edge_count, pair_count, spill_count, successor_count;
	uint32_t current_loop, move_base;
	uint64_t used_callee[2], parallel_avoid[2];
	struct actx *arrays;
	uint64_t register_mask[2];	/* registers regval may name: allocatable, and every fixed one seen */
	bool linear, failed;
	uint32_t save_point;	/* the save point (15.12), VIR_BLOCK_NONE none */
	bool before_save_point;		/* the block being assigned is in front of it */
	uint32_t operands[512];
	uint8_t register_bytes[512];
	uint8_t operand_state[512];	/* step 1: operand j is 0 fixed and not placed yet, 1 placed, 2 not fixed */
	uint32_t operand_next_use[512];
} vir_allocator;

#define ax (*a->arrays)

static inline bool vir_alloc_value_is_constant(const vir_function *f, uint32_t v) { return v < f->constant_count; }
static inline uint32_t vir_alloc_value_class(const vir_function *f, uint32_t v) { return vir_register_class(f->insns[v].type); }
static inline uint32_t vir_alloc_block_last(const vir_function *f, uint32_t b) { return f->blocks[b].first + f->blocks[b].insn_count - 1; }

static uint32_t vir_alloc_value_block(const vir_function *f, uint32_t v)	/* the block holding instruction v */
{
	uint32_t lo = 0, hi = f->block_count;

	while (lo < hi) {
		uint32_t m = lo + (hi - lo) / 2;

		if (f->blocks[m].first + f->blocks[m].insn_count <= v)
			lo = m + 1;
		else
			hi = m;
	}
	return lo < f->block_count && f->blocks[lo].first <= v ? lo : VIR_BLOCK_NONE;
}

/* ---------------- webs ---------------- */

static inline bool vir_alloc_web_has_member(const vir_allocator *a, uint32_t n, uint32_t v) { return ax.wi[v] < n && ax.wd[ax.wi[v]] == v; }

static uint32_t vir_alloc_web_root(const vir_allocator *a, uint32_t v)
{
	uint32_t r = ax.wpar[v];

	if (r == VIR_ALLOC_NO_WEB)
		return VIR_ALLOC_NO_WEB;
	while (ax.wpar[r] != r)
		r = ax.wpar[r] = ax.wpar[ax.wpar[r]];
	return r;
}

/* v's web when v shares it (no other member left in it is live where v is): VIR_ALLOC_NO_WEB otherwise */
static inline uint32_t vir_alloc_web_is_valid(const vir_allocator *a, uint32_t v)
{
	uint32_t r = vir_alloc_web_root(a, v);

	return r != VIR_ALLOC_NO_WEB && !ax.wbad[r] && !ax.wout[v] ? r : VIR_ALLOC_NO_WEB;
}

static void vir_alloc_web_single_set(vir_allocator *a, uint32_t v)	/* while joining: whead/wlst/wnext list a web's parameters */
{
	const vir_function *f = a->function;
	uint32_t d;

	if (ax.wpar[v] != VIR_ALLOC_NO_WEB)
		return;
	d = vir_alloc_value_block(f, v);
	ax.wpar[v] = v;
	ax.wnext[v] = VIR_ALLOC_NO_WEB;
	ax.wcnt[v] = d != VIR_BLOCK_NONE && v - f->blocks[d].first < f->blocks[d].parameter_count;
	ax.whead[v] = ax.wlst[v] = ax.wcnt[v] ? v : VIR_ALLOC_NO_WEB;
}

/* join x's and y's webs unless both hold a parameter of the same block (those are live
 * together at its top: one of them would leave the web anyway) */
static void vir_alloc_web_union(vir_allocator *a, uint32_t x, uint32_t y, uint32_t *gen)
{
	const vir_function *f = a->function;

	vir_alloc_web_single_set(a, x);
	vir_alloc_web_single_set(a, y);
	x = vir_alloc_web_root(a, x);
	y = vir_alloc_web_root(a, y);
	if (x == y)
		return;
	if (ax.wcnt[x] && ax.wcnt[y] && ax.wcnt[x] + ax.wcnt[y] <= 512) {
		++*gen;
		for (uint32_t p = ax.whead[x]; p != VIR_ALLOC_NO_WEB; p = ax.wnext[p])
			ax.wlive[vir_alloc_value_block(f, p)] = *gen;
		for (uint32_t p = ax.whead[y]; p != VIR_ALLOC_NO_WEB; p = ax.wnext[p])
			if (ax.wlive[vir_alloc_value_block(f, p)] == *gen)
				return;
	}
	if (x > y) {
		uint32_t t = x;

		x = y;
		y = t;
	}
	ax.wpar[y] = x;
	if (ax.whead[y] != VIR_ALLOC_NO_WEB) {
		if (ax.whead[x] == VIR_ALLOC_NO_WEB)
			ax.whead[x] = ax.whead[y];
		else
			ax.wnext[ax.wlst[x]] = ax.whead[y];
		ax.wlst[x] = ax.wlst[y];
	}
	ax.wcnt[x] += ax.wcnt[y];
}

/* the webs of f: each `br` argument joins its parameter (constants do not) */
static void webs(vir_allocator *a)
{
	vir_function *f = a->function;
	uint32_t gen = 0;

	memset(ax.wpar, 0xff, (size_t)f->insn_count * 4);
	memset(ax.wout, 0, f->insn_count);
	memset(ax.wlive, 0, (size_t)f->block_count * 4);
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_insn *t = &f->insns[vir_alloc_block_last(f, b)];

		if (t->op != VIR_OP_BR)
			continue;
		for (uint32_t j = 0; j < f->blocks[t->a].parameter_count; j++) {
			uint32_t x = f->extra[t->c + j];

			if (!vir_alloc_value_is_constant(f, x))
				vir_alloc_web_union(a, x, f->blocks[t->a].first + j, &gen);
		}
	}
	for (uint32_t v = 0; v < f->insn_count; v++)
		if (ax.wpar[v] == v) {
			ax.wbad[v] = 0;
			ax.wslot[v] = VIR_BLOCK_NONE;
			ax.wreg[v] = VIR_REGISTER_NONE;
			ax.wlst[v] = 0;
			ax.whead[v] = VIR_ALLOC_NO_WEB;
			ax.wcnt[v] = 0;
		}
	for (uint32_t v = f->insn_count; v-- > 0;) {	/* the members of each web, in order */
		uint32_t r = vir_alloc_web_root(a, v);

		if (r == VIR_ALLOC_NO_WEB)
			continue;
		ax.wnext[v] = ax.whead[r];
		ax.whead[r] = v;
		if (++ax.wcnt[r] > VIR_ALLOC_WEB_MAX)
			ax.wbad[r] = 1;
	}
	ax.wgen = 0;
}

/* v is live round a loop that contains instruction position pos */
static bool vir_alloc_value_is_live_in_loop(const vir_allocator *a, uint32_t v, uint32_t pos)
{
	for (uint32_t k = ax.rhead[v]; k != VIR_ALLOC_NO_WEB; k = ax.rnext[k])
		if (ax.rlo[k] <= pos && pos <= ax.rhi[k])
			return true;
	return false;
}

/* after sweep 5: a member z live round loops (its ranges, where livein and dmark take
 * it as live) interferes with a member defined there: z leaves, or, when z is a block
 * parameter (the web's meeting point), the members defined there leave */
static void vir_alloc_webs_loops_trim(vir_allocator *a)
{
	vir_function *f = a->function;

	for (uint32_t z = 0; z < f->insn_count; z++) {
		uint32_t r = vir_alloc_web_root(a, z);

		if (r == VIR_ALLOC_NO_WEB || ax.wbad[r] || ax.wout[z] || !ax.lend[z])
			continue;
		for (uint32_t m = ax.whead[r]; m != VIR_ALLOC_NO_WEB; m = ax.wnext[m]) {
			if (m == z || ax.wout[m] || !(a->linear ? m > z && m <= ax.lend[z] : vir_alloc_value_is_live_in_loop(a, z, m)))
				continue;
			if (f->insns[z].op != VIR_OP_BPARAM) {
				ax.wout[z] = 1;
				break;
			}
			ax.wout[m] = 1;
		}
	}
	/* a web lives across calls (in a loop) when a member does: every member prefers a
	 * callee-saved register then, so the web keeps one register */
	for (uint32_t v = 0; v < f->insn_count; v++)
		if (ax.wpar[v] == v)
			ax.wlive[v] = 0;
	for (uint32_t v = 0; v < f->insn_count; v++) {
		uint32_t r = vir_alloc_web_is_valid(a, v);

		if (r != VIR_ALLOC_NO_WEB)
			ax.wlive[r] |= ax.vfl[v] & (VIR_ALLOC_FLAG_CROSSES_CALL | VIR_ALLOC_FLAG_CROSSES_LOOP);
	}
	for (uint32_t v = 0; v < f->insn_count; v++) {
		uint32_t r = vir_alloc_web_is_valid(a, v);

		if (r != VIR_ALLOC_NO_WEB && !(ax.vfl[v] & VIR_ALLOC_FLAG_DEAD))
			ax.vfl[v] |= (uint8_t)ax.wlive[r];
	}
}

/* sweep 5: member v enters the live set; or leaves it at its definition, where another
 * live member means the two interfere */
static inline void vir_alloc_web_member_enter(vir_allocator *a, uint32_t v)
{
	uint32_t r = vir_alloc_web_root(a, v);

	if (r == VIR_ALLOC_NO_WEB || ax.wout[v])
		return;
	if (ax.wlst[r] != ax.wgen) {
		ax.wlst[r] = ax.wgen;
		ax.wlive[r] = 0;
	}
	ax.wlive[r]++;
}

static inline void vir_alloc_web_member_leave(vir_allocator *a, uint32_t v, uint32_t wn)
{
	uint32_t r = vir_alloc_web_root(a, v);

	if (r == VIR_ALLOC_NO_WEB || ax.wout[v] || ax.wlst[r] != ax.wgen)
		return;
	if (!--ax.wlive[r])
		return;
	/* defined where another member is live: a block parameter (the web's meeting point)
	 * stays and the live members leave; any other definition leaves */
	if (a->function->insns[v].op != VIR_OP_BPARAM) {
		ax.wout[v] = 1;
		return;
	}
	for (uint32_t j = 0; j < wn; j++) {
		uint32_t u = ax.wd[j];

		if (!ax.wout[u] && vir_alloc_web_root(a, u) == r) {
			ax.wout[u] = 1;
			ax.wlive[r]--;
		}
	}
}

static inline void vir_alloc_fix_get(const vir_allocator *a, uint32_t i, vir_register_constraint *x)
{
	vir_fix_clear(x);
	if (!a->registers->operand_fixes || a->registers->operand_fixes[a->function->insns[i].op])
		a->registers->fixed(a->function, i, x, a->registers->context);
}

static bool vir_alloc_failure_set(vir_allocator *a, const char *fmt, uint32_t x)
{
	if (!a->failed)
		snprintf(a->out->error_message, sizeof(a->out->error_message), fmt, x);
	a->failed = true;
	return false;
}

/* the value operands of instruction i in vir_operand_iterator order, without `multi` ones; n */
static uint32_t vir_alloc_insn_operands(vir_allocator *a, uint32_t i)
{
	vir_insn in = a->function->insns[i];
	vir_operand_iterator it;
	uint32_t *p, n = 0;

	if (in.op < VIR_OP_TARGET_FIRST && !(ax.vfm[in.op] & 8)) {	/* the fields alone: no iterator */
		uint32_t m = ax.vfm[in.op], w[3] = { in.a, in.b, in.c };

		for (int k = 0; k < 3; k++)
			if ((m >> k & 1) && a->function->insns[w[k]].type != VIR_TYPE_MULTI)
				a->operands[n++] = w[k];
		return n;
	}

	vir_operand_iterator_init(&it, a->function, &in, false);
	while ((p = vir_operand_iterator_next(&it))) {
		if (a->function->insns[*p].type == VIR_TYPE_MULTI)
			continue;
		if (n >= 512)
			return vir_alloc_failure_set(a, "more than 512 operands at instruction %u", i), 0;
		a->operands[n++] = *p;
	}
	return n;
}

/* ---------------- sweep 5 ---------------- */

/* v's definition dominates block b, or either is unreachable: only then can v be live at b's
 * end (strict SSA: a path from the entry to b and on to a use would miss the definition) */
static bool vir_alloc_definition_dominates(vir_allocator *a, uint32_t v, uint32_t b)
{
	const vir_function *f = a->function;
	uint32_t d = vir_alloc_value_block(f, v);

	return d == VIR_BLOCK_NONE || f->blocks[d].immediate_dominator == VIR_BLOCK_NONE || f->blocks[b].immediate_dominator == VIR_BLOCK_NONE || vir_block_dominates(f, d, b);
}

/* v (whose live range ends at e) lives across a call: after one, by the live sets; a value
 * live round a loop (not in the live sets past its back edge) or any value in linear mode
 * by the order of instructions (a call before e) */
static uint8_t xcall(const vir_allocator *a, uint32_t v, uint32_t lastcall, bool callloop, uint32_t e)
{
	if (!a->linear && ax.rhead[v] == VIR_ALLOC_NO_WEB)
		return (ax.vfl[v] & VIR_ALLOC_FLAG_LIVE_AFTER_CALL ? VIR_ALLOC_FLAG_CROSSES_CALL : 0) | (ax.vfl[v] & VIR_ALLOC_FLAG_LIVE_AFTER_LOOP_CALL ? VIR_ALLOC_FLAG_CROSSES_LOOP : 0);
	return lastcall && lastcall < e ? VIR_ALLOC_FLAG_CROSSES_CALL | (callloop ? VIR_ALLOC_FLAG_CROSSES_LOOP : 0) : 0;
}

static bool vir_alloc_liveness_sweep(vir_allocator *a)
{
	vir_function *f = a->function;
	const vir_loops *lp = a->loops;
	uint32_t nb = f->block_count, wn = 0, cnt[2] = { 0, 0 }, lastcall = 0;
	bool callloop = false;

	memset(ax.nu, 0, (size_t)f->insn_count * 4);
	memset(ax.lend, 0, (size_t)f->insn_count * 4);
	memset(ax.rhead, 0xff, (size_t)f->insn_count * 4);
	ax.nrng = 0;
	memset(ax.endp, 0, (size_t)f->insn_count * 4);
	memset(ax.hint, 0, (size_t)f->insn_count * 4);
	memset(ax.vfl, 0, f->insn_count);
	memset(ax.maxp, 0, (size_t)lp->count * 8);
	memset(ax.lcall, 0, lp->count);
	a->stack_count = a->operand_stack_count = 0;
	a->pool_count = 0;
	ax.wgen++;
	for (uint32_t b = nb; b-- > 0;) {
		const vir_block *k = &f->blocks[b];
		uint32_t li = lp->inner[b];
		bool cold = k->flags & VIR_BLOCK_FLAG_COLD;

		if (!a->linear) {
			wn = cnt[0] = cnt[1] = 0;
			ax.wgen++;
			for (uint32_t s = ax.soff[b]; s < ax.soff[b + 1]; s++) {
				uint32_t t = ax.succ[s];

				if (t <= b)
					continue;
				for (uint32_t j = 0; j < ax.lin_n[t]; j++) {
					uint32_t v = ax.pool[ax.lin_off[t] + j];

					if (!vir_alloc_web_has_member(a, wn, v)) {
						ax.wi[v] = wn;
						ax.wd[wn++] = v;
						cnt[vir_alloc_value_class(f, v)]++;
						vir_alloc_web_member_enter(a, v);
					}
				}
				/* an edge into a loop: the values live around the loop are live at t without
				 * being in its W when they are used only past a back edge (into the middle of
				 * a loop, or at its head when the loop is irreducible); they are the W of its
				 * back-edge targets, defined before t (and dominating b: live at b at all) */
				uint32_t lo = VIR_BLOCK_NONE;

				for (uint32_t l = lp->inner[t]; l != VIR_BLOCK_NONE && lp->loops[l].head > b; l = lp->loops[l].parent)
					lo = l;
				if (lo != VIR_BLOCK_NONE)
					for (uint32_t x = lp->loops[lo].head; x <= lp->loops[lo].end; x++) {
						if (!ax.tend[x])
							continue;
						for (uint32_t j = 0; j < ax.lin_n[x]; j++) {
							uint32_t v = ax.pool[ax.lin_off[x] + j];

							if (v < f->blocks[t].first && !vir_alloc_web_has_member(a, wn, v) && vir_alloc_definition_dominates(a, v, b)) {
								ax.wi[v] = wn;
								ax.wd[wn++] = v;
								cnt[vir_alloc_value_class(f, v)]++;
								vir_alloc_web_member_enter(a, v);
							}
						}
					}
			}
		}
		for (uint32_t i = k->first + k->insn_count; i-- > k->first;) {
			const vir_insn *in = &f->insns[i];
			vir_register_constraint fx;
			uint32_t n;

			if (in->op == VIR_OP_BPARAM)
				break;
			vir_alloc_fix_get(a, i, &fx);
			if (in->type != VIR_TYPE_VOID && in->type != VIR_TYPE_MULTI) {	/* the definition */
				uint32_t e = ax.endp[i] > ax.lend[i] ? ax.endp[i] : ax.lend[i];

				if (vir_alloc_web_has_member(a, wn, i)) {
					uint32_t x = ax.wd[--wn];

					ax.wd[ax.wi[i]] = x;
					ax.wi[x] = ax.wi[i];
					cnt[vir_alloc_value_class(f, i)]--;
					vir_alloc_web_member_leave(a, i, wn);
				}
				ax.endp[i] = e;
				ax.vfl[i] |= xcall(a, i, lastcall, callloop, e);
				if (!ax.nu[i] && !ax.lend[i])
					ax.vfl[i] |= VIR_ALLOC_FLAG_DEAD;
				if ((fx.flags & VIR_CONSTRAINT_FLAG_TIED) && ax.hint[i] && in->a && !vir_alloc_value_is_constant(f, in->a) && !ax.hint[in->a])
					ax.hint[in->a] = ax.hint[i];
				if (fx.result != VIR_REGISTER_NONE && !ax.hint[i])
					ax.hint[i] = VIR_ALLOC_HINT_REGISTER | fx.result;
			}
			if (in->op == VIR_OP_CALL && !cold) {
				lastcall = i;
				callloop = li != VIR_BLOCK_NONE;
				if (callloop)
					ax.lcall[li] = 1;
				if (!a->linear)	/* the values live after it (W, before its operands join) */
					for (uint32_t j = 0; j < wn; j++)
						ax.vfl[ax.wd[j]] |= VIR_ALLOC_FLAG_LIVE_AFTER_CALL | (callloop ? VIR_ALLOC_FLAG_LIVE_AFTER_LOOP_CALL : 0);
			}
			n = vir_alloc_insn_operands(a, i);
			if (a->failed)
				return false;
			/* sweep 6 takes them from here, in the opposite order of instructions */
			memcpy(&ax.ostk[a->operand_stack_count], a->operands, (size_t)n * 4);
			a->operand_stack_count += n;
			ax.nop[i] = (uint16_t)n;
			for (uint32_t j = n; j-- > 0;) {
				uint32_t v = a->operands[j];

				if (vir_alloc_value_is_constant(f, v)) {
					ax.stk[a->stack_count++] = ax.nu[v];
					ax.nu[v] = i;
					continue;
				}
				bool live = vir_alloc_web_has_member(a, wn, v);

				ax.stk[a->stack_count++] = ax.nu[v] | (live ? 0 : VIR_ALLOC_KILL);
				if (!ax.nu[v])
					ax.endp[v] = i;
				ax.nu[v] = i;
				if (!live) {
					ax.wi[v] = wn;
					ax.wd[wn++] = v;
					cnt[vir_alloc_value_class(f, v)]++;
					vir_alloc_web_member_enter(a, v);
				}
				if (j < fx.operand_count && fx.operands[j] != VIR_REGISTER_NONE && (in->op != VIR_OP_CALL || !live))
					ax.hint[v] = VIR_ALLOC_HINT_REGISTER | fx.operands[j];
				else if (in->op == VIR_OP_BR && j < f->blocks[in->a].parameter_count)
					ax.hint[v] = f->blocks[in->a].first + j;
			}
			if (li != VIR_BLOCK_NONE) {
				uint32_t *m = &ax.maxp[2 * li];

				if (cnt[0] > m[0]) m[0] = cnt[0];
				if (cnt[1] > m[1]) m[1] = cnt[1];
			}
		}
		for (uint32_t j = k->parameter_count; j-- > 0;) {	/* block parameters: definitions at the top */
			uint32_t q = k->first + j, e = ax.endp[q] > ax.lend[q] ? ax.endp[q] : ax.lend[q];

			if (vir_alloc_web_has_member(a, wn, q)) {
				uint32_t x = ax.wd[--wn];

				ax.wd[ax.wi[q]] = x;
				ax.wi[x] = ax.wi[q];
				cnt[vir_alloc_value_class(f, q)]--;
				vir_alloc_web_member_leave(a, q, wn);
			}
			ax.endp[q] = e;
			ax.vfl[q] |= xcall(a, q, lastcall, callloop, e);
			if (!ax.nu[q] && !ax.lend[q])
				ax.vfl[q] |= VIR_ALLOC_FLAG_DEAD;
			else if (lastcall && lastcall <= vir_alloc_block_last(f, b) && (!ax.nu[q] || ax.nu[q] > lastcall))
				ax.vfl[q] |= VIR_ALLOC_FLAG_CALL_FIRST;
		}
		/* a block entered with every value in its slot: an `ibr` target, or one with no earlier predecessor */
		if (b && (k->flags & VIR_BLOCK_FLAG_ADDR || ax.bmark[b])) {
			for (uint32_t j = 0; j < wn; j++)
				ax.vfl[ax.wd[j]] |= VIR_ALLOC_FLAG_NEEDS_SLOT;
		}
		if (!a->linear) {
			if (a->pool_count + (uint64_t)wn > a->pool_maximum)
				return false;	/* the caller restarts in linear mode */
			ax.lin_off[b] = a->pool_count;
			ax.lin_n[b] = wn;
			memcpy(&ax.pool[a->pool_count], ax.wd, (size_t)wn * 4);
			a->pool_count += wn;
		}
		if (ax.tend[b]) {	/* the target of a back edge (a loop header, or an entry of an irreducible one) */
			uint32_t h = ax.tbeg[b];
			/* a block of the loop that is entered with everything in slots (before b too: the
			 * values are live around the whole loop) holds them without having them in its W */
			uint8_t ns = ax.apre[ax.tendb[b] + 1] > ax.apre[ax.tbeg[b]] ? VIR_ALLOC_FLAG_NEEDS_SLOT : 0;
			uint8_t nsi = ax.apre[ax.tendib[b] + 1] > ax.apre[ax.tbeg[b]] ? VIR_ALLOC_FLAG_NEEDS_SLOT : 0;

			for (uint32_t j = 0; j < wn; j++) {
				uint32_t v = ax.wd[j];
				/* one whose definition does not dominate the loop's head (one defined in the
				 * loop): a path from the head to b passes the definition, so it is live only to
				 * the end of the back edges that reach b without passing the head */
				uint32_t d = vir_alloc_value_block(f, v);
				bool in = d != VIR_BLOCK_NONE && f->blocks[h].immediate_dominator != VIR_BLOCK_NONE && f->blocks[d].immediate_dominator != VIR_BLOCK_NONE &&
					  (d == h || !vir_block_dominates(f, d, h));
				uint32_t e = in ? ax.tendi[b] : ax.tend[b];

				if (ax.lend[v] < e)
					ax.lend[v] = e;
				ax.vfl[v] |= in ? nsi : ns;
				if (ax.nrng < (1u << 27)) {
					ax.rlo[ax.nrng] = f->blocks[ax.tbeg[b]].first;
					ax.rhi[ax.nrng] = e;
					ax.rnext[ax.nrng] = ax.rhead[v];
					ax.rhead[v] = ax.nrng++;
				} else {
					a->failed = true;
					snprintf(a->out->error_message, sizeof(a->out->error_message), "too many loop ranges");
					return false;
				}
			}
		}
	}
	/* loop pressure includes the loops inside */
	for (uint32_t l = lp->count; l-- > 0;) {
		uint32_t p = lp->loops[l].parent;

		if (p != VIR_BLOCK_NONE) {
			for (int c = 0; c < 2; c++)
				if (ax.maxp[2 * l + c] > ax.maxp[2 * p + c])
					ax.maxp[2 * p + c] = ax.maxp[2 * l + c];
			ax.lcall[p] |= ax.lcall[l];
		}
	}
	return true;
}

/* ---------------- sweep 6: registers ---------------- */

static inline bool vir_alloc_value_is_in_register(const vir_allocator *a, uint32_t v) { return ax.reg[v] != VIR_REGISTER_NONE && ax.regval[ax.reg[v]] == v; }

static inline void vir_alloc_register_set(vir_allocator *a, uint32_t v, uint32_t r)
{
	uint32_t w = vir_alloc_web_is_valid(a, v);

	ax.regval[r] = v;
	ax.reg[v] = (uint8_t)r;
	ax.fr[VIR_REGISTER_CLASS(r)] &= ~(1ull << VIR_REGISTER_NUMBER(r));
	if (w != VIR_ALLOC_NO_WEB && ax.wreg[w] == VIR_REGISTER_NONE && a->function->insns[v].op == VIR_OP_BPARAM)
		ax.wreg[w] = (uint8_t)r;	/* the web's register: its first parameter's */
	if (a->registers->callee[VIR_REGISTER_CLASS(r)] >> VIR_REGISTER_NUMBER(r) & 1)
		a->used_callee[VIR_REGISTER_CLASS(r)] |= 1ull << VIR_REGISTER_NUMBER(r);
}

/* a new stamp for ax.estamp (never 0; the arrays are cleared on wrap-around) */
static uint32_t vir_alloc_stamp_next(vir_allocator *a)
{
	if (!++ax.sgen) {
		memset(ax.estamp, 0, (size_t)a->function->insn_count * 4);
		ax.sgen = 1;
	}
	return ax.sgen;
}

static inline void vir_alloc_register_clear(vir_allocator *a, uint32_t r)
{
	ax.regval[r] = 0;
	ax.fr[VIR_REGISTER_CLASS(r)] |= 1ull << VIR_REGISTER_NUMBER(r);
}

static void vir_alloc_edit_add(vir_allocator *a, uint32_t pos, uint32_t kind, uint32_t dst, uint32_t src, uint32_t v)
{
	vir_edit *e = &ax.edit[a->edit_count++];

	e->pos = pos;
	e->kind = (uint8_t)kind;
	e->dst = (uint8_t)dst;
	e->src = (uint8_t)src;
	e->pad = 0;
	e->value = v;
	if (kind != VIR_EDIT_SWAP && kind != VIR_EDIT_STORE && dst < 128 && (a->registers->callee[VIR_REGISTER_CLASS(dst)] >> VIR_REGISTER_NUMBER(dst) & 1))
		a->used_callee[VIR_REGISTER_CLASS(dst)] |= 1ull << VIR_REGISTER_NUMBER(dst);	/* a register written only by an edit */
	switch (kind) {
	case VIR_EDIT_RELOAD: a->out->statistics.reloads++; break;
	case VIR_EDIT_REMATERIALIZE: a->out->statistics.remats++; break;
	case VIR_EDIT_MOVE: a->out->statistics.moves++; break;
	case VIR_EDIT_SWAP: a->out->statistics.swaps++; break;
	case VIR_EDIT_STORE: a->out->statistics.stores++; break;
	}
}

static uint32_t vir_alloc_definition_position(const vir_function *f, uint32_t v) { return v; }

/* a spill slot for v: shared with an earlier occupant whose range ended before v's definition */
static void vir_alloc_spill_slot_assign(vir_allocator *a, uint32_t v)
{
	vir_function *f = a->function;
	uint32_t c, s = VIR_BLOCK_NONE;

	if (ax.slot[v] != VIR_BLOCK_NONE || vir_alloc_value_is_constant(f, v))
		return;
	c = vir_type_size(f->insns[v].type) == 4 ? 0 : vir_type_size(f->insns[v].type) == 16 ? 2 : 1;
	{
		uint32_t w = vir_alloc_web_is_valid(a, v);

		if (w != VIR_ALLOC_NO_WEB) {	/* one slot for the web, its own (outside the ring) */
			if (ax.wslot[w] == VIR_BLOCK_NONE) {
				ax.wslot[w] = a->spill_count++;
				ax.ssz[ax.wslot[w]] = (uint8_t)(4u << c);
			}
			ax.slot[v] = ax.wslot[w];
			a->out->statistics.spills++;
			return;
		}
	}
	for (uint32_t j = 0; j < ax.nring[c]; j++) {
		uint32_t t = ax.ring[c][j];

		if (ax.send[t] < vir_alloc_definition_position(f, v)) {
			s = t;
			break;
		}
	}
	if (s == VIR_BLOCK_NONE) {
		s = a->spill_count++;
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
	if (vir_alloc_definition_position(f, v) > ax.send[s])
		ax.send[s] = vir_alloc_definition_position(f, v);
	ax.slot[v] = s;
	a->out->statistics.spills++;
}

static inline bool vir_alloc_value_is_stored(vir_allocator *a, uint32_t v) { return ax.sst[v] == ax.bgen; }
static inline void vir_alloc_value_stored_mark(vir_allocator *a, uint32_t v) { ax.sst[v] = ax.bgen; }

/* v leaves register r before instruction pos: into its slot, stored there now unless it is
 * already (VIR_ALLOC_NO_POSITION: at a block's entry, where the edges into it store it) */
static void evict(vir_allocator *a, uint32_t r, uint32_t pos)
{
	uint32_t v = ax.regval[r];

	if (v && ax.reg[v] == r && !vir_alloc_value_is_constant(a->function, v)) {
		vir_alloc_spill_slot_assign(a, v);
		if (pos != VIR_ALLOC_NO_POSITION && !vir_alloc_value_is_stored(a, v)) {
			vir_alloc_edit_add(a, pos, VIR_EDIT_STORE, VIR_REGISTER_NONE, r, v);
			vir_alloc_value_stored_mark(a, v);
		}
	}
	vir_alloc_register_clear(a, r);
}

/* the distance to v's next use from position p (15.6) */
static uint64_t vir_alloc_next_use_distance(vir_allocator *a, uint32_t v, uint32_t p)
{
	const vir_loops *lp = a->loops;
	uint32_t n = ax.nu[v], l = a->current_loop;
	uint64_t d;

	if (l != VIR_BLOCK_NONE) {
		uint32_t le = ax.lendpos[l];

		if ((n == 0 || n > le) && ax.lastpos[v] != VIR_ALLOC_NO_POSITION && ax.lastpos[v] >= ax.lhead[l] && ax.lend[v] >= le)
			return le - p;
	}
	if (!n)
		return VIR_ALLOC_FAR;
	d = n > p ? n - p : 0;
	for (; l != VIR_BLOCK_NONE && ax.lendpos[l] < n; l = lp->loops[l].parent)
		d += 100000;
	return d;
}

static int vir_alloc_mask_lowest_bit(uint64_t m) { return m ? __builtin_ctzll(m) : -1; }

/* hint r is caller-saved, v lives across a call in a loop, and a callee-saved register is free */
static inline bool vir_alloc_hint_prefers_callee_saved(const vir_allocator *a, uint32_t v, uint32_t c, uint64_t m, uint32_t r)
{
	uint64_t cs = a->registers->callee[c];

	return (ax.vfl[v] & (VIR_ALLOC_FLAG_CROSSES_CALL | VIR_ALLOC_FLAG_CROSSES_LOOP)) == (VIR_ALLOC_FLAG_CROSSES_CALL | VIR_ALLOC_FLAG_CROSSES_LOOP) && !(cs >> VIR_REGISTER_NUMBER(r) & 1) && (m & cs);
}

/* a free register of class c not in avoid, by v's hint and call preference; -1 if none */
static int vir_alloc_register_choose(vir_allocator *a, uint32_t v, uint32_t c, uint64_t avoid)
{
	uint64_t m = ax.fr[c] & a->registers->alloc[c] & ~avoid & ~a->parallel_avoid[c], cs = a->registers->callee[c];
	uint32_t h = ax.hint[v];

	if (!m)
		return -1;
	if (a->before_save_point && (m & ~cs))
		m &= ~cs;	/* in front of the save point: nothing that the frame has to save */
	if (h) {
		uint32_t r = VIR_REGISTER_NONE;

		if (h & VIR_ALLOC_HINT_REGISTER)
			r = h & 0xff;
		else if (h < a->function->insn_count)
			r = vir_alloc_value_is_in_register(a, h) ? ax.reg[h] : (a->function->insns[h].op == VIR_OP_BPARAM ? ax.treg[h] : VIR_REGISTER_NONE);
		if (r < 128 && VIR_REGISTER_CLASS(r) == c && (m >> VIR_REGISTER_NUMBER(r) & 1) && !vir_alloc_hint_prefers_callee_saved(a, v, c, m, r))
			return (int)r;
	}
	{
		uint32_t w = vir_alloc_web_is_valid(a, v), r;

		if (w != VIR_ALLOC_NO_WEB && (r = ax.wreg[w]) != VIR_REGISTER_NONE && VIR_REGISTER_CLASS(r) == c && (m >> VIR_REGISTER_NUMBER(r) & 1) &&
		    !vir_alloc_hint_prefers_callee_saved(a, v, c, m, r))
			return (int)r;
	}
	/* used next by an instruction with fixed operands (a call's arguments): not in the register
	 * another of its operands goes to, when there is a choice */
	if (ax.nu[v] && ax.nu[v] < a->function->insn_count) {
		vir_register_constraint x;
		uint64_t o = 0;

		vir_alloc_fix_get(a, ax.nu[v], &x);
		for (uint32_t j = 0; j < x.operand_count; j++)
			if (x.operands[j] != VIR_REGISTER_NONE && VIR_REGISTER_CLASS(x.operands[j]) == c)
				o |= 1ull << VIR_REGISTER_NUMBER(x.operands[j]);
		if (m & ~o)
			m &= ~o;
	}
	if ((ax.vfl[v] & VIR_ALLOC_FLAG_CROSSES_CALL) && (m & cs)) {	/* (a caller-saved one is moved to a callee-saved one at the call) */
		uint64_t u = m & cs & a->used_callee[c];

		return VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(u ? u : m & cs));
	}
	if (m & ~cs)
		return VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m & ~cs));
	return VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m));
}

/* the register to evict in class c (not in avoid), or -1 */
static int vir_alloc_register_victim_find(vir_allocator *a, uint32_t c, uint64_t avoid, uint32_t p)
{
	uint64_t m = a->registers->alloc[c] & ~avoid & ~ax.fr[c];
	int best = -1;
	uint64_t bd = 0;

	while (m) {
		uint32_t r = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m)), v = ax.regval[r];
		uint64_t d;

		m &= m - 1;
		d = vir_alloc_next_use_distance(a, v, p) * 2 + (vir_alloc_value_is_constant(a->function, v) ? 1 : 0);	/* at equal distance a constant first */
		if (best < 0 || d > bd) {
			best = (int)r;
			bd = d;
		}
	}
	return best;
}

/* a register for v of class c at p, evicting if needed; -1 if every register is in avoid */
static int vir_alloc_register_get(vir_allocator *a, uint32_t v, uint32_t c, uint64_t avoid, uint32_t p)
{
	int r = vir_alloc_register_choose(a, v, c, avoid);

	if (r >= 0)
		return r;
	r = vir_alloc_register_victim_find(a, c, avoid, p);
	if (r < 0)
		return -1;
	evict(a, (uint32_t)r, p);
	return r;
}

/* bring v (not in a register) into r: rematerialize or reload */
static bool vir_alloc_value_fetch(vir_allocator *a, uint32_t v, uint32_t r, uint32_t p)
{
	if (vir_alloc_value_is_constant(a->function, v)) {
		vir_alloc_edit_add(a, p, VIR_EDIT_REMATERIALIZE, r, VIR_REGISTER_NONE, v);
	} else {
		if (ax.slot[v] == VIR_BLOCK_NONE)
			return vir_alloc_failure_set(a, "internal: value %u is in no register and has no slot", v);
		vir_alloc_edit_add(a, p, VIR_EDIT_RELOAD, r, VIR_REGISTER_NONE, v);
		vir_alloc_value_stored_mark(a, v);	/* (it was out of its register: in its slot) */
	}
	return true;
}

/* move w out of register r (it stays readable there until the instruction): to a free register
 * outside avoid, else evict it */
static void vir_alloc_register_displace(vir_allocator *a, uint32_t r, uint64_t *avoid, uint32_t p)
{
	uint32_t w = ax.regval[r], c = VIR_REGISTER_CLASS(r);

	if (!w)
		return;
	if (ax.reg[w] != r || vir_alloc_value_is_constant(a->function, w)) {	/* a copy, or a constant: drop */
		vir_alloc_register_clear(a, r);
		return;
	}
	int r2 = vir_alloc_register_choose(a, w, c, avoid[c]);

	if (r2 >= 0) {
		vir_alloc_edit_add(a, p, VIR_EDIT_MOVE, (uint32_t)r2, r, w);
		vir_alloc_register_clear(a, r);
		vir_alloc_register_set(a, w, (uint32_t)r2);
	} else {
		evict(a, r, p);
	}
}

/* Applied after operand placement, including terminators whose outgoing
 * state must describe the registers after the machine instruction. */
static void vir_alloc_clobbers_apply(vir_allocator *a, const vir_register_constraint *fx, const uint64_t *lock, const uint64_t *fixm, uint32_t pos)
{
	uint64_t avoid[2] = { lock[0] | fixm[0] | fx->clobbered[0] | fx->edge_clobbered[0],
	                      lock[1] | fixm[1] | fx->clobbered[1] | fx->edge_clobbered[1] };

	for (int c = 0; c < 2; c++) {
		uint64_t m = (fx->clobbered[c] | fx->edge_clobbered[c]) & a->registers->alloc[c];

		while (m) {
			uint32_t r = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m));

			m &= m - 1;
			if (ax.regval[r])
				vir_alloc_register_displace(a, r, avoid, pos);
		}
	}
}

/* ---------------- sweep 6: edges ---------------- */

static bool livein(vir_allocator *a, uint32_t v, uint32_t b);

static inline uint32_t vir_alloc_location_index(uint32_t l) { return l & VIR_LOCATION_SLOT ? 256 + (l & ~VIR_LOCATION_SLOT) : l; }

static void vir_alloc_move_add(vir_allocator *a, uint32_t kind, uint32_t dst, uint32_t src, uint32_t v)
{
	vir_edge_move *m = &ax.move[a->move_count++];
	vir_alloc_statistics *s = &a->out->statistics;

	m->dst = dst;
	m->src = src;
	m->value = v;
	m->kind = (uint8_t)kind;
	m->pad[0] = m->pad[1] = m->pad[2] = 0;
	if (kind == VIR_EDIT_EXCHANGE)
		s->exchanges++;
	else if (kind == VIR_EDIT_REMATERIALIZE)
		s->edge_rematerializations++;
	else if (dst & VIR_LOCATION_SLOT)
		s->edge_stores++;
	else if (src & VIR_LOCATION_SLOT)
		s->edge_reloads++;
	else
		s->edge_moves++;
	if (!(dst & VIR_LOCATION_SLOT) && dst < 128 && (a->registers->callee[VIR_REGISTER_CLASS(dst)] >> VIR_REGISTER_NUMBER(dst) & 1)) {
		a->used_callee[VIR_REGISTER_CLASS(dst)] |= 1ull << VIR_REGISTER_NUMBER(dst);
	}
}

/* sequence the n parallel moves in ax.pm (unique destinations) into a->move; nmask: the
 * registers of the target's entry state (not free as temporaries) */
static void vir_alloc_moves_sequence(vir_allocator *a, uint32_t n, const uint64_t *nmask)
{
	vir_alloc_parallel_move *pm = ax.pm;
	uint32_t g = ++ax.lgen, done = 0;

	if (!g) {
		memset(ax.lcs, 0, (256 + (size_t)a->spill_count) * 4);
		memset(ax.lds, 0, (256 + (size_t)a->spill_count) * 4);
		g = ax.lgen = 1;
	}
	for (uint32_t i = 0; i < n; i++) {
		uint32_t d = vir_alloc_location_index(pm[i].dst);

		ax.lds[d] = g;
		ax.ldm[d] = i;
		if (pm[i].kind != VIR_EDIT_REMATERIALIZE) {
			uint32_t s = vir_alloc_location_index(pm[i].src);

			if (ax.lcs[s] != g) {
				ax.lcs[s] = g;
				ax.lcnt[s] = 0;
			}
			ax.lcnt[s]++;
		}
	}
#define VIR_ALLOC_LIVE_COUNT(l) (ax.lcs[l] == g ? ax.lcnt[l] : 0)
	/* a worklist of moves whose destination nobody still reads; a move done may free the
	 * move that writes its source */
	uint32_t *rq = ax.rq, nr = 0, h = 0;	/* a queue: moves keep their order where they can */

	for (uint32_t i = 0; i < n; i++)
		if (!VIR_ALLOC_LIVE_COUNT(vir_alloc_location_index(pm[i].dst)))
			rq[nr++] = i;
	while (done < n) {
		while (h < nr) {
			uint32_t i = rq[h++], d, kind = pm[i].kind;

			if (kind == 0xff || VIR_ALLOC_LIVE_COUNT(d = vir_alloc_location_index(pm[i].dst)))
				continue;
			vir_alloc_move_add(a, kind, pm[i].dst, pm[i].src, pm[i].value);
			ax.lds[d] = 0;
			pm[i].kind = 0xff;
			done++;
			if (kind != VIR_EDIT_REMATERIALIZE) {
				uint32_t s = vir_alloc_location_index(pm[i].src);

				if (!--ax.lcnt[s] && ax.lds[s] == g)
					rq[nr++] = ax.ldm[s];
			}
		}
		if (done == n)
			break;
		h = nr = 0;
		/* a cycle: every pending destination is read by another pending move */
		for (uint32_t i = 0; i < n; i++) {
			uint32_t d, c, t = VIR_REGISTER_NONE;

			if (pm[i].kind == 0xff)
				continue;
			d = vir_alloc_location_index(pm[i].dst);
			c = vir_alloc_value_class(a->function, pm[i].value);
			for (uint32_t j = 0; j < n; j++)
				if (pm[j].kind != 0xff && pm[j].kind != VIR_EDIT_REMATERIALIZE && vir_alloc_location_index(pm[j].src) == d) {
					c = vir_alloc_value_class(a->function, pm[j].value);	/* the class of what d holds */
					break;
				}
			{
				uint64_t m = a->registers->alloc[c] & ~nmask[c];

				while (m) {
					uint32_t r = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m));

					m &= m - 1;
					if (!VIR_ALLOC_LIVE_COUNT(r) && ax.lds[r] != g) {
						t = r;
						break;
					}
				}
			}
			if (t == VIR_REGISTER_NONE && !(pm[i].dst & VIR_LOCATION_SLOT) && !(pm[i].src & VIR_LOCATION_SLOT) && pm[i].kind == VIR_EDIT_MOVE) {
				uint32_t s = vir_alloc_location_index(pm[i].src);

				/* exchange: d gets s's value (this move is done), s gets d's: d's readers read s */
				vir_alloc_move_add(a, VIR_EDIT_EXCHANGE, pm[i].dst, pm[i].src, pm[i].value);
				ax.lcnt[s]--;
				for (uint32_t j = 0; j < n; j++)
					if (pm[j].kind != 0xff && pm[j].kind != VIR_EDIT_REMATERIALIZE && j != i && vir_alloc_location_index(pm[j].src) == d) {
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
			if (t == VIR_REGISTER_NONE)
				t = VIR_REGISTER_SCRATCH(c);
			/* save d's content in t (the value its readers read); d's readers read t */
			vir_alloc_move_add(a, VIR_EDIT_MOVE, t, pm[i].dst, pm[i].value);
			for (uint32_t j = 0, lab = 0; j < n; j++)
				if (pm[j].kind != 0xff && pm[j].kind != VIR_EDIT_REMATERIALIZE && vir_alloc_location_index(pm[j].src) == d) {
					if (!lab++)
						ax.move[a->move_count - 1].value = pm[j].value;
					pm[j].src = t;
				}
			if (ax.lcs[t] != g)
				ax.lcs[t] = g;
			ax.lcnt[t] = ax.lcnt[d];
			ax.lcnt[d] = 0;
			break;
		}
		for (uint32_t i = 0; i < n; i++)	/* what the break freed */
			if (pm[i].kind != 0xff && !VIR_ALLOC_LIVE_COUNT(vir_alloc_location_index(pm[i].dst)))
				rq[nr++] = i;
	}
#undef VIR_ALLOC_LIVE_COUNT
}

/* the edge from p (its successor number k) to b: moves that turn p's end state into b's entry state */
static void conform(vir_allocator *a, uint32_t p, uint32_t k, uint32_t b)
{
	vir_function *f = a->function;
	const vir_block *t = &f->blocks[b];
	uint32_t g = vir_alloc_stamp_next(a), n = 0, term = vir_alloc_block_last(f, p), first = a->move_count;
	uint64_t nmask[2] = { 0, 0 };
	const vir_insn *ti = &f->insns[term];

	for (uint32_t j = 0; j < ax.end_n[p]; j++) {
		uint32_t v = ax.pv[ax.end_off[p] + j];

		ax.estamp[v] = g;
		ax.eloc[v] = ax.pr[ax.end_off[p] + j];
		ax.epst[v] = ax.ps[ax.end_off[p] + j];
	}
	for (uint32_t j = 0; j < ax.ent_n[b]; j++) {
		uint32_t r = ax.pr[ax.ent_off[b] + j];

		nmask[VIR_REGISTER_CLASS(r)] |= 1ull << VIR_REGISTER_NUMBER(r);
		ax.tmk[ax.pv[ax.ent_off[b] + j]] = g;
	}
	/* stores: b's entry has the value in its slot (out of a register, or stored on the
	 * earlier paths), p has it only in a register */
	for (uint32_t j = 0; j < ax.end_n[p]; j++) {
		uint32_t v = ax.pv[ax.end_off[p] + j];

		if (ax.ps[ax.end_off[p] + j] || vir_alloc_value_is_constant(f, v) || ax.slot[v] == VIR_BLOCK_NONE || ax.eloc[v] != ax.pr[ax.end_off[p] + j])
			continue;
		if (v >= t->first && v < t->first + t->parameter_count)
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
		ax.pm[n++] = (vir_alloc_parallel_move){ VIR_LOCATION_SLOT | ax.slot[v], ax.eloc[v], v, VIR_EDIT_MOVE };
	}
	for (uint32_t j = 0; j < ax.ent_n[b]; j++) {
		uint32_t v = ax.pv[ax.ent_off[b] + j], r = ax.pr[ax.ent_off[b] + j];
		vir_alloc_parallel_move *m = &ax.pm[n];

		if (v >= t->first && v < t->first + t->parameter_count)
			continue;	/* block parameters below */
		if (ax.estamp[v] == g) {
			if (ax.eloc[v] == r)
				continue;
			*m = (vir_alloc_parallel_move){ r, ax.eloc[v], v, VIR_EDIT_MOVE };
		} else if (vir_alloc_value_is_constant(f, v)) {
			*m = (vir_alloc_parallel_move){ r, 0, v, VIR_EDIT_REMATERIALIZE };
		} else if (ax.slot[v] != VIR_BLOCK_NONE) {
			*m = (vir_alloc_parallel_move){ r, VIR_LOCATION_SLOT | ax.slot[v], v, VIR_EDIT_MOVE };
		} else {
			continue;	/* not defined on this path (15.5, irreducible) */
		}
		n++;
	}
	if (ti->op == VIR_OP_BR && ti->a == b)
		for (uint32_t j = 0; j < t->parameter_count; j++) {
			uint32_t q = t->first + j, x = f->extra[ti->c + j], dst, src;
			uint8_t r = ax.opr[ax.boff[b] + j];
			vir_alloc_parallel_move *m = &ax.pm[n];

			if (r != VIR_REGISTER_NONE)
				dst = r;
			else if (ax.slot[q] != VIR_BLOCK_NONE)
				dst = VIR_LOCATION_SLOT | ax.slot[q];
			else
				continue;	/* unused parameter */
			if (vir_alloc_value_is_constant(f, x)) {
				*m = (vir_alloc_parallel_move){ dst, 0, x, VIR_EDIT_REMATERIALIZE };
				n++;
				continue;
			}
			if (ax.estamp[x] == g && ax.epst[x] && (dst & VIR_LOCATION_SLOT) && ax.slot[x] == (dst & ~VIR_LOCATION_SLOT))
				continue;	/* stored there already (a web's slot) */
			if (ax.estamp[x] == g)
				src = ax.eloc[x];
			else if (ax.slot[x] != VIR_BLOCK_NONE)
				src = VIR_LOCATION_SLOT | ax.slot[x];
			else
				continue;
			if (src == dst)
				continue;
			*m = (vir_alloc_parallel_move){ dst, src, x, VIR_EDIT_MOVE };
			n++;
		}
	if (n) {
		vir_alloc_moves_sequence(a, n, nmask);
		ax.eidx[ax.soff[p] + k] = a->edge_count;
		ax.edge[a->edge_count++] = (vir_alloc_edge){ p, b, k, first, a->move_count - first };
		a->out->statistics.edges++;
	}
}

static void vir_alloc_state_save(vir_allocator *a, uint32_t *off, uint32_t *cnt, uint32_t b)
{
	off[b] = a->pair_count;
	for (int c = 0; c < 2; c++) {
		uint64_t m = a->registers->alloc[c] & ~ax.fr[c];

		while (m) {
			uint32_t r = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m)), v = ax.regval[r];

			m &= m - 1;
			if (v && ax.reg[v] == r) {
				ax.pv[a->pair_count] = v;
				ax.ps[a->pair_count] = vir_alloc_value_is_stored(a, v);
				ax.pr[a->pair_count++] = (uint8_t)r;
			}
		}
	}
	cnt[b] = a->pair_count - off[b];
}

/* q, not read in b, is read near the top of each successor of b (two at most) before any call there (a
 * join's value the next test reads: in its slot it would be stored on each edge and loaded
 * at once) */
static bool vir_alloc_value_is_read_next(vir_allocator *a, uint32_t b, uint32_t q)
{
	vir_function *f = (vir_function *)a->function;

	if (ax.soff[b + 1] - ax.soff[b] > 2)
		return false;
	for (uint32_t s = ax.soff[b]; s < ax.soff[b + 1]; s++) {
		const vir_block *k = &f->blocks[ax.succ[s]];
		uint32_t end = k->first + (k->insn_count < k->parameter_count + 16 ? k->insn_count : k->parameter_count + 16);
		bool hit = false;

		for (uint32_t i = k->first + k->parameter_count; i < end && !hit && f->insns[i].op != VIR_OP_CALL; i++) {
			vir_operand_iterator it;
			uint32_t *p;

			vir_operand_iterator_init(&it, f, &f->insns[i], false);
			while ((p = vir_operand_iterator_next(&it)))
				hit |= *p == q;
		}
		if (!hit)
			return false;
	}
	return ax.soff[b + 1] > ax.soff[b];
}

static bool livein(vir_allocator *a, uint32_t v, uint32_t b)
{
	const vir_function *f = a->function;
	uint32_t first = f->blocks[b].first;

	if (vir_alloc_value_is_constant(f, v)) {
		const vir_block *k = &f->blocks[b];
		uint32_t l = a->loops->inner[b];

		if (!ax.nu[v])
			return false;
		if (k->predecessor_count == 1)
			return true;
		return l != VIR_BLOCK_NONE && a->loops->loops[l].head == b && ax.nu[v] <= ax.lendpos[l];
	}
	if (v >= first)
		return false;
	if (a->linear)
		return ax.endp[v] >= first;
	/* (live round a loop: in each of its blocks) */
	return ax.mark[v] == a->move_base + b || vir_alloc_value_is_live_in_loop(a, v, first);
}

/* parameter j of b (class c), tentatively in h: the register in which most of the
 * predecessors placed already end with their argument, when at least two do and more
 * than have it in h (their edges then move nothing: a loop whose common path passes
 * its values on unchanged pays for the rare path's moves otherwise); -1 if none */
static int vir_alloc_argument_register_vote(vir_allocator *a, uint32_t b, uint32_t j, uint32_t c, uint32_t h)
{
	const vir_function *f = a->function;
	const vir_block *k = &f->blocks[b];
	uint8_t cnt[64] = { 0 };
	int best = -1;

	if (k->predecessor_count < 3 || k->predecessor_count > 32)
		return -1;
	for (uint32_t i = 0; i < k->predecessor_count; i++) {
		uint32_t p = f->extra[k->predecessor_first + i], x;
		const vir_insn *t;

		if (p >= b)
			continue;
		t = &f->insns[vir_alloc_block_last(f, p)];
		if (t->op != VIR_OP_BR || t->a != b || vir_alloc_value_is_constant(f, x = f->extra[t->c + j]))
			continue;
		for (uint32_t y = 0; y < ax.end_n[p]; y++)
			if (ax.pv[ax.end_off[p] + y] == x) {
				uint32_t r = ax.pr[ax.end_off[p] + y];

				if (r < 128 && VIR_REGISTER_CLASS(r) == c && ++cnt[VIR_REGISTER_NUMBER(r)] > (best < 0 ? 0 : cnt[best]))
					best = (int)VIR_REGISTER_NUMBER(r);
				break;
			}
	}
	if (best < 0 || cnt[best] < 2 || (h < 128 && VIR_REGISTER_CLASS(h) == c && cnt[VIR_REGISTER_NUMBER(h)] >= cnt[best]))
		return -1;
	return (int)VIR_REGISTER_MAKE(c, best);
}

/* The header b of loop l, entered from `best`: a value that is used in the loop and comes
 * in in its slot takes a register on the way in (the edges into b load it: once, in front
 * of the loop) when the loop leaves one free: beside the values in registers now, the
 * most the body has live at once beyond those live here (maxp) and one more, since maxp
 * does not see a value that is live round the loop below its last use.  In a loop with a call
 * only a callee-saved register keeps it.  Soonest use first. */
static void vir_alloc_loop_values_preload(vir_allocator *a, uint32_t b, uint32_t l, uint32_t best)
{
	vir_function *f = a->function;
	const vir_block *k = &f->blocks[b];
	uint32_t le = ax.lendpos[l], live[2] = { 0, 0 }, cand[16], nc = 0;

	for (uint32_t j = 0; j < k->parameter_count; j++)
		if (!(ax.vfl[k->first + j] & VIR_ALLOC_FLAG_DEAD))
			live[vir_alloc_value_class(f, k->first + j)]++;
	for (uint32_t j = 0; j < ax.lin_n[b]; j++) {
		uint32_t v = ax.pool[ax.lin_off[b] + j];

		if (vir_alloc_value_is_constant(f, v))
			continue;
		live[vir_alloc_value_class(f, v)]++;
		if (nc < 16 && !vir_alloc_value_is_in_register(a, v) && ax.slot[v] != VIR_BLOCK_NONE && ax.nu[v] && ax.nu[v] <= le)
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
		c = vir_alloc_value_class(f, v);
		set = a->registers->alloc[c] & (ax.lcall[l] ? a->registers->callee[c] : ~0ull);
		nr = (uint32_t)__builtin_popcountll(set);
		used = (uint32_t)__builtin_popcountll(set & ~ax.fr[c]);
		extra = ax.maxp[2 * l + c] > live[c] ? ax.maxp[2 * l + c] - live[c] : 0;
		if (used + extra + 1 >= nr || (r = vir_alloc_register_choose(a, v, c, ~set)) < 0)	/* (one is left for a temporary) */
			continue;
		vir_alloc_register_set(a, v, (uint32_t)r);
		/* in its slot on the way from `best`; another earlier predecessor may hold it unstored */
		for (uint32_t j = 0; j < k->predecessor_count && st; j++) {
			uint32_t p = f->extra[k->predecessor_first + j];

			if (p >= b || p == best)
				continue;
			for (uint32_t y = 0; y < ax.end_n[p]; y++)
				if (ax.pv[ax.end_off[p] + y] == v && !ax.ps[ax.end_off[p] + y])
					st = false;
		}
		if (st)
			vir_alloc_value_stored_mark(a, v);
	}
}

/* A block parameter that is used late and lives across a call enters in the
 * web's slot unless a callee-saved register keeps it.  Whether it keeps one
 * depends on how many the target has:
 *  - few (x86-64: rbx, r12-r15): the register it already holds is kept
 *    whenever it is callee-saved, since a caller-saved one only delays the
 *    spill.  The PR's authors measured +1.3 to +2.0 % on SQLite speedtest1
 *    (x86-64 Linux, their numbers; not reproducible on this arm64 machine).
 *  - many (arm64: x19-x28, d8-d15): only when at least two other callee-saved
 *    registers of the class stay free, since the values used sooner need
 *    them.  Measured on arm64 (SQLite speedtest1 --size 100, instructions
 *    retired): the few-registers rule makes it 0.84 % slower.
 * The target says which it is through its register description. */
enum { VIR_ALLOC_MANY_CALLEE_SAVED = 8 };
static bool vir_alloc_late_parameter_keeps_callee_saved(const vir_allocator *a, uint32_t c, uint32_t r)
{
	uint64_t callee = a->registers->callee[c];

	if (!(callee >> VIR_REGISTER_NUMBER(r) & 1))
		return false;
	if (__builtin_popcountll(callee & a->registers->alloc[c]) < VIR_ALLOC_MANY_CALLEE_SAVED)
		return true;
	return __builtin_popcountll(ax.fr[c] & callee & ~(1ull << VIR_REGISTER_NUMBER(r))) >= 2;
}

static void vir_alloc_block_enter(vir_allocator *a, uint32_t b)
{
	vir_function *f = a->function;
	const vir_block *k = &f->blocks[b];
	uint32_t best = VIR_BLOCK_NONE, bs = 0, l = a->loops->inner[b];
	bool relocate;

	if (!++ax.bgen) {	/* a new path state: nothing stored (the stamps wrapped: clear) */
		memset(ax.sst, 0, (size_t)VIR_MAX_INS * 4);
		ax.bgen = 1;
	}
	for (int c = 0; c < 2; c++)	/* the registers that can hold a value: allocatable or fixed */
		for (uint64_t m = a->register_mask[c] & ~ax.fr[c]; m; m &= m - 1)
			ax.regval[VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m))] = 0;
	ax.fr[0] = a->registers->alloc[0];
	ax.fr[1] = a->registers->alloc[1];
	if (!a->linear)
		for (uint32_t j = 0; j < ax.lin_n[b]; j++)
			ax.mark[ax.pool[ax.lin_off[b] + j]] = a->move_base + b;
	if (b && !(k->flags & VIR_BLOCK_FLAG_ADDR)) {
		for (uint32_t j = 0; j < k->predecessor_count; j++) {
			uint32_t p = f->extra[k->predecessor_first + j], s = 0;

			if (p >= b)
				continue;
			if (k->predecessor_count == 1) {	/* nothing to choose */
				best = p;
				break;
			}
			for (uint32_t x = 0; x < ax.end_n[p]; x++)
				s += livein(a, ax.pv[ax.end_off[p] + x], b);
			/* most live-in values in registers, then the deeper loop, not cold, the block before */
			s = s * 4 + (f->blocks[p].flags & VIR_BLOCK_FLAG_COLD ? 0 : 2) + (p == b - 1);
			{
				uint32_t lp_ = a->loops->inner[p], dp = lp_ == VIR_BLOCK_NONE ? 0 : a->loops->loops[lp_].depth;

				s += dp * 1024 * 1024;
			}
			if (best == VIR_BLOCK_NONE || s > bs) {
				best = p;
				bs = s;
			}
		}
		if (best != VIR_BLOCK_NONE) {
			for (uint32_t x = 0; x < ax.end_n[best]; x++) {
				uint32_t v = ax.pv[ax.end_off[best] + x];

				if (livein(a, v, b)) {
					vir_alloc_register_set(a, v, ax.pr[ax.end_off[best] + x]);
					if (ax.ps[ax.end_off[best] + x])
						vir_alloc_value_stored_mark(a, v);
				}
			}
			/* stored on entry: on every earlier predecessor (one with it in a register
			 * and not in its slot clears it; a later one stores it on its edge) */
			for (uint32_t j = 0; j < k->predecessor_count; j++) {
				uint32_t p = f->extra[k->predecessor_first + j];

				if (p >= b || p == best)
					continue;
				for (uint32_t x = 0; x < ax.end_n[p]; x++)
					if (!ax.ps[ax.end_off[p] + x])
						ax.sst[ax.pv[ax.end_off[p] + x]] = 0;
			}
		}
	}
	if (a->options && a->options->entry)
		a->options->entry(a->options->entry_context, b, best, 0, VIR_REGISTER_NONE, false, "predecessor");
	/* the loop-header rule: drop values live through the loop without a use in it */
	if (l != VIR_BLOCK_NONE && a->loops->loops[l].head == b)
		for (int c = 0; c < 2; c++) {
			uint32_t nr = (uint32_t)__builtin_popcountll(a->registers->alloc[c]), le = ax.lendpos[l];
			uint32_t excess = ax.maxp[2 * l + c] > nr ? ax.maxp[2 * l + c] - nr : 0;
			uint64_t m = a->registers->alloc[c] & ~ax.fr[c];

			while (m && excess) {
				uint32_t r = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m)), v = ax.regval[r];

				m &= m - 1;
				if (!vir_alloc_value_is_constant(f, v) && (ax.nu[v] == 0 || ax.nu[v] > le)) {
					evict(a, r, VIR_ALLOC_NO_POSITION);
					excess--;
				}
			}
		}
	/* block parameters */
	for (uint32_t j = 0; j < k->parameter_count; j++) {
		uint32_t q = k->first + j, c = vir_alloc_value_class(f, q);
		int r = -1;
		const char *decision = "dead-or-address";

		ax.slot[q] = VIR_BLOCK_NONE;
		ax.reg[q] = VIR_REGISTER_NONE;
		ax.lastpos[q] = q;
		ax.dmark[q] = 0;	/* (not an earlier function's: it is read for any value in a register) */
		if (!(ax.vfl[q] & VIR_ALLOC_FLAG_DEAD) && !(k->flags & VIR_BLOCK_FLAG_ADDR)) {
			uint32_t h = ax.treg[q];
			int vr = vir_alloc_argument_register_vote(a, b, j, c, h);

			if (vr >= 0 && (ax.fr[c] >> VIR_REGISTER_NUMBER((uint32_t)vr) & 1) &&
			    !vir_alloc_hint_prefers_callee_saved(a, q, c, ax.fr[c] & a->registers->alloc[c], (uint32_t)vr))
				h = (uint32_t)vr;
			if (h < 128 && VIR_REGISTER_CLASS(h) == c && (ax.fr[c] >> VIR_REGISTER_NUMBER(h) & 1) &&
			    !vir_alloc_hint_prefers_callee_saved(a, q, c, ax.fr[c] & a->registers->alloc[c], h))
				r = (int)h;
			else
				r = vir_alloc_register_choose(a, q, c, 0);
			decision = r < 0 ? "pressure-slot" : r == (int)h ? "hint-register" : "chosen-register";
			/* a web's parameter that lives across a call and is not used soon (not in b nor
			 * at the top of each successor, or after a call, or only passed on by b's br)
			 * enters in the web's slot: the edges then move nothing for an argument already
			 * there */
			if (r >= 0 && vir_alloc_web_is_valid(a, q) != VIR_ALLOC_NO_WEB) {
				uint32_t u = ax.nu[q], e = vir_alloc_block_last(f, b);
				const vir_insn *t = &f->insns[e];
				bool late = (!u || u > e || (ax.vfl[q] & VIR_ALLOC_FLAG_CALL_FIRST)) &&
					    !((!u || u > e) && !(ax.vfl[q] & VIR_ALLOC_FLAG_CALL_FIRST) && vir_alloc_value_is_read_next(a, b, q));

				if (u == e && t->op == VIR_OP_BR && t->a <= b) {
					/* only passed on, to a parameter placed already (a loop's header): where
					 * that one is */
					for (uint32_t x = 0; x < f->blocks[t->a].parameter_count; x++)
						if (f->extra[t->c + x] == q) {
							uint8_t pr = ax.opr[ax.boff[t->a] + x];

							if (pr == VIR_REGISTER_NONE) {
								r = -1;
								decision = "match-header-slot";
							} else if (VIR_REGISTER_CLASS(pr) == c && (ax.fr[c] >> VIR_REGISTER_NUMBER(pr) & 1)) {
								r = pr;
								decision = "match-header-register";
							}
							break;
						}
				} else if ((ax.vfl[q] & VIR_ALLOC_FLAG_CROSSES_CALL) && (late || (u == e && t->op == VIR_OP_BR)) &&
					   !vir_alloc_late_parameter_keeps_callee_saved(a, c, (uint32_t)r)) {
					r = -1;
					decision = "late-call-web-slot";
				}
			}
		}
		ax.sdef[q] = 0;
		if (r >= 0) {
			vir_alloc_register_set(a, q, (uint32_t)r);
			ax.treg[q] = (uint8_t)r;
		} else if (!(ax.vfl[q] & VIR_ALLOC_FLAG_DEAD)) {
			vir_alloc_spill_slot_assign(a, q);	/* the edges write the slot */
			vir_alloc_value_stored_mark(a, q);
		}
		if (ax.vfl[q] & VIR_ALLOC_FLAG_NEEDS_SLOT) {
			vir_alloc_spill_slot_assign(a, q);
			ax.sdef[q] = 1;	/* stored at the block's top */
			vir_alloc_value_stored_mark(a, q);
		}
		ax.opr[a->operand_register_count++] = r >= 0 ? (uint8_t)r : VIR_REGISTER_NONE;
		if (a->options && a->options->entry)
			a->options->entry(a->options->entry_context, b, best, q, r >= 0 ? (uint8_t)r : VIR_REGISTER_NONE,
					  ax.sst[q] == ax.bgen, decision);
	}
	if (l != VIR_BLOCK_NONE && a->loops->loops[l].head == b && best != VIR_BLOCK_NONE && !a->linear)
		vir_alloc_loop_values_preload(a, b, l, best);
	vir_alloc_state_save(a, ax.ent_off, ax.ent_n, b);
	if (a->options && a->options->entry)
		for (uint32_t j = 0; j < ax.ent_n[b]; j++) {
			uint32_t x = ax.ent_off[b] + j;
			a->options->entry(a->options->entry_context, b, best, ax.pv[x], ax.pr[x], ax.ps[x], "entry-register");
		}
	relocate = b == a->save_point;
	/* forward edges into b */
	{
		uint32_t g = vir_alloc_stamp_next(a);

		for (uint32_t j = 0; j < k->predecessor_count; j++) {
			uint32_t p = f->extra[k->predecessor_first + j];

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
			uint64_t m = a->registers->alloc[c] & ~ax.fr[c] & ~a->registers->callee[c];

			while (m) {
				uint32_t r = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m)), v = ax.regval[r];
				uint64_t fc = ax.fr[c] & a->registers->alloc[c] & a->registers->callee[c], u = fc & a->used_callee[c];
				uint32_t r2;

				m &= m - 1;
				if (!fc)
					break;
				if (!v || ax.reg[v] != r || vir_alloc_value_is_constant(f, v) || !(ax.vfl[v] & VIR_ALLOC_FLAG_CROSSES_CALL))
					continue;
				r2 = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(u ? u : fc));
				vir_alloc_edit_add(a, k->first + k->parameter_count, VIR_EDIT_MOVE, r2, r, v);
				vir_alloc_register_clear(a, r);
				vir_alloc_register_set(a, v, r2);
			}
		}
}

static bool vir_alloc_insn_assign(vir_allocator *a, uint32_t b, uint32_t i, bool term)
{
	vir_function *f = a->function;
	const vir_insn *in = &f->insns[i];
	vir_register_constraint fx;
	uint32_t n = ax.nop[i], *ops = &ax.ostk[a->operand_stack_count -= n];
	uint64_t lock[2] = { 0, 0 }, fixm[2], av[2];
	uint8_t *byte = a->register_bytes;

	vir_alloc_fix_get(a, i, &fx);
	if ((in->op == VIR_OP_ASM || in->op == VIR_OP_ASMGOTO) && n)
		return vir_alloc_failure_set(a, "inline assembly with operands (15.10) at instruction %u is not supported", i);
	for (uint32_t j = 0; j < n; j++) {	/* pop in operand order */
		uint32_t w = ax.stk[--a->stack_count], v = ops[j];

		a->operand_next_use[j] = w;
		ax.nu[v] = w & ~VIR_ALLOC_KILL;
		ax.lastpos[v] = i;
		/* dies here: the kill bit is on its last occurrence, which is assigned last */
		ax.dmark[v] = (vir_alloc_value_is_constant(f, v) ? !ax.nu[v] : (w & VIR_ALLOC_KILL) && (a->linear ? ax.lend[v] <= i : !vir_alloc_value_is_live_in_loop(a, v, i))) ? i : 0;
	}
	if (in->op == VIR_OP_BR) {
		for (uint32_t j = 0; j < n; j++)
			ax.opr[a->operand_register_count++] = vir_alloc_value_is_in_register(a, ops[j]) ? ax.reg[ops[j]] : VIR_REGISTER_NONE;
		vir_alloc_state_save(a, ax.end_off, ax.end_n, b);
		return true;
	}
	fixm[0] = fx.edge_clobbered[0];
	fixm[1] = fx.edge_clobbered[1];
	if (fx.result != VIR_REGISTER_NONE)
		fixm[VIR_REGISTER_CLASS(fx.result)] |= 1ull << VIR_REGISTER_NUMBER(fx.result);
	for (uint32_t j = 0; j < n && j < fx.operand_count; j++)
		if (fx.operands[j] != VIR_REGISTER_NONE)
			fixm[VIR_REGISTER_CLASS(fx.operands[j])] |= 1ull << VIR_REGISTER_NUMBER(fx.operands[j]);
	a->register_mask[0] |= fixm[0];
	a->register_mask[1] |= fixm[1];
	/* 1: fixed operands, as a parallel move: first those whose register does not hold a
	 * fixed operand that is still to be placed (an argument that moves from one argument
	 * register to the next is copied before its register is written, not moved aside);
	 * in a cycle the lowest one, whose register's value is moved aside */
	for (uint32_t j = 0; j < n; j++) {
		byte[j] = VIR_REGISTER_NONE;
		a->operand_state[j] = j < fx.operand_count && fx.operands[j] != VIR_REGISTER_NONE ? 0 : 2;
	}
	for (;;) {
		uint32_t j = n, first = n, v, R;

		for (uint32_t k = 0; k < n && k < fx.operand_count && j == n; k++) {
			uint32_t w;
			bool ok = true;

			if (a->operand_state[k])
				continue;
			if (first == n)
				first = k;
			w = ax.regval[fx.operands[k]];
			if (w && w != ops[k])
				for (uint32_t y = 0; y < n && y < fx.operand_count && ok; y++)
					ok = a->operand_state[y] || y == k || ops[y] != w;
			if (ok)
				j = k;
		}
		if (j == n)
			j = first;
		if (j == n)
			break;
		a->operand_state[j] = 1;
		v = ops[j];
		R = fx.operands[j];
		if (ax.regval[R] != v) {
			uint32_t w = ax.regval[R];
			bool dies = w && ax.dmark[w] == i;	/* (an operand here, read before the clobbers) */

			/* a value that dies here, with its every operand already placed (copied to its
			   fixed register): nothing reads it in R any more */
			for (uint32_t k = 0; dies && k < n; k++)
				if (k != j && ops[k] == w && a->operand_state[k] != 1)
					dies = false;
			if (dies && ax.reg[w] == R) {
				bool placed = false;

				for (uint32_t k = 0; k < n; k++)
					placed |= k != j && a->operand_state[k] == 1 && ops[k] == w;
				if (placed) {
					vir_alloc_register_clear(a, R);
					goto free;
				}
			}
			dies = w && ax.dmark[w] == i;
			av[0] = lock[0] | fixm[0] | (dies ? 0 : fx.clobbered[0]);
			av[1] = lock[1] | fixm[1] | (dies ? 0 : fx.clobbered[1]);
			vir_alloc_register_displace(a, R, av, i);
		free:
			if (vir_alloc_value_is_in_register(a, v)) {
				vir_alloc_edit_add(a, i, VIR_EDIT_MOVE, R, ax.reg[v], v);
				ax.regval[R] = v;	/* a copy: v's register stays where it is */
				ax.fr[VIR_REGISTER_CLASS(R)] &= ~(1ull << VIR_REGISTER_NUMBER(R));
			} else {
				if (!vir_alloc_value_fetch(a, v, R, i))
					return false;
				vir_alloc_register_set(a, v, R);
			}
		}
		lock[VIR_REGISTER_CLASS(R)] |= 1ull << VIR_REGISTER_NUMBER(R);
		byte[j] = (uint8_t)R;
	}
	/* 2: the other operands */
	for (uint32_t j = 0; j < n; j++) {
		uint32_t v = ops[j], c = vir_alloc_value_class(f, v);
		bool mem = (j < 32 && (fx.mem >> j & 1)) || (j >= fx.operand_count && (fx.flags & VIR_CONSTRAINT_FLAG_REST_MEMORY));

		if (j < fx.operand_count && fx.operands[j] != VIR_REGISTER_NONE)
			continue;
		if (vir_alloc_value_is_in_register(a, v)) {
			uint32_t r = ax.reg[v];

			if (fx.edge_clobbered[c] >> VIR_REGISTER_NUMBER(r) & 1) {
				int r2 = vir_alloc_register_get(a, v, c, lock[c] | fixm[c], i);

				if (r2 < 0)
					return vir_alloc_failure_set(a, "no register for an operand at instruction %u", i);
				vir_alloc_edit_add(a, i, VIR_EDIT_MOVE, (uint32_t)r2, r, v);
				vir_alloc_register_clear(a, r);
				vir_alloc_register_set(a, v, (uint32_t)r2);
				r = (uint32_t)r2;
			}
			lock[c] |= 1ull << VIR_REGISTER_NUMBER(r);
			byte[j] = (uint8_t)r;
			continue;
		}
		if (mem && (vir_alloc_value_is_constant(f, v) || ax.slot[v] != VIR_BLOCK_NONE))
			continue;	/* an immediate, or memory */
		{
			int r = vir_alloc_register_get(a, v, c, lock[c] | fixm[c], i);

			if (r < 0)
				return vir_alloc_failure_set(a, "no register for an operand at instruction %u", i);
			if (!vir_alloc_value_fetch(a, v, (uint32_t)r, i))
				return false;
			vir_alloc_register_set(a, v, (uint32_t)r);
			lock[c] |= 1ull << VIR_REGISTER_NUMBER(r);
			byte[j] = (uint8_t)r;
		}
	}
	for (uint32_t j = 0; j < n; j++)
		ax.opr[a->operand_register_count++] = byte[j];
	if (term) {
		/* A dead operand or fixed-register copy in a clobbered register needs
		 * no preservation. The physical input remains readable until dispatch. */
		for (uint32_t j = 0; j < n; j++) {
			uint32_t r=byte[j],v=ops[j];
			if(r!=VIR_REGISTER_NONE && ((fx.clobbered[VIR_REGISTER_CLASS(r)]|fx.edge_clobbered[VIR_REGISTER_CLASS(r)]) >> VIR_REGISTER_NUMBER(r) & 1) &&
			   ax.regval[r]==v && (ax.reg[v]!=r || ax.dmark[v]==i))vir_alloc_register_clear(a,r);
		}
		vir_alloc_clobbers_apply(a, &fx, lock, fixm, i);
		vir_alloc_state_save(a, ax.end_off, ax.end_n, b);
		return true;
	}
	/* 3: the registers of operands that die here, and copies in fixed registers */
	for (uint32_t j = 0; j < n; j++) {
		uint32_t v = ops[j];

		if (byte[j] != VIR_REGISTER_NONE && ax.regval[byte[j]] == v && ax.reg[v] != byte[j])
			vir_alloc_register_clear(a, byte[j]);
		if (ax.dmark[v] == i && vir_alloc_value_is_in_register(a, v))
			vir_alloc_register_clear(a, ax.reg[v]);
	}
	/* 4: clobbers: live values leave the clobbered registers */
	vir_alloc_clobbers_apply(a, &fx, lock, fixm, i);
	/* 5: the result */
	if (in->type != VIR_TYPE_VOID && in->type != VIR_TYPE_MULTI) {
		uint32_t c = vir_alloc_value_class(f, i);
		int r = -1;

		ax.slot[i] = VIR_BLOCK_NONE;
		ax.reg[i] = VIR_REGISTER_NONE;
		ax.lastpos[i] = i;
		ax.dmark[i] = 0;
		if (fx.result != VIR_REGISTER_NONE) {
			av[0] = lock[0] | fixm[0];
			av[1] = lock[1] | fixm[1];
			if (ax.regval[fx.result])
				vir_alloc_register_displace(a, fx.result, av, i);
			r = fx.result;
		} else if (ax.vfl[i] & VIR_ALLOC_FLAG_DEAD) {
			r = vir_alloc_register_choose(a, i, c, lock[c]);
		} else if ((fx.flags & VIR_CONSTRAINT_FLAG_TIED) && n) {
			/* (not in place in a caller-saved register when the result lives across a call
			 * and a callee-saved one is free: one move here, not one at every call) */
			uint64_t fm = ax.fr[c] & a->registers->alloc[c] & ~lock[c] & ~a->parallel_avoid[c];
			bool xc = byte[0] != VIR_REGISTER_NONE && (ax.vfl[i] & (VIR_ALLOC_FLAG_CROSSES_CALL | VIR_ALLOC_FLAG_CROSSES_LOOP)) == (VIR_ALLOC_FLAG_CROSSES_CALL | VIR_ALLOC_FLAG_CROSSES_LOOP) &&
				  !(a->registers->callee[c] >> VIR_REGISTER_NUMBER(byte[0]) & 1) && (fm & a->registers->callee[c]);

			/* an extension is an instruction in any register: in its hint's, when that is free */
			uint32_t h = ax.hint[i];
			bool hx = in->op == VIR_OP_EXT && (h & VIR_ALLOC_HINT_REGISTER) && VIR_REGISTER_CLASS(h & 0xff) == c && (fm >> VIR_REGISTER_NUMBER(h & 0xff) & 1) &&
				  (h & 0xff) != byte[0];

			if (hx)
				r = (int)(h & 0xff);
			else if (!xc && byte[0] != VIR_REGISTER_NONE && ax.dmark[ops[0]] == i && !ax.regval[byte[0]])
				r = byte[0];
			else if (!xc && (fx.flags & VIR_CONSTRAINT_FLAG_COMMUTATIVE) && n >= 2 && byte[1] != VIR_REGISTER_NONE && ax.dmark[ops[1]] == i && ops[1] != ops[0] &&
				 !ax.regval[byte[1]]) {
				vir_alloc_edit_add(a, i, VIR_EDIT_SWAP, VIR_REGISTER_NONE, VIR_REGISTER_NONE, i);
				r = byte[1];
			} else {
				r = vir_alloc_register_get(a, i, c, lock[c], i);
				if (r < 0 && byte[0] != VIR_REGISTER_NONE) {
					/* every register holds an operand: the first one leaves for its slot (or is
					 * dropped, a constant) and the result is computed in place */
					evict(a, byte[0], i);
					r = byte[0];
				}
			}
		} else {
			r = vir_alloc_register_choose(a, i, c, 0);
			if (r < 0)
				r = vir_alloc_register_get(a, i, c, 0, i);
		}
		if (r < 0 && !(ax.vfl[i] & VIR_ALLOC_FLAG_DEAD)) {
			snprintf(a->out->error_message, sizeof(a->out->error_message), "no register for the result of instruction %u (%s)", i,
				 vir_opcode_table[in->op].name);
			a->failed = true;
			return false;
		}
		if (r >= 0) {
			uint32_t h = ax.hint[i];

			vir_alloc_register_set(a, i, (uint32_t)r);
			if (h && !(h & VIR_ALLOC_HINT_REGISTER) && h < f->insn_count && f->insns[h].op == VIR_OP_BPARAM && ax.treg[h] == VIR_REGISTER_NONE)
				ax.treg[h] = (uint8_t)r;	/* the first argument sets the parameter's tentative register */
		}
		ax.opr[a->operand_register_count++] = r >= 0 ? (uint8_t)r : VIR_REGISTER_NONE;
		ax.sdef[i] = 0;
		if (ax.vfl[i] & VIR_ALLOC_FLAG_NEEDS_SLOT) {
			vir_alloc_spill_slot_assign(a, i);
			ax.sdef[i] = 1;	/* stored at its definition */
			vir_alloc_value_stored_mark(a, i);
		}
		if ((ax.vfl[i] & VIR_ALLOC_FLAG_DEAD) && r >= 0)
			vir_alloc_register_clear(a, (uint32_t)r);
		/* a fixed result (a parameter, a call's) that lives across a later call leaves its
		 * caller-saved register now, for a callee-saved one: the register is free for the
		 * arguments computed before that call, instead of moved away at it */
		if (fx.result != VIR_REGISTER_NONE && r >= 0 && !(ax.vfl[i] & VIR_ALLOC_FLAG_DEAD) && (ax.vfl[i] & VIR_ALLOC_FLAG_CROSSES_CALL) && !a->before_save_point &&
		    !(a->registers->callee[c] >> VIR_REGISTER_NUMBER(r) & 1) && i + 1 <= vir_alloc_block_last(f, b)) {
			uint64_t m = ax.fr[c] & a->registers->alloc[c] & a->registers->callee[c] & ~a->parallel_avoid[c];

			if (m) {
				uint64_t u = m & a->used_callee[c];
				uint32_t r2 = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(u ? u : m));

				vir_alloc_edit_add(a, i + 1, VIR_EDIT_MOVE, r2, (uint32_t)r, i);
				vir_alloc_register_clear(a, (uint32_t)r);
				vir_alloc_register_set(a, i, r2);
			}
		}
	}
	return true;
}

/* ---------------- the driver ---------------- */

static bool vir_alloc_preconditions_check(vir_allocator *a)
{
	vir_function *f = a->function;
	const vir_block *k = &f->blocks[0];
	bool body = false;

	for (uint32_t i = k->first; i < k->first + k->insn_count; i++) {
		if (f->insns[i].op == VIR_OP_PARAM && body)
			return vir_alloc_failure_set(a, "`param` at instruction %u after other instructions", i);
		if (f->insns[i].op != VIR_OP_PARAM)
			body = true;
	}
	for (uint32_t i = f->constant_count; i < f->insn_count; i++)
		if (f->insns[i].op == VIR_OP_PROJ) {
			uint32_t c = f->insns[i].a, p = i - 1;

			while (p > c && f->insns[p].op == VIR_OP_PROJ && f->insns[p].a == c)
				p--;
			if (p != c)
				return vir_alloc_failure_set(a, "`proj` at instruction %u does not follow its call", i);
		}
	return true;
}

/*
 * Shrink-wrapping (15.12).  The save point S: the block that dominates every block that
 * needs the frame (a call that is not a sibling call, memcpy/memset, inline asm, va_start,
 * a slot's address, a block entered through its address), is in no loop, and dominates
 * every block that can be reached from it.  Each way through the function then passes S
 * once or never, and a return that S does not dominate is reached without the frame.
 * VIR_BLOCK_NONE when that is the entry, or the dominator tree is not in block order.
 */
static uint32_t vir_alloc_dominator_common(const vir_function *f, uint32_t x, uint32_t y)
{
	while (x != y) {
		uint32_t *hi = x > y ? &x : &y, d = f->blocks[*hi].immediate_dominator;

		if (d == VIR_BLOCK_NONE || d >= *hi)
			return 0;
		*hi = d;
	}
	return x;
}

/* block t only returns (through lone `br`s at most): the lowering gives each side of the
 * save point its own copy */
static bool vir_alloc_block_is_return_only(const vir_function *f, uint32_t t)
{
	for (int hop = 0; hop < 8; hop++) {
		const vir_block *k = &f->blocks[t];
		const vir_insn *x = &f->insns[k->first + k->insn_count - 1];

		if (k->flags & VIR_BLOCK_FLAG_ADDR)
			return false;
		if (k->insn_count - k->parameter_count == 1 && x->op == VIR_OP_RET)
			return true;
		if (k->parameter_count || k->insn_count != 1 || x->op != VIR_OP_BR)
			return false;
		t = x->a;
	}
	return false;
}

static uint32_t vir_alloc_save_point_find(vir_allocator *a, const vir_alloc_options *opt)
{
	vir_function *f = a->function;
	uint32_t nb = f->block_count, s = VIR_BLOCK_NONE;
	bool ch = true;

	for (uint32_t b = 0; b < nb; b++) {
		const vir_block *k = &f->blocks[b];
		bool need = b && (k->flags & VIR_BLOCK_FLAG_ADDR);

		if (k->immediate_dominator == VIR_BLOCK_NONE)
			return VIR_BLOCK_NONE;	/* (an unreachable block: not worth the care) */
		for (uint32_t i = k->first + k->parameter_count; i < k->first + k->insn_count && !need; i++) {
			uint32_t n;

			switch (f->insns[i].op) {
			case VIR_OP_CALL:
				need = !(opt->no_frame && opt->no_frame[i]);
				break;
			case VIR_OP_ASM: case VIR_OP_ASMGOTO: case VIR_OP_MEMCPY: case VIR_OP_MEMSET: case VIR_OP_VA_START:
			case VIR_OP_FRAMEADDR: case VIR_OP_RETADDR: case VIR_OP_TLSADDR:
				need = true;
				break;
			}
			n = vir_alloc_insn_operands(a, i);
			if (a->failed)
				return VIR_BLOCK_NONE;
			for (uint32_t j = 0; j < n; j++)
				need |= f->insns[a->operands[j]].op == VIR_OP_SLOTADDR;
		}
		if (need)
			s = s == VIR_BLOCK_NONE ? b : vir_alloc_dominator_common(f, s, b);
		if (!s)
			return VIR_BLOCK_NONE;
	}
	if (s == VIR_BLOCK_NONE)
		return VIR_BLOCK_NONE;
	while (ch) {
		ch = false;
		while (s && a->loops->inner[s] != VIR_BLOCK_NONE)
			s = f->blocks[s].immediate_dominator < s ? f->blocks[s].immediate_dominator : 0;
		if (!s)
			return VIR_BLOCK_NONE;
		for (uint32_t b = s; b < nb && !ch; b++) {
			if (!vir_block_dominates(f, s, b))
				continue;
			for (uint32_t x = ax.soff[b]; x < ax.soff[b + 1]; x++)
				if (!vir_block_dominates(f, s, ax.succ[x]) && !vir_alloc_block_is_return_only(f, ax.succ[x])) {
					s = vir_alloc_dominator_common(f, s, ax.succ[x]);
					ch = true;
					break;
				}
		}
		if (!s)
			return VIR_BLOCK_NONE;
	}
	return s;
}

bool vir_alloc(vir_function *f, const vir_loops *lp, const vir_target_registers *rg, const vir_alloc_options *opt, vir_alloc_out *out)
{
	vir_allocator a_, *a = &a_;
	uint32_t nb = f->block_count, ns = 0;

	vir_alloc_thread_arrays_init();
	memset(a, 0, sizeof(*a));
	a->arrays = &vir_alloc_thread_arrays_instance;
	memset(out, 0, sizeof(*out));
	a->function = f;
	a->loops = lp;
	a->registers = rg;
	a->options = opt;
	a->out = out;
	a->pool_maximum = opt && opt->pool ? opt->pool : VIR_MAX_INS;
	if (!nb || !vir_alloc_preconditions_check(a))
		return false;
	if (!vir_cfg_build(f) || !vir_dominators_build(f))
		return vir_alloc_failure_set(a, "bad control-flow graph%s", 0);
	/* successors (CSR), loop end positions, blocks with no earlier predecessor */
	for (uint32_t b = 0; b < nb; b++) {
		vir_insn t = f->insns[vir_alloc_block_last(f, b)];
		vir_operand_iterator it;
		uint32_t *p;
		bool early = false;

		ax.soff[b] = ns;
		vir_operand_iterator_init(&it, f, &t, true);
		while ((p = vir_operand_iterator_next(&it))) {
			ax.eidx[ns] = VIR_BLOCK_NONE;
			ax.succ[ns++] = *p;
		}
		for (uint32_t j = 0; j < f->blocks[b].predecessor_count; j++)
			early |= f->extra[f->blocks[b].predecessor_first + j] < b;
		ax.bmark[b] = !early;
	}
	ax.soff[nb] = ns;
	a->successor_count = ns;
	/* apre[b]: blocks before b entered with every value in its slot */
	ax.apre[0] = 0;
	for (uint32_t b = 0; b < nb; b++)
		ax.apre[b + 1] = ax.apre[b] + (b && ((f->blocks[b].flags & VIR_BLOCK_FLAG_ADDR) || ax.bmark[b]));
	for (uint32_t l = 0; l < lp->count; l++) {
		ax.lendpos[l] = vir_alloc_block_last(f, lp->loops[l].end);
		ax.lhead[l] = f->blocks[lp->loops[l].head].first;
	}
	/* per back-edge target t: the end of the innermost loop that contains each edge s -> t (s >= t);
	 * values live into t are live to there (15.5).  In reducible flow t is a loop header and this
	 * is its loop's end; in irreducible flow t can be any entry of a merged range */
	for (uint32_t b = 0; b < nb; b++) {
		const vir_block *k = &f->blocks[b];

		ax.tend[b] = ax.tendb[b] = 0;
		for (uint32_t j = 0; j < k->predecessor_count; j++) {
			uint32_t p = f->extra[k->predecessor_first + j], l = lp->inner[b];

			if (p < b)
				continue;
			while (l != VIR_BLOCK_NONE && lp->loops[l].end < p)
				l = lp->loops[l].parent;
			if (l != VIR_BLOCK_NONE && ax.lendpos[l] > ax.tend[b]) {
				ax.tend[b] = ax.lendpos[l];
				ax.tendb[b] = lp->loops[l].end;
				ax.tbeg[b] = lp->loops[l].head;
			}
		}
		ax.tendi[b] = ax.tend[b];
		ax.tendib[b] = ax.tendb[b];
	}
	/* tendi: per loop, its back edges to other blocks than its head, merged where they
	 * overlap; a value live into such a target t whose definition does not dominate the head
	 * is live to the end of t's group, not round the loop (15.5) */
	for (uint32_t l = 0; l < lp->count; l++) {
		uint32_t h = lp->loops[l].head, e = lp->loops[l].end, gs = VIR_BLOCK_NONE, ge = 0;

		for (uint32_t x = h + 1; x <= e + 1; x++) {
			uint32_t sm = 0;

			if (x <= e)
				for (uint32_t j = 0; j < f->blocks[x].predecessor_count; j++) {
					uint32_t p = f->extra[f->blocks[x].predecessor_first + j];

					if (p >= x && p > sm)
						sm = p;
				}
			if (x <= e && !sm)
				continue;
			if (gs != VIR_BLOCK_NONE && x <= ge && x <= e) {
				if (sm > ge)
					ge = sm;
				continue;
			}
			/* a group [gs, ge] ends: its targets of this loop take ge */
			if (gs != VIR_BLOCK_NONE)
				for (uint32_t t = gs; t <= ge; t++)
					if (ax.tend[t] == ax.lendpos[l] && ax.tbeg[t] == h) {
						ax.tendi[t] = vir_alloc_block_last(f, ge);
						ax.tendib[t] = ge;
					}
			gs = x;
			ge = sm;
		}
	}
	webs(a);
	if (!vir_alloc_liveness_sweep(a)) {
		if (a->failed)
			return false;
		a->linear = true;
		out->statistics.linear = 1;
		webs(a);
		if (!vir_alloc_liveness_sweep(a))
			return false;
	}
	vir_alloc_webs_loops_trim(a);
	out->statistics.pool = a->pool_count;
	a->save_point = opt && opt->shrink && !a->linear ? vir_alloc_save_point_find(a, opt) : VIR_BLOCK_NONE;
	if (a->failed)
		return false;
	if (a->save_point != VIR_BLOCK_NONE)
		for (uint32_t b = 0; b < nb; b++)
			ax.inp[b] = !vir_block_dominates(f, a->save_point, b);
	/* sweep 6 */
	for (uint32_t b = 0; b < nb; b++)
		ax.bmark[b] = 0;
	ax.nring[0] = ax.nring[1] = ax.nring[2] = ax.cring[0] = ax.cring[1] = ax.cring[2] = 0;
	for (uint32_t i = 0; i < f->constant_count; i++)
		ax.reg[i] = VIR_REGISTER_NONE;
	for (uint32_t r = 0; r < 256; r++)
		ax.cfirst[r] = VIR_BLOCK_NONE;
	memset(ax.slot, 0xff, (size_t)f->insn_count * 4);
	memset(ax.sdef, 0, f->insn_count);
	memset(ax.treg, VIR_REGISTER_NONE, f->insn_count);
	if (ax.mgen + (uint64_t)nb + 1 >= UINT32_MAX) {
		memset(ax.mark, 0, (size_t)VIR_MAX_INS * 4);	/* rare: the stamps wrapped */
		ax.mgen = 0;
	}
	a->move_base = ax.mgen + 1;
	memset(ax.regval, 0, sizeof(ax.regval));
	ax.fr[0] = a->register_mask[0] = rg->alloc[0];
	ax.fr[1] = a->register_mask[1] = rg->alloc[1];
	ax.mgen += nb + 1;
	/* the incoming registers of block 0's params are not free before their param */
	for (uint32_t i = f->blocks[0].first; i < f->blocks[0].first + f->blocks[0].insn_count && f->insns[i].op == VIR_OP_PARAM; i++) {
		vir_register_constraint fx;

		vir_alloc_fix_get(a, i, &fx);
		if (fx.result != VIR_REGISTER_NONE && !(ax.vfl[i] & VIR_ALLOC_FLAG_DEAD))
			a->parallel_avoid[VIR_REGISTER_CLASS(fx.result)] |= 1ull << VIR_REGISTER_NUMBER(fx.result);
	}
	for (uint32_t b = 0; b < nb; b++) {
		const vir_block *k = &f->blocks[b];
		uint64_t before[2] = { a->used_callee[0], a->used_callee[1] };

		a->current_loop = lp->inner[b];
		a->before_save_point = a->save_point != VIR_BLOCK_NONE && ax.inp[b];
		ax.boff[b] = a->operand_register_count;
		ax.boff[nb + 1 + b] = a->edit_count;
		vir_alloc_block_enter(a, b);
		for (uint32_t i = k->first + k->parameter_count; i < k->first + k->insn_count; i++) {
			if (f->insns[i].op == VIR_OP_PARAM) {
				vir_register_constraint fx;

				vir_alloc_fix_get(a, i, &fx);
				if (fx.result != VIR_REGISTER_NONE)
					a->parallel_avoid[VIR_REGISTER_CLASS(fx.result)] &= ~(1ull << VIR_REGISTER_NUMBER(fx.result));
			} else {
				a->parallel_avoid[0] = a->parallel_avoid[1] = 0;
			}
			if (!vir_alloc_insn_assign(a, b, i, i + 1 == k->first + k->insn_count))
				return false;
		}
		for (int c = 0; c < 2; c++) {
			uint64_t m = a->used_callee[c] & ~before[c];

			while (m) {
				uint32_t r = VIR_REGISTER_MAKE(c, vir_alloc_mask_lowest_bit(m));

				m &= m - 1;
				if (ax.cfirst[r] == VIR_BLOCK_NONE)
					ax.cfirst[r] = b;
			}
		}
		/* back edges from b */
		for (uint32_t s = ax.soff[b]; s < ax.soff[b + 1]; s++)
			if (ax.succ[s] <= b)
				conform(a, b, s - ax.soff[b], ax.succ[s]);
	}
	ax.boff[nb] = a->operand_register_count;
	ax.boff[2 * nb + 1] = a->edit_count;
	out->slot = ax.slot;
	out->spill_defined = ax.sdef;
	out->operand_registers = ax.opr;
	out->operand_register_count = a->operand_register_count;
	out->block_offsets = ax.boff;
	out->edit = ax.edit;
	out->edit_count = a->edit_count;
	out->edge = ax.edge;
	out->edge_count = a->edge_count;
	out->move = ax.move;
	out->move_count = a->move_count;
	out->edge_indices = ax.eidx;
	out->slot_offsets = ax.soff;
	out->slot_sizes = ax.ssz;
	out->spill_count = a->spill_count;
	out->callee[0] = a->used_callee[0];
	out->callee[1] = a->used_callee[1];
	out->callee_first = ax.cfirst;
	out->save_point = a->save_point;
	out->statistics.slots = a->spill_count;
	out->statistics.callee = (uint32_t)(__builtin_popcountll(a->used_callee[0]) + __builtin_popcountll(a->used_callee[1]));
	return true;
}
