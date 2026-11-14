/* Frame slots to variables: a slot whose address does not escape becomes SSA values. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/* ---------------- slots to variables ---------------- */

#define VIRBUILD_PROMOTE_NO_CONVERSION UINT32_MAX

typedef struct virbuild_promote_field { uint32_t slot; int64_t relative_offset; uint32_t size, type; } virbuild_promote_field;

/* the conversion a load of type ty (flags fl) applies to the stored value of type st: VIRBUILD_PROMOTE_NO_CONVERSION
 * none, VIR_OP_EXT/P2I/BITCAST one instruction; false if it takes more */
static bool virbuild_promote_load_conversion(uint32_t st, uint32_t ty, uint32_t fl, uint32_t *op)
{
	uint32_t w = VIR_INSN_FLAG_WIDTH_LOG2(fl);

	/* A narrow load, or an i32 field read into i64, extends its stored bits. */
	if (w < 2 || (fl & VIR_INSN_FLAG_MS) || (w == 2 && st == VIR_TYPE_I32 && ty == VIR_TYPE_I64)) {
		*op = VIR_OP_EXT;
		return st == VIR_TYPE_I32 && (ty == VIR_TYPE_I32 || ty == VIR_TYPE_I64);
	}
	if (st == ty)
		*op = VIRBUILD_PROMOTE_NO_CONVERSION;
	else if (st == VIR_TYPE_PTR && ty == VIR_TYPE_I64)
		*op = VIR_OP_P2I;
	else if (st == VIR_TYPE_I64 && ty == VIR_TYPE_PTR)
		*op = VIR_OP_I2P;
	else if ((st == VIR_TYPE_I32 && ty == VIR_TYPE_F32) || (st == VIR_TYPE_F32 && ty == VIR_TYPE_I32) ||
		 (st == VIR_TYPE_I64 && ty == VIR_TYPE_F64) || (st == VIR_TYPE_F64 && ty == VIR_TYPE_I64))
		*op = VIR_OP_BITCAST;
	else
		return false;
	return true;
}

static int64_t virbuild_promote_relative_offset(const virbuild_preir_insn *a) { return (int64_t)((uint64_t)a->insn.b | (uint64_t)a->insn.c << 32); }

static void virbuild_promote_escape_mark(virbuild_context *V, uint8_t *bad, uint32_t v)
{
	const virbuild_preir_insn *q = &V->insns[virbuild_value_resolve(V, v)];

	if (q->insn.op == VIR_OP_SLOTADDR && q->block == VIRBUILD_BLOCK_NONE)
		bad[q->insn.a] = 1;
}

/* var's value on entry to b (b's own definitions come later in it) */
static uint32_t virbuild_promote_variable_entry(codegen_context *e, uint32_t var, uint32_t b, uint32_t ty, bool defs)
{
	virbuild_context *V = e->vir_builder;
	uint32_t *d, p;

	if (!defs)
		return virbuild_variable_read(e, var, b, ty);
	if (!V->blocks[b].predecessor_count)
		return virbuild_undefined_value(e, ty);
	if (V->blocks[b].predecessor_count == 1)
		return virbuild_variable_read(e, var, V->predecessors[V->blocks[b].predecessor_first], ty);
	d = virbuild_variable_slot(e, b, var | 0x80000000u, false);
	if (d)
		return virbuild_value_resolve(V, *d);
	p = virbuild_parameter_new(e, b, var, ty);
	*virbuild_variable_slot(e, b, var | 0x80000000u, true) = p;
	virbuild_parameter_operands_fill(e, p, b);
	return virbuild_value_resolve(V, p);
}

static uint32_t virbuild_promote_zero_constant(codegen_context *e, uint32_t ty)
{
	return ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64 ? virbuild_constant_make(e, VIR_OP_FCONST, ty, 0, 0, 0) : virbuild_constant_integer(e, ty, 0);
}

/* memset q, of a constant length with 0: the bytes (len) it zeroes */
static bool virbuild_promote_memset_zero_length(virbuild_context *V, uint32_t q, int64_t *len)
{
	int64_t z;

	return V->insns[q].insn.op == VIR_OP_MEMSET && virbuild_value_integer_constant(V, virbuild_value_resolve(V, V->insns[q].insn.b), &z) && !z &&
	       virbuild_value_integer_constant(V, virbuild_value_resolve(V, V->insns[q].insn.c), len) && *len > 0;
}

struct virbuild_promote_memset { uint32_t insn, slot; int64_t relative_offset, length; };	/* a memset of a slot with 0 */

static bool virbuild_promote_memset_covers(const struct virbuild_promote_memset *m, const virbuild_promote_field *f)
{
	return f->slot == m->slot && m->relative_offset <= f->relative_offset && f->relative_offset + (int64_t)f->size <= m->relative_offset + m->length;
}

