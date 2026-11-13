/* vcc arm64 code generator: frame slots, addresses and typed loads and stores that VIR's lowering shares with the walker */
#include "../../../codegen/codegen-internal.h"

/* a stack slot: returns its offset (A64_REGISTER_FRAME base) */
VPRIV int64_t a64_frame_slot_allocate(codegen_context *e, uint64_t size, uint32_t align)
{
	if (align < 1)
		align = 1;
	if (align > 16) {	/* x28: the base of the realigned frame if it turns out dynamic */
		if (e->local_register_candidate_count)
			_longjmp(e->bail_jump, 2);	/* it holds a register variable (they start at x28): again without */
		e->temporaries_reserved |= 1u << codegen_temporary_index(A64_REGISTER_FRAME_BASE);
	}
	if (align > e->frame_alignment)
		e->frame_alignment = align;	/* over 16: finish_function realigns sp to it */
	/* Keep the allocation and its alignment rounding within signed offsets. */
	uint64_t limit = (uint64_t)INT64_MAX - align;
	if ((uint64_t)e->frame_offset > limit || size > limit - (uint64_t)e->frame_offset)
		codegen_fatal_report(e, 0, "stack object too large");
	/* allocations past the current top ended with their scope */
	while (e->live_slot_count && e->slot_allocations[e->live_slots[e->live_slot_count - 1]].offset < -e->frame_offset)
		e->live_slot_count--;
	e->frame_offset = (int64_t)(((uint64_t)e->frame_offset + size + align - 1) & ~(uint64_t)(align - 1));
	if (e->frame_offset > e->frame_maximum)
		e->frame_maximum = e->frame_offset;
	if (e->slot_allocation_count < CODEGEN_SLOT_ALLOCATION_CAPACITY) {
		e->slot_allocations[e->slot_allocation_count] = (codegen_slot_allocation){ -e->frame_offset, size };
		e->live_slots[e->live_slot_count++] = e->slot_allocation_count;
	}
	e->slot_allocation_count++;
	return -e->frame_offset;
}

/* tag record i (frame offset off, len bytes; -1: an address) with its live allocation */
VPRIV void a64_record_slot_tag(codegen_context *e, a64_record *i, int64_t off, int64_t len)
{
	uint32_t lo = 0, hi = e->live_slot_count;

	if (e->unit->options->opt < 1 || e->volatile_untag_depth)
		return;
	/* live allocations are disjoint, offsets decreasing with depth */
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (e->slot_allocations[e->live_slots[mid]].offset > off)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < e->live_slot_count) {
		const codegen_slot_allocation *a = &e->slot_allocations[e->live_slots[lo]];
		uint32_t id = e->live_slots[lo] + 1;

		if (off >= a->offset && (len < 0 ? off <= a->offset + (int64_t)a->size : off + len <= a->offset + (int64_t)a->size) &&
		    id <= 0xffff)
			i->rm = (uint8_t)id, i->ra = (uint8_t)(id >> 8);
		/* an address at the start of one allocation is also just past the end of the one below */
		if (len < 0 && off == a->offset && lo + 1 < e->live_slot_count) {
			const codegen_slot_allocation *b = &e->slot_allocations[e->live_slots[lo + 1]];
			uint32_t id2 = e->live_slots[lo + 1] + 1;

			if (b->offset + (int64_t)b->size == off) {
				if (id2 > 0xffff)
					i->rm = i->ra = 0;	/* untagged */
				else
					i->condition = (uint8_t)id2, i->shift = (uint8_t)(id2 >> 8);
			}
		}
	}
}

/* address of a symbol into rd */
VPRIV void a64_reference_address_emit(codegen_context *e, int rd, uint32_t ref, int64_t off)
{
	cc *c = e->compile_context;

	codegen_reference_note(e, ref);
	if (codegen_symbol_needs_got(c, ref)) {
		a64_record_emit_symbol(e, A64_OP_LDGOT, 1, 0, rd, 0, ref, 0);
		if (off)
			a64_record_emit_immediate(e, A64_OP_ADDI, 1, rd, rd, off);
		return;
	}
	if (!CODEGEN_SYMBOL_OFFSET_FITS(c, off)) {
		a64_record_emit_symbol(e, A64_OP_ADRP, 1, 0, rd, 0, ref, 0);
		a64_record_emit_symbol(e, A64_OP_ADDLO, 1, 0, rd, rd, ref, 0);
		a64_record_emit_immediate(e, A64_OP_ADDI, 1, rd, rd, (int64_t)off);
		return;
	}
	a64_record_emit_symbol(e, A64_OP_ADRP, 1, 0, rd, 0, ref, off);
	a64_record_emit_symbol(e, A64_OP_ADDLO, 1, 0, rd, rd, ref, off);
}

