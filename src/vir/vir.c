/*
 * vir.c: VIR's construction and analyses: the thread's reservations, building a function, operand
 * iteration, the CFG (vir_cfg_build), dominators, use counts, the canonical form (vir_canon) and the unit's
 * tables (SPEC-vir.md 2-3, 22).
 *
 * A function's arrays live in the thread's vir_context (vir_context_get: reserved once per thread, reused by
 * every function), so vir_function_begin invalidates the previous function on that thread: two functions of one
 * thread are never alive at once, and a vir_function is not passed to another thread.  Nothing is
 * allocated or cleared per function; constants are hash-consed in a table stamped with a generation
 * (22.3), which is what keeps vir_function_begin cheap.  Analyses are cached in the function with a validity
 * bit and recomputed by the getter only when something invalidated them; a pass that changes control
 * flow or operands must say so (vir_cfg_build's and vir_dominators_build's callers rely on it).  vir_canon is the form
 * every pass receives (the verifier rejects a builder-form function whose constants are not yet
 * in the constant region).
 * Running past a limit (VIR_MAX_INS, the
 * constant or extra tables) sets the thread's fail flag and the builder call returns 0; the caller
 * checks vir_is_failed() before using the function.
 */
#include "vir-internal.h"

/* ---------------- the thread's reservations (22.2) ---------------- */

static _Thread_local vir_context *vir_context_thread;

vir_context *vir_context_get(void)
{
	vir_context *c = vir_context_thread;

	if (c)
		return c;
	c = calloc(1, sizeof(*c));
	if (!c)
		abort();
	for (int i = 0; i < 2; i++) {
		c->insns[i] = vreserve((size_t)VIR_MAX_INS * sizeof(vir_insn), MAP_NORESERVE, "vir: ins");
		c->blocks[i] = vreserve((size_t)VIR_MAX_BLK * sizeof(vir_block), MAP_NORESERVE, "vir: blk");
		c->locations[i] = vreserve((size_t)VIR_MAX_LOC * sizeof(vir_location), MAP_NORESERVE, "vir: loc");
	}
	c->slots = vreserve((size_t)VIR_MAX_SLOT * sizeof(vir_slot), MAP_NORESERVE, "vir: slot");
	c->extra_words = vreserve((size_t)VIR_MAX_EXTRA * 4, MAP_NORESERVE, "vir: extra");
	c->scratch = vreserve((size_t)VIR_MAX_INS * 4, MAP_NORESERVE, "vir: tmp");
	c->auxiliary = vreserve((size_t)VIR_MAX_INS * 4, MAP_NORESERVE, "vir: aux");
	c->ctab = vreserve(((size_t)1 << VIR_CONSTANT_TABLE_BITS) * sizeof(*c->ctab), MAP_NORESERVE, "vir: constants");
	for (int i = 0; i < 6; i++)
		c->bs[i] = vreserve((size_t)VIR_MAX_BLK * 4, MAP_NORESERVE, "vir: block scratch");
	c->sc = vreserve((size_t)VIR_MAX_EXTRA * 4, MAP_NORESERVE, "vir: successors");
	vir_context_thread = c;
	return c;
}

bool vir_is_failed(void) { return vir_context_thread && vir_context_thread->fail; }

/* a new generation: every table entry written before is empty (22.3) */
VPRIV void vir_constant_table_reset(vir_context *c)
{
	if (++c->gen == 0) {
		memset(c->ctab, 0, ((size_t)1 << VIR_CONSTANT_TABLE_BITS) * sizeof(*c->ctab));
		c->gen = 1;
	}
	c->ncon = 0;
}

static inline uint32_t vir_constant_hash(uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	uint64_t h = ((uint64_t)op << 8 | ty) * 0x9e3779b97f4a7c15ull;

	h = (h ^ a) * 0xff51afd7ed558ccdull;
	h = (h ^ b) * 0xc4ceb9fe1a85ec53ull;
	h = (h ^ c) * 0x9e3779b97f4a7c15ull;
	return (uint32_t)(h >> 40) & (((uint32_t)1 << VIR_CONSTANT_TABLE_BITS) - 1);
}

static inline bool vir_constant_matches(const vir_insn *x, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	return x->op == op && x->type == ty && x->a == a && x->b == b && x->c == c;
}

