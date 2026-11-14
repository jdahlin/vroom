/* vcc code generator: calls and inline expansion */
#include "../../../codegen/codegen-internal.h"

/* the first report of an error() call at loc (inlined copies share it) */
static bool err_call_once(codegen_context *e, uint32_t loc)
{
	codegen_unit *E = e->unit;
	bool fresh = true;

	pthread_mutex_lock(&E->diagnostic_mutex);
	for (uint32_t i = 0; i < E->nerrcall_loc; i++)
		if (E->errcall[i] == loc)
			fresh = false;
	if (fresh && E->nerrcall_loc < 256)
		E->errcall[E->nerrcall_loc++] = loc;
	pthread_mutex_unlock(&E->diagnostic_mutex);
	return fresh;
}

#define MAXARGS 128

/* park an argument value: registers go to a slot, aggregates are copied */
static void park(codegen_context *e, a64_call_argument *a)
{
	cc *c = e->compile_context;

	if (a->slot != 1)
		return;		/* parked while the arguments were evaluated */
	if (codegen_type_is_aggregate(c, a->type)) {
		uint64_t sz = (codegen_type_size(c, a->type) + 7) & ~(uint64_t)7;
		int64_t sl = a64_frame_slot_allocate(e, sz ? sz : 8, codegen_type_is_bitint_big(c, a->type) ? codegen_type_alignment(c, a->type) : codegen_type_alignment(c, a->type) > 8 ? 16 : 8);
		int rs = toreg(e, &a->value), rd = treg(e);

		a64_record_emit_immediate(e, A64_OP_ADDI, 1, rd, A64_REGISTER_FRAME, sl);
		copy_mem(e, rd, rs, codegen_type_size(c, a->type));
		tfree(e, rd);
		val_free(e, &a->value);
		a->slot = sl;
		return;
	}
	if (a->value.kind == CODEGEN_VALUE_REGISTER) {
		a->slot = a64_frame_slot_allocate(e, 8, 8);
		a64_record_emit_memory(e, A64_OP_ST, 3, 0, a->value.register_number, A64_REGISTER_FRAME, a->slot);
		val_free(e, &a->value);
	} else if (a->value.kind == CODEGEN_VALUE_REGISTER_PAIR) {
		a->slot = a64_frame_slot_allocate(e, 16, 16);
		a64_record_emit_memory(e, A64_OP_ST, 3, 0, a->value.register_number, A64_REGISTER_FRAME, a->slot);
		a64_record_emit_memory(e, A64_OP_ST, 3, 0, a->value.register_high, A64_REGISTER_FRAME, a->slot + 8);
		val_free(e, &a->value);
	}
}

/* a parked argument back in a register (aggregates: their copy's address) */
static void unpark(codegen_context *e, a64_call_argument *a)
{
	if (a->slot == 1)
		return;
	if (codegen_type_is_aggregate(e->compile_context, a->type)) {
		int r = treg(e);

		a64_record_emit_immediate(e, A64_OP_ADDI, 1, r, A64_REGISTER_FRAME, a->slot);
		a->value = codegen_value_register(r);
	} else {
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = a->slot, .align = 8 };

		a->value = load(e, &l, a->type);
	}
	a->slot = 1;
}

/* put 8-byte word k of argument a into register rd */
static void arg_word(codegen_context *e, const a64_call_argument *a, int k, int rd)
{
	if (a->slot != 1) {
		a64_record_emit_memory(e, A64_OP_LD, 3, 0, rd, A64_REGISTER_FRAME, a->slot + 8 * k);
		return;
	}
	if (a->value.kind == CODEGEN_VALUE_REGISTER || a->value.kind == CODEGEN_VALUE_REGISTER_PAIR) {	/* kept in its temporary (do_call) */
		a64_record_emit_move(e, 1, rd, k ? a->value.register_high : a->value.register_number);
	} else if (a->value.kind == CODEGEN_VALUE_CONSTANT) {
		a64_record_emit_move_immediate(e, 1, rd, (int64_t)(uint64_t)((u128)a->value.integer >> (64 * k)));
	} else if (a->value.kind == CODEGEN_VALUE_ADDRESS) {
		a64_reference_address_emit(e, rd, a->value.reference, (int64_t)a->value.integer);
	} else {
		a64_record_emit_move_immediate(e, 1, rd, 0);
	}
}

/*
 * The call proper: args are evaluated (vals); callee is a ref (direct) or a
 * value (indirect).  Returns the result value.
 */
/*
 * A call to an error() function survived folding.  clang -O2 would have
 * inlined the static function this code belongs to into its callers, where
 * the arguments are constants: mark it, and codegen_unit_emit generates the TU again
 * with it inlined.
 */
static void need_fold(codegen_context *e)
{
	const codegen_function_instance *f = e->function_instance;
	uint32_t s;

	while (f->outer)
		f = f->outer;
	s = f->function;
	if (e->compile_context->syms[s].sc == AST_STORAGE_STATIC && !e->unit->inline_everywhere[s]) {
		e->unit->inline_everywhere[s] = 1;
		e->unit->newly_marked_count++;
	}
}

