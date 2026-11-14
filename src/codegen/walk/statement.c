/*
 * statement.c: the walker's statements: the per-function state (wkst), initializers, declarations,
 * switch, control flow, inlining, and the function itself (walk_function_emit).
 *
 * Compiled twice, like expr.c (x86-64's direct lowering, and VIR's builder through codegen/vir/walker-instance.c
 * with WALK_PREFIX virbuild_): see expr.c for the rules that follow.  walk_function_emit(e, s) is what tu.c (for
 * x86-64) and codegen/vir/build-attempt.c call for one function; it sets up the frame slots and labels (all
 * indexed per symbol or node with a generation number, so nothing is cleared between functions),
 * walks the body, and finishes through WALK_LOWER(finish).  Reachability (e->reach) follows branches so that
 * no code is generated behind constant conditions; array-element constants are forgotten at every
 * label (walk_label_place), so no fact crosses a join.  Inlining is done here: an inlined copy binds its
 * parameters to the arguments, and its returns go to a label (wkinl); the prepass (prepass.c) says
 * whether a function may be inlined.
 */
#include "walk.h"

/* ---------------- state ---------------- */

/* Array element constants are local to a straight-line region. Forget them at
   every control-flow join/loop header rather than guessing predecessor facts. */
VPRIV void walk_label_place(codegen_context *e, uint32_t label)
{
	e->known_values_valid = 0;
	WALK_LOWER(label)(e, label);
}

VPRIV walk_state *walk_state_get(codegen_context *e)
{
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;

	if (!W) {
		W = calloc(1, sizeof *W);
		W->cases = codegen_memory_reserve(WALK_CASE_MAX * sizeof(walk_case));
		e->walk_state = W;
	}
	if (!W->symbol_generations) {
		/* sized for this TU (gen-tagged: never cleared); one TU per ecx */
		W->symbol_generations = codegen_memory_reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->symbol_offsets = codegen_memory_reserve(((size_t)c->nsym + 64) * sizeof(int64_t));
		W->label_generations = codegen_memory_reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->user_labels = codegen_memory_reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->case_generations = codegen_memory_reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->case_labels = codegen_memory_reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->constant_generations = codegen_memory_reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->constant_values = codegen_memory_reserve(((size_t)c->nsym + 64) * sizeof(codegen_value));
		W->label_instances = codegen_memory_reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->case_instances = codegen_memory_reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->cleanup_symbol_nodes = codegen_memory_reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->cleanup_symbol_generations = codegen_memory_reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->label_cleanup_nodes = codegen_memory_reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->label_cleanup_generations = codegen_memory_reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->vla_zero_generations = codegen_memory_reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->vla_generations = codegen_memory_reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->vla_instances = codegen_memory_reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->vla_offsets = codegen_memory_reserve(((size_t)c->nnode + 64) * sizeof(int64_t));
	}
	return W;
}

VPRIV void WALK_LOWER(ecx_free)(codegen_context *e);

/* the walker's and the lowering's state of this ecx, given back (the TU is done) */
VPRIV void walk_context_free(codegen_context *e)
{
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;

	WALK_LOWER(ecx_free)(e);
	if (!W)
		return;
	if (W->symbol_generations) {
		munmap(W->symbol_generations, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->symbol_offsets, ((size_t)c->nsym + 64) * sizeof(int64_t));
		munmap(W->label_generations, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->user_labels, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->case_generations, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->case_labels, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->constant_generations, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->constant_values, ((size_t)c->nsym + 64) * sizeof(codegen_value));
		munmap(W->label_instances, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->case_instances, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->cleanup_symbol_nodes, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->cleanup_symbol_generations, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->label_cleanup_nodes, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->label_cleanup_generations, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->vla_zero_generations, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->vla_generations, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->vla_instances, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->vla_offsets, ((size_t)c->nnode + 64) * sizeof(int64_t));
	}
	munmap(W->cases, WALK_CASE_MAX * sizeof(walk_case));
	free(W);
	e->walk_state = NULL;
}

/* a temporary frame slot: a free one of the same size and alignment, else new */
VPRIV int64_t walk_slot_temporary(codegen_context *e, uint64_t size, uint32_t align)
{
	walk_state *W = e->walk_state;

	if (!W->statement_depth || size > UINT32_MAX)
		return walk_slot_permanent(e, size, align);
	if (align < 1)
		align = 1;
	for (uint32_t i = W->temporary_slot_free_count; i-- > 0;)
		if (W->temporary_slots_free[i].size == size && W->temporary_slots_free[i].align == align) {
			struct walk_temporary_slot t = W->temporary_slots_free[i];

			W->temporary_slots_free[i] = W->temporary_slots_free[--W->temporary_slot_free_count];
			if (W->temporary_slot_in_use_count < WALK_TEMPORARY_SLOT_MAX)
				W->temporary_slots_in_use[W->temporary_slot_in_use_count++] = t;
			WALK_LOWER(slot)(e, t.offset, size, align, WALK_SLOT_TEMPORARY);
			return t.offset;
		}
	int64_t o = walk_slot_permanent_kind(e, size, align, WALK_SLOT_TEMPORARY);

	if (W->temporary_slot_in_use_count < WALK_TEMPORARY_SLOT_MAX)
		W->temporary_slots_in_use[W->temporary_slot_in_use_count++] = (struct walk_temporary_slot){ o, (uint32_t)size, align };
	return o;
}

/* a frame slot of size bytes for the whole function: its offset from the frame base (negative) */
VPRIV int64_t walk_slot_permanent(codegen_context *e, uint64_t size, uint32_t align)
{
	return walk_slot_permanent_kind(e, size, align, WALK_SLOT_FUNCTION);
}

VPRIV int64_t walk_slot_permanent_kind(codegen_context *e, uint64_t size, uint32_t align, int kind)
{
	walk_state *W = e->walk_state;
	int64_t o;

	if (align < 1)
		align = 1;
	/* Reserve alignment rounding and the prologue's final 16-byte rounding
	   before adding, so even malformed enormous objects cannot overflow. */
	uint64_t limit = (uint64_t)INT64_MAX - align - 15;
	if ((uint64_t)W->frame_offset > limit || size > limit - (uint64_t)W->frame_offset)
		codegen_fatal_report(e, e->current_location, "x86-64: frame too large");
	o = W->frame_offset + (int64_t)size;
	o = (o + align - 1) & ~(int64_t)(align - 1);
	W->frame_offset = o;
	if (align > W->frame_alignment)
		W->frame_alignment = align;
	WALK_LOWER(slot)(e, -(int64_t)o, size, align, kind);
	return -(int64_t)o;
}

VPRIV uint32_t walk_label_new(codegen_context *e) { return ++e->walk_state->label_count; }

static uint32_t walk_user_label_find(codegen_context *e, uint32_t l)
{
	walk_state *W = e->walk_state;

	if (W->label_generations[l] != W->generation || W->label_instances[l] != W->instance) {
		W->label_generations[l] = W->generation;
		W->label_instances[l] = W->instance;
		W->user_labels[l] = walk_label_new(e);
	}
	return W->user_labels[l];
}

VPRIV uint32_t walk_user_label_get(codegen_context *e, uint32_t l) { return walk_user_label_find(e, l); }

static uint32_t walk_case_label_get(codegen_context *e, uint32_t n)
{
	walk_state *W = e->walk_state;

	if (W->case_generations[n] != W->generation || W->case_instances[n] != W->instance) {
		W->case_generations[n] = W->generation;
		W->case_instances[n] = W->instance;
		W->case_labels[n] = walk_label_new(e);
	}
	return W->case_labels[n];
}

/* ---------------- initializers ---------------- */

static bool walk_initializer_is_zero(cc *c, uint32_t v)
{
	const node *x = N(v);

	if (!v || x->op == AST_NODE_ZERO)
		return true;
	if (x->op == AST_NODE_NUMBER)
		return c->ipool[x->a] == 0;
	if (x->op == AST_NODE_CAST && (x->aux == AST_CAST_INTEGER || x->aux == AST_CAST_NOP || x->aux == AST_CAST_NULL_POINTER || x->aux == AST_CAST_INTEGER_TO_POINTER))
		return x->aux == AST_CAST_NULL_POINTER || walk_initializer_is_zero(c, x->a);
	return false;
}

/* the address of d (a frame slot or a symbol) in a new temporary */
static int walk_lvalue_address_get(codegen_context *e, const codegen_lvalue *d)
{
	if (d->kind == CODEGEN_LVALUE_REGISTER)
		codegen_fatal_report(e, e->current_location, "internal error: an initializer through a register");
	return WALK_LOWER(laddr)(e, d);
}