/* the entry for a key: its value, or 0 and *slot at the empty entry where it goes */
VPRIV uint32_t vir_constant_find(vir_context *x, const vir_function *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c,
		      uint32_t *slot)
{
	uint32_t m = ((uint32_t)1 << VIR_CONSTANT_TABLE_BITS) - 1, h = vir_constant_hash(op, ty, a, b, c);

	while (x->ctab[h].gen == x->gen) {
		uint32_t v = x->ctab[h].val;

		if (vir_constant_matches(&f->insns[v], op, ty, a, b, c))
			return v;
		h = (h + 1) & m;
	}
	*slot = h;
	return 0;
}

/* the table of the constants [1, nconst) of f (after canon, a load, or the verifier) */
VPRIV void vir_constant_table_rebuild(vir_function *f)
{
	vir_context *x = vir_context_get();

	vir_constant_table_reset(x);
	for (uint32_t j = 1; j < f->constant_count && j < f->insn_count; j++) {
		const vir_insn *c = &f->insns[j];
		uint32_t slot = 0;

		if (vir_opcode_is_constant(c->op) && !vir_constant_find(x, f, c->op, c->type, c->a, c->b, c->c, &slot) && x->ncon < VIR_MAX_CONST) {
			x->ctab[slot].gen = x->gen;
			x->ctab[slot].val = j;
			x->ncon++;
		}
	}
}

/* ---------------- construction ---------------- */

void vir_function_begin(vir_function *f, uint32_t target, uint32_t sym, uint32_t fl)
{
	vir_context *c = vir_context_get();

	memset(f, 0, sizeof(*f));
	c->fail = false;
	c->current = 0;
	c->block_current = c->location_current = 0;
	c->cfg_mark = c->cfg_end = 0;
	vir_constant_table_reset(c);
	f->version = 1;
	f->target = target;
	f->symbol = sym;
	f->flags = fl;
	f->insns = c->insns[0];
	f->blocks = c->blocks[0];
	f->slot = c->slots;
	f->extra = c->extra_words;
	f->locations = c->locations[0];
	f->scratch = c->scratch;
	memset(&f->insns[0], 0, sizeof(vir_insn));	/* index 0: a nop */
	f->insn_count = 1;
	f->extra[0] = 0;	/* offset 0 means "none" */
	f->extra_count = 1;
}

static inline bool vir_block_is_open(const vir_function *f) { return f->block_count != 0; }

static uint32_t vir_insn_push(vir_function *f, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c)
{
	vir_context *x = vir_context_get();
	vir_insn *p;

	if (f->insn_count >= VIR_MAX_INS) {
		x->fail = true;
		return 0;
	}
	p = &f->insns[f->insn_count];
	p->op = (uint8_t)op;
	p->type = (uint8_t)ty;
	p->flags = (uint16_t)fl;
	p->a = a;
	p->b = b;
	p->c = c;
	if (vir_block_is_open(f))
		f->blocks[f->block_count - 1].insn_count++;
	vir_analyses_invalidate(f);
	return f->insn_count++;
}

uint32_t vir_constant_raw_add(vir_function *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	return vir_insn_push(f, op, ty, 0, a, b, c);
}

uint32_t vir_constant_add(vir_function *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	vir_context *x = vir_context_get();
	uint32_t slot = 0, v = vir_constant_find(x, f, op, ty, a, b, c, &slot);

	if (v)
		return v;
	if (x->ncon >= VIR_MAX_CONST) {
		x->fail = true;
		return 0;
	}
	v = vir_insn_push(f, op, ty, 0, a, b, c);
	if (!v)
		return 0;
	x->ctab[slot].gen = x->gen;
	x->ctab[slot].val = v;
	x->ncon++;
	return v;
}

uint32_t vir_block_add(vir_function *f, uint16_t fl)
{
	vir_context *x = vir_context_get();
	vir_block *b;

	if (f->block_count >= VIR_MAX_BLK) {
		x->fail = true;
		return VIR_BLOCK_NONE;
	}
	b = &f->blocks[f->block_count];
	memset(b, 0, sizeof(*b));
	b->first = f->insn_count;
	b->flags = fl;
	b->immediate_dominator = VIR_BLOCK_NONE;
	vir_analyses_invalidate(f);
	return f->block_count++;
}

uint32_t vir_insn_add(vir_function *f, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c)
{
	vir_block *k;
	uint32_t v;

	if (!f->block_count) {
		vir_context_get()->fail = true;
		return 0;
	}
	k = &f->blocks[f->block_count - 1];
	if (op == VIR_OP_BPARAM && k->parameter_count == k->insn_count) {
		if (k->parameter_count >= VIR_MAX_BPARAM) {	/* (the count would wrap: a br's list has its length) */
			vir_context_get()->fail = true;
			return 0;
		}
		k->parameter_count++;
	}
	v = vir_insn_push(f, op, ty, fl, a, b, c);
	if (!v && op == VIR_OP_BPARAM && k->parameter_count > k->insn_count)
		k->parameter_count--;
	return v;
}