/* nfix: the callee's named parameters if it is variadic, else -1 */
VPRIV codegen_value do_call(codegen_context *e, uint32_t ref, codegen_value *fnv, a64_call_argument *args, int n, ast_qualified_type rty, bool noret, int nfix)
{
	cc *c = e->compile_context;
	int ngrn = 0, nsaa = 0;
	int64_t fslot = 1, rslot = 1;
	bool sret = a64_abi_result_is_indirect(c, rty);
	uint32_t keep = 0;	/* temporaries holding register arguments: moved, not parked or spilled */
	a64_abi_spill sp;
	codegen_value res = codegen_value_none();
	int hn;

	e->has_calls = true;
	if (fnv) {
		if (fnv->kind == CODEGEN_VALUE_ADDRESS && fnv->integer == 0) {
			ref = fnv->reference;
			codegen_reference_note(e, ref);
		} else {
			int r = toreg(e, fnv);

			fslot = a64_frame_slot_allocate(e, 8, 8);
			a64_record_emit_memory(e, A64_OP_ST, 3, 0, r, A64_REGISTER_FRAME, fslot);
			val_free(e, fnv);
		}
	} else {
		codegen_reference_note(e, ref);
	}
	e->next_vector_register = 0;
	for (int i = 0; i < n; i++) {
		a64_call_argument *a = &args[i];

		if (!CODEGEN_IS_DARWIN(c) && nfix >= 0 && i >= nfix && codegen_type_is_bool_vector(c, a->type) &&
		    (T(c, a->type)->n == 64 || T(c, a->type)->n == 128))
			codegen_fatal_report(e, e->current_location, "arm64: variadic transport of 64/128-lane packed boolean vectors is not supported");

		if (nfix >= 0 && i >= nfix && CODEGEN_IS_DARWIN(c))
			a64_abi_variadic_argument_classify(e, a->type, &nsaa, &a->argument);
		else
			a64_abi_argument_classify(e, a->type, &ngrn, &nsaa, &a->argument);
		/* a scalar for x0-x7 in a temporary stays there until its mov (the moves
		 * write only x0-x7, never a temporary, so their order does not matter) */
		if (e->unit->options->opt >= 1 && a->slot == 1 && a->argument.kind == A64_ABI_ARGUMENT_REGISTER && !codegen_type_is_aggregate(c, a->type) &&
		    ((a->value.kind == CODEGEN_VALUE_REGISTER && a->argument.register_count == 1) || (a->value.kind == CODEGEN_VALUE_REGISTER_PAIR && a->argument.register_count == 2)) &&
		    codegen_temporary_index(a->value.register_number) >= 0 && !(e->temporaries_reserved & (1u << codegen_temporary_index(a->value.register_number))) &&
		    (a->value.kind == CODEGEN_VALUE_REGISTER || (codegen_temporary_index(a->value.register_high) >= 0 && !(e->temporaries_reserved & (1u << codegen_temporary_index(a->value.register_high)))))) {
			keep |= 1u << codegen_temporary_index(a->value.register_number);
			if (a->value.kind == CODEGEN_VALUE_REGISTER_PAIR)
				keep |= 1u << codegen_temporary_index(a->value.register_high);
			continue;
		}
		park(e, a);
	}
	if (CODEGEN_IS_DARWIN(c))
		nsaa = (nsaa + 7) & ~7;
	if (nsaa > e->outgoing_argument_bytes_max)
		e->outgoing_argument_bytes_max = nsaa;
	if (sret)
		rslot = a64_frame_slot_allocate(e, codegen_type_size(c, rty), codegen_type_alignment(c, rty) > 16 ? 16 : codegen_type_alignment(c, rty));
	if (!e->is_reachable)
		return codegen_type_is_void(c, rty) ? codegen_value_none() : codegen_value_constant(0);
	if (unlikely(CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_SYMBOL && !fnv && codegen_symbol_attributes(c, CODEGEN_REFERENCE_INDEX(ref))->error_fn)) {
		/*
		 * An error() call that folding did not remove.  clang removed it
		 * (the tree builds with clang), with value ranges vcc does not
		 * have (min(len, ...) inside while (len > 0)).  Drop the call and
		 * go on, as the kernel's own -O0 fallback does
		 * (__compiletime_assert is then ((void)(condition))): a trap
		 * here would fire on paths clang proved are fine.
		 */
		if (!e->dry_run_depth && !e->unit->options->vcc_kernel) {
			/* as clang: every call that survives is an error */
			const attrs *at = codegen_symbol_attributes(c, CODEGEN_REFERENCE_INDEX(ref));
			const vident *nm = &vintern_ents[c->syms[CODEGEN_REFERENCE_INDEX(ref)].name];

			if (err_call_once(e, e->current_location))
				codegen_error_report(e, e->current_location, "call to '%.*s' declared with 'error' attribute: %s", (int)nm->len, nm->s,
				       at->errmsg ? c->strs[at->errmsg - 1].s : "");
		} else if (!e->dry_run_depth) {
			need_fold(e);
			e->unit->nerrcall++;
			if (e->unit->stats)
				codegen_diagnostic_report(e, e->current_location, "error() call '%.*s' not folded: dropped", (int)vintern_ents[c->syms[CODEGEN_REFERENCE_INDEX(ref)].name].len,
				      vintern_ents[c->syms[CODEGEN_REFERENCE_INDEX(ref)].name].s);
		}
		return codegen_type_is_void(c, rty) ? codegen_value_none() : codegen_value_constant(0);
	}
	e->temporaries_in_use &= ~keep;	/* dead after their moves: not spilled */
	a64_abi_temporaries_spill(e, &sp);
	e->temporaries_in_use |= keep;
	for (int i = 0; i < n; i++) {
		a64_call_argument *a = &args[i];

		switch (a->argument.kind) {
		case A64_ABI_ARGUMENT_BOOL_LANES:
			for (unsigned k = 0; k < T(c, a->type)->n; k++) {
				int rd = k < a->argument.register_count ? a->argument.register_number + k : R_X16;
				a64_record_emit_memory(e, A64_OP_LD, 0, 0, rd, A64_REGISTER_FRAME, a->slot + k / 8);
				a64_record_emit_bitfield(e, A64_OP_UBFM, 1, rd, rd, k % 8, k % 8);
				if (k >= a->argument.register_count) a64_record_emit_memory(e, A64_OP_ST, 3, 0, rd, SP, a->argument.stack_offset + (k - a->argument.register_count) * 8);
			}
			break;
		case A64_ABI_ARGUMENT_REGISTER:
			for (int k = 0; k < a->argument.register_count; k++)
				arg_word(e, a, k, a->argument.register_number + k);
			if (codegen_type_is_darwin_narrow(c, a->type))	/* Apple: the caller extends char/short/_Bool to 32 bits */
				extend(e, a->argument.register_number, a->argument.register_number, (int)codegen_type_size(c, a->type), codegen_type_is_signed(c, a->type));
			break;
		case A64_ABI_ARGUMENT_STACK:
			if (CODEGEN_IS_DARWIN(c) && a->argument.size < 8 && !codegen_type_is_aggregate(c, a->type)) {	/* Apple: packed */
				/* a variadic one has an 8-byte slot: store all of it, as clang
				 * does, so va_arg(ap, T *) of a literal 0 (sqlite's shell)
				 * reads no stale upper half */
				int va = nfix >= 0 && i >= nfix;

				arg_word(e, a, 0, R_X16);
				a64_record_emit_memory(e, A64_OP_ST, va ? 3 : a64_size_log2(a->argument.size), 0, R_X16, SP, a->argument.stack_offset);
				break;
			}
			for (int k = 0; k < a->argument.register_count; k++) {
				/* an Apple HFA of floats on the stack: a 4-byte last part */
				int lg = a->argument.float_member_size && a->argument.size - 8 * k < 8 ? 2 : 3;

				arg_word(e, a, k, R_X16);
				a64_record_emit_memory(e, A64_OP_ST, lg, 0, R_X16, SP, a->argument.stack_offset + 8 * k);
			}
			break;
		case A64_ABI_ARGUMENT_REFERENCE_REGISTER:
			a64_record_emit_immediate(e, A64_OP_ADDI, 1, a->argument.register_number, A64_REGISTER_FRAME, a->slot);
			break;
		case A64_ABI_ARGUMENT_REFERENCE_STACK:
			a64_record_emit_immediate(e, A64_OP_ADDI, 1, R_X16, A64_REGISTER_FRAME, a->slot);
			a64_record_emit_memory(e, A64_OP_ST, 3, 0, R_X16, SP, a->argument.stack_offset);
			break;
		case A64_ABI_ARGUMENT_VECTOR_REGISTER:
			arg_word(e, a, 0, R_X16);
			a64_record_emit_word(e, a64_word_fmov_to_vector(a->argument.size >= 8, a->argument.register_number, R_X16));
			if (a->argument.size == 16) {
				arg_word(e, a, 1, R_X16);
				a64_record_emit_word(e, a64_word_fmov_high_to_vector(a->argument.register_number, R_X16));
			}
			break;
		case A64_ABI_ARGUMENT_HFA:
			for (int k = 0; k < a->argument.register_count; k++)
				vr_load(e, a->argument.float_member_size, a->argument.register_number + k, A64_REGISTER_FRAME, a->slot + a->argument.float_member_size * k);
			break;
		}
	}
	for (int i = 0; i < n && keep; i++)
		if (args[i].slot == 1 && (args[i].value.kind == CODEGEN_VALUE_REGISTER || args[i].value.kind == CODEGEN_VALUE_REGISTER_PAIR) && args[i].argument.kind == A64_ABI_ARGUMENT_REGISTER)
			val_free(e, &args[i].value);
	if (sret)
		a64_record_emit_immediate(e, A64_OP_ADDI, 1, 8, A64_REGISTER_FRAME, rslot);
	if (fslot != 1) {
		a64_record_emit_memory(e, A64_OP_LD, 3, 0, R_X16, A64_REGISTER_FRAME, fslot);
		a64_record_emit(e, A64_OP_BLR)->rn = R_X16;
	} else {
		a64_record_emit(e, A64_OP_BL)->reference = ref;
	}
	if (codegen_type_is_void(c, rty)) {
		res = codegen_value_none();
	} else if (sret) {
		int r = treg(e);

		a64_record_emit_immediate(e, A64_OP_ADDI, 1, r, A64_REGISTER_FRAME, rslot);
		res = codegen_value_register(r);
	} else if (codegen_type_is_bool_vector(c, rty)) {
		int64_t sl = a64_frame_slot_allocate(e, 8, 8);
		int r = treg(e);
		a64_abi_bool_vector_result_store(e, rty, sl);
		a64_record_emit_immediate(e, A64_OP_ADDI, 1, r, A64_REGISTER_FRAME, sl);
		res = codegen_value_register(r);
	} else if (codegen_type_is_record_like(c, rty) && a64_abi_hfa_classify(c, rty, &hn)) {
		int hb = a64_abi_hfa_classify(c, rty, &hn);
		int64_t sl = a64_frame_slot_allocate(e, 64, 16);
		int r = treg(e);

		for (int k = 0; k < hn; k++)
			vr_store(e, hb, k, A64_REGISTER_FRAME, sl + hb * k);
		a64_record_emit_immediate(e, A64_OP_ADDI, 1, r, A64_REGISTER_FRAME, sl);
		res = codegen_value_register(r);
	} else if (a64_abi_vector_is_small(c, rty)) {
		int64_t sl = a64_frame_slot_allocate(e, 8, 8);
		int r = treg(e);

		a64_abi_small_vector_result_store(e, rty, sl);
		a64_record_emit_immediate(e, A64_OP_ADDI, 1, r, A64_REGISTER_FRAME, sl);
		res = codegen_value_register(r);
	} else if (codegen_type_is_record_like(c, rty)) {
		int64_t sl = a64_frame_slot_allocate(e, 16, 16);
		int r = treg(e);

		a64_record_emit_memory(e, A64_OP_ST, 3, 0, 0, A64_REGISTER_FRAME, sl);
		if (codegen_type_size(c, rty) > 8)
			a64_record_emit_memory(e, A64_OP_ST, 3, 0, 1, A64_REGISTER_FRAME, sl + 8);
		a64_record_emit_immediate(e, A64_OP_ADDI, 1, r, A64_REGISTER_FRAME, sl);
		res = codegen_value_register(r);
	} else if (codegen_type_is_long_double(c, rty)) {
		res.kind = CODEGEN_VALUE_REGISTER_PAIR;
		res.register_number = (uint8_t)treg(e);
		res.register_high = (uint8_t)treg(e);
		a64_record_emit_word(e, a64_word_fmov_from_vector(1, res.register_number, 0));
		a64_record_emit_word(e, a64_word_fmov_high_from_vector(res.register_high, 0));
	} else if (codegen_type_size(c, rty) == 16 && !codegen_type_is_float(c, rty)) {
		res.kind = CODEGEN_VALUE_REGISTER_PAIR;
		res.register_number = (uint8_t)treg(e);
		res.register_high = (uint8_t)treg(e);
		a64_record_emit_move(e, 1, res.register_number, 0);
		a64_record_emit_move(e, 1, res.register_high, 1);
	} else if (codegen_type_is_float(c, rty)) {
		int r = treg(e);

		a64_record_emit_word(e, a64_word_fmov_from_vector(fpk(e, rty), r, 0));
		res = codegen_value_register(r);
	} else {
		int r = treg(e);

		a64_record_emit_move(e, 1, r, 0);
		res = codegen_value_register(r);
	}
	a64_abi_temporaries_restore(e, &sp);
	if (noret)
		e->is_reachable = false;
	return res;
}