/* the elements of init stored into the (already zeroed) object at d */
static void walk_initializer_elements_store(codegen_context *e, const codegen_lvalue *d, ast_qualified_type ty, uint32_t init)
{
	cc *c = e->compile_context;
	const node *x = N(init);

	if (!init || x->op == AST_NODE_ZERO)
		return;
	if (x->op == AST_NODE_INITIALIZER) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			ast_qualified_type et = T(c, ty)->base;
			uint64_t es = codegen_type_size(c, et);

			for (uint32_t i = 0; i < x->c; i++) {
				uint32_t lo = v[3 * i], hi = v[3 * i + 1], vi = v[3 * i + 2];

				for (uint64_t k = lo; k <= hi; k++) {
					codegen_lvalue el = *d;

					if (codegen_type_is_bool_vector(c, ty)) {
						el.offset += (int64_t)(k / 8);
						el.is_bitfield = 1, el.width = 1, el.bit_offset = k % 8;
						walk_bitfield_initialize(e, &el, et, vi);
						continue;
					}
					el.offset += (int64_t)(k * es);
					walk_initializer_elements_store(e, &el, et, vi);
				}
			}
			return;
		}
		{
			const tag *r = &c->tags[T(c, ty)->aux];

			for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
				const field *f = &r->f[i];
				codegen_lvalue el = *d;

				if ((x->aux == 2 && i != x->a) || !v[i])
					continue;
				el.offset += (int64_t)(f->off / 8);
				if (f->bitfield) {
					if (walk_initializer_is_zero(c, v[i]))
						continue;
					el.is_bitfield = 1;
					el.bit_offset = (uint16_t)(f->off % 8);
					el.width = f->width;
					el.is_signed = codegen_type_is_signed(c, f->ty);
					walk_bitfield_initialize(e, &el, f->ty, v[i]);
					continue;
				}
				walk_initializer_elements_store(e, &el, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == AST_NODE_STRING && codegen_type_is_array(c, ty)) {
		/* the literal's bytes (up to the array's size; the rest is zero) */
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz, cap = codegen_type_size(c, ty);
		codegen_lvalue src = { .kind = CODEGEN_LVALUE_SYMBOL, .reference = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_STRING, x->a), .align = s->esz };
		int rd, rs;

		if (n > cap)
			n = cap;
		codegen_reference_note(e, src.reference);
		rd = walk_lvalue_address_get(e, d);
		rs = WALK_LOWER(laddr)(e, &src);
		WALK_LOWER(copy)(e, rd, rs, n);
		WALK_LOWER(tfree)(e, rs);
		WALK_LOWER(tfree)(e, rd);
		return;
	}
	if (walk_initializer_is_zero(c, init) && !walk_type_is_in_memory(e, ty))
		return;
	if (walk_type_is_in_memory(e, ty)) {
		/* another object's bytes (a struct variable, a call's result ...) */
		codegen_value v = walk_value_emit(e, init);
		int rs = walk_value_to_register(e, &v, e->compile_context->t_size), rd = walk_lvalue_address_get(e, d);

		uint64_t bytes = codegen_type_size(c, ty);
		if (codegen_type_is_record(c, ty) && (T(c, ty)->flags & AST_TYPE_FLAG_ATOMIC)) {
			/* Initialization is not a concurrent store; copy only the source
			   payload and initialize any padding added by _Atomic. */
			WALK_LOWER(zero)(e, rd, bytes);
			bytes = codegen_type_size(c, AST_QUALIFIED_TYPE(T(c, ty)->n, 0));
		}
		WALK_LOWER(copy)(e, rd, rs, bytes);
		WALK_LOWER(tfree)(e, rs);
		WALK_LOWER(tfree)(e, rd);
		return;
	}
	{
		codegen_value v = walk_value_emit(e, init);
		int r = walk_value_to_register(e, &v, ty);

		WALK_LOWER(store)(e, r, d, walk_type_size(c, ty));
		WALK_LOWER(tfree)(e, r);
	}
}

VPRIV void walk_initializer_emit(codegen_context *e, const codegen_lvalue *d, ast_qualified_type t, uint32_t init)
{
	cc *c = e->compile_context;
	const node *x = N(init);
	int vol = (AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) != 0;

	if (!init)
		return;
	if (x->op == AST_NODE_INITIALIZER || x->op == AST_NODE_ZERO || (x->op == AST_NODE_STRING && codegen_type_is_array(c, t))) {
		int rd = walk_lvalue_address_get(e, d);

		WALK_LOWER(zero)(e, rd, codegen_type_size(c, t));
		WALK_LOWER(tfree)(e, rd);
		e->volatile_untag_depth += vol;	/* (a volatile object's initializing stores are volatile accesses too) */
		walk_initializer_elements_store(e, d, t, init);
		e->volatile_untag_depth -= vol;
		return;
	}
	if (walk_type_is_in_memory(e, t)) {
		e->volatile_untag_depth += vol;
		walk_initializer_elements_store(e, d, t, init);	/* another object's bytes */
		e->volatile_untag_depth -= vol;
		return;
	}
	{
		/* a scalar: stored whatever its value (the object was not zeroed) */
		codegen_value v = walk_value_emit(e, init);
		int r = walk_value_to_register(e, &v, t);

		e->volatile_untag_depth += vol;
		WALK_LOWER(store)(e, r, d, walk_type_size(c, t));
		e->volatile_untag_depth -= vol;
		WALK_LOWER(tfree)(e, r);
	}
}

/* ---------------- declarations ---------------- */

/* cleanup(f) variables: f(&var), innermost first, down to depth `to` (the stack stays) */
VPRIV void walk_cleanups_run(codegen_context *e, int to)
{
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;

	for (int i = W->cleanup_count; i-- > to;) {
		uint32_t s = W->cleanups[i], fn = codegen_symbol_canonical(c, codegen_symbol_attributes(c, s)->cleanup);
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_local_slot(e, s), .align = 8 };
		walk_argument a;
		codegen_value r;

		em_abi(e, c->syms[fn].ty);
		WALK_LOWER(classify)(e, e->compile_context->t_size, &a, false);
		a.slot = walk_slot_temporary(e, 8, 8);
		{
			int ra = WALK_LOWER(laddr)(e, &l);
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_FRAME, .offset = a.slot, .align = 8 };

			WALK_LOWER(store)(e, ra, &d, 8);
			WALK_LOWER(tfree)(e, ra);
		}
		codegen_reference_note(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, fn));
		e->has_calls = true;
		em_abi(e, c->syms[fn].ty);
		r = WALK_LOWER(call)(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, fn), NULL, &a, 1, false, NULL, codegen_type_primitive(AST_TYPE_VOID));
		walk_value_free(e, &r);
	}
}

static void walk_declaration_emit(codegen_context *e, const node *x)
{
	cc *c = e->compile_context;
	uint32_t s = x->a;
	const sym *y = &c->syms[s];

	walk_array_elements_clear(e, s);

	if (!x->aux && unlikely(codegen_type_is_variably_modified(c, y->ty)))
		walk_vla_sizes_evaluate(e, y->ty, false);
	if (y->kind != AST_SYMBOL_VARIABLE || y->sc == AST_STORAGE_STATIC || y->sc == AST_STORAGE_EXTERN || y->sc == AST_STORAGE_TYPEDEF)
		return;
	if (T(c, y->ty)->flags & AST_TYPE_FLAG_VLA) {
		walk_vla_declare(e, s, x->b != 0);
		return;
	}
	{
		unsigned recipe = em_unused_init(e, x, e->walk_state->inline_call ? e->walk_state->inline_call->function : e->walk_state->function_symbol, 64);

		if (recipe != EM_UNUSED_KEEP) {
			if (recipe == EM_UNUSED_EVAL) {
				codegen_value v = walk_value_emit(e, x->b);

				walk_value_free(e, &v);
			}
			return;
		}
	}
	e->walk_state->constant_generations[s] = 0;
	/* A read of a never-written automatic integer has an indeterminate
	   value. Choose zero without allocating or reading a stack slot. */
	if (e->unit->options->opt && !x->b && codegen_type_is_integer(c, y->ty) && !walk_type_is_in_memory(e, y->ty) &&
	    !(e->unit->symbol_flags[s] & (CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_CONSTANT_WRITTEN | CODEGEN_SYMBOL_CONSTANT_MULTIPLE)) && !e->unit->local_needs_memory[s] &&
	    !(AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_VOLATILE) && !(T(c, y->ty)->flags & AST_TYPE_FLAG_ATOMIC) &&
	    !(y->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) && y->sc != AST_STORAGE_REGISTER && !codegen_symbol_attributes(c, s)->cleanup) {
		e->walk_state->constant_generations[s] = e->walk_state->generation;
		e->walk_state->constant_values[s] = codegen_value_constant(0);
		return;
	}
	if (e->unit->options->opt && x->b && !codegen_expression_has_side_effects(c, x->b) && !codegen_symbol_attributes(c, s)->cleanup) {
		/* A side-effect-free constant initializer for a local never written. */
		cc_value cv;
		uint8_t f = e->unit->symbol_flags[s];

		if (!(f & (CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_CONSTANT_MULTIPLE)) && !(AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_VOLATILE) && y->sc != AST_STORAGE_REGISTER &&
		    (codegen_type_is_integer(c, y->ty) || codegen_type_is_pointer(c, y->ty)) && !walk_type_is_in_memory(e, y->ty) && cc_eval(c, x->b, &cv) && cv.kind == CC_VALUE_INTEGER &&
		    (!(f & CODEGEN_SYMBOL_CONSTANT_WRITTEN) || (codegen_type_is_integer(c, y->ty) && e->unit->constant_written_values[s] == codegen_integer_truncate(c, cv.i, y->ty)))) {
			/* (CODEGEN_SYMBOL_CONSTANT_WRITTEN: every assignment stores this same constant, as set_uncached = IS_ENABLED(...)) */
			e->walk_state->constant_generations[s] = e->walk_state->generation;
			e->walk_state->constant_values[s] = codegen_value_constant(codegen_integer_truncate(c, cv.i, y->ty));
			return;
		}
	}
	{
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .align = codegen_symbol_alignment(c, s) };

		if (!WALK_LOWER(local)(e, s, &l))
			l.offset = walk_local_slot(e, s);
		if (!x->b && e->unit->options->autozero && codegen_type_size(c, y->ty) && !(y->sc == AST_STORAGE_REGISTER && codegen_symbol_attributes(c, s)->asmname)) {
			/* -ftrivial-auto-var-init=zero: a local without an initializer starts as zeros
			   (a scalar by a store: it may live in a register) */
			if (walk_type_is_in_memory(e, y->ty) || codegen_type_is_array(c, y->ty)) {
				int rd = WALK_LOWER(laddr)(e, &l);

				WALK_LOWER(zero)(e, rd, codegen_type_size(c, y->ty));
				WALK_LOWER(tfree)(e, rd);
			} else {
				codegen_value z = walk_value_convert(e, codegen_value_constant(0), codegen_type_primitive(AST_TYPE_INT), y->ty);

				walk_lvalue_store(e, &l, y->ty, &z);
			}
		}
#ifdef WALK_PREFIX
		if (!x->b && !e->unit->options->autozero)
			WALK_LOWER(undecl)(e, &l, codegen_type_size(c, y->ty));
#endif
		walk_initializer_emit(e, &l, y->ty, x->b);
	}
	if (codegen_symbol_attributes(c, s)->cleanup) {
		if (e->walk_state->cleanup_count == WALK_CLEANUP_MAX)
			codegen_fatal_report(e, x->loc, "too many cleanup variables in scope");
		e->walk_state->cleanups[e->walk_state->cleanup_count++] = s;
	}
}