uint32_t vir_slot_add(vir_function *f, uint64_t size, uint32_t align, uint32_t fl, int64_t hint)
{
	vir_slot *s;

	if (f->slot_count >= VIR_MAX_SLOT) {
		vir_context_get()->fail = true;
		return VIR_BLOCK_NONE;
	}
	s = &f->slot[f->slot_count];
	s->size = size;
	s->align = align;
	s->flags = fl;
	s->hint = hint;
	return f->slot_count++;
}

uint32_t vir_extra_add(vir_function *f, const uint32_t *w, uint32_t n)
{
	uint32_t off = f->extra_count;

	if (n > VIR_MAX_EXTRA - off) {
		vir_context_get()->fail = true;
		return 0;
	}
	if (n)
		memcpy(&f->extra[off], w, (size_t)n * 4);
	f->extra_count += n;
	return off;
}

void vir_location_add(vir_function *f, uint32_t loc)
{
	if (f->location_count && f->locations[f->location_count - 1].source_location == loc)
		return;
	if (f->location_count >= VIR_MAX_LOC) {
		vir_context_get()->fail = true;
		return;
	}
	f->locations[f->location_count].first = f->insn_count;
	f->locations[f->location_count].source_location = loc;
	f->location_count++;
}

/* ---------------- operand and block-reference iteration ---------------- */

static inline bool vir_operand_kind_is_value(uint32_t k)
{
	return (k >= VIR_OPERAND_KIND_T && k <= VIR_OPERAND_KIND_BITC) || k == VIR_OPERAND_KIND_CONSTV;
}

/* per opcode: the fields that hold values (low three bits) and blocks (the next three) */
static uint8_t vir_field_masks[VIR_OP_TARGET_FIRST];

__attribute__((constructor)) static void vir_field_masks_init(void)
{
	for (uint32_t op = 0; op < VIR_OP_TARGET_FIRST; op++)
		for (int k = 0; k < 3; k++)
			vir_field_masks[op] |= (uint8_t)((vir_operand_kind_is_value(vir_opcode_table[op].operand_kinds[k]) ? 1u : 0) << k |
					     (vir_opcode_table[op].operand_kinds[k] == VIR_OPERAND_KIND_BLK ? 8u : 0) << k);
}

void vir_operand_iterator_init(vir_operand_iterator *it, vir_function *f, vir_insn *in, bool blocks)
{
	const vir_opcode_info *oi = &vir_opcode_table[in->op];
	uint32_t op = in->op, ne = f->extra_count;

	*it = (vir_operand_iterator){ .function = f, .insn = in, .stride = 1 };
	if (op >= VIR_OP_TARGET_FIRST || !oi->name)
		return;
	it->field_mask = (blocks ? vir_field_masks[op] >> 3 : vir_field_masks[op]) & 7;
	/* extra lists: n words at base (each a pointer-sized group, stride apart) */
	if (!blocks) {
		if ((op == VIR_OP_CALL || op == VIR_OP_RET) && in->c < ne) {
			uint32_t n = f->extra[in->c];

			if ((uint64_t)in->c + 1 + n <= ne) {
				it->base = in->c + 1;
				it->count = n;
			}
		} else if (op == VIR_OP_BR) {
			uint32_t n = in->a < f->block_count ? f->blocks[in->a].parameter_count : 0;

			if ((uint64_t)in->c + n <= ne) {
				it->base = in->c;
				it->count = n;
			}
		} else if ((op == VIR_OP_ASM || op == VIR_OP_ASMGOTO) && (uint64_t)in->b + 2 <= ne) {
			uint32_t no = f->extra[in->b], ni = f->extra[in->b + 1];

			if ((uint64_t)in->b + 2 + no + 2 * (uint64_t)ni <= ne) {
				it->base = in->b + 2 + no + 1;	/* the first input's value */
				it->count = ni;
				it->stride = 2;
			}
		}
	} else if (op == VIR_OP_SWITCH && in->c < ne) {
		uint32_t n = f->extra[in->c], ty = in->a < f->insn_count ? f->insns[in->a].type : 0;
		uint32_t st = ty == VIR_TYPE_I32 ? 3 : ty == VIR_TYPE_I64 ? 5 : 0;

		if (st && (uint64_t)in->c + 1 + (uint64_t)n * st <= ne) {
			it->base = in->c + 1 + st - 1;
			it->count = n;
			it->stride = st;
		}
	} else if (op == VIR_OP_IBR && in->c < ne) {
		uint32_t n = f->extra[in->c];

		if ((uint64_t)in->c + 1 + n <= ne) {
			it->base = in->c + 1;
			it->count = n;
		}
	} else if (op == VIR_OP_ASMGOTO && in->c < ne) {
		uint32_t n = f->extra[in->c];

		if ((uint64_t)in->c + 2 + n <= ne) {
			it->base = in->c + 1;
			it->count = n + 1;	/* the fallthrough block, then the labels */
		}
	}
}