VPRIV codegen_value call_named(codegen_context *e, uint32_t ref, codegen_value *vals, ast_qualified_type *tys, int n, ast_qualified_type rty, bool noret)
{
	a64_call_argument a[8];

	for (int i = 0; i < n; i++) {
		a[i].value = vals[i];
		a[i].type = tys[i];
		a[i].slot = 1;
	}
	return do_call(e, ref, NULL, a, n, rty, noret, -1);
}

/* ---------------- inline expansion ---------------- */

static bool on_inline_stack(codegen_context *e, uint32_t fn)
{
	for (codegen_function_instance *f = e->function_instance; f; f = f->outer)
		if (f->function == fn)
			return true;
	return false;
}

/* a local only ever assigned constants: follow its value (v: the initializer, or none) */
VPRIV void kv_track(codegen_context *e, uint32_t s, const codegen_value *v)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	codegen_binding *b = codegen_symbol_binding_get(e, s);
	int k;

	if ((e->unit->symbol_flags[s] & (CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_CONSTANT_WRITTEN)) != CODEGEN_SYMBOL_CONSTANT_WRITTEN || !codegen_type_is_integer(c, y->ty) || codegen_type_is_wide(c, y->ty) ||
	    (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_VOLATILE) || (T(c, y->ty)->flags & AST_TYPE_FLAG_ATOMIC) || codegen_symbol_attributes(c, s)->cleanup || e->known_value_count == CODEGEN_KNOWN_VALUE_MAX)
		return;
	k = e->known_value_count++;
	b->known_value_index = (uint8_t)(k + 1);
	e->known_values_valid &= (uint16_t)~(1u << k);
	if (v && v->kind == CODEGEN_VALUE_CONSTANT) {
		e->known_values[k] = (int64_t)codegen_integer_truncate(c, v->integer, y->ty);
		e->known_values_valid |= (uint16_t)(1u << k);
	}
}