/*
 * The VLAs declared in the statements under n: their saved sp zeroed on
 * entry, so a declaration reached again (a goto back) frees the previous
 * instance first.  (One inside a statement expression is not seen: it is
 * not freed by a goto back.)
 */
static void walk_vla_scan(codegen_context *e, uint32_t n, uint32_t *top)
{
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;
	const node *x;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case AST_NODE_DECLARATION: {
		const sym *y = &c->syms[x->a];

		if (y->kind == AST_SYMBOL_VARIABLE && y->sc != AST_STORAGE_TYPEDEF && y->sc != AST_STORAGE_STATIC && y->sc != AST_STORAGE_EXTERN &&
		    (T(c, y->ty)->flags & AST_TYPE_FLAG_VLA)) {
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_local_slot(e, x->a) + 8, .align = 8 };
			codegen_value z = codegen_value_constant(0);
			int r = WALK_LOWER(toreg)(e, &z, 8);

			WALK_LOWER(store)(e, r, &l, 8);
			WALK_LOWER(tfree)(e, r);
			W->vla_zero_generations[x->a] = W->generation;
		}
		if (y->kind == AST_SYMBOL_VARIABLE && codegen_symbol_attributes(c, x->a)->cleanup && W->cleanup_node_count + 1 < WALK_CLEANUP_MAX) {
			W->cleanup_node_count++;
			W->cleanup_nodes[W->cleanup_node_count].symbol = x->a;
			W->cleanup_nodes[W->cleanup_node_count].parent = *top;
			W->cleanup_symbol_nodes[x->a] = W->cleanup_node_count;
			W->cleanup_symbol_generations[x->a] = W->generation;
			*top = W->cleanup_node_count;	/* in scope for the rest of the block */
		}
		return;
	}
	case AST_NODE_BLOCK: {
		uint32_t t = *top;

		for (uint32_t i = 0; i < x->c; i++)
			walk_vla_scan(e, c->lists[x->b + i], &t);
		return;
	}
	case AST_NODE_IF: {
		uint32_t t = *top, u = *top;

		walk_vla_scan(e, x->b, &t);
		walk_vla_scan(e, x->c, &u);
		return;
	}
	case AST_NODE_WHILE: case AST_NODE_DO: case AST_NODE_SWITCH: {
		uint32_t t = *top;

		walk_vla_scan(e, x->b, &t);
		return;
	}
	case AST_NODE_FOR: {
		uint32_t t = *top;

		/* a cleanup variable of the first clause is around the body's labels
		   (scoped_guard: the step's goto into the body runs no cleanup) */
		if (x->a && N(x->a)->op == AST_NODE_BLOCK)
			for (uint32_t i = 0; i < N(x->a)->c; i++)
				walk_vla_scan(e, c->lists[N(x->a)->b + i], &t);
		else
			walk_vla_scan(e, x->a, &t);
		walk_vla_scan(e, c->lists[x->c + 1], &t);
		return;
	}
	case AST_NODE_LABEL:
		W->label_cleanup_nodes[x->a] = *top;
		W->label_cleanup_generations[x->a] = W->generation;
		walk_vla_scan(e, x->c, top);
		return;
	case AST_NODE_CASE: case AST_NODE_DEFAULT:
		walk_vla_scan(e, x->c, top);
		return;
	default:
		return;
	}
}

/* A discarded branch can still be entered through a C label, a switch case,
   or a label inside a GNU statement expression. Unknown forms conservatively
   keep the branch; declarations/VLA bounds need their own scope traversal. */
VPRIV bool walk_branch_entry_check(cc *c, uint32_t n)
{
	if (!n) return false;
	const node *x = N(n);
	switch (x->op) {
	case AST_NODE_NONE: case AST_NODE_NUMBER: case AST_NODE_BITINT_NUMBER: case AST_NODE_FLOAT_NUMBER: case AST_NODE_STRING: case AST_NODE_VARIABLE:
	case AST_NODE_LABEL_ADDRESS: case AST_NODE_ZERO: case AST_NODE_GOTO: case AST_NODE_BREAK: case AST_NODE_CONTINUE:
		return false;
	case AST_NODE_LABEL: case AST_NODE_CASE: case AST_NODE_DEFAULT:
		return true;
	case AST_NODE_BLOCK: case AST_NODE_BUILTIN:
		for (uint32_t i = 0; i < x->c; i++)
			if (walk_branch_entry_check(c, c->lists[x->b + i])) return true;
		return false;
	case AST_NODE_CALL:
		if (walk_branch_entry_check(c, x->a)) return true;
		for (uint32_t i = 0; i < x->c; i++)
			if (walk_branch_entry_check(c, c->lists[x->b + i])) return true;
		return false;
	case AST_NODE_MEMBER: case AST_NODE_UNARY: case AST_NODE_ADDRESS: case AST_NODE_DEREFERENCE:
	case AST_NODE_PRE_INCREMENT: case AST_NODE_PRE_DECREMENT: case AST_NODE_POST_INCREMENT: case AST_NODE_POST_DECREMENT:
	case AST_NODE_STATEMENT_EXPRESSION: case AST_NODE_VA_ARG: case AST_NODE_EXPRESSION: case AST_NODE_RETURN: case AST_NODE_GOTO_INDIRECT:
		return walk_branch_entry_check(c, x->a);
	case AST_NODE_CAST:
		return (T(c, x->ty)->flags & AST_TYPE_FLAG_VLA) || walk_branch_entry_check(c, x->a);
	case AST_NODE_INDEX: case AST_NODE_BINARY: case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR:
	case AST_NODE_ASSIGN: case AST_NODE_COMMA: case AST_NODE_COMPLEX:
	case AST_NODE_WHILE: case AST_NODE_DO: case AST_NODE_SWITCH:
		return walk_branch_entry_check(c, x->a) || walk_branch_entry_check(c, x->b);
	case AST_NODE_CONDITIONAL: case AST_NODE_IF:
		return walk_branch_entry_check(c, x->a) || walk_branch_entry_check(c, x->b) || walk_branch_entry_check(c, x->c);
	case AST_NODE_FOR:
		return walk_branch_entry_check(c, x->a) || walk_branch_entry_check(c, x->b) ||
		       walk_branch_entry_check(c, c->lists[x->c]) || walk_branch_entry_check(c, c->lists[x->c + 1]);
	case AST_NODE_ASM: {
		const ast_asm_statement *a = &c->asms[x->a];
		for (uint32_t i = 0; i < a->nout + a->nin; i++)
			if (walk_branch_entry_check(c, a->ops[i].expr)) return true;
		return false;
	}
	case AST_NODE_DECLARATION: {
		/* (a VLA or a cleanup variable needs its scope walked; else only its initializer) */
		const sym *y = &c->syms[x->a];

		if (y->kind == AST_SYMBOL_VARIABLE && ((T(c, y->ty)->flags & AST_TYPE_FLAG_VLA) || codegen_symbol_attributes(c, x->a)->cleanup))
			return true;
		return y->kind == AST_SYMBOL_VARIABLE && y->sc != AST_STORAGE_STATIC && y->sc != AST_STORAGE_EXTERN && walk_branch_entry_check(c, x->b);
	}
	default:
		return true;
	}
}

/* ---------------- switch ---------------- */

static void walk_cases_collect(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;
	const node *x;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case AST_NODE_CASE: case AST_NODE_DEFAULT:
		if (W->case_count == WALK_CASE_MAX)
			codegen_fatal_report(e, x->loc, "too many case labels");
		W->cases[W->case_count++] = (walk_case){ x->op == AST_NODE_CASE ? c->ipool[x->a] : 0, x->op == AST_NODE_CASE ? c->ipool[x->b] : 0, n };
		walk_cases_collect(e, x->c);
		return;
	case AST_NODE_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			walk_cases_collect(e, c->lists[x->b + i]);
		return;
	case AST_NODE_IF:
		walk_cases_collect(e, x->b);
		walk_cases_collect(e, x->c);
		return;
	case AST_NODE_WHILE: case AST_NODE_DO:
		walk_cases_collect(e, x->b);
		return;
	case AST_NODE_FOR:
		walk_cases_collect(e, c->lists[x->c + 1]);
		return;
	case AST_NODE_LABEL:
		walk_cases_collect(e, x->c);
		return;
	default:
		return;
	}
}

/* Every terminal dispatch jumps, so laying out the right subtree first does
 * not introduce a path into the left subtree or the source-order case body. */