uint32_t *vir_operand_iterator_next(vir_operand_iterator *it)
{
	while (it->field_index < 3) {
		uint32_t k = it->field_index++;

		if (it->field_mask >> k & 1) {
			it->pos = k;
			return k == 0 ? &it->insn->a : k == 1 ? &it->insn->b : &it->insn->c;
		}
	}
	if (it->index < it->count) {
		it->pos = 3;
		return &it->function->extra[it->base + (it->index++) * it->stride];
	}
	return NULL;
}

/* ---------------- control-flow graph ---------------- */

/* index of the last real instruction of block b: what is in its range that is not a constant created inside it */
VPRIV uint32_t vir_block_terminator(const vir_function *f, uint32_t b)
{
	const vir_block *k = &f->blocks[b];

	for (uint32_t i = k->first + k->insn_count; i > k->first; i--)
		if (!vir_opcode_is_constant(f->insns[i - 1].op))
			return i - 1;
	return 0;
}

/* successor lists of every block (CSR: succ[off[b] .. off[b+1])); false if a target is out of range or a range is */
VPRIV bool vir_successors_build(vir_function *f, uint32_t *off, uint32_t *succ)
{
	uint32_t e = 0;

	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *k = &f->blocks[b];
		uint32_t t;

		off[b] = e;
		if ((uint64_t)k->first + k->insn_count > f->insn_count || !k->insn_count)
			continue;
		t = vir_block_terminator(f, b);
		if (t && vir_opcode_is_terminator(f->insns[t].op)) {
			vir_insn x = f->insns[t];
			vir_operand_iterator it;
			uint32_t *p;

			vir_operand_iterator_init(&it, f, &x, true);
			while ((p = vir_operand_iterator_next(&it))) {
				if (*p >= f->block_count || e >= VIR_MAX_EXTRA)
					return false;
				succ[e++] = *p;
			}
		}
	}
	off[f->block_count] = e;
	return true;
}

bool vir_cfg_build(vir_function *f)
{
	vir_context *x = vir_context_get();
	uint32_t *off = x->bs[0], *cnt = x->bs[1], *succ = x->sc, ne, n = f->block_count;

	if (f->valid & VIR_VALID_CFG)
		return true;
	if (x->cfg_end == f->extra_count && x->cfg_end > x->cfg_mark)
		f->extra_count = x->cfg_mark;	/* the lists of the last call: replaced */
	x->cfg_mark = x->cfg_end = f->extra_count;
	if (!vir_successors_build(f, off, succ))
		return false;
	ne = off[n];
	if (ne > VIR_MAX_EXTRA - f->extra_count)
		return false;
	memset(cnt, 0, (size_t)n * 4);
	for (uint32_t i = 0; i < ne; i++)
		cnt[succ[i]]++;
	for (uint32_t b = 0, p = f->extra_count; b < n; b++) {
		f->blocks[b].predecessor_first = p;
		f->blocks[b].predecessor_count = 0;
		p += cnt[b];
	}
	for (uint32_t b = 0; b < n; b++)
		for (uint32_t i = off[b]; i < off[b + 1]; i++) {
			vir_block *t = &f->blocks[succ[i]];

			f->extra[t->predecessor_first + t->predecessor_count++] = b;
		}
	f->extra_count += ne;
	x->cfg_end = f->extra_count;
	f->valid |= VIR_VALID_CFG;
	return true;
}

/* reverse postorder from block 0 into rpo (successors visited last to first, so that
 * the first successor comes first); returns the count of reachable blocks */
