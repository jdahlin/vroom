/* The walker's lowering interface for memory: frame slots, loads and stores, locals. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- slots and memory ---------------- */

VPRIV void virbuild_slot(codegen_context *e, int64_t off, uint64_t size, uint32_t align, int kind)
{
	virbuild_context *V = e->vir_builder;
	uint32_t n = 0;

	(void)kind;
	if (!size)
		size = 1;
	/* the new slot owns [off, off+size): earlier owners of an overlapping range are no longer current */
	for (uint32_t i = 0; i < V->active_slot_count; i++) {
		virbuild_preir_slot *s = &V->slots[V->active_slots[i]];

		if (s->off < off + (int64_t)size && off < s->off + (int64_t)s->size)
			continue;
		V->active_slots[n++] = V->active_slots[i];
	}
	V->active_slot_count = n;
	VIRBUILD_ARRAY_GROW(V->slots, V->slot_count, V->slot_capacity, 1);
	V->slots[V->slot_count] = (virbuild_preir_slot){ off, size, align ? align : 1, 0, 0, 0, VIRBUILD_BLOCK_NONE, 0 };
	VIRBUILD_ARRAY_GROW(V->active_slots, V->active_slot_count, V->active_slot_capacity, 1);
	V->active_slots[V->active_slot_count++] = V->slot_count++;
	/* a fresh object: what the store table knew about these bytes is gone */
	for (uint32_t i = 0; i < V->store_count; i++)
		if (V->stores[i].offset < off + (int64_t)size && off < V->stores[i].offset + V->stores[i].size)
			V->stores[i--] = V->stores[--V->store_count];
}

/* the slot that owns frame offset off now, and off's offset in it */
VPRIV uint32_t virbuild_slot_find(codegen_context *e, int64_t off, uint64_t size, int64_t *rel)
{
	virbuild_context *V = e->vir_builder;

	for (uint32_t i = V->active_slot_count; i-- > 0;) {
		virbuild_preir_slot *s = &V->slots[V->active_slots[i]];

		if (s->off <= off && off < s->off + (int64_t)s->size) {
			if (off + (int64_t)size > s->off + (int64_t)s->size)
				virbuild_fail(e, VIRBUILD_FAILURE(access_across_slots));
			*rel = off - s->off;
			return V->active_slots[i];
		}
	}
	virbuild_fail(e, VIRBUILD_FAILURE(access_outside_slots));
}

VPRIV uint32_t virbuild_slot_address(codegen_context *e, uint32_t s, int64_t rel)
{
	return virbuild_constant_make(e, VIR_OP_SLOTADDR, VIR_TYPE_PTR, s, (uint32_t)rel, (uint32_t)((uint64_t)rel >> 32));
}

/*
 * A store through a pointer, a call or a barrier: what the store table knows of the
 * frame is gone.  Not only of slots whose address has been taken so far: the walk is
 * in text order, and a loop can run this code after an address taken further down
 * (x = i; *p = 1; s += x; p = &x;).  A slot whose address is never taken is promoted
 * afterwards (virbuild_slots_promote, with the whole function's uses), so its loads go anyway.
 */
VPRIV void virbuild_store_table_clear(codegen_context *e, bool exposed_only)
{
	(void)exposed_only;
	e->vir_builder->store_count = 0;
}

/* the address of l (a ptr value); *slot: the slot (VIRBUILD_BLOCK_NONE: not a frame access), *rel its offset */
static uint32_t virbuild_address_make(codegen_context *e, const codegen_lvalue *l, uint64_t size, uint32_t *slot, int64_t *rel)
{
	*slot = VIRBUILD_BLOCK_NONE;
	switch (l->kind) {
	case CODEGEN_LVALUE_FRAME:
		*slot = virbuild_slot_find(e, l->offset, size, rel);
		return virbuild_slot_address(e, *slot, *rel);
	case CODEGEN_LVALUE_SYMBOL:
		codegen_reference_note(e, l->reference);
		return virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR, l->reference, (uint32_t)l->offset, (uint32_t)((uint64_t)l->offset >> 32));
	case CODEGEN_LVALUE_REGISTER: {
		uint32_t b = virbuild_register_read(e, l->register_number, VIR_TYPE_PTR);

		if (!l->offset)
			return b;
		return virbuild_insn_append(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, b, virbuild_constant_integer(e, VIR_TYPE_I64, l->offset), 0);
	}
	default:
		virbuild_fail(e, VIRBUILD_FAILURE(unsupported_lvalue_kind));
	}
}