static void walk_switch_tree_emit(codegen_context *e, const em_switch_case *cases, uint32_t n,
			   int r, int w, bool sg, uint32_t dflt)
{
	if (!n) {
		WALK_LOWER(jump)(e, dflt);
		return;
	}
	uint32_t mid = n / 2;
	const em_switch_case *k = &cases[mid];
	codegen_value v = { .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
	if (n == 1 && k->lo == k->hi) {
		WALK_LOWER(cmpbr)(e, LEX_TOKEN_EQUAL_EQUAL, w, sg, v, codegen_value_constant(k->lo), k->target);
		WALK_LOWER(tkeep)(e, r);
		WALK_LOWER(jump)(e, dflt);
		return;
	}
	uint32_t left = mid ? walk_label_new(e) : dflt;
	WALK_LOWER(cmpbr)(e, LEX_TOKEN_LESS, w, sg, v, codegen_value_constant(k->lo), left);
	WALK_LOWER(tkeep)(e, r);
	WALK_LOWER(cmpbr)(e, LEX_TOKEN_LESS_EQUAL, w, sg, v, codegen_value_constant(k->hi), k->target);
	WALK_LOWER(tkeep)(e, r);
	walk_switch_tree_emit(e, k + 1, n - mid - 1, r, w, sg, dflt);
	if (mid) {
		walk_label_place(e, left);
		walk_switch_tree_emit(e, cases, mid, r, w, sg, dflt);
	}
}

/* Evaluate once; small and wide switches retain their comparison chain. */
static void walk_switch_emit(codegen_context *e, uint32_t n)
{
	if (walk_switch_values_emit(e,n)) return;
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;
	const node *x = N(n);
	ast_qualified_type ct = N(x->a)->ty;
	int w = walk_type_size(c, ct) > 4 ? 8 : 4;
	bool sg = walk_type_is_signed(c, ct);
	uint32_t c0 = W->case_count, lbrk = walk_label_new(e), ldef = lbrk;
	codegen_value v;
	int r;

	if (!codegen_expression_has_side_effects(c, x->a) && !walk_type_is_in_memory(e, ct) && walk_expression_is_constant(e, x->a)) {
		/* a constant (switch (sizeof(x)) in the kernel's macros): only the selected case,
		   the others dead as clang -O2 drops them (their __bad_size() calls) */
		bool on0 = W->switch_is_constant;
		uint32_t sel0 = W->switch_selected_case, sel = 0;
		i128 k;

		v = walk_value_emit(e, x->a);
		if (v.kind == CODEGEN_VALUE_CONSTANT) {
			k = codegen_integer_truncate(c, v.integer, ct);
			walk_cases_collect(e, x->b);
			for (uint32_t i = c0; i < W->case_count && !sel; i++)
				if (N(W->cases[i].node)->op == AST_NODE_CASE && W->cases[i].low <= k && k <= W->cases[i].high)
					sel = W->cases[i].node;
			for (uint32_t i = c0; i < W->case_count && !sel; i++)
				if (N(W->cases[i].node)->op == AST_NODE_DEFAULT)
					sel = W->cases[i].node;
			W->case_count = c0;
			if (W->loop_count == WALK_LOOP_MAX)
				codegen_fatal_report(e, x->loc, "statements nested too deeply");
			W->loop[W->loop_count++] = (walk_loop){ lbrk, 0, W->cleanup_count };
			W->switch_is_constant = true, W->switch_selected_case = sel;
			WALK_LOWER(jump)(e, sel ? walk_case_label_get(e, sel) : lbrk);	/* (a user label before it is no fall-through) */
			e->is_reachable = false;
			walk_statement_emit(e, x->b);
			W->switch_is_constant = on0, W->switch_selected_case = sel0;
			W->loop_count--;
			WALK_LOWER(label)(e, lbrk);
			return;
		}
		walk_value_free(e, &v);
	}
	v = walk_value_emit(e, x->a);
	r = walk_value_to_register(e, &v, ct);

	walk_cases_collect(e, x->b);
	uint32_t count = W->case_count - c0;
	bool bitset = false;
	if (count >= 4 && count <= 65 && e->unit->options->opt && !e->unit->cleanups_disabled &&
	    !walk_type_is_in_memory(e, ct) && !codegen_type_is_bitint(c, ct)) {
		em_switch_bitset_recipe recipe;
		bitset = em_switch_bitset_plan(c, x->b, count, (unsigned)w * 8, sg, &recipe);
		if (bitset) {
			/* Discover the default only after the bounded proof succeeds. */
			for (uint32_t i = c0; i < W->case_count; i++)
				if (N(W->cases[i].node)->op == AST_NODE_DEFAULT)
					ldef = walk_case_label_get(e, W->cases[i].node);
			WALK_LOWER(switch_bitset)(e, r, w, &recipe, walk_case_label_get(e, recipe.node), ldef);
		}
	}
	bool table = false;
	if (!bitset && e->unit->options->opt >= 1 && !e->unit->cleanups_disabled && !c->o->no_jump_tables && !walk_type_is_in_memory(e, ct) &&
	    !codegen_type_is_bitint(c, ct) && count >= 4) {
		uint32_t nc = 0;
		for (uint32_t i = c0; i < W->case_count; i++)
			nc += N(W->cases[i].node)->op != AST_NODE_DEFAULT;
		em_switch_case *cases = nc >= 4 ? malloc(nc * sizeof *cases) : NULL;
		uint32_t j = 0;
		bool ranges = false;
		for (uint32_t i = c0; cases && i < W->case_count; i++) {
			walk_case *k = &W->cases[i];
			uint32_t l = walk_case_label_get(e, k->node);
			if (N(k->node)->op == AST_NODE_DEFAULT)
				ldef = l;
			else {
				cases[j++] = (em_switch_case){ k->low, k->high, l };
				ranges |= k->low != k->high;
			}
		}
		if (cases && em_switch_prepare(cases, j, (unsigned)w * 8, sg) &&
		    em_switch_strategy(j, cases[0].lo, cases[j - 1].hi, ranges, true) == EM_SWITCH_TABLE) {
			if (e->is_reachable)
				WALK_LOWER(switch_table)(e, r, w, cases, j, ldef);
			table = true;
		}
		free(cases);
	}
	if (e->unit->options->opt && !e->unit->cleanups_disabled && count >= 8)
		for (uint32_t i = c0; i < W->case_count; i++)
			count -= N(W->cases[i].node)->op == AST_NODE_DEFAULT;
	bool tree = !bitset && !table && e->unit->options->opt >= 1 && !e->unit->cleanups_disabled &&
		em_switch_strategy(count, 0, 0, false, false) == EM_SWITCH_TREE && !walk_type_is_in_memory(e, ct);
	if (tree) {
		em_switch_case *cases = malloc(count * sizeof *cases);
		uint32_t j = 0;
		for (uint32_t i = c0; cases && i < W->case_count; i++) {
			walk_case *k = &W->cases[i];
			uint32_t l = walk_case_label_get(e, k->node);
			if (N(k->node)->op == AST_NODE_DEFAULT)
				ldef = l;
			else
				cases[j++] = (em_switch_case){ k->low, k->high, l };
		}
		/* A default label is not a comparison. */
		tree = cases && em_switch_prepare(cases, j, (unsigned)w * 8, sg);
		if (tree)
			walk_switch_tree_emit(e, cases, j, r, w, sg, ldef);
		free(cases);
	}
	for (uint32_t i = c0; !tree && !bitset && !table && i < W->case_count; i++) {
		walk_case *k = &W->cases[i];
		uint32_t l = walk_case_label_get(e, k->node);

		if (N(k->node)->op == AST_NODE_DEFAULT) {
			ldef = l;
			continue;
		}
		if (walk_type_is_in_memory(e, ct)) {
			/* __int128: each test a 0/1 value (the value is an object, r its address) */
			codegen_value t = codegen_type_is_bitint_big(c, ct) ? WALK_LOWER(bigcmp)(e, k->low == k->high ? LEX_TOKEN_EQUAL_EQUAL : LEX_TOKEN_GREATER_EQUAL, ct, (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }, codegen_value_constant(k->low)) : WALK_LOWER(wcmp)(e, k->low == k->high ? LEX_TOKEN_EQUAL_EQUAL : LEX_TOKEN_GREATER_EQUAL, sg, (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }, codegen_value_constant(k->low));
			uint32_t skip = walk_label_new(e);

			WALK_LOWER(tkeep)(e, r);
			if (k->low == k->high) {
				WALK_LOWER(brz)(e, t, 4, true, l);
				continue;
			}
			WALK_LOWER(brz)(e, t, 4, false, skip);
			t = codegen_type_is_bitint_big(c, ct) ? WALK_LOWER(bigcmp)(e, LEX_TOKEN_LESS_EQUAL, ct, (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }, codegen_value_constant(k->high)) : WALK_LOWER(wcmp)(e, LEX_TOKEN_LESS_EQUAL, sg, (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }, codegen_value_constant(k->high));
			WALK_LOWER(tkeep)(e, r);
			WALK_LOWER(brz)(e, t, 4, true, l);
			walk_label_place(e, skip);
		} else if (k->low == k->high) {
			WALK_LOWER(cmpbr)(e, LEX_TOKEN_EQUAL_EQUAL, w, sg, (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }, codegen_value_constant(k->low), l);
			WALK_LOWER(tkeep)(e, r);
		} else {
			/* lo <= v <= hi */
			uint32_t skip = walk_label_new(e);

			WALK_LOWER(cmpbr)(e, LEX_TOKEN_LESS, w, sg, (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }, codegen_value_constant(k->low), skip);
			WALK_LOWER(tkeep)(e, r);
			WALK_LOWER(cmpbr)(e, LEX_TOKEN_LESS_EQUAL, w, sg, (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }, codegen_value_constant(k->high), l);
			WALK_LOWER(tkeep)(e, r);
			walk_label_place(e, skip);
		}
	}
	W->case_count = c0;
	WALK_LOWER(tfree)(e, r);
	if (!tree && !bitset && !table)
		WALK_LOWER(jump)(e, ldef);
	if (W->loop_count == WALK_LOOP_MAX)
		codegen_fatal_report(e, x->loc, "statements nested too deeply");
	W->loop[W->loop_count++] = (walk_loop){ lbrk, 0, W->cleanup_count };
	{
		bool on0 = W->switch_is_constant;

		W->switch_is_constant = false;	/* (its cases are this switch's) */
		walk_statement_emit(e, x->b);
		W->switch_is_constant = on0;
	}
	W->loop_count--;
	walk_label_place(e, lbrk);
}

/* ---------------- statements ---------------- */

static void walk_loop_push(codegen_context *e, uint32_t brk, uint32_t cont)
{
	walk_state *W = e->walk_state;

	if (W->loop_count == WALK_LOOP_MAX)
		codegen_fatal_report(e, e->current_location, "statements nested too deeply");
	W->loop[W->loop_count++] = (walk_loop){ brk, cont, W->cleanup_count };
}

VPRIV void walk_goto_emit(codegen_context *e, uint32_t label)
{
	walk_state *W = e->walk_state;

	if (W->cleanup_count) {
		/* the cleanups of the scopes left: those not around the target label */
		uint32_t t = W->label_cleanup_generations[label] == W->generation ? W->label_cleanup_nodes[label] : 0;
		int k = W->cleanup_count;

		for (; k > 0; k--) {
			uint32_t s2 = W->cleanups[k - 1], id = W->cleanup_symbol_generations[s2] == W->generation ? W->cleanup_symbol_nodes[s2] : 0, a = t;

			while (a && a != id)
				a = W->cleanup_nodes[a].parent;
			if (id && a == id)
				break;
		}
		if (W->inline_call && k < W->inline_call->cleanup_base)
			k = W->inline_call->cleanup_base;
		walk_cleanups_run(e, k);
	}
	WALK_LOWER(jump)(e, walk_user_label_find(e, label));
}

/* Entering a label in a discarded branch bypasses every preceding statement.
   Keep that suffix, including its normal fallthrough, without retaining dead
   calls before the first label. Unknown containers remain conservative. */
static bool walk_statement_entries_emit(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	if (!walk_branch_entry_check(c, n)) return false;
	const node *x = N(n);
	if (x->op == AST_NODE_BLOCK) {
		/* Declarations carry cleanup/VLA scope bookkeeping even when their
		   initialization is bypassed; retain the normal block traversal. */
		for (uint32_t i = 0; i < x->c; i++)
			if (N(c->lists[x->b + i])->op == AST_NODE_DECLARATION) {
				walk_statement_emit(e, n);
				return true;
			}
		bool live = false;
		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t child = c->lists[x->b + i];
			if (live) walk_statement_emit(e, child);
			else live = walk_statement_entries_emit(e, child);
		}
		return live;
	}
	if (x->op == AST_NODE_IF && !walk_branch_entry_check(c, x->a)) {
		uint32_t end = walk_label_new(e);
		bool left = walk_statement_entries_emit(e, x->b);
		if (left) WALK_LOWER(jump)(e, end);
		bool right = walk_statement_entries_emit(e, x->c);
		if (left || right) walk_label_place(e, end);
		return left || right;
	}
	walk_statement_emit(e, n);
	return true;
}

