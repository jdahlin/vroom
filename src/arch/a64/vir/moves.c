/* arm64 lowering of VIR: failing out, registers and constants, the allocator's moves, spills and reloads, labels and the edges of branches. */

#include "lowering-internal.h"

VPRIV __attribute__((noreturn, cold)) void a64_vir_fail(a64_vir_context *L, const char *why)
{
	L->context->vir_failure_reason = why;	/* (in ecx: L is not volatile across the jump) */
	_longjmp(L->context->bail_jump, 3);
}

VPRIV void *a64_vir_array_grow(void *p, uint32_t *cap, uint32_t n, size_t sz)
{
	if (n <= *cap)
		return p;
	*cap = n + n / 2 + 16;
	p = realloc(p, (size_t)*cap * sz);
	if (!p)
		abort();
	return p;
}

/* constant v into general register rd */
static void a64_vir_constant_materialize(a64_vir_context *L, uint32_t v, int rd)
{
	codegen_context *e = L->context;
	const vir_insn *k = &L->function->insns[v];

	switch (k->op) {
	case VIR_OP_ICONST:
		a64_record_emit_move_immediate(e, 1, rd, k->type == VIR_TYPE_I32 ? (int64_t)(uint32_t)k->b : a64_vir_constant_bits(k));
		return;
	case VIR_OP_FCONST:
		a64_record_emit_move_immediate(e, 1, rd, a64_vir_constant_bits(k));
		return;
	case VIR_OP_SYMADDR:
		a64_reference_address_emit(e, rd, k->a, a64_vir_constant_bits(&(vir_insn){ .b = k->b, .c = k->c }));
		return;
	case VIR_OP_SLOTADDR:
		a64_record_emit_immediate(e, A64_OP_ADDI, 1, rd, A64_REGISTER_FRAME, L->slot_offsets[k->a] + a64_vir_constant_bits(&(vir_insn){ .b = k->b, .c = k->c }));
		return;
	default:
		a64_vir_fail(L, VIRBUILD_FAILURE(constant_not_materializable));
	}
}

/* constant v into float register vd, through general register t */
static void a64_vir_float_constant_materialize(a64_vir_context *L, uint32_t v, int vd, int t)
{
	const vir_insn *k = &L->function->insns[v];
	int d = k->type == VIR_TYPE_F64;

	if (k->op != VIR_OP_FCONST)
		a64_vir_fail(L, VIRBUILD_FAILURE(float_constant_not_fconst));
	if (!k->b && !k->c) {
		a64_record_emit_word(L->context, a64_word_fmov_to_vector(d, vd, ZR));
		return;
	}
	a64_vir_constant_materialize(L, v, t);
	a64_record_emit_word(L->context, a64_word_fmov_to_vector(d, vd, t));
}

/* operand k as a general register: its register, else the constant or its slot in t */
VPRIV int a64_vir_operand_general_register(a64_vir_context *L, uint32_t k, int t)
{
	uint8_t r = L->operand_registers[k];
	uint32_t v = L->operand_values[k];

	if (r != VIR_REGISTER_NONE)
		return a64_vir_general_register(r);
	if (a64_vir_value_is_zero(L->function, v))
		return ZR;
	if (a64_vir_value_is_constant(L->function, v)) {
		a64_vir_constant_materialize(L, v, t);
		return t;
	}
	if (L->allocation->slot[v] != VIR_BLOCK_NONE) {
		a64_record_emit_memory(L->context, A64_OP_LD, a64_vir_slot_size_log2(L, L->allocation->slot[v]), 0, t, A64_REGISTER_FRAME, L->spill_offsets[L->allocation->slot[v]]);
		return t;
	}
	a64_vir_fail(L, VIRBUILD_FAILURE(operand_without_location));
}

VPRIV int a64_vir_operand_float_register(a64_vir_context *L, uint32_t k)
{
	if (L->operand_registers[k] == VIR_REGISTER_NONE)
		a64_vir_fail(L, VIRBUILD_FAILURE(float_operand_without_register));
	return a64_vir_vector_register(L->operand_registers[k]);
}

