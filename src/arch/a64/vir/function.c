/*
 * Allocated VIR to arm64 records (PLAN-vir.md V4, SPEC-vir.md 15.3): the
 * registers arm64 offers the allocator, the fixed operands of calls,
 * parameters and returns, then one pass over the blocks that writes the
 * emitter's records with the allocated registers, the edits and the edges'
 * moves.  finish_function then runs the record clean-ups, lays out the frame
 * and prints or encodes, as for any function.
 *
 *   integer: x0-x15 and x19-x28 (callee-saved); x16 is VIR_REGISTER_SCRATCH0 and the
 *            lowering's own scratch, x17 the printer's (large immediates),
 *            x18 the platform's, x29/x30 the frame
 *   float:   v0-v7 and v16-v30, caller-saved, and v8-v15, whose low 64 bits a
 *            call keeps: offered when no vector lives across a call (a64_vir_vectors_cross_call)
 *            and saved and restored here, not by the prologue; v31 is VIR_REGISTER_SCRATCH1
 *
 * Floating-point operations are encoded words (I_WORD), as in fp.c.  A
 * value in a spill slot or a VIR slot is addressed from FRAME, so the frame
 * layout stays finish_function's.  Edge moves of a branch that is not a `br`
 * go into a small block after the branch's own (a label, the moves, a jump).
 */

#include "lowering-internal.h"

VPRIV _Thread_local struct a64_vir_thread_state a64_vir_thread_state_instance;

/* The callbacks of the shared driver (codegen/vir/lowering-driver.h) */

static void vl_target_block_begin(void *ctx, uint32_t b)
{
	a64_vir_context *L = ctx;
	const vir_alloc_out *o = L->allocation;

	L->flag_condition = -1, L->zero_test_register = -1, L->test_bit = -1;
	L->chain_high = 0;
	L->folded_low = L->folded_high = 0;
	L->pointer_value = 0, L->index_value = 0, L->chain_consumer = VIR_BLOCK_NONE;
	L->edit_first = o->block_offsets[L->function->block_count + 1 + b], L->edit_end = o->block_offsets[L->function->block_count + 2 + b];
	L->edit_next = L->edit_first;
	L->edge_branch_count = 0;
	L->skip = 0;
}

/* the label defined by the layout loop: the prologue's nop, and what the code at it may do */
static void vl_target_block_enter(void *ctx, uint32_t b)
{
	a64_vir_context *L = ctx;

	if (b == L->save_point)
		put(L->context, I_NOP)->cond = NOP_PROLOG;
	L->is_before_save_point = L->save_point != VIR_BLOCK_NONE && !vir_block_dominates(L->function, L->save_point, b);
	L->at = b;
	L->fallok = true;
}

static void vl_target_instruction(void *ctx, uint32_t b, uint32_t i, const uint8_t *opr, uint32_t nopr, uint8_t res)
{
	a64_vir_context *L = ctx;
	codegen_context *e = L->context;

	(void)opr;	/* (the driver filled L->ov and L->opr) */
	L->operand_count = nopr;
	if (L->flag_condition >= 0 && L->flag_value != L->function->insns[i].a && !(i > L->folded_low && i < L->folded_high))
		L->flag_condition = -1;
	if (!e->is_reachable)
		deflabel_forced(e, newlabel(e));	/* (code after a trap: never reached) */
	a64_vir_insn_lower(L, b, i, res);
}

static void vl_target_result_spill(void *ctx, uint8_t res, uint32_t slot)
{
	a64_vir_register_spill(ctx, res, slot, R_X16);
}

static void vl_target_edit(void *ctx, const vir_edit *x)
{
	vl_edit(ctx, x);
}

static void vl_target_edge_moves(void *ctx, uint32_t g)
{
	a64_vir_edge_moves_emit(ctx, g);
}

static void vl_target_label(void *ctx, uint32_t label)
{
	deflabel_forced(((a64_vir_context *)ctx)->context, label);
}

