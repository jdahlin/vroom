/* The simplifier's driver and the pass that gives each access its own address. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/*
 * base + constant as an address is the load's or store's own operand on the CPU, but the
 * lowering folds only a ptradd that its one user follows directly; numbering gives a
 * block's accesses to one field a shared ptradd, which then costs an instruction and a
 * register.  So each such access gets its own copy just before it (the shared one dies
 * when nothing else reads it).  Only offsets every CPU here encodes: -256..255, or a
 * multiple of the access size up to 4095 of them.
 */
static void virbuild_address_offsets_fold(codegen_context *e, virbuild_simplify_state *S)
{
	virbuild_context *V = e->vir_builder;

	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t q = V->blocks[b].first_insn, pq = 0; q; pq = q, q = V->insns[q].next) {
			vir_insn in = V->insns[q].insn;
			uint32_t a, n, w = 1u << VIR_INSN_FLAG_WIDTH_LOG2(in.flags);
			int64_t k;

			if ((in.op != VIR_OP_LOAD && in.op != VIR_OP_STORE) || q >= S->constant_start || virbuild_value_resolve(V, q) != q || virbuild_promote_store_is_dead(V, q) ||
			    (in.op == VIR_OP_LOAD && !S->use_counts[q] && !(in.flags & VIR_INSN_FLAG_VOL)))
				continue;
			a = virbuild_value_resolve(V, in.a);
			if (a >= S->constant_start || V->insns[a].insn.op != VIR_OP_PTRADD || V->insns[a].block == VIRBUILD_BLOCK_NONE ||
			    !virbuild_value_integer_constant(V, virbuild_value_resolve(V, V->insns[a].insn.b), &k) ||
			    !((k >= -256 && k <= 255) || (k > 0 && !(k % w) && k / w <= 4095)))
				continue;
			if (a == pq && S->use_counts[a] == 1)
				continue;	/* (its own already) */
			n = virbuild_preir_insn_add(e, VIR_OP_PTRADD, VIR_TYPE_PTR, V->insns[a].insn.flags, virbuild_value_resolve(V, V->insns[a].insn.a), virbuild_value_resolve(V, V->insns[a].insn.b), 0, b);
			V->insns[n].location = V->insns[q].location;
			V->insns[n].next = q;
			if (pq)
				V->insns[pq].next = n;
			else
				V->blocks[b].first_insn = n;
			V->insns[q].insn.a = n;
			virbuild_use_count_decrement(V, S, a), virbuild_use_count_increment(V, S, V->insns[n].insn.a);
		}
	}
}

/* A single-use memcmp result tested against zero needs only byte equality.
 * Replace the call at its own position so intervening stores cannot move
 * ahead of the read. The compared word is exactly the requested length. */