/* may variable s (param or local) be bound to the constant v for good? */
VPRIV bool const_var(codegen_context *e, uint32_t s, const codegen_value *v)
{
	uint8_t f = e->unit->symbol_flags[s];

	if ((T(e->compile_context, e->compile_context->syms[s].ty)->flags & AST_TYPE_FLAG_ATOMIC) || (f & (CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_CONSTANT_MULTIPLE)))
		return false;
	if (f & CODEGEN_SYMBOL_CONSTANT_WRITTEN)
		return v->kind == CODEGEN_VALUE_CONSTANT && codegen_integer_truncate(e->compile_context, v->integer, e->compile_context->syms[s].ty) == e->unit->constant_written_values[s];
	return v->kind == CODEGEN_VALUE_CONSTANT || v->kind == CODEGEN_VALUE_ADDRESS;
}

#define INLINE_NODES 40	/* AST nodes: a few statements */
#ifndef INLINE_STATIC_NODES
#define INLINE_STATIC_NODES 40	/* the same for a static function not declared inline (clang -O2 inlines it) */
#endif

/* a static function clang would inline: no inline asm, no locals aligned above 16 */
static bool tiny_ok(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return true;
	x = N(n);
	switch (x->op) {
	case AST_NODE_ASM:
		return false;
	case AST_NODE_DECLARATION:
		return c->syms[x->a].kind != AST_SYMBOL_VARIABLE || codegen_symbol_alignment(c, x->a) <= 16;
	case AST_NODE_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (!tiny_ok(c, c->lists[x->b + i]))
				return false;
		return true;
	case AST_NODE_IF:
		return tiny_ok(c, x->b) && tiny_ok(c, x->c);
	case AST_NODE_WHILE: case AST_NODE_DO: case AST_NODE_SWITCH: case AST_NODE_LABEL: case AST_NODE_CASE: case AST_NODE_DEFAULT:
		return tiny_ok(c, x->op == AST_NODE_LABEL || x->op == AST_NODE_CASE || x->op == AST_NODE_DEFAULT ? x->c : x->b);
	case AST_NODE_FOR:
		return tiny_ok(c, c->lists[x->c + 1]);
	default:
		return true;
	}
}