/* the block's branches' edges with moves: a small block each after it (a label, the moves, a jump) */
static void vl_target_block_end(void *ctx, uint32_t b)
{
	a64_vir_context *L = ctx;

	if (L->context->is_reachable)
		a64_vir_fail(L, VIRBUILD_FAILURE(lowering_block_without_terminator));
	if (L->edge_branch_count) {	/* (a copy in a64_vir_goto_emit reuses L->tr) */
		uint32_t n = L->edge_branch_count;
		struct { uint32_t l, e, to; } *tr = __builtin_alloca(n * sizeof *tr);

		memcpy(tr, L->edge_branches, n * sizeof *tr);
		L->edge_branch_count = 0;
		bool fo = L->fallok;

		for (uint32_t t = 0; t < n; t++) {
			virbuild_lower_edge_block(&L->drv, tr[t].l, tr[t].e);
			L->fallok = fo && t == n - 1;
			a64_vir_goto_emit(L, b, a64_vir_block_chase(L, tr[t].to));
		}
		L->fallok = fo;
	}
	L->edge_branch_count = 0;
}

static void vl_target_body_begin(void *ctx)
{
	a64_vir_callee_saved_float_save(ctx, false, R_X16);
}

static void vl_target_after_blocks(void *ctx)
{
	a64_vir_context *L = ctx;

	for (uint32_t b = 0; b < L->function->block_count && L->save_point != VIR_BLOCK_NONE; b++)
		if (a64_vir_thread_state_instance.epilogue_labels[b]) {	/* a return in front of the save point, for the code behind it */
			deflabel_forced(L->context, a64_vir_thread_state_instance.epilogue_labels[b]);
			L->is_before_save_point = false;
			L->at = b;
			L->fallok = false;
			virbuild_lower_block(&L->drv, b);
		}
}

static void vl_target_fail(void *ctx, const char *why)
{
	a64_vir_fail(ctx, why);
}

static void vl_target_order_adjust(void *ctx);

static const virbuild_lowering_target vl_lowering_target = {
	.block_begin = vl_target_block_begin,
	.block_enter = vl_target_block_enter,
	.instruction = vl_target_instruction,
	.result_spill = vl_target_result_spill,
	.edit = vl_target_edit,
	.edge_moves = vl_target_edge_moves,
	.label = vl_target_label,
	.block_end = vl_target_block_end,
	.order_adjust = vl_target_order_adjust,
	.body_begin = vl_target_body_begin,
	.after_blocks = vl_target_after_blocks,
	.fail = vl_target_fail,
	/* unresolved differences from x86-64 (Q-VIRB-02) */
	.params_in_loop = false,
	.edits_at_block_end = false,
	.skip_multi_operands = true,
	.rotation_cached = true,
};

/*
 * The layout (L->ord): VIR's block order, except that a loop whose header is a test (a
 * `brif` after a few plain instructions: a while or for loop) has the header after its
 * last block when that block ends in a `br` to it.  The latch then falls into the test and
 * the test's branch goes back to the body: one branch a round instead of two.  The edges
 * into the loop get a copy of the test (a64_vir_goto_emit), not a jump to it: with the jump,
 * speedtest1 ran more instructions than without the rotation (short loops).  a64_vir_thread_state_instance.hl[b] is
 * the loop of a header placed so, else VIR_BLOCK_NONE.  VCC_VIR_ROT=0: VIR's order.  The driver
 * makes that layout; this puts the blocks in front of the save point first.
 */