static void walk_constant_if_emit(codegen_context *e, const node *x, bool truth)
{
	cc *c = e->compile_context;
	uint32_t live = truth ? x->b : x->c, dead = truth ? x->c : x->b;
	if (!walk_branch_entry_check(c, dead)) {
		walk_statement_emit(e, live);
		return;
	}
	uint32_t join = walk_label_new(e), other = walk_label_new(e);
	if (truth) {
		walk_statement_emit(e, live);
		WALK_LOWER(jump)(e, join);
		walk_statement_entries_emit(e, dead);
	} else {
		WALK_LOWER(jump)(e, other);
		walk_statement_entries_emit(e, dead);
		WALK_LOWER(jump)(e, join);
		walk_label_place(e, other);
		walk_statement_emit(e, live);
	}
	walk_label_place(e, join);
}

static void walk_statement_emit_inner(codegen_context *e, uint32_t n);

VPRIV void walk_statement_emit(codegen_context *e, uint32_t n)
{
	walk_state *W = e->walk_state;
	uint32_t mark = W->temporary_slot_in_use_count;

	W->statement_depth++;
	walk_statement_emit_inner(e, n);
	W->statement_depth--;
	/* the statement's temporaries are free for the next ones */
	while (W->temporary_slot_in_use_count > mark && W->temporary_slot_free_count < WALK_TEMPORARY_SLOT_MAX)
		W->temporary_slots_free[W->temporary_slot_free_count++] = W->temporary_slots_in_use[--W->temporary_slot_in_use_count];
	W->temporary_slot_in_use_count = mark;
}