static uint32_t vir_blocks_reverse_postorder(const vir_function *f, const uint32_t *off, const uint32_t *succ, uint32_t *rpo, uint32_t *stk,
			  uint32_t *pos)
{
	uint32_t n = f->block_count, sp = 0, np = 0;

	if (!n)
		return 0;
	for (uint32_t b = 0; b < n; b++)
		pos[b] = VIR_BLOCK_NONE;
	/* pos[] doubles as the visited mark (1 on the stack) while the DFS runs */
	stk[sp++] = 0;
	pos[0] = off[1] - off[0];	/* successors left to visit */
	while (sp) {
		uint32_t b = stk[sp - 1];

		if (pos[b]) {
			uint32_t s = succ[off[b] + --pos[b]];

			if (pos[s] == VIR_BLOCK_NONE) {
				pos[s] = off[s + 1] - off[s];
				stk[sp++] = s;
			}
		} else {
			rpo[np++] = b;	/* postorder for now */
			pos[b] = 0;
			sp--;
		}
	}
	for (uint32_t i = 0; i < np / 2; i++) {
		uint32_t t = rpo[i];

		rpo[i] = rpo[np - 1 - i];
		rpo[np - 1 - i] = t;
	}
	for (uint32_t b = 0; b < n; b++)
		pos[b] = VIR_BLOCK_NONE;
	for (uint32_t i = 0; i < np; i++)
		pos[rpo[i]] = i;
	return np;
}

bool vir_dominators_build(vir_function *f)
{
	vir_context *x = vir_context_get();
	uint32_t n = f->block_count, np, *rpo = x->bs[2], *stk = x->bs[3], *rn = x->bs[4], *id = x->bs[5];
	uint32_t *off = x->bs[0], *succ = x->sc, *sz = x->bs[1], *nx = x->bs[3];
	bool ch = true, fresh = !(f->valid & VIR_VALID_CFG);

	if (f->valid & VIR_VALID_DOM)
		return vir_cfg_build(f);	/* vir_dominators_build has always left the predecessor lists behind; canon's renumbering did not */
	if (!n || !vir_cfg_build(f))
		return false;
	/* the successors must be in off/succ: vir_cfg_build left them unless it returned a cached result;
	 * stk and nx share bs[3] (the DFS is over) */
	if (!fresh && !vir_successors_build(f, off, succ))
		return false;
	np = vir_blocks_reverse_postorder(f, off, succ, rpo, stk, rn);
	for (uint32_t b = 0; b < n; b++)
		id[b] = VIR_BLOCK_NONE;
	id[0] = 0;
	while (ch) {	/* Cooper, Harvey and Kennedy */
		ch = false;
		for (uint32_t i = 1; i < np; i++) {
			uint32_t b = rpo[i], ni = VIR_BLOCK_NONE;
			const vir_block *k = &f->blocks[b];

			for (uint32_t j = 0; j < k->predecessor_count; j++) {
				uint32_t p = f->extra[k->predecessor_first + j];

				if (id[p] == VIR_BLOCK_NONE)
					continue;
				if (ni == VIR_BLOCK_NONE) {
					ni = p;
					continue;
				}
				{
					uint32_t a = p, c = ni;

					while (a != c) {
						while (rn[a] > rn[c])
							a = id[a];
						while (rn[c] > rn[a])
							c = id[c];
					}
					ni = a;
				}
			}
			if (id[b] != ni) {
				id[b] = ni;
				ch = true;
			}
		}
	}
	/* subtree sizes, then preorder numbers: a child's range is inside its parent's */
	for (uint32_t b = 0; b < n; b++)
		sz[b] = 1;
	for (uint32_t i = np; i-- > 1;)
		sz[id[rpo[i]]] += sz[rpo[i]];
	for (uint32_t b = 0; b < n; b++) {
		f->blocks[b].immediate_dominator = VIR_BLOCK_NONE;
		f->blocks[b].preorder = f->blocks[b].preorder_last = VIR_BLOCK_NONE;
	}
	f->blocks[0].immediate_dominator = 0;
	f->blocks[0].preorder = 0;
	f->blocks[0].preorder_last = sz[0] - 1;
	nx[0] = 1;
	for (uint32_t i = 1; i < np; i++) {
		uint32_t b = rpo[i], p = id[b];

		f->blocks[b].immediate_dominator = p;
		f->blocks[b].preorder = nx[p];
		nx[p] += sz[b];
		nx[b] = f->blocks[b].preorder + 1;
		f->blocks[b].preorder_last = f->blocks[b].preorder + sz[b] - 1;
	}
	f->valid |= VIR_VALID_DOM;
	return true;
}