static void vl_target_order_adjust(void *ctx)
{
	a64_vir_context *L = ctx;
	const vir_function *f = L->function;
	uint32_t n = f->block_count;

	if (L->save_point != VIR_BLOCK_NONE) {
		/* the blocks in front of the save point first, then it and what it dominates:
		 * the prologue stands between them (a64_vir_thread_state_instance.path: scratch here) */
		uint32_t np = 0, nq = 0, *q = a64_vir_thread_state_instance.path;

		for (uint32_t p = 0; p < n; p++)
			if (vir_block_dominates(f, L->save_point, L->order[p]))
				q[nq++] = L->order[p];
			else
				L->order[np++] = L->order[p];
		memcpy(&L->order[np], q, (size_t)nq * sizeof *q);
		if (a64_vir_block_is_skipped(L, L->save_point) || L->order[np] != L->save_point)
			L->save_point = VIR_BLOCK_NONE;	/* (never the case in dominator order; the order stays) */
		/* from behind the save point back in front of it only to a block that is a return */
		for (uint32_t p = np; p < n && L->save_point != VIR_BLOCK_NONE; p++) {
			vir_insn t = f->insns[f->blocks[L->order[p]].first + f->blocks[L->order[p]].insn_count - 1];
			vir_operand_iterator it;
			uint32_t *x;

			vir_operand_iterator_init(&it, (vir_function *)f, &t, true);
			while (L->save_point != VIR_BLOCK_NONE && (x = vir_operand_iterator_next(&it))) {
				const vir_block *k = &f->blocks[a64_vir_block_chase(L, *x)];

				if (!vir_block_dominates(f, L->save_point, a64_vir_block_chase(L, *x)) &&
				    ((k->flags & VIR_BLOCK_FLAG_ADDR) || k->insn_count - k->parameter_count != 1 || f->insns[k->first + k->insn_count - 1].op != VIR_OP_RET))
					L->save_point = VIR_BLOCK_NONE;
			}
		}
	}
	for (uint32_t p = 0; p < n; p++)
		L->order_positions[L->order[p]] = p;
}

VPRIV void a64_vir_function_body_lower(a64_vir_context *L)
{
	virbuild_lower_body(&L->drv);
}

/* does a vector live across a call?  (A call keeps only the low half of v8-v15: such a function
 * is not offered them.)  Liveness of the vector values alone, a bit each */
static bool a64_vir_vectors_cross_call(vir_function *f)
{
	uint32_t nv = 0, W, *ix;
	uint64_t *in, *out, *live;
	bool calls = false, hit = false;

	for (uint32_t b = 0; b < f->block_count; b++)
		for (uint32_t i = f->blocks[b].first; i < f->blocks[b].first + f->blocks[b].insn_count; i++) {
			nv += vir_type_is_vector(f->insns[i].type);
			calls |= f->insns[i].op == VIR_OP_CALL || ((f->insns[i].op == VIR_OP_MEMCPY || f->insns[i].op == VIR_OP_MEMSET) && a64_vir_insn_is_big_memory(f, &f->insns[i]));
		}
	if (!nv || !calls)
		return false;
	W = (nv + 63) / 64;
	ix = malloc((size_t)f->insn_count * sizeof *ix);
	in = calloc((size_t)(2 * f->block_count + 1) * W, sizeof *in);
	if (!ix || !in) {
		free(ix), free(in);
		return true;
	}
	out = in + (size_t)f->block_count * W, live = out + (size_t)f->block_count * W;
	nv = 0;
	for (uint32_t i = 0; i < f->insn_count; i++)
		ix[i] = vir_type_is_vector(f->insns[i].type) ? nv++ : UINT32_MAX;
	for (int pass = 0, ch = 1; ch || pass < 2; pass += !ch) {	/* to a fixed point, then once more to look at the calls */
		ch = 0;
		for (uint32_t b = f->block_count; b-- > 0;) {
			const vir_block *k = &f->blocks[b];
			vir_insn t = f->insns[k->first + k->insn_count - 1];
			vir_operand_iterator it;
			uint32_t *p;

			memset(live, 0, W * sizeof *live);
			vir_operand_iterator_init(&it, f, &t, true);
			while ((p = vir_operand_iterator_next(&it)))
				if (*p < f->block_count)
					for (uint32_t w = 0; w < W; w++)
						live[w] |= in[(size_t)*p * W + w];
			for (uint32_t w = 0; w < W; w++)
				if (out[(size_t)b * W + w] != live[w])
					out[(size_t)b * W + w] = live[w], ch = 1;
			for (uint32_t i = k->first + k->insn_count; i-- > k->first;) {
				vir_insn x = f->insns[i];

				if (ix[i] != UINT32_MAX)
					live[ix[i] / 64] &= ~(1ull << (ix[i] & 63));
				if (pass && (x.op == VIR_OP_CALL || x.op == VIR_OP_MEMCPY || x.op == VIR_OP_MEMSET))
					for (uint32_t w = 0; w < W; w++)
						hit |= live[w] != 0;
				vir_operand_iterator_init(&it, f, &x, false);
				while ((p = vir_operand_iterator_next(&it)))
					if (*p < f->insn_count && ix[*p] != UINT32_MAX)
						live[ix[*p] / 64] |= 1ull << (ix[*p] & 63);
			}
			for (uint32_t w = 0; w < W; w++)
				if (in[(size_t)b * W + w] != live[w])
					in[(size_t)b * W + w] = live[w], ch = 1;
		}
		if (pass && !ch)
			break;
	}
	free(ix), free(in);
	return hit;
}