static void walk_statement_emit_inner(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;
	const node *x;

	if (!n)
		return;
	x = N(n);
	if (!e->is_reachable && !(e->unit->node_flags[n] & CODEGEN_NODE_HAS_LABEL) && x->op != AST_NODE_BLOCK)
		return;	/* unreachable and no label or case in it: nothing to generate */
	e->current_location = x->loc;
	if (unlikely(e->unit->options->debug) && x->op != AST_NODE_BLOCK)
		WALK_LOWER(dbgrow)(e, x->loc);
	switch (x->op) {
	case AST_NODE_BLOCK: {
		uint32_t vs = 0;	/* the block's first VLA: its saved sp is restored at the end */
		int cmark = W->cleanup_count, vmark = W->vla_count;

		for (uint32_t i = 0; i < x->c; i++) {
			const node *d = N(c->lists[x->b + i]);

			if (!vs && d->op == AST_NODE_DECLARATION && c->syms[d->a].kind == AST_SYMBOL_VARIABLE && c->syms[d->a].sc != AST_STORAGE_TYPEDEF &&
			    c->syms[d->a].sc != AST_STORAGE_STATIC && c->syms[d->a].sc != AST_STORAGE_EXTERN && (T(c, c->syms[d->a].ty)->flags & AST_TYPE_FLAG_VLA))
				vs = d->a;
			uint32_t from, to;
			if (i && em_block_value_reuse(e, x, 0, i, &from, &to)) {
				uint32_t oldfrom = e->reuse_from, oldto = e->reuse_to;
				e->reuse_from = from; e->reuse_to = to;
				walk_statement_emit(e, c->lists[x->b + i]);
				e->reuse_from = oldfrom; e->reuse_to = oldto;
			} else {
				walk_statement_emit(e, c->lists[x->b + i]);
			}
			if (d->op == AST_NODE_DECLARATION && c->syms[d->a].kind == AST_SYMBOL_VARIABLE && c->syms[d->a].sc != AST_STORAGE_TYPEDEF &&
			    c->syms[d->a].sc != AST_STORAGE_STATIC && c->syms[d->a].sc != AST_STORAGE_EXTERN && (T(c, c->syms[d->a].ty)->flags & AST_TYPE_FLAG_VLA)) {
				if (W->vla_count == WALK_CLEANUP_MAX) codegen_fatal_report(e, d->loc, "too many cleanup variables in scope");
				W->vla[W->vla_count++] = d->a;
			}
		}
		W->vla_count = vmark;
		if (W->cleanup_count > cmark) {
			walk_cleanups_run(e, cmark);
			W->cleanup_count = cmark;
		}
		if (vs) {
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_local_slot(e, vs) + 8, .align = 8 };
			int r = WALK_LOWER(treg)(e);

			WALK_LOWER(load)(e, r, &l, 8, false);
			WALK_LOWER(spset)(e, r);
		}
		return;
	}
	case AST_NODE_EXPRESSION:
		if (x->a && e->unit->options->opt && !codegen_expression_has_side_effects(c, x->a)) {
			/* a plain load whose value is dropped: not even made, as clang -O2 ((void)RCU) */
			uint32_t t = x->a;

			while (N(t)->op == AST_NODE_CAST && N(t)->a)
				t = N(t)->a;
			if (N(t)->op == AST_NODE_VARIABLE || N(t)->op == AST_NODE_NUMBER)
				return;
		}
		if (x->a) {
			codegen_value v = walk_value_emit(e, x->a);

			walk_value_free(e, &v);
		}
		return;
	case AST_NODE_DECLARATION:
		walk_declaration_emit(e, x);
		return;
	case AST_NODE_IF: {
		cc_value cv;
		int known = walk_condition_fold(e, x->a);
		if (known >= 0) {
			walk_constant_if_emit(e, x, known);
			return;
		}
		codegen_value test = codegen_value_none();
		uint32_t tc = x->a;

		while ((N(tc)->op == AST_NODE_CAST && N(tc)->a) || (N(tc)->op == AST_NODE_UNARY && N(tc)->aux == LEX_TOKEN_EXCLAMATION))
			tc = N(tc)->a;
		/* (a call too: an inlined function or a folded builtin may give a constant,
		   if (!__builtin_strcmp(op, "<")), if (dax_mapping(m))) */
		if ((cc_eval(c, x->a, &cv) && cv.kind == CC_VALUE_INTEGER) || walk_expression_is_constant(e, x->a) || N(tc)->op == AST_NODE_CALL ||
		    (N(tc)->op == AST_NODE_STATEMENT_EXPRESSION && !(e->unit->node_flags[tc] & CODEGEN_NODE_HAS_LABEL))) {	/* (WARN_ON_ONCE(1): a constant from a statement expression) */
			/* Front-end constant_p can change after binding inline arguments.
			   Trust the generated value and retain condition effects exactly once. */
			test = walk_value_emit(e, x->a);
			if (test.kind == CODEGEN_VALUE_CONSTANT) {
				walk_constant_if_emit(e, x, test.integer != 0);
				return;
			}
		}
		uint32_t lelse = walk_label_new(e), lend = walk_label_new(e);

		if (test.kind == CODEGEN_VALUE_NONE) {
			e->walk_state->condition_jumped = false;
			walk_condition_emit(e, x->a, false, lelse);
			if (e->walk_state->condition_jumped && e->is_reachable && !(e->unit->node_flags[x->b] & CODEGEN_NODE_HAS_LABEL)) {
				/* false whatever its operands (0 << x != 0, a dead && operand): the then branch is dead */
				e->walk_state->condition_jumped = false;
				walk_label_place(e, lelse);
				if (x->c)
					walk_statement_emit(e, x->c);
				walk_label_place(e, lend);
				return;
			}
		} else {
			WALK_LOWER(brz)(e, walk_value_convert(e, test, N(x->a)->ty, codegen_type_primitive(AST_TYPE_BOOL)), 4, false, lelse);
		}
		walk_statement_emit(e, x->b);
		if (x->c) {
			WALK_LOWER(jump)(e, lend);
			walk_label_place(e, lelse);
			walk_statement_emit(e, x->c);
			walk_label_place(e, lend);
		} else {
			walk_label_place(e, lelse);
		}
		return;
	}
	case AST_NODE_WHILE: {
		uint32_t lhead = walk_label_new(e), lbrk = walk_label_new(e);

		walk_label_place(e, lhead);
		walk_condition_emit(e, x->a, false, lbrk);
		walk_loop_push(e, lbrk, lhead);
		walk_statement_emit(e, x->b);
		W->loop_count--;
		WALK_LOWER(jump)(e, lhead);
		walk_label_place(e, lbrk);
		return;
	}
	case AST_NODE_DO: {
		uint32_t lhead = walk_label_new(e), lcont = walk_label_new(e), lbrk = walk_label_new(e);

		walk_label_place(e, lhead);
		walk_loop_push(e, lbrk, lcont);
		walk_statement_emit(e, x->b);
		W->loop_count--;
		walk_label_place(e, lcont);
		walk_condition_emit(e, x->a, true, lhead);
		walk_label_place(e, lbrk);
		return;
	}
	case AST_NODE_FOR: {
		uint32_t lhead = walk_label_new(e), lcont = walk_label_new(e), lbrk = walk_label_new(e);
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int cmark = W->cleanup_count;	/* a cleanup variable declared in the first clause: the loop's scope */

		if (x->a && N(x->a)->op == AST_NODE_BLOCK)	/* declarations: in scope until the loop ends */
			for (uint32_t i = 0; i < N(x->a)->c; i++)
				walk_statement_emit(e, c->lists[N(x->a)->b + i]);
		else
			walk_statement_emit(e, x->a);
		walk_label_place(e, lhead);
		if (x->b)
			walk_condition_emit(e, x->b, false, lbrk);
		walk_loop_push(e, lbrk, lcont);
		walk_statement_emit(e, body);
		W->loop_count--;
		walk_label_place(e, lcont);
		if (step) {
			codegen_value v = walk_value_emit(e, step);

			walk_value_free(e, &v);
		}
		WALK_LOWER(jump)(e, lhead);
		walk_label_place(e, lbrk);
		if (W->cleanup_count > cmark) {
			walk_cleanups_run(e, cmark);
			W->cleanup_count = cmark;
		}
		return;
	}
	case AST_NODE_SWITCH:
		walk_switch_emit(e, n);
		return;
	case AST_NODE_CASE: case AST_NODE_DEFAULT:
		if (!W->switch_is_constant || n == W->switch_selected_case)
			walk_label_place(e, walk_case_label_get(e, n));	/* (a constant switch: the selected case only) */
		walk_statement_emit(e, x->c);
		return;
	case AST_NODE_LABEL:
		walk_label_place(e, walk_user_label_find(e, x->a));
		if (c->labels[x->a].addr_taken)
			WALK_LOWER(named)(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_LABEL, x->a));	/* &&label: .Ltmp.lbl.N, as data initializers name it */
		walk_statement_emit(e, x->c);
		return;
	case AST_NODE_GOTO:
		walk_goto_emit(e, x->a);
		e->is_reachable = false;	/* (what follows is dead until a label: not emitted) */
		return;
	case AST_NODE_BREAK: case AST_NODE_CONTINUE: {
		int i = W->loop_count - 1;

		while (i >= 0 && x->op == AST_NODE_CONTINUE && !W->loop[i].continue_label)
			i--;
		if (i < 0)
			codegen_fatal_report(e, x->loc, "break/continue outside a loop");
		walk_cleanups_run(e, W->loop[i].cleanup_depth);
		WALK_LOWER(jump)(e, x->op == AST_NODE_BREAK ? W->loop[i].break_label : W->loop[i].continue_label);
		e->is_reachable = false;
		return;
	}
	case AST_NODE_RETURN:
		if (WALK_LOWER(tail_return)(e, x->a)) {
			e->is_reachable = false;
			return;
		}
		if (W->inline_call) {
			/* an inlined call's return: the value to its slot, then past the body */
			walk_inline_call *I = W->inline_call;

			if (x->a && KIND(c, I->return_type) != AST_TYPE_VOID) {
				codegen_value v = walk_value_emit(e, x->a);
				codegen_lvalue t = { .kind = CODEGEN_LVALUE_FRAME, .offset = I->return_slot, .align = 16 };

				if (v.kind == CODEGEN_VALUE_CONSTANT && (!I->any_return || (I->constant_is_ok && I->constant.integer == v.integer)) && !walk_type_is_in_memory(e, I->return_type) && !codegen_type_is_float(c, I->return_type)) {
					I->constant = v;
				} else {
					I->constant_is_ok = false;
				}
				I->any_return = true;
				if (walk_type_is_in_memory(e, I->return_type)) {
					v = walk_lvalue_store_memory(e, &t, I->return_type, v);
					walk_value_free(e, &v);
				} else {
					int r = walk_value_to_register(e, &v, I->return_type);

					WALK_LOWER(store)(e, r, &t, walk_type_size(c, I->return_type));
					WALK_LOWER(tfree)(e, r);
				}
			} else if (x->a) {
				codegen_value v = walk_value_emit(e, x->a);

				walk_value_free(e, &v);
			}
			walk_cleanups_run(e, I->cleanup_base);
			WALK_LOWER(jump)(e, I->end_label);
			e->is_reachable = false;
			return;
		}
		if (x->a && walk_type_is_in_memory(e, W->return_type)) {
			/* the object's address; a size not a multiple of 8 through a slot of whole words */
			codegen_value v = walk_value_emit(e, x->a);
			int r = walk_value_to_register(e, &v, e->compile_context->t_size);

			if (W->cleanup_count) {
				/* the cleanups run after the value is taken: a copy, except of a local
				   itself (clang returns it in place: the cleanups' changes are seen) */
				if (!(N(x->a)->op == AST_NODE_VARIABLE && codegen_symbol_is_local_variable(c, N(x->a)->a))) {
					codegen_lvalue t = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, codegen_type_size(c, W->return_type), 16), .align = 16 };
					int rt = WALK_LOWER(laddr)(e, &t);

					WALK_LOWER(copy)(e, rt, r, codegen_type_size(c, W->return_type));
					WALK_LOWER(tfree)(e, r);
					r = rt;
				}
				walk_cleanups_run(e, 0);
			}

			if (!W->return_record.is_in_memory && W->return_record.size % 8) {
				codegen_lvalue t = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, (W->return_record.size + 7) & ~(uint64_t)7, 8), .align = 8 };
				int rt = WALK_LOWER(laddr)(e, &t);

				WALK_LOWER(copy)(e, rt, r, W->return_record.size);
				WALK_LOWER(tfree)(e, r);
				r = rt;
			}
			WALK_LOWER(retrec)(e, r, &W->return_record, W->return_label);
		} else if (x->a && KIND(c, W->return_type) != AST_TYPE_VOID) {
			codegen_value v = walk_value_emit(e, x->a);

			if (W->cleanup_count) {
				/* the value parked while the cleanups run */
				codegen_lvalue t = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 8, 8), .align = 8 };
				int r = walk_value_to_register(e, &v, W->return_type);

				WALK_LOWER(store)(e, r, &t, 8);
				WALK_LOWER(tfree)(e, r);
				walk_cleanups_run(e, 0);
				v = (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)(codegen_type_is_float(c, W->return_type) ? WALK_LOWER(ftreg)(e) : WALK_LOWER(treg)(e)) };
				WALK_LOWER(load)(e, v.register_number, &t, 8, false);
			}
			WALK_LOWER(ret)(e, &v, W->return_type, W->return_label);
		} else {
			if (x->a) {
				codegen_value v = walk_value_emit(e, x->a);

				walk_value_free(e, &v);
			}
			walk_cleanups_run(e, 0);
			WALK_LOWER(ret)(e, NULL, codegen_type_primitive(AST_TYPE_VOID), W->return_label);
		}
		e->is_reachable = false;
		return;
	case AST_NODE_ASM: {
		e->known_values_valid = 0;
		const ast_asm_statement *a = &c->asms[x->a];
		bool empty = a->tmpl && !*c->strs[a->tmpl - 1].s && !a->nout && !a->nin && !a->go;

		for (uint32_t i = 0; empty && i < a->nclob; i++) {
			const char *s = c->strs[a->clob[i] - 1].s;

			empty = !strcmp(s, "memory") || !strcmp(s, "cc");
		}
		if (empty) {
			/* Locals live in memory; the record preserves the sequencing point
			   for passes that will cache loads or schedule instructions. */
			WALK_LOWER(barrier)(e);
			return;
		}
		WALK_LOWER(asm)(e, n);
		return;
	}
	case AST_NODE_GOTO_INDIRECT: {
		codegen_value v = walk_value_emit(e, x->a);

		WALK_LOWER(jumpr)(e, walk_value_to_register(e, &v, e->compile_context->t_size));
		return;
	}
	default:
		codegen_fatal_report(e, x->loc, "x86-64: unsupported statement (node %d)", x->op);
	}
}

/* ---------------- inlining ---------------- */

#define WALK_INLINE_NODE_MAX 40	/* AST nodes: a few statements (as arm64) */

VPRIV bool walk_symbol_constant_get(codegen_context *e, uint32_t s, codegen_value *v)
{
	walk_state *W = e->walk_state;

	if (W->constant_generations[s] != W->generation)
		return false;
	*v = W->constant_values[s];
	return true;
}

/*
 * As clang -O2 sees it: always_inline functions, and small inline ones (the
 * kernel and the gcc tests lean on __builtin_constant_p and folding after
 * inlining).  Not: variadic or unprototyped, recursive, a label address
 * taken (one copy only), deeper than 16.
 */
/* a tiny static function clang would inline: no opaque inline asm, no locals aligned above 16
   (either could need registers the caller's frame holds) */
static bool walk_function_is_tiny(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return true;
	x = N(n);
	switch (x->op) {
	case AST_NODE_ASM:
#ifdef WALK_PREFIX
		/* This template becomes an ordinary register operation in VIR. */
		return virbuild_asm_is_crc32q(c, &c->asms[x->a]);
#else
		return false;
#endif
	case AST_NODE_DECLARATION:
		return c->syms[x->a].kind != AST_SYMBOL_VARIABLE || codegen_symbol_alignment(c, x->a) <= 16;
	case AST_NODE_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (!walk_function_is_tiny(c, c->lists[x->b + i]))
				return false;
		return true;
	case AST_NODE_IF:
		return walk_function_is_tiny(c, x->b) && walk_function_is_tiny(c, x->c);
	case AST_NODE_WHILE: case AST_NODE_DO: case AST_NODE_SWITCH: case AST_NODE_LABEL: case AST_NODE_CASE: case AST_NODE_DEFAULT:
		return walk_function_is_tiny(c, x->op == AST_NODE_LABEL || x->op == AST_NODE_CASE || x->op == AST_NODE_DEFAULT ? x->c : x->b);
	case AST_NODE_FOR:
		return walk_function_is_tiny(c, c->lists[x->c + 1]);
	default:
		return true;
	}
}