void vir_uses_count(const vir_function *f, uint8_t *cnt)
{
	memset(cnt, 0, f->insn_count);
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *k = &f->blocks[b];

		for (uint32_t i = k->first; i < k->first + k->insn_count && i < f->insn_count; i++) {
			vir_insn x = f->insns[i];
			vir_operand_iterator it;
			uint32_t *p;

			if (vir_opcode_is_constant(x.op))
				continue;
			vir_operand_iterator_init(&it, (vir_function *)f, &x, false);
			while ((p = vir_operand_iterator_next(&it)))
				if (*p < f->insn_count && cnt[*p] != 255)
					cnt[*p]++;
		}
	}
}

/* ---------------- the canonical form (2.4, 22.5) ---------------- */

/* the source location of old instruction index i (the run that holds it), or 0xffffffff */
static uint32_t vir_location_at(const vir_location *l, uint32_t n, uint32_t i)
{
	uint32_t lo = 0, hi = n;

	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;

		if (l[m].first <= i)
			lo = m + 1;
		else
			hi = m;
	}
	return lo ? l[lo - 1].source_location : 0xffffffffu;
}

bool vir_canon(vir_function *f)
{
	vir_context *x = vir_context_get();
	uint32_t n = f->block_count, np, *rpo, *order, nord = 0, *tmp = f->scratch, *nb;
	vir_insn *dst;
	vir_block *nbk;
	vir_location *nl;
	uint32_t nc = 1, k;
	bool keep = true;

	vir_analyses_invalidate(f);	/* the order is read from this run's scratch (bs[2]), not from a cached result */
	if (!n || !f->insn_count || !vir_dominators_build(f))
		return false;
	rpo = x->bs[2];
	np = 0;
	for (uint32_t b = 0; b < n; b++)
		np += f->blocks[b].immediate_dominator != VIR_BLOCK_NONE;
	/* keep the array order when every block comes after its immediate dominator */
	for (uint32_t b = 1; b < n; b++)
		if (f->blocks[b].immediate_dominator != VIR_BLOCK_NONE && f->blocks[b].immediate_dominator > b)
			keep = false;
	order = x->auxiliary;	/* [0, np): the new order of blocks; it is not used as a value array until pass 2 */
	nb = x->bs[0];	/* old block id -> new, VIR_BLOCK_NONE if dropped (off[] is dead now) */
	for (uint32_t b = 0; b < n; b++)
		nb[b] = VIR_BLOCK_NONE;
	if (keep) {
		for (uint32_t b = 0; b < n; b++)
			if (f->blocks[b].immediate_dominator != VIR_BLOCK_NONE)
				order[nord++] = b;
	} else {
		for (uint32_t i = 0; i < np; i++)
			order[nord++] = rpo[i];
	}
	for (uint32_t i = 0; i < nord; i++)
		nb[order[i]] = i;
	/* pass 1: the constants in order of first use (duplicates unified), written to dst */
	dst = x->insns[x->current ^ 1];
	memset(&dst[0], 0, sizeof(vir_insn));
	memset(tmp, 0xff, (size_t)f->insn_count * 4);
	tmp[0] = 0;
	vir_constant_table_reset(x);
	{
		vir_function g = *f;	/* the table is searched in dst's numbering */

		g.insns = dst;
		for (uint32_t i = 0; i < nord; i++) {
			const vir_block *kb = &f->blocks[order[i]];

			if ((uint64_t)kb->first + kb->insn_count > f->insn_count)
				return false;
			for (uint32_t q = kb->first; q < kb->first + kb->insn_count; q++) {
				vir_insn y = f->insns[q];
				vir_operand_iterator it;
				uint32_t *p;

				if (y.op == VIR_OP_NOP || vir_opcode_is_constant(y.op))
					continue;
				vir_operand_iterator_init(&it, f, &y, false);
				while ((p = vir_operand_iterator_next(&it))) {
					uint32_t v = *p;

					if (v >= f->insn_count || tmp[v] != 0xffffffffu || !vir_opcode_is_constant(f->insns[v].op))
						continue;
					{
						vir_insn cc = f->insns[v];
						uint32_t slot = 0, h;

						if (cc.op == VIR_OP_LABELADDR) {
							if (cc.a >= n || nb[cc.a] == VIR_BLOCK_NONE)
								return false;
							cc.a = nb[cc.a];
						}
						h = vir_constant_find(x, &g, cc.op, cc.type, cc.a, cc.b, cc.c, &slot);
						if (!h) {
							if (nc >= VIR_MAX_CONST)
								return false;
							h = nc++;
							dst[h] = cc;
							dst[h].flags = 0;
							x->ctab[slot].gen = x->gen;
							x->ctab[slot].val = h;
							x->ncon++;
						}
						tmp[v] = h;
					}
				}
			}
		}
	}
	/* pass 2: the block instructions, renumbered; lists are rewritten in place */
	nbk = x->blocks[x->block_current ^ 1];
	nl = x->locations[x->location_current ^ 1];
	k = nc;
	{
		uint32_t nnl = 0;

		for (uint32_t i = 0; i < nord; i++) {
			const vir_block *kb = &f->blocks[order[i]];
			vir_block *o = &nbk[i];
			uint32_t first = k;

			*o = *kb;
			o->immediate_dominator = nb[kb->immediate_dominator];	/* the tree over the reachable blocks only changes names; pre and post stay */
			o->first = first;
			o->parameter_count = 0;
			o->predecessor_count = o->predecessor_first = 0;
			for (uint32_t q = kb->first; q < kb->first + kb->insn_count; q++) {
				vir_insn y = f->insns[q], z;
				vir_operand_iterator iv, ib;
				uint32_t *p;

				if (y.op == VIR_OP_NOP || vir_opcode_is_constant(y.op))
					continue;
				if (k >= VIR_MAX_INS)
					return false;
				z = y;
				vir_operand_iterator_init(&iv, f, &z, false);
				vir_operand_iterator_init(&ib, f, &z, true);
				while ((p = vir_operand_iterator_next(&iv))) {
					uint32_t v = *p;

					*p = v < f->insn_count && tmp[v] != 0xffffffffu ? tmp[v] : 0;
				}
				while ((p = vir_operand_iterator_next(&ib)))
					*p = *p < n && nb[*p] != VIR_BLOCK_NONE ? nb[*p] : 0;
				if (z.op == VIR_OP_BPARAM && o->parameter_count == k - first)
					o->parameter_count++;
				tmp[q] = k;
				dst[k] = z;
				if (f->location_count && nl) {
					uint32_t l = vir_location_at(f->locations, f->location_count, q);

					if (l != 0xffffffffu && (!nnl || nl[nnl - 1].source_location != l) && nnl < VIR_MAX_LOC) {
						nl[nnl].first = k;
						nl[nnl].source_location = l;
						nnl++;
					}
				}
				k++;
			}
			o->insn_count = k - first;
		}
		f->location_count = nnl;
	}
	/* a block (and the constants) may only mention what is renumbered: operands defined later are 0 */
	f->insns = dst;
	f->blocks = nbk;
	f->locations = nl;
	x->current ^= 1;
	x->block_current ^= 1;
	x->location_current ^= 1;
	f->insn_count = k;
	f->constant_count = nc;
	f->block_count = nord;
	vir_constant_table_rebuild(f);
	f->extra_count = x->cfg_mark;	/* the old predecessor lists */
	x->cfg_end = x->cfg_mark;
	f->valid = VIR_VALID_DOM;	/* renumbered above; the predecessor lists are built when asked for */
	return true;
}

