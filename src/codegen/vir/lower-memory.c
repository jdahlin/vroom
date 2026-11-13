/* The walker's lowering interface for memory: frame slots, loads and stores, locals. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- slots and memory ---------------- */

VPRIV void virbuild_slot(ecx *e, int64_t off, uint64_t size, uint32_t align, int kind)
{
	virbuild_context *V = e->vw;
	uint32_t n = 0;

	(void)kind;
	if (!size)
		size = 1;
	/* the new slot owns [off, off+size): earlier owners of an overlapping range are no longer current */
	for (uint32_t i = 0; i < V->nact; i++) {
		virbuild_preir_slot *s = &V->sl[V->act[i]];

		if (s->off < off + (int64_t)size && off < s->off + (int64_t)s->size)
			continue;
		V->act[n++] = V->act[i];
	}
	V->nact = n;
	VIRBUILD_ARRAY_GROW(V->sl, V->nsl, V->csl, 1);
	V->sl[V->nsl] = (virbuild_preir_slot){ off, size, align ? align : 1, 0, 0, 0, VIRBUILD_BLOCK_NONE, 0 };
	VIRBUILD_ARRAY_GROW(V->act, V->nact, V->cact, 1);
	V->act[V->nact++] = V->nsl++;
	/* a fresh object: what the store table knew about these bytes is gone */
	for (uint32_t i = 0; i < V->nst; i++)
		if (V->st[i].off < off + (int64_t)size && off < V->st[i].off + V->st[i].size)
			V->st[i--] = V->st[--V->nst];
}

/* the slot that owns frame offset off now, and off's offset in it */
VPRIV uint32_t virbuild_slot_find(ecx *e, int64_t off, uint64_t size, int64_t *rel)
{
	virbuild_context *V = e->vw;

	for (uint32_t i = V->nact; i-- > 0;) {
		virbuild_preir_slot *s = &V->sl[V->act[i]];

		if (s->off <= off && off < s->off + (int64_t)s->size) {
			if (off + (int64_t)size > s->off + (int64_t)s->size)
				virbuild_fail(e, VIRBUILD_FAILURE(access_across_slots));
			*rel = off - s->off;
			return V->act[i];
		}
	}
	virbuild_fail(e, VIRBUILD_FAILURE(access_outside_slots));
}

VPRIV uint32_t virbuild_slot_address(ecx *e, uint32_t s, int64_t rel)
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
VPRIV void virbuild_store_table_clear(ecx *e, bool exposed_only)
{
	(void)exposed_only;
	e->vw->nst = 0;
}