/* lower V->f (allocated in V->ao) for function s; false: why in *why, nothing emitted */
VPRIV bool a64_vir_function_lower(codegen_context *e, uint32_t s, const char **why)
{
	virbuild_context *V = e->vir_builder;
	vir_function *f = &V->f;
	vir_target_registers regs;
	a64_vir_context L = { 0 };
	cc *c = e->compile_context;
	const ctype *ft = T(c, c->syms[s].ty);

	*why = NULL;
	if (e->unit->options->debug) {
		*why = VIRBUILD_FAILURE(debug_info);
		return false;
	}
	/* (unwind tables would have to describe the d registers' slots: not written) */
	a64_vir_registers_describe(&regs, CODEGEN_IS_DARWIN(c), !e->unit->options->cfi && !a64_vir_vectors_cross_call(f));
	if (!vir_loops_build(f, &V->loops)) {
		*why = VIRBUILD_FAILURE(loop_analysis);
		return false;
	}
	/* i2p and p2i are copies here: their users read the operand (a load's address an add,
	 * to fold), and they go unemitted */
	for (uint32_t b = 0; b < f->block_count; b++)
		for (uint32_t i = f->blocks[b].first; i < f->blocks[b].first + f->blocks[b].insn_count; i++) {
			vir_operand_iterator it;
			uint32_t *p;

			vir_operand_iterator_init(&it, f, &f->insns[i], false);
			while ((p = vir_operand_iterator_next(&it)))
				while (f->insns[*p].op == VIR_OP_I2P || f->insns[*p].op == VIR_OP_P2I)
					*p = f->insns[*p].a;
		}
	/* shrink-wrapping (SPEC-vir.md 15.12): the allocator is told which calls will be sibling
	 * calls, which need no frame.  Not with unwind tables (the rows would have to follow the
	 * prologue into the body).  VCC_VIR_SHRINK=0: off */
	{
		static int on = -1;
		vir_alloc_options ao = { 0 };

		if (on < 0) {
			const char *x = getenv("VCC_VIR_SHRINK");

			on = !(x && *x == '0');
		}
		L.context = e, L.function = f;
		a64_vir_thread_state_instance.low32 = a64_vir_array_grow(a64_vir_thread_state_instance.low32, &a64_vir_thread_state_instance.low32_capacity, f->insn_count + 1, 1);
		a64_vir_thread_state_instance.needs_fallthrough = a64_vir_array_grow(a64_vir_thread_state_instance.needs_fallthrough, &a64_vir_thread_state_instance.fallthrough_capacity, f->insn_count + 1, 1);
		L.ret32 = KIND(c, ft->base) != AST_TYPE_VOID && codegen_type_is_integer(c, ft->base) && codegen_type_size(c, ft->base) == 4;
		L.tail = !(ft->flags & AST_TYPE_FLAG_VARIADIC) && em_tail_caller_ok(e, s) && !a64_vir_tail_call_reason(&L, a64_vir_thread_state_instance.low32);
		if (on && !e->unit->cleanups_disabled && !(e->unit->options->cfi && !CODEGEN_IS_DARWIN(c))) {
			memset(a64_vir_thread_state_instance.needs_fallthrough, 0, f->insn_count);
			for (uint32_t b = 0; L.tail && b < f->block_count; b++)
				for (uint32_t i = f->blocks[b].first; i < f->blocks[b].first + f->blocks[b].insn_count; i++)
					if (f->insns[i].op == VIR_OP_CALL) {
						a64_vir_thread_state_instance.needs_fallthrough[i] = a64_vir_call_is_sibling_candidate(&L, b, i);
					}
			ao.shrink = true;
			ao.no_frame = a64_vir_thread_state_instance.needs_fallthrough;
		}
		if (!vir_alloc(f, &V->loops, &regs, &ao, &V->allocation)) {
			*why = VIRBUILD_FAILURE(allocation_failed);
			snprintf(V->failure_detail, sizeof V->failure_detail, "%s", V->allocation.error_message);
			return false;
		}
		L.save_point = ao.shrink ? V->allocation.save_point : VIR_BLOCK_NONE;
	}
	if (getenv("VCC_VIR_CHECK")) {
		vbuffer msg = { 0 };
		uint32_t n = vir_alloc_check(f, &regs, &V->allocation, &msg);

		if (n) {
			fprintf(stderr, "vir: allocation check: %u failures\n%.*s", n, (int)msg.n, msg.p ? (char *)msg.p : "");
			free(msg.p);
			*why = VIRBUILD_FAILURE(allocation_check_failed);
			return false;
		}
		free(msg.p);
	}
	L.context = e, L.function = f, L.allocation = &V->allocation;
	a64_vir_thread_state_instance.slot_offsets = a64_vir_array_grow(a64_vir_thread_state_instance.slot_offsets, &a64_vir_thread_state_instance.slot_offset_capacity, f->slot_count + 1, sizeof(int64_t));
	a64_vir_thread_state_instance.spill_offsets = a64_vir_array_grow(a64_vir_thread_state_instance.spill_offsets, &a64_vir_thread_state_instance.spill_offset_capacity, V->allocation.spill_count + 1, sizeof(int64_t));
	a64_vir_thread_state_instance.labels = a64_vir_array_grow(a64_vir_thread_state_instance.labels, &a64_vir_thread_state_instance.label_capacity, f->block_count + 1, sizeof(uint32_t));
	a64_vir_thread_state_instance.uses = a64_vir_array_grow(a64_vir_thread_state_instance.uses, &a64_vir_thread_state_instance.uses_capacity, f->insn_count + 1, 1);
	a64_vir_thread_state_instance.low32 = a64_vir_array_grow(a64_vir_thread_state_instance.low32, &a64_vir_thread_state_instance.low32_capacity, f->insn_count + 1, 1);
	a64_vir_thread_state_instance.chases = a64_vir_array_grow(a64_vir_thread_state_instance.chases, &a64_vir_thread_state_instance.chase_capacity, f->block_count + 1, sizeof(uint32_t));
	a64_vir_thread_state_instance.path = a64_vir_array_grow(a64_vir_thread_state_instance.path, &a64_vir_thread_state_instance.path_capacity, f->block_count + 1, sizeof(uint32_t));
	L.slot_offsets = a64_vir_thread_state_instance.slot_offsets, L.spill_offsets = a64_vir_thread_state_instance.spill_offsets, L.labels = a64_vir_thread_state_instance.labels, L.uses = a64_vir_thread_state_instance.uses, L.low32 = a64_vir_thread_state_instance.low32;
	L.chases = a64_vir_thread_state_instance.chases;
	a64_vir_thread_state_instance.order = a64_vir_array_grow(a64_vir_thread_state_instance.order, &a64_vir_thread_state_instance.order_capacity, f->block_count + 1, sizeof(uint32_t));
	a64_vir_thread_state_instance.order_positions = a64_vir_array_grow(a64_vir_thread_state_instance.order_positions, &a64_vir_thread_state_instance.order_position_capacity, f->block_count + 1, sizeof(uint32_t));
	a64_vir_thread_state_instance.head_labels = a64_vir_array_grow(a64_vir_thread_state_instance.head_labels, &a64_vir_thread_state_instance.head_label_capacity, f->block_count + 1, sizeof(uint32_t));
	a64_vir_thread_state_instance.epilogue_labels = a64_vir_array_grow(a64_vir_thread_state_instance.epilogue_labels, &a64_vir_thread_state_instance.epilogue_label_capacity, f->block_count + 1, sizeof(uint32_t));
	memset(a64_vir_thread_state_instance.epilogue_labels, 0, (size_t)(f->block_count + 1) * sizeof(uint32_t));
	L.order = a64_vir_thread_state_instance.order, L.order_positions = a64_vir_thread_state_instance.order_positions, L.loops = &V->loops;
	L.drv = (virbuild_lowering){
		.target = &vl_lowering_target, .ctx = &L, .f = f, .o = L.allocation, .lp = L.loops,
		.order = L.order, .position = L.order_positions, .rotation = a64_vir_thread_state_instance.head_labels, .chase = L.chases, .path = a64_vir_thread_state_instance.path,
		.block_label = L.labels, .optimize = !e->unit->cleanups_disabled,
		.operand_value = L.operand_values, .operand_reg = L.operand_registers, .max_operands = A64_VIR_OPERAND_MAX, .skip_at = &L.skip,
	};
	L.ret32 = KIND(c, ft->base) != AST_TYPE_VOID && codegen_type_is_integer(c, ft->base) && codegen_type_size(c, ft->base) == 4;
	L.tail = !(ft->flags & AST_TYPE_FLAG_VARIADIC) && em_tail_caller_ok(e, s) && !a64_vir_tail_call_reason(&L, a64_vir_thread_state_instance.low32);	/* (lo32: scratch until it is filled) */
	vir_uses_count(f, L.uses);
	vir_demand_low32_compute(f, L.ret32, L.low32);

	/* Keep result tables queued by the VIR walker: end_function emits them. */
	/* the emitter's per-function state, as emit_function1 sets it */
	e->record_count = 0;
	e->debug_row_count = 0;
	e->raw.n = 0;
	e->label_count = 0;
	e->known_value_count = 0, e->known_values_valid = 0, e->known_value_state_count = 0, e->array_element_count = 0;
	e->frame_offset = e->frame_maximum = e->outgoing_argument_bytes_max = e->compound_literal_top = 0;
	e->slot_allocation_count = e->live_slot_count = 0;
	e->frame_alignment = 16, e->temporaries_reserved = 0, e->vla_size_count = 0;
	e->dynamic_frame = e->has_calls = false;
	e->temporaries_in_use = 0;
	e->callee_saved_used = 0;
	e->loop_count = e->cleanup_count = 0;
	e->case_count = 0;
	e->jump_table_count = e->jump_table_label_count = 0;
	e->is_reachable = true;
	e->top_symbol = s;
	e->asm_statement_count = 0;
	e->inline_depth = 0;
	e->is_variadic = (ft->flags & AST_TYPE_FLAG_VARIADIC) != 0;
	e->local_register_candidate_count = 0;
	e->function_instance = NULL;
	e->vir_failure_reason = NULL;
	e->vir_lowering = true;
	e->vir_try = true;	/* efatal, eerror: back here */
	switch (_setjmp(e->bail_jump)) {
	case 0:
		break;
	default:
		*why = e->vir_failure_reason ? e->vir_failure_reason : VIRBUILD_FAILURE(lowering_failed);
		e->vir_try = false;
		e->vir_lowering = false;
		return false;
	}
	for (uint32_t k = 0; k < f->slot_count; k++) {
		if (f->slot[k].align > 16)
			a64_vir_fail(&L, VIRBUILD_FAILURE(over_aligned_slot));
		L.slot_offsets[k] = slot(e, f->slot[k].size ? f->slot[k].size : 1, f->slot[k].align);
	}
	for (uint32_t k = 0; k < V->allocation.spill_count; k++)
		L.spill_offsets[k] = slot(e, V->allocation.slot_sizes[k], V->allocation.slot_sizes[k]);
	for (int r = 8; r < 16; r++)
		if (V->allocation.callee[1] >> r & 1)
			L.callee_saved_float_offsets[r - 8] = slot(e, 8, 8);
	for (uint32_t b = 0; b < f->block_count; b++)
		L.labels[b] = newlabel(e);
	L.return_label = newlabel(e);
	a64_vir_function_body_lower(&L);
	deflabel_forced(e, L.return_label);
	if (L.frame_is_at_risk && e->frame_maximum + e->outgoing_argument_bytes_max + 128 > 4000)
		a64_vir_fail(&L, VIRBUILD_FAILURE(large_frame_scratch));
	for (int r = 19; r <= 28; r++)
		if (V->allocation.callee[0] >> r & 1)
			e->callee_saved_used |= 1u << r;
	finish_function(e, s);
	e->vir_try = false;
	e->vir_lowering = false;
	return true;
}