static void a64_vir_register_move(a64_vir_context *L, uint8_t dst, uint8_t src)
{
	if (dst == src)
		return;
	if (a64_vir_register_is_float(dst) != a64_vir_register_is_float(src))
		a64_vir_fail(L, VIRBUILD_FAILURE(move_between_register_classes));
	if (a64_vir_register_is_float(dst))
		a64_record_emit_word(L->context, 0x4ea01c00u | (unsigned)a64_vir_vector_register(src) << 16 | (unsigned)a64_vir_vector_register(src) << 5 | (unsigned)a64_vir_vector_register(dst));	/* mov v.16b, v.16b (a vector's upper half too) */
	else
		a64_record_emit_move(L->context, 1, a64_vir_general_register(dst), a64_vir_general_register(src));
}

/* a vector register and its 16-byte spill slot: the slot's address in t, then ldr/str q */
static void a64_vir_vector_slot_access(a64_vir_context *L, bool load, int v, uint32_t s, int t)
{
	a64_record_emit_immediate(L->context, A64_OP_ADDI, 1, t, A64_REGISTER_FRAME, L->spill_offsets[s]);
	a64_record_emit_word(L->context, (load ? 0x3dc00000u : 0x3d800000u) | (unsigned)t << 5 | (unsigned)v);
}

/* register dst <- spill slot s, through general register t for a float */
static void a64_vir_register_reload(a64_vir_context *L, uint8_t dst, uint32_t s, int t)
{
	int sz = a64_vir_slot_size_log2(L, s);

	if (L->allocation->slot_sizes[s] == 16) {
		a64_vir_vector_slot_access(L, true, a64_vir_vector_register(dst), s, t);
	} else if (a64_vir_register_is_float(dst)) {
		a64_record_emit_memory(L->context, A64_OP_LDF, sz, 0, a64_vir_vector_register(dst), A64_REGISTER_FRAME, L->spill_offsets[s]);
	} else {
		a64_record_emit_memory(L->context, A64_OP_LD, sz, 0, a64_vir_general_register(dst), A64_REGISTER_FRAME, L->spill_offsets[s]);
	}
}

VPRIV void a64_vir_register_spill(a64_vir_context *L, uint8_t src, uint32_t s, int t)
{
	int sz = a64_vir_slot_size_log2(L, s);

	if (L->allocation->slot_sizes[s] == 16) {
		a64_vir_vector_slot_access(L, false, a64_vir_vector_register(src), s, t);
	} else if (a64_vir_register_is_float(src)) {
		a64_record_emit_memory(L->context, A64_OP_STF, sz, 0, a64_vir_vector_register(src), A64_REGISTER_FRAME, L->spill_offsets[s]);
	} else {
		a64_record_emit_memory(L->context, A64_OP_ST, sz, 0, a64_vir_general_register(src), A64_REGISTER_FRAME, L->spill_offsets[s]);
	}
}

static void a64_vir_register_rematerialize(a64_vir_context *L, uint8_t dst, uint32_t v, int t)
{
	if (a64_vir_register_is_float(dst))
		a64_vir_float_constant_materialize(L, v, a64_vir_vector_register(dst), t);
	else
		a64_vir_constant_materialize(L, v, a64_vir_general_register(dst));
}

/* VCC_VL_PROBE=MASK (measuring): a nop after every allocator edit of the kinds in MASK, so
 * that instructions retired minus an unprobed run's is how often that kind executes.
 * Bits: 0 reload, 1 remat, 2 move, 3 store (edits before an instruction); 4 remat, 5 store,
 * 6 reload, 7 move (on an edge).  An upper bound: the nop also stops the record folds. */
static int a64_vir_probe_mask_get(void)
{
	static int m = -1;

	if (m < 0)
		m = getenv("VCC_VL_PROBE") ? atoi(getenv("VCC_VL_PROBE")) : 0;
	return m;
}

#define A64_VIR_PROBE(bit) do { if (a64_vir_probe_mask_get() >> (bit) & 1) a64_record_emit_word(L->context, 0xd503201f); } while (0)