VPRIV uint32_t virbuild_integer_log2(uint64_t x) { uint32_t k = 0; while (x > 1) x >>= 1, k++; return k; }

VPRIV uint32_t virbuild_memory_flags_make(int size, uint32_t align, bool sgn)
{
	uint32_t w = virbuild_integer_log2((uint64_t)size), a = align ? virbuild_integer_log2(align & -align) : 0;

	if (a > 15)
		a = 15;
	return w | (sgn ? VIR_INSN_FLAG_MS : 0) | a << 4;
}

/* the variable of a local (CODEGEN_LVALUE_VARIABLE): its type */
static uint32_t virbuild_lvalue_type(codegen_context *e, const codegen_lvalue *l) { return e->vir_builder->variable_types[l->reference - VIRBUILD_LOCAL_VARIABLE_FIRST]; }

VPRIV void virbuild_load(codegen_context *e, int rd, const codegen_lvalue *l, int size, bool sgn)
{
	virbuild_context *V = e->vir_builder;
	bool f = rd >= VIRBUILD_FLOAT_REGISTER_FIRST;
	uint32_t ty, slot, a, v;
	int64_t rel;

	if (l->is_bitfield)
		virbuild_fail(e, VIRBUILD_FAILURE(bit_field_lvalue));
	if (l->kind == CODEGEN_LVALUE_VARIABLE) {
		v = virbuild_variable_read(e, l->reference, V->current_block, virbuild_lvalue_type(e, l));
		if (f)
			v = virbuild_value_convert(e, v, size == 4 ? VIR_TYPE_F32 : VIR_TYPE_F64);
		virbuild_register_write(e, rd, v);
		return;
	}
	if (f)
		ty = size == 4 ? VIR_TYPE_F32 : size == 8 ? VIR_TYPE_F64 : 0;
	else
		ty = size == 8 ? VIR_TYPE_I64 : sgn ? VIR_TYPE_I64 : VIR_TYPE_I32;
	if (!ty)
		virbuild_fail(e, VIRBUILD_FAILURE(float_load_size));
	a = virbuild_address_make(e, l, (uint64_t)size, &slot, &rel);
	if (f && size == 8 && slot != VIRBUILD_BLOCK_NONE && V->slots[slot].size == 8 && V->slots[slot].float32_written) {
		/* the word of a float temporary (a conditional expression's value): the f32 stored in it */
		size = 4, ty = VIR_TYPE_F32;
		a = virbuild_address_make(e, l, 4, &slot, &rel);
	}
	if (slot != VIRBUILD_BLOCK_NONE && !e->volatile_untag_depth) {	/* the value last stored here, in this block (not volatile) */
		for (uint32_t i = V->store_count; i-- > 0;) {
			virbuild_store_entry *s = &V->stores[i];

			if (s->slot == slot && s->offset == l->offset && s->size == size) {
				uint32_t x = s->v, xt = virbuild_value_type(e, x);

				if (!f && size < 8 && xt != VIR_TYPE_F32 && xt != VIR_TYPE_F64) {	/* the low bytes, extended as a load would */
					x = virbuild_value_convert(e, x, size == 4 && !sgn ? VIR_TYPE_I32 : VIR_TYPE_I64);
					if (size < 4 || sgn)
						x = virbuild_insn_append(e, VIR_OP_EXT, virbuild_value_type(e, x), virbuild_integer_log2((uint64_t)size) | (sgn ? VIR_INSN_FLAG_MS : 0), x, 0, 0);
				} else {
					x = virbuild_value_convert(e, x, ty);
				}
				virbuild_register_write(e, rd, x);
				return;
			}
		}
		V->slots[slot].loads++;
	}
	v = virbuild_insn_append(e, VIR_OP_LOAD, ty, virbuild_memory_flags_make(size, l->align, !f && sgn && size < 8) | (e->volatile_untag_depth ? VIR_INSN_FLAG_VOL : 0), a, 0, 0);
	if (slot != VIRBUILD_BLOCK_NONE)
		V->insns[v].slot_plus_one = slot + 1;	/* (which slot: for the dead-store pass) */
	virbuild_register_write(e, rd, v);
}