/* a store the promotion made dead (a memset of a promoted slot too) */
VPRIV bool virbuild_promote_store_is_dead(const virbuild_context *V, uint32_t q)
{
	const virbuild_preir_insn *x = &V->insns[q];

	return (x->insn.op == VIR_OP_STORE || x->insn.op == VIR_OP_MEMSET) && x->slot_plus_one && V->slots[x->slot_plus_one - 1].is_promoted;
}

/* a slot whose address does not escape, read and written only as fields (offset, size) that
 * do not overlap, becomes one variable per field: its loads read the value the field holds
 * (Braun, as the walker's locals), its stores go */
VPRIV bool virbuild_slots_promote(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	virbuild_promote_field *fd = NULL;
	uint32_t nf = 0, cf = 0, *fof, *cur, *cst, np0 = V->insn_count, var0;
	uint32_t *sf, *fnx = NULL, cfnx = 0;	/* a slot's fields: first + 1 per slot, next + 1 per field */
	uint8_t *bad;
	bool done = false;
	int64_t rel;
	struct virbuild_promote_memset *ms = NULL;	/* the memsets of slots with 0 */
	uint32_t nms = 0, cms = 0;

	if (!V->slot_count)
		return false;
	bad = calloc(V->slot_count, 1);
	sf = calloc(V->slot_count, sizeof *sf);
	fof = malloc((size_t)V->insn_count * sizeof *fof);	/* access -> field + 1 */
	if (!bad || !fof || !sf)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t s = 0; s < V->slot_count; s++)
		bad[s] = V->slots[s].is_promoted;	/* (an earlier run's: its stores are dead, its loads gone) */
	/* a slot escapes when its address is anything but the address of a load or store (the
	 * walker's exposed flag is set by any address taken, *(T *)&x too) */
	for (uint32_t i = 0; i < np0; i++) {
		const virbuild_preir_insn *q = &V->insns[i];
		const vir_opcode_info *oi = &vir_opcode_table[q->insn.op];

		if (q->block == VIRBUILD_BLOCK_NONE || virbuild_value_resolve(V, i) != i || virbuild_promote_store_is_dead(V, i))
			continue;	/* (a store to a slot promoted before is dead) */
		if (q->insn.op == VIR_OP_BPARAM) {
			if (q->insn.b != VIRBUILD_BLOCK_NONE)
				for (uint32_t j = 0; j < V->blocks[q->block].predecessor_count; j++)
					virbuild_promote_escape_mark(V, bad, V->extra[q->insn.b + j]);
			continue;
		}
		for (int k = 0; k < 3; k++)
			if (virbuild_operand_kind_is_value(oi->operand_kinds[k]) && !(q->insn.op == VIR_OP_BRIF && k > 0) &&
			    !(k == 0 && (q->insn.op == VIR_OP_LOAD || q->insn.op == VIR_OP_STORE || virbuild_promote_memset_zero_length(V, i, &rel))))
				virbuild_promote_escape_mark(V, bad, (&q->insn.a)[k]);
		if (q->insn.op == VIR_OP_CALL || q->insn.op == VIR_OP_RET)
			for (uint32_t k = 0; k < V->extra[q->insn.c]; k++)
				virbuild_promote_escape_mark(V, bad, V->extra[q->insn.c + 1 + k]);
	}
	/* the fields, and what rules a slot out */
	for (uint32_t i = 0; i < np0; i++) {
		virbuild_preir_insn *q = &V->insns[i];
		uint32_t s, size, ty, k, a;
		int64_t len;

		fof[i] = 0;
		if ((q->insn.op != VIR_OP_LOAD && q->insn.op != VIR_OP_STORE && q->insn.op != VIR_OP_MEMSET) ||
		    q->block == VIRBUILD_BLOCK_NONE || virbuild_value_resolve(V, i) != i)
			continue;
		a = virbuild_value_resolve(V, q->insn.a);
		if (V->insns[a].insn.op != VIR_OP_SLOTADDR)
			continue;
		s = V->insns[a].insn.a;
		q->insn.a = a;
		q->slot_plus_one = s + 1;	/* (also an access through the address taken) */
		if (bad[s])
			continue;
		if (q->insn.op == VIR_OP_MEMSET) {	/* zeroes the fields it covers: checked once all are known */
			if (!virbuild_promote_memset_zero_length(V, i, &len)) {
				bad[s] = 1;
				continue;
			}
			VIRBUILD_ARRAY_GROW(ms, nms, cms, 1);
			ms[nms] = (struct virbuild_promote_memset){ i, s, virbuild_promote_relative_offset(&V->insns[a]), len };
			fof[i] = UINT32_MAX - nms++;	/* (a memset: its index in ms) */
			continue;
		}
		if (q->insn.flags & VIR_INSN_FLAG_VOL) {
			bad[s] = 1;
			continue;
		}
		rel = virbuild_promote_relative_offset(&V->insns[a]);
		size = 1u << VIR_INSN_FLAG_WIDTH_LOG2(q->insn.flags);
		ty = q->insn.op == VIR_OP_STORE ? V->insns[q->insn.b].insn.type : VIR_TYPE_VOID;
		/* Simplification can remove a truncation before a narrow store.
		 * Its field still holds only the stored low bits, even when another
		 * store uses an i32 carrier. Normalize before comparing field types. */
		if (size <= 4 && ty == VIR_TYPE_I64)
			ty = VIR_TYPE_I32;
		for (k = sf[s] - 1; k != UINT32_MAX; k = fnx[k] - 1) {	/* (the slot's own fields only) */
			virbuild_promote_field *f = &fd[k];

			if (f->relative_offset == rel && f->size == size)
				break;
			if (f->relative_offset < rel + (int64_t)size && rel < f->relative_offset + (int64_t)f->size)
				bad[s] = 1;	/* overlapping fields */
		}
		if (bad[s])
			continue;
		if (k == UINT32_MAX) {
			k = nf;
			VIRBUILD_ARRAY_GROW(fd, nf, cf, 1);
			VIRBUILD_ARRAY_GROW(fnx, nf, cfnx, 1);
			fnx[nf] = sf[s];
			sf[s] = nf + 1;
			fd[nf++] = (virbuild_promote_field){ s, rel, size, VIR_TYPE_VOID };
		}
		if (ty != VIR_TYPE_VOID) {
			if (fd[k].type != VIR_TYPE_VOID && fd[k].type != ty) {
				if ((fd[k].type == VIR_TYPE_PTR || fd[k].type == VIR_TYPE_I64) && (ty == VIR_TYPE_PTR || ty == VIR_TYPE_I64))
					ty = VIR_TYPE_I64;	/* a pointer and its integer (p++ as p2i, add, i2p) */
				else
					bad[s] = 1;	/* stores of different types */
			}
			fd[k].type = ty;
		}
		fof[i] = k + 1;
	}
	/* a memset covers each field of its slot whole or not at all */
	for (uint32_t m = 0; m < nms; m++)
		for (uint32_t k = sf[ms[m].slot] - 1; k != UINT32_MAX; k = fnx[k] - 1) {
			const virbuild_promote_field *f = &fd[k];
			int64_t lo = ms[m].relative_offset, hi = ms[m].relative_offset + ms[m].length;

			if (f->slot == ms[m].slot && f->relative_offset < hi && lo < f->relative_offset + (int64_t)f->size &&
			    !(lo <= f->relative_offset && f->relative_offset + (int64_t)f->size <= hi))
				bad[f->slot] = 1;
		}
	/* every load must take the field's stored type with one conversion at most */
	for (uint32_t i = 0; i < V->insn_count; i++) {
		virbuild_preir_insn *q = &V->insns[i];
		uint32_t op;

		if (!fof[i] || q->insn.op != VIR_OP_LOAD || bad[q->slot_plus_one - 1])
			continue;
		virbuild_promote_field *f = &fd[fof[i] - 1];

		if (f->type == VIR_TYPE_VOID)	/* never stored: as the first load reads it */
			f->type = VIR_INSN_FLAG_WIDTH_LOG2(q->insn.flags) < 2 || (q->insn.flags & VIR_INSN_FLAG_MS) ? VIR_TYPE_I32 : q->insn.type;
		if (!virbuild_promote_load_conversion(f->type, q->insn.type, q->insn.flags, &op))
			bad[q->slot_plus_one - 1] = 1;
	}
	{
		uint32_t any = 0;

		for (uint32_t k = 0; k < nf; k++)
			any += !bad[fd[k].slot];
		if (!any)
			goto out;
	}
	/* Normalize each promoted store to its field's carrier: pointer/integer
	 * fields use p2i, narrow integer fields truncate an i64 source. Place
	 * the conversion after the store, which promotion makes dead. */
	for (uint32_t i = 0; i < np0; i++) {
		uint32_t p, b;

		if (V->insns[i].insn.op != VIR_OP_STORE || !fof[i] || bad[V->insns[i].slot_plus_one - 1] || virbuild_value_resolve(V, i) != i)
			continue;
		uint32_t to = fd[fof[i] - 1].type, from = V->insns[V->insns[i].insn.b].insn.type, op;
		if (to == VIR_TYPE_I64 && from == VIR_TYPE_PTR)
			op = VIR_OP_P2I;
		else if (to == VIR_TYPE_I32 && from == VIR_TYPE_I64)
			op = VIR_OP_TRUNC;
		else
			continue;
		b = V->insns[i].block;
		p = virbuild_preir_insn_add(e, op, to, 0, V->insns[i].insn.b, 0, 0, b);
		V->insns[p].next = V->insns[i].next;
		V->insns[i].next = p;
		if (V->blocks[b].last_insn == i)
			V->blocks[b].last_insn = p;
		V->insns[i].insn.b = p;
	}
	/* the variables: VIRBUILD_LOCAL_VARIABLE_FIRST + nloc + earlier runs' + field */
	var0 = V->location_count + V->promoted_count;
	VIRBUILD_ARRAY_GROW(V->variable_types, var0, V->variable_type_capacity, nf);
	for (uint32_t k = 0; k < nf; k++)
		V->variable_types[var0 + k] = (uint8_t)fd[k].type;
	V->promoted_count += nf;
	cur = malloc((size_t)nf * 2 * sizeof *cur);
	if (!cur)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	cst = cur + nf;
	/* each block's last store to a field is the field's value at its end */
	for (uint32_t oi = 0; oi < V->block_order_count; oi++) {
		uint32_t b = V->block_order[oi];

		for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
			if (q >= np0 || !fof[q] || bad[V->insns[q].slot_plus_one - 1] || V->insns[q].insn.op == VIR_OP_LOAD || virbuild_value_resolve(V, q) != q)
				continue;
			if (V->insns[q].insn.op == VIR_OP_STORE) {
				uint32_t var = VIRBUILD_LOCAL_VARIABLE_FIRST + var0 + fof[q] - 1;

				virbuild_variable_write(e, var, b, V->insns[q].insn.b);
				*virbuild_variable_slot(e, b, var | 0x40000000u, true) = 1;	/* b defines var */
				continue;
			}
			for (uint32_t k = sf[V->insns[q].slot_plus_one - 1] - 1; k != UINT32_MAX; k = fnx[k] - 1) {
				uint32_t var = VIRBUILD_LOCAL_VARIABLE_FIRST + var0 + k;

				if (!virbuild_promote_memset_covers(&ms[UINT32_MAX - fof[q]], &fd[k]))
					continue;
				virbuild_variable_write(e, var, b, virbuild_promote_zero_constant(e, fd[k].type));
				*virbuild_variable_slot(e, b, var | 0x40000000u, true) = 1;
			}
		}
	}
	for (uint32_t k = 0; k < nf; k++)
		cst[k] = UINT32_MAX;
	for (uint32_t oi = 0; oi < V->block_order_count; oi++) {
		uint32_t b = V->block_order[oi];

		for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
			virbuild_preir_insn *x = &V->insns[q];
			uint32_t k, var, v, op;

			if (q >= np0 || !fof[q] || bad[x->slot_plus_one - 1] || virbuild_value_resolve(V, q) != q)
				continue;
			if (x->insn.op == VIR_OP_MEMSET) {
				const struct virbuild_promote_memset *m = &ms[UINT32_MAX - fof[q]];

				for (k = sf[m->slot] - 1; k != UINT32_MAX; k = fnx[k] - 1)
					if (virbuild_promote_memset_covers(m, &fd[k])) {
						cur[k] = virbuild_promote_zero_constant(e, fd[k].type);
						cst[k] = oi;
					}
				continue;
			}
			k = fof[q] - 1;
			var = VIRBUILD_LOCAL_VARIABLE_FIRST + var0 + k;
			if (x->insn.op == VIR_OP_STORE) {
				cur[k] = x->insn.b;
				cst[k] = oi;
				continue;
			}
			if (cst[k] == oi) {
				v = cur[k];
			} else {
				v = virbuild_promote_variable_entry(e, var, b, fd[k].type, virbuild_variable_slot(e, b, var | 0x40000000u, false) != NULL);
				x = &V->insns[q];	/* (virbuild_promote_variable_entry can grow V->p) */
			}
			virbuild_promote_load_conversion(fd[k].type, x->insn.type, x->insn.flags, &op);
			if (op == VIRBUILD_PROMOTE_NO_CONVERSION) {
				V->forward[q] = v;
			} else {
				x->insn.op = op;
				x->insn.flags = op == VIR_OP_EXT ? (x->insn.flags & (3 | VIR_INSN_FLAG_MS)) : 0;
				x->insn.a = v;
				x->insn.b = x->insn.c = 0;
				x->slot_plus_one = 0;
			}
		}
	}
	free(cur);
	for (uint32_t k = 0; k < nf; k++)
		if (!bad[fd[k].slot])
			V->slots[fd[k].slot].is_promoted = 1, done = true;
out:
	free(ms);
	free(fd);
	free(fnx);
	free(sf);
	free(fof);
	free(bad);
	return done;
}