VPRIV bool can_inline(codegen_context *e, uint32_t s)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];

	if (!(y->flags & AST_SYMBOL_FLAG_DEFINED) || !y->body)
		return false;
	if (!(y->flags & AST_SYMBOL_FLAG_ALWAYS_INLINE)) {
		/*
		 * A plain inline function that calls an error() function (the
		 * kernel's BUILD_BUG_ON) only compiles when an optimizing compiler
		 * inlines it and folds the call away: inline it too.
		 */
		if (y->flags & AST_SYMBOL_FLAG_NOINLINE)
			return false;
		if (!e->unit->inline_everywhere[s]) {
			if (y->sc == AST_STORAGE_STATIC)
				codegen_log_append(e, CODEGEN_LOG_DEPENDENCY, s);
			if (!(y->flags & AST_SYMBOL_FLAG_INLINE) && (y->sc != AST_STORAGE_STATIC || !e->unit->options->opt))
				return false;
			codegen_function_prepass_request(e, s);
			if (!(y->flags & AST_SYMBOL_FLAG_INLINE)) {
				/* a plain static function: small, no inline asm or over-aligned locals */
				if (e->unit->function_node_counts[s] > INLINE_STATIC_NODES || !tiny_ok(c, y->body))
					return false;
			}
			/*
			 * clang -O2 inlines small inline functions, and the kernel
			 * leans on it: scs_is_enabled() { return false; } guards
			 * references to objects that only exist when it is true.
			 */
			if (!(e->unit->symbol_flags[s] & CODEGEN_SYMBOL_MUST_FOLD) && e->unit->function_node_counts[s] > INLINE_NODES)
				return false;
		}
	}
	if (T(c, y->ty)->flags & AST_TYPE_FLAG_VARIADIC)
		return false;
	codegen_function_walk_ensure(e->unit, c, s);
	if (e->unit->symbol_flags[s] & CODEGEN_SYMBOL_NO_INLINE)
		return false;
	if (e->inline_depth >= 48 || on_inline_stack(e, s))
		return false;
	return true;
}

/* temporaries an inlined call leaves free when it keeps params and its result in registers:
   e->inlfree, CODEGEN_INLINE_FREE_FAST first, CODEGEN_INLINE_FREE_SAFE when generated again (out of registers) */
#define INLINE_FREE (e->inline_free_temporaries ? e->inline_free_temporaries : CODEGEN_INLINE_FREE_SAFE)

/* an integer or pointer that fits one register (what CODEGEN_BINDING_REGISTER and retreg hold) */
static bool reg_scalar(codegen_context *e, ast_qualified_type t)
{
	cc *c = e->compile_context;

	return (codegen_type_is_integer(c, t) || codegen_type_is_pointer(c, t)) && !codegen_type_is_wide(c, t) && !(AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) && !(T(c, t)->flags & AST_TYPE_FLAG_ATOMIC) && codegen_type_size(c, t) - 1 < 8;
}