/* value v stored as size bytes: the type a store of it has */
static uint32_t virbuild_store_type(codegen_context *e, uint32_t v, int size)
{
	uint32_t t = virbuild_value_type(e, v);

	if (t == VIR_TYPE_F32 || t == VIR_TYPE_F64)
		return size == 4 ? VIR_TYPE_F32 : size == 8 ? VIR_TYPE_F64 : 0;
	if (size == 8)
		return t == VIR_TYPE_PTR ? VIR_TYPE_PTR : VIR_TYPE_I64;
	return VIR_TYPE_I32;
}

VPRIV void virbuild_store(codegen_context *e, int rs, const codegen_lvalue *l, int size)
{
	virbuild_context *V = e->vir_builder;
	uint32_t v = virbuild_register_read_raw(e, rs), ty, slot, a;
	int64_t rel;

	if (rs >= VIRBUILD_FLOAT_REGISTER_FIRST && rs < VIRBUILD_REGISTER_NONE)
		V->float_products[rs - VIRBUILD_FLOAT_REGISTER_FIRST] = 0;	/* (an assignment's value is not the product any more, as clang) */
	if (l->is_bitfield)
		virbuild_fail(e, VIRBUILD_FAILURE(bit_field_lvalue));
	if (l->kind == CODEGEN_LVALUE_VARIABLE) {
		uint32_t lt = virbuild_lvalue_type(e, l);

		if (lt == VIR_TYPE_F32 || lt == VIR_TYPE_F64)
			v = virbuild_value_convert(e, v, lt);
		else {
			v = virbuild_value_convert(e, v, size == 8 ? (lt == VIR_TYPE_PTR ? VIR_TYPE_PTR : VIR_TYPE_I64) : VIR_TYPE_I32);
			if (size < 4)	/* the local's value is always its type's (normalized) */
				v = virbuild_insn_append(e, VIR_OP_EXT, VIR_TYPE_I32, virbuild_integer_log2((uint64_t)size) | (V->variable_is_signed[l->reference - VIRBUILD_LOCAL_VARIABLE_FIRST] ? VIR_INSN_FLAG_MS : 0), v, 0, 0);
			v = virbuild_value_convert(e, v, lt);
		}
		virbuild_variable_write(e, l->reference, V->current_block, v);
		return;
	}
	{
		bool fw = size == 8 && virbuild_value_type(e, v) == VIR_TYPE_F32;

		if (fw)
			size = 4;	/* (a float's word in an argument or temporary slot: its low 4 bytes) */
		ty = virbuild_store_type(e, v, size);
		if (!ty)
			virbuild_fail(e, VIRBUILD_FAILURE(float_store_size));
		a = virbuild_address_make(e, l, (uint64_t)size, &slot, &rel);
		if (slot != VIRBUILD_BLOCK_NONE && V->slots[slot].size == 8 && (fw || size == 8))
			V->slots[slot].float32_written = fw;	/* (what a load of the word into a float name reads back) */
	}
	v = virbuild_value_convert(e, v, ty);
	{
		uint32_t s = virbuild_insn_append(e, VIR_OP_STORE, VIR_TYPE_VOID, virbuild_memory_flags_make(size, l->align, false) | (e->volatile_untag_depth ? VIR_INSN_FLAG_VOL : 0), a, v, 0);

		if (slot != VIRBUILD_BLOCK_NONE)
			V->insns[s].slot_plus_one = slot + 1;
	}
	if (slot != VIRBUILD_BLOCK_NONE) {
		/* the bytes it covers are this value now */
		for (uint32_t i = 0; i < V->store_count; i++)
			if (V->stores[i].slot == slot && V->stores[i].offset < l->offset + size && l->offset < V->stores[i].offset + V->stores[i].size)
				V->stores[i--] = V->stores[--V->store_count];
		VIRBUILD_ARRAY_GROW(V->stores, V->store_count, V->store_capacity, 1);
		V->stores[V->store_count++] = (virbuild_store_entry){ slot, l->offset, size, v };
	} else {
		virbuild_store_table_clear(e, true);	/* through a pointer: any exposed slot */
	}
}