/* ---------------- units ---------------- */

void vir_unit_init(vir_unit *u) { memset(u, 0, sizeof(*u)); }

VPRIV void *vir_unit_alloc(vir_unit *u, size_t n)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		abort();
	if (u->heap_size == u->heap_capacity) {
		u->heap_capacity = u->heap_capacity ? u->heap_capacity * 2 : 64;
		u->heap = realloc(u->heap, u->heap_capacity * sizeof(void *));
		if (!u->heap)
			abort();
	}
	u->heap[u->heap_size++] = p;
	return p;
}

VPRIV const char *vir_unit_string_copy(vir_unit *u, const char *s, size_t n)
{
	char *p = vir_unit_alloc(u, n + 1);

	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

#define VIR_UNIT_GROW(arr, n, cap) do { \
	if ((n) == (cap)) { \
		(cap) = (cap) ? (cap) * 2 : 16; \
		(arr) = realloc((arr), (size_t)(cap) * sizeof(*(arr))); \
		if (!(arr)) abort(); \
	} \
} while (0)

void vir_unit_free(vir_unit *u)
{
	for (uint32_t i = 0; i < u->function_count; i++) {
		free(u->functions[i].insns);
		free(u->functions[i].blocks);
		free(u->functions[i].slot);
		free(u->functions[i].extra);
		free(u->functions[i].locations);
	}
	for (uint32_t i = 0; i < u->heap_size; i++)
		free(u->heap[i]);
	free(u->heap);
	free(u->symbols);
	free(u->signatures);
	free(u->signature_words);
	free(u->asms);
	free(u->item);
	free(u->functions);
	free(u->function_symbols);
	memset(u, 0, sizeof(*u));
}