static bool is_temp(int r)
{
	for (int i = 0; i < CODEGEN_TEMPORARY_COUNT; i++)
		if (codegen_temporary_registers[i] == r)
			return true;
	return false;
}

VPRIV codegen_value inline_call(codegen_context *e, uint32_t s, a64_call_argument *args, int n, uint32_t call)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	const node *body = N(y->body);
	codegen_function_instance F;
	ast_qualified_type rty = T(c, y->ty)->base;
	int64_t saved_sp = 0;
	codegen_value res;

	codegen_function_prepass_request(e, s);
	memset(&F, 0, sizeof(F));
	F.function = s;
	F.call = call;
	F.instance = ++e->next_instance;
	F.is_inlined = true;
	F.return_type = rty;
	F.return_label = a64_label_new(e);
	F.constant_is_ok = true;
	F.cleanup_base = e->cleanup_count;
	F.loop_depth = e->loop_count;
	F.outer = e->function_instance;
	if (e->unit->options->opt >= 1 && reg_scalar(e, rty) && CODEGEN_TEMPORARY_COUNT - __builtin_popcount(e->temporaries_in_use) > INLINE_FREE)
		F.return_register = (uint8_t)(treg(e) + 1);	/* returns put the value there: no slot */
	else if (!codegen_type_is_void(c, rty))
		F.return_slot = a64_frame_slot_allocate(e, codegen_type_size(c, rty) ? codegen_type_size(c, rty) : 8, codegen_type_alignment(c, rty) > 16 ? 16 : codegen_type_alignment(c, rty));
	for (int i = 0; i < (int)body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *ps = &c->syms[p];
		codegen_binding *b = codegen_symbol_binding_get(e, p);
		/* Reload only this parameter: restoring all parked values together
		 * exhausts temporaries for wide or numerous inline arguments. */
		if (i < n)
			unpark(e, &args[i]);
		codegen_value v = i < n ? args[i].value : codegen_value_constant(0);

		if (const_var(e, p, &v) && !codegen_type_is_aggregate(c, ps->ty) &&
		    !(AST_QUALIFIED_TYPE_QUALIFIERS(ps->ty) & AST_QUALIFIER_VOLATILE) && !codegen_type_is_wide(c, ps->ty)) {
			b->instance = F.instance;
			b->kind = CODEGEN_BINDING_CONSTANT;
			b->known_value_index = 0;
			b->value = v;
			continue;
		}
		/*
		 * A param the body never writes (nor takes the address of: that
		 * counts as a write) keeps the argument's register: reads copy it.
		 * Extended as a load from its slot would be, so it is the same value.
		 */
		if (e->unit->options->opt >= 1 && i < n && v.kind == CODEGEN_VALUE_REGISTER && is_temp(v.register_number) && !(e->unit->symbol_flags[p] & (CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_CONSTANT_WRITTEN)) &&
		    reg_scalar(e, ps->ty) && !codegen_symbol_attributes(c, p)->cleanup && ps->sc != AST_STORAGE_REGISTER &&
		    CODEGEN_TEMPORARY_COUNT - __builtin_popcount(e->temporaries_in_use) > INLINE_FREE) {
			uint64_t sz = codegen_type_size(c, ps->ty);

			if (sz < 8)
				extend(e, v.register_number, v.register_number, (int)sz, codegen_type_is_signed(c, ps->ty));
			b->instance = F.instance;
			b->kind = CODEGEN_BINDING_REGISTER;
			b->known_value_index = 0;
			b->value = v;
			continue;
		}
		{
			int64_t sl;
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME };

			b->instance = 0;
			sl = bind_slot(e, p);
			codegen_symbol_binding_get(e, p)->instance = F.instance;
			l.offset = sl;
			l.align = codegen_type_alignment(c, ps->ty);
			store(e, &l, ps->ty, &v);
			val_free(e, &v);
		}
	}
	if (e->unit->node_flags[y->body] & CODEGEN_NODE_HAS_ALLOCA) {
		int r = treg(e);
		saved_sp = a64_frame_slot_allocate(e, 8, 8);
		a64_record_emit_move(e, 1, r, SP);
		a64_record_emit_memory(e, A64_OP_ST, 3, 0, r, A64_REGISTER_FRAME, saved_sp);
		tfree(e, r);
	}
	e->function_instance = &F;
	e->inline_depth++;
	e->unit->inline_total++;
	gen_stmt(e, y->body);
	e->inline_depth--;
	e->function_instance = F.outer;
	deflabel(e, F.return_label);
	if (saved_sp) {
		int r = treg(e);
		a64_record_emit_memory(e, A64_OP_LD, 3, 0, r, A64_REGISTER_FRAME, saved_sp);
		a64_record_emit_move(e, 1, SP, r);
		tfree(e, r);
	}
	for (int i = 0; i < (int)body->aux; i++) {
		codegen_binding *b = codegen_symbol_binding_get(e, c->lists[body->a + i]);

		if (b->kind == CODEGEN_BINDING_REGISTER && b->instance == F.instance) {
			tfree(e, b->value.register_number);
			b->kind = B_NONE, b->instance = 0;
		}
	}
	if (F.return_register) {
		if (F.constant_is_ok && F.any_ret) {
			tfree(e, F.return_register - 1);
			return F.constant;
		}
		return codegen_value_register(F.return_register - 1);
	}
	if (codegen_type_is_void(c, rty))
		return codegen_value_none();
	if (F.constant_is_ok && F.any_ret)
		return F.constant;
	{
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = F.return_slot, .align = codegen_type_alignment(c, rty) };

		res = load(e, &l, rty);
	}
	return res;
}