VPRIV int virbuild_laddr(codegen_context *e, const codegen_lvalue *l)
{
	virbuild_context *V = e->vir_builder;
	uint32_t slot, a;
	int64_t rel;
	int r;

	if (l->kind == CODEGEN_LVALUE_VARIABLE || l->kind == CODEGEN_LVALUE_LOCAL_REGISTER)
		virbuild_fail(e, VIRBUILD_FAILURE(register_variable_address));
	a = virbuild_address_make(e, l, 1, &slot, &rel);
	if (slot != VIRBUILD_BLOCK_NONE) {
		V->slots[slot].exposed = 1;
		virbuild_store_table_clear(e, true);
	}
	r = l->kind == CODEGEN_LVALUE_REGISTER ? l->register_number : virbuild_treg(e);
	virbuild_register_write(e, r, a);
	return r;
}

VPRIV void virbuild_copy(codegen_context *e, int rdst, int rsrc, uint64_t n)
{
	uint32_t d = virbuild_register_read(e, rdst, VIR_TYPE_PTR), s = virbuild_register_read(e, rsrc, VIR_TYPE_PTR);

	if (n == 1 || n == 2 || n == 4 || n == 8) {	/* one load and store (memcpy(&u, &d, 8)): promotable */
		uint32_t fl = virbuild_memory_flags_make((int)n, 1, false);

		virbuild_insn_append(e, VIR_OP_STORE, VIR_TYPE_VOID, fl, d, virbuild_insn_append(e, VIR_OP_LOAD, n == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32, fl, s, 0, 0), 0);
	} else {
		virbuild_insn_append(e, VIR_OP_MEMCPY, VIR_TYPE_VOID, 0, d, s, virbuild_constant_integer(e, VIR_TYPE_I64, (int64_t)n));
	}
	virbuild_store_table_clear(e, true);
}

VPRIV void virbuild_zero(codegen_context *e, int rdst, uint64_t n)
{
	uint32_t d = virbuild_register_read(e, rdst, VIR_TYPE_PTR);

	virbuild_insn_append(e, VIR_OP_MEMSET, VIR_TYPE_VOID, 0, d, virbuild_constant_integer(e, VIR_TYPE_I32, 0), virbuild_constant_integer(e, VIR_TYPE_I64, (int64_t)n));
	virbuild_store_table_clear(e, true);
}

/* ---------------- locals ---------------- */

VPRIV void virbuild_locals_begin(codegen_context *e, uint32_t s) { (void)e, (void)s; }

static uint32_t virbuild_local_hash(uint32_t sym, uint32_t inst)
{
	return (uint32_t)(((uint64_t)sym << 32 | inst) * 0x9e3779b97f4a7c15ull >> 40);
}