uint32_t vir_unit_symbol_intern(vir_unit *u, const char *name, size_t n)
{
	for (uint32_t i = 0; i < u->symbol_count; i++)
		if (strlen(u->symbols[i].name) == n && !memcmp(u->symbols[i].name, name, n))
			return i;
	VIR_UNIT_GROW(u->symbols, u->symbol_count, u->symbol_capacity);
	memset(&u->symbols[u->symbol_count], 0, sizeof(vir_unit_symbol));
	u->symbols[u->symbol_count].name = vir_unit_string_copy(u, name, n);
	u->symbols[u->symbol_count].kind = VIR_UNIT_ITEM_UNDECL;
	return u->symbol_count++;
}

uint32_t vir_unit_signature_words_intern(vir_unit *u, const uint32_t *w, uint32_t n)
{
	for (uint32_t i = 0; i < u->signature_count; i++)
		if (u->signatures[i].count == n && !memcmp(&u->signature_words[u->signatures[i].offset], w, (size_t)n * 4))
			return i;
	while (u->signature_word_count + n > u->signature_word_capacity) {
		u->signature_word_capacity = u->signature_word_capacity ? u->signature_word_capacity * 2 : 64;
		u->signature_words = realloc(u->signature_words, (size_t)u->signature_word_capacity * 4);
		if (!u->signature_words)
			abort();
	}
	VIR_UNIT_GROW(u->signatures, u->signature_count, u->signature_capacity);
	memcpy(&u->signature_words[u->signature_word_count], w, (size_t)n * 4);
	u->signatures[u->signature_count].offset = u->signature_word_count;
	u->signatures[u->signature_count].count = n;
	u->signature_word_count += n;
	return u->signature_count++;
}

static void *vir_array_duplicate(const void *p, size_t n)
{
	void *q = malloc(n ? n : 1);

	if (!q)
		abort();
	if (n)
		memcpy(q, p, n);
	return q;
}

static void vir_function_copy(vir_function *d, const vir_function *f)
{
	*d = *f;
	d->insns = vir_array_duplicate(f->insns, (size_t)f->insn_count * sizeof(vir_insn));
	d->blocks = vir_array_duplicate(f->blocks, (size_t)f->block_count * sizeof(vir_block));
	d->slot = vir_array_duplicate(f->slot, (size_t)f->slot_count * sizeof(vir_slot));
	d->extra = vir_array_duplicate(f->extra, (size_t)f->extra_count * 4);
	d->locations = vir_array_duplicate(f->locations, (size_t)f->location_count * sizeof(vir_location));
	d->scratch = NULL;
}

void vir_function_save(vir_unit *u, const vir_function *f, uint32_t sym)
{
	VIR_UNIT_GROW(u->functions, u->function_count, u->function_capacity);
	u->function_symbols = realloc(u->function_symbols, (size_t)u->function_capacity * sizeof(uint32_t));
	if (!u->function_symbols)
		abort();
	vir_function_copy(&u->functions[u->function_count], f);
	u->function_symbols[u->function_count] = sym;
	u->function_count++;
}

void vir_function_store(vir_unit *u, uint32_t i, const vir_function *f)
{
	vir_function *d = &u->functions[i];

	free(d->insns);
	free(d->blocks);
	free(d->slot);
	free(d->extra);
	free(d->locations);
	vir_function_copy(d, f);
}

void vir_function_load(vir_function *f, const vir_unit *u, uint32_t i)
{
	const vir_function *s = &u->functions[i];

	vir_function_begin(f, s->target, s->symbol, s->flags);
	f->version = s->version;
	f->signature = s->signature;
	memcpy(f->insns, s->insns, (size_t)s->insn_count * sizeof(vir_insn));
	memcpy(f->blocks, s->blocks, (size_t)s->block_count * sizeof(vir_block));
	memcpy(f->slot, s->slot, (size_t)s->slot_count * sizeof(vir_slot));
	memcpy(f->extra, s->extra, (size_t)s->extra_count * 4);
	memcpy(f->locations, s->locations, (size_t)s->location_count * sizeof(vir_location));
	f->insn_count = s->insn_count;
	f->constant_count = s->constant_count;
	f->block_count = s->block_count;
	f->slot_count = s->slot_count;
	f->extra_count = s->extra_count;
	f->location_count = s->location_count;
	vir_constant_table_rebuild(f);
}