/* __builtin_strcmp of two string literals (the kernel picks operators by name) */
static codegen_value fold_strcmp(codegen_context *e, const node *x)
{
	cc *c = e->compile_context;
	uint32_t f = codegen_expression_casts_strip(c, x->a), a = codegen_expression_casts_strip(c, c->lists[x->b]), b = codegen_expression_casts_strip(c, c->lists[x->b + 1]);
	int r;

	if (N(f)->op != AST_NODE_VARIABLE || c->syms[N(f)->a].kind != AST_SYMBOL_BUILTIN || N(a)->op != AST_NODE_STRING || N(b)->op != AST_NODE_STRING ||
	    cc_builtin_library_kind(c->syms[N(f)->a].val) != CC_LIBRARY_KIND_STRCMP)
		return codegen_value_none();
	r = strcmp(c->strs[N(a)->a].s, c->strs[N(b)->a].s);
	return codegen_value_constant(r < 0 ? -1 : r > 0);
}

/*
 * __builtin_memcpy/memmove/memset with a constant size: inline stores, as
 * clang at every -O and under -fno-builtin/-ffreestanding (LLVM's memcpy
 * intrinsic; the vDSO has no memcpy to call: get/put_unaligned in
 * lib/vdso/getrandom.c).  Plain memcpy() stays a call.  The value is dst.
 */
/* one ldp/stp of x registers at [base + off] (off: 8-aligned, -512..504) */
static void pair(codegen_context *e, int op, int r1, int r2, int base, int64_t off)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, op);
	i->size = 1, i->rd = (uint8_t)r1, i->ra = (uint8_t)r2, i->rn = (uint8_t)base, i->immediate = off;
}

/* memcpy of 16 to 128 bytes: ldp/stp pairs, the rest as copy_mem */
static void copy_pairs(codegen_context *e, int d, int sr, uint64_t n)
{
	uint64_t o = 0;

	for (; n - o >= 16; o += 16) {
		pair(e, A64_OP_LDP, R_X16, R_X17, sr, (int64_t)o);
		pair(e, A64_OP_STP, R_X16, R_X17, d, (int64_t)o);
	}
	if (o < n) {
		int t = R_X16;

		for (uint64_t k; o < n; o += k) {
			k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;
			a64_record_emit_memory(e, A64_OP_LD, a64_size_log2(k), 0, t, sr, (int64_t)o);
			a64_record_emit_memory(e, A64_OP_ST, a64_size_log2(k), 0, t, d, (int64_t)o);
		}
	}
}

static bool mem_inline(codegen_context *e, int kind, a64_call_argument *args, codegen_value *res)
{
	uint64_t n;
	int d;

	if (kind < CC_LIBRARY_KIND_MEMCPY || kind > CC_LIBRARY_KIND_MEMSET || args[2].value.kind != CODEGEN_VALUE_CONSTANT)
		return false;
	n = (uint64_t)args[2].value.integer;
	if (n > 128 || (kind == 2 && n & (n - 1)) || (kind == 2 && n > 8) || (kind == 3 && args[1].value.kind != CODEGEN_VALUE_CONSTANT))
		return false;	/* memmove: one load before one store (overlap) */
	/* parked while many registers were live: their v is a freed register */
	unpark(e, &args[0]);
	if (kind < 3)
		unpark(e, &args[1]);
	d = toreg(e, &args[0].value);
	if (kind < 3) {
		int sr = toreg(e, &args[1].value);

		if (kind == CC_LIBRARY_KIND_MEMCPY && n >= 16 && e->unit->options->opt >= 1)
			copy_pairs(e, d, sr, n);
		else
			copy_mem(e, d, sr, n);
		val_free(e, &args[1].value);
	} else if (!(args[1].value.integer & 0xff)) {
		zero_mem(e, d, n);
	} else {
		uint64_t pat = (uint64_t)(args[1].value.integer & 0xff) * 0x0101010101010101ull, o = 0;
		int t = R_X16;	/* as copy_mem: not x17 */

		a64_record_emit_move_immediate(e, 1, t, (int64_t)pat);
		while (o < n) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			a64_record_emit_memory(e, A64_OP_ST, a64_size_log2(k), 0, t, d, (int64_t)o);
			o += k;
		}
	}
	*res = args[0].value;
	return true;
}

static int64_t chk_plain(const a64_call_argument *args, int na, int64_t bi)
{
	const codegen_value *o = &args[na - 1].value, *l = &args[na >= 2 ? na - 2 : 0].value;

	return cc_builtin_checked_simplify(bi, na, o->kind == CODEGEN_VALUE_CONSTANT, (uint64_t)o->integer, l->kind == CODEGEN_VALUE_CONSTANT, (uint64_t)l->integer);
}