static void virbuild_literal_memcmp_fold(codegen_context *e, const virbuild_simplify_state *S, uint32_t q)
{
	virbuild_context *V = e->vir_builder;
	vir_insn cmp = V->insns[q].insn;
	int64_t zero, size;
	bool branch = cmp.op == VIR_OP_BRIF;
	if (!branch && (cmp.flags > 1 || !virbuild_value_integer_constant(V, cmp.b, &zero) || zero))
		return;
	uint32_t trunc = virbuild_value_resolve(V, cmp.a);
	if (trunc >= S->constant_start || S->use_counts[trunc] != 1 || V->insns[trunc].insn.op != VIR_OP_TRUNC)
		return;
	uint32_t id = virbuild_value_resolve(V, V->insns[trunc].insn.a);
	vir_insn call = V->insns[id].insn;
	if (id >= S->constant_start || S->use_counts[id] != 1 || call.op != VIR_OP_CALL || V->extra[call.c] != 3)
		return;
	vir_insn fn = V->insns[call.a].insn;
	if (fn.op != VIR_OP_SYMADDR || CODEGEN_REFERENCE_KIND(fn.a) != CODEGEN_REFERENCE_SYMBOL || fn.b || fn.c)
		return;
	cc *c = e->compile_context;
	uint32_t symid = CODEGEN_REFERENCE_INDEX(fn.a);
	const sym *y = &c->syms[symid];
	if (!CODEGEN_IDENTIFIER_IS(y->name, "memcmp") || y->kind != AST_SYMBOL_FUNCTION || y->body ||
	    y->sc == AST_STORAGE_STATIC || !(y->flags & AST_SYMBOL_FLAG_GLOBAL) ||
	    codegen_symbol_attributes(c, symid)->asmname || e->unit->options->no_builtin)
		return;
	for (unsigned i = 0; i < CC_DISABLED_BUILTIN_WORDS; i++)
		if (e->unit->options->disabled_builtins[i])
			return;
	const ctype *ft = T(c, y->ty);
	if (ft->kind != AST_TYPE_FUNCTION || ft->n != 3 || (ft->flags & (AST_TYPE_FLAG_NO_PROTOTYPE | AST_TYPE_FLAG_VARIADIC)) ||
	    KIND(c, ft->base) != AST_TYPE_INT || !codegen_type_is_pointer(c, c->params[ft->aux]) ||
	    !codegen_type_is_pointer(c, c->params[ft->aux + 1]) ||
	    KIND(c, T(c, c->params[ft->aux])->base) != AST_TYPE_VOID ||
	    KIND(c, T(c, c->params[ft->aux + 1])->base) != AST_TYPE_VOID ||
	    KIND(c, c->params[ft->aux + 2]) != (CODEGEN_IS_WIN64(c) ? AST_TYPE_UNSIGNED_LONG_LONG : AST_TYPE_UNSIGNED_LONG) ||
	    !virbuild_value_integer_constant(V, V->extra[call.c + 3], &size) ||
	    (size != 1 && size != 2 && size != 4 && size != 8))
		return;
	for (unsigned side = 0; side < 2; side++) {
		uint32_t ptr = V->extra[call.c + 1 + side];
		vir_insn literal = V->insns[V->extra[call.c + 2 - side]].insn;
		if (V->insns[ptr].insn.type != VIR_TYPE_PTR || literal.op != VIR_OP_SYMADDR ||
		    CODEGEN_REFERENCE_KIND(literal.a) != CODEGEN_REFERENCE_STRING || literal.b || literal.c)
			continue;
		const cstr *str = &c->strs[CODEGEN_REFERENCE_INDEX(literal.a)];
		if (str->esz != 1 || size > str->n)
			continue;
		uint64_t bits = 0;
		for (int i = 0; i < size; i++)
			bits |= (uint64_t)(unsigned char)str->s[i] << (8 * i);
		uint32_t type = size == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32;
		uint32_t value = virbuild_constant_integer(e, type, bits);
		V->insns[id].insn = (vir_insn){ VIR_OP_LOAD, type, size == 8 ? 3 : size == 4 ? 2 : size == 2, ptr, 0, 0 };
		if (branch) {
			V->insns[trunc].insn = (vir_insn){ VIR_OP_ICMP, VIR_TYPE_I32, 1, id, value, 0 };
		} else {
			V->insns[trunc].insn = (vir_insn){ VIR_OP_NOP, VIR_TYPE_VOID, 0, 0, 0, 0 };
			V->insns[q].insn.a = id;
			V->insns[q].insn.b = value;
		}
		return;
	}
}