/* one allocator edit, before the instruction it is positioned at */
VPRIV void vl_edit(a64_vir_context *L, const vir_edit *x)
{
	const vir_alloc_out *o = L->allocation;

	L->edit_next = (uint32_t)(x - o->edit) + 1;	/* (the next edit: what the instruction's folds look past) */
	switch (x->kind) {
	case VIR_EDIT_RELOAD:
		a64_vir_register_reload(L, x->dst, o->slot[x->value], R_X16); A64_VIR_PROBE(0);
		break;
	case VIR_EDIT_REMATERIALIZE:
		a64_vir_register_rematerialize(L, x->dst, x->value, R_X16); A64_VIR_PROBE(1);
		break;
	case VIR_EDIT_MOVE:
		a64_vir_register_move(L, x->dst, (uint8_t)x->src); A64_VIR_PROBE(2);
		break;
	case VIR_EDIT_STORE:
		a64_vir_register_spill(L, x->src, o->slot[x->value], R_X16); A64_VIR_PROBE(3);
		break;
	default:
		a64_vir_fail(L, VIRBUILD_FAILURE(swapped_operands));
	}
}

/* edge g's sequenced parallel move */
VPRIV void a64_vir_edge_moves_emit(a64_vir_context *L, uint32_t g)
{
	const vir_alloc_out *o = L->allocation;
	const vir_alloc_edge *ed = &o->edge[g];
	int t = R_X16;

	for (uint32_t m = 0; m < ed->count; m++) {
		const vir_edge_move *mv = &o->move[ed->first + m];

		if ((!(mv->dst & VIR_LOCATION_SLOT) && mv->dst == VIR_REGISTER_SCRATCH0) || (mv->kind != VIR_EDIT_REMATERIALIZE && !(mv->src & VIR_LOCATION_SLOT) && mv->src == VIR_REGISTER_SCRATCH0))
			t = R_X17;
	}
	for (uint32_t m = 0; m < ed->count; m++) {
		const vir_edge_move *mv = &o->move[ed->first + m];
		bool ds = mv->dst & VIR_LOCATION_SLOT, ss = mv->src & VIR_LOCATION_SLOT;
		uint32_t dn = mv->dst & ~VIR_LOCATION_SLOT, sn = mv->src & ~VIR_LOCATION_SLOT;

		if (t == R_X17 && (ds || ss || mv->kind == VIR_EDIT_REMATERIALIZE))
			L->frame_is_at_risk = true;
		switch (mv->kind) {
		case VIR_EDIT_REMATERIALIZE:
			if (ds) {
				a64_vir_constant_materialize(L, mv->value, t);
				a64_record_emit_memory(L->context, A64_OP_ST, a64_vir_slot_size_log2(L, dn), 0, t, A64_REGISTER_FRAME, L->spill_offsets[dn]);
			} else {
				a64_vir_register_rematerialize(L, (uint8_t)dn, mv->value, t);
			}
			A64_VIR_PROBE(4);
			break;
		case VIR_EDIT_MOVE:
			if (ds && ss && L->allocation->slot_sizes[sn] == 16) {	/* (through v31: no move of this edge may hold it) */
				for (uint32_t k = 0; k < ed->count; k++) {
					const vir_edge_move *o2 = &o->move[ed->first + k];

					if ((!(o2->dst & VIR_LOCATION_SLOT) && o2->dst == VIR_REGISTER_SCRATCH1) || (o2->kind != VIR_EDIT_REMATERIALIZE && !(o2->src & VIR_LOCATION_SLOT) && o2->src == VIR_REGISTER_SCRATCH1))
						a64_vir_fail(L, VIRBUILD_FAILURE(vector_spill_move_scratch_taken));
				}
				a64_vir_vector_slot_access(L, true, 31, sn, t);
				a64_vir_vector_slot_access(L, false, 31, dn, t);
			} else if (ds && ss) {
				a64_record_emit_memory(L->context, A64_OP_LD, a64_vir_slot_size_log2(L, sn), 0, t, A64_REGISTER_FRAME, L->spill_offsets[sn]);
				a64_record_emit_memory(L->context, A64_OP_ST, a64_vir_slot_size_log2(L, dn), 0, t, A64_REGISTER_FRAME, L->spill_offsets[dn]);
			} else if (ds) {
				a64_vir_register_spill(L, (uint8_t)sn, dn, t); A64_VIR_PROBE(5);
			} else if (ss) {
				a64_vir_register_reload(L, (uint8_t)dn, sn, t); A64_VIR_PROBE(6);
			} else {
				a64_vir_register_move(L, (uint8_t)dn, (uint8_t)sn); A64_VIR_PROBE(7);
			}
			break;
		case VIR_EDIT_EXCHANGE:
			if (ds || ss || a64_vir_register_is_float((uint8_t)dn) != a64_vir_register_is_float((uint8_t)sn))
				a64_vir_fail(L, VIRBUILD_FAILURE(exchange_with_slot));
			if (a64_vir_register_is_float((uint8_t)dn)) {
				unsigned a = (unsigned)a64_vir_vector_register((uint8_t)dn), b = (unsigned)a64_vir_vector_register((uint8_t)sn);

				a64_record_emit_word(L->context, 0x6e201c00u | b << 16 | a << 5 | a);	/* eor a.16b, a.16b, b.16b */
				a64_record_emit_word(L->context, 0x6e201c00u | a << 16 | b << 5 | b);
				a64_record_emit_word(L->context, 0x6e201c00u | b << 16 | a << 5 | a);
			} else {
				int a = a64_vir_general_register((uint8_t)dn), b = a64_vir_general_register((uint8_t)sn);

				a64_record_emit_three_registers(L->context, A64_OP_EOR, 1, a, a, b);
				a64_record_emit_three_registers(L->context, A64_OP_EOR, 1, b, b, a);
				a64_record_emit_three_registers(L->context, A64_OP_EOR, 1, a, a, b);
			}
			break;
		default:
			a64_vir_fail(L, VIRBUILD_FAILURE(unknown_edge_move));
		}
	}
}