/* Keep the argument scratch buffer out of gen_inner's recursive frame.
 * Inlining it there charges every arithmetic node for MAXARGS call slots. */
__attribute__((noinline)) VPRIV codegen_value gen_call(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	uint32_t ref = 0, s = codegen_call_direct_callee(c, x->a, &ref);
	a64_call_argument argbuf[MAXARGS], *args = argbuf;
	int na = (int)x->c;
	codegen_value fnv = codegen_value_none(), r;
	bool noret = false, inl;

	/* more than MAXARGS (psql's sql_help.c passes ~260): on the heap */
	if (na > MAXARGS && !(args = malloc((size_t)na * sizeof(*args))))
		codegen_fatal_report(e, x->loc, "out of memory");
	if (s) {
		const sym *y = &c->syms[s];

		noret = (y->flags & AST_SYMBOL_FLAG_NORETURN) != 0;
		/* use the definition's flags (always_inline may be on a later declaration) */
	}
	if (!s && ref && na == 2) {
		codegen_value f = fold_strcmp(e, x);

		if (f.kind == CODEGEN_VALUE_CONSTANT)
			return f;
	}
	if (!s && !ref)
		fnv = gen(e, x->a);
	inl = s && can_inline(e, s);
	if (inl)
		codegen_function_prepass_request(e, s);
	for (int i = 0; i < na; i++) {
		uint32_t an = c->lists[x->b + i];

		/* an argument the inlined body never reads: only its side effects count */
		if (inl && i < (int)N(c->syms[s].body)->aux &&
		    !(e->unit->symbol_flags[c->lists[N(c->syms[s].body)->a + i]] & CODEGEN_SYMBOL_USED) && !codegen_expression_has_side_effects(c, an) &&
		    !codegen_type_is_aggregate(c, c->syms[c->lists[N(c->syms[s].body)->a + i]].ty)) {
			args[i].value = codegen_value_constant(0);
			args[i].type = N(an)->ty;
			args[i].slot = 1;
			continue;
		}
		args[i].value = gen(e, an);
		args[i].type = N(an)->ty;
		args[i].slot = 1;
		/* many arguments: keep the registers for evaluating the rest */
		if (__builtin_popcount(e->temporaries_in_use & ~e->temporaries_reserved) > (CODEGEN_TEMPORARY_COUNT - __builtin_popcount(e->temporaries_reserved)) / 2 &&
		    (args[i].value.kind == CODEGEN_VALUE_REGISTER || args[i].value.kind == CODEGEN_VALUE_REGISTER_PAIR))
			park(e, &args[i]);
	}
	if (!s && CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_NAME && CODEGEN_REFERENCE_INDEX(ref) < 0x8000000u && na >= 3 && cc_builtin_library_kind(CODEGEN_REFERENCE_INDEX(ref)) == CC_LIBRARY_KIND_CHECKED) {
		int64_t p = chk_plain(args, na, CODEGEN_REFERENCE_INDEX(ref));

		if (p >= 0) {
			ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_NAME, (uint32_t)p);
			na--;
		}
	}
	if (!s && CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_NAME && CODEGEN_REFERENCE_INDEX(ref) < 0x8000000u && na == 1 &&
	    codegen_type_is_long_double(c, x->ty) && cc_builtin_library_kind(CODEGEN_REFERENCE_INDEX(ref)) == CC_LIBRARY_KIND_FABSL) {
		/* Clear binary128's sign without touching a signaling NaN payload. */
		unpark(e, &args[0]);
		r = args[0].value;
		if (r.kind == CODEGEN_VALUE_CONSTANT)
			r.integer = (i128)((u128)r.integer & ~((u128)1 << 127));
		else {
			toreg2(e, &r, false);
			a64_record_emit_immediate(e, A64_OP_ANDI, 1, r.register_high, r.register_high, INT64_MAX);
		}
	} else if (inl) {
		r = inline_call(e, s, args, na, n);
	} else if (!s && ref && na == 3 && CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_NAME && CODEGEN_REFERENCE_INDEX(ref) < 0x8000000u &&
		   mem_inline(e, cc_builtin_library_kind(CODEGEN_REFERENCE_INDEX(ref)), args, &r)) {
		/* inlined */
	} else if (s && codegen_memory_function_kind(e, s, na) && args[2].value.kind == CODEGEN_VALUE_CONSTANT && (uint64_t)args[2].value.integer <= 64 &&
		   mem_inline(e, codegen_memory_function_kind(e, s, na), args, &r)) {
		/* inlined */
	} else {
		ast_qualified_type ft = N(x->a)->ty;
		int nfix = -1;

		if (codegen_type_is_pointer(c, ft))
			ft = T(c, ft)->base;
		if (codegen_type_is_function(c, ft) && (T(c, ft)->flags & AST_TYPE_FLAG_VARIADIC))
			nfix = (int)T(c, ft)->n;
		r = s || ref ? do_call(e, ref, NULL, args, na, x->ty, noret, nfix)
			     : do_call(e, 0, &fnv, args, na, x->ty, false, nfix);
	}
	if (args != argbuf)
		free(args);
	return r;
}