VPRIV bool walk_call_can_inline(codegen_context *e, uint32_t s, uint32_t call)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	const ctype *ft = T(c, y->ty);

	if (!(y->flags & AST_SYMBOL_FLAG_DEFINED) || !y->body || (ft->flags & (AST_TYPE_FLAG_VARIADIC | AST_TYPE_FLAG_NO_PROTOTYPE)) || N(y->body)->aux > 64)
		return false;
	if (CODEGEN_IS_X64(c)) {
		uint32_t caller = e->walk_state->inline_call ? e->walk_state->inline_call->function : e->walk_state->function_symbol;
		x86_features need = cc_x86_features_get(c, s), have = cc_x86_features_get(c, caller);
		if (!x86_features_subset(need, have)) {
			if (y->flags & AST_SYMBOL_FLAG_ALWAYS_INLINE)
				for (unsigned bit = 0; bit < X86_FEATURE_COUNT; bit++)
					if (x86_feature_has(need, bit) && !x86_feature_has(have, bit))
						codegen_fatal_report(e, e->current_location, "always_inline function requires target feature '%s', but the caller is compiled without it", x86_feature_names[bit]);
			return false;
		}
	}
	for (uint32_t i = 0; i < N(y->body)->aux; i++)
		if (codegen_type_is_variably_modified(c, c->syms[c->lists[N(y->body)->a + i]].ty))
			return false;	/* (a parameter's VLA sizes) */
	if (!(y->flags & AST_SYMBOL_FLAG_ALWAYS_INLINE)) {
		uint32_t lim = WALK_INLINE_NODE_MAX;

		if (!e->unit->options->opt)
			return false;
		if ((y->flags & AST_SYMBOL_FLAG_NOINLINE) || (!(y->flags & AST_SYMBOL_FLAG_INLINE) && y->sc != AST_STORAGE_STATIC))
			return false;
#ifdef WALK_PREFIX
		/* VIR: per call site by the callee's size with the callees it inlines itself
		   (codegen_inline_size, bottom-up as LLVM), so nested copies cannot grow exponentially; a
		   static function called once is its only copy, so inlined up to a large body
		   (clang's last-call-to-static bonus) */
		if (e->vir_try && e->unit->inline_sizes) {
			uint32_t caller=e->walk_state->inline_call?e->walk_state->inline_call->function:e->walk_state->function_symbol;
			/* Keep the ordinary/context budget when the caller already
			 * exceeds the large-copy budget: even one copy can grow its
			 * allocation unit and register pressure substantially. */
			bool once;
			uint32_t size;

			codegen_function_walk_ensure(e->unit, c, caller);
			once = (e->unit->once[s] & 1) && y->sc == AST_STORAGE_STATIC && !codegen_symbol_attributes(c, s)->used &&
			       e->unit->function_node_counts[caller]<=WALK_INLINE_ONCE_MAX;
			lim = once ? WALK_INLINE_ONCE_MAX : WALK_INLINE_VIR_MAX;
			/* codegen_inline_size includes the same constant-argument sites bottom-up,
			 * so selected nested copies are included in the caller's bound. */
			size = once ? 0 : codegen_inline_size(e->unit, c, s);
			if(!once && size>WALK_INLINE_VIR_MAX &&
			   size<=WALK_INLINE_CONSTANT_FACTOR*WALK_INLINE_VIR_MAX && codegen_inline_has_constant_argument(e->unit,c,s,call))
				lim=WALK_INLINE_CONSTANT_FACTOR*WALK_INLINE_VIR_MAX;
			codegen_function_prepass_request(e, s);
			if (!once && size > lim)
				return false;
		}
#endif
		codegen_function_prepass_request(e, s);
		if (e->unit->function_node_counts[s] > lim)
			return false;	/* (a tiny static function too, as clang: an empty stub drops its arguments) */
		if (!(y->flags & AST_SYMBOL_FLAG_INLINE) && !walk_function_is_tiny(c, y->body))
			return false;
	}
	codegen_function_walk_ensure(e->unit, c, s);
	if (e->unit->symbol_flags[s] & CODEGEN_SYMBOL_NO_INLINE)
		return false;
#ifdef WALK_PREFIX
	if (e->vir_try && e->unit->once && (e->unit->once[s] & 2))
		return false;	/* (calls setjmp: prepass.c) */
#endif
	if (s == e->walk_state->function_symbol || e->walk_state->depth >= 16)
		return false;
#ifdef WALK_PREFIX
	if (e->vir_try && e->unit->inline_sizes &&
	    codegen_recursion_group(e->unit, c, s) == codegen_recursion_group(e->unit, c, e->walk_state->inline_call ? e->walk_state->inline_call->function : e->walk_state->function_symbol))
		return false;	/* (its own recursion group: codegen_recursion_group) */
#endif
	for (walk_inline_call *I = e->walk_state->inline_call; I; I = I->outer)
		if (I->function == s)
			return false;
	return true;
}

VPRIV codegen_value walk_call_inline(codegen_context *e, uint32_t n, uint32_t s)
{
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;
	const node *x = N(n), *body = N(c->syms[s].body);
	walk_inline_call I = { .outer = W->inline_call, .function = s, .call = n, .end_label = walk_label_new(e), .return_type = T(c, c->syms[s].ty)->base, .cleanup_base = W->cleanup_count,
		    .constant_is_ok = true, .vla_base = W->vla_count };
	uint32_t inst0 = W->instance;
	int64_t saved_sp = 0;
	codegen_value res;

	uint32_t lmark = W->local_slot_count;

	memcpy(I.caller_array_elements, W->array_elements, sizeof I.caller_array_elements), I.caller_array_element_count = W->array_element_count;
	codegen_function_prepass_request(e, s);
	if (KIND(c, I.return_type) != AST_TYPE_VOID)
		I.return_slot = walk_slot_temporary(e, codegen_type_size(c, I.return_type) ? codegen_type_size(c, I.return_type) : 8, codegen_type_alignment(c, I.return_type) < 8 ? 8 : codegen_type_alignment(c, I.return_type));
	/* the arguments in order, each parked (a constant kept), then bound to the parameters: an
	   argument that inlines the same function again cannot overwrite them */
	{
		codegen_value av[64];
		int64_t as[64];

		for (uint32_t i = 0; i < body->aux; i++) {
			uint32_t p = c->lists[body->a + i];
			ast_qualified_type pt = c->syms[p].ty;
			/* an argument the copy never reads, without side effects: not evaluated, as clang
			   (the kernel's __acquire_shared_ctx_lock(RCU): RCU is an extern never defined) */
			codegen_value v = i < x->c && ((e->unit->symbol_flags[p] & CODEGEN_SYMBOL_USED) || codegen_expression_has_side_effects(c, c->lists[x->b + i]) ||
					     walk_type_is_in_memory(e, pt) || codegen_type_is_float(c, pt))
				? walk_value_emit(e, c->lists[x->b + i]) : codegen_value_constant(0);

			as[i] = 0;
			/* (an address constant, &key, stays one too: an asm "i" operand in the copy) */
			bool kaddr = v.kind == CODEGEN_VALUE_ADDRESS && codegen_type_is_pointer(c, pt) && !walk_symbol_address_may_be_zero(c, v);

			if ((v.kind != CODEGEN_VALUE_CONSTANT && !kaddr) || walk_type_is_in_memory(e, pt) || codegen_type_is_float(c, pt)) {
				codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, codegen_type_size(c, pt) ? codegen_type_size(c, pt) : 8, codegen_type_alignment(c, pt) < 8 ? 8 : codegen_type_alignment(c, pt)), .align = 8 };

				as[i] = (int64_t)l.offset;
				if (walk_type_is_in_memory(e, pt)) {
					v = walk_lvalue_store_memory(e, &l, pt, v);
					walk_value_free(e, &v);
				} else {
					int r = walk_value_to_register(e, &v, pt);

					WALK_LOWER(store)(e, r, &l, walk_type_size(c, pt));
					WALK_LOWER(tfree)(e, r);
				}
			}
			av[i] = v;
		}
		W->inline_local_depth++;
		for (uint32_t i = 0; i < body->aux; i++) {
			uint32_t p = c->lists[body->a + i];
			ast_qualified_type pt = c->syms[p].ty;
			codegen_lvalue l, t = { .kind = CODEGEN_LVALUE_FRAME, .offset = as[i], .align = 8 };
			codegen_value v;

			if (!as[i] && !(e->unit->symbol_flags[p] & (CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_CONSTANT_WRITTEN)) && !(AST_QUALIFIED_TYPE_QUALIFIERS(pt) & AST_QUALIFIER_VOLATILE) &&
			    !codegen_symbol_attributes(c, p)->cleanup) {
				W->constant_generations[p] = W->generation;
				W->constant_values[p] = av[i].kind == CODEGEN_VALUE_ADDRESS ? av[i] : codegen_value_constant(codegen_integer_truncate(c, av[i].integer, pt));
				continue;
			}
			/* the home the copy's body reads: a lowering's variable of the copy's own instance
			   (VIR), else the frame slot */
			W->instance = W->instance_count + 1;
			if (!WALK_LOWER(local)(e, p, &l))
				l = (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = walk_local_slot(e, p), .align = codegen_symbol_alignment(c, p) };
			W->instance = inst0;
			W->constant_generations[p] = 0;
			if ((av[i].kind == CODEGEN_VALUE_CONSTANT || (av[i].kind == CODEGEN_VALUE_ADDRESS && codegen_type_is_pointer(c, pt) && !walk_symbol_address_may_be_zero(c, av[i]))) && !walk_type_is_in_memory(e, pt) &&
			    !codegen_type_is_float(c, pt) && W->parameter_constant_count < 16) {
				int k;

				for (int j = 0; j < W->parameter_constant_count; j++)	/* (an earlier copy's binding) */
					if (W->parameter_constants[j].symbol == p)
						W->parameter_constants[j] = W->parameter_constants[--W->parameter_constant_count], j--;
				k = W->parameter_constant_count++;
				W->parameter_constants[k].symbol = p, W->parameter_constants[k].generation = W->generation, W->parameter_constants[k].instance = 0, W->parameter_constants[k].value = av[i];
			}
			v = as[i] ? walk_lvalue_reload(e, &t, pt) : av[i];
			if (walk_type_is_in_memory(e, pt)) {
				v = walk_lvalue_store_memory(e, &l, pt, v);
				walk_value_free(e, &v);
			} else {
				int r = walk_value_to_register(e, &v, pt);

				WALK_LOWER(store)(e, r, &l, walk_type_size(c, pt));
				WALK_LOWER(tfree)(e, r);
			}
		}
	}
	{
		/* the body's cleanup scopes and VLAs, under the caller's innermost cleanup */
		uint32_t top = 0;

		if (W->cleanup_count) {
			uint32_t cs = W->cleanups[W->cleanup_count - 1];

			top = W->cleanup_symbol_generations[cs] == W->generation ? W->cleanup_symbol_nodes[cs] : 0;
		}
		walk_vla_scan(e, c->syms[s].body, &top);
	}
	/* alloca belongs to this function instance, including its early returns. */
	if (e->unit->node_flags[c->syms[s].body] & CODEGEN_NODE_HAS_ALLOCA) {
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = saved_sp = walk_slot_temporary(e, 8, 8), .align = 8 };
		int r = WALK_LOWER(spget)(e);
		WALK_LOWER(store)(e, r, &l, 8);
		WALK_LOWER(tfree)(e, r);
	}
	W->instance = ++W->instance_count;
	W->inline_call = &I;
	W->depth++;
	walk_statement_emit(e, c->syms[s].body);
	W->depth--;
	W->inline_local_depth--;
	/* the copy's locals and parameters are dead: their slots free, the symbols unbound */
	while (W->local_slot_count > lmark) {
		struct walk_local_slot_entry l = W->local_slots[--W->local_slot_count];

		W->symbol_generations[l.symbol] = 0;
		if (W->temporary_slot_free_count < WALK_TEMPORARY_SLOT_MAX)
			W->temporary_slots_free[W->temporary_slot_free_count++] = (struct walk_temporary_slot){ l.offset, l.size, l.align };
	}
	W->inline_call = I.outer;
	W->instance = inst0;
	walk_label_place(e, I.end_label);
	if (saved_sp) {
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = saved_sp, .align = 8 };
		int r = WALK_LOWER(treg)(e);
		WALK_LOWER(load)(e, r, &l, 8, false);
		WALK_LOWER(spset)(e, r);
	}
	/* the callee cannot touch the caller's non-escaping local arrays: their known
	   elements (the snapshot at the call) hold past the copy's labels */
	memcpy(W->array_elements, I.caller_array_elements, sizeof I.caller_array_elements), W->array_element_count = I.caller_array_element_count;
	if (KIND(c, I.return_type) == AST_TYPE_VOID)
		return codegen_value_constant(0);
	if (I.any_return && I.constant_is_ok)
		return I.constant;
	{
		codegen_lvalue t = { .kind = CODEGEN_LVALUE_FRAME, .offset = I.return_slot, .align = 16 };

		if (walk_type_is_in_memory(e, I.return_type))
			return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)WALK_LOWER(laddr)(e, &t) };
		res = (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)(codegen_type_is_float(c, I.return_type) ? WALK_LOWER(ftreg)(e) : WALK_LOWER(treg)(e)) };
		WALK_LOWER(load)(e, res.register_number, &t, walk_type_size(c, I.return_type), walk_type_is_signed(c, I.return_type));
		return res;
	}
}