/* Block t's label for the code being emitted.  With a save point (shrink-wrapping), code
 * behind the prologue that goes to a block in front of it (a return: a64_vir_blocks_order saw to that)
 * gets a second copy of the block, with the epilogue (a64_vir_thread_state_instance.lbf, emitted last by a64_vir_function_body_lower). */
VPRIV uint32_t a64_vir_block_label(a64_vir_context *L, uint32_t t)
{
	if (L->save_point != VIR_BLOCK_NONE && !L->is_before_save_point && !vir_block_dominates(L->function, L->save_point, t)) {
		if (!a64_vir_thread_state_instance.epilogue_labels[t])
			a64_vir_thread_state_instance.epilogue_labels[t] = a64_label_new(L->context);
		return a64_vir_thread_state_instance.epilogue_labels[t];
	}
	return L->labels[t];
}

/* a block every branch goes past (a64_vir_block_chase): not emitted */
VPRIV bool a64_vir_block_is_skipped(a64_vir_context *L, uint32_t b) { return b && a64_vir_block_chase(L, b) != b; }

static uint32_t a64_vir_block_next(a64_vir_context *L, uint32_t b)	/* the block emitted after b */
{
	uint32_t p = L->order_positions[b], nb = L->function->block_count;

	while (++p < nb && a64_vir_block_is_skipped(L, L->order[p]))
		;
	return p < nb ? L->order[p] : nb;
}

/* the block the code being emitted falls through to (none: UINT32_MAX) */
VPRIV uint32_t a64_vir_block_fallthrough(a64_vir_context *L) { return L->fallok ? a64_vir_block_next(L, L->at) : UINT32_MAX; }

/* the label a branch from block b to its successor k (block to) takes: the block's, or a
 * new one whose moves follow the block */
VPRIV uint32_t a64_vir_successor_label(a64_vir_context *L, uint32_t b, uint32_t k, uint32_t to)
{
	const vir_alloc_out *o = L->allocation;
	uint32_t g = o->edge_indices[o->slot_offsets[b] + k];

	if (g == VIR_BLOCK_NONE || !o->edge[g].count)
		return a64_vir_block_label(L, a64_vir_block_chase(L, to));
	if (L->edge_branch_count == sizeof L->edge_branches / sizeof L->edge_branches[0])
		a64_vir_fail(L, VIRBUILD_FAILURE(too_many_edge_moves));
	L->edge_branches[L->edge_branch_count].l = a64_label_new(L->context);
	L->edge_branches[L->edge_branch_count].e = g;
	L->edge_branches[L->edge_branch_count].to = to;
	return L->edge_branches[L->edge_branch_count++].l;
}

VPRIV bool a64_vir_edge_is_plain(a64_vir_context *L, uint32_t b, uint32_t k)	/* the edge has no moves */
{
	uint32_t g = L->allocation->edge_indices[L->allocation->slot_offsets[b] + k];

	return g == VIR_BLOCK_NONE || !L->allocation->edge[g].count;
}
