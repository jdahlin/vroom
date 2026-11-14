/*
 * layout.c: block placement shared by the lowerings: chase blocks that only jump, order blocks,
 * rotate tested loop headers below their bodies, and decide which entry tests to duplicate
 * (include/vir/layout.h).
 *
 * The order is the one the code is emitted in: loops stay contiguous, a loop whose header is only a
 * test is rotated so the back edge is the conditional branch, and `chase` skips empty blocks so that
 * a branch targets the real destination.  The module allocates nothing (the caller owns the arrays,
 * f->block_count entries each) and emits no CPU code: the lowering still emits its own instructions and edge
 * copies from this plan.  -fno-rotate-loops turns rotation off.  A lowering that follows the plan
 * without duplicating the test on entry edges when vir_layout_duplicate asks produces wrong
 * control flow.
 */
#include "vir-internal.h"
#include "vir/layout.h"

typedef struct vir_layout_state {
	const vir_function *function;
	const vir_loops *loops;
	const uint32_t *chase;
	uint32_t *order, *rotation;
} vir_layout_state;

static bool vir_layout_block_is_lone_branch(const vir_function *f, const vir_alloc_out *o, uint32_t t)
{
	const vir_block *k = &f->blocks[t];
	const vir_insn *in = &f->insns[k->first];
	/* The save point may have allocator edits even in a lone branch. */
	if (k->parameter_count || k->insn_count != 1 || in->op != VIR_OP_BR || (k->flags & VIR_BLOCK_FLAG_ADDR) ||
	    o->block_offsets[f->block_count + 1 + t] != o->block_offsets[f->block_count + 2 + t])
		return false;
	uint32_t g = o->edge_indices[o->slot_offsets[t]];
	return g == VIR_BLOCK_NONE || !o->edge[g].count;
}

/* The target of every block; in a cycle of lone brs the block the walk reaches twice stays */
void vir_layout_chases_compute(const vir_function *f, const vir_alloc_out *o, uint32_t *c, uint32_t *path)
{
	uint32_t nb = f->block_count;
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
			if (!vir_layout_block_is_lone_branch(f, o, t)) {
				r = t;
				break;
			}
			c[t] = busy;
			path[n++] = t;
			t = f->insns[f->blocks[t].first].a;
		}
		c[t] = c[t] == busy || c[t] == todo ? r : c[t];
		while (n)
			c[path[--n]] = r;
	}
}

static bool vir_layout_block_is_plain(const vir_function *f, uint32_t b,uint8_t term)
{
	const vir_block *k = &f->blocks[b];

	if ((k->flags & VIR_BLOCK_FLAG_ADDR) || k->insn_count - k->parameter_count > 8 || f->insns[k->first + k->insn_count - 1].op != term)
		return false;
	for (uint32_t i = k->first + k->parameter_count; i < k->first + k->insn_count; i++)
		switch (f->insns[i].op) {
		case VIR_OP_CALL: case VIR_OP_ASM: case VIR_OP_ASMGOTO: case VIR_OP_MEMCPY: case VIR_OP_MEMSET: case VIR_OP_SWITCH:
			return false;
		}
	return true;
}

static bool vir_layout_block_is_rotatable(const vir_function *f,uint32_t b) {return vir_layout_block_is_plain(f,b,VIR_OP_BRIF);}

static void vir_layout_order_range(vir_layout_state *L, uint32_t lo, uint32_t hi, uint32_t *n)
{
	const vir_function *f = L->function;

	for (uint32_t b = lo; b <= hi;) {
		uint32_t l = L->rotation[b], at = *n, last;
		const vir_insn *t;

		if (l == VIR_BLOCK_NONE || !b || L->loops->loops[l].end <= b || L->loops->loops[l].end > hi || !vir_layout_block_is_rotatable(f, b)) {
			L->rotation[b] = VIR_BLOCK_NONE;
			L->order[(*n)++] = b++;
			continue;
		}
		vir_layout_order_range(L, b + 1, L->loops->loops[l].end, n);
		for (last = *n; last > at && (L->order[last - 1] && L->chase[L->order[last - 1]] != L->order[last - 1]); last--)
			;
		t = last > at ? &f->insns[f->blocks[L->order[last - 1]].first + f->blocks[L->order[last - 1]].insn_count - 1] : NULL;
		if (t && t->op == VIR_OP_BR && L->chase[t->a] == b) {
			L->order[(*n)++] = b;
		} else {
			memmove(&L->order[at + 1], &L->order[at], (size_t)(*n - at) * sizeof *L->order);
			L->order[at] = b;
			L->rotation[b] = VIR_BLOCK_NONE;
			++*n;
		}
		b = L->loops->loops[l].end + 1;
	}
}


void vir_layout_order(const vir_function *f, const vir_loops *lp, const uint32_t *chase,
                      bool rotate, uint32_t *order, uint32_t *position, uint32_t *rotation)
{
	vir_layout_state L = { f, lp, chase, order, rotation };
	uint32_t n = 0;

	for (uint32_t b = 0; b < f->block_count; b++)
		rotation[b] = VIR_BLOCK_NONE;
	/* Bound recursion as in the original ARM lowering. */
	if (rotate && f->block_count < 60000)
		for (uint32_t l = 0; l < lp->count; l++) {
			uint32_t h = lp->loops[l].head;

			if (rotation[h] == VIR_BLOCK_NONE || lp->loops[rotation[h]].end < lp->loops[l].end)
				rotation[h] = l;
		}
	if (f->block_count)
		vir_layout_order_range(&L, 0, f->block_count - 1, &n);
	for (uint32_t p = 0; p < n; p++)
		position[order[p]] = p;
}

bool vir_layout_duplicate(const vir_function *f, const vir_loops *lp, const uint32_t *rotation,
                          uint32_t from, uint32_t to, uint32_t vir_parse_token_next, unsigned depth, bool optimize)
{
	if (to == vir_parse_token_next || to == from)
		return false;
	if (rotation[to] != VIR_BLOCK_NONE && depth < 8 && (from < to || from > lp->loops[rotation[to]].end))
		return true;
	const vir_block *k = &f->blocks[to];
	uint32_t n = k->insn_count - k->parameter_count;
	uint8_t op = f->insns[k->first + k->insn_count - 1].op;

	if (!(k->flags & VIR_BLOCK_FLAG_ADDR) && n == 1 && (op == VIR_OP_RET || (optimize && op == VIR_OP_BR && depth < 8)))
		return true;
	return optimize && to > from && depth < 2 && n <= 2 && !(f->blocks[from].flags & VIR_BLOCK_FLAG_COLD) && (vir_layout_block_is_rotatable(f, to) || vir_layout_block_is_plain(f,to,VIR_OP_BR));
}