/* ---------------- the function ---------------- */

static bool walk_function_is_main(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	return y->depth == 0 && y->sc != AST_STORAGE_STATIC && CODEGEN_IDENTIFIER_IS(y->name, "main");
}

VPRIV void walk_function_emit(codegen_context *e, uint32_t s)
{
	em_switch_values_reset(e);
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	const ctype *ft = T(c, y->ty);
	const node *body = N(y->body);
	walk_state *W = walk_state_get(e);
	walk_argument pabuf[256];
	walk_argument *pa = body->aux > sizeof pabuf / sizeof *pabuf ?
		malloc((size_t)body->aux * sizeof *pa) : pabuf;

	codegen_function_prepass_request(e, s);
	W->generation++;
	W->frame_offset = 0;
	W->frame_alignment = 16;
	W->temporary_slot_in_use_count = W->temporary_slot_free_count = W->statement_depth = W->local_slot_count = W->inline_local_depth = 0;
	W->label_count = 0;
	W->loop_count = 0;
	W->case_count = 0;
	W->return_type = ft->base;
	W->function_symbol = s;
	e->known_value_count = e->array_element_count = e->known_values_valid = 0;
	W->return_record = (walk_argument){ 0 };
	W->cleanup_count = W->vla_count = 0;
	W->instance = 0;
	W->inline_call = NULL;
	W->depth = 0;
	e->top_symbol = s;
	e->is_reachable = true;
	e->has_calls = false;
	e->current_location = y->loc;
	WALK_LOWER(begin)(e, s);
	if (_setjmp(e->bail_jump)) {
		if (pa != pabuf) free(pa);
		return;	/* efatal: the function is not emitted, the error counted */
	}
	if (!pa)
		codegen_fatal_report(e, y->loc, "out of memory");
	W->canary = e->unit->options->sprot ? walk_slot_temporary(e, 8, 8) : 0;
	W->stack_pointer_needed = e->unit->options->sprot == 3;
	if (KIND(c, W->return_type) != AST_TYPE_VOID) {
		int k = KIND(c, W->return_type);

		if (((k == AST_TYPE_INT128 || k == AST_TYPE_UINT128) && !WALK_LOWER(i128mem)()))
			codegen_fatal_report(e, y->loc, "x86-64: this return type is not supported yet (PLAN-x86.md X3)");
	}
	W->return_label = walk_label_new(e);
	WALK_LOWER(locals_begin)(e, s);
	em_abi(e, y->ty);
	if (walk_type_is_in_memory(e, W->return_type)) {
		WALK_LOWER(classify)(e, W->return_type, &W->return_record, false);
		W->return_record.slot = walk_slot_temporary(e, 8, 8);	/* (memory: the hidden pointer's slot) */
	}
	/* Parameters: slots unless the lowering selected a persistent register home. */
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		ast_qualified_type pt = c->syms[p].ty;
		int k = KIND(c, pt);

		if (((k == AST_TYPE_INT128 || k == AST_TYPE_UINT128) && !WALK_LOWER(i128mem)()))
			codegen_fatal_report(e, c->syms[p].loc, "x86-64: this parameter type is not supported yet (PLAN-x86.md X3)");
		if ((ft->flags & AST_TYPE_FLAG_NO_PROTOTYPE) && k == AST_TYPE_FLOAT)
			pt = codegen_type_primitive(AST_TYPE_DOUBLE);	/* K&R: received as the caller promoted it */
		WALK_LOWER(classify)(e, pt, &pa[i], false);
		codegen_lvalue home;
		pa[i].slot = WALK_LOWER(local)(e, p, &home) ? 0 : WALK_LOWER(byref)(e, p) ? walk_slot_permanent_kind(e, 8, 8, WALK_SLOT_PARAMETER) :
			walk_slot_permanent_kind(e, pa[i].size > 8 ? (pa[i].size + 7) & ~(uint64_t)7 : 8, pa[i].align, WALK_SLOT_PARAMETER);	/* (sdepth is 0: walk_slot_temporary was walk_slot_permanent) */
	}
	em_abi(e, y->ty);
	WALK_LOWER(params)(e, pa, (int)body->aux, walk_type_is_in_memory(e, W->return_type) ? &W->return_record : NULL, (ft->flags & AST_TYPE_FLAG_VARIADIC) != 0);
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];

		W->symbol_generations[p] = W->generation;
		W->symbol_offsets[p] = pa[i].slot;
		if ((ft->flags & AST_TYPE_FLAG_NO_PROTOTYPE) && KIND(c, c->syms[p].ty) == AST_TYPE_FLOAT) {
			/* the double narrowed to the float the parameter is, in place */
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = pa[i].slot, .align = 8 };
			codegen_value v = { .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)WALK_LOWER(ftreg)(e) };
			int r;

			WALK_LOWER(load)(e, v.register_number, &l, 8, false);
			v = WALK_LOWER(cvt)(e, v, codegen_type_primitive(AST_TYPE_DOUBLE), codegen_type_primitive(AST_TYPE_FLOAT));
			r = v.register_number;
			WALK_LOWER(store)(e, r, &l, 4);
			WALK_LOWER(tfree)(e, r);
		}
	}
	for (uint32_t i = 0; i < body->aux; i++) {	/* int (*a)[n]: n as on entry */
		ast_qualified_type pt = c->syms[c->lists[body->a + i]].ty;

		if (unlikely(codegen_type_is_variably_modified(c, pt)))
			walk_vla_sizes_evaluate(e, pt, true);
	}
	W->cleanup_node_count = 0;
	{
		uint32_t top = 0;

		walk_vla_scan(e, y->body, &top);
	}
	walk_statement_emit(e, y->body);
	if (walk_function_is_main(c, s) && KIND(c, W->return_type) == AST_TYPE_INT) {
		codegen_value z = codegen_value_constant(0);

		WALK_LOWER(ret)(e, &z, codegen_type_primitive(AST_TYPE_INT), W->return_label);	/* falling off the end of main returns 0 */
	}
	walk_label_place(e, W->return_label);
	WALK_LOWER(finish)(e, s);
	if (pa != pabuf) free(pa);
}
