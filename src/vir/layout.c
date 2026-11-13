/*
 * layout.c: block placement shared by the lowerings: chase blocks that only jump, order blocks,
 * rotate tested loop headers below their bodies, and decide which entry tests to duplicate
 * (include/vir/layout.h).
 *
 * The order is the one the code is emitted in: loops stay contiguous, a loop whose header is only a
 * test is rotated so the back edge is the conditional branch, and `chase` skips empty blocks so that
 * a branch targets the real destination.  The module allocates nothing (the caller owns the arrays,
 * f->nblk entries each) and emits no CPU code: the lowering still emits its own instructions and edge
 * copies from this plan.  -fno-rotate-loops turns rotation off.  A lowering that follows the plan
 * without duplicating the test on entry edges when vir_layout_duplicate asks produces wrong
 * control flow.
 */
#include "virimpl.h"
#include "vir/layout.h"

typedef struct lay {
	const vir_fn *f;
	const vir_loops *lp;
	const uint32_t *chase;
	uint32_t *order, *rotation;
} lay;

static bool lay_lonebr(const vir_fn *f, const vir_alloc_out *o, uint32_t t)
{
	const vir_blk *k = &f->blk[t];
	const vir_ins *in = &f->ins[k->first];
	/* The save point may have allocator edits even in a lone branch. */
	if (k->nparam || k->n != 1 || in->op != VO_BR || (k->fl & VB_ADDR) ||
	    o->boff[f->nblk + 1 + t] != o->boff[f->nblk + 2 + t])
		return false;
	uint32_t g = o->eidx[o->soff[t]];
	return g == VIR_NOBLK || !o->edge[g].n;
}

/* The target of every block; in a cycle of lone brs the block the walk reaches twice stays */
void vir_layout_chases(const vir_fn *f, const vir_alloc_out *o, uint32_t *c, uint32_t *path)
{
	uint32_t nb = f->nblk;
	const uint32_t todo = UINT32_MAX, busy = UINT32_MAX - 1;

	for (uint32_t b = 0; b < nb; b++)
		c[b] = todo;
	for (uint32_t b = 0; b < nb; b++) {
		uint32_t t = b, n = 0, r;

		for (;;) {
			if (c[t] == busy) {
				r = t;
				break;
			}
			if (c[t] != todo) {
				r = c[t];
				break;
			}
			if (!lay_lonebr(f, o, t)) {
				r = t;
				break;
			}
			c[t] = busy;
			path[n++] = t;
			t = f->ins[f->blk[t].first].a;
		}
		c[t] = c[t] == busy || c[t] == todo ? r : c[t];
		while (n)
			c[path[--n]] = r;
	}
}

static bool lay_plain(const vir_fn *f, uint32_t b,uint8_t term)
{
	const vir_blk *k = &f->blk[b];

	if ((k->fl & VB_ADDR) || k->n - k->nparam > 8 || f->ins[k->first + k->n - 1].op != term)
		return false;
	for (uint32_t i = k->first + k->nparam; i < k->first + k->n; i++)
		switch (f->ins[i].op) {
		case VO_CALL: case VO_ASM: case VO_ASMGOTO: case VO_MEMCPY: case VO_MEMSET: case VO_SWITCH:
			return false;
		}
	return true;
}

static bool lay_rotok(const vir_fn *f,uint32_t b) {return lay_plain(f,b,VO_BRIF);}

static void lay_order_range(lay *L, uint32_t lo, uint32_t hi, uint32_t *n)
{
	const vir_fn *f = L->f;

	for (uint32_t b = lo; b <= hi;) {
		uint32_t l = L->rotation[b], at = *n, last;
		const vir_ins *t;

		if (l == VIR_NOBLK || !b || L->lp->l[l].end <= b || L->lp->l[l].end > hi || !lay_rotok(f, b)) {
			L->rotation[b] = VIR_NOBLK;
			L->order[(*n)++] = b++;
			continue;
		}
		lay_order_range(L, b + 1, L->lp->l[l].end, n);
		for (last = *n; last > at && (L->order[last - 1] && L->chase[L->order[last - 1]] != L->order[last - 1]); last--)
			;
		t = last > at ? &f->ins[f->blk[L->order[last - 1]].first + f->blk[L->order[last - 1]].n - 1] : NULL;
		if (t && t->op == VO_BR && L->chase[t->a] == b) {
			L->order[(*n)++] = b;
		} else {
			memmove(&L->order[at + 1], &L->order[at], (size_t)(*n - at) * sizeof *L->order);
			L->order[at] = b;
			L->rotation[b] = VIR_NOBLK;
			++*n;
		}
		b = L->lp->l[l].end + 1;
	}
}


void vir_layout_order(const vir_fn *f, const vir_loops *lp, const uint32_t *chase,
                      bool rotate, uint32_t *order, uint32_t *position, uint32_t *rotation)
{
	lay L = { f, lp, chase, order, rotation };
	uint32_t n = 0;

	for (uint32_t b = 0; b < f->nblk; b++)
		rotation[b] = VIR_NOBLK;
	/* Bound recursion as in the original ARM lowering. */
	if (rotate && f->nblk < 60000)
		for (uint32_t l = 0; l < lp->n; l++) {
			uint32_t h = lp->l[l].head;

			if (rotation[h] == VIR_NOBLK || lp->l[rotation[h]].end < lp->l[l].end)
				rotation[h] = l;
		}
	if (f->nblk)
		lay_order_range(&L, 0, f->nblk - 1, &n);
	for (uint32_t p = 0; p < n; p++)
		position[order[p]] = p;
}

bool vir_layout_duplicate(const vir_fn *f, const vir_loops *lp, const uint32_t *rotation,
                          uint32_t from, uint32_t to, uint32_t next, unsigned depth, bool optimize)
{
	if (to == next || to == from)
		return false;
	if (rotation[to] != VIR_NOBLK && depth < 8 && (from < to || from > lp->l[rotation[to]].end))
		return true;
	const vir_blk *k = &f->blk[to];
	uint32_t n = k->n - k->nparam;
	uint8_t op = f->ins[k->first + k->n - 1].op;

	if (!(k->fl & VB_ADDR) && n == 1 && (op == VO_RET || (optimize && op == VO_BR && depth < 8)))
		return true;
	return optimize && to > from && depth < 2 && n <= 2 && !(f->blk[from].fl & VB_COLD) && (lay_rotok(f, to) || lay_plain(f,to,VO_BR));
}