/* every eligible scalar local is a variable, keyed by (symbol, inline copy) */
VPRIV bool virbuild_local(codegen_context *e, uint32_t s, codegen_lvalue *l)
{
	virbuild_context *V = e->vir_builder;
	cc *c = e->compile_context;
	ast_qualified_type t = c->syms[s].ty;
	uint32_t ty, inst = e->walk_state->instance, lj;

	if (!em_local_eligible(e, s))
		return false;
	if (codegen_type_is_pointer(c, t))
		ty = VIR_TYPE_PTR;
	else if (codegen_type_is_integer(c, t) && !codegen_type_is_wide(c, t) && codegen_type_size(c, t) - 1 < 8)
		ty = codegen_type_size(c, t) == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32;
	else if (KIND(c, t) == AST_TYPE_FLOAT)
		ty = VIR_TYPE_F32;
	else if (KIND(c, t) == AST_TYPE_DOUBLE)
		ty = VIR_TYPE_F64;
	else
		return false;
	if ((V->location_count + 1) * 2 > V->local_hash_capacity) {	/* (a function with everything inlined has thousands) */
		uint32_t nc = V->local_hash_capacity ? V->local_hash_capacity * 2 : 256;

		if (nc > V->local_hash_allocated) {
			free(V->local_hash);
			V->local_hash = malloc(nc * sizeof *V->local_hash);
			if (!V->local_hash) {
				V->local_hash_capacity = V->local_hash_allocated = 0;
				virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
			}
			V->local_hash_allocated = nc;
		}
		memset(V->local_hash, 0, nc * sizeof *V->local_hash);
		V->local_hash_capacity = nc;
		for (uint32_t i = 0; i < V->location_count; i++) {
			uint32_t j = virbuild_local_hash(V->locations[i].symbol, V->locations[i].instance) & (nc - 1);

			while (V->local_hash[j])
				j = (j + 1) & (nc - 1);
			V->local_hash[j] = i + 1;
		}
	}
	lj = virbuild_local_hash(s, inst) & (V->local_hash_capacity - 1);
	for (; V->local_hash[lj]; lj = (lj + 1) & (V->local_hash_capacity - 1)) {
		uint32_t i = V->local_hash[lj] - 1;

		if (V->locations[i].symbol == s && V->locations[i].instance == inst) {
			*l = (codegen_lvalue){ .kind = CODEGEN_LVALUE_VARIABLE, .reference = VIRBUILD_LOCAL_VARIABLE_FIRST + i, .align = codegen_symbol_alignment(c, s) };
			return true;
		}
	}
	V->local_hash[lj] = V->location_count + 1;
	VIRBUILD_ARRAY_GROW(V->locations, V->location_count, V->location_capacity, 1);
	VIRBUILD_ARRAY_GROW(V->variable_types, V->location_count, V->variable_type_capacity, 1);
	VIRBUILD_ARRAY_GROW(V->variable_is_signed, V->location_count, V->variable_is_signed_capacity, 1);
	V->locations[V->location_count] = (virbuild_location){ s, inst };
	V->variable_types[V->location_count] = (uint8_t)ty;
	V->variable_is_signed[V->location_count] = codegen_type_is_integer(c, t) && codegen_type_is_signed(c, t);
	*l = (codegen_lvalue){ .kind = CODEGEN_LVALUE_VARIABLE, .reference = VIRBUILD_LOCAL_VARIABLE_FIRST + V->location_count, .align = codegen_symbol_alignment(c, s) };
	V->location_count++;
	return true;
}

/* a local declared without an initializer: a value from an earlier pass through its scope
 * (a loop's) is not its value, so its variable or slot gets a definition here and a promoted
 * local is not carried around the loop.  (The value is 0: virbuild_undefined_value.)  In a slot, a memset
 * marked VIRBUILD_MEMSET_LIFETIME_FLAG: promotion takes it as zero stores, emission drops it. */
VPRIV void virbuild_undecl(codegen_context *e, const codegen_lvalue *l, uint64_t size)
{
	virbuild_context *V = e->vir_builder;
	uint32_t slot, a, m;
	int64_t rel;

	if (l->kind == CODEGEN_LVALUE_VARIABLE) {
		virbuild_variable_write(e, l->reference, V->current_block, virbuild_undefined_value(e, virbuild_lvalue_type(e, l)));
		return;
	}
	if (l->kind != CODEGEN_LVALUE_FRAME || !size || size > INT32_MAX)
		return;
	a = virbuild_address_make(e, l, size, &slot, &rel);
	if (slot == VIRBUILD_BLOCK_NONE)
		return;
	m = virbuild_insn_append(e, VIR_OP_MEMSET, VIR_TYPE_VOID, VIRBUILD_MEMSET_LIFETIME_FLAG, a, virbuild_constant_integer(e, VIR_TYPE_I32, 0), virbuild_constant_integer(e, VIR_TYPE_I64, (int64_t)size));
	V->insns[m].slot_plus_one = slot + 1;
	for (uint32_t i = 0; i < V->store_count; i++)	/* (what store forwarding knew of these bytes) */
		if (V->stores[i].slot == slot && V->stores[i].offset < l->offset + (int64_t)size && l->offset < V->stores[i].offset + V->stores[i].size)
			V->stores[i--] = V->stores[--V->store_count];
}