VPRIV void virbuild_function_simplify(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	virbuild_simplify_state S = { 0 };
	uint32_t *stk, ns = 0;
	bool has_memcmp = false;

	S.constant_start = V->insn_count;
	S.use_counts = calloc(2 * ((size_t)S.constant_start + 1), sizeof *S.use_counts);
	stk = malloc(((size_t)S.constant_start + 1) * sizeof *stk);
	{
		uint32_t kc = 16;

		for (uint32_t i = 0; i < V->insn_count; i++)
			S.constant_count += V->insns[i].block == VIRBUILD_BLOCK_NONE;
		while (kc < 2 * (V->insn_count + 64))	/* (and the constants the pass makes) */
			kc *= 2;
		S.constant_table = calloc(kc, sizeof *S.constant_table);
		S.constant_table_mask = kc - 1;
	}
	if (!S.use_counts || !stk || !S.constant_table)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	uint32_t *pointer_copies = S.use_counts + S.constant_start + 1;
	/* uses */
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t p = V->blocks[b].first_parameter; p; p = V->insns[p].next)
			if (V->forward[p] == p)
				VIRBUILD_FOR_EACH_OPERAND(V, p, o, virbuild_use_count_increment(V, &S, virbuild_value_resolve(V, *o)));
		for (uint32_t q = V->blocks[b].first_insn;; q = V->insns[q].next) {
			uint32_t x = q ? q : V->blocks[b].terminator;

			if (!x)
				break;
			if (virbuild_value_resolve(V, x) == x &&
			    !virbuild_promote_store_is_dead(V, x)) {
				VIRBUILD_FOR_EACH_OPERAND(V, x, o, virbuild_use_count_increment(V, &S, virbuild_value_resolve(V, *o)));
				if (!has_memcmp && V->insns[x].insn.op == VIR_OP_CALL) {
					vir_insn fn = V->insns[virbuild_value_resolve(V, V->insns[x].insn.a)].insn;
					if (fn.op == VIR_OP_SYMADDR && CODEGEN_REFERENCE_KIND(fn.a) == CODEGEN_REFERENCE_SYMBOL)
						has_memcmp = CODEGEN_IDENTIFIER_IS(e->compile_context->syms[CODEGEN_REFERENCE_INDEX(fn.a)].name, "memcmp");
				}
				if (V->insns[x].insn.op == VIR_OP_I2P) pointer_copies[virbuild_value_resolve(V, V->insns[x].insn.a)]++;
			}
			if (!q)
				break;
		}
	}
	/* Pointer-only views need no separately allocated integer carrier. */
	for (uint32_t q = 0; q < S.constant_start; q++) {
		vir_insn *in = &V->insns[q].insn;
		if (in->op == VIR_OP_LOAD && in->type == VIR_TYPE_I64 && !(in->flags & (VIR_INSN_FLAG_VOL | VIR_INSN_FLAG_MS)) &&
		    VIR_INSN_FLAG_WIDTH_LOG2(in->flags) == 3 && pointer_copies[q] && pointer_copies[q] == S.use_counts[q]) in->type = VIR_TYPE_PTR;
	}
	/* rewriting and numbering over the dominator tree */
	virbuild_block_dominators_compute(e, &S);
	virbuild_far_load_init(e, &S);
	{
		uint32_t cap = 16, top = 0, *bst = malloc((size_t)(V->block_count + 1) * 2 * sizeof *bst);

		while (cap < 2 * (V->insn_count - S.constant_count) + 2)
			cap *= 2;
		S.value_table = calloc(cap, sizeof *S.value_table);
		S.value_table_mask = cap - 1;
		S.table_log = malloc((2 * (size_t)V->insn_count + 2) * sizeof *S.table_log);	/* (a store or a load adds two) */
		S.end_generations = malloc(((size_t)V->block_count + 1) * sizeof *S.end_generations);
		if (!S.value_table || !S.table_log || !bst || !S.end_generations)
			virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
		bst[top++] = V->block_order[0], bst[top++] = 0;
		while (top) {
			uint32_t ph = bst[--top], b = bst[--top];

			if (ph) {	/* leaving b: its entries go */
				while (S.table_log_count > ph - 1) {
					S.table_log_count--;
					S.value_table[S.table_log[S.table_log_count]].value = 0;
				}
				continue;
			}
			bst[top++] = b, bst[top++] = S.table_log_count + 1;
			S.memory_generation = V->blocks[b].predecessor_count == 1 ? S.end_generations[V->predecessors[V->blocks[b].predecessor_first]] : ++S.block_generations;
			S.block = ++S.block_generations;
			virbuild_block_values_number(e, &S, b);
			S.end_generations[b] = S.memory_generation;
			for (uint32_t c = S.dominator_first_child[b]; c != VIRBUILD_BLOCK_NONE; c = S.dominator_next_sibling[c])
				bst[top++] = c, bst[top++] = 0;
		}
		free(bst);
	}
	virbuild_address_offsets_fold(e, &S);
	/* what nothing reads */
	for (uint32_t i = 0; i < V->block_order_count; i++)
		for (uint32_t q = V->blocks[V->block_order[i]].first_insn; q; q = V->insns[q].next)
			if (q < S.constant_start && virbuild_value_resolve(V, q) == q && !S.use_counts[q])
				stk[ns++] = q;
	while (ns) {
		uint32_t q = stk[--ns], op = V->insns[q].insn.op, eff = vir_opcode_table[op].effects;

		if (virbuild_value_resolve(V, q) != q || S.use_counts[q] || V->insns[q].block == VIRBUILD_BLOCK_NONE)
			continue;
		if (op == VIR_OP_LOAD ? (V->insns[q].insn.flags & VIR_INSN_FLAG_VOL) != 0 :
		    ((eff & ~(VIR_EFFECT_UF | VIR_EFFECT_F)) != VIR_EFFECT_P || op == VIR_OP_BPARAM || op == VIR_OP_PARAM))	/* (FP status flags are not kept: no FENV_ACCESS) */
			continue;
		VIRBUILD_FOR_EACH_OPERAND(V, q, o, {
			uint32_t x = virbuild_value_resolve(V, *o);

			virbuild_use_count_decrement(V, &S, x);
			if (x < S.constant_start && V->insns[x].block != VIRBUILD_BLOCK_NONE && !S.use_counts[x] && V->insns[x].insn.op != VIR_OP_BPARAM)
				stk[ns++] = x;
		});
		V->forward[q] = virbuild_simplify_undefined_value(e, &S, V->insns[q].insn.type);
	}
	for (uint32_t i = 0; has_memcmp && i < V->block_order_count; i++) {
		const virbuild_preir_block *block = &V->blocks[V->block_order[i]];
		for (uint32_t q = block->first_insn; q; q = V->insns[q].next)
			if (V->insns[q].insn.op == VIR_OP_ICMP && V->forward[q] == q)
				virbuild_literal_memcmp_fold(e, &S, q);
		uint32_t term = block->terminator;
		if (term && V->insns[term].insn.op == VIR_OP_BRIF)
			virbuild_literal_memcmp_fold(e, &S, term);
	}
	free(S.value_table), free(S.table_log), free(S.end_generations), free(S.constant_table);
	free(S.immediate_dominators), free(S.dominator_first_child), free(S.dominator_next_sibling);
	free(S.positions), free(S.writers), free(S.reverse_postorder_index), free(S.block_end_positions), free(S.loop_innermost), free(S.loop_start), free(S.loop_end), free(S.loop_parent), free(S.loop_is_merged);
	free(S.use_counts);
	free(stk);
}