/* base register + offset for a memory access; may load a symbol page into x16 */
VPRIV int a64_lvalue_base_register(codegen_context *e, const codegen_lvalue *l, int64_t *off, int size)
{
	switch (l->kind) {
	case CODEGEN_LVALUE_FRAME:
		*off = l->offset;
		return A64_REGISTER_FRAME;
	case CODEGEN_LVALUE_REGISTER:
		*off = l->offset;
		return l->register_number;
	case CODEGEN_LVALUE_LOCAL_REGISTER:
		codegen_fatal_report(e, e->current_location, "internal: memory access to a register variable");
	default:
		a64_reference_address_emit(e, R_X16, l->reference, l->offset);
		*off = 0;
		(void)size;
		return R_X16;
	}
}

VPRIV int a64_size_log2(uint64_t n) { return n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : 3; }

/* the alignment the object a reference names is known to have */
static uint32_t a64_reference_alignment(codegen_context *e, const codegen_lvalue *l)
{
	cc *c = e->compile_context;
	uint32_t a;

	switch (CODEGEN_REFERENCE_KIND(l->reference)) {
	case CODEGEN_REFERENCE_SYMBOL: a = codegen_symbol_alignment(c, codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(l->reference))); break;
	case CODEGEN_REFERENCE_STRING: a = c->strs[CODEGEN_REFERENCE_INDEX(l->reference)].esz; break;
	case CODEGEN_REFERENCE_LABEL: a = 1; break;
	default: return l->align;
	}
	return a < l->align ? a : l->align;
}

/* load a scalar of size n (1,2,4,8) into rd; ext: 0 zero, 1 sign to x */
VPRIV void a64_lvalue_load(codegen_context *e, int rd, const codegen_lvalue *l, int n, int ext)
{
	int64_t off;
	int sz = a64_size_log2((uint64_t)n);

	if (l->kind == CODEGEN_LVALUE_SYMBOL && !codegen_symbol_needs_got(e->compile_context, l->reference) && a64_reference_alignment(e, l) >= (uint32_t)n && (l->offset & (n - 1)) == 0 &&
	    CODEGEN_SYMBOL_OFFSET_FITS(e->compile_context, l->offset)) {
		codegen_reference_note(e, l->reference);
		a64_record_emit_symbol(e, A64_OP_ADRP, 1, 0, R_X16, 0, l->reference, l->offset);
		a64_record_emit_symbol(e, A64_OP_LDLO, sz, ext, rd, R_X16, l->reference, l->offset);
		return;
	}
	{
		int b = a64_lvalue_base_register(e, l, &off, n);

		a64_record_emit_memory(e, A64_OP_LD, sz, ext, rd, b, off);
	}
}

VPRIV void a64_lvalue_store(codegen_context *e, int rs, const codegen_lvalue *l, int64_t extra, int n)
{
	int64_t off;
	int sz = a64_size_log2((uint64_t)n);

	if (l->kind == CODEGEN_LVALUE_SYMBOL && !codegen_symbol_needs_got(e->compile_context, l->reference) && a64_reference_alignment(e, l) >= (uint32_t)n && ((l->offset + extra) & (n - 1)) == 0 &&
	    CODEGEN_SYMBOL_OFFSET_FITS(e->compile_context, l->offset + extra)) {
		codegen_reference_note(e, l->reference);
		a64_record_emit_symbol(e, A64_OP_ADRP, 1, 0, R_X16, 0, l->reference, l->offset + extra);
		a64_record_emit_symbol(e, A64_OP_STLO, sz, 0, rs, R_X16, l->reference, l->offset + extra);
		return;
	}
	{
		int b = a64_lvalue_base_register(e, l, &off, n);

		a64_record_emit_memory(e, A64_OP_ST, sz, 0, rs, b, off + extra);
	}
}