/* the address of l (a ptr value); *slot: the slot (VIRBUILD_BLOCK_NONE: not a frame access), *rel its offset */
static uint32_t virbuild_address_make(ecx *e, const lval *l, uint64_t size, uint32_t *slot, int64_t *rel)
{
	*slot = VIRBUILD_BLOCK_NONE;
	switch (l->k) {
	case L_FRAME:
		*slot = virbuild_slot_find(e, l->off, size, rel);
		return virbuild_slot_address(e, *slot, *rel);
	case L_SYM:
		note_ref(e, l->ref);
		return virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR, l->ref, (uint32_t)l->off, (uint32_t)((uint64_t)l->off >> 32));
	case L_REG: {
		uint32_t b = virbuild_register_read(e, l->r, VIR_TYPE_PTR);

		if (!l->off)
			return b;
		return virbuild_insn_append(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, b, virbuild_constant_integer(e, VIR_TYPE_I64, l->off), 0);
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

/* the variable of a local (L_VAR): its type */
static uint32_t virbuild_lvalue_type(ecx *e, const lval *l) { return e->vw->vty[l->ref - VIRBUILD_LOCAL_VARIABLE_FIRST]; }

VPRIV void virbuild_load(ecx *e, int rd, const lval *l, int size, bool sgn)
{
	virbuild_context *V = e->vw;
	bool f = rd >= VIRBUILD_FLOAT_REGISTER_FIRST;
	uint32_t ty, slot, a, v;
	int64_t rel;

	if (l->bf)
		virbuild_fail(e, VIRBUILD_FAILURE(bit_field_lvalue));
	if (l->k == L_VAR) {
		v = virbuild_variable_read(e, l->ref, V->cur, virbuild_lvalue_type(e, l));
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
	if (f && size == 8 && slot != VIRBUILD_BLOCK_NONE && V->sl[slot].size == 8 && V->sl[slot].f32w) {
		/* the word of a float temporary (a conditional expression's value): the f32 stored in it */
		size = 4, ty = VIR_TYPE_F32;
		a = virbuild_address_make(e, l, 4, &slot, &rel);
	}
	if (slot != VIRBUILD_BLOCK_NONE && !e->novtag) {	/* the value last stored here, in this block (not volatile) */
		for (uint32_t i = V->nst; i-- > 0;) {
			virbuild_store_entry *s = &V->st[i];

			if (s->slot == slot && s->off == l->off && s->size == size) {
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
		V->sl[slot].loads++;
	}
	v = virbuild_insn_append(e, VIR_OP_LOAD, ty, virbuild_memory_flags_make(size, l->align, !f && sgn && size < 8) | (e->novtag ? VIR_INSN_FLAG_VOL : 0), a, 0, 0);
	if (slot != VIRBUILD_BLOCK_NONE)
		V->p[v].slot = slot + 1;	/* (which slot: for the dead-store pass) */
	virbuild_register_write(e, rd, v);
}

/* value v stored as size bytes: the type a store of it has */
static uint32_t virbuild_store_type(ecx *e, uint32_t v, int size)
{
	uint32_t t = virbuild_value_type(e, v);

	if (t == VIR_TYPE_F32 || t == VIR_TYPE_F64)
		return size == 4 ? VIR_TYPE_F32 : size == 8 ? VIR_TYPE_F64 : 0;
	if (size == 8)
		return t == VIR_TYPE_PTR ? VIR_TYPE_PTR : VIR_TYPE_I64;
	return VIR_TYPE_I32;
}

VPRIV void virbuild_store(ecx *e, int rs, const lval *l, int size)
{
	virbuild_context *V = e->vw;
	uint32_t v = virbuild_register_read_raw(e, rs), ty, slot, a;
	int64_t rel;

	if (rs >= VIRBUILD_FLOAT_REGISTER_FIRST && rs < VIRBUILD_REGISTER_NONE)
		V->fprod[rs - VIRBUILD_FLOAT_REGISTER_FIRST] = 0;	/* (an assignment's value is not the product any more, as clang) */
	if (l->bf)
		virbuild_fail(e, VIRBUILD_FAILURE(bit_field_lvalue));
	if (l->k == L_VAR) {
		uint32_t lt = virbuild_lvalue_type(e, l);

		if (lt == VIR_TYPE_F32 || lt == VIR_TYPE_F64)
			v = virbuild_value_convert(e, v, lt);
		else {
			v = virbuild_value_convert(e, v, size == 8 ? (lt == VIR_TYPE_PTR ? VIR_TYPE_PTR : VIR_TYPE_I64) : VIR_TYPE_I32);
			if (size < 4)	/* the local's value is always its type's (normalized) */
				v = virbuild_insn_append(e, VIR_OP_EXT, VIR_TYPE_I32, virbuild_integer_log2((uint64_t)size) | (V->vsgn[l->ref - VIRBUILD_LOCAL_VARIABLE_FIRST] ? VIR_INSN_FLAG_MS : 0), v, 0, 0);
			v = virbuild_value_convert(e, v, lt);
		}
		virbuild_variable_write(e, l->ref, V->cur, v);
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
		if (slot != VIRBUILD_BLOCK_NONE && V->sl[slot].size == 8 && (fw || size == 8))
			V->sl[slot].f32w = fw;	/* (what a load of the word into a float name reads back) */
	}
	v = virbuild_value_convert(e, v, ty);
	{
		uint32_t s = virbuild_insn_append(e, VIR_OP_STORE, VIR_TYPE_VOID, virbuild_memory_flags_make(size, l->align, false) | (e->novtag ? VIR_INSN_FLAG_VOL : 0), a, v, 0);

		if (slot != VIRBUILD_BLOCK_NONE)
			V->p[s].slot = slot + 1;
	}
	if (slot != VIRBUILD_BLOCK_NONE) {
		/* the bytes it covers are this value now */
		for (uint32_t i = 0; i < V->nst; i++)
			if (V->st[i].slot == slot && V->st[i].off < l->off + size && l->off < V->st[i].off + V->st[i].size)
				V->st[i--] = V->st[--V->nst];
		VIRBUILD_ARRAY_GROW(V->st, V->nst, V->cst, 1);
		V->st[V->nst++] = (virbuild_store_entry){ slot, l->off, size, v };
	} else {
		virbuild_store_table_clear(e, true);	/* through a pointer: any exposed slot */
	}
}

VPRIV int virbuild_laddr(ecx *e, const lval *l)
{
	virbuild_context *V = e->vw;
	uint32_t slot, a;
	int64_t rel;
	int r;

	if (l->k == L_VAR || l->k == L_VREG)
		virbuild_fail(e, VIRBUILD_FAILURE(register_variable_address));
	a = virbuild_address_make(e, l, 1, &slot, &rel);
	if (slot != VIRBUILD_BLOCK_NONE) {
		V->sl[slot].exposed = 1;
		virbuild_store_table_clear(e, true);
	}
	r = l->k == L_REG ? l->r : virbuild_treg(e);
	virbuild_register_write(e, r, a);
	return r;
}

VPRIV void virbuild_copy(ecx *e, int rdst, int rsrc, uint64_t n)
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

VPRIV void virbuild_zero(ecx *e, int rdst, uint64_t n)
{
	uint32_t d = virbuild_register_read(e, rdst, VIR_TYPE_PTR);

	virbuild_insn_append(e, VIR_OP_MEMSET, VIR_TYPE_VOID, 0, d, virbuild_constant_integer(e, VIR_TYPE_I32, 0), virbuild_constant_integer(e, VIR_TYPE_I64, (int64_t)n));
	virbuild_store_table_clear(e, true);
}

/* ---------------- locals ---------------- */

VPRIV void virbuild_locals_begin(ecx *e, uint32_t s) { (void)e, (void)s; }

static uint32_t virbuild_local_hash(uint32_t sym, uint32_t inst)
{
	return (uint32_t)(((uint64_t)sym << 32 | inst) * 0x9e3779b97f4a7c15ull >> 40);
}

/* every eligible scalar local is a variable, keyed by (symbol, inline copy) */
VPRIV bool virbuild_local(ecx *e, uint32_t s, lval *l)
{
	virbuild_context *V = e->vw;
	cc *c = e->c;
	qty t = c->syms[s].ty;
	uint32_t ty, inst = e->wk->inst, lj;

	if (!em_local_eligible(e, s))
		return false;
	if (is_ptr(c, t))
		ty = VIR_TYPE_PTR;
	else if (is_integer(c, t) && !is_wide(c, t) && tsize(c, t) - 1 < 8)
		ty = tsize(c, t) == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32;
	else if (KIND(c, t) == TY_FLOAT)
		ty = VIR_TYPE_F32;
	else if (KIND(c, t) == TY_DOUBLE)
		ty = VIR_TYPE_F64;
	else
		return false;
	if ((V->nloc + 1) * 2 > V->lhcap) {	/* (a function with everything inlined has thousands) */
		uint32_t nc = V->lhcap ? V->lhcap * 2 : 256;

		if (nc > V->lhalloc) {
			free(V->lh);
			V->lh = malloc(nc * sizeof *V->lh);
			if (!V->lh) {
				V->lhcap = V->lhalloc = 0;
				virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
			}
			V->lhalloc = nc;
		}
		memset(V->lh, 0, nc * sizeof *V->lh);
		V->lhcap = nc;
		for (uint32_t i = 0; i < V->nloc; i++) {
			uint32_t j = virbuild_local_hash(V->loc[i].sym, V->loc[i].inst) & (nc - 1);

			while (V->lh[j])
				j = (j + 1) & (nc - 1);
			V->lh[j] = i + 1;
		}
	}
	lj = virbuild_local_hash(s, inst) & (V->lhcap - 1);
	for (; V->lh[lj]; lj = (lj + 1) & (V->lhcap - 1)) {
		uint32_t i = V->lh[lj] - 1;

		if (V->loc[i].sym == s && V->loc[i].inst == inst) {
			*l = (lval){ .k = L_VAR, .ref = VIRBUILD_LOCAL_VARIABLE_FIRST + i, .align = sym_align(c, s) };
			return true;
		}
	}
	V->lh[lj] = V->nloc + 1;
	VIRBUILD_ARRAY_GROW(V->loc, V->nloc, V->cloc, 1);
	VIRBUILD_ARRAY_GROW(V->vty, V->nloc, V->cvty, 1);
	VIRBUILD_ARRAY_GROW(V->vsgn, V->nloc, V->cvsgn, 1);
	V->loc[V->nloc] = (virbuild_location){ s, inst };
	V->vty[V->nloc] = (uint8_t)ty;
	V->vsgn[V->nloc] = is_integer(c, t) && is_signed(c, t);
	*l = (lval){ .k = L_VAR, .ref = VIRBUILD_LOCAL_VARIABLE_FIRST + V->nloc, .align = sym_align(c, s) };
	V->nloc++;
	return true;
}

/* a local declared without an initializer: a value from an earlier pass through its scope
 * (a loop's) is not its value, so its variable or slot gets a definition here and a promoted
 * local is not carried around the loop.  (The value is 0: virbuild_undefined_value.)  In a slot, a memset
 * marked VIRBUILD_MEMSET_LIFETIME_FLAG: promotion takes it as zero stores, emission drops it. */
VPRIV void virbuild_undecl(ecx *e, const lval *l, uint64_t size)
{
	virbuild_context *V = e->vw;
	uint32_t slot, a, m;
	int64_t rel;

	if (l->k == L_VAR) {
		virbuild_variable_write(e, l->ref, V->cur, virbuild_undefined_value(e, virbuild_lvalue_type(e, l)));
		return;
	}
	if (l->k != L_FRAME || !size || size > INT32_MAX)
		return;
	a = virbuild_address_make(e, l, size, &slot, &rel);
	if (slot == VIRBUILD_BLOCK_NONE)
		return;
	m = virbuild_insn_append(e, VIR_OP_MEMSET, VIR_TYPE_VOID, VIRBUILD_MEMSET_LIFETIME_FLAG, a, virbuild_constant_integer(e, VIR_TYPE_I32, 0), virbuild_constant_integer(e, VIR_TYPE_I64, (int64_t)size));
	V->p[m].slot = slot + 1;
	for (uint32_t i = 0; i < V->nst; i++)	/* (what store forwarding knew of these bytes) */
		if (V->st[i].slot == slot && V->st[i].off < l->off + (int64_t)size && l->off < V->st[i].off + V->st[i].size)
			V->st[i--] = V->st[--V->nst];
}
